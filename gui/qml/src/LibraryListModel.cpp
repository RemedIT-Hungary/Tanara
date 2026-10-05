#include "LibraryListModel.h"

#include "AppContext.h"
#include "LibraryDemoData.h"
#include "ShellFormat.h"

#include "tanara/AppController.h"
#include "tanara/library/TextFold.h"
#include "tanara/tags/TagService.h"

#include <algorithm>

namespace tanara_qml {

using tanara::LibraryEntry;
using tanara::StepState;

namespace {

// A találat előtti rész rövidítése: a keskeny oldalsávban a találatnak a sor elején kell
// látszania („…a demó előtt még egyszer…”), ezért legfeljebb ~16 karakter marad előtte.
QString clipBefore(const QString& before)
{
    const int keep = 16;
    if (before.size() <= keep)
        return before;
    int cut = before.indexOf(QLatin1Char(' '), before.size() - keep);
    if (cut < 0 || cut >= before.size() - 1)
        cut = before.size() - keep;
    return QStringLiteral("…") + before.mid(cut + 1);
}

struct Parts { QString before, match, after; };

Parts split(const QString& text, const tanara::textfold::Range& r, bool clip)
{
    if (!r.isValid() || r.start + r.length > text.size())
        return {QString(), QString(), text};
    const QString before = text.left(r.start);
    return {clip ? clipBefore(before) : before, text.mid(r.start, r.length),
            text.mid(r.start + r.length)};
}

QString summaryKey(const LibraryEntry& e)
{
    if (e.state.summaryState == StepState::Done && e.state.summaryStale)
        return QStringLiteral("stale");
    return fmt::stepStateKey(e.state.summaryState);
}

bool sameEntry(const LibraryEntry& a, const LibraryEntry& b)
{
    return a.id == b.id && a.title == b.title && a.startedAt == b.startedAt
        && a.durationMs == b.durationMs && a.section == b.section
        && a.state.transcriptState == b.state.transcriptState
        && a.state.summaryState == b.state.summaryState
        && a.state.identifyState == b.state.identifyState
        && a.state.summaryStale == b.state.summaryStale
        && a.titleMatch.start == b.titleMatch.start && a.titleMatch.length == b.titleMatch.length
        && a.snippet == b.snippet && a.snippetMatch.start == b.snippetMatch.start
        && a.snippetMatch.length == b.snippetMatch.length && a.snippetMs == b.snippetMs
        && a.tagIds == b.tagIds && a.tagNames == b.tagNames && a.tagMatch == b.tagMatch
        && a.tagMatchName == b.tagMatchName;
}

QVariantMap tagOption(const tanara::TagUsage& u)
{
    return {{QStringLiteral("id"), u.tag.id},
            {QStringLiteral("name"), u.tag.name},
            {QStringLiteral("count"), u.meetingCount}};
}

} // namespace

LibraryListModel::LibraryListModel(QObject* parent) : QAbstractListModel(parent)
{
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, &LibraryListModel::refreshNow);
    m_dayTimer.setInterval(30 * 60 * 1000);
    connect(&m_dayTimer, &QTimer::timeout, this, [this] { scheduleRefresh(); });
    m_dayTimer.start();

    AppContext* ctx = AppContext::instance();
    m_controller = ctx->controller();
    connect(ctx, &AppContext::controllerChanged, this, [this, ctx] {
        if (m_controllerInjected) return;
        m_controller = ctx->controller();
        attach();
    });
    connect(ctx, &AppContext::demoChanged, this, [this] { refreshNow(); emit tagOptionsChanged(); });
    // Demóban a tömeges címkézés a mintakönyvtár címkéit módosítja.
    connect(demo::tagNotifier(), &demo::DemoTagNotifier::changed, this, [this] {
        if (library()) return;
        refreshNow();
        emit tagOptionsChanged();
        emit tagFilterItemsChanged();
    });
    attach();
}

void LibraryListModel::setController(tanara::AppController* controller)
{
    m_controllerInjected = true;
    m_controller = controller;
    attach();
}

tanara::MeetingLibrary* LibraryListModel::library() const
{
    return m_controller ? m_controller->library() : nullptr;
}

void LibraryListModel::attach()
{
    tanara::MeetingLibrary* lib = library();
    if (lib != m_library) {
        if (m_library)
            m_library->disconnect(this);
        m_library = lib;
        if (lib) {
            connect(lib, &tanara::MeetingLibrary::meetingAdded, this, [this](const QString&) {
                scheduleRefresh();
                emit peopleOptionsChanged();
                emit tagOptionsChanged();
            });
            connect(lib, &tanara::MeetingLibrary::meetingRemoved, this, [this](const QString&) {
                scheduleRefresh();
                emit peopleOptionsChanged();
                emit tagOptionsChanged();
            });
            connect(lib, &tanara::MeetingLibrary::meetingChanged, this,
                    &LibraryListModel::onMeetingChanged);
            connect(lib, &tanara::MeetingLibrary::reset, this, [this] {
                scheduleRefresh();
                emit peopleOptionsChanged();
                emit tagOptionsChanged();
            });
            // A készlet változott (átnevezés, törlés, összevonás): sorok, chipek, popover.
            connect(lib, &tanara::MeetingLibrary::tagsChanged, this, [this] {
                scheduleRefresh();
                emit tagOptionsChanged();
                emit tagFilterItemsChanged();
            });
            // Az előtöltés után a szöveges keresés találatai teljesek.
            connect(lib, &tanara::MeetingLibrary::warmedUp, this, [this] {
                if (!m_query.text.trimmed().isEmpty()) scheduleRefresh();
            });
        }
    }
    refreshNow();
    emit peopleOptionsChanged();
    emit tagOptionsChanged();
    emit tagFilterItemsChanged();
}

int LibraryListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QHash<int, QByteArray> LibraryListModel::roleNames() const
{
    return {
        {MeetingIdRole, "meetingId"},
        {TitleRole, "title"},
        {MetaRole, "meta"},
        {SectionRole, "section"},
        {TranscriptStateRole, "transcriptState"},
        {SummaryStateRole, "summaryState"},
        {IdentifyStateRole, "identifyState"},
        {TranscriptTipRole, "transcriptTip"},
        {SummaryTipRole, "summaryTip"},
        {IdentifyTipRole, "identifyTip"},
        {TitleBeforeRole, "titleBefore"},
        {TitleMatchRole, "titleMatch"},
        {TitleAfterRole, "titleAfter"},
        {SnippetBeforeRole, "snippetBefore"},
        {SnippetMatchRole, "snippetMatch"},
        {SnippetAfterRole, "snippetAfter"},
        {SnippetMsRole, "snippetMs"},
        {HasSnippetRole, "hasSnippet"},
        {TagIdsRole, "tagIds"},
        {TagNamesRole, "tagNames"},
        {TagLineRole, "tagLine"},
        {TagMatchRole, "tagMatch"},
        {TagMatchNameRole, "tagMatchName"},
    };
}

QVariant LibraryListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const LibraryEntry& e = m_entries.at(index.row());
    switch (role) {
    case MeetingIdRole: return e.id;
    case Qt::DisplayRole:
    case TitleRole: return e.title;
    case MetaRole: {
        const QString date = fmt::shortDate(e.startedAt);
        if (e.durationMs <= 0) return date;
        return date.isEmpty() ? fmt::shortDuration(e.durationMs)
                              : date + QStringLiteral(" · ") + fmt::shortDuration(e.durationMs);
    }
    case SectionRole:
        return filtered() ? QString() : tanara::MeetingLibrary::sectionTitle(e.section);
    case TranscriptStateRole: return fmt::stepStateKey(e.state.transcriptState);
    case SummaryStateRole: return summaryKey(e);
    case IdentifyStateRole: return fmt::stepStateKey(e.state.identifyState);
    case TranscriptTipRole:
        switch (e.state.transcriptState) {
        case StepState::Done: return tr("Átirat: kész");
        case StepState::Running: return tr("Átirat: készül…");
        case StepState::Failed: return tr("Átirat: nem sikerült");
        case StepState::None: break;
        }
        return tr("Átirat: még nincs");
    case SummaryTipRole:
        switch (e.state.summaryState) {
        case StepState::Done:
            return e.state.summaryStale ? tr("Összefoglaló: elavult (a beszélők változtak)")
                                        : tr("Összefoglaló: kész");
        case StepState::Running: return tr("Összefoglaló: készül…");
        case StepState::Failed: return tr("Összefoglaló: nem sikerült");
        case StepState::None: break;
        }
        return tr("Összefoglaló: még nincs");
    case IdentifyTipRole:
        switch (e.state.identifyState) {
        case StepState::Done: return tr("Résztvevők: azonosítva");
        case StepState::Running: return tr("Résztvevők: azonosítás folyamatban…");
        default: break;
        }
        return tr("Résztvevők: nincs azonosítva");
    case TitleBeforeRole: return split(e.title, e.titleMatch, false).before;
    case TitleMatchRole: return split(e.title, e.titleMatch, false).match;
    case TitleAfterRole: return split(e.title, e.titleMatch, false).after;
    case SnippetBeforeRole: return split(e.snippet, e.snippetMatch, true).before;
    case SnippetMatchRole: return split(e.snippet, e.snippetMatch, true).match;
    case SnippetAfterRole: return split(e.snippet, e.snippetMatch, true).after;
    case SnippetMsRole: return e.snippetMs;
    case HasSnippetRole: return !e.snippet.isEmpty();
    case TagIdsRole: return e.tagIds;
    case TagNamesRole: return e.tagNames;
    case TagLineRole: return tagLine(e.tagNames);
    case TagMatchRole: return e.tagMatch;
    case TagMatchNameRole: return e.tagMatchName;
    }
    return {};
}

void LibraryListModel::setSearchText(const QString& text)
{
    if (text == m_query.text) return;
    const bool wasFiltered = filtered();
    m_query.text = text;
    emit queryChanged();
    // Gépelés közben nem keresünk minden leütésre; a szűrés be-/kikapcsolása azonnali.
    scheduleRefresh(wasFiltered == filtered() ? 140 : 0);
}

void LibraryListModel::setNoTranscript(bool on)
{
    if (on == m_query.noTranscript) return;
    m_query.noTranscript = on;
    emit queryChanged();
    refreshNow();
}

void LibraryListModel::setNoSummary(bool on)
{
    if (on == m_query.noSummary) return;
    m_query.noSummary = on;
    emit queryChanged();
    refreshNow();
}

void LibraryListModel::setPeople(const QStringList& people)
{
    if (people == m_query.people) return;
    m_query.people = people;
    emit queryChanged();
    refreshNow();
}

void LibraryListModel::addPerson(const QString& name)
{
    if (name.isEmpty() || m_query.people.contains(name)) return;
    QStringList p = m_query.people;
    p << name;
    setPeople(p);
}

void LibraryListModel::removePerson(const QString& name)
{
    QStringList p = m_query.people;
    if (p.removeAll(name) > 0)
        setPeople(p);
}

void LibraryListModel::setTags(const QStringList& tagIds)
{
    QStringList ids;
    for (const QString& id : tagIds)
        if (!id.isEmpty() && !ids.contains(id)) ids << id;
    if (ids == m_query.tags) return;
    m_query.tags = ids;
    emit queryChanged();
    emit tagFilterItemsChanged();
    refreshNow();
}

void LibraryListModel::setTagsAll(bool all)
{
    if (all == m_query.tagsAll) return;
    m_query.tagsAll = all;
    emit queryChanged();
    refreshNow();
}

void LibraryListModel::setUntagged(bool on)
{
    if (on == m_query.untagged) return;
    m_query.untagged = on;
    emit queryChanged();
    refreshNow();
}

void LibraryListModel::addTag(const QString& tagId)
{
    if (tagId.isEmpty() || m_query.tags.contains(tagId)) return;
    setTags(m_query.tags + QStringList{tagId});
}

void LibraryListModel::removeTag(const QString& tagId)
{
    QStringList t = m_query.tags;
    if (t.removeAll(tagId) > 0)
        setTags(t);
}

void LibraryListModel::toggleTag(const QString& tagId)
{
    if (m_query.tags.contains(tagId))
        removeTag(tagId);
    else
        addTag(tagId);
}

QString LibraryListModel::tagName(const QString& tagId) const
{
    if (m_controller && m_controller->tags())
        return m_controller->tags()->tag(tagId).name;
    if (AppContext::instance()->demo())
        return demo::tagName(tagId);
    return {};
}

QVariantList LibraryListModel::tagFilterItems() const
{
    QVariantList out;
    for (const QString& id : m_query.tags) {
        const QString name = tagName(id);
        if (!name.isEmpty())
            out.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}});
    }
    return out;
}

QVariantList LibraryListModel::tagOptions() const
{
    return tagOptionsMatching(QString());
}

QVariantList LibraryListModel::tagOptionsMatching(const QString& text) const
{
    QVector<tanara::TagUsage> tags;
    if (tanara::MeetingLibrary* lib = library(); lib && !m_forceEmpty)
        tags = lib->tagOptions();
    else if (!lib && !m_forceEmpty && AppContext::instance()->demo())
        tags = demo::tagOptions();
    // A kiválasztott címkék elöl (a popover tetején látszik, mire szűrünk), a többi marad
    // gyakoriság szerint.
    std::stable_partition(tags.begin(), tags.end(), [this](const tanara::TagUsage& u) {
        return m_query.tags.contains(u.tag.id);
    });
    const QString needle = tanara::textfold::foldQuery(text);
    QVariantList out;
    for (const tanara::TagUsage& u : std::as_const(tags)) {
        if (!needle.isEmpty()
            && !tanara::textfold::find(tanara::textfold::fold(tanara::textfold::normalize(u.tag.name)), needle).isValid())
            continue;
        out.append(tagOption(u));
    }
    return out;
}

int LibraryListModel::untaggedCount() const
{
    if (m_forceEmpty) return 0;
    if (tanara::MeetingLibrary* lib = library())
        return lib->untaggedCount();
    if (AppContext::instance()->demo())
        return demo::untaggedCount();
    return 0;
}

QStringList LibraryListModel::tagLine(const QStringList& names)
{
    // A design szabálya (T04/T05): az első mindig, a többi addig, amíg a nevek hossza
    // (+2 a közért) ~24 karakterbe fér; a maradék „+N”.
    const int budget = 24;
    QStringList out;
    int len = 0;
    for (int k = 0; k < names.size(); ++k) {
        const QString& name = names.at(k);
        if (k > 0 && len + name.size() > budget) {
            out << QStringLiteral("+%1").arg(names.size() - k);
            break;
        }
        out << QStringLiteral("#") + name;
        len += int(name.size()) + 2;
    }
    return out;
}

tanara::LibraryEntry LibraryListModel::entryFor(const QString& meetingId) const
{
    const int row = indexOfMeeting(meetingId);
    if (row >= 0)
        return m_entries.at(row);
    if (tanara::MeetingLibrary* lib = library())
        return lib->entry(meetingId);
    if (AppContext::instance()->demo())
        return demo::entry(meetingId);
    return {};
}

void LibraryListModel::clearFilters()
{
    if (m_query.isEmpty() && m_query.text.isEmpty()) return;
    const bool hadTags = !m_query.tags.isEmpty();
    m_query = {};
    emit queryChanged();
    if (hadTags) emit tagFilterItemsChanged();
    refreshNow();
}

void LibraryListModel::setForceEmpty(bool on)
{
    if (on == m_forceEmpty) return;
    m_forceEmpty = on;
    emit forceEmptyChanged();
    refreshNow();
    emit tagOptionsChanged();
}

QString LibraryListModel::resultText() const
{
    if (!m_query.text.trimmed().isEmpty()) {
        // „4 találat · címben, címkében, átiratban” — csak a ténylegesen előforduló helyek.
        bool inTitle = false, inTag = false, inText = false;
        for (const LibraryEntry& e : m_entries) {
            inTitle = inTitle || e.titleMatch.isValid();
            inTag = inTag || e.tagMatch;
            inText = inText || !e.snippet.isEmpty();
        }
        QStringList where;
        if (inTitle) where << tr("címben");
        if (inTag) where << tr("címkében");
        if (inText) where << tr("átiratban");
        const QString n = tr("%n találat", nullptr, count());
        return where.isEmpty() ? n : n + QStringLiteral(" · ") + where.join(QStringLiteral(", "));
    }
    return tr("%n megbeszélés a szűrő szerint", nullptr, count());
}

QVariantList LibraryListModel::peopleOptions() const
{
    QVector<tanara::PersonPresence> people;
    if (tanara::MeetingLibrary* lib = library())
        people = lib->people();
    else if (AppContext::instance()->demo() && !m_forceEmpty)
        people = demo::people();
    QVariantList out;
    int i = 0;
    for (const tanara::PersonPresence& p : std::as_const(people))
        out.append(QVariantMap{{QStringLiteral("name"), p.name},
                               {QStringLiteral("count"), p.meetingCount},
                               {QStringLiteral("colorIndex"), i++}});
    return out;
}

int LibraryListModel::personColorIndex(const QString& name) const
{
    const QVariantList opts = peopleOptions();
    for (const QVariant& v : opts) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("name")).toString() == name)
            return m.value(QStringLiteral("colorIndex")).toInt();
    }
    return 0;
}

int LibraryListModel::indexOfMeeting(const QString& meetingId) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id == meetingId)
            return i;
    return -1;
}

QString LibraryListModel::meetingIdAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).id : QString();
}

QString LibraryListModel::titleOf(const QString& meetingId) const
{
    const int i = indexOfMeeting(meetingId);
    return i >= 0 ? m_entries.at(i).title : QString();
}

void LibraryListModel::warmUp()
{
    if (tanara::MeetingLibrary* lib = library())
        if (!lib->isWarm())
            lib->warmUp();
}

void LibraryListModel::scheduleRefresh(int delayMs)
{
    // Több jel egy eseményhurok-körben (pl. átirat kész: store + tracker) → egy lekérdezés.
    if (!m_refreshTimer.isActive() || m_refreshTimer.remainingTime() > delayMs)
        m_refreshTimer.start(delayMs);
}

tanara::LibraryResult LibraryListModel::runQuery() const
{
    if (m_forceEmpty)
        return {};
    if (tanara::MeetingLibrary* lib = library())
        return lib->query(m_query);
    if (AppContext::instance()->demo())
        return demo::query(m_query);
    return {};
}

void LibraryListModel::refreshNow()
{
    m_refreshTimer.stop();
    apply(runQuery());
}

void LibraryListModel::onMeetingChanged(const QString& meetingId)
{
    // Szűrés nélkül egy meeting változása (cím, állapot-ikon, futó feladat) nem érinti a
    // sorrendet (az a kezdési idő szerinti) → csak azt az egy sort frissítjük. Szűrt
    // listában a tagság is változhat, ott újra lekérdezünk.
    emit tagOptionsChanged();     // a címkék darabszáma változhatott
    tanara::MeetingLibrary* lib = library();
    const int row = indexOfMeeting(meetingId);
    if (!lib || filtered() || row < 0 || m_refreshTimer.isActive()) {
        scheduleRefresh();
        return;
    }
    const LibraryEntry fresh = lib->entry(meetingId);
    if (fresh.id.isEmpty() || fresh.startedAt != m_entries.at(row).startedAt) {
        scheduleRefresh();
        return;
    }
    if (!sameEntry(fresh, m_entries.at(row))) {
        m_entries[row] = fresh;
        emit dataChanged(index(row), index(row));
    }
}

void LibraryListModel::apply(const tanara::LibraryResult& result)
{
    const QVector<LibraryEntry>& fresh = result.entries;
    const int oldCount = int(m_entries.size());
    const int oldTotal = m_total;
    const int n = int(fresh.size());

    auto sameIdsFrom = [&](int oldFrom, int newFrom) {
        for (int i = oldFrom, j = newFrom; i < oldCount && j < n; ++i, ++j)
            if (m_entries.at(i).id != fresh.at(j).id) return false;
        return true;
    };
    int firstDiff = 0;
    while (firstDiff < oldCount && firstDiff < n
           && m_entries.at(firstDiff).id == fresh.at(firstDiff).id)
        ++firstDiff;

    if (n == oldCount && firstDiff == n) {
        // Ugyanazok a sorok: csak a ténylegesen megváltozottakat jelezzük.
        for (int i = 0; i < n; ++i) {
            if (sameEntry(m_entries.at(i), fresh.at(i))) continue;
            m_entries[i] = fresh.at(i);
            emit dataChanged(index(i), index(i));
        }
    } else if (n == oldCount + 1 && sameIdsFrom(firstDiff, firstDiff + 1)) {
        beginInsertRows({}, firstDiff, firstDiff);
        m_entries = fresh;
        endInsertRows();
        if (n > 1) emit dataChanged(index(0), index(n - 1));
    } else if (n == oldCount - 1 && sameIdsFrom(firstDiff + 1, firstDiff)) {
        beginRemoveRows({}, firstDiff, firstDiff);
        m_entries = fresh;
        endRemoveRows();
        if (n > 0) emit dataChanged(index(0), index(n - 1));
    } else {
        beginResetModel();
        m_entries = fresh;
        endResetModel();
    }
    m_total = result.totalMeetings;
    if (oldCount != n || oldTotal != m_total)
        emit countChanged();
    emit resultChanged();
}

} // namespace tanara_qml

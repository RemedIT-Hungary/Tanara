#include "LibraryListModel.h"

#include "AppContext.h"
#include "ShellFormat.h"

#include "tanara/library/ILibraryFacade.h"
#include "tanara/foundation/text/Text.h"
#include "tanara/library/tags/TagService.h"

#include <algorithm>

namespace tanara_qml {

using tanara::library::LibraryEntry;
using tanara::StepState;

namespace {

// Shortening the part before the hit: in the narrow sidebar the hit must show at the start of
// the line ("...once more before the demo..."), so at most ~16 characters stay before it.
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

Parts split(const QString& text, const tanara::text::Range& r, bool clip)
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
    init();
    // Without an injected facade the model follows the App singleton's services (real library, or the
    // fictional one in demo mode).
    AppContext* ctx = AppContext::instance();
    const auto resolve = [this] {
        if (m_injected) return;
        m_facade = AppContext::instance()->services().library;
        attach();
    };
    connect(ctx, &AppContext::controllerChanged, this, resolve);
    connect(ctx, &AppContext::demoChanged, this, resolve);
    m_facade = ctx->services().library;
    attach();
}

LibraryListModel::LibraryListModel(tanara::library::ILibraryFacade* facade, QObject* parent)
    : QAbstractListModel(parent), m_facade(facade), m_injected(true)
{
    init();
    attach();
}

void LibraryListModel::init()
{
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, &LibraryListModel::refreshNow);
    m_dayTimer.setInterval(30 * 60 * 1000);
    connect(&m_dayTimer, &QTimer::timeout, this, [this] { scheduleRefresh(); });
    m_dayTimer.start();
}

void LibraryListModel::attach()
{
    tanara::library::ILibraryFacade* lib = m_facade;
    if (lib != m_attached) {
        if (m_attached)
            m_attached->disconnect(this);
        m_attached = lib;
        if (lib) {
            using tanara::library::ILibraryFacade;
            connect(lib, &ILibraryFacade::meetingAdded, this, [this](const QString&) {
                scheduleRefresh();
                emit peopleOptionsChanged();
                emit tagOptionsChanged();
            });
            connect(lib, &ILibraryFacade::meetingRemoved, this, [this](const QString&) {
                scheduleRefresh();
                emit peopleOptionsChanged();
                emit tagOptionsChanged();
            });
            connect(lib, &ILibraryFacade::meetingChanged, this, &LibraryListModel::onMeetingChanged);
            connect(lib, &ILibraryFacade::reset, this, [this] {
                scheduleRefresh();
                emit peopleOptionsChanged();
                emit tagOptionsChanged();
            });
            // The pool changed (rename, delete, merge): rows, chips, popover.
            connect(lib, &ILibraryFacade::tagsChanged, this, [this] {
                scheduleRefresh();
                emit tagOptionsChanged();
                emit tagFilterItemsChanged();
            });
            // After preloading, the text search's hits are complete.
            connect(lib, &ILibraryFacade::warmedUp, this, [this] {
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
        return filtered() ? QString() : tanara::library::MeetingLibrary::sectionTitle(e.section);
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
    // While typing we do not search on every keystroke; switching the filter on / off is immediate.
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

tanara::PersonIds LibraryListModel::idsForNames(const QStringList& names) const
{
    QVector<tanara::library::PersonPresence> people;
    if (m_facade && !m_forceEmpty)
        people = m_facade->people();
    tanara::PersonIds out;
    for (const QString& name : names) {
        tanara::PersonId id;
        for (const tanara::library::PersonPresence& p : std::as_const(people))
            if (p.name == name) { id = p.id; break; }
        // An unknown name filters to nothing (as a name nobody carries did before).
        out << (id.isNull() ? tanara::PersonId::fromString(QStringLiteral("unknown:") + name) : id);
    }
    return out;
}

void LibraryListModel::setPeople(const QStringList& people)
{
    if (people == m_peopleNames) return;
    m_peopleNames = people;
    m_query.people = idsForNames(people);
    emit queryChanged();
    refreshNow();
}

void LibraryListModel::addPerson(const QString& name)
{
    if (name.isEmpty() || m_peopleNames.contains(name)) return;
    QStringList p = m_peopleNames;
    p << name;
    setPeople(p);
}

void LibraryListModel::removePerson(const QString& name)
{
    QStringList p = m_peopleNames;
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
    return m_facade ? m_facade->tagName(tagId) : QString();
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
    if (m_facade && !m_forceEmpty)
        tags = m_facade->tagOptions();
    // The selected tags first (the popover's top shows what we filter on), the rest stay by frequency.
    std::stable_partition(tags.begin(), tags.end(), [this](const tanara::TagUsage& u) {
        return m_query.tags.contains(u.tag.id);
    });
    const QString needle = tanara::text::foldQuery(text);
    QVariantList out;
    for (const tanara::TagUsage& u : std::as_const(tags)) {
        if (!needle.isEmpty()
            && !tanara::text::find(tanara::text::fold(tanara::text::normalize(u.tag.name)), needle).isValid())
            continue;
        out.append(tagOption(u));
    }
    return out;
}

int LibraryListModel::untaggedCount() const
{
    if (m_forceEmpty || !m_facade) return 0;
    return m_facade->untaggedCount();
}

QStringList LibraryListModel::tagLine(const QStringList& names)
{
    // The design's rule: the first always, the others as long as the names' length (+2 for the
    // gap) fits ~24 characters; the rest "+N".
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

tanara::library::LibraryEntry LibraryListModel::entryFor(const QString& meetingId) const
{
    const int row = indexOfMeeting(meetingId);
    if (row >= 0)
        return m_entries.at(row);
    return m_facade ? m_facade->entry(meetingId) : tanara::library::LibraryEntry{};
}

void LibraryListModel::clearFilters()
{
    if (m_query.isEmpty() && m_query.text.isEmpty()) return;
    const bool hadTags = !m_query.tags.isEmpty();
    m_query = {};
    m_peopleNames.clear();
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
        // "4 hits · in title, tag, transcript": only the places that actually occur.
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
    QVector<tanara::library::PersonPresence> people;
    if (m_facade && !m_forceEmpty)
        people = m_facade->people();
    QVariantList out;
    int i = 0;
    for (const tanara::library::PersonPresence& p : std::as_const(people))
        out.append(QVariantMap{{QStringLiteral("name"), p.name},
                               {QStringLiteral("personId"), p.id.toString()},
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
    if (m_facade)
        m_facade->warmUp();
}

void LibraryListModel::scheduleRefresh(int delayMs)
{
    // Several signals in one event-loop turn (e.g. transcript done: store + tracker) -> one query.
    if (!m_refreshTimer.isActive() || m_refreshTimer.remainingTime() > delayMs)
        m_refreshTimer.start(delayMs);
}

tanara::library::LibraryResult LibraryListModel::runQuery() const
{
    if (m_forceEmpty)
        return {};
    return m_facade ? m_facade->query(m_query) : tanara::library::LibraryResult{};
}

void LibraryListModel::refreshNow()
{
    m_refreshTimer.stop();
    apply(runQuery());
}

void LibraryListModel::onMeetingChanged(const QString& meetingId)
{
    // Without filtering, a meeting's change (title, state icon, running task) does not affect the
    // order (which is by start time) -> only that one row is refreshed. In a filtered list
    // membership can change too, so we query again.
    emit tagOptionsChanged();     // the tag counts may have changed
    tanara::library::ILibraryFacade* lib = m_facade;
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

void LibraryListModel::apply(const tanara::library::LibraryResult& result)
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
        // The same rows: only the really changed ones are signaled.
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

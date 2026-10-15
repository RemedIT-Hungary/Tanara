#include "PeopleViewModel.h"

#include "DemoText.h"

#include "AppContext.h"
#include "PlayerController.h"
#include "ShellFormat.h"
#include "TagBackend.h"
#include "TagsViewModel.h"

#include "tanara/domain/SpeakerText.h"
#include "tanara/foundation/text/Text.h"
#include "tanara/library/people/IPeopleFacade.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QCollator>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace tanara;

namespace tanara_qml {

namespace {

constexpr auto kSortAbc = "abc";
constexpr auto kSortRecent = "recent";
constexpr auto kSortMeetings = "meetings";

QString capitalized(QString text)
{
    if (!text.isEmpty()) text[0] = text.at(0).toUpper();
    return text;
}

const QString kTagsUndo = QStringLiteral("tags");
const QString kPeopleUndo = QStringLiteral("people");

// Monogram: the words' initials (at most two); a leading abbreviation in the name ("B. Gergő") is skipped.
QString monogramOf(const QString& name)
{
    QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() > 1 && words.first().size() == 2 && words.first().endsWith(QLatin1Char('.')))
        words.removeFirst();
    QString out;
    for (const QString& w : std::as_const(words)) {
        if (!w.at(0).isLetterOrNumber()) continue;
        out += w.at(0).toUpper();
        if (out.size() == 2) break;
    }
    return out.isEmpty() ? QStringLiteral("?") : out;
}

// The name list's section in alphabetical order: the initial; a long vowel goes with its
// base letter (Á with A, Ő with Ö), as Hungarian alphabetical order treats them together.
QString initialOf(const QString& name)
{
    for (const QChar c : name) {
        if (!c.isLetterOrNumber()) continue;
        const QChar u = c.toUpper();
        switch (u.unicode()) {
        case 0x00C1: return QStringLiteral("A");
        case 0x00C9: return QStringLiteral("E");
        case 0x00CD: return QStringLiteral("I");
        case 0x00D3: return QStringLiteral("O");
        case 0x00DA: return QStringLiteral("U");
        case 0x0150: return QStringLiteral("Ö");
        case 0x0170: return QStringLiteral("Ü");
        default: return QString(u);
        }
    }
    return QStringLiteral("#");
}

QString sampleIcon(const QString& kind)
{
    if (kind == QStringLiteral("mic")) return QStringLiteral("mic");
    if (kind == QStringLiteral("call")) return QStringLiteral("headphones");
    if (kind == QStringLiteral("mix")) return QStringLiteral("audio-lines");
    return QStringLiteral("speaker");
}

// Where the searched text is in the NAME (accent- and case-insensitive). The folded form
// matches the original character by character; if not (rare, decomposed accents), no highlight.
bool matchRange(const QString& name, const QString& foldedNeedle, int* pos, int* len)
{
    const QString folded = text::foldForSearch(name);
    const int at = folded.indexOf(foldedNeedle);
    if (at < 0) return false;
    *pos = at;
    *len = folded.size() == name.size() ? int(foldedNeedle.size()) : 0;
    return true;
}

} // namespace

PeopleViewModel::PeopleViewModel(QObject* parent) : QObject(parent)
{
    initTimers();
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, &PeopleViewModel::resolveServices);
    connect(ctx, &AppContext::demoChanged, this, &PeopleViewModel::resolveServices);
    resolveServices();
}

PeopleViewModel::PeopleViewModel(const UiServices& services, QObject* parent) : QObject(parent), m_explicitServices(true)
{
    initTimers();
    useServices(services);
}

PeopleViewModel::~PeopleViewModel() = default;

void PeopleViewModel::initTimers()
{
    m_playTick.setInterval(50);
    connect(&m_playTick, &QTimer::timeout, this, [this] {
        if (m_backend && m_playEndMs > 0 && m_backend->position() >= m_playEndMs) finishPlayback();
    });

    m_backfillPoll.setInterval(1000);
    connect(&m_backfillPoll, &QTimer::timeout, this, &PeopleViewModel::refreshBackfill);
}

// The process's services changed (a controller arrived, App.demo flipped).
void PeopleViewModel::resolveServices()
{
    if (m_explicitServices) return;
    useServices(AppContext::instance()->services());
}

void PeopleViewModel::useServices(const UiServices& services)
{
    for (const QMetaObject::Connection& c : std::as_const(m_connections)) disconnect(c);
    m_connections.clear();
    if (m_facade && m_facade->parent() == this) m_facade->deleteLater();   // a fictional facade of ours
    m_facade = services.people(this);
    m_demo = m_facade->readOnly();
    m_facade->applyDemoState(m_demoState);
    m_selected.clear();
    m_planFor.clear();
    auto dirty = [this] { m_planFor.clear(); scheduleRebuild(); };
    m_connections << connect(m_facade, &library::IPeopleFacade::changed, this, &PeopleViewModel::scheduleRebuild);
    m_connections << connect(m_facade, &library::IPeopleFacade::undoChanged, this, &PeopleViewModel::undoChanged);
    m_connections << connect(m_facade, &library::IPeopleFacade::statsChanged, this, dirty);
    m_connections << connect(m_facade, &library::IPeopleFacade::peopleChanged, this, &PeopleViewModel::scheduleRebuild);
    m_connections << connect(m_facade, &library::IPeopleFacade::voiceprintsChanged, this, dirty);
    // There is no signal for the start of the backfill: we check on a model change (where it starts) and at the end.
    m_connections << connect(m_facade, &library::IPeopleFacade::voiceModelsChanged, this, [this, dirty] {
        dirty();
        QTimer::singleShot(0, this, &PeopleViewModel::refreshBackfill);
    });
    m_connections << connect(m_facade, &library::IPeopleFacade::backfillFinished, this, [this, dirty] {
        dirty();
        refreshBackfill();
    });
    TagBackend* old = m_ownTagBackend;
    m_ownTagBackend = services.tagBackend(this);
    attachTags(m_ownTagBackend);
    if (old) old->deleteLater();
    m_undoOrder.clear();
    rebuild();
    refreshBackfill();
    emit undoChanged();
}

void PeopleViewModel::setTagBackend(TagBackend* backend)
{
    attachTags(backend ? backend : m_ownTagBackend);
    rebuild();
}

void PeopleViewModel::attachTags(TagBackend* backend)
{
    for (const QMetaObject::Connection& c : std::as_const(m_tagConnections)) disconnect(c);
    m_tagConnections.clear();
    m_tagBackend = backend;
    if (!backend) return;
    m_tagConnections << connect(backend, &TagBackend::personTagsChanged, this, &PeopleViewModel::refreshTags);
    m_tagConnections << connect(backend, &TagBackend::personStatsChanged, this, &PeopleViewModel::refreshTags);
    m_tagConnections << connect(backend, &TagBackend::tagsChanged, this, &PeopleViewModel::refreshTags);
    m_tagConnections << connect(backend, &TagBackend::undoChanged, this, &PeopleViewModel::undoChanged);
}

void PeopleViewModel::refreshBackfill()
{
    const bool running = m_facade->backfillRunning();
    if (running && !m_backfillPoll.isActive() && !m_demo) m_backfillPoll.start();
    if (!running) m_backfillPoll.stop();
    if (running == m_backfillRunning) return;
    m_backfillRunning = running;
    emit backfillChanged();
}

void PeopleViewModel::setDemoState(const QString& state)
{
    if (m_demoState == state) return;
    m_demoState = state;
    emit demoStateChanged();
    if (!m_demo) return;
    m_facade->applyDemoState(state);
    m_selected.clear();
    if (state == QStringLiteral("P02")) m_query = demoText("ger", "greg");
    else if (state == QStringLiteral("noResult")) m_query = demoText("bár g", "barn g");
    else m_query.clear();
    emit queryChanged();
    rebuild();
    if (state == QStringLiteral("P03") || state == QStringLiteral("creating"))
        setSelectedName(demoName("Tóth Eszter"));
    else if (state == QLatin1String("tagsSection"))
        setSelectedName(demoName("Varga Árpád"));
    else if (state == QLatin1String("tagsSuggested") || state == QLatin1String("tagsFocus"))
        setSelectedName(demoName("Fehér Gábor"));
    else if (state == QLatin1String("tagsSelf"))
        setSelectedName(demoName("Kovács Lilla"));
    else if (state != QStringLiteral("P06"))
        setSelectedName(demoName("Bárány Gergely"));
    // P01: the second sample is playing.
    m_playingId = state == QStringLiteral("P01") ? QStringLiteral("demo-1") : QString();
    emit playingChanged();
    refreshBackfill();
    if (state == QStringLiteral("creating")) {
        m_creating = true;
        m_creatingText = tr("%1 / %2 megbeszélés…").arg(2).arg(3);
        emit planChanged();
    }
}

void PeopleViewModel::refresh()
{
    if (m_demo) return;
    m_planFor.clear();
    rebuild();
    m_facade->refreshStats();
    refreshBackfill();
}

void PeopleViewModel::scheduleRebuild()
{
    if (m_rebuildScheduled) return;
    m_rebuildScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_rebuildScheduled = false;
        rebuild();
    });
}

const PeopleViewModel::Person* PeopleViewModel::find(const QString& name) const
{
    for (const Person& p : m_persons)
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) return &p;
    return nullptr;
}

tanara::PersonId PeopleViewModel::idOf(const QString& name) const
{
    const Person* p = find(name);
    return p ? p->id : tanara::PersonId();
}

void PeopleViewModel::rebuild()
{
    m_facade->reload();
    m_statsReady = m_facade->statsReady();
    m_persons.clear();
    const QVector<library::PersonRecord> records = m_facade->persons();
    m_persons.reserve(records.size());
    for (const library::PersonRecord& r : records) {
        Person p;
        p.id = r.id;
        p.name = r.name;
        p.isSelf = r.isSelf;
        p.aliases = r.aliases;
        p.note = r.note;
        p.sampleCount = r.sampleCount;
        p.defaultSide = r.defaultSide;
        p.defaultSideManual = r.defaultSideManual;
        if (m_statsReady) {
            const library::PersonStats st = m_facade->stats(r.id);
            p.meetingCount = st.meetingCount;
            p.talkMs = st.talkMs;
            p.lastSeen = st.lastSeen;
        }
        m_persons.append(p);
    }

    // Keeps the selection; if the person is gone, the row in its place (or the own one).
    const int previousRow = m_list.indexOfName(m_selected);
    const bool lost = !m_selected.isEmpty() && !find(m_selected);
    rebuildList();
    QString next = m_selected;
    if (lost || m_selected.isEmpty()) {
        next.clear();
        if (lost && m_list.count() > 0)
            next = m_list.nameAt(std::clamp(previousRow, 0, m_list.count() - 1));
        if (next.isEmpty()) {
            for (const Person& p : std::as_const(m_persons))
                if (p.isSelf) { next = p.name; break; }
        }
        if (next.isEmpty() && m_list.count() > 0) next = m_list.nameAt(0);
    } else if (const Person* p = find(m_selected)) {
        next = p->name;   // the stored spelling
    }
    const bool selectionMoved = next != m_selected;
    m_selected = next;
    refreshDetail();
    emit listChanged();
    if (selectionMoved) emit selectionChanged();
}

QString PeopleViewModel::rowMeta(const Person& p, const QString& aliasHit) const
{
    QStringList parts;
    if (!aliasHit.isEmpty()) parts << tr("„%1”").arg(aliasHit);
    if (m_statsReady) parts << tr("%n megbeszélés", "", p.meetingCount);
    parts << (p.sampleCount > 0 ? tr("%n minta", "", p.sampleCount) : tr("nincs hanglenyomat"));
    return parts.join(QStringLiteral(" · "));
}

void PeopleViewModel::rebuildList()
{
    const QString needle = text::foldForSearch(m_query.trimmed());
    struct Shown { const Person* p; int pos = 0; int len = 0; QString aliasHit; QString tagHit; };
    QVector<Shown> shown;
    // Also matches a tag ("nordvik" or "#nordvik"): among the person's manual tags.
    QString tagNeedle = needle;
    while (tagNeedle.startsWith(QLatin1Char('#'))) tagNeedle.remove(0, 1);
    QHash<QString, QString> tagNames;
    if (!tagNeedle.isEmpty() && m_tagBackend)
        for (const TagItem& t : m_tagBackend->tags()) tagNames.insert(t.id, t.name);
    for (const Person& p : std::as_const(m_persons)) {
        Shown s{&p};
        if (!needle.isEmpty() && !matchRange(p.name, needle, &s.pos, &s.len)) {
            // Not in the name: the first matching nickname.
            for (const QString& a : p.aliases)
                if (text::foldForSearch(a).contains(needle)) { s.aliasHit = a; break; }
            if (s.aliasHit.isEmpty() && !tagNames.isEmpty() && !p.isSelf) {
                for (const QString& id : m_tagBackend->tagsOfPerson(p.name))
                    if (text::foldForSearch(tagNames.value(id)).contains(tagNeedle)) { s.tagHit = tagNames.value(id); break; }
            }
            if (s.aliasHit.isEmpty() && s.tagHit.isEmpty()) continue;
        }
        shown.append(s);
    }

    QCollator collator(fmt::uiLocale());
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    auto byName = [&](const Person* a, const Person* b) { return collator.compare(a->name, b->name) < 0; };
    std::sort(shown.begin(), shown.end(), [&](const Shown& x, const Shown& y) {
        const Person* a = x.p;
        const Person* b = y.p;
        if (a->isSelf != b->isSelf) return a->isSelf;       // the own person is always first
        if (m_sort == QLatin1String(kSortRecent)) {
            if (a->lastSeen != b->lastSeen) {
                if (!a->lastSeen.isValid()) return false;
                if (!b->lastSeen.isValid()) return true;
                return a->lastSeen > b->lastSeen;
            }
        } else if (m_sort == QLatin1String(kSortMeetings)) {
            if (a->meetingCount != b->meetingCount) return a->meetingCount > b->meetingCount;
        }
        return byName(a, b);
    });

    QVector<PeopleListRow> rows;
    rows.reserve(shown.size());
    QString lastSection;
    for (const Shown& s : std::as_const(shown)) {
        PeopleListRow r;
        r.name = s.p->name;
        r.monogram = monogramOf(s.p->name);
        r.isSelf = s.p->isSelf;
        r.hasVoiceprint = s.p->sampleCount > 0;
        r.meta = rowMeta(*s.p, s.aliasHit);
        if (!s.tagHit.isEmpty()) {
            r.meta = tr("címke: #%1").arg(s.tagHit) + QStringLiteral(" · ") + r.meta;
            r.tagHit = true;
        }
        if (!needle.isEmpty() && s.aliasHit.isEmpty() && s.tagHit.isEmpty() && s.len > 0) {
            r.before = s.p->name.left(s.pos);
            r.match = s.p->name.mid(s.pos, s.len);
            r.after = s.p->name.mid(s.pos + s.len);
        } else {
            r.before = s.p->name;
        }
        // Sections: "You", then initials (alphabetical) or "Others"; none while searching.
        if (needle.isEmpty()) {
            const QString section = s.p->isSelf ? tr("Te")
                : m_sort == QLatin1String(kSortAbc) ? initialOf(s.p->name) : tr("Többiek");
            if (section != lastSection) r.header = section;
            lastSection = section;
        }
        rows.append(r);
    }
    m_list.setRows(rows);
}

QString PeopleViewModel::personMeta(const Person& p) const
{
    if (!m_statsReady) return tr("Megbeszélések számolása…");
    if (p.meetingCount <= 0) return tr("Még egy megbeszélésen sem szerepel");
    QStringList parts;
    if (p.lastSeen.isValid()) parts << tr("Utoljára: %1").arg(fmt::shortDate(p.lastSeen));
    parts << tr("%n megbeszélés", "", p.meetingCount);
    if (p.talkMs > 0) parts << tr("%1 beszéd").arg(fmt::shortDuration(p.talkMs));
    return parts.join(QStringLiteral(" · "));
}

void PeopleViewModel::refreshDetail()
{
    const Person* p = find(m_selected);
    m_detail = p ? *p : Person();
    m_detailMeta = p ? personMeta(*p) : QString();
    m_samples.clear();
    m_meetings.clear();
    QSet<QString> sources;

    if (p) {
        for (const library::VoiceSample& s : m_facade->samples(p->id)) {
            QString label = s.deviceLabel;
            if (label.isEmpty())
                label = s.sourceKind == QStringLiteral("mix") ? tr("A megbeszélés lekevert hangja")
                                                             : tr("ismeretlen eszköz");
            sources.insert(s.sourceKind + QLatin1Char('|') + label);
            m_samples.append(QVariantMap{
                {QStringLiteral("id"), s.id},
                {QStringLiteral("kind"), s.sourceKind},
                {QStringLiteral("icon"), sampleIcon(s.sourceKind)},
                {QStringLiteral("label"), label},
                {QStringLiteral("meetingTitle"), s.meetingTitle.isEmpty() ? tr("A megbeszélés már nincs meg")
                                                                          : s.meetingTitle},
                {QStringLiteral("date"), s.recordedAt.isValid() ? s.recordedAt.toString(QStringLiteral("yyyy-MM-dd"))
                                                                : QStringLiteral("–")},
                {QStringLiteral("length"), s.durationMs() > 0 ? fmt::clock(s.durationMs()) : QStringLiteral("–")},
                {QStringLiteral("playable"), s.audioExists},
                {QStringLiteral("path"), s.audioPath},
                {QStringLiteral("startMs"), s.startMs},
                {QStringLiteral("endMs"), s.endMs},
            });
        }
        if (m_statsReady) {
            for (const library::PersonMeetingStat& m : m_facade->stats(p->id).meetings)
                m_meetings.append(QVariantMap{
                    {QStringLiteral("title"), m.title},
                    {QStringLiteral("date"), m.startedAt.toString(QStringLiteral("yyyy-MM-dd"))},
                    {QStringLiteral("talk"), m.talkMs > 0 ? fmt::shortDuration(m.talkMs) : QStringLiteral("–")},
                });
        }
    }
    m_samplesSub = m_samples.isEmpty() ? QString()
        : tr("%n minta", "", int(m_samples.size())) + QStringLiteral(" · ")
          + tr("%n forrásból", "", int(sources.size()));

    // If the sample being played is no longer in the list (deleted, another person), playback stops.
    if (!m_playingId.isEmpty() && !m_demo) {
        bool still = false;
        for (const QVariant& v : std::as_const(m_samples))
            if (v.toMap().value(QStringLiteral("id")).toString() == m_playingId) { still = true; break; }
        if (!still) finishPlayback();
    }
    decorateSamples();
    emit detailChanged();
    refreshPlan();
    refreshTags();
}

// The sample's model label: which used voice model has a voiceprint of it (from the sibling group).
void PeopleViewModel::decorateSamples()
{
    const QStringList active = m_facade->activeVoiceModelIds();
    if (active.isEmpty()) return;
    QHash<QString, QString> display;
    for (const processing::VoiceModelSpec& spec : processing::VoiceModelRegistry::builtin()) display.insert(spec.id, spec.displayName);
    auto label = [&](const QStringList& ids) {
        QStringList out;
        for (const QString& id : ids) out << display.value(id, id);
        return out.join(QStringLiteral(", "));
    };
    for (int i = 0; i < m_samples.size(); ++i) {
        QVariantMap sample = m_samples[i].toMap();
        const QStringList have = m_facade->voiceprintModels(sample.value(QStringLiteral("id")).toString());
        QStringList present, missing;
        for (const QString& id : std::as_const(active)) (have.contains(id) ? present : missing) << id;
        // With one model, if present, there is nothing to indicate.
        if (active.size() < 2 && missing.isEmpty()) continue;
        sample.insert(QStringLiteral("modelText"), tr("%1/%2 modell").arg(present.size()).arg(active.size()));
        sample.insert(QStringLiteral("modelComplete"), missing.isEmpty());
        QStringList tip;
        if (!present.isEmpty()) tip << tr("Megvan: %1").arg(label(present));
        if (!missing.isEmpty()) tip << tr("Hiányzik: %1").arg(label(missing));
        sample.insert(QStringLiteral("modelTip"), tip.join(QLatin1Char('\n')));
        m_samples[i] = sample;
    }
}

// ---- tags on the person ----------------------------------------------------------

bool PeopleViewModel::isPlaceholderName(const QString& name) const
{
    // The "Anonymous N" created as a sample holder on delete (PeopleService) cannot be tagged.
    const QString pattern = QCoreApplication::translate("tanara::PeopleService", "Névtelen %1");
    QStringList parts = pattern.split(QStringLiteral("%1"));
    for (QString& part : parts) part = QRegularExpression::escape(part);
    const QString rx = parts.join(QStringLiteral("\\d+"));
    return QRegularExpression(QStringLiteral("^") + rx + QStringLiteral("$")).match(name.trimmed()).hasMatch();
}

QString PeopleViewModel::tagNameOf(const QString& id) const
{
    if (!m_tagBackend) return {};
    for (const TagItem& t : m_tagBackend->tags())
        if (t.id == id) return t.name;
    return {};
}

void PeopleViewModel::refreshTags()
{
    QString mode = QStringLiteral("none");
    QVariantList tags, suggestions;
    QString hint;
    if (m_tagBackend && hasSelection()) {
        if (m_detail.isSelf) mode = QStringLiteral("self");
        else if (!isPlaceholderName(m_selected)) mode = QStringLiteral("edit");
    }
    if (mode == QLatin1String("edit")) {
        QHash<QString, QString> names;
        for (const TagItem& t : m_tagBackend->tags()) names.insert(t.id, t.name);
        for (const QString& id : m_tagBackend->tagsOfPerson(m_selected))
            tags << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), names.value(id)}};
        if (m_tagBackend->personStatsBusy()) {
            hint = tr("Megbeszélések számolása…");
        } else if (m_tagBackend->personMeetingCount(m_selected) < 2) {
            hint = tr("A javaslathoz legalább két megbeszélés kell.");
        } else {
            for (const PersonTagItem& s : m_tagBackend->suggestTagsForPerson(m_selected)) {
                const QString name = names.value(s.tagId);
                if (name.isEmpty()) continue;
                suggestions << QVariantMap{
                    {QStringLiteral("id"), s.tagId},
                    {QStringLiteral("name"), name},
                    {QStringLiteral("countText"), QStringLiteral("%1/%2").arg(s.shared).arg(s.tagTotal)},
                    {QStringLiteral("toolTip"), capitalized(tr("%1 #%2 %3 megbeszéléséből %4 alkalommal ott volt.")
                                                                .arg(tagArticle(name), name).arg(s.tagTotal).arg(s.shared))},
                };
            }
        }
    }
    if (mode == m_tagsMode && tags == m_personTags && suggestions == m_tagSuggestions && hint == m_tagHint) return;
    m_tagsMode = mode;
    m_personTags = tags;
    m_tagSuggestions = suggestions;
    m_tagHint = hint;
    emit personTagsChanged();
}

void PeopleViewModel::tagStep(const QString& label)
{
    m_undoOrder << kTagsUndo;
    refreshTags();
    emit undoChanged();
    emit toast(label, m_tagBackend && m_tagBackend->canUndo());
}

void PeopleViewModel::addPersonTag(const QString& nameOrId)
{
    if (!m_tagBackend || m_tagsMode != QLatin1String("edit") || nameOrId.trimmed().isEmpty()) return;
    const QString existing = tagNameOf(nameOrId);
    const QString shown = existing.isEmpty() ? nameOrId.trimmed() : existing;
    const QString label = tr("Címke hozzáadva: #%1").arg(shown);
    m_tagBackend->beginGroup(label);
    const QString id = m_tagBackend->addPersonTag(m_selected, nameOrId.trimmed());
    m_tagBackend->endGroup();
    if (id.isEmpty()) return;
    tagStep(tr("Címke hozzáadva: #%1").arg(tagNameOf(id)));
}

void PeopleViewModel::removePersonTag(const QString& tagId)
{
    if (!m_tagBackend || m_tagsMode != QLatin1String("edit")) return;
    const QString label = tr("Címke levéve: #%1").arg(tagNameOf(tagId));
    m_tagBackend->beginGroup(label);
    m_tagBackend->removePersonTag(m_selected, tagId);
    m_tagBackend->endGroup();
    tagStep(label);
}

void PeopleViewModel::acceptTagSuggestion(const QString& tagId) { addPersonTag(tagId); }

void PeopleViewModel::acceptAllTagSuggestions()
{
    if (!m_tagBackend || m_tagsMode != QLatin1String("edit") || m_tagSuggestions.isEmpty()) return;
    const QVariantList all = m_tagSuggestions;
    const QString label = tr("%n javasolt címke hozzáadva", "", int(all.size()));
    m_tagBackend->beginGroup(label);
    for (const QVariant& v : all) m_tagBackend->addPersonTag(m_selected, v.toMap().value(QStringLiteral("id")).toString());
    m_tagBackend->endGroup();
    tagStep(label);
}

void PeopleViewModel::rejectTagSuggestion(const QString& tagId)
{
    if (!m_tagBackend || m_tagsMode != QLatin1String("edit")) return;
    const QString label = tr("Javaslat elutasítva: #%1").arg(tagNameOf(tagId));
    m_tagBackend->beginGroup(label);
    m_tagBackend->rejectPersonTag(m_selected, tagId);
    m_tagBackend->endGroup();
    tagStep(label);
}

// ---- person without a voiceprint ---------------------------------------------

void PeopleViewModel::refreshPlan()
{
    const bool wanted = hasSelection() && m_detail.sampleCount == 0;
    if (!wanted) {
        ++m_planGeneration;
        m_planFor.clear();
        if (m_planReady || m_planPossible) {
            m_planReady = m_planPossible = false;
            emit planChanged();
        }
        return;
    }
    if (m_planFor == m_selected && m_planReady) return;   // nothing changed that affects the plan
    if (!m_statsReady) {
        m_planReady = false;
        emit planChanged();
        return;
    }
    m_planReady = false;
    emit planChanged();
    const int generation = ++m_planGeneration;
    const QString name = m_selected;
    // Reviewing the meetings (row-by-row material) can take a few tenths of a second: the
    // selection shows first, the plan afterwards.
    QTimer::singleShot(0, this, [this, generation, name] {
        if (generation != m_planGeneration) return;
        const library::VoiceprintPlan plan = m_facade->voiceprintPlan(idOf(name));
        const int usable = plan.usableMeetingIds.size();
        m_planFor = name;
        m_planMeetingIds = plan.usableMeetingIds;
        m_planPossible = plan.modelAvailable && usable > 0;
        m_planNote.clear();
        m_planButton.clear();
        if (plan.meetingsWithLines == 0) {
            m_planText = tr("Még egy megbeszélésen sincsenek sorai. Ha az átiratban hozzárendeled a "
                            "sorait, azokból mintát vehetünk.");
        } else if (usable == 0) {
            m_planText = tr("Kézzel rendelted hozzá %n megbeszélés sorait, de egyiken sincs elég hosszú, "
                            "biztos sora egy mintához (megbeszélésenként legalább 15 másodpercnyi kell, "
                            "3 másodpercnél hosszabb sorokból).", "", plan.meetingsWithLines);
        } else if (!plan.modelAvailable) {
            m_planText = tr("Kézzel rendelted hozzá %n megbeszélés sorait. A hangfelismerő modell nincs "
                            "telepítve ezen a gépen, ezért most nem készíthető hanglenyomat.", "",
                            plan.meetingsWithLines);
        } else {
            m_planText = tr("Kézzel rendelted hozzá %n megbeszélés sorait, ezért eddig nem ismerjük fel "
                            "magától. A biztos sorokból mintát vehetünk.", "", plan.meetingsWithLines);
            m_planButton = tr("Minta %n megbeszélésből", "", usable);
            const qint64 sec = plan.usableMs / 1000;
            const QString amount = sec < 90 ? tr("%n másodperc", "", int(sec))
                                            : tr("%n perc", "", int((sec + 30) / 60));
            m_planNote = tr("kb. %1 hang, a gépen marad").arg(amount);
            if (usable < plan.meetingsWithLines)
                m_planNote += QStringLiteral(" · ")
                    + tr("%n megbeszélésen nincs elég hosszú sor", "", plan.meetingsWithLines - usable);
        }
        m_planReady = true;
        emit planChanged();
    });
}

void PeopleViewModel::createVoiceprint()
{
    if (m_demo || m_creating || !m_planPossible || m_planMeetingIds.isEmpty()) return;
    m_creating = true;
    m_creatingFor = m_selected;
    m_createQueue = m_planMeetingIds;
    m_createTotal = m_createQueue.size();
    m_createOk = m_createFailed = 0;
    m_createError.clear();
    m_creatingText = tr("%1 / %2 megbeszélés…").arg(0).arg(m_createTotal);
    emit planChanged();
    QTimer::singleShot(30, this, &PeopleViewModel::runNextVoiceprint);
}

// One step per meeting (decoding the audio can take a few seconds): between two meetings
// the UI gets room to breathe, and the progress shows.
void PeopleViewModel::runNextVoiceprint()
{
    if (!m_creating) return;
    if (m_demo || m_createQueue.isEmpty()) {
        m_creating = false;
        m_planFor.clear();
        QString text = m_createOk > 0 ? tr("%n minta készült.", "", m_createOk)
                                      : tr("Nem készült minta.");
        if (m_createFailed > 0)
            text += QLatin1Char(' ') + tr("%n megbeszélésből nem sikerült.", "", m_createFailed);
        if (m_createOk == 0 && !m_createError.isEmpty()) text += QLatin1Char(' ') + m_createError;
        emit planChanged();
        rebuild();
        emit toast(text, false);
        return;
    }
    const QString meetingId = m_createQueue.takeFirst();
    const VoiceprintResult r = m_facade->createVoiceprintFromMeeting(idOf(m_creatingFor), meetingId);
    if (r.ok) ++m_createOk;
    else {
        ++m_createFailed;
        m_createError = r.error;
    }
    m_creatingText = tr("%1 / %2 megbeszélés…").arg(m_createOk + m_createFailed).arg(m_createTotal);
    emit planChanged();
    QTimer::singleShot(30, this, &PeopleViewModel::runNextVoiceprint);
}

// ---- list state ------------------------------------------------------------

void PeopleViewModel::setQuery(const QString& query)
{
    if (m_query == query) return;
    m_query = query;
    emit queryChanged();
    rebuildList();
    emit listChanged();
}

void PeopleViewModel::setSort(const QString& sort)
{
    const QString s = sort == QLatin1String(kSortRecent) || sort == QLatin1String(kSortMeetings)
        ? sort : QString::fromLatin1(kSortAbc);
    if (m_sort == s) return;
    m_sort = s;
    emit sortChanged();
    rebuildList();
    emit listChanged();
}

QString PeopleViewModel::sortLabel() const
{
    if (m_sort == QLatin1String(kSortRecent)) return tr("Legutóbb");
    if (m_sort == QLatin1String(kSortMeetings)) return tr("Legtöbb megbeszélés");
    return tr("ABC");
}

QString PeopleViewModel::countText() const
{
    return searching() ? tr("%n találat", "", m_list.count())
                       : tr("%n személy", "", int(m_persons.size()));
}

bool PeopleViewModel::onlySelf() const
{
    return m_persons.isEmpty() || (m_persons.size() == 1 && m_persons.first().isSelf);
}

void PeopleViewModel::setSelectedName(const QString& name)
{
    const Person* p = find(name);
    const QString next = p ? p->name : QString();
    if (next == m_selected) return;
    m_selected = next;
    refreshDetail();
    emit selectionChanged();
    emit listChanged();
}

bool PeopleViewModel::selectPerson(const QString& name)
{
    if (!find(name)) return false;
    setSelectedName(name);
    // If the person would not show in the list because of the search, the search is cleared.
    if (m_list.indexOfName(m_selected) < 0) setQuery(QString());
    return true;
}

QString PeopleViewModel::selectedMonogram() const
{
    return m_selected.isEmpty() ? QString() : monogramOf(m_selected);
}

bool PeopleViewModel::personExists(const QString& name) const { return find(name.trimmed()) != nullptr; }

bool PeopleViewModel::canUndo() const
{
    if (!m_undoOrder.isEmpty() && m_undoOrder.last() == kTagsUndo && m_tagBackend && m_tagBackend->canUndo())
        return true;
    return m_facade->canUndo();
}

// ---- actions ------------------------------------------------------------------

QString PeopleViewModel::addPerson(const QString& name)
{
    if (m_demo) return QString();
    const library::PeopleOpResult r = m_facade->addPerson(name);
    rebuild();
    // For an existing name too, the person gets selected (the caller shows the error).
    if (!r.name.isEmpty()) selectPerson(r.name);
    return r.ok ? QString() : r.error;
}

QString PeopleViewModel::rename(const QString& newName)
{
    if (m_demo || m_selected.isEmpty()) return QString();
    const QString old = m_selected;
    const library::PeopleOpResult r = m_facade->renamePerson(idOf(old), newName);
    if (!r.ok) return r.error;
    m_selected = r.name;
    rebuild();
    emit selectionChanged();
    if (r.name != old) {
        m_undoOrder << kPeopleUndo;
        emit toast(tr("Átnevezve: %1").arg(r.name), true);
    }
    return QString();
}

QString PeopleViewModel::addAlias(const QString& alias)
{
    if (m_demo || m_selected.isEmpty()) return QString();
    const library::PeopleOpResult r = m_facade->addAlias(idOf(m_selected), alias);
    rebuild();
    return r.ok ? QString() : r.error;
}

void PeopleViewModel::removeAlias(const QString& alias)
{
    if (m_demo || m_selected.isEmpty()) return;
    if (!m_facade->removeAlias(idOf(m_selected), alias)) return;
    rebuild();
    m_undoOrder << kPeopleUndo;
    emit toast(tr("Becenév törölve: %1").arg(alias), true);
}

QString PeopleViewModel::defaultSideNote() const
{
    if (m_detail.defaultSide.isEmpty()) return {};
    return m_detail.defaultSideManual ? tr("kézzel beállítva") : tr("tanult");
}

bool PeopleViewModel::defaultSideVisible() const
{
    return !m_selected.isEmpty() && !m_detail.isSelf && !isPlaceholderName(m_selected);
}

void PeopleViewModel::setDefaultSide(const QString& side)
{
    if (m_selected.isEmpty() || !defaultSideVisible()) return;
    const QString clean = side == QLatin1String("local") || side == QLatin1String("remote") ? side : QString();
    if (!m_facade->setDefaultSide(idOf(m_selected), clean)) return;
    rebuild();
}

void PeopleViewModel::requestSection(const QString& section)
{
    if (!section.isEmpty()) emit sectionRequested(section);
}

void PeopleViewModel::setNote(const QString& note)
{
    if (m_demo || m_selected.isEmpty() || note == m_detail.note) return;
    m_facade->setNote(idOf(m_selected), note);
    rebuild();
}

void PeopleViewModel::removeSample(const QString& sampleId)
{
    if (m_demo) return;
    const library::PeopleOpResult r = m_facade->removeSample(sampleId);
    rebuild();
    if (!r.ok) {
        emit toast(r.error, false);
        return;
    }
    m_undoOrder << kPeopleUndo;
    const QString title = m_facade->undoInfo().detail;
    emit toast(title.isEmpty() ? tr("Minta törölve") : tr("Minta törölve: %1").arg(title), true);
}

QString PeopleViewModel::moveSample(const QString& sampleId, const QString& toName)
{
    if (m_demo) return QString();
    // The target: a listed person by name, otherwise a new person with that name.
    const tanara::PersonId target = idOf(toName);
    const bool existed = !target.isNull();
    const library::PeopleOpResult r = m_facade->moveSample(
        sampleId, existed ? tanara::PersonChoice::existing(target) : tanara::PersonChoice::create(toName));
    if (!r.ok) return r.error;
    rebuild();
    m_undoOrder << kPeopleUndo;
    emit toast(existed ? tr("Minta áthelyezve ide: %1").arg(r.name)
                       : tr("Új személy a mintából: %1").arg(r.name), true);
    return QString();
}

QVariantList PeopleViewModel::personChoices(const QString& query) const
{
    QVariantList out;
    const QString needle = text::foldForSearch(query.trimmed());
    struct Choice { const Person* p; QString aliasHit; };
    QVector<Choice> list;
    for (const Person& p : m_persons) {
        if (p.name == m_selected) continue;
        QString aliasHit;
        if (!needle.isEmpty() && !text::foldForSearch(p.name).contains(needle)) {
            for (const QString& a : p.aliases)
                if (text::foldForSearch(a).contains(needle)) { aliasHit = a; break; }
            if (aliasHit.isEmpty()) continue;
        }
        list.append({&p, aliasHit});
    }
    QCollator collator(fmt::uiLocale());
    std::sort(list.begin(), list.end(), [&](const Choice& a, const Choice& b) {
        return collator.compare(a.p->name, b.p->name) < 0;
    });
    for (const Choice& c : std::as_const(list))
        out.append(QVariantMap{{QStringLiteral("name"), c.p->name},
                               {QStringLiteral("monogram"), monogramOf(c.p->name)},
                               {QStringLiteral("isSelf"), c.p->isSelf},
                               {QStringLiteral("meta"), rowMeta(*c.p, c.aliasHit)}});
    return out;
}

QVariantList PeopleViewModel::mergeCandidates(const QString& query) const
{
    struct Candidate { const Person* p; int percent; QString aliasHit; };
    QVector<Candidate> list;
    const QString needle = text::foldForSearch(query.trimmed());
    for (const Person& p : m_persons) {
        if (p.name == m_selected) continue;
        QString aliasHit;
        if (!needle.isEmpty() && !text::foldForSearch(p.name).contains(needle)) {
            for (const QString& a : p.aliases)
                if (text::foldForSearch(a).contains(needle)) { aliasHit = a; break; }
            if (aliasHit.isEmpty()) continue;
        }
        int percent = -1;
        const double sim = m_facade->similarity(idOf(m_selected), p.id);
        if (sim >= 0) percent = qRound(sim * 100);
        list.append(Candidate{&p, percent, aliasHit});
    }
    // By voice similarity (those without a voiceprint last), alphabetical within equal ones.
    QCollator collator(fmt::uiLocale());
    std::sort(list.begin(), list.end(), [&](const Candidate& a, const Candidate& b) {
        if (a.percent != b.percent) return a.percent > b.percent;
        return collator.compare(a.p->name, b.p->name) < 0;
    });
    QVariantList out;
    for (const Candidate& c : std::as_const(list)) {
        QString meta = rowMeta(*c.p, c.aliasHit);
        // A percentage only if both sides have a voiceprint; "similar" only for a meaningful match.
        if (c.percent >= 50) meta += QStringLiteral(" · ") + tr("hang alapján hasonló (%1%)").arg(c.percent);
        else if (c.percent >= 0) meta += QStringLiteral(" · ") + tr("hang-egyezés: %1%").arg(c.percent);
        out.append(QVariantMap{{QStringLiteral("name"), c.p->name},
                               {QStringLiteral("monogram"), monogramOf(c.p->name)},
                               {QStringLiteral("meta"), meta},
                               {QStringLiteral("similarity"), c.percent}});
    }
    return out;
}

QString PeopleViewModel::mergeResultText(const QString& other, bool keepSelected) const
{
    const Person* o = find(other);
    if (!o || m_selected.isEmpty()) return QString();
    const QString loser = keepSelected ? o->name : m_selected;
    const QString survivor = keepSelected ? m_selected : o->name;
    const library::MergePreview preview = m_facade->mergePreview(idOf(loser), idOf(survivor));
    const int meetingsN = preview.meetingCount;
    const int samplesN = preview.sampleCount;
    const int stale = preview.staleSummaries;
    QString text = tr("Eredmény: <b>%1</b>, <b>%2</b>. „%3” becenév lesz.")
        .arg(tr("%n megbeszélés", "", meetingsN), tr("%n minta", "", samplesN), loser.toHtmlEscaped());
    if (stale > 0) text += QLatin1Char(' ') + tr("%n érintett összefoglaló elavultnak jelölődik.", "", stale);
    // If both have tags: the union.
    if (m_tagBackend) {
        const QStringList a = m_tagBackend->tagsOfPerson(m_selected);
        const QStringList b = m_tagBackend->tagsOfPerson(o->name);
        if (!a.isEmpty() && !b.isEmpty()) {
            QStringList names;
            for (const QString& id : a + b) {
                const QString n = QStringLiteral("#") + tagNameOf(id).toHtmlEscaped();
                if (!names.contains(n)) names << n;
            }
            text += QLatin1Char(' ') + tr("A címkék összeadódnak (%1).").arg(names.join(QStringLiteral(", ")));
        }
    }
    return text;
}

QString PeopleViewModel::merge(const QString& other, bool keepSelected)
{
    if (m_demo || m_selected.isEmpty()) return QString();
    const QString loser = keepSelected ? other : m_selected;
    const QString survivor = keepSelected ? m_selected : other;
    const library::PeopleOpResult r = m_facade->merge(idOf(loser), idOf(survivor));
    if (!r.ok) return r.error;
    m_selected = r.name;
    rebuild();
    emit selectionChanged();
    m_undoOrder << kPeopleUndo;
    emit toast(tr("Összevonva: %1").arg(r.name) + QStringLiteral(" — ")
               + tr("%n megbeszélés", "", r.meetings) + QStringLiteral(", ")
               + tr("%n minta", "", r.samples), true);
    return QString();
}

bool PeopleViewModel::deleteHasSamples() const { return m_detail.sampleCount > 0; }

QString PeopleViewModel::deleteText(bool keepSamples) const
{
    if (m_selected.isEmpty()) return QString();
    const library::DeletePreview p = m_facade->deletePreview(idOf(m_selected));
    QStringList parts;
    if (p.meetingCount <= 0)
        parts << tr("Egy megbeszélésen sem szerepel.");
    else if (!p.exampleLabel.isEmpty())
        parts << tr("%n megbeszélésen névtelen beszélőként marad meg (pl. „%1”), a szöveg nem változik.", "",
                    p.meetingCount).arg(tanara::speakerLabelText(p.exampleLabel));   // a raw key in the UI language
    else
        parts << tr("%n megbeszélésen névtelen beszélőként marad meg, a szöveg nem változik.", "",
                    p.meetingCount);
    if (p.sampleCount > 0)
        parts << (keepSamples
            ? tr("%n hangmintája megmarad egy új, névtelen személynél, akit később átnevezhetsz vagy "
                 "összevonhatsz.", "", p.sampleCount)
            : tr("%n hangmintája törlődik, így hang alapján többé nem ismerjük fel.", "", p.sampleCount));
    if (p.staleSummaries > 0)
        parts << tr("%n összefoglaló elavultnak jelölődik.", "", p.staleSummaries);
    parts << tr("A törlés nem vonható vissza.");
    return parts.join(QLatin1Char(' '));
}

QString PeopleViewModel::deletePerson(bool keepSamples)
{
    if (m_demo || m_selected.isEmpty()) return QString();
    const QString name = m_selected;
    const library::PeopleOpResult r = m_facade->removePerson(idOf(name), keepSamples);
    if (!r.ok) return r.error;
    rebuild();
    if (!r.name.isEmpty()) {
        selectPerson(r.name);
        emit toast(tr("Törölve: %1.").arg(name) + QLatin1Char(' ')
                   + tr("A mintái itt maradtak: %1").arg(r.name), false);
    } else {
        emit toast(tr("Törölve: %1").arg(name), false);
    }
    return QString();
}

void PeopleViewModel::undo()
{
    // The more recent step: if that was a tag action, on the TagService stack.
    if (!m_undoOrder.isEmpty() && m_undoOrder.last() == kTagsUndo) {
        m_undoOrder.removeLast();
        if (m_tagBackend && m_tagBackend->canUndo()) {
            const QString label = m_tagBackend->undoLabel();
            m_tagBackend->undo();
            refreshTags();
            emit undoChanged();
            emit toast(tr("Visszavonva: %1").arg(label), false);
            return;
        }
    } else if (!m_undoOrder.isEmpty()) {
        m_undoOrder.removeLast();
    }
    if (m_demo || !m_facade->canUndo()) return;
    const library::PeopleUndoInfo info = m_facade->undo();
    rebuild();
    if (!info.person.isNull())
        for (const Person& p : std::as_const(m_persons))
            if (p.id == info.person) { selectPerson(p.name); break; }
    QString text;
    switch (info.kind) {
    case library::PeopleUndoKind::SampleRemoved: text = tr("A minta visszakerült."); break;
    case library::PeopleUndoKind::SampleMoved: text = tr("A minta visszakerült a korábbi helyére."); break;
    case library::PeopleUndoKind::AliasRemoved: text = tr("Becenév visszaállítva: %1").arg(info.detail); break;
    case library::PeopleUndoKind::Renamed: text = tr("Az átnevezés visszavonva."); break;
    case library::PeopleUndoKind::Merged: text = tr("Az összevonás visszavonva: %1").arg(info.detail); break;
    case library::PeopleUndoKind::None: return;
    }
    emit toast(text, false);
}

// ---- listening to a sample ---------------------------------------------------------

void PeopleViewModel::ensureBackend()
{
    if (m_backend) return;
    m_backend = PlayerController::createBackend(this);
    connect(m_backend, &PlayerBackend::finished, this, &PeopleViewModel::finishPlayback);
    connect(m_backend, &PlayerBackend::errorOccurred, this, [this](const QString& message) {
        finishPlayback();
        emit toast(message, false);
    });
}

void PeopleViewModel::finishPlayback()
{
    m_playTick.stop();
    if (m_backend && m_backend->isPlaying()) m_backend->pause();
    if (m_playingId.isEmpty()) return;
    m_playingId.clear();
    emit playingChanged();
}

void PeopleViewModel::stopSample() { finishPlayback(); }

void PeopleViewModel::toggleSample(const QString& sampleId)
{
    if (m_demo) {
        m_playingId = m_playingId == sampleId ? QString() : sampleId;
        emit playingChanged();
        return;
    }
    if (m_playingId == sampleId) {
        finishPlayback();
        return;
    }
    QVariantMap sample;
    for (const QVariant& v : std::as_const(m_samples))
        if (v.toMap().value(QStringLiteral("id")).toString() == sampleId) { sample = v.toMap(); break; }
    if (sample.isEmpty()) return;
    if (!sample.value(QStringLiteral("playable")).toBool()) {
        emit toast(tr("A minta hangfájlja már nincs meg, ezért nem hallgatható meg."), false);
        return;
    }
    finishPlayback();
    ensureBackend();
    const qint64 start = sample.value(QStringLiteral("startMs")).toLongLong();
    const qint64 end = sample.value(QStringLiteral("endMs")).toLongLong();
    m_playEndMs = end > start ? end : 0;
    // The silent engine (test, demo) "plays" for the given length; the real one knows it from the file.
    m_backend->setSource(sample.value(QStringLiteral("path")).toString(), end > start ? end + 500 : 0);
    m_backend->setPosition(start);
    m_backend->play();
    m_playingId = sampleId;
    m_playTick.start();
    emit playingChanged();
}

} // namespace tanara_qml

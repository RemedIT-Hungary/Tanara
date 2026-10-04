#include "PeopleViewModel.h"

#include "AppContext.h"
#include "PlayerController.h"
#include "ShellFormat.h"

#include "tanara/AppController.h"
#include "tanara/edit/PeopleDirectory.h"
#include "tanara/people/PeopleService.h"
#include "tanara/people/PeopleStats.h"

#include <QCollator>
#include <QSet>

#include <algorithm>

using namespace tanara;

namespace tanara_qml {

namespace {

constexpr auto kSortAbc = "abc";
constexpr auto kSortRecent = "recent";
constexpr auto kSortMeetings = "meetings";

// Monogram: a szavak kezdőbetűi (legfeljebb kettő); a név eleji rövidítés („B. Gergő”) kimarad.
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

// A névsor szakasza ABC-rendben: a kezdőbetű; a hosszú magánhangzó az alapbetűjéhez kerül
// (Á → A, Ő → Ö), ahogy a magyar betűrend is együtt kezeli őket.
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

// A keresett szöveg helye a NÉVBEN (ékezet- és kisbetű-függetlenül). A hajtogatott alak
// karakterenként felel meg az eredetinek; ha nem (ritka, felbontott ékezetek), nincs kiemelés.
bool matchRange(const QString& name, const QString& foldedNeedle, int* pos, int* len)
{
    const QString folded = foldForSearch(name);
    const int at = folded.indexOf(foldedNeedle);
    if (at < 0) return false;
    *pos = at;
    *len = folded.size() == name.size() ? int(foldedNeedle.size()) : 0;
    return true;
}

} // namespace

PeopleViewModel::PeopleViewModel(QObject* parent) : QObject(parent)
{
    m_playTick.setInterval(50);
    connect(&m_playTick, &QTimer::timeout, this, [this] {
        if (m_backend && m_playEndMs > 0 && m_backend->position() >= m_playEndMs) finishPlayback();
    });

    AppContext* ctx = AppContext::instance();
    m_controller = ctx->controller();
    connect(ctx, &AppContext::controllerChanged, this, [this, ctx] {
        if (m_controllerInjected) return;
        m_controller = ctx->controller();
        attach();
    });
    attach();
}

PeopleViewModel::~PeopleViewModel() = default;

QObject* PeopleViewModel::controllerObject() const { return m_controller; }

void PeopleViewModel::setControllerObject(QObject* controller)
{
    setController(qobject_cast<AppController*>(controller));
}

void PeopleViewModel::setController(AppController* controller)
{
    m_controllerInjected = true;
    if (m_controller == controller && m_attached == controller) return;
    m_controller = controller;
    attach();
    emit controllerChanged();
}

void PeopleViewModel::attach()
{
    for (const QMetaObject::Connection& c : std::as_const(m_connections)) disconnect(c);
    m_connections.clear();
    m_attached = m_controller;
    m_service = m_controller ? m_controller->peopleService() : nullptr;
    m_stats = m_controller ? m_controller->peopleStats() : nullptr;
    m_demo = !m_service;
    m_selected.clear();
    m_planFor.clear();
    if (m_service) {
        AppController* c = m_controller;
        auto dirty = [this] { m_planFor.clear(); scheduleRebuild(); };
        m_connections << connect(m_service, &PeopleService::changed, this, &PeopleViewModel::scheduleRebuild);
        m_connections << connect(m_service, &PeopleService::undoChanged, this, &PeopleViewModel::undoChanged);
        m_connections << connect(m_stats, &PeopleStats::changed, this, dirty);
        m_connections << connect(c, &AppController::peopleChanged, this, &PeopleViewModel::scheduleRebuild);
        m_connections << connect(c, &AppController::voiceprintsChanged, this, dirty);
    } else {
        loadDemo();
    }
    rebuild();
    emit undoChanged();
}

void PeopleViewModel::setDemoState(const QString& state)
{
    if (m_demoState == state) return;
    m_demoState = state;
    emit demoStateChanged();
    if (!m_demo) return;
    loadDemo();
    m_selected.clear();
    if (state == QStringLiteral("P02")) m_query = QStringLiteral("ger");
    else if (state == QStringLiteral("noResult")) m_query = QStringLiteral("bár g");
    else m_query.clear();
    emit queryChanged();
    rebuild();
    if (state == QStringLiteral("P03") || state == QStringLiteral("creating"))
        setSelectedName(QStringLiteral("Tóth Eszter"));
    else if (state != QStringLiteral("P06"))
        setSelectedName(QStringLiteral("Bárány Gergely"));
    // P01: a második minta épp szól.
    m_playingId = state == QStringLiteral("P01") ? QStringLiteral("demo-1") : QString();
    emit playingChanged();
    if (state == QStringLiteral("creating")) {
        m_creating = true;
        m_creatingText = tr("%1 / %2 megbeszélés…").arg(2).arg(3);
        emit planChanged();
    }
}

void PeopleViewModel::refresh()
{
    if (!m_service) return;
    m_planFor.clear();
    rebuild();
    if (m_stats) m_stats->refresh();
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

void PeopleViewModel::rebuild()
{
    if (m_service) {
        m_service->reload();
        m_statsReady = m_stats && m_stats->ready();
        m_persons.clear();
        const QVector<PersonRecord> records = m_service->persons();
        m_persons.reserve(records.size());
        for (const PersonRecord& r : records) {
            Person p;
            p.name = r.name;
            p.isSelf = r.isSelf;
            p.aliases = r.aliases;
            p.note = r.note;
            p.sampleCount = r.sampleCount;
            if (m_statsReady) {
                const PersonStats s = m_stats->stats(r.name);
                p.meetingCount = s.meetingCount;
                p.talkMs = s.talkMs;
                p.lastSeen = s.lastSeen;
            }
            m_persons.append(p);
        }
    } else {
        m_statsReady = true;
    }

    // A kijelölés megtartása; ha a személy megszűnt, a helyén álló (vagy a saját) sor.
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
        next = p->name;   // a tárolt írásmód
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
    const QString needle = foldForSearch(m_query.trimmed());
    struct Shown { const Person* p; int pos = 0; int len = 0; QString aliasHit; };
    QVector<Shown> shown;
    for (const Person& p : std::as_const(m_persons)) {
        Shown s{&p};
        if (!needle.isEmpty() && !matchRange(p.name, needle, &s.pos, &s.len)) {
            // A névben nincs: a becenevek közt az első egyező.
            for (const QString& a : p.aliases)
                if (foldForSearch(a).contains(needle)) { s.aliasHit = a; break; }
            if (s.aliasHit.isEmpty()) continue;
        }
        shown.append(s);
    }

    QCollator collator(fmt::uiLocale());
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    auto byName = [&](const Person* a, const Person* b) { return collator.compare(a->name, b->name) < 0; };
    std::sort(shown.begin(), shown.end(), [&](const Shown& x, const Shown& y) {
        const Person* a = x.p;
        const Person* b = y.p;
        if (a->isSelf != b->isSelf) return a->isSelf;       // a saját személy mindig elöl
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
        if (!needle.isEmpty() && s.aliasHit.isEmpty() && s.len > 0) {
            r.before = s.p->name.left(s.pos);
            r.match = s.p->name.mid(s.pos, s.len);
            r.after = s.p->name.mid(s.pos + s.len);
        } else {
            r.before = s.p->name;
        }
        // Szakaszok: „Te”, utána kezdőbetűk (ABC) vagy „Többiek”; keresés közben nincsenek.
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

    if (p && m_service) {
        for (const VoiceSample& s : m_service->samples(p->name)) {
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
        if (m_statsReady && m_stats) {
            for (const PersonMeetingStat& m : m_stats->stats(p->name).meetings)
                m_meetings.append(QVariantMap{
                    {QStringLiteral("title"), m.title},
                    {QStringLiteral("date"), m.startedAt.toString(QStringLiteral("yyyy-MM-dd"))},
                    {QStringLiteral("talk"), m.talkMs > 0 ? fmt::shortDuration(m.talkMs) : QStringLiteral("–")},
                });
        }
    } else if (p) {
        int i = 0;
        for (const DemoSample& s : m_demoSamples.value(p->name)) {
            sources.insert(s.kind + QLatin1Char('|') + s.label);
            m_samples.append(QVariantMap{
                {QStringLiteral("id"), QStringLiteral("demo-%1").arg(i++)},
                {QStringLiteral("kind"), s.kind},
                {QStringLiteral("icon"), sampleIcon(s.kind)},
                {QStringLiteral("label"), s.label},
                {QStringLiteral("meetingTitle"), s.meeting},
                {QStringLiteral("date"), s.date},
                {QStringLiteral("length"), s.length},
                {QStringLiteral("playable"), true},
            });
        }
        for (const DemoMeeting& m : m_demoMeetings.value(p->name))
            m_meetings.append(QVariantMap{{QStringLiteral("title"), m.title},
                                          {QStringLiteral("date"), m.date},
                                          {QStringLiteral("talk"), m.talk}});
    }
    m_samplesSub = m_samples.isEmpty() ? QString()
        : tr("%n minta", "", int(m_samples.size())) + QStringLiteral(" · ")
          + tr("%n forrásból", "", int(sources.size()));

    // Ha a lejátszott minta már nincs a listában (törölték, más személy), a lejátszás leáll.
    if (!m_playingId.isEmpty() && !m_demo) {
        bool still = false;
        for (const QVariant& v : std::as_const(m_samples))
            if (v.toMap().value(QStringLiteral("id")).toString() == m_playingId) { still = true; break; }
        if (!still) finishPlayback();
    }
    emit detailChanged();
    refreshPlan();
}

// ---- hanglenyomat nélküli személy ---------------------------------------------

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
    if (m_demo) {
        m_planReady = m_planPossible = true;
        m_planText = tr("Kézzel rendelted hozzá %n megbeszélés sorait, ezért eddig nem ismerjük fel "
                        "magától. A biztos sorokból mintát vehetünk.", "", m_detail.meetingCount);
        m_planButton = tr("Minta %n megbeszélésből", "", m_detail.meetingCount);
        m_planNote = tr("kb. %1 hang, a gépen marad").arg(tr("%n perc", "", 2));
        emit planChanged();
        return;
    }
    if (m_planFor == m_selected && m_planReady) return;   // nem változott, ami a tervet érinti
    if (!m_statsReady) {
        m_planReady = false;
        emit planChanged();
        return;
    }
    m_planReady = false;
    emit planChanged();
    const int generation = ++m_planGeneration;
    const QString name = m_selected;
    // A megbeszélések átnézése (soronkénti anyag) pár tized másodperc lehet: a kijelölés
    // előbb megjelenik, a terv utána.
    QTimer::singleShot(0, this, [this, generation, name] {
        if (generation != m_planGeneration || !m_service) return;
        const VoiceprintPlan plan = m_service->voiceprintPlan(name);
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
    if (!m_service || m_creating || !m_planPossible || m_planMeetingIds.isEmpty()) return;
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

// Megbeszélésenként egy lépés (a hang dekódolása pár másodperc lehet): két megbeszélés
// között a felület lélegzethez jut, és a haladás látszik.
void PeopleViewModel::runNextVoiceprint()
{
    if (!m_creating) return;
    if (!m_service || m_createQueue.isEmpty()) {
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
    const VoiceprintResult r = m_service->createVoiceprintFromMeeting(m_creatingFor, meetingId);
    if (r.ok) ++m_createOk;
    else {
        ++m_createFailed;
        m_createError = r.error;
    }
    m_creatingText = tr("%1 / %2 megbeszélés…").arg(m_createOk + m_createFailed).arg(m_createTotal);
    emit planChanged();
    QTimer::singleShot(30, this, &PeopleViewModel::runNextVoiceprint);
}

// ---- lista-állapot ------------------------------------------------------------

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
    if (m_demo && m_demoState == QStringLiteral("P06")) return true;
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
    // Ha a keresés miatt nem látszana a listában, a kereső kiürül.
    if (m_list.indexOfName(m_selected) < 0) setQuery(QString());
    return true;
}

QString PeopleViewModel::selectedMonogram() const
{
    return m_selected.isEmpty() ? QString() : monogramOf(m_selected);
}

bool PeopleViewModel::personExists(const QString& name) const { return find(name.trimmed()) != nullptr; }

bool PeopleViewModel::canUndo() const { return m_service && m_service->canUndo(); }

// ---- műveletek ------------------------------------------------------------------

QString PeopleViewModel::addPerson(const QString& name)
{
    if (!m_service) return QString();
    const PeopleOpResult r = m_service->addPerson(name);
    rebuild();
    // Létező névnél is a személy lesz kijelölve (a hívó a hibát megmutatja).
    if (!r.name.isEmpty()) selectPerson(r.name);
    return r.ok ? QString() : r.error;
}

QString PeopleViewModel::rename(const QString& newName)
{
    if (!m_service || m_selected.isEmpty()) return QString();
    const QString old = m_selected;
    const PeopleOpResult r = m_service->renamePerson(old, newName);
    if (!r.ok) return r.error;
    m_selected = r.name;
    rebuild();
    emit selectionChanged();
    if (r.name != old) emit toast(tr("Átnevezve: %1").arg(r.name), true);
    return QString();
}

QString PeopleViewModel::addAlias(const QString& alias)
{
    if (!m_service || m_selected.isEmpty()) return QString();
    const PeopleOpResult r = m_service->addAlias(m_selected, alias);
    rebuild();
    return r.ok ? QString() : r.error;
}

void PeopleViewModel::removeAlias(const QString& alias)
{
    if (!m_service || m_selected.isEmpty()) return;
    if (!m_service->removeAlias(m_selected, alias)) return;
    rebuild();
    emit toast(tr("Becenév törölve: %1").arg(alias), true);
}

void PeopleViewModel::setNote(const QString& note)
{
    if (!m_service || m_selected.isEmpty() || note == m_detail.note) return;
    m_service->setNote(m_selected, note);
    rebuild();
}

void PeopleViewModel::removeSample(const QString& sampleId)
{
    if (!m_service) return;
    const PeopleOpResult r = m_service->removeSample(sampleId);
    rebuild();
    if (!r.ok) {
        emit toast(r.error, false);
        return;
    }
    const QString title = m_service->undoInfo().detail;
    emit toast(title.isEmpty() ? tr("Minta törölve") : tr("Minta törölve: %1").arg(title), true);
}

QString PeopleViewModel::moveSample(const QString& sampleId, const QString& toName)
{
    if (!m_service) return QString();
    const bool existed = m_service->exists(toName);
    const PeopleOpResult r = m_service->moveSample(sampleId, toName);
    if (!r.ok) return r.error;
    rebuild();
    emit toast(existed ? tr("Minta áthelyezve ide: %1").arg(r.name)
                       : tr("Új személy a mintából: %1").arg(r.name), true);
    return QString();
}

QVariantList PeopleViewModel::personChoices(const QString& query) const
{
    QVariantList out;
    const QString needle = foldForSearch(query.trimmed());
    struct Choice { const Person* p; QString aliasHit; };
    QVector<Choice> list;
    for (const Person& p : m_persons) {
        if (p.name == m_selected) continue;
        QString aliasHit;
        if (!needle.isEmpty() && !foldForSearch(p.name).contains(needle)) {
            for (const QString& a : p.aliases)
                if (foldForSearch(a).contains(needle)) { aliasHit = a; break; }
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
    const QString needle = foldForSearch(query.trimmed());
    for (const Person& p : m_persons) {
        if (p.name == m_selected) continue;
        QString aliasHit;
        if (!needle.isEmpty() && !foldForSearch(p.name).contains(needle)) {
            for (const QString& a : p.aliases)
                if (foldForSearch(a).contains(needle)) { aliasHit = a; break; }
            if (aliasHit.isEmpty()) continue;
        }
        int percent = -1;
        if (m_service) {
            const double s = m_service->similarity(m_selected, p.name);
            if (s >= 0) percent = qRound(s * 100);
        } else if (p.name == QStringLiteral("B. Gergő") && m_selected == QStringLiteral("Bárány Gergely")) {
            percent = 88;
        }
        list.append(Candidate{&p, percent, aliasHit});
    }
    // Hang-hasonlóság szerint (akinek nincs lenyomata, a végén), azonoson belül névsorban.
    QCollator collator(fmt::uiLocale());
    std::sort(list.begin(), list.end(), [&](const Candidate& a, const Candidate& b) {
        if (a.percent != b.percent) return a.percent > b.percent;
        return collator.compare(a.p->name, b.p->name) < 0;
    });
    QVariantList out;
    for (const Candidate& c : std::as_const(list)) {
        QString meta = rowMeta(*c.p, c.aliasHit);
        // Százalék csak akkor, ha mindkét félnek van lenyomata; „hasonló” csak érdemi egyezésnél.
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
    int meetingsN = 0, samplesN = 0, stale = 0;
    if (m_service) {
        const MergePreview p = m_service->mergePreview(loser, survivor);
        meetingsN = p.meetingCount;
        samplesN = p.sampleCount;
        stale = p.staleSummaries;
    } else {
        meetingsN = m_detail.meetingCount + o->meetingCount;
        samplesN = m_detail.sampleCount + o->sampleCount;
        stale = 3;
    }
    QString text = tr("Eredmény: <b>%1</b>, <b>%2</b>. „%3” becenév lesz.")
        .arg(tr("%n megbeszélés", "", meetingsN), tr("%n minta", "", samplesN), loser.toHtmlEscaped());
    if (stale > 0) text += QLatin1Char(' ') + tr("%n érintett összefoglaló elavultnak jelölődik.", "", stale);
    text += QLatin1Char(' ') + tr("Az összevonás nem vonható vissza.");
    return text;
}

QString PeopleViewModel::merge(const QString& other, bool keepSelected)
{
    if (!m_service || m_selected.isEmpty()) return QString();
    const QString loser = keepSelected ? other : m_selected;
    const QString survivor = keepSelected ? m_selected : other;
    const PeopleOpResult r = m_service->merge(loser, survivor);
    if (!r.ok) return r.error;
    m_selected = r.name;
    rebuild();
    emit selectionChanged();
    emit toast(tr("Összevonva: %1").arg(r.name) + QStringLiteral(" — ")
               + tr("%n megbeszélés", "", r.meetings) + QStringLiteral(", ")
               + tr("%n minta", "", r.samples), false);
    return QString();
}

bool PeopleViewModel::deleteHasSamples() const { return m_detail.sampleCount > 0; }

QString PeopleViewModel::deleteText(bool keepSamples) const
{
    if (m_selected.isEmpty()) return QString();
    DeletePreview p;
    if (m_service) {
        p = m_service->deletePreview(m_selected);
    } else {
        p.meetingCount = m_detail.meetingCount;
        p.sampleCount = m_detail.sampleCount;
        p.exampleLabel = QStringLiteral("Távoli 2");
    }
    QStringList parts;
    if (p.meetingCount <= 0)
        parts << tr("Egy megbeszélésen sem szerepel.");
    else if (!p.exampleLabel.isEmpty())
        parts << tr("%n megbeszélésen névtelen beszélőként marad meg (pl. „%1”), a szöveg nem változik.", "",
                    p.meetingCount).arg(p.exampleLabel);
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
    if (!m_service || m_selected.isEmpty()) return QString();
    const QString name = m_selected;
    const PeopleOpResult r = m_service->removePerson(name, keepSamples);
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
    if (!m_service || !m_service->canUndo()) return;
    const PeopleUndoInfo info = m_service->undo();
    rebuild();
    if (!info.person.isEmpty()) selectPerson(info.person);
    QString text;
    switch (info.kind) {
    case PeopleUndoKind::SampleRemoved: text = tr("A minta visszakerült."); break;
    case PeopleUndoKind::SampleMoved: text = tr("A minta visszakerült a korábbi helyére."); break;
    case PeopleUndoKind::AliasRemoved: text = tr("Becenév visszaállítva: %1").arg(info.detail); break;
    case PeopleUndoKind::Renamed: text = tr("Az átnevezés visszavonva."); break;
    case PeopleUndoKind::None: return;
    }
    emit toast(text, false);
}

// ---- minta meghallgatása ---------------------------------------------------------

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
    // A néma motor (teszt, demó) a megadott hosszig „játszik”; a valódi a fájlból tudja.
    m_backend->setSource(sample.value(QStringLiteral("path")).toString(), end > start ? end + 500 : 0);
    m_backend->setPosition(start);
    m_backend->play();
    m_playingId = sampleId;
    m_playTick.start();
    emit playingChanged();
}

// ---- demó: kitalált személyek (design/handoff-people) -----------------------------

void PeopleViewModel::loadDemo()
{
    m_persons.clear();
    m_demoSamples.clear();
    m_demoMeetings.clear();
    struct Row { const char* name; int meetings; int samples; bool self; };
    static const Row rows[] = {
        {"Kovács Lilla", 41, 23, true}, {"Albert Noémi", 6, 3, false}, {"Antal Benedek", 2, 0, false},
        {"Bárány Gergely", 23, 9, false}, {"B. Gergő", 4, 2, false}, {"Balogh Réka", 11, 5, false},
        {"Csonka Ferenc", 3, 0, false}, {"Dénes Zsófia", 17, 6, false}, {"Erdei Márton", 8, 4, false},
        {"Fazekas Ilona", 5, 2, false}, {"Gál Bence", 12, 7, false}, {"Halász Dóra", 9, 3, false},
        {"Szilágyi Bálint", 14, 5, false}, {"Tóth Eszter", 3, 0, false},
    };
    static const DemoSample samplePool[] = {
        {QStringLiteral("mic"), QStringLiteral("Trust USB mikrofon"),
         QStringLiteral("Termékcsapat heti egyeztetés – Q4 ütemterv és a mobilos kiadás kockázatai"),
         QStringLiteral("2026-10-01"), QStringLiteral("0:42")},
        {QStringLiteral("call"), QStringLiteral("Hívás hangja · Teams"),
         QStringLiteral("Northwind átadás: ügyféltámogatási folyamatok, eszkalációs szintek és SLA"),
         QStringLiteral("2026-09-30"), QStringLiteral("1:15")},
        {QStringLiteral("call"), QStringLiteral("Hívás hangja · Zoom"), QStringLiteral("Partnerdemó – adatimport"),
         QStringLiteral("2026-09-24"), QStringLiteral("0:38")},
        {QStringLiteral("unknown"), QStringLiteral("ismeretlen eszköz"),
         QStringLiteral("Régi felvétel importálva (hangfájl)"), QStringLiteral("2026-08-28"), QStringLiteral("0:51")},
        {QStringLiteral("mic"), QStringLiteral("Trust USB mikrofon"), QStringLiteral("Belső retró, szeptember"),
         QStringLiteral("2026-08-18"), QStringLiteral("0:27")},
        {QStringLiteral("call"), QStringLiteral("Hívás hangja · Teams"), QStringLiteral("Roadmap-áttekintés"),
         QStringLiteral("2026-08-11"), QStringLiteral("0:33")},
        {QStringLiteral("mix"), QStringLiteral("A megbeszélés lekevert hangja"),
         QStringLiteral("Kiadási egyeztetés"), QStringLiteral("2026-08-04"), QStringLiteral("0:19")},
        {QStringLiteral("mic"), QStringLiteral("Trust USB mikrofon"), QStringLiteral("Sprint tervezés"),
         QStringLiteral("2026-07-28"), QStringLiteral("0:46")},
        {QStringLiteral("call"), QStringLiteral("Hívás hangja · Zoom"), QStringLiteral("Hibajavítási kör"),
         QStringLiteral("2026-07-21"), QStringLiteral("0:24")},
    };
    static const DemoMeeting meetingPool[] = {
        {QStringLiteral("Termékcsapat heti egyeztetés – Q4 ütemterv és a mobilos kiadás kockázatai"),
         QStringLiteral("2026-10-01"), QStringLiteral("18 p")},
        {QStringLiteral("Northwind átadás: ügyféltámogatási folyamatok, eszkalációs szintek és SLA"),
         QStringLiteral("2026-09-30"), QStringLiteral("31 p")},
        {QStringLiteral("Partnerdemó – adatimport"), QStringLiteral("2026-09-24"), QStringLiteral("9 p")},
        {QStringLiteral("Belső retró, szeptember"), QStringLiteral("2026-09-18"), QStringLiteral("12 p")},
        {QStringLiteral("Roadmap-áttekintés"), QStringLiteral("2026-09-11"), QStringLiteral("22 p")},
        {QStringLiteral("Kiadási egyeztetés"), QStringLiteral("2026-09-04"), QStringLiteral("7 p")},
        {QStringLiteral("Sprint tervezés"), QStringLiteral("2026-08-28"), QStringLiteral("15 p")},
        {QStringLiteral("Hibajavítási kör"), QStringLiteral("2026-08-21"), QStringLiteral("5 p")},
    };
    const int samplePoolSize = int(std::size(samplePool));
    const int meetingPoolSize = int(std::size(meetingPool));
    const bool empty = m_demoState == QStringLiteral("P06");
    int n = 0;
    for (const Row& row : rows) {
        if (empty && !row.self) continue;
        Person p;
        p.name = QString::fromUtf8(row.name);
        p.isSelf = row.self;
        p.meetingCount = row.meetings;
        p.sampleCount = row.samples;
        p.talkMs = qint64(row.meetings) * 14 * 60000;
        p.lastSeen = QDateTime(QDate(2026, 10, 1).addDays(-(n * 3) % 40), QTime(10, 0));
        QVector<DemoSample> samples;
        for (int i = 0; i < row.samples; ++i) samples.append(samplePool[i % samplePoolSize]);
        QVector<DemoMeeting> meetings;
        for (int i = 0; i < row.meetings; ++i) meetings.append(meetingPool[i % meetingPoolSize]);
        if (p.name == QStringLiteral("Bárány Gergely")) {
            p.aliases = {QStringLiteral("Gergely"), QStringLiteral("G. Bárány")};
            p.note = QStringLiteral("Northwind oldali projektvezető. Gyakran telefonról csatlakozik, ilyenkor halkabb.");
            p.talkMs = (6 * 60 + 12) * 60000;
            p.lastSeen = QDateTime(QDate(2026, 10, 1), QTime(10, 0));
        } else if (p.name == QStringLiteral("Tóth Eszter")) {
            p.aliases = {QStringLiteral("Eszti")};
            p.talkMs = 22 * 60000;
            p.lastSeen = QDateTime(QDate(2026, 9, 26), QTime(10, 0));
            meetings = {{QStringLiteral("Pénzügyi zárás, szeptember"), QStringLiteral("2026-09-26"), QStringLiteral("8 p")},
                        {QStringLiteral("Beszállítói egyeztetés – Contoso"), QStringLiteral("2026-09-12"), QStringLiteral("11 p")},
                        {QStringLiteral("Kickoff: új számlázó"), QStringLiteral("2026-09-03"), QStringLiteral("3 p")}};
        } else if (p.name == QStringLiteral("Gál Bence")) {
            p.aliases = {QStringLiteral("Gergő kollégája")};
        }
        m_demoSamples.insert(p.name, samples);
        m_demoMeetings.insert(p.name, meetings);
        m_persons.append(p);
        ++n;
    }
}

} // namespace tanara_qml

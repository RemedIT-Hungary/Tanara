#include "demo/SummaryDemoFacade.h"

#include "demo/DemoText.h"

#include <QUuid>

#include <algorithm>

namespace tanara_demo {

using namespace tanara;
using namespace tanara::processing;

namespace {

constexpr qint64 minSec(int m, int s) { return qint64(m * 60 + s) * 1000; }

const QStringList& sourcesStates()
{
    static const QStringList v3{QStringLiteral("sourcesOn"), QStringLiteral("sourcesOff"),
                                QStringLiteral("sourcePopover"), QStringLiteral("memoSections"),
                                QStringLiteral("staleTargeted"), QStringLiteral("noSources")};
    return v3;
}

// The A family's speakers (the 76-minute meeting), by speaking time.
QVector<SummarySpeaker> doneSpeakers()
{
    return {
        {demoName("Kovács Lilla"), demoName("Kovács Lilla"), 0, 0.34}, {demoName("Tóth Bence"), demoName("Tóth Bence"), 1, 0.22},
        {demoName("Varga Nóra"), demoName("Varga Nóra"), 2, 0.17},     {demoName("Molnár Eszter"), demoName("Molnár Eszter"), 3, 0.14},
        {demoName("Szabó Áron"), demoName("Szabó Áron"), 4, 0.08},     {demoRaw(QStringLiteral("Távoli 2")), QString(), 5, 0.05},
    };
}

// The B family's speakers, in the design's order (not by share).
QVector<SummarySpeaker> sourcesSpeakers()
{
    return {
        {demoName("Kovács Lilla"), demoName("Kovács Lilla"), 0, 0.22}, {demoName("Fehér Gábor"), demoName("Fehér Gábor"), 1, 0.26},
        {demoName("Varga Árpád"), demoName("Varga Árpád"), 2, 0.19},   {demoName("Molnár Eszter"), demoName("Molnár Eszter"), 3, 0.15},
        {demoRaw(QStringLiteral("Távoli 1")), QString(), 5, 0.07},
    };
}

} // namespace

SummaryDemoFacade::SummaryDemoFacade(const QString& scenario, QObject* parent)
    : ISummaryFacade(parent), m_state(scenario.isEmpty() ? QStringLiteral("stale") : scenario)
{
    if (m_state == QLatin1String("topics"))
        loadDemoTopics();
}

std::optional<SummaryUiPreset> SummaryDemoFacade::uiPreset() const
{
    // The memo demo states show the memo (for screenshots); the others the short form.
    SummaryUiPreset p;
    const bool memoV3 = m_state == QLatin1String("memoSections");
    p.memo = m_state == QLatin1String("memo") || m_state == QLatin1String("memoShort")
          || m_state == QLatin1String("oldMemo") || memoV3;
    p.noteOpen = m_state == QLatin1String("noteOpen");
    // The selected statement / section and the Sources switch, for screenshots.
    p.sourcesVisible = m_state != QLatin1String("sourcesOff");
    if (m_state == QLatin1String("sourcesOn") || m_state == QLatin1String("sourcePopover"))
        p.activeStatementId = QStringLiteral("s1");
    p.activeSection = memoV3 ? 2 : -1;
    return p;
}

SummaryData SummaryDemoFacade::load(const QString&) const
{
    SummaryData d = build();
    if (m_staleDismissed)
        d.stale.stale = false;
    if (m_errorCleared) {
        d.summaryError = JobError();
        d.errorKeptParts = false;
    }
    for (SummaryStatement& st : d.statements)
        if (m_flagged.contains(st.id))
            st.flagged = true;
    for (SummaryStatement& st : d.document.summary.statements)
        if (m_flagged.contains(st.id))
            st.flagged = true;
    return d;
}

SummaryJobs SummaryDemoFacade::jobs(const QString&) const
{
    return build().jobs;
}

SummarySpeakers SummaryDemoFacade::speakers(const QString&) const
{
    return build().speakers;
}

SummaryQuote SummaryDemoFacade::quote(const QString&, const QString& utteranceId) const
{
    // Fictional utterances for the quotes (id: "u<startMs>"), the B family's.
    struct Line { qint64 at; qint64 len; int speaker; QString text; };
    const QVector<Line> lines{
        {minSec(0, 4), 9000, 0, tr("Az első és legfontosabb: a támogatási jegyek száma harmadával csökkent, miközben az aktív felhasználók száma nagyjából tizenkét százalékkal nőtt.")},
        {minSec(1, 16), 5000, 0, tr("Az összesre. Az újaknál még jobb az arány, de ott kicsi a minta.")},
        {minSec(4, 18), 8000, 1, tr("A súgóoldalak júliusban mentek élesbe, és az első hat hétben a keresések kétharmada ott ért véget.")},
        {minSec(12, 41), 7000, 2, tr("Nálunk a beállítási kérdések szinte eltűntek, a számlázásiak viszont maradtak, főleg a Nordvik-ügyfeleknél.")},
        {minSec(13, 1), 6000, 1, tr("Az első változatot megcsinálom a dokumentációs csapattal, október közepére.")},
        {minSec(13, 12), 6000, 0, tr("A negyedik negyedév elejére kell, különben a megújításnál újra előjön.")},
        {minSec(22, 52), 5000, 0, tr("Akkor a súgóoldalakat a többi termékre is kiterjesztjük.")},
        {minSec(31, 10), 7000, 0, tr("Én a számlázást javasolnám következő súgó-témának, ott a jegyek fele számlázási.")},
        {minSec(33, 40), 6000, 1, tr("Vállalom, de két hét dokumentációs kapacitás kell hozzá.")},
        {minSec(36, 2), 7000, 3, tr("A számlázási kérdések felét a díjbekérők adják; ha azokat külön kezeljük, a maradék is eltűnhet.")},
        {minSec(39, 20), 6000, 2, tr("Ha a súgó nem lesz kész a negyedév elejére, a megújításnál újra előjön ugyanez.")},
        {minSec(41, 5), 5000, 0, tr("A Nordvik-tárgyalás pont akkor indul, addig kell valami kézzelfogható.")},
        {minSec(42, 48), 6000, 1, tr("A partnerportálon a leggyakoribb keresés a „számla letöltése”.")},
        {minSec(43, 30), 7000, 3, tr("És ki adja a dokumentációs kapacitást, ha közben a kiterjesztésen dolgoztok?")},
        {minSec(47, 2), 6000, 3, tr("A díjbekérős kérdéseket érdemes lenne külön kezelni a súgóban.")},
        {minSec(50, 15), 6000, 4, tr("A nyomtatható változatot többen kérdezték, de nem tudom, megéri-e.")},
        {minSec(58, 40), 5000, 2, tr("A létszámról a költségvetési tervezésnél döntünk.")},
    };
    if (!sourcesStates().contains(m_state))
        return {};
    const QVector<SummarySpeaker> sp = sourcesSpeakers();
    for (const Line& l : lines) {
        if (QStringLiteral("u%1").arg(l.at) != utteranceId)
            continue;
        const SummarySpeaker& s = sp.at(l.speaker);
        SummaryQuote q;
        q.startMs = l.at;
        q.endMs = l.at + l.len;
        q.text = l.text;
        q.name = s.name;
        q.colorIndex = s.colorIndex;
        return q;
    }
    return {};
}

QString SummaryDemoFacade::chatProviderName(const QString&) const
{
    return QStringLiteral("LM Studio");
}

bool SummaryDemoFacade::flagStatement(const QString&, const QString& statementId)
{
    m_flagged.insert(statementId);
    return true;
}

void SummaryDemoFacade::dismissStale(const QString& meetingId)
{
    m_staleDismissed = true;
    emit changed(meetingId);
}

void SummaryDemoFacade::clearError(const QString& meetingId)
{
    m_errorCleared = true;
    emit changed(meetingId);
}

// ---- the per-topic workspace -------------------------------------------

QVector<SummaryTopic> SummaryDemoFacade::topics(const QString&) const { return m_topics; }

QVector<TopicStatus> SummaryDemoFacade::topicStatuses(const QString&) const
{
    QVector<TopicStatus> out;
    for (const SummaryTopic& t : m_topics) {
        TopicStatus s = m_statuses.value(t.id);
        s.topicId = t.id;
        out << s;
    }
    return out;
}

QVector<TopicAnalysis> SummaryDemoFacade::topicAnalyses(const QString&) const
{
    QVector<TopicAnalysis> out;
    for (const SummaryTopic& t : m_topics)
        if (m_analyses.contains(t.id))
            out << m_analyses.value(t.id);
    return out;
}

void SummaryDemoFacade::setTopics(const QString& meetingId, const QVector<SummaryTopic>& topics)
{
    QVector<SummaryTopic> kept;
    for (SummaryTopic t : topics) {
        t.title = t.title.trimmed();
        if (t.title.isEmpty())
            continue;
        if (t.id.isEmpty())
            t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        kept << t;
    }
    m_topics = kept;
    emit topicsChanged(meetingId);
}

// Fictional topic cards: two done, one running, one failed, one waiting.
void SummaryDemoFacade::loadDemoTopics()
{
    auto make = [this](const char* id, const QString& title, const QString& summary, TopicState st) {
        m_topics << SummaryTopic{QString::fromLatin1(id), title, summary};
        TopicStatus s;
        s.topicId = QString::fromLatin1(id);
        s.state = st;
        m_statuses.insert(s.topicId, s);
        return s.topicId;
    };
    const QString a = make("demo-1", tr("Támogatási igény alakulása"),
                           tr("Jegyszám, felhasználónkénti arány, okok"), TopicState::Done);
    TopicAnalysis ra;
    ra.topicId = a;
    ra.detail = tr("A jegyek száma harmadával csökkent, miközben az aktív felhasználók 12%-kal "
                   "nőttek. A csökkenés fő oka a termékbe épített súgó; a partnerek ugyanezt "
                   "tapasztalják.");
    ra.decisions = QStringList{tr("A súgóoldalakat a többi termékre is kiterjesztik.")};
    ra.openQuestions = QStringList{tr("Legyen-e nyomtatható változata a súgóoldalaknak?")};
    m_analyses.insert(a, ra);
    const QString b = make("demo-2", tr("Súgóoldalak kiterjesztése"),
                           tr("Melyik termékre, milyen sorrendben"), TopicState::Done);
    TopicAnalysis rb;
    rb.topicId = b;
    rb.detail = tr("A beállítási oldalak mintáját viszik tovább. A számlázás előtt a folyamatot "
                   "egyszerűsítik.");
    m_analyses.insert(b, rb);
    make("demo-3", tr("Számlázási folyamat"), tr("Termék vagy dokumentáció a gond?"), TopicState::Running);
    const QString d = make("demo-4", tr("Dokumentációs kapacitás"), tr("Létszámigény és költségvetés"),
                           TopicState::Failed);
    m_statuses[d].error = tr("A szolgáltató időtúllépés miatt megszakította.");
    make("demo-5", tr("Következő negyedév mérőszámai"), tr("Mit és hogyan mérünk"), TopicState::Waiting);
}

// ---- the fixtures ------------------------------------------------------

SummaryNotePin SummaryDemoFacade::notePin() const
{
    // The note: fictional, with spelling fixes; the "emptyNote" / "noteOpen" state with
    // suggestions from earlier, similarly titled meetings.
    SummaryNotePin n;
    if (m_state == QLatin1String("emptyNote")) {
        n.detectedCall = QStringLiteral("Microsoft Teams");
        n.suggestions = true;
    } else if (m_state == QLatin1String("noteOpen")) {
        n.note = tr("Negyedéves partner-egyeztetés: támogatási igény, súgóoldalak, számlázás. "
                    "A „Szúgó Pont” helyesen: SúgóPont.");
        n.suggestions = true;
    } else {
        n.note = tr("Negyedéves partner-egyeztetés: támogatási igény, súgóoldalak, számlázás. "
                    "A „Szúgó Pont” helyesen: SúgóPont.");
    }
    return n;
}

SummaryData SummaryDemoFacade::build() const
{
    SummaryData d;
    d.valid = true;
    d.readiness.runnable = true;
    d.providerName = QStringLiteral("LM Studio");
    d.topicsOpen = m_state == QLatin1String("topics");
    d.document.meta.providerId = QStringLiteral("lmstudio");

    if (sourcesStates().contains(m_state)) {
        buildSources(d);
        return d;
    }
    d.durationMs = 76 * 60 * 1000 + 4000;
    d.note = notePin();
    if (m_state.startsWith(QLatin1String("empty"))) {
        buildEmpty(d);
        return d;
    }
    buildDone(d);
    return d;
}

// A running summary with the core's phases (the mapping is the same as live).
JobProgress SummaryDemoFacade::demoJob(const QString& kind) const
{
    JobProgress job;
    job.meetingId = QStringLiteral("demo");
    job.kind = JobKind::Summarize;
    job.title = tr("Összefoglaló készítése");
    if (kind == QLatin1String("single")) {
        job.stages = {{QStringLiteral("single"), tr("Összefoglalás"), StageState::Running, -1, QString()}};
    } else {
        const bool merging = kind == QLatin1String("merge");
        job.done = merging ? 6 : 3;
        job.total = 6;
        job.stages = {
            {QStringLiteral("notes"), tr("Jegyzetelés részenként"),
             merging ? StageState::Done : StageState::Running, -1,
             merging ? tr("%n rész kész", nullptr, 6)
                     : tr("%1/%2. rész").arg(4).arg(6) + tr(" · %n korábbi futásból", nullptr, 2)},
            {QStringLiteral("merge"), tr("Összegzés"),
             merging ? StageState::Running : StageState::Waiting, -1, QString()},
        };
    }
    return job;
}

void SummaryDemoFacade::buildEmpty(SummaryData& d) const
{
    // The transcript line quotes 4 speakers and 412 utterances although no summary (or list) shows them.
    d.speakers.speakerCount = 4;
    d.speakers.utteranceCount = 412;
    const QString& st = m_state;
    if (st == QLatin1String("emptyBlocked")) {
        d.readiness.runnable = false;
        d.readiness.blockerKind = providers::BlockerKind::ProviderConfig;
        d.readiness.detail = tr("Hiányzik: Cím (URL) (OpenAI-kompatibilis)");
    } else if (st == QLatin1String("emptyRunning")) {
        d.jobs.shown = demoJob(QStringLiteral("single"));
    } else if (st == QLatin1String("emptyRunningParts")) {
        d.jobs.shown = demoJob(QStringLiteral("notes"));
    } else if (st == QLatin1String("emptyRunningMerge")) {
        d.jobs.shown = demoJob(QStringLiteral("merge"));
    } else if (st == QLatin1String("emptyError")) {
        d.summaryError.message = tr("A szolgáltató nem válaszolt időben. Próbáld újra.");
        d.summaryError.detail = QStringLiteral("HTTP 504 · gateway_timeout");
    } else if (st == QLatin1String("emptyErrorKept")) {
        d.summaryError.message = tr("A szolgáltatónál hiba történt. Próbáld újra később.");
        d.summaryError.detail = QStringLiteral("HTTP 500 · server_error · model crashed");
        d.errorKeptParts = true;
    } else if (st == QLatin1String("emptyErrorContext")) {
        // The classified context error (LM Studio): fix buttons per the hint.
        d.summaryError.message = tr("A modell 4096 tokenes kontextussal van betöltve, a kérés 6042 token volt — nem fér bele. "
                                    "A Tanara újra tudja tölteni az LM Studióban legalább 16 384 tokenes kontextussal, és újraindítja a feladatot.");
        d.summaryError.detail = QStringLiteral("HTTP 400 · exceed_context_size_error · ") + tr("kérés 6042 token · kontextus 4096 token");
        d.summaryError.fixActionHint = QStringLiteral("llm:reload-context:16384");
    }
}

QVector<MemoSection> SummaryDemoFacade::demoMemo(bool longForm) const
{
    // The demo's memo: 24 sections of a 76-minute meeting, or (short variant) the first four.
    struct Row { qint64 from; qint64 to; QString title; QStringList points; };
    auto t = [](int m, int s) { return minSec(m, s); };
    const QVector<Row> rows{
        {t(0, 0), t(2, 40), tr("Nyitás, napirend"),
         {demoText("Kovács Lilla összefoglalta a negyedév fő számait és a mai napirendet.", "Emma Carter summed up the main figures of the quarter and today’s agenda."),
          tr("A partnerek kérték, hogy a számlázás külön napirendi pont legyen.")}},
        {t(2, 40), t(7, 15), tr("Támogatási jegyek alakulása"),
         {tr("A jegyek száma harmadával csökkent az előző negyedévhez képest."),
          tr("Az aktív felhasználók száma közben 12%-kal nőtt."),
          tr("A csökkenés nagyobb része a beállítási kérdéseknél jelentkezett.")}},
        {t(7, 15), t(12, 52), tr("A súgóoldalak hatása"),
         {demoText("Tóth Bence szerint a termékbe épített súgó a beállítási kérdések felét kiváltotta.", "According to Ryan Mitchell, the in-product help replaced half of the setup questions."),
          tr("A partnerek ugyanezt tapasztalják a saját ügyfélszolgálatukon."),
          tr("A súgót a többi termékre is kiterjesztik.")}},
        {t(12, 52), t(17, 30), tr("A kiterjesztés sorrendje"),
         {tr("Elsőként a riportmodul kap súgóoldalakat, utána az integrációk."),
          demoText("Kovács Lilla október 15-ig ütemtervet készít.", "Emma Carter prepares a schedule by October 15.")}},
        {t(17, 30), t(22, 5), tr("Számlázási panaszok"),
         {tr("A számlázási jegyek száma nem csökkent; a panaszok fele a számla módosításáról szól."),
          demoText("Varga Nóra szerint a leírás rendben van, a folyamat hosszú.", "According to Olivia Grant, the documentation is fine; the process is long.")}},
        {t(22, 5), t(26, 40), tr("Folyamat vagy dokumentáció"),
         {tr("Két partner a lépések számát tartja a fő gondnak."),
          tr("Abban maradtak, hogy előbb a folyamatot egyszerűsítik, utána a leírást.")}},
        {t(26, 40), t(31, 10), tr("Elakadási pontok gyűjtése"),
         {demoText("Varga Nóra október 10-ig összegyűjti, hol akadnak el a felhasználók.", "Olivia Grant collects where users get stuck by October 10."),
          tr("A gyűjtéshez a jegyek címkéit és a munkamenet-felvételeket használják.")}},
        {t(31, 10), t(35, 20), tr("Új ügyfelek bevezetése"),
         {tr("Az új ügyfelek az első két hétben háromszor annyi jegyet nyitnak."),
          demoText("Molnár Eszter bontást kért ügyféltípus szerint.", "Sophie Bennett asked for a breakdown by customer type.")}},
        {t(35, 20), t(39, 45), tr("Bevezető levelek"),
         {tr("A bevezető levelek megnyitási aránya 40% alatt van."),
          tr("Javaslat: a levelek a súgóoldalakra mutassanak, ne a dokumentációra.")}},
        {t(39, 45), t(43, 30), tr("Partneri visszajelzések"),
         {tr("A partnerek a keresőt dicsérték, a nyomtatható változatot hiányolják.")}},
        {t(43, 30), t(46, 50), tr("Nyomtatható súgó"),
         {tr("Nem döntöttek; a nyomtatható változat igényét előbb felmérik.")}},
        {t(46, 50), t(51, 40), tr("Dokumentációs kapacitás"),
         {tr("A dokumentációs csapat két fővel dolgozik, ez a kiterjesztéshez kevés."),
          demoText("Szabó Áron szerint legalább egy fő kellene a következő félévre.", "According to Lucas Reed, at least one more person is needed for the next half-year.")}},
        {t(51, 40), t(55, 15), tr("Költségvetési keret"),
         {tr("A létszámbővítés legkorábban a jövő évi tervben szerepelhet.")}},
        {t(55, 15), t(58, 40), tr("Döntés a létszámról"),
         {tr("A dokumentációs létszámról a költségvetési tervezés dönt."),
          demoText("Szabó Áron október 20-ig beadja az igényt.", "Lucas Reed submits the request by October 20.")}},
        {t(58, 40), t(61, 30), tr("Külső szövegíró"),
         {tr("Átmeneti megoldásként felmerült egy külső szövegíró; nem döntöttek.")}},
        {t(61, 30), t(64, 10), tr("Mérőszámok"),
         {tr("A jegyszám mellett a megoldási időt is mérik; ez nem javult.")}},
        {t(64, 10), t(67, 0), tr("A következő negyedév céljai"),
         {tr("Cél a számlázási jegyek 20%-os csökkentése.")}},
        {t(67, 0), t(69, 20), tr("Riportok a partnereknek"),
         {tr("A partnerek havi bontású riportot kérnek a jegyekről.")}},
        {t(69, 20), t(71, 0), tr("A riport formája"),
         {tr("A riport táblázatként megy ki, grafikon nélkül.")}},
        {t(71, 0), t(72, 30), tr("Partnerportál"),
         {tr("A partnerportál frissítése a következő negyedévre csúszik.")}},
        {t(72, 30), t(73, 40), tr("Kapcsolattartók"),
         {tr("Minden partnernél egy állandó kapcsolattartó lesz.")}},
        {t(73, 40), t(74, 40), tr("Új-ügyfél bontás"),
         {demoText("Kovács Lilla október 4-ig elküldi a bontást a partnereknek.", "Emma Carter sends the breakdown to the partners by October 4.")}},
        {t(74, 40), t(75, 30), tr("Nyitva maradt ügyek"),
         {tr("Nyitva maradt a nyomtatható súgó és a külső szövegíró kérdése.")}},
        {t(75, 30), t(76, 4), tr("Zárás"),
         {tr("A következő partnertalálkozó januárban lesz.")}},
    };
    const int n = longForm ? int(rows.size()) : 4;
    QVector<MemoSection> memo;
    for (int i = 0; i < n; ++i)
        memo.append({rows[i].title, rows[i].from, rows[i].to, rows[i].points});
    return memo;
}

// A finished summary (the A family): matching the design's three summary screens.
void SummaryDemoFacade::buildDone(SummaryData& d) const
{
    const QString& st = m_state;
    d.speakers.list = doneSpeakers();
    d.speakers.speakerCount = 4;       // the transcript line quotes 4 speakers (the design's number)
    d.speakers.utteranceCount = 412;

    // Old (pre-memo) summary: no memo, no open questions, no model data either.
    const bool old = st == QLatin1String("oldSummary") || st == QLatin1String("oldMemo");
    Summary& s = d.document.summary;
    s.execSummary = tr("A partnerek megerősítették, hogy a harmadik negyedévben a támogatási igény "
                       "harmadával csökkent, főként az új súgóoldalak miatt. A súgót a többi termékre "
                       "is kiterjesztik, de a számlázásnál előbb a folyamatot egyszerűsítik. A "
                       "dokumentációs létszámbővítésről a költségvetés dönt.");
    s.decisions = QStringList{
        tr("[12:52] A súgóoldalakat a többi termékre is kiterjesztik."),
        tr("[31:10] A számlázásnál előbb a folyamatot egyszerűsítik, utána a leírást."),
        tr("[58:40] A dokumentációs létszámról a költségvetési tervezés dönt."),
    };
    if (!old)
        s.openQuestions = QStringList{
            tr("[43:30] Legyen-e nyomtatható változata a súgóoldalaknak?"),
            tr("[58:40] Bevonjanak-e átmenetileg külső szövegírót a dokumentációhoz?"),
            tr("Mikorra frissül a partnerportál?"),
        };
    s.actionItems = {
        {tr("A súgóoldalak kiterjesztése a további termékekre: ütemterv"), demoName("Kovács Lilla"), tr("okt. 15.")},
        {tr("Számlázási elakadási pontok összegyűjtése"), demoName("Varga Nóra"), tr("okt. 10.")},
        {tr("Dokumentációs létszámigény a költségvetési tervbe"), demoName("Szabó Áron"), tr("okt. 20.")},
        {tr("Új-ügyfél bontás elküldése a partnereknek"), demoName("Kovács Lilla"), tr("okt. 4.")},
    };
    for (const SummarySpeaker& sp : std::as_const(d.speakers.list))
        s.participants << sp.name;
    if (!old && st != QLatin1String("topicsDoc"))
        s.memo = demoMemo(st != QLatin1String("memoShort"));

    library::SummaryDocument& doc = d.document;
    doc.exists = true;
    doc.markdown = s.renderMarkdown();
    doc.meta.mode = old ? library::SummaryMode::Unknown : library::SummaryMode::Quick;
    doc.meta.model = old ? QString() : QStringLiteral("gemma-4-12b");
    doc.meta.contextNoteKnown = true;
    doc.meta.contextNote = st == QLatin1String("noteChanged")
        ? tr("Negyedéves partner-egyeztetés: támogatási igény, súgóoldalak, számlázás.")
        : notePin().note;
    d.generatedText = demoText("okt. 1. 17:05", "Oct 1, 5:05 PM");

    d.stale.stale = st == QLatin1String("stale");
    d.stale.correctedSpeakers = d.stale.stale ? 3 : 0;

    if (st == QLatin1String("running"))
        d.jobs.shown = demoJob(QStringLiteral("notes"));

    if (st == QLatin1String("topicsDoc")) {
        doc.meta.mode = library::SummaryMode::Topics;
        TopicAnalysis a;
        a.title = tr("Támogatási igény alakulása");
        a.detail = tr("A jegyek száma harmadával csökkent, miközben az aktív "
                      "felhasználók 12%-kal nőttek. A csökkenés fő oka a "
                      "termékbe épített súgó.");
        a.decisions = QStringList{tr("A súgóoldalakat a többi termékre is kiterjesztik.")};
        a.openQuestions = QStringList{tr("Legyen-e nyomtatható változata a súgóoldalaknak?")};
        a.actionItems = {{tr("Ütemterv a kiterjesztéshez"), demoName("Kovács Lilla"), tr("okt. 15.")}};
        TopicAnalysis b;
        b.title = tr("Számlázási folyamat");
        b.detail = tr("A partnerek szerint a gond nem a leírás, hanem maga a "
                      "folyamat: túl sok lépés, kevés visszajelzés.");
        doc.topics = {a, b};
    }
}

// Sample data of the B family: "Quarterly partner meeting", 2:11:04, five speakers, with source links.
void SummaryDemoFacade::buildSources(SummaryData& d) const
{
    const QString& state = m_state;
    auto t = [](int m, int s) { return minSec(m, s); };
    d.durationMs = t(131, 4);
    d.note = SummaryNotePin{};   // an empty note block
    d.generatedText = demoText("okt. 1. 17:05", "Oct 1, 5:05 PM");
    d.providerText = QStringLiteral("LM Studio");
    d.speakers.list = sourcesSpeakers();
    d.speakers.utteranceCount = 1298;
    const QVector<SummarySpeaker>& speakers = d.speakers.list;

    library::SummaryDocument& doc = d.document;
    doc.exists = true;
    doc.meta.mode = library::SummaryMode::Quick;
    doc.meta.model = QStringLiteral("gemma-4-12b");
    Summary& sum = doc.summary;

    // The quotes' utterances start at these times (id: "u<startMs>").
    auto endOf = [this](qint64 at) { return quote(QString(), QStringLiteral("u%1").arg(at)).endMs; };
    auto span = [&](std::initializer_list<qint64> starts) {
        SourceSpan s;
        for (qint64 at : starts) {
            s.utteranceIds << QStringLiteral("u%1").arg(at);
            if (s.startMs < 0) s.startMs = at;
            s.endMs = endOf(at);
        }
        return s;
    };
    auto statement = [](const QString& id, StatementKind kind, const QString& text,
                        const QVector<SourceSpan>& spans, const QString& owner = QString()) {
        SummaryStatement st;
        st.id = id; st.kind = kind; st.text = text; st.sourceSpans = spans; st.owner = owner;
        return st;
    };

    const QString s1 = tr("A harmadik negyedévben a támogatási jegyek száma harmadával csökkent, miközben az aktív felhasználók száma 12%-kal nőtt.");
    const QString s2 = tr("A javulás fő oka a júliusban élesített súgóoldalak: az első hat hétben a keresések kétharmada ott ért véget.");
    const QString s3 = tr("A partnerek oldalán a beállítási kérdések szinte eltűntek, a számlázási kérdések viszont maradtak, főleg a Nordvik-ügyfeleknél.");
    const QString s4 = tr("A számlázási súgónak a negyedik negyedév elejére el kell készülnie, különben a megújításnál újra előjön a probléma.");
    sum.execSummary = QStringList{s1, s2, s3, s4}.join(QLatin1Char(' '));
    sum.decisions = QStringList{
        tr("A súgóoldalakat a többi termékre is kiterjesztik."),
        tr("A számlázásnál előbb a folyamatot egyszerűsítik, utána a leírást."),
        tr("A dokumentációs létszámról a költségvetési tervezés dönt."),
    };
    sum.actionItems = {
        {tr("Számlázási súgó első változata a dokumentációs csapattal"), demoName("Fehér Gábor"), tr("okt. 15.")},
        {tr("Díjbekérő-kérdések külön kezelése a súgóban"), demoName("Molnár Eszter"), tr("okt. 22.")},
    };
    for (const SummarySpeaker& sp : speakers)
        sum.participants << sp.name;

    QVector<SummaryStatement> sts{
        statement(QStringLiteral("s1"), StatementKind::Statement, s1, {span({t(0, 4), t(1, 16)})}),
        statement(QStringLiteral("s2"), StatementKind::Statement, s2, {span({t(4, 18)})}),
        statement(QStringLiteral("s3"), StatementKind::Statement, s3, {span({t(12, 41)}), span({t(47, 2)})}),
        statement(QStringLiteral("s4"), StatementKind::Statement, s4, {span({t(13, 12)})}),
        statement(QStringLiteral("d1"), StatementKind::Decision, sum.decisions.at(0), {span({t(22, 52)})}),
        statement(QStringLiteral("d2"), StatementKind::Decision, sum.decisions.at(1), {span({t(31, 10)}), span({t(33, 40)})}),
        statement(QStringLiteral("d3"), StatementKind::Decision, sum.decisions.at(2), {span({t(58, 40)})}),
        statement(QStringLiteral("t1"), StatementKind::Todo, sum.actionItems.at(0).text, {span({t(13, 1)})}, demoName("Fehér Gábor")),
        statement(QStringLiteral("t2"), StatementKind::Todo, sum.actionItems.at(1).text, {span({t(47, 2)})}, demoName("Molnár Eszter")),
    };

    if (state == QLatin1String("staleTargeted")) {
        // A line of Varga Árpád was corrected from Fehér Gábor: the affected statement and action item.
        d.stale.stale = true;
        d.stale.correctedSpeakers = 3;
        d.stale.targeted = true;
        sts[2].staleBecause = QStringList{demoText("Fehér Gábor → Varga Árpád?", "Daniel Hughes → James Walker?")};
        sts[7].staleBecause = QStringList{demoText("Fehér Gábor → Varga Árpád?", "Daniel Hughes → James Walker?")};
        sts[7].ownerStaleBecause = demoText("Fehér Gábor → Varga Árpád?", "Daniel Hughes → James Walker?");
        d.stale.affectedStatements = 1;
        d.stale.affectedTodos = 1;
        d.stale.ownerChanges = 1;
        d.stale.affectedUtteranceIds = QStringList{QStringLiteral("u%1").arg(t(12, 41)), QStringLiteral("u%1").arg(t(13, 1))};
    }

    // Open questions (as a statement, with a source chip).
    sum.openQuestions = QStringList{
        tr("Ki adja a dokumentációs kapacitást, ha a csapat a negyedik negyedévben a súgó kiterjesztésén dolgozik?"),
        tr("Megéri-e elkészíteni a súgó nyomtatható változatát?"),
    };
    sts << statement(QStringLiteral("q1"), StatementKind::OpenQuestion, sum.openQuestions.at(0), {span({t(43, 30)})})
        << statement(QStringLiteral("q2"), StatementKind::OpenQuestion, sum.openQuestions.at(1), {span({t(50, 15)})});

    // Memo: seven sections (the 3rd highlighted), with an intro and subsections.
    struct Sec { qint64 from; qint64 to; QString title; QStringList speakers; QStringList points; };
    const QString lilla = demoName("Kovács Lilla"), gabor = demoName("Fehér Gábor"), arpad = demoName("Varga Árpád"),
                  eszter = demoName("Molnár Eszter"), tavoli = demoRaw(QStringLiteral("Távoli 1"));
    const QVector<Sec> secs{
        {t(0, 0), t(12, 30), tr("Nyitás, Q3 számok"), {lilla, gabor},
         {demoText("Kovács Lilla összefoglalta a negyedév számait: a jegyek harmadával kevesebbek, az aktív felhasználók 12%-kal többen vannak.", "Emma Carter summarized the quarter's numbers: tickets are down by a third, active users are up 12%."),
          tr("A partnerek kérték, hogy a számlázás külön napirendi pont legyen.")}},
        {t(12, 30), t(31, 10), tr("Súgóoldalak"), {gabor, arpad, lilla},
         {tr("A júliusban élesített súgóoldalak az első hat hétben a keresések kétharmadát lezárták."),
          tr("A súgót a többi termékre is kiterjesztik.")}},
        {t(31, 10), t(44, 5), tr("Számlázási súgó és a Nordvik-megújítás"), {lilla, gabor, eszter},
         {tr("A díjbekérőkkel kapcsolatos kérdések a számlázási kérdések felét adják; külön kezelve a maradék is eltűnhet."),
          tr("Ha a súgó nem készül el a negyedév elejére, a megújítási tárgyalásokon újra előjön a probléma."),
          tr("A partnerportál keresési adatai alapján a „számla letöltése” a leggyakoribb keresés."),
          tr("Ki adja a dokumentációs kapacitást, ha a csapat a negyedik negyedévben a súgó kiterjesztésén dolgozik?")}},
        {t(44, 5), t(64, 40), tr("Partneri visszajelzések"), {eszter, arpad, tavoli},
         {tr("A partnerek a keresőt dicsérték, a nyomtatható változatot hiányolják."),
          tr("A díjbekérős kérdéseket külön kezelik a súgóban.")}},
        {t(64, 40), t(92, 10), tr("Nordvik-megújítás"), {arpad, lilla},
         {tr("A Nordvik-szerződés megújítása a számlázási súgón múlik."),
          tr("A tárgyalás a negyedik negyedév elején indul.")}},
        {t(92, 10), t(115, 50), tr("Költségvetés"), {lilla, arpad, gabor},
         {tr("A dokumentációs létszámról a költségvetési tervezés dönt.")}},
        {t(115, 50), t(131, 4), tr("Lezárás, teendők"), {lilla, gabor, eszter},
         {demoText("Fehér Gábor október 15-ig elkészíti a számlázási súgó első változatát.", "Daniel Hughes will finish the first version of the billing help by October 15."),
          demoText("Molnár Eszter október 22-ig kidolgozza a díjbekérő-kérdések külön kezelését.", "Sophie Bennett will work out the separate handling of proforma invoice questions by October 22.")}},
    };
    for (const Sec& s : secs)
        sum.memo.append({s.title, s.from, s.to, s.points, s.speakers});
    // The 3rd section: a two-sentence intro, "Arguments raised" and "Open question" subsections.
    MemoSection& third = sum.memo[2];
    third.intro = demoText("Kovács Lilla szerint a számlázási kérdések a jegyek felét teszik ki a Nordvik-ügyfeleknél, ezért a következő súgó-témának ezt javasolja.", "According to Emma Carter, billing questions make up half the tickets for Nordvik customers, so she suggests it as the next help topic.")
                + QLatin1Char(' ')
                + demoText("Fehér Gábor vállalta az első változatot, de két hét dokumentációs kapacitást kér hozzá.", "Daniel Hughes took on the first version, but asks for two weeks of documentation capacity.");
    third.pointGroups = QStringList{QStringLiteral("arguments"), QStringLiteral("arguments"),
                                    QStringLiteral("arguments"), QStringLiteral("open")};
    sum.memo[0].intro = tr("A negyedév számai jók, a számlázás külön napirendi pontot kapott.");
    // The memo's statements (points and intro sentences with a source; a few points without one).
    auto memoSt = [&](const QString& id, StatementKind kind, const QString& text,
                      const QVector<SourceSpan>& spans) { sts << statement(id, kind, text, spans); };
    memoSt(QStringLiteral("i1.1"), StatementKind::MemoIntro, sum.memo[0].intro, {span({t(0, 4)})});
    memoSt(QStringLiteral("m1.1"), StatementKind::MemoPoint, sum.memo[0].points.at(0), {span({t(0, 4)}), span({t(1, 16)})});
    memoSt(QStringLiteral("m1.2"), StatementKind::MemoPoint, sum.memo[0].points.at(1), {});
    memoSt(QStringLiteral("m2.1"), StatementKind::MemoPoint, sum.memo[1].points.at(0), {span({t(4, 18)})});
    memoSt(QStringLiteral("m2.2"), StatementKind::MemoPoint, sum.memo[1].points.at(1), {span({t(22, 52)})});
    memoSt(QStringLiteral("i3.1"), StatementKind::MemoIntro,
           demoText("Kovács Lilla szerint a számlázási kérdések a jegyek felét teszik ki a Nordvik-ügyfeleknél, ezért a következő súgó-témának ezt javasolja.", "According to Emma Carter, billing questions make up half the tickets for Nordvik customers, so she suggests it as the next help topic."),
           {span({t(31, 10)})});
    memoSt(QStringLiteral("i3.2"), StatementKind::MemoIntro,
           demoText("Fehér Gábor vállalta az első változatot, de két hét dokumentációs kapacitást kér hozzá.", "Daniel Hughes took on the first version, but asks for two weeks of documentation capacity."),
           {span({t(33, 40)})});
    memoSt(QStringLiteral("m3.1"), StatementKind::MemoPoint, third.points.at(0), {span({t(36, 2)})});
    memoSt(QStringLiteral("m3.2"), StatementKind::MemoPoint, third.points.at(1), {span({t(39, 20)}), span({t(41, 5)})});
    memoSt(QStringLiteral("m3.3"), StatementKind::MemoPoint, third.points.at(2), {span({t(42, 48)})});
    memoSt(QStringLiteral("m3.4"), StatementKind::MemoPoint, third.points.at(3), {span({t(43, 30)})});
    memoSt(QStringLiteral("m4.1"), StatementKind::MemoPoint, sum.memo[3].points.at(0), {span({t(50, 15)})});
    memoSt(QStringLiteral("m4.2"), StatementKind::MemoPoint, sum.memo[3].points.at(1), {span({t(47, 2)})});
    memoSt(QStringLiteral("m6.1"), StatementKind::MemoPoint, sum.memo[5].points.at(0), {span({t(58, 40)})});
    memoSt(QStringLiteral("m7.1"), StatementKind::MemoPoint, sum.memo[6].points.at(0), {span({t(13, 1)})});
    memoSt(QStringLiteral("m7.2"), StatementKind::MemoPoint, sum.memo[6].points.at(1), {span({t(47, 2)})});

    doc.markdown = sum.renderMarkdown();

    // "noSources": an old (source-less) summary, today's view.
    if (state != QLatin1String("noSources")) {
        sum.statements = sts;
        d.statements = sts;
    }
}

} // namespace tanara_demo

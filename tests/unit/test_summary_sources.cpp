//
// Összefoglaló forrás-hivatkozások — a jelölő-értelmező (`[t=mm:ss]`, hiányzó / rossz jelölő,
// ±5 s tűrés), a mondatokra bontás, a SummaryService a jelölős (hamis) LLM-válasszal, a
// summary.json oda-vissza (statements, sourceSpeakers, memó-beszélők, jelölés), a memó-szakaszok
// időkerete + beszélői, a célzott elavulás (hamis overlay-változással) és a prompt-sablonok.
// LLM nincs: a válaszok kézzel írt szövegek.
//
#include <QtTest>
#include <QTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>

#include "tanara/Types.h"
#include "tanara/PromptLibrary.h"
#include "tanara/SummaryService.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/llm/ILlmProvider.h"
#include "tanara/summary/SummaryPipeline.h"
#include "tanara/summary/SummarySources.h"
#include "tanara/summary/SummaryStore.h"

using namespace tanara;
using namespace tanara::summarysrc;

// ---- forgatókönyvezett ál-LLM (mint a test_summary-ban) ---------------------------------
class FakeJob : public LlmJob {
    Q_OBJECT
public:
    explicit FakeJob(QObject* parent) : LlmJob(parent) {}
    void cancel() override {}
};

class FakeProvider : public QObject, public ILlmProvider {
    Q_OBJECT
public:
    std::function<QString(int, const LlmRequest&)> script;
    QVector<LlmRequest> requests;
    QString name() const override { return QStringLiteral("fake"); }
    bool supportsStreaming() const override { return false; }
    LlmJob* chat(const LlmRequest& req) override {
        const int n = int(requests.size());
        requests.append(req);
        auto* job = new FakeJob(this);
        const QString text = script ? script(n, req) : QString();
        QTimer::singleShot(0, job, [job, text]() { emit job->finished(text); });
        return job;
    }
};

namespace {

SourceLine line(const QString& id, qint64 start, qint64 end, const QString& key, const QString& name = QString())
{
    SourceLine l;
    l.id = id;
    l.startMs = start;
    l.endMs = end;
    l.speakerKey = key;
    l.speakerName = name.isEmpty() ? key : name;
    return l;
}

// Három megszólalás: 1:00–1:20 Ádám, 2:00–2:30 Béla, 5:00–5:10 Ádám.
QVector<SourceLine> threeLines()
{
    return {line(QStringLiteral("u60000"), 60000, 80000, QStringLiteral("Ádám")),
            line(QStringLiteral("u120000"), 120000, 150000, QStringLiteral("Béla")),
            line(QStringLiteral("u300000"), 300000, 310000, QStringLiteral("Ádám"))};
}

// Átirat: beszélőnként váltakozó bekezdések a megadott perceknél (20 s hosszúak).
MergedTranscript transcriptAt(const QVector<double>& minutes)
{
    MergedTranscript t;
    t.language = QStringLiteral("hu");
    for (int i = 0; i < minutes.size(); ++i) {
        TranscriptToken tok;
        tok.text = QStringLiteral(" szó").repeated(20) + QStringLiteral(" %1").arg(i);
        tok.speaker = i % 2 ? QStringLiteral("Béla") : QStringLiteral("Ádám");
        tok.startMs = qint64(minutes[i] * 60000.0);
        tok.endMs = tok.startMs + 20000;
        t.tokens.append(tok);
    }
    return t;
}

bool writeJson(const QString& path, const QJsonDocument& doc)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(doc.toJson());
    return true;
}

} // namespace

class SummarySourcesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    // jelölők
    void markersExtracted_data();
    void markersExtracted();
    void badTimesAreStrippedWithoutSource();
    void textWithoutMarkersUnchanged();
    // mondatok
    void sentenceSplit();
    // feloldás
    void resolveContainsAndTolerance();
    void resolveRangeAndIdAndDedupe();
    // összerakás
    void attachSourcesBuildsStatements();
    void missingMarkerMeansNoSource();
    void memoSectionTimesAndSpeakers();
    // vezénylés hamis LLM-mel
    void serviceSingleCallWithMarkers();
    void serviceMergeWithMarkers();
    // tárolás
    void storeRoundTrip();
    void oldJsonLoadsWithoutStatements();
    void flagStatementKeepsJsonMtime();
    void handEditedMarkdownKeepsMatchingStatements();
    // célzott elavulás
    void targetedStalenessPure();
    void targetedStalenessFromOverlay();
    // prompt
    void promptTemplatesAskForMarkers();
};

void SummarySourcesTest::initTestCase()
{
    // Homokozó: a valódi ~/.tanara érintetlen.
    static QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("TANARA_HOME", home.path().toUtf8());
}

// ---- jelölők ------------------------------------------------------------------------------

void SummarySourcesTest::markersExtracted_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("clean");
    QTest::addColumn<QList<qint64>>("starts");
    QTest::addColumn<QList<qint64>>("ends");
    QTest::addColumn<QStringList>("ids");

    QTest::newRow("after punct") << QStringLiteral("Az ár 100 euró. [t=01:05]")
        << QStringLiteral("Az ár 100 euró.") << QList<qint64>{65000} << QList<qint64>{65000} << QStringList{};
    QTest::newRow("before punct") << QStringLiteral("Az ár 100 euró [t=1:05].")
        << QStringLiteral("Az ár 100 euró.") << QList<qint64>{65000} << QList<qint64>{65000} << QStringList{};
    QTest::newRow("several") << QStringLiteral("Két helyszín lesz. [t=02:00, t=05:01]")
        << QStringLiteral("Két helyszín lesz.") << QList<qint64>{120000, 301000}
        << QList<qint64>{120000, 301000} << QStringList{};
    QTest::newRow("two markers") << QStringLiteral("Két helyszín lesz. [t=02:00][t=05:01]")
        << QStringLiteral("Két helyszín lesz.") << QList<qint64>{120000, 301000}
        << QList<qint64>{120000, 301000} << QStringList{};
    QTest::newRow("range") << QStringLiteral("Vita volt. [t=02:00-02:40]")
        << QStringLiteral("Vita volt.") << QList<qint64>{120000} << QList<qint64>{160000} << QStringList{};
    QTest::newRow("hours") << QStringLiteral("Késő. [t=1:02:03]")
        << QStringLiteral("Késő.") << QList<qint64>{3723000} << QList<qint64>{3723000} << QStringList{};
    QTest::newRow("bare time") << QStringLiteral("Döntés született. [12:30]")
        << QStringLiteral("Döntés született.") << QList<qint64>{750000} << QList<qint64>{750000} << QStringList{};
    QTest::newRow("utterance id") << QStringLiteral("Elhangzott. [u120000]")
        << QStringLiteral("Elhangzott.") << QList<qint64>{-1} << QList<qint64>{-1}
        << QStringList{QStringLiteral("u120000")};
    QTest::newRow("spaces") << QStringLiteral("t= with  space [ t = 03:00 ] marad")
        << QStringLiteral("t= with space marad") << QList<qint64>{180000} << QList<qint64>{180000} << QStringList{};
}

void SummarySourcesTest::markersExtracted()
{
    QFETCH(QString, input);
    QFETCH(QString, clean);
    QFETCH(QList<qint64>, starts);
    QFETCH(QList<qint64>, ends);
    QFETCH(QStringList, ids);
    const MarkedText mt = extractMarkers(input);
    QCOMPARE(mt.text, clean);
    QCOMPARE(mt.refs.size(), starts.size());
    QStringList gotIds;
    for (int i = 0; i < mt.refs.size(); ++i) {
        QCOMPARE(mt.refs[i].startMs, starts[i]);
        QCOMPARE(mt.refs[i].endMs, ends[i]);
        if (!mt.refs[i].utteranceId.isEmpty()) gotIds << mt.refs[i].utteranceId;
    }
    QCOMPARE(gotIds, ids);
}

void SummarySourcesTest::badTimesAreStrippedWithoutSource()
{
    // Rossz idő: a jelölő lekerül, hivatkozás nem lesz.
    for (const QString& in : {QStringLiteral("Mondat. [t=12:7]"), QStringLiteral("Mondat. [t=12:75]"),
                              QStringLiteral("Mondat. [t=abc]"), QStringLiteral("Mondat. [t=]"),
                              QStringLiteral("Mondat. [t=1:61:00]")}) {
        const MarkedText mt = extractMarkers(in);
        QCOMPARE(mt.text, QStringLiteral("Mondat."));
        QVERIFY2(mt.refs.isEmpty(), qPrintable(in));
    }
    // Jól formált, de az átiraton kívüli idő: nincs megszólalás a tűrésen belül → nincs forrás.
    QVERIFY(resolveRefs(extractMarkers(QStringLiteral("x [t=59:00]")).refs, threeLines()).isEmpty());
}

void SummarySourcesTest::textWithoutMarkersUnchanged()
{
    const QString t = QStringLiteral("Első bekezdés, 14:00-kor kezdünk.\n\nMásodik [link] bekezdés.");
    QCOMPARE(stripMarkers(t), t);
    QVERIFY(extractMarkers(t).refs.isEmpty());
}

// ---- mondatok -----------------------------------------------------------------------------

void SummarySourcesTest::sentenceSplit()
{
    const QVector<MarkedText> s = splitSentences(QStringLiteral(
        "A pilot két helyszínen indul. [t=01:05] Dr. Kovács jóváhagyta az árat [t=02:10]. "
        "Az ár kb. 100 euró, ill. több? [t=05:00]\n\nÚj bekezdés jelölő nélkül 2026. október 1-jén"));
    QCOMPARE(s.size(), 4);
    QCOMPARE(s[0].text, QStringLiteral("A pilot két helyszínen indul."));
    QCOMPARE(s[0].refs.size(), 1);
    QCOMPARE(s[0].refs[0].startMs, 65000);
    QCOMPARE(s[1].text, QStringLiteral("Dr. Kovács jóváhagyta az árat."));
    QCOMPARE(s[1].refs.value(0).startMs, 130000);
    QCOMPARE(s[2].text, QStringLiteral("Az ár kb. 100 euró, ill. több?"));
    QCOMPARE(s[2].refs.value(0).startMs, 300000);
    QCOMPARE(s[3].text, QStringLiteral("Új bekezdés jelölő nélkül 2026. október 1-jén"));
    QVERIFY(s[3].refs.isEmpty());
}

// ---- feloldás -----------------------------------------------------------------------------

void SummarySourcesTest::resolveContainsAndTolerance()
{
    const QVector<SourceLine> lines = threeLines();
    auto at = [&](const QString& marker) { return resolveRefs(extractMarkers(marker).refs, lines); };
    // A megszólalás belsejében.
    QCOMPARE(at(QStringLiteral("[t=01:10]")).value(0).utteranceIds, QStringList{QStringLiteral("u60000")});
    // A vége után 4 s (1:24) → még az első sor; a következő kezdete előtt 4 s (1:56) → a második.
    QCOMPARE(at(QStringLiteral("[t=01:24]")).value(0).utteranceIds, QStringList{QStringLiteral("u60000")});
    QCOMPARE(at(QStringLiteral("[t=01:56]")).value(0).utteranceIds, QStringList{QStringLiteral("u120000")});
    // 1:40: mindkettőtől 20 s → nincs forrás.
    QVERIFY(at(QStringLiteral("[t=01:40]")).isEmpty());
    // 4:53: az 5:00-s sor előtt 6 s (a másodperc vége 4:53.999) → kívül a tűrésen; 4:56 → belül.
    QVERIFY(at(QStringLiteral("[t=04:53]")).isEmpty());
    QCOMPARE(at(QStringLiteral("[t=04:56]")).value(0).utteranceIds, QStringList{QStringLiteral("u300000")});
    // A span a megszólalás teljes időkerete.
    const SourceSpan sp = at(QStringLiteral("[t=02:05]")).value(0);
    QCOMPARE(sp.startMs, 120000);
    QCOMPARE(sp.endMs, 150000);
}

void SummarySourcesTest::resolveRangeAndIdAndDedupe()
{
    const QVector<SourceLine> lines = threeLines();
    // Tartomány: minden átfedő sor, időrendben.
    QVector<SourceSpan> r = resolveRefs(extractMarkers(QStringLiteral("[t=01:10-02:10]")).refs, lines);
    QCOMPARE(r.size(), 2);
    QCOMPARE(r[0].utteranceIds.value(0), QStringLiteral("u60000"));
    QCOMPARE(r[1].utteranceIds.value(0), QStringLiteral("u120000"));
    // Id-jelölő; ismeretlen id kimarad; ugyanaz a sor kétszer → egyszer.
    r = resolveRefs(extractMarkers(QStringLiteral("[u300000] [u999] [t=05:02] [t=02:00]")).refs, lines);
    QCOMPARE(r.size(), 2);
    QCOMPARE(r[0].utteranceIds.value(0), QStringLiteral("u120000"));
    QCOMPARE(r[1].utteranceIds.value(0), QStringLiteral("u300000"));
}

// ---- összerakás ---------------------------------------------------------------------------

void SummarySourcesTest::attachSourcesBuildsStatements()
{
    Summary s;
    s.execSummary = QStringLiteral("A pilot két helyszínen indul. [t=01:05] Béla szerint az ár magas. [t=02:10]");
    s.decisions = {QStringLiteral("A pilot két helyszínen indul. [t=01:10]")};
    s.openQuestions = {QStringLiteral("Mennyi a végső ár? [t=02:20]")};
    ActionItem ai;
    ai.text = QStringLiteral("Ajánlat küldése [t=05:02]");
    ai.owner = QStringLiteral("Béla");
    ai.due = QStringLiteral("péntek");
    s.actionItems = {ai};
    attachSources(s, threeLines());

    // A tárolt mezők jelölő nélküliek → a markdown olvasható marad.
    QCOMPARE(s.execSummary, QStringLiteral("A pilot két helyszínen indul. Béla szerint az ár magas."));
    QCOMPARE(s.decisions, QStringList{QStringLiteral("A pilot két helyszínen indul.")});
    QCOMPARE(s.openQuestions, QStringList{QStringLiteral("Mennyi a végső ár?")});
    QCOMPARE(s.actionItems[0].text, QStringLiteral("Ajánlat küldése"));
    QVERIFY(!s.renderMarkdown().contains(QStringLiteral("[t=")));

    QCOMPARE(s.statements.size(), 4);
    QCOMPARE(s.statements[0].id, QStringLiteral("s1"));
    QCOMPARE(s.statements[0].kind, StatementKind::Statement);
    QCOMPARE(s.statements[0].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u60000")});
    QCOMPARE(s.statements[1].id, QStringLiteral("s2"));
    QCOMPARE(s.statements[1].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u120000")});
    QCOMPARE(s.statements[2].id, QStringLiteral("d1"));
    QCOMPARE(s.statements[2].kind, StatementKind::Decision);
    QCOMPARE(s.statements[3].id, QStringLiteral("t1"));
    QCOMPARE(s.statements[3].kind, StatementKind::Todo);
    QCOMPARE(s.statements[3].owner, QStringLiteral("Béla"));
    QCOMPARE(s.statements[3].text, QStringLiteral("Ajánlat küldése"));
    QCOMPARE(s.statements[3].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u300000")});
    // A beszélő-pillanatkép csak a hivatkozott sorokra.
    QCOMPARE(s.sourceSpeakers.size(), 3);
    QCOMPARE(s.sourceSpeakers.value(QStringLiteral("u120000")).name, QStringLiteral("Béla"));
}

void SummarySourcesTest::missingMarkerMeansNoSource()
{
    Summary s;
    s.execSummary = QStringLiteral("Első mondat forrással. [t=01:00] Második mondat forrás nélkül.");
    s.decisions = {QStringLiteral("Döntés jelölő nélkül")};
    attachSources(s, threeLines());
    QCOMPARE(s.statements.size(), 3);
    QCOMPARE(s.statements[0].sourceSpans.size(), 1);
    QVERIFY(s.statements[1].sourceSpans.isEmpty());   // „nincs forrás", nem hiba
    QVERIFY(s.statements[2].sourceSpans.isEmpty());
    // Sorok nélkül (pl. hiányzó átirat-sorok): állítások vannak, forrás nincs.
    Summary t;
    t.execSummary = QStringLiteral("Mondat. [t=01:00]");
    attachSources(t, {});
    QCOMPARE(t.statements.size(), 1);
    QVERIFY(t.statements[0].sourceSpans.isEmpty());
    QCOMPARE(t.execSummary, QStringLiteral("Mondat."));
}

void SummarySourcesTest::memoSectionTimesAndSpeakers()
{
    // Két rész jegyzete: az elsőben az első tárgynak nincs ideje (→ a rész kezdete), a második
    // tárgy kerete 2:00–4:00; a második rész tárgya 5:00–6:00.
    summarypipe::TranscriptPart p0;
    p0.index = 0; p0.startMs = 60000; p0.endMs = 240000;
    summarypipe::TranscriptPart p1;
    p1.index = 1; p1.startMs = 300000; p1.endMs = 360000;
    const summarypipe::PartNotes n0 = summarypipe::parseNotes(QStringLiteral(
        "TOPICS\n### Nyitás\n- Bemutatkozás. [t=01:05]\n### [02:00-04:00] Árazás\n- Az ár 100 euró.\n"
        "DECISIONS\n- none\nOPEN\n- none\nACTIONS\n- none\n"), p0);
    const summarypipe::PartNotes n1 = summarypipe::parseNotes(QStringLiteral(
        "TOPICS\n### [05:00-06:00] Teendők\n- Ajánlat megy.\n"), p1);
    summarypipe::MergeResult mr;
    mr.ok = true;
    mr.execSummary = QStringLiteral("Összegzés.");
    const Summary s = summarypipe::buildSummary(mr, {n0, n1}, {QStringLiteral("Ádám"), QStringLiteral("Béla")},
                                                threeLines());
    QCOMPARE(s.memo.size(), 3);
    QCOMPARE(s.memo[0].startMs, 60000);         // a rész kezdete (nem egyértelmű határ)
    QCOMPARE(s.memo[0].endMs, 120000);          // a következő tárgy kezdetéig
    QCOMPARE(s.memo[0].speakers, QStringList{QStringLiteral("Ádám")});
    QCOMPARE(s.memo[0].points.value(0), QStringLiteral("Bemutatkozás."));   // jelölő le
    QCOMPARE(s.memo[1].startMs, 120000);
    QCOMPARE(s.memo[1].endMs, 240000);
    QCOMPARE(s.memo[1].speakers, QStringList{QStringLiteral("Béla")});
    QCOMPARE(s.memo[2].speakers, QStringList{QStringLiteral("Ádám")});
}

// ---- vezénylés hamis LLM-mel ------------------------------------------------------------

void SummarySourcesTest::serviceSingleCallWithMarkers()
{
    FakeProvider llm;
    llm.script = [](int, const LlmRequest&) {
        return QStringLiteral(
            "TOPICS\n### [00:00-01:00] Árazás\n- Az ár 100 euró.\n### [01:00-02:30] Pilot\n- Két helyszín.\n"
            "SUMMARY\n"
            R"({"execSummary":"Az árról volt szó. [t=00:05] A pilot két helyszínen indul. [t=01:30, t=02:00]",)"
            R"("decisions":["Két helyszín lesz. [t=01:31]"],"openQuestions":[],)"
            R"("actionItems":[{"text":"Ajánlat küldése [t=02:01]","owner":"Ádám","due":""}]})");
    };
    SummaryService svc(&llm);
    QSignalSpy ready(&svc, &SummaryService::summaryReady);
    QSignalSpy failed(&svc, &SummaryService::summaryFailed);
    SummaryRequest req;
    req.transcript = transcriptAt({0, 0.5, 1.0, 1.5, 2.0});
    req.language = QStringLiteral("magyar");
    req.model = QStringLiteral("teszt");
    svc.summarize(req);
    QVERIFY(ready.wait(2000));
    QCOMPARE(failed.count(), 0);
    // A beépített egylépéses prompt kéri a jelölőket.
    QVERIFY(llm.requests[0].messages[0].content.contains(QStringLiteral("[t=mm:ss]")));

    const Summary s = qvariant_cast<Summary>(ready[0][0]);
    QCOMPARE(s.execSummary, QStringLiteral("Az árról volt szó. A pilot két helyszínen indul."));
    QCOMPARE(s.decisions, QStringList{QStringLiteral("Két helyszín lesz.")});
    QCOMPARE(s.actionItems.value(0).text, QStringLiteral("Ajánlat küldése"));
    QCOMPARE(s.statements.size(), 4);
    // Sorok nélkül a beszéd-blokkokból: "u<startMs>" id-k.
    QCOMPARE(s.statements[0].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u0")});
    QCOMPARE(s.statements[1].sourceSpans.size(), 2);
    QCOMPARE(s.statements[1].sourceSpans[0].utteranceIds, QStringList{QStringLiteral("u90000")});
    QCOMPARE(s.statements[1].sourceSpans[1].utteranceIds, QStringList{QStringLiteral("u120000")});
    QCOMPARE(s.statements[3].owner, QStringLiteral("Ádám"));
    QCOMPARE(s.sourceSpeakers.value(QStringLiteral("u90000")).name, QStringLiteral("Béla"));
    QCOMPARE(s.memo.value(1).speakers, QStringList({QStringLiteral("Ádám"), QStringLiteral("Béla")}));
    QVERIFY(!s.renderMarkdown().contains(QStringLiteral("[t=")));
}

void SummarySourcesTest::serviceMergeWithMarkers()
{
    // Két rész (0–15 perc és 15–30 perc): jegyzetek, majd az összegzés jelölőkkel; a sorok
    // a kérésből jönnek (a valódi segments.json id-jei és a feloldott nevek).
    QVector<double> mins;
    for (double x = 0; x < 30; x += 0.5) mins << x;
    FakeProvider llm;
    llm.script = [](int, const LlmRequest& r) {
        const QString sys = r.messages.value(0).content;
        if (sys.contains(QStringLiteral("ONE PART"))) {
            const bool second = r.messages.value(1).content.contains(QStringLiteral("PART 2 of 2"));
            return second ? QStringLiteral("TOPICS\n### [15:00-29:30] Pilot\n- Két helyszín.\nDECISIONS\n"
                                           "- [16:00] Két helyszín lesz.\nOPEN\n- none\nACTIONS\n- none\n")
                          : QStringLiteral("TOPICS\n### [00:00-14:30] Árazás\n- Az ár 100 euró.\nDECISIONS\n"
                                           "- none\nOPEN\n- none\nACTIONS\n- [03:00] Ajánlat — Béla — péntek\n");
        }
        return QStringLiteral(
            R"({"decisions":["Két helyszín lesz. [t=16:00]"],"openQuestions":[],)"
            R"("actionItems":[{"text":"Ajánlat küldése [t=03:00]","owner":"Béla","due":"péntek"}],)"
            R"("execSummary":"Az árazásról és a pilotról volt szó. [t=00:00, t=15:00]"})");
    };
    SummaryService svc(&llm);
    QSignalSpy ready(&svc, &SummaryService::summaryReady);
    SummaryRequest req;
    req.transcript = transcriptAt(mins);
    req.language = QStringLiteral("magyar");
    req.model = QStringLiteral("teszt");
    req.sourceLines = {line(QStringLiteral("u0"), 0, 20000, QStringLiteral("Beszélő 1"), QStringLiteral("Fehér Gábor")),
                       line(QStringLiteral("u180000"), 180000, 200000, QStringLiteral("Beszélő 2"), QStringLiteral("Varga Árpád")),
                       line(QStringLiteral("u900000"), 900000, 920000, QStringLiteral("Beszélő 1"), QStringLiteral("Fehér Gábor")),
                       line(QStringLiteral("u960000"), 960000, 980000, QStringLiteral("Beszélő 2"), QStringLiteral("Varga Árpád"))};
    svc.summarize(req);
    QVERIFY(ready.wait(3000));
    QCOMPARE(llm.requests.size(), 3);
    QVERIFY(llm.requests[2].messages[0].content.contains(QStringLiteral("SOURCE MARKERS")));
    const Summary s = qvariant_cast<Summary>(ready[0][0]);
    QCOMPARE(s.statements.size(), 3);
    QCOMPARE(s.statements[0].sourceSpans.size(), 2);
    QCOMPARE(s.statements[1].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u960000")});
    QCOMPARE(s.statements[2].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u180000")});
    QCOMPARE(s.sourceSpeakers.value(QStringLiteral("u180000")).key, QStringLiteral("Beszélő 2"));
    QCOMPARE(s.memo.value(0).speakers,
             QStringList({QStringLiteral("Fehér Gábor"), QStringLiteral("Varga Árpád")}));
}

// ---- tárolás ------------------------------------------------------------------------------

void SummarySourcesTest::storeRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Summary s;
    s.execSummary = QStringLiteral("Az árról volt szó. [t=01:05] Forrás nélküli mondat.");
    ActionItem ai;
    ai.text = QStringLiteral("Ajánlat [t=05:00]");
    ai.owner = QStringLiteral("Béla");
    s.actionItems = {ai};
    MemoSection m;
    m.title = QStringLiteral("Árazás");
    m.startMs = 60000;
    m.endMs = 150000;
    m.points = {QStringLiteral("Pont.")};
    s.memo = {m};
    attachSources(s, threeLines());
    s.statements[0].flagged = true;

    SummaryDocument doc;
    doc.exists = true;
    doc.summary = s;
    doc.meta.mode = SummaryMode::Quick;
    QVERIFY(summarystore::save(dir.path(), doc));
    const SummaryDocument back = summarystore::load(dir.path());
    QCOMPARE(back.summary.statements.size(), 3);
    const SummaryStatement& b0 = back.summary.statements[0];
    QCOMPARE(b0.id, QStringLiteral("s1"));
    QCOMPARE(b0.text, QStringLiteral("Az árról volt szó."));
    QVERIFY(b0.flagged);
    QCOMPARE(b0.sourceSpans.size(), 1);
    QCOMPARE(b0.sourceSpans[0].startMs, 60000);
    QCOMPARE(b0.sourceSpans[0].endMs, 80000);
    QCOMPARE(b0.sourceSpans[0].utteranceIds, QStringList{QStringLiteral("u60000")});
    QVERIFY(back.summary.statements[1].sourceSpans.isEmpty());
    QCOMPARE(back.summary.statements[2].kind, StatementKind::Todo);
    QCOMPARE(back.summary.statements[2].owner, QStringLiteral("Béla"));
    QVERIFY(!back.summary.statements[2].flagged);
    QCOMPARE(back.summary.sourceSpeakers.size(), 2);
    QCOMPARE(back.summary.sourceSpeakers.value(QStringLiteral("u300000")).key, QStringLiteral("Ádám"));
    QCOMPARE(back.summary.memo.value(0).speakers,
             QStringList({QStringLiteral("Ádám"), QStringLiteral("Béla")}));
}

void SummarySourcesTest::oldJsonLoadsWithoutStatements()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QJsonObject sum{{"execSummary", "Régi összefoglaló."}, {"decisions", QJsonArray{"Régi döntés"}},
                    {"actionItems", QJsonArray{}}, {"participants", QJsonArray{"Ádám"}},
                    {"memo", QJsonArray{QJsonObject{{"title", "Tárgy"}, {"startMs", 0}, {"endMs", 60000},
                                                    {"points", QJsonArray{"Pont"}}}}}};
    QJsonObject root{{"version", 2}, {"mode", "quick"}, {"summary", sum}};
    QVERIFY(writeJson(summarystore::jsonPath(dir.path()), QJsonDocument(root)));
    const SummaryDocument back = summarystore::load(dir.path());
    QVERIFY(back.exists);
    QCOMPARE(back.summary.execSummary, QStringLiteral("Régi összefoglaló."));
    QVERIFY(back.summary.statements.isEmpty());
    QVERIFY(back.summary.sourceSpeakers.isEmpty());
    QVERIFY(back.summary.memo.value(0).speakers.isEmpty());
    // Jelölni nincs mit.
    QVERIFY(!summarystore::setStatementFlagged(dir.path(), QStringLiteral("s1")));
}

void SummarySourcesTest::flagStatementKeepsJsonMtime()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Summary s;
    s.execSummary = QStringLiteral("Első. [t=01:00] Második. [t=02:00]");
    attachSources(s, threeLines());
    SummaryDocument doc;
    doc.exists = true;
    doc.summary = s;
    QVERIFY(summarystore::save(dir.path(), doc));
    const QString path = summarystore::jsonPath(dir.path());
    {
        // Régebbi módosítási idő (mintha a summary.md-t azóta kézzel szerkesztették volna).
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
    }
    const QDateTime before = QFileInfo(path).lastModified();
    QVERIFY(summarystore::setStatementFlagged(dir.path(), QStringLiteral("s2")));
    QCOMPARE(QFileInfo(path).lastModified().toSecsSinceEpoch(), before.toSecsSinceEpoch());
    const SummaryDocument back = summarystore::load(dir.path());
    QVERIFY(!back.summary.statements[0].flagged);
    QVERIFY(back.summary.statements[1].flagged);
    QVERIFY(!summarystore::setStatementFlagged(dir.path(), QStringLiteral("nincs-ilyen")));
}

void SummarySourcesTest::handEditedMarkdownKeepsMatchingStatements()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Summary s;
    s.execSummary = QStringLiteral("Első mondat. [t=01:00] Második mondat. [t=02:00]");
    attachSources(s, threeLines());
    SummaryDocument doc;
    doc.exists = true;
    doc.summary = s;
    QVERIFY(summarystore::save(dir.path(), doc));
    {
        QFile f(summarystore::jsonPath(dir.path()));
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
    }
    // A kézzel szerkesztett markdownban csak az első mondat maradt változatlan.
    QFile md(summarystore::markdownPath(dir.path()));
    QVERIFY(md.open(QIODevice::WriteOnly));
    md.write(QStringLiteral("## Vezetői összefoglaló\n\nElső mondat. Átírt második.\n").toUtf8());
    md.close();
    const SummaryDocument back = summarystore::load(dir.path());
    QVERIFY(back.fromMarkdown);
    QCOMPARE(back.summary.statements.size(), 1);
    QCOMPARE(back.summary.statements[0].id, QStringLiteral("s1"));
}

// ---- célzott elavulás ---------------------------------------------------------------------

void SummarySourcesTest::targetedStalenessPure()
{
    Summary s;
    s.execSummary = QStringLiteral("Fehér Gábor szerint jó. [t=01:00] Varga Árpád hallgatott. [t=02:00]");
    s.decisions = {QStringLiteral("Marad az ár. [t=01:05]")};
    ActionItem ai;
    ai.text = QStringLiteral("Ajánlat [t=01:10]");
    ai.owner = QStringLiteral("Fehér Gábor");
    s.actionItems = {ai};
    attachSources(s, {line(QStringLiteral("u60000"), 60000, 80000, QStringLiteral("Beszélő 1"), QStringLiteral("Fehér Gábor")),
                      line(QStringLiteral("u120000"), 120000, 150000, QStringLiteral("Beszélő 2"), QStringLiteral("Varga Árpád"))});

    // Most: az u60000 sor Varga Árpádé lett (kézi átsorolás a Beszélő 2-höz).
    QHash<QString, SourceSpeaker> now{
        {QStringLiteral("u60000"), {QStringLiteral("Beszélő 2"), QStringLiteral("Varga Árpád")}},
        {QStringLiteral("u120000"), {QStringLiteral("Beszélő 2"), QStringLiteral("Varga Árpád")}}};
    QVector<SummaryStatement> sts = s.statements;
    SummaryStaleInfo info;
    applyTargetedStaleness(sts, s.sourceSpeakers, now, {QStringLiteral("Beszélő 2")}, info);
    QVERIFY(info.targeted);
    QCOMPARE(info.affectedStatements, 2);     // s1 + d1
    QCOMPARE(info.affectedTodos, 1);          // t1
    QCOMPARE(info.ownerChanges, 1);
    QCOMPARE(info.affectedStatementIds, QStringList({QStringLiteral("s1"), QStringLiteral("d1"), QStringLiteral("t1")}));
    QCOMPARE(info.affectedUtteranceIds, QStringList{QStringLiteral("u60000")});
    QCOMPARE(sts[0].staleBecause, QStringList{QStringLiteral("Fehér Gábor → Varga Árpád?")});
    QVERIFY(sts[1].staleBecause.isEmpty());
    QCOMPARE(sts[3].ownerStaleBecause, QStringLiteral("Fehér Gábor → Varga Árpád?"));

    // „Rendben így" után (nincs javított kulcs) semmi sem érintett.
    sts = s.statements;
    SummaryStaleInfo ok;
    applyTargetedStaleness(sts, s.sourceSpeakers, now, {}, ok);
    QVERIFY(ok.targeted);
    QCOMPARE(ok.affectedStatements + ok.affectedTodos, 0);
    // Egy másik beszélő javítása nem érinti ezeket a sorokat.
    SummaryStaleInfo other;
    applyTargetedStaleness(sts, s.sourceSpeakers, now, {QStringLiteral("Beszélő 7")}, other);
    QCOMPARE(other.affectedStatements + other.affectedTodos, 0);
    // Forrás nélküli állításoknál nincs célzott jelzés (az egész-dokumentum jelzés a fallback).
    QVector<SummaryStatement> bare{SummaryStatement{}};
    SummaryStaleInfo fb;
    applyTargetedStaleness(bare, {}, {}, {QStringLiteral("Beszélő 2")}, fb);
    QVERIFY(!fb.targeted);
}

void SummarySourcesTest::targetedStalenessFromOverlay()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Átirat-sorok (transcript.segments.json) két nyers beszélővel.
    QJsonArray segs;
    auto seg = [&](qint64 s, qint64 e, const char* sp) {
        segs.append(QJsonObject{{"startMs", double(s)}, {"endMs", double(e)}, {"speaker", sp}, {"text", "x"}});
    };
    seg(60000, 80000, "Beszélő 1");
    seg(120000, 150000, "Beszélő 2");
    seg(300000, 310000, "Beszélő 1");
    QVERIFY(writeJson(speakeredit::segmentsPath(dir.path()), QJsonDocument(segs)));

    Meeting m;
    m.id = QStringLiteral("m1");
    m.folder = dir.path();
    m.hasSummary = true;
    m.speakerMap = {{QStringLiteral("Beszélő 1"), QStringLiteral("Fehér Gábor")},
                    {QStringLiteral("Beszélő 2"), QStringLiteral("Varga Árpád")}};

    // Összefoglaló a mostani állapottal (a beszélő-pillanatkép a resolvedSourceLines-ból).
    const QVector<SourceLine> lines = speakeredit::resolvedSourceLines(m);
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines[0].speakerName, QStringLiteral("Fehér Gábor"));
    Summary s;
    s.execSummary = QStringLiteral("Gábor vállalta az ajánlatot. [t=05:00] Árpád kérdezett. [t=02:00]");
    ActionItem ai;
    ai.text = QStringLiteral("Ajánlat küldése [t=05:01]");
    ai.owner = QStringLiteral("Fehér Gábor");
    s.actionItems = {ai};
    attachSources(s, lines);
    SummaryDocument doc;
    doc.exists = true;
    doc.summary = s;
    QVERIFY(summarystore::save(dir.path(), doc));

    // Javítás nélkül: célzott, de semmi sem érintett.
    SummaryStaleInfo info0 = speakeredit::summaryStale(m);
    speakeredit::fillTargetedStale(m, info0);
    QVERIFY(!info0.stale);
    QVERIFY(info0.targeted);
    QCOMPARE(info0.affectedStatements + info0.affectedTodos, 0);

    // Hamis overlay-változás: az 5:00-s sort Varga Árpádhoz (Beszélő 2) sorolták.
    SpeakerOverlay ov;
    ov.transcriptFingerprint = speakeredit::transcriptFingerprint(speakeredit::loadTranscriptLines(dir.path()));
    OverlayUtterance ou;
    ou.speaker = QStringLiteral("Beszélő 2");
    ou.corrected = true;
    ov.utterances.insert(QStringLiteral("u300000"), ou);
    ov.changedSinceSummary = {QStringLiteral("Beszélő 1"), QStringLiteral("Beszélő 2")};
    QVERIFY(speakeredit::saveOverlay(dir.path(), ov));

    SummaryStaleInfo info = speakeredit::summaryStale(m);
    QVERIFY(info.stale);
    QCOMPARE(info.correctedSpeakers, 2);
    QVector<SummaryStatement> sts;
    speakeredit::fillTargetedStale(m, info, &sts);
    QVERIFY(info.targeted);
    QCOMPARE(info.affectedStatements, 1);
    QCOMPARE(info.affectedTodos, 1);
    QCOMPARE(info.ownerChanges, 1);
    QCOMPARE(info.affectedUtteranceIds, QStringList{QStringLiteral("u300000")});
    QCOMPARE(sts.size(), 3);
    QCOMPARE(sts[0].staleBecause, QStringList{QStringLiteral("Fehér Gábor → Varga Árpád?")});
    QVERIFY(sts[1].staleBecause.isEmpty());
    QCOMPARE(sts[2].ownerStaleBecause, QStringLiteral("Fehér Gábor → Varga Árpád?"));

    // „Rendben így": a jelzés eltűnik, a célzott lista kiürül.
    QVERIFY(speakeredit::clearSummaryStale(dir.path()));
    SummaryStaleInfo after = speakeredit::summaryStale(m);
    speakeredit::fillTargetedStale(m, after, &sts);
    QVERIFY(!after.stale);
    QCOMPARE(after.affectedStatements + after.affectedTodos, 0);
    QVERIFY(sts[0].staleBecause.isEmpty());

    // Összefoglaló nélküli meeting: semmi.
    Meeting none = m;
    none.hasSummary = false;
    SummaryStaleInfo ni;
    speakeredit::fillTargetedStale(none, ni, &sts);
    QVERIFY(!ni.targeted);
    QVERIFY(sts.isEmpty());
}

// ---- prompt -------------------------------------------------------------------------------

void SummarySourcesTest::promptTemplatesAskForMarkers()
{
    for (const char* id : {"single", "merge"}) {
        const QString p = promptBuiltin(QString::fromLatin1(id));
        QVERIFY2(p.contains(QStringLiteral("SOURCE MARKERS")), id);
        QVERIFY2(p.contains(QStringLiteral("[t=mm:ss]")), id);
        QVERIFY2(!p.contains(QLatin1Char('%')), id);        // minden %…% helyőrző feloldva
    }
    QVERIFY(promptBuiltin(QStringLiteral("merge")).contains(QStringLiteral("the time written in the notes")));
    QVERIFY(promptBuiltin(QStringLiteral("single")).contains(QStringLiteral("transcript paragraph")));
    // A jegyzet-prompt nem változott: a memó-pontok jelölő nélküliek maradnak.
    QVERIFY(!promptBuiltin(QStringLiteral("notes")).contains(QStringLiteral("SOURCE MARKERS")));
    // A Beállítások kimeneti sémája is mutatja a jelölőt.
    QVERIFY(promptOutputFormat(QStringLiteral("merge")).body.contains(QStringLiteral("[t=mm:ss]")));
}

QTEST_GUILESS_MAIN(SummarySourcesTest)
#include "test_summary_sources.moc"

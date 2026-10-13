// Summary source references: the marker parser (`[t=mm:ss]`, a missing / bad marker, +-5 s
// tolerance), splitting into sentences, SummaryService with a marked (fake) LLM reply,
// summary.json both ways (statements, sourceSpeakers, memo speakers, flag), the memo
// sections' time frames + speakers, the targeted staleness (with a fake overlay change) and
// the prompt templates. No LLM: the replies are hand-written texts.
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

#include "tanara/domain/Types.h"
#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/processing/summary/SummaryService.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/providers/chat/ILlmProvider.h"
#include "tanara/processing/summary/SummaryPipeline.h"
#include "tanara/processing/summary/SummarySources.h"
#include "tanara/library/summary/SummaryStore.h"

using namespace tanara;
using namespace tanara::processing::summarysrc;
using namespace tanara::summarysrc;

// ---- scripted fake LLM (as in test_summary) ----
class FakeJob : public providers::LlmJob {
    Q_OBJECT
public:
    explicit FakeJob(QObject* parent) : providers::LlmJob(parent) {}
    void cancel() override {}
};

class FakeProvider : public QObject, public providers::ILlmProvider {
    Q_OBJECT
public:
    std::function<QString(int, const providers::LlmRequest&)> script;
    QVector<providers::LlmRequest> requests;
    QString name() const override { return QStringLiteral("fake"); }
    bool supportsStreaming() const override { return false; }
    providers::LlmJob* chat(const providers::LlmRequest& req) override {
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

// Three utterances: 1:00-1:20 Ádám, 2:00-2:30 Béla, 5:00-5:10 Ádám.
QVector<summarysrc::SourceLine> threeLines()
{
    return {line(QStringLiteral("u60000"), 60000, 80000, QStringLiteral("Ádám")),
            line(QStringLiteral("u120000"), 120000, 150000, QStringLiteral("Béla")),
            line(QStringLiteral("u300000"), 300000, 310000, QStringLiteral("Ádám"))};
}

// Transcript: paragraphs alternating by speaker at the given minutes (20 s long).
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
    // markers
    void markersExtracted_data();
    void markersExtracted();
    void badTimesAreStrippedWithoutSource();
    void textWithoutMarkersUnchanged();
    // sentences
    void sentenceSplit();
    // resolution
    void resolveContainsAndTolerance();
    void resolveRangeAndIdAndDedupe();
    // assembly
    void attachSourcesBuildsStatements();
    void missingMarkerMeansNoSource();
    void memoSectionTimesAndSpeakers();
    void memoPointsIntroAndGroups();
    void memoJoinKeepsGroupsAndDedupesMarkers();
    void memoGroupKeys();
    // orchestration with a fake LLM
    void serviceSingleCallWithMarkers();
    void serviceMergeWithMarkers();
    // storage
    void storeRoundTrip();
    void oldJsonLoadsWithoutStatements();
    void flagStatementKeepsJsonMtime();
    void handEditedMarkdownKeepsMatchingStatements();
    void memoStatementsRoundTrip();
    // targeted staleness
    void targetedStalenessPure();
    void targetedStalenessFromOverlay();
    void targetedStalenessCoversMemoAndQuestions();
    // prompt
    void promptTemplatesAskForMarkers();
};

void SummarySourcesTest::initTestCase()
{
    // Sandbox: the real ~/.tanara is untouched.
    static QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("TANARA_HOME", home.path().toUtf8());
}

// ---- markers ----

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
    // Bad time: the marker is removed, there is no reference.
    for (const QString& in : {QStringLiteral("Mondat. [t=12:7]"), QStringLiteral("Mondat. [t=12:75]"),
                              QStringLiteral("Mondat. [t=abc]"), QStringLiteral("Mondat. [t=]"),
                              QStringLiteral("Mondat. [t=1:61:00]")}) {
        const MarkedText mt = extractMarkers(in);
        QCOMPARE(mt.text, QStringLiteral("Mondat."));
        QVERIFY2(mt.refs.isEmpty(), qPrintable(in));
    }
    // Well-formed, but a time outside the transcript: no utterance within the tolerance, so no source.
    QVERIFY(processing::summarysrc::resolveRefs(processing::summarysrc::extractMarkers(QStringLiteral("x [t=59:00]")).refs, threeLines()).isEmpty());
}

void SummarySourcesTest::textWithoutMarkersUnchanged()
{
    const QString t = QStringLiteral("Első bekezdés, 14:00-kor kezdünk.\n\nMásodik [link] bekezdés.");
    QCOMPARE(processing::summarysrc::stripMarkers(t), t);
    QVERIFY(processing::summarysrc::extractMarkers(t).refs.isEmpty());
}

// ---- sentences ----

void SummarySourcesTest::sentenceSplit()
{
    const QVector<processing::summarysrc::MarkedText> s = processing::summarysrc::splitSentences(QStringLiteral(
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

// ---- resolution ----

void SummarySourcesTest::resolveContainsAndTolerance()
{
    const QVector<summarysrc::SourceLine> lines = threeLines();
    auto at = [&](const QString& marker) { return processing::summarysrc::resolveRefs(processing::summarysrc::extractMarkers(marker).refs, lines); };
    // Inside the utterance.
    QCOMPARE(at(QStringLiteral("[t=01:10]")).value(0).utteranceIds, QStringList{QStringLiteral("u60000")});
    // 4 s after the end (1:24) is still the first line; 4 s before the next one's start (1:56) is the second.
    QCOMPARE(at(QStringLiteral("[t=01:24]")).value(0).utteranceIds, QStringList{QStringLiteral("u60000")});
    QCOMPARE(at(QStringLiteral("[t=01:56]")).value(0).utteranceIds, QStringList{QStringLiteral("u120000")});
    // 1:40: 20 s from both, so no source.
    QVERIFY(at(QStringLiteral("[t=01:40]")).isEmpty());
    // 4:53: 6 s before the 5:00 line (the second's end is 4:53.999), so outside the tolerance; 4:56 is inside.
    QVERIFY(at(QStringLiteral("[t=04:53]")).isEmpty());
    QCOMPARE(at(QStringLiteral("[t=04:56]")).value(0).utteranceIds, QStringList{QStringLiteral("u300000")});
    // The span is the utterance's whole time frame.
    const SourceSpan sp = at(QStringLiteral("[t=02:05]")).value(0);
    QCOMPARE(sp.startMs, 120000);
    QCOMPARE(sp.endMs, 150000);
}

void SummarySourcesTest::resolveRangeAndIdAndDedupe()
{
    const QVector<summarysrc::SourceLine> lines = threeLines();
    // Range: every overlapping line, in chronological order.
    QVector<SourceSpan> r = processing::summarysrc::resolveRefs(processing::summarysrc::extractMarkers(QStringLiteral("[t=01:10-02:10]")).refs, lines);
    QCOMPARE(r.size(), 2);
    QCOMPARE(r[0].utteranceIds.value(0), QStringLiteral("u60000"));
    QCOMPARE(r[1].utteranceIds.value(0), QStringLiteral("u120000"));
    // Id marker; an unknown id is left out; the same line twice counts once.
    r = processing::summarysrc::resolveRefs(processing::summarysrc::extractMarkers(QStringLiteral("[u300000] [u999] [t=05:02] [t=02:00]")).refs, lines);
    QCOMPARE(r.size(), 2);
    QCOMPARE(r[0].utteranceIds.value(0), QStringLiteral("u120000"));
    QCOMPARE(r[1].utteranceIds.value(0), QStringLiteral("u300000"));
}

// ---- assembly ----

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
    processing::summarysrc::attachSources(s, threeLines());

    // The stored fields have no markers, so the markdown stays readable.
    QCOMPARE(s.execSummary, QStringLiteral("A pilot két helyszínen indul. Béla szerint az ár magas."));
    QCOMPARE(s.decisions, QStringList{QStringLiteral("A pilot két helyszínen indul.")});
    QCOMPARE(s.openQuestions, QStringList{QStringLiteral("Mennyi a végső ár?")});
    QCOMPARE(s.actionItems[0].text, QStringLiteral("Ajánlat küldése"));
    QVERIFY(!s.renderMarkdown().contains(QStringLiteral("[t=")));

    QCOMPARE(s.statements.size(), 5);
    QCOMPARE(s.statements[0].id, QStringLiteral("s1"));
    QCOMPARE(s.statements[0].kind, StatementKind::Statement);
    QCOMPARE(s.statements[0].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u60000")});
    QCOMPARE(s.statements[1].id, QStringLiteral("s2"));
    QCOMPARE(s.statements[1].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u120000")});
    QCOMPARE(s.statements[2].id, QStringLiteral("d1"));
    QCOMPARE(s.statements[2].kind, StatementKind::Decision);
    // The open question is a claim too, before the action items.
    QCOMPARE(s.statements[3].id, QStringLiteral("q1"));
    QCOMPARE(s.statements[3].kind, StatementKind::OpenQuestion);
    QCOMPARE(s.statements[3].text, QStringLiteral("Mennyi a végső ár?"));
    QCOMPARE(s.statements[3].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u120000")});
    QCOMPARE(s.statements[4].id, QStringLiteral("t1"));
    QCOMPARE(s.statements[4].kind, StatementKind::Todo);
    QCOMPARE(s.statements[4].owner, QStringLiteral("Béla"));
    QCOMPARE(s.statements[4].text, QStringLiteral("Ajánlat küldése"));
    QCOMPARE(s.statements[4].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u300000")});
    // The speaker snapshot only for the referenced lines.
    QCOMPARE(s.sourceSpeakers.size(), 3);
    QCOMPARE(s.sourceSpeakers.value(QStringLiteral("u120000")).name, QStringLiteral("Béla"));
}

void SummarySourcesTest::missingMarkerMeansNoSource()
{
    Summary s;
    s.execSummary = QStringLiteral("Első mondat forrással. [t=01:00] Második mondat forrás nélkül.");
    s.decisions = {QStringLiteral("Döntés jelölő nélkül")};
    processing::summarysrc::attachSources(s, threeLines());
    QCOMPARE(s.statements.size(), 3);
    QCOMPARE(s.statements[0].sourceSpans.size(), 1);
    QVERIFY(s.statements[1].sourceSpans.isEmpty());   // "no source", not an error
    QVERIFY(s.statements[2].sourceSpans.isEmpty());
    // Without lines (e.g. missing transcript lines): the claims exist, there is no source.
    Summary t;
    t.execSummary = QStringLiteral("Mondat. [t=01:00]");
    processing::summarysrc::attachSources(t, {});
    QCOMPARE(t.statements.size(), 1);
    QVERIFY(t.statements[0].sourceSpans.isEmpty());
    QCOMPARE(t.execSummary, QStringLiteral("Mondat."));
}

void SummarySourcesTest::memoSectionTimesAndSpeakers()
{
    // Notes of two parts: in the first, the first subject has no time (the part's start), the
    // second subject's frame is 2:00-4:00; the second part's subject is 5:00-6:00.
    processing::summarypipe::TranscriptPart p0;
    p0.index = 0; p0.startMs = 60000; p0.endMs = 240000;
    processing::summarypipe::TranscriptPart p1;
    p1.index = 1; p1.startMs = 300000; p1.endMs = 360000;
    const processing::summarypipe::PartNotes n0 = processing::summarypipe::parseNotes(QStringLiteral(
        "TOPICS\n### Nyitás\n- Bemutatkozás. [t=01:05]\n### [02:00-04:00] Árazás\n- Az ár 100 euró.\n"
        "DECISIONS\n- none\nOPEN\n- none\nACTIONS\n- none\n"), p0);
    const processing::summarypipe::PartNotes n1 = processing::summarypipe::parseNotes(QStringLiteral(
        "TOPICS\n### [05:00-06:00] Teendők\n- Ajánlat megy.\n"), p1);
    processing::summarypipe::MergeResult mr;
    mr.ok = true;
    mr.execSummary = QStringLiteral("Összegzés.");
    const Summary s = processing::summarypipe::buildSummary(mr, {n0, n1}, {QStringLiteral("Ádám"), QStringLiteral("Béla")},
                                                threeLines());
    QCOMPARE(s.memo.size(), 3);
    QCOMPARE(s.memo[0].startMs, 60000);         // the part's start (not a clear boundary)
    QCOMPARE(s.memo[0].endMs, 120000);          // up to the next subject's start
    QCOMPARE(s.memo[0].speakers, QStringList{QStringLiteral("Ádám")});
    QCOMPARE(s.memo[0].points.value(0), QStringLiteral("Bemutatkozás."));   // marker removed
    QCOMPARE(s.memo[1].startMs, 120000);
    QCOMPARE(s.memo[1].endMs, 240000);
    QCOMPARE(s.memo[1].speakers, QStringList{QStringLiteral("Béla")});
    QCOMPARE(s.memo[2].speakers, QStringList{QStringLiteral("Ádám")});
}

void SummarySourcesTest::memoPointsIntroAndGroups()
{
    // A subject with an intro, a body point, "#### Arguments" and "#### Open questions"
    // sub-headings (the latter is NOT the OPEN section), the subsection's first line without a
    // bullet; then the real OPEN.
    processing::summarypipe::TranscriptPart p;
    p.index = 0; p.startMs = 0; p.endMs = 400000;
    const processing::summarypipe::PartNotes n = processing::summarypipe::parseNotes(QStringLiteral(
        "TOPICS\n"
        "### [01:00-05:10] Számlázási súgó\n"
        "Lilla a számlázást javasolja. [t=01:05] Béla vállalta.\n"
        "[t=02:00]\n"
        "- A díjbekérők adják a felét. [t=02:10]\n"
        "#### Arguments\n"
        "- Ha nem készül el, újra előjön. [t=01:10, t=05:02]\n"
        "- Tördelt\n  pont.\n"
        "#### Open questions\n"
        "Ki adja a kapacitást? [t=05:05]\n"
        "OPEN\n- [05:00] Kapacitás?\n"), p);
    QCOMPARE(n.topics.size(), 1);
    const MemoSection& t = n.topics[0];
    QCOMPARE(t.points.size(), 4);
    QCOMPARE(t.pointGroups, QStringList({QString(), QStringLiteral("arguments"), QStringLiteral("arguments"),
                                         QStringLiteral("open")}));
    QCOMPARE(t.points[2], QStringLiteral("Tördelt pont."));
    QCOMPARE(n.open, QStringList{QStringLiteral("[05:00] Kapacitás?")});
    // The merge input carries the sub-headings and the markers too.
    const QString merged = processing::summarypipe::renderNotesForMerge({n}, {});
    QVERIFY(merged.contains(QStringLiteral("#### Arguments")));
    QVERIFY(merged.contains(QStringLiteral("#### Unresolved")));
    QVERIFY(merged.contains(QStringLiteral("[t=02:10]")));

    processing::summarypipe::MergeResult mr;
    mr.ok = true;
    mr.execSummary = QStringLiteral("Összegzés.");
    mr.openQuestions = {QStringLiteral("Ki adja a kapacitást? [t=05:05]")};
    const Summary s = processing::summarypipe::buildSummary(mr, {n}, {}, threeLines());
    const MemoSection& m = s.memo.value(0);
    QCOMPARE(m.intro, QStringLiteral("Lilla a számlázást javasolja. Béla vállalta."));
    QCOMPARE(m.points[0], QStringLiteral("A díjbekérők adják a felét."));
    QCOMPARE(m.pointGroups.size(), 4);

    auto byId = [&s](const QString& id) {
        for (const SummaryStatement& st : s.statements) if (st.id == id) return st;
        return SummaryStatement{};
    };
    QCOMPARE(byId(QStringLiteral("q1")).kind, StatementKind::OpenQuestion);
    QCOMPARE(byId(QStringLiteral("q1")).sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u300000")});
    // Intro: per sentence; a marker on its own line belongs to the previous sentence.
    QCOMPARE(byId(QStringLiteral("i1.1")).kind, StatementKind::MemoIntro);
    QCOMPARE(byId(QStringLiteral("i1.1")).sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u60000")});
    QCOMPARE(byId(QStringLiteral("i1.2")).text, QStringLiteral("Béla vállalta."));
    QCOMPARE(byId(QStringLiteral("i1.2")).sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u120000")});
    QCOMPARE(byId(QStringLiteral("m1.1")).kind, StatementKind::MemoPoint);
    QCOMPARE(byId(QStringLiteral("m1.1")).sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u120000")});
    QCOMPARE(byId(QStringLiteral("m1.2")).sourceSpans.size(), 2);
    QVERIFY(byId(QStringLiteral("m1.3")).sourceSpans.isEmpty());
    QCOMPARE(byId(QStringLiteral("m1.4")).text, QStringLiteral("Ki adja a kapacitást?"));
    QCOMPARE(byId(QStringLiteral("m1.4")).sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u300000")});

    // The markdown is clean (without markers), the subsections with a Hungarian label.
    const QString md = s.renderMarkdown();
    QVERIFY(!md.contains(QStringLiteral("[t=")));
    QVERIFY(md.contains(QStringLiteral("Lilla a számlázást javasolja. Béla vállalta.\n\n- A díjbekérők")));
    QVERIFY(md.contains(QStringLiteral("**Elhangzott érvek:**")));
    QVERIFY(md.contains(QStringLiteral("**Nyitott kérdés:**\n\n- Ki adja a kapacitást?")));
}

void SummarySourcesTest::memoJoinKeepsGroupsAndDedupesMarkers()
{
    // A subject continuing across a part boundary: the intros are joined, the subsections stay,
    // a repeat of the same point with another marker is left out.
    processing::summarypipe::TranscriptPart p0; p0.index = 0; p0.endMs = 15 * 60000;
    processing::summarypipe::TranscriptPart p1; p1.index = 1; p1.startMs = 15 * 60000; p1.endMs = 30 * 60000;
    const processing::summarypipe::PartNotes a = processing::summarypipe::parseNotes(QStringLiteral(
        "TOPICS\n### [10:00-15:00] Raktárrendszer migráció\nA migráció indul. [t=10:00]\n"
        "- az adatok exportja [t=11:00]\n"), p0);
    const processing::summarypipe::PartNotes b = processing::summarypipe::parseNotes(QStringLiteral(
        "TOPICS\n### [15:00-20:00] Raktárrendszer migráció\nA teszt jövő héten. [t=15:30]\n"
        "- az adatok exportja [t=15:10]\n#### Unresolved\n- Ki teszteli? [t=19:00]\n"), p1);
    const QVector<MemoSection> memo = processing::summarypipe::assembleMemo({a, b});
    QCOMPARE(memo.size(), 1);
    QCOMPARE(memo[0].intro, QStringLiteral("A migráció indul. [t=10:00] A teszt jövő héten. [t=15:30]"));
    QCOMPARE(memo[0].points.size(), 2);
    QCOMPARE(memo[0].pointGroups, QStringList({QString(), QStringLiteral("open")}));
    const auto groups = processing::summarysrc::memoGroups(memo[0]);
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups[1].first, QStringLiteral("open"));
    QCOMPARE(groups[1].second, QVector<int>{1});
}

void SummarySourcesTest::memoGroupKeys()
{
    QCOMPARE(processing::summarysrc::memoGroupKey(QStringLiteral("#### Arguments")), QStringLiteral("arguments"));
    QCOMPARE(processing::summarysrc::memoGroupKey(QStringLiteral("**Elhangzott érvek:**")), QStringLiteral("arguments"));
    QCOMPARE(processing::summarysrc::memoGroupKey(QStringLiteral("Unresolved")), QStringLiteral("open"));
    QCOMPARE(processing::summarysrc::memoGroupKey(QStringLiteral("Nyitott kérdések")), QStringLiteral("open"));
    QCOMPARE(processing::summarysrc::memoGroupKey(QStringLiteral("Kockázatok:")), QStringLiteral("Kockázatok"));
    QVERIFY(processing::summarysrc::memoGroupKey(QStringLiteral("  ** ")).isEmpty());
    QCOMPARE(processing::summarysrc::memoGroupLabel(QStringLiteral("open")), QStringLiteral("Nyitott kérdés"));
    QCOMPARE(processing::summarysrc::memoGroupLabel(QStringLiteral("Kockázatok")), QStringLiteral("Kockázatok"));
    QCOMPARE(processing::summarysrc::kindFromString(processing::summarysrc::kindToString(StatementKind::MemoPoint)), StatementKind::MemoPoint);
    QCOMPARE(processing::summarysrc::kindFromString(processing::summarysrc::kindToString(StatementKind::MemoIntro)), StatementKind::MemoIntro);
    QCOMPARE(processing::summarysrc::kindFromString(processing::summarysrc::kindToString(StatementKind::OpenQuestion)), StatementKind::OpenQuestion);
}

// ---- orchestration with a fake LLM ----

void SummarySourcesTest::serviceSingleCallWithMarkers()
{
    FakeProvider llm;
    llm.script = [](int, const providers::LlmRequest&) {
        return QStringLiteral(
            "TOPICS\n### [00:00-01:00] Árazás\nAz árról egyeztettek. [t=00:05]\n- Az ár 100 euró. [t=00:31]\n"
            "### [01:00-02:30] Pilot\n- Két helyszín.\n"
            "SUMMARY\n"
            R"({"execSummary":"Az árról volt szó. [t=00:05] A pilot két helyszínen indul. [t=01:30, t=02:00]",)"
            R"("decisions":["Két helyszín lesz. [t=01:31]"],"openQuestions":[],)"
            R"("actionItems":[{"text":"Ajánlat küldése [t=02:01]","owner":"Ádám","due":""}]})");
    };
    processing::SummaryService svc(&llm);
    QSignalSpy ready(&svc, &processing::SummaryService::summaryReady);
    QSignalSpy failed(&svc, &processing::SummaryService::summaryFailed);
    processing::SummaryRequest req;
    req.transcript = transcriptAt({0, 0.5, 1.0, 1.5, 2.0});
    req.language = QStringLiteral("magyar");
    req.model = QStringLiteral("teszt");
    svc.summarize(req);
    QVERIFY(ready.wait(2000));
    QCOMPARE(failed.count(), 0);
    // The built-in single-pass prompt asks for the markers.
    QVERIFY(llm.requests[0].messages[0].content.contains(QStringLiteral("[t=mm:ss]")));

    const Summary s = qvariant_cast<Summary>(ready[0][0]);
    QCOMPARE(s.execSummary, QStringLiteral("Az árról volt szó. A pilot két helyszínen indul."));
    QCOMPARE(s.decisions, QStringList{QStringLiteral("Két helyszín lesz.")});
    QCOMPARE(s.actionItems.value(0).text, QStringLiteral("Ajánlat küldése"));
    // 2 sentences + decision + action item, then the memo: section 1's intro and point, section 2's point.
    QCOMPARE(s.statements.size(), 7);
    QCOMPARE(s.statements[4].id, QStringLiteral("i1.1"));
    QCOMPARE(s.statements[4].kind, StatementKind::MemoIntro);
    QCOMPARE(s.statements[4].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u0")});
    QCOMPARE(s.statements[5].id, QStringLiteral("m1.1"));
    QCOMPARE(s.statements[5].kind, StatementKind::MemoPoint);
    QCOMPARE(s.statements[5].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u30000")});
    QCOMPARE(s.statements[6].id, QStringLiteral("m2.1"));
    QVERIFY(s.statements[6].sourceSpans.isEmpty());          // a point without a marker: no source
    QCOMPARE(s.memo.value(0).intro, QStringLiteral("Az árról egyeztettek."));
    QCOMPARE(s.memo.value(0).points, QStringList{QStringLiteral("Az ár 100 euró.")});
    // Without lines, from the speech blocks: "u<startMs>" ids.
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
    // Two parts (0-15 and 15-30 minutes): notes, then the summary with markers; the lines come
    // from the request (the real segments.json ids and the resolved names).
    QVector<double> mins;
    for (double x = 0; x < 30; x += 0.5) mins << x;
    FakeProvider llm;
    llm.script = [](int, const providers::LlmRequest& r) {
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
    processing::SummaryService svc(&llm);
    QSignalSpy ready(&svc, &processing::SummaryService::summaryReady);
    processing::SummaryRequest req;
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
    QCOMPARE(s.statements.size(), 5);                          // + the two memo points (m1.1, m2.1)
    QCOMPARE(s.statements[3].id, QStringLiteral("m1.1"));
    QCOMPARE(s.statements[0].sourceSpans.size(), 2);
    QCOMPARE(s.statements[1].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u960000")});
    QCOMPARE(s.statements[2].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u180000")});
    QCOMPARE(s.sourceSpeakers.value(QStringLiteral("u180000")).key, QStringLiteral("Beszélő 2"));
    QCOMPARE(s.memo.value(0).speakers,
             QStringList({QStringLiteral("Fehér Gábor"), QStringLiteral("Varga Árpád")}));
}

// ---- storage ----

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
    processing::summarysrc::attachSources(s, threeLines());
    s.statements[0].flagged = true;

    library::SummaryDocument doc;
    doc.exists = true;
    doc.summary = s;
    doc.meta.mode = library::SummaryMode::Quick;
    QVERIFY(library::summarystore::save(dir.path(), doc));
    const library::SummaryDocument back = library::summarystore::load(dir.path());
    QCOMPARE(back.summary.statements.size(), 4);              // s1, s2, t1, m1.1
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
    QVERIFY(writeJson(library::summarystore::jsonPath(dir.path()), QJsonDocument(root)));
    const library::SummaryDocument back = library::summarystore::load(dir.path());
    QVERIFY(back.exists);
    QCOMPARE(back.summary.execSummary, QStringLiteral("Régi összefoglaló."));
    QVERIFY(back.summary.statements.isEmpty());
    QVERIFY(back.summary.sourceSpeakers.isEmpty());
    QVERIFY(back.summary.memo.value(0).speakers.isEmpty());
    // Nothing to mark.
    QVERIFY(!library::summarystore::setStatementFlagged(dir.path(), QStringLiteral("s1")));
}

void SummarySourcesTest::flagStatementKeepsJsonMtime()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Summary s;
    s.execSummary = QStringLiteral("Első. [t=01:00] Második. [t=02:00]");
    processing::summarysrc::attachSources(s, threeLines());
    library::SummaryDocument doc;
    doc.exists = true;
    doc.summary = s;
    QVERIFY(library::summarystore::save(dir.path(), doc));
    const QString path = library::summarystore::jsonPath(dir.path());
    {
        // An older modification time (as if summary.md had been hand-edited since).
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
    }
    const QDateTime before = QFileInfo(path).lastModified();
    QVERIFY(library::summarystore::setStatementFlagged(dir.path(), QStringLiteral("s2")));
    QCOMPARE(QFileInfo(path).lastModified().toSecsSinceEpoch(), before.toSecsSinceEpoch());
    const library::SummaryDocument back = library::summarystore::load(dir.path());
    QVERIFY(!back.summary.statements[0].flagged);
    QVERIFY(back.summary.statements[1].flagged);
    QVERIFY(!library::summarystore::setStatementFlagged(dir.path(), QStringLiteral("nincs-ilyen")));
}

void SummarySourcesTest::handEditedMarkdownKeepsMatchingStatements()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Summary s;
    s.execSummary = QStringLiteral("Első mondat. [t=01:00] Második mondat. [t=02:00]");
    processing::summarysrc::attachSources(s, threeLines());
    library::SummaryDocument doc;
    doc.exists = true;
    doc.summary = s;
    QVERIFY(library::summarystore::save(dir.path(), doc));
    {
        QFile f(library::summarystore::jsonPath(dir.path()));
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
    }
    // In the hand-edited markdown only the first sentence stayed unchanged.
    QFile md(library::summarystore::markdownPath(dir.path()));
    QVERIFY(md.open(QIODevice::WriteOnly));
    md.write(QStringLiteral("## Vezetői összefoglaló\n\nElső mondat. Átírt második.\n").toUtf8());
    md.close();
    const library::SummaryDocument back = library::summarystore::load(dir.path());
    QVERIFY(back.fromMarkdown);
    QCOMPARE(back.summary.statements.size(), 1);
    QCOMPARE(back.summary.statements[0].id, QStringLiteral("s1"));
}

// ---- targeted staleness ----

void SummarySourcesTest::targetedStalenessPure()
{
    Summary s;
    s.execSummary = QStringLiteral("Fehér Gábor szerint jó. [t=01:00] Varga Árpád hallgatott. [t=02:00]");
    s.decisions = {QStringLiteral("Marad az ár. [t=01:05]")};
    ActionItem ai;
    ai.text = QStringLiteral("Ajánlat [t=01:10]");
    ai.owner = QStringLiteral("Fehér Gábor");
    s.actionItems = {ai};
    processing::summarysrc::attachSources(s, {line(QStringLiteral("u60000"), 60000, 80000, QStringLiteral("Beszélő 1"), QStringLiteral("Fehér Gábor")),
                      line(QStringLiteral("u120000"), 120000, 150000, QStringLiteral("Beszélő 2"), QStringLiteral("Varga Árpád"))});

    // Now: the u60000 line became Varga Árpád's (a manual reassignment to Speaker 2).
    QHash<QString, SourceSpeaker> now{
        {QStringLiteral("u60000"), {QStringLiteral("Beszélő 2"), QStringLiteral("Varga Árpád")}},
        {QStringLiteral("u120000"), {QStringLiteral("Beszélő 2"), QStringLiteral("Varga Árpád")}}};
    QVector<SummaryStatement> sts = s.statements;
    SummaryStaleInfo info;
    summarysrc::applyTargetedStaleness(sts, s.sourceSpeakers, now, {QStringLiteral("Beszélő 2")}, info);
    QVERIFY(info.targeted);
    QCOMPARE(info.affectedStatements, 2);     // s1 + d1
    QCOMPARE(info.affectedTodos, 1);          // t1
    QCOMPARE(info.ownerChanges, 1);
    QCOMPARE(info.affectedStatementIds, QStringList({QStringLiteral("s1"), QStringLiteral("d1"), QStringLiteral("t1")}));
    QCOMPARE(info.affectedUtteranceIds, QStringList{QStringLiteral("u60000")});
    QCOMPARE(sts[0].staleBecause, QStringList{QStringLiteral("Fehér Gábor → Varga Árpád?")});
    QVERIFY(sts[1].staleBecause.isEmpty());
    QCOMPARE(sts[3].ownerStaleBecause, QStringLiteral("Fehér Gábor → Varga Árpád?"));

    // After "Fine as is" (no fixed key) nothing is affected.
    sts = s.statements;
    SummaryStaleInfo ok;
    applyTargetedStaleness(sts, s.sourceSpeakers, now, {}, ok);
    QVERIFY(ok.targeted);
    QCOMPARE(ok.affectedStatements + ok.affectedTodos, 0);
    // Fixing another speaker does not affect these lines.
    SummaryStaleInfo other;
    applyTargetedStaleness(sts, s.sourceSpeakers, now, {QStringLiteral("Beszélő 7")}, other);
    QCOMPARE(other.affectedStatements + other.affectedTodos, 0);
    // For claims without a source there is no targeted flag (the whole-document flag is the fallback).
    QVector<SummaryStatement> bare{SummaryStatement{}};
    SummaryStaleInfo fb;
    applyTargetedStaleness(bare, {}, {}, {QStringLiteral("Beszélő 2")}, fb);
    QVERIFY(!fb.targeted);
}

void SummarySourcesTest::targetedStalenessFromOverlay()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Transcript lines (transcript.segments.json) with two raw speakers.
    QJsonArray segs;
    auto seg = [&](qint64 s, qint64 e, const char* sp) {
        segs.append(QJsonObject{{"startMs", double(s)}, {"endMs", double(e)}, {"speaker", sp}, {"text", "x"}});
    };
    seg(60000, 80000, "Beszélő 1");
    seg(120000, 150000, "Beszélő 2");
    seg(300000, 310000, "Beszélő 1");
    QVERIFY(writeJson(editing::speakeredit::segmentsPath(dir.path()), QJsonDocument(segs)));

    Meeting m;
    m.id = QStringLiteral("m1");
    m.folder = dir.path();
    m.hasSummary = true;
    m.speakerMap = {{QStringLiteral("Beszélő 1"), QStringLiteral("Fehér Gábor")},
                    {QStringLiteral("Beszélő 2"), QStringLiteral("Varga Árpád")}};

    // A summary with the current state (the speaker snapshot from resolvedSourceLines).
    const QVector<summarysrc::SourceLine> lines = editing::speakeredit::resolvedSourceLines(m);
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines[0].speakerName, QStringLiteral("Fehér Gábor"));
    Summary s;
    s.execSummary = QStringLiteral("Gábor vállalta az ajánlatot. [t=05:00] Árpád kérdezett. [t=02:00]");
    ActionItem ai;
    ai.text = QStringLiteral("Ajánlat küldése [t=05:01]");
    ai.owner = QStringLiteral("Fehér Gábor");
    s.actionItems = {ai};
    processing::summarysrc::attachSources(s, lines);
    library::SummaryDocument doc;
    doc.exists = true;
    doc.summary = s;
    QVERIFY(library::summarystore::save(dir.path(), doc));

    // Without a fix: targeted, but nothing is affected.
    SummaryStaleInfo info0 = editing::speakeredit::summaryStale(m);
    editing::speakeredit::fillTargetedStale(m, info0);
    QVERIFY(!info0.stale);
    QVERIFY(info0.targeted);
    QCOMPARE(info0.affectedStatements + info0.affectedTodos, 0);

    // A fake overlay change: the 5:00 line was assigned to Varga Árpád (Speaker 2).
    SpeakerOverlay ov;
    ov.transcriptFingerprint = editing::speakeredit::transcriptFingerprint(editing::speakeredit::loadTranscriptLines(dir.path()));
    OverlayUtterance ou;
    ou.speaker = QStringLiteral("Beszélő 2");
    ou.corrected = true;
    ov.utterances.insert(QStringLiteral("u300000"), ou);
    ov.changedSinceSummary = {QStringLiteral("Beszélő 1"), QStringLiteral("Beszélő 2")};
    QVERIFY(editing::speakeredit::saveOverlay(dir.path(), ov));

    SummaryStaleInfo info = editing::speakeredit::summaryStale(m);
    QVERIFY(info.stale);
    QCOMPARE(info.correctedSpeakers, 2);
    QVector<SummaryStatement> sts;
    editing::speakeredit::fillTargetedStale(m, info, &sts);
    QVERIFY(info.targeted);
    QCOMPARE(info.affectedStatements, 1);
    QCOMPARE(info.affectedTodos, 1);
    QCOMPARE(info.ownerChanges, 1);
    QCOMPARE(info.affectedUtteranceIds, QStringList{QStringLiteral("u300000")});
    QCOMPARE(sts.size(), 3);
    QCOMPARE(sts[0].staleBecause, QStringList{QStringLiteral("Fehér Gábor → Varga Árpád?")});
    QVERIFY(sts[1].staleBecause.isEmpty());
    QCOMPARE(sts[2].ownerStaleBecause, QStringLiteral("Fehér Gábor → Varga Árpád?"));

    // "Fine as is": the flag disappears, the targeted list empties.
    QVERIFY(editing::speakeredit::clearSummaryStale(dir.path()));
    SummaryStaleInfo after = editing::speakeredit::summaryStale(m);
    editing::speakeredit::fillTargetedStale(m, after, &sts);
    QVERIFY(!after.stale);
    QCOMPARE(after.affectedStatements + after.affectedTodos, 0);
    QVERIFY(sts[0].staleBecause.isEmpty());

    // A meeting without a summary: nothing.
    Meeting none = m;
    none.hasSummary = false;
    SummaryStaleInfo ni;
    editing::speakeredit::fillTargetedStale(none, ni, &sts);
    QVERIFY(!ni.targeted);
    QVERIFY(sts.isEmpty());
}

void SummarySourcesTest::memoStatementsRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Summary s;
    s.execSummary = QStringLiteral("Összegzés. [t=01:00]");
    s.openQuestions = {QStringLiteral("Mennyi az ár? [t=02:05]")};
    MemoSection m;
    m.title = QStringLiteral("Árazás");
    m.startMs = 60000;
    m.endMs = 310000;
    m.intro = QStringLiteral("Az árról volt szó. [t=01:02]");
    m.points = {QStringLiteral("Az ár 100 euró. [t=02:00]"), QStringLiteral("Drága. [t=05:01]"),
                QStringLiteral("Ki dönt? [t=05:03]")};
    m.pointGroups = {QString(), QStringLiteral("arguments"), QStringLiteral("open")};
    s.memo = {m};
    processing::summarysrc::attachSources(s, threeLines());
    s.statements.last().flagged = true;
    library::SummaryDocument doc;
    doc.exists = true;
    doc.summary = s;
    doc.meta.mode = library::SummaryMode::Quick;
    QVERIFY(library::summarystore::save(dir.path(), doc));

    QFile f(library::summarystore::jsonPath(dir.path()));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 4);

    const library::SummaryDocument back = library::summarystore::load(dir.path());
    QCOMPARE(back.summary.statements.size(), 6);              // s1, q1, i1.1, m1.1–m1.3
    QStringList ids;
    for (const SummaryStatement& st : back.summary.statements) ids << st.id;
    QCOMPARE(ids, QStringList({QStringLiteral("s1"), QStringLiteral("q1"), QStringLiteral("i1.1"),
                               QStringLiteral("m1.1"), QStringLiteral("m1.2"), QStringLiteral("m1.3")}));
    QCOMPARE(back.summary.statements[1].kind, StatementKind::OpenQuestion);
    QCOMPARE(back.summary.statements[2].kind, StatementKind::MemoIntro);
    QCOMPARE(back.summary.statements[5].kind, StatementKind::MemoPoint);
    QVERIFY(back.summary.statements[5].flagged);
    QCOMPARE(back.summary.statements[4].sourceSpans.value(0).utteranceIds, QStringList{QStringLiteral("u300000")});
    const MemoSection& bm = back.summary.memo.value(0);
    QCOMPARE(bm.intro, QStringLiteral("Az árról volt szó."));
    QCOMPARE(bm.points, QStringList({QStringLiteral("Az ár 100 euró."), QStringLiteral("Drága."),
                                     QStringLiteral("Ki dönt?")}));
    QCOMPARE(bm.pointGroups, m.pointGroups);
    QVERIFY(library::summarystore::setStatementFlagged(dir.path(), QStringLiteral("q1")));

    // Read back from summary.md the memo is the same too (hand edit / old path).
    const library::SummaryDocument md = library::summarystore::parseMarkdown(back.summary.renderMarkdown());
    QCOMPARE(md.summary.memo.size(), 1);
    QCOMPARE(md.summary.memo[0].intro, bm.intro);
    QCOMPARE(md.summary.memo[0].points, bm.points);
    QCOMPARE(md.summary.memo[0].pointGroups, bm.pointGroups);
    QCOMPARE(md.summary.openQuestions, QStringList{QStringLiteral("Mennyi az ár?")});
}

void SummarySourcesTest::targetedStalenessCoversMemoAndQuestions()
{
    Summary s;
    s.openQuestions = {QStringLiteral("Mennyi az ár? [t=02:05]")};
    MemoSection m;
    m.title = QStringLiteral("Árazás");
    m.points = {QStringLiteral("Béla szerint drága. [t=02:10]"), QStringLiteral("Ádám bemutatta. [t=01:05]")};
    s.memo = {m};
    processing::summarysrc::attachSources(s, threeLines());
    // Béla's line (u120000) was fixed to Csaba.
    QHash<QString, SourceSpeaker> now;
    for (auto it = s.sourceSpeakers.cbegin(); it != s.sourceSpeakers.cend(); ++it) now.insert(it.key(), it.value());
    now[QStringLiteral("u120000")] = SourceSpeaker{QStringLiteral("Csaba"), QStringLiteral("Csaba")};
    SummaryStaleInfo info;
    summarysrc::applyTargetedStaleness(s.statements, s.sourceSpeakers, now, {QStringLiteral("Béla")}, info);
    QVERIFY(info.targeted);
    QCOMPARE(info.affectedStatementIds, QStringList({QStringLiteral("q1"), QStringLiteral("m1.1")}));
    QCOMPARE(info.affectedStatements, 2);
    QCOMPARE(info.affectedTodos, 0);
    QCOMPARE(info.affectedUtteranceIds, QStringList{QStringLiteral("u120000")});
    for (const SummaryStatement& st : s.statements)
        if (st.id == QLatin1String("m1.1")) QCOMPARE(st.staleBecause, QStringList{QStringLiteral("Béla → Csaba?")});
}

// ---- prompt -------------------------------------------------------------------------------

void SummarySourcesTest::promptTemplatesAskForMarkers()
{
    for (const char* id : {"single", "merge"}) {
        const QString p = processing::promptBuiltin(QString::fromLatin1(id));
        QVERIFY2(p.contains(QStringLiteral("SOURCE MARKERS")), id);
        QVERIFY2(p.contains(QStringLiteral("[t=mm:ss]")), id);
        QVERIFY2(!p.contains(QLatin1Char('%')), id);        // every %...% placeholder resolved
    }
    QVERIFY(processing::promptBuiltin(QStringLiteral("merge")).contains(QStringLiteral("the time written in the notes")));
    QVERIFY(processing::promptBuiltin(QStringLiteral("single")).contains(QStringLiteral("transcript paragraph")));
    // The note prompt (and the single-pass one's TOPICS part) asks for a marker on the memo
    // points and the intro too, and describes the subsections; the short form asks for the open questions too.
    for (const char* id : {"notes", "single"}) {
        const QString p = processing::promptBuiltin(QString::fromLatin1(id));
        QVERIFY2(p.contains(QStringLiteral("Every gist sentence and every point ends with a source marker")), id);
        QVERIFY2(p.contains(QStringLiteral("#### Arguments")), id);
        QVERIFY2(p.contains(QStringLiteral("#### Unresolved")), id);
    }
    QVERIFY(!processing::promptBuiltin(QStringLiteral("notes")).contains(QStringLiteral("SOURCE MARKERS")));
    QVERIFY(processing::promptBuiltin(QStringLiteral("merge")).contains(QStringLiteral("every open question")));
    QVERIFY(processing::promptBuiltin(QStringLiteral("merge")).contains(QStringLiteral("a [t=mm:ss] marker in TOPICS")));
    // The Settings output schema shows the marker too.
    QVERIFY(processing::promptOutputFormat(QStringLiteral("merge")).body.contains(QStringLiteral("[t=mm:ss]")));
}

QTEST_GUILESS_MAIN(SummarySourcesTest)
#include "test_summary_sources.moc"

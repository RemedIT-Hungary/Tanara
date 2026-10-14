// Recheck from the confirmed lines + "they talked over each other" (noisy line): the pure
// analysis functions (SpeakerAnalysis) and the editor session's persistent path. In a temporary
// folder, with MADE-UP data; the embeddings go straight into the cache (no model, no audio, no ffmpeg).
//
// The scenario: under diarization's "Speaker 1" (Anna) label 3 lines got in that are really
// Béla's voice (P: Béla's voice with noise, it also resembles Anna a little). They pull Anna's
// centroid away, so the regular verdict does not flag them. If we confirm 3 of Anna's real
// lines, the recheck finds exactly these 3 lines doubtful, with Béla as the suggestion.
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/domain/Meeting.h"
#include "tanara/domain/Overlay.h"
#include "tanara/domain/Voice.h"
#include "tanara/editing/review/SpeakerAnalysis.h"
#include "../support/EditSessionHarness.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"

#include <cmath>
#include <memory>

using namespace tanara;
using namespace tanara::editing::speakeredit;

namespace {

const QString kB1 = QStringLiteral("Beszélő 1");
const QString kB2 = QStringLiteral("Beszélő 2");

// The "voices": A = Anna, B = Béla, P = Béla's noisy voice (cos(P,B) = 0.8, cos(P,A) = 0.6).
const QVector<float> kA{1.0f, 0.0f, 0.0f};
const QVector<float> kB{0.0f, 1.0f, 0.0f};
const QVector<float> kP{0.6f, 0.8f, 0.0f};

struct Row { qint64 startMs; qint64 durMs; QString raw; char voice; };   // voice: A/B/P, '-' = none

const QVector<Row>& scenario()
{
    static const QVector<Row> rows{
        {0,     4000, kB1, 'A'},    // 0
        {5000,  4000, kB2, 'B'},    // 1
        {10000, 4000, kB1, 'A'},    // 2
        {15000, 4000, kB1, 'P'},    // 3  really Béla
        {20000, 4000, kB2, 'B'},    // 4
        {25000, 4000, kB1, 'A'},    // 5
        {30000, 4000, kB1, 'P'},    // 6  really Béla
        {35000, 4000, kB2, 'B'},    // 7
        {40000, 4000, kB1, 'A'},    // 8
        {45000, 4000, kB1, 'P'},    // 9  really Béla
        {50000, 4000, kB2, 'B'},    // 10
        {55000, 4000, kB2, 'B'},    // 11 Anna interjects ("yes, yes"), so overlap
        {56500, 1200, kB1, '-'},    // 12 the interjection (short, no embedding)
        {60000, 4000, kB2, 'B'},    // 13
    };
    return rows;
}

QString uid(int row) { return QStringLiteral("u%1").arg(scenario()[row].startMs); }
QStringList uids(std::initializer_list<int> rows)
{
    QStringList out;
    for (int r : rows) out << uid(r);
    return out;
}

const QVector<float>& voiceVec(char v)
{
    static const QVector<float> none;
    switch (v) {
    case 'A': return kA;
    case 'B': return kB;
    case 'P': return kP;
    default:  return none;
    }
}

class NullEmbedder : public processing::IUtteranceEmbedder {
public:
    bool open(const QString&) override { return false; }
    QVector<float> embed(qint64, qint64) override { return {}; }
};

struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<library::MeetingRepository> store;
    std::unique_ptr<library::PeopleStore> people;
    std::unique_ptr<library::VoiceprintStore> prints;
    Meeting meeting;

    explicit Fixture(bool withCache = true)
    {
        store = std::make_unique<library::MeetingRepository>(dir.filePath(QStringLiteral("rec")),
                                               dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<library::PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<library::VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
        meeting = store->createMeeting(QStringLiteral("Újraellenőrzés"));
        QJsonArray segs;
        int i = 0;
        for (const Row& r : scenario()) {
            segs.append(QJsonObject{{"startMs", double(r.startMs)}, {"endMs", double(r.startMs + r.durMs)},
                                    {"speaker", r.raw}, {"text", QStringLiteral("Sor %1").arg(i++)}});
        }
        QFile f(editing::speakeredit::segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        meeting.speakerMap = {{kB1, people->ensure(QStringLiteral("Anna"))}, {kB2, people->ensure(QStringLiteral("Béla"))}};
        store->saveMeeting(meeting);
        if (withCache) writeCache();
    }

    void writeCache()
    {
        const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(meeting.folder);
        processing::UtteranceEmbeddingCache c;
        c.fingerprint = editing::speakeredit::transcriptFingerprint(lines);
        for (int i = 0; i < scenario().size(); ++i)
            if (scenario()[i].voice != '-') c.set(QStringLiteral("campplus"), lines[i].id, voiceVec(scenario()[i].voice));
        QVERIFY(c.save(meeting.folder));
    }

    test_support::EditSessionHarness editor(bool withFactory = false)
    {
        auto ed = test_support::EditSessionHarness(store.get(), people.get(), prints.get(), meeting.id);
        if (withFactory) ed.setEmbedderFactory([] { return std::make_unique<NullEmbedder>(); });
        return ed;
    }

    QJsonObject overlayUtterance(const QString& id) const
    {
        QFile f(editing::speakeredit::overlayPath(meeting.folder));
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(f.readAll()).object()
            .value(QStringLiteral("utterances")).toObject().value(id).toObject();
    }
};

// For the pure functions: the scenario's analysis lines (Anna = 0, Béla = 1).
struct PureLines {
    QVector<QVector<float>> vecs;
    QVector<editing::speakeredit::AnalysisLine> lines;
    void add(const QVector<float>& v, int speaker, qint64 dur = 4000, bool locked = false, bool noisy = false)
    {
        vecs.append(v);
        editing::speakeredit::AnalysisLine l;
        l.speaker = speaker;
        l.durationMs = dur;
        l.locked = locked;
        l.noisy = noisy;
        lines.append(l);
    }
    // The vectors' address in the final storage (it no longer moves after append).
    QVector<editing::speakeredit::AnalysisLine> build()
    {
        for (int i = 0; i < lines.size(); ++i) lines[i].embedding = &vecs[i];
        return lines;
    }
};

PureLines pollutedSpeaker(bool lockAnna)
{
    PureLines p;
    for (int i = 0; i < 4; ++i) p.add(kA, 0, 4000, lockAnna && i < 3);   // 0-3 Anna (0-2 confirmed)
    for (int i = 0; i < 3; ++i) p.add(kP, 0);                            // 4-6 Béla's voice under Anna
    for (int i = 0; i < 4; ++i) p.add(kB, 1);                            // 7-10 Béla
    return p;
}

// ---- pairwise review: two SIMILAR voices ----
// a = Anna, b = Béla (cos(a,b) = 0.85; the "similar lines" guard already takes this for one
// person), m = between the two, 0.087 closer to Béla (below the global 0.10 threshold, above
// the pairwise 0.05), c = Csaba (another voice), d = unknown voice.
const QVector<float> pa{1.0f, 0.0f, 0.0f, 0.0f};
const QVector<float> pb{0.85f, 0.5268f, 0.0f, 0.0f};
const QVector<float> pm{0.9063f, 0.4226f, 0.0f, 0.0f};
const QVector<float> pc{0.0f, 0.0f, 1.0f, 0.0f};
const QVector<float> pd{0.0f, 0.0f, 0.0f, 1.0f};

const QString kB3 = QStringLiteral("Beszélő 3");

struct PairRow { QString raw; char voice; };
const QVector<PairRow>& pairScenario()
{
    static const QVector<PairRow> rows{
        {kB1, 'a'},     // 0  Anna (to be confirmed)
        {kB2, 'b'},     // 1  Béla (to be confirmed)
        {kB1, 'a'},     // 2  Anna (to be confirmed)
        {kB1, 'b'},     // 3  really Béla
        {kB2, 'b'},     // 4  Béla (to be confirmed)
        {kB1, 'a'},     // 5  Anna (to be confirmed)
        {kB3, 'c'},     // 6  Csaba (to be confirmed)
        {kB1, 'b'},     // 7  really Béla
        {kB2, 'b'},     // 8  Béla (to be confirmed)
        {kB3, 'c'},     // 9  Csaba (to be confirmed)
        {kB1, 'm'},     // 10 between the two, closer to Béla
        {kB3, 'c'},     // 11 Csaba (to be confirmed)
        {kB3, 'a'},     // 12 really Anna, under Csaba (the pairwise review does not touch it)
        {kB1, 'd'},     // 13 unknown voice under Anna
        {kB2, 'b'},     // 14 Béla
        {kB1, 'b'},     // 15 really Béla
        {kB3, 'c'},     // 16 Csaba
        {kB2, 'a'},     // 17 really Anna, under Béla
    };
    return rows;
}
QString pid(int row) { return QStringLiteral("u%1").arg(qint64(row) * 5000); }
QStringList pids(std::initializer_list<int> rows)
{
    QStringList out;
    for (int r : rows) out << pid(r);
    return out;
}
const QVector<float>& pairVec(char v)
{
    switch (v) {
    case 'a': return pa;
    case 'b': return pb;
    case 'm': return pm;
    case 'c': return pc;
    default:  return pd;
    }
}

struct PairFixture {
    QTemporaryDir dir;
    std::unique_ptr<library::MeetingRepository> store;
    std::unique_ptr<library::PeopleStore> people;
    std::unique_ptr<library::VoiceprintStore> prints;
    Meeting meeting;

    PairFixture()
    {
        store = std::make_unique<library::MeetingRepository>(dir.filePath(QStringLiteral("rec")),
                                               dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<library::PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<library::VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
        meeting = store->createMeeting(QStringLiteral("Páros átnézés"));
        QJsonArray segs;
        for (int i = 0; i < pairScenario().size(); ++i)
            segs.append(QJsonObject{{"startMs", double(i * 5000)}, {"endMs", double(i * 5000 + 4000)},
                                    {"speaker", pairScenario()[i].raw},
                                    {"text", QStringLiteral("Sor %1").arg(i)}});
        QFile f(editing::speakeredit::segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        meeting.speakerMap = {{kB1, people->ensure(QStringLiteral("Anna"))}, {kB2, people->ensure(QStringLiteral("Béla"))},
                              {kB3, people->ensure(QStringLiteral("Csaba"))}};
        store->saveMeeting(meeting);
        const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(meeting.folder);
        processing::UtteranceEmbeddingCache c;
        c.fingerprint = editing::speakeredit::transcriptFingerprint(lines);
        for (int i = 0; i < lines.size(); ++i)
            c.set(QStringLiteral("campplus"), lines[i].id, pairVec(pairScenario()[i].voice));
        c.save(meeting.folder);
    }

    test_support::EditSessionHarness editor()
    {
        return test_support::EditSessionHarness(store.get(), people.get(), prints.get(), meeting.id);
    }

    QJsonObject overlayUtterance(const QString& id) const
    {
        QFile f(editing::speakeredit::overlayPath(meeting.folder));
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(f.readAll()).object()
            .value(QStringLiteral("utterances")).toObject().value(id).toObject();
    }
};

// For the pure functions: Anna = 0, Béla = 1. Anna: 3 confirmed + 2 plain a lines, 3 b lines
// (Béla's); Béla: 3 confirmed + 2 plain b lines, 1 a line (Anna's).
PureLines similarPair(int lockedAnna = 3, int lockedBela = 3)
{
    PureLines p;
    for (int i = 0; i < 5; ++i) p.add(pa, 0, 4000, i < lockedAnna);    // 0–4
    for (int i = 0; i < 3; ++i) p.add(pb, 0);                           // 5-7 Béla's voice under Anna
    for (int i = 0; i < 5; ++i) p.add(pb, 1, 4000, i < lockedBela);    // 8–12
    p.add(pa, 1);                                                       // 13 Anna's voice under Béla
    return p;
}

// Voiceprint prior per speaker index (for the pure functions).
QVector<editing::speakeredit::SpeakerPrior> priorFor(int speakerCount, int speaker, const QVector<QVector<float>>& prior,
                               const QVector<QVector<float>>& local = {}, const QVector<int>& replaced = {})
{
    QVector<editing::speakeredit::SpeakerPrior> out(speakerCount);
    out[speaker].vectors = prior;
    out[speaker].localVectors = local;
    out[speaker].replacedLines = replaced;
    return out;
}

// cos(e, sum of w_i * v_i): for checking the test by hand.
double cosToWeighted(const QVector<float>& e, const QVector<QPair<double, QVector<float>>>& parts)
{
    QVector<double> sum(e.size(), 0.0);
    for (const auto& p : parts)
        for (int k = 0; k < e.size(); ++k) sum[k] += p.first * double(p.second[k]);
    double d = 0.0, n = 0.0;
    for (int k = 0; k < e.size(); ++k) { d += sum[k] * double(e[k]); n += sum[k] * sum[k]; }
    return d / std::sqrt(n);
}

Voiceprint makePrint(const QVector<float>& v, const QString& meetingId, const QString& sampleRef = {})
{
    Voiceprint vp;
    vp.embedding = v;
    vp.sourceMeetingId = meetingId;
    vp.sampleRef = sampleRef;
    return vp;
}

} // namespace

class SpeakerRecheckTest : public QObject {
    Q_OBJECT
private slots:
    // ---- pure functions ----

    void pure_pollutedCentroidHidesMislabeledLines()
    {
        PureLines p = pollutedSpeaker(/*lockAnna*/ true);
        const QVector<editing::speakeredit::AnalysisLine> al = p.build();
        // The regular verdict: Anna's (pulled) centroid hides the 3 wrong lines.
        const QVector<bool> plain = editing::speakeredit::computeUncertain(al, 2);
        QCOMPARE(int(std::count(plain.cbegin(), plain.cend(), true)), 0);

        const RecheckAnalysis r = computeUncertainRechecked(al, 2);
        QCOMPARE(r.flagged(), 3);
        for (int i = 4; i <= 6; ++i) {
            QVERIFY2(r.lines[i].uncertain, qPrintable(QString::number(i)));
            QCOMPARE(r.lines[i].otherSpeaker, 1);
            QVERIFY(r.lines[i].other - r.lines[i].own >= editing::speakeredit::kUncertainMargin);
        }
        QVERIFY(!r.lines[3].uncertain);                 // Anna's unconfirmed line is fine
        for (int i = 7; i <= 10; ++i) QVERIFY(!r.lines[i].uncertain);
        QCOMPARE(r.trustedCore, (QVector<bool>{true, false}));
        QCOMPARE(r.coreLines, (QVector<int>{3, 0}));
        QCOMPARE(r.coreSpeakers(), 1);
        QCOMPARE(r.coreLineTotal(), 3);
        QVERIFY(editing::speakeredit::hasTrustedCore(al, 2));
    }

    void pure_withoutEnoughLockedLines_noCore()
    {
        PureLines p = pollutedSpeaker(/*lockAnna*/ false);
        p.lines[0].locked = p.lines[1].locked = true;    // only 2 confirmed lines
        const QVector<editing::speakeredit::AnalysisLine> al = p.build();
        QVERIFY(!editing::speakeredit::hasTrustedCore(al, 2));
        const RecheckAnalysis r = computeUncertainRechecked(al, 2);
        QCOMPARE(r.coreSpeakers(), 0);
        QCOMPARE(r.flagged(), 0);                       // with the regular centroids the same as today
    }

    void pure_noisyLinesDoNotPolluteAndAreNotFlagged()
    {
        // Base: the contaminated speaker; then 3 NOISY Béla-voiced lines under Anna too (interjections).
        PureLines base = pollutedSpeaker(true);
        const QVector<editing::speakeredit::AnalysisLine> al0 = base.build();
        const QVector<editing::speakeredit::LineFit> fits0 = editing::speakeredit::computeFits(al0, 2);

        PureLines p = pollutedSpeaker(true);
        for (int i = 0; i < 3; ++i) p.add(kB, 0, 4000, /*locked*/ false, /*noisy*/ true);   // 11–13
        p.add(kB, 0, 4000, /* 14 confirmed, noisy */ true, /* 14 confirmed, noisy */ true);                                // 14 confirmed, noisy
        const QVector<editing::speakeredit::AnalysisLine> al = p.build();

        // The regular centroid does not use them either (Anna has enough clean lines).
        const QVector<editing::speakeredit::LineFit> fits = editing::speakeredit::computeFits(al, 2);
        for (int i = 0; i <= 10; ++i) {
            QVERIFY(std::abs(fits[i].own - fits0[i].own) < 1e-9
                    || (std::isnan(fits[i].own) && std::isnan(fits0[i].own)));
        }
        // The recheck's core consists of the clean confirmed lines; it does not flag the noisy lines.
        const RecheckAnalysis r = computeUncertainRechecked(al, 2);
        QCOMPARE(r.coreLines.value(0), 3);
        QCOMPARE(r.flagged(), 3);
        for (int i = 11; i <= 14; ++i) QVERIFY(!r.lines[i].uncertain);
        for (int i = 4; i <= 6; ++i) QVERIFY(r.lines[i].uncertain);
    }

    void pure_noisyLockedLinesCountOnlyWithoutCleanOnes()
    {
        PureLines p = pollutedSpeaker(false);
        p.lines[0].locked = p.lines[1].locked = true;               // 2 clean confirmed
        p.add(kA, 0, 4000, /* + 1 noisy confirmed */ true, /* + 1 noisy confirmed */ true);        // + 1 noisy confirmed
        const QVector<editing::speakeredit::AnalysisLine> al = p.build();
        QVERIFY(editing::speakeredit::hasTrustedCore(al, 2));
        const RecheckAnalysis r = computeUncertainRechecked(al, 2);
        QCOMPARE(r.coreLines.value(0), 3);                          // the noisy one counts too

        // If there are enough clean ones, the noisy one is left out of the core.
        PureLines q = pollutedSpeaker(true);
        q.add(kB, 0, 4000, true, true);
        const RecheckAnalysis r2 = computeUncertainRechecked(q.build(), 2);
        QCOMPARE(r2.coreLines.value(0), 3);
        QCOMPARE(r2.flagged(), 3);
    }

    void pure_overlapDetection()
    {
        const QVector<editing::speakeredit::TimedLine> tl{
            {0, 4000, 0},           // 0: 1000 ms overlap, so noisy (>= 1000 ms)
            {3000, 4000, 1},        // 1: fully overlapped, so noisy
            {5000, 15000, 0},       // 2: 500 ms / 10 s: clean
            {9000, 9500, 1},        // 3: fully overlapped, so noisy
            {20000, 50000, 0},      // 4: long; the overlap is OUTSIDE the embedding window, so clean
            {21000, 23000, 1},      // 5: fully overlapped, so noisy
            {52000, 56000, 0},      // 6: the same speaker overlaps it, so clean
            {53000, 54500, 0},      // 7
            {60000, 61000, 0},      // 8: 400 ms / 1000 ms = 40%, so noisy (ratio rule)
            {60600, 64000, 1},      // 9: 400 ms / 3400 ms, about 12%: clean
        };
        const QVector<bool> noisy = editing::speakeredit::computeOverlapNoisy(tl);
        QCOMPARE(noisy, (QVector<bool>{true, true, false, true, false, true, false, false, true, false}));
    }

    // ---- pairwise review (pure) ----

    void pure_pair_similarVoices_guardBlocksSuggestion_pairFlags()
    {
        PureLines p = similarPair();
        const QVector<editing::speakeredit::AnalysisLine> al = p.build();
        // The "similar lines" suggestion is silent because of the similar voice, and now says so.
        const SuggestOutcome sg = suggestSimilarDetailed(al, /*source*/ 0, /*target*/ 1);
        QVERIFY(sg.lines.isEmpty());
        QVERIFY(sg.blockedBySimilarity);
        QVERIFY(sg.centroidSimilarity > editing::speakeredit::kSuggestMaxCentroidSimilarity);
        QVERIFY(editing::speakeredit::suggestSimilar(al, 0, 1).isEmpty());

        // The pairwise review: no guard, the wrong lines come up in both directions.
        const PairRecheckAnalysis r = computePairRecheck(al, 0, 1);
        QVERIFY(r.valid);
        QCOMPARE(r.refLinesA, 3);
        QCOMPARE(r.refLinesB, 3);
        QVERIFY(!r.fallbackA);
        QVERIFY(!r.fallbackB);
        QVERIFY(std::abs(r.centroidSimilarity - 0.85) < 1e-3);
        QCOMPARE(r.flagged(), 4);
        for (int i : {5, 6, 7}) {
            QVERIFY2(r.lines[i].flagged, qPrintable(QString::number(i)));
            QCOMPARE(r.lines[i].hintedSpeaker, 1);
            QVERIFY(std::abs(r.lines[i].toA - 0.85) < 1e-3);
            QVERIFY(std::abs(r.lines[i].toB - 1.0) < 1e-3);
        }
        QVERIFY(r.lines[13].flagged);
        QCOMPARE(r.lines[13].hintedSpeaker, 0);
        for (int i : {0, 1, 2, 3, 4, 8, 9, 10, 11, 12}) QVERIFY(!r.lines[i].flagged);
        // It does not judge the locked lines (no value), but it does the plain ones.
        QVERIFY(std::isnan(r.lines[0].toA));
        QVERIFY(std::abs(r.lines[3].toA - 1.0) < 1e-3);
    }

    void pure_pair_marginsAndSkippedLines()
    {
        PureLines p = similarPair();
        p.add(pm, 0);                               // 14 m: 0.087 closer to Béla, so doubtful
        p.add(pm, 0, 2000);                         // 15 the same in a short line: 0.10 is needed, so no
        p.add(pb, 0, 2000);                         // 16 short, 0.15, so doubtful
        p.add(pb, 0, 1000);                         // 17 too short, never
        p.add(pb, 0, 4000, false, /* 18 noisy: never */ true);  // 18 noisy: never
        p.add(pb, 0, 4000, /* 19 locked (and noisy: not in the reference either) */ true, true);  // 19 locked (and noisy: not in the reference either)
        const PairRecheckAnalysis r = computePairRecheck(p.build(), 0, 1);
        QCOMPARE(r.refLinesA, 3);
        QVERIFY(r.lines[14].flagged);
        QVERIFY(!r.lines[15].flagged);
        QVERIFY(r.lines[16].flagged);
        QVERIFY(!r.lines[17].flagged);
        QVERIFY(!r.lines[18].flagged);
        QVERIFY(!r.lines[19].flagged);
        // The global recheck (0.10 threshold) does not flag the m line.
        QVERIFY(!editing::speakeredit::computeUncertainRechecked(p.build(), 2).lines[14].uncertain);
    }

    void pure_pair_fallbackWhenFewLockedLines()
    {
        // Anna has only 2 confirmed lines: her reference is all her clean lines (8).
        PureLines p = similarPair(/*lockedAnna*/ 2);
        const PairRecheckAnalysis r = computePairRecheck(p.build(), 0, 1);
        QVERIFY(r.valid);
        QVERIFY(r.fallbackA);
        QVERIFY(!r.fallbackB);
        QCOMPARE(r.refLinesA, 8);
        QCOMPARE(r.refLinesB, 3);
        // They come up with the contaminated reference too (the line measured without itself), but closer.
        for (int i : {5, 6, 7}) QVERIFY(r.lines[i].flagged);
        QVERIFY(r.centroidSimilarity > 0.85);

        // Few lines (2 clean): does not run.
        PureLines q;
        q.add(pa, 0, 4000, true);
        q.add(pa, 0, 4000, true);
        q.add(pa, 0, 4000, false, /*noisy*/ true);
        for (int i = 0; i < 4; ++i) q.add(pb, 1, 4000, true);
        const PairRecheckAnalysis r2 = computePairRecheck(q.build(), 0, 1);
        QVERIFY(!r2.valid);
        QVERIFY(r2.fallbackA);
        QCOMPARE(r2.refLinesA, 2);
        QCOMPARE(r2.flagged(), 0);
        // The same speaker twice / an invalid index: nothing.
        QVERIFY(!editing::speakeredit::computePairRecheck(p.build(), 1, 1).valid);
        QVERIFY(!editing::speakeredit::computePairRecheck(p.build(), 0, -1).valid);
    }

    // ---- pairwise review (RecheckService) ----

    void editor_pairRecheck_replacesOnlyPairMarks_persistsAndUndoes()
    {
        PairFixture fx;
        auto ed = fx.editor();
        QVERIFY(ed->confirmUtterances(pids({0, 2, 5, 1, 4, 8, 6, 9, 11})));

        // First the global recheck: for Anna 3, 7, 15 (Béla), 13 (unknown voice), for Béla 17 (Anna),
        // for Csaba 12 (Anna). The m line (10) stays below the global threshold.
        const editing::RecheckService::RecheckResult g = ed.recheck()->recheckFromConfirmed();
        QCOMPARE(g.flagged, 6);
        QVERIFY(ed->utterance(pid(13)).rechecked);
        QVERIFY(!ed->utterance(pid(10)).rechecked);
        QCOMPARE(ed->utterance(pid(12)).likelySpeakerKey, kB1);

        QSignalSpy undoSpy(ed.get(), &editing::SpeakerEditSession::undoStateChanged);
        const editing::RecheckService::PairRecheckResult r = ed.recheck()->recheckPair(kB1, kB2);
        QVERIFY(r.ran);
        QVERIFY(r.blocker.isEmpty());
        QCOMPARE(r.flagged, 5);
        QCOMPARE(r.refLinesA, 3);
        QCOMPARE(r.refLinesB, 3);
        QVERIFY(!r.fallbackA && !r.fallbackB);
        QVERIFY(std::abs(r.centroidSimilarity - 0.85) < 1e-3);
        QCOMPARE(ed->undoText(), QStringLiteral("Átnézés: Anna és Béla"));
        QVERIFY(undoSpy.count() > 0);

        for (int row : {3, 7, 10, 15}) {
            const editing::EditorUtterance u = ed->utterance(pid(row));
            QVERIFY2(u.rechecked && u.uncertain, qPrintable(QString::number(row)));
            QCOMPARE(u.likelySpeakerKey, kB2);
        }
        QCOMPARE(ed->utterance(pid(17)).likelySpeakerKey, kB1);
        // On the A/B line that this review did not find doubtful, the old flag is replaced ...
        QVERIFY(!ed->utterance(pid(13)).rechecked);
        QVERIFY(!fx.overlayUtterance(pid(13)).contains(QStringLiteral("rechecked")));
        // ... the others' (Csaba) is untouched.
        QVERIFY(ed->utterance(pid(12)).rechecked);
        QCOMPARE(ed->utterance(pid(12)).likelySpeakerKey, kB1);

        // Persistence.
        QCOMPARE(fx.overlayUtterance(pid(10)).value(QStringLiteral("recheckHint")).toString(), kB2);
        {
            auto again = fx.editor();
            QVERIFY(again->utterance(pid(10)).rechecked);
            QCOMPARE(again->utterance(pid(17)).likelySpeakerKey, kB1);
            QVERIFY(!again->utterance(pid(13)).rechecked);
            QVERIFY(again->utterance(pid(12)).rechecked);
        }

        // One undo step: the global state comes back; redo gives the pairwise one.
        ed->undo();
        QVERIFY(ed->utterance(pid(13)).rechecked);
        QVERIFY(!ed->utterance(pid(10)).rechecked);
        QCOMPARE(ed->undoText(), QStringLiteral("Beszélők újraellenőrzése"));
        ed->redo();
        QVERIFY(ed->utterance(pid(10)).rechecked);
        QVERIFY(!ed->utterance(pid(13)).rechecked);

        // A fix / "Fine as is" clears it; an unchanged rerun is not a new step.
        QVERIFY(ed->confirmUtterances({pid(10)}));
        QVERIFY(!ed->utterance(pid(10)).rechecked);
        QVERIFY(ed->moveUtterances({pid(3)}, kB2));
        QVERIFY(!ed->utterance(pid(3)).rechecked);
        ed.recheck()->recheckPair(kB1, kB2);
        const QString undo = ed->undoText();
        QCOMPARE(ed.recheck()->recheckPair(kB1, kB2).flagged, 3);
        QCOMPARE(ed->undoText(), undo);
    }

    void editor_pairRecheck_blockers()
    {
        PairFixture fx;
        auto ed = fx.editor();
        QVERIFY(!ed.recheck()->pairRecheckBlocker(kB1, kB1).isEmpty());
        QVERIFY(!ed.recheck()->pairRecheckBlocker(kB1, QStringLiteral("nincs ilyen")).isEmpty());
        // It runs without confirmation too (with all clean lines; the fallback signals this).
        QVERIFY(ed.recheck()->pairRecheckBlocker(kB1, kB2).isEmpty());
        const editing::RecheckService::PairRecheckResult r = ed.recheck()->recheckPair(kB1, kB2);
        QVERIFY(r.ran);
        QVERIFY(r.fallbackA && r.fallbackB);
        QCOMPARE(r.refLinesA, 8);
        QCOMPARE(r.refLinesB, 5);

        // Few lines: 3 of Csaba's 5 lines go elsewhere.
        QVERIFY(ed->moveUtterances(pids({6, 9, 11}), kB1));
        const QString why = ed.recheck()->pairRecheckBlocker(kB3, kB2);
        QVERIFY2(why.startsWith(QStringLiteral("Csaba hangjához kevés a minta")), qPrintable(why));
        QVERIFY(!ed.recheck()->recheckPair(kB3, kB2).ran);
    }

    void editor_pairOffer_onlyWhenGuardBlocksAndBothHaveConfirmedLines()
    {
        PairFixture fx;
        auto ed = fx.editor();
        // Without confirmed lines: the suggestion is silent (guard), but there is no offer either.
        QVERIFY(ed->moveUtterances({pid(3)}, kB2));
        QVERIFY(!ed->hasPairOffer());
        ed->undo();

        QVERIFY(ed->confirmUtterances(pids({0, 2, 5, 1, 4, 8})));
        QSignalSpy spy(ed.get(), &editing::SpeakerEditSession::suggestionChanged);
        QVERIFY(ed->moveUtterances({pid(3)}, kB2));
        QVERIFY(!ed->hasSuggestion());
        QVERIFY(ed->hasPairOffer());
        QVERIFY(spy.count() > 0);
        const editing::PairRecheckOffer o = ed->pairOffer();
        QCOMPARE(o.sourceSpeakerKey, kB1);
        QCOMPARE(o.targetSpeakerKey, kB2);
        QVERIFY(o.centroidSimilarity > editing::speakeredit::kSuggestMaxCentroidSimilarity);

        // The next edit drops it; after "Not now" this pair does not come up again.
        QVERIFY(ed->confirmUtterances({pid(13)}));
        QVERIFY(!ed->hasPairOffer());
        QVERIFY(ed->moveUtterances({pid(7)}, kB2));
        QVERIFY(ed->hasPairOffer());
        ed->declinePairOffer();
        QVERIFY(!ed->hasPairOffer());
        QVERIFY(ed->moveUtterances({pid(15)}, kB2));
        QVERIFY(!ed->hasPairOffer());
        // The other direction is the same pair.
        QVERIFY(ed->moveUtterances({pid(17)}, kB1));
        QVERIFY(!ed->hasPairOffer());

        // Anonymous target: no offer.
        PairFixture fx2;
        fx2.meeting = fx2.store->load(fx2.meeting.id);
        fx2.meeting.speakerMap.remove(kB2);
        fx2.store->saveMeeting(fx2.meeting);
        auto ed2 = fx2.editor();
        QVERIFY(ed2->confirmUtterances(pids({0, 2, 5, 1, 4, 8})));
        QVERIFY(ed2->moveUtterances({pid(3)}, kB2));
        QVERIFY(!ed2->hasPairOffer());
    }

    // ---- SpeakerEditSession + RecheckService -----------------------------------------------------

    void editor_overlapMarksCrossTalkAutomatically()
    {
        Fixture fx;
        auto ed = fx.editor();
        QStringList noisy;
        for (const editing::EditorUtterance& u : ed->utterances())
            if (u.noisy) noisy << u.id;
        // In the scenario exactly the interjection and the line under it.
        QCOMPARE(noisy, uids({11, 12}));
        QVERIFY(ed->utterance(uid(11)).noisyOverlap);
        // The automatic flag does not persist.
        QVERIFY(!QFile::exists(editing::speakeredit::overlayPath(fx.meeting.folder)));
    }

    void editor_recheckFlagsExactlyTheMislabeledLines()
    {
        Fixture fx;
        auto ed = fx.editor();
        QCOMPARE(ed->uncertainCount(), 0);              // the regular verdict does not see them
        QVERIFY(!ed.recheck()->canRecheck());
        QVERIFY(ed.recheck()->recheckBlocker().contains(QStringLiteral("Előbb erősíts meg vagy javíts legalább 3 sort egy beszélőnél")));
        QVERIFY(!ed.recheck()->recheckFromConfirmed().ran);

        QVERIFY(ed->confirmUtterances(uids({0, 2, 5})));
        QVERIFY(ed.recheck()->canRecheck());
        QVERIFY(ed.recheck()->recheckBlocker().isEmpty());

        QSignalSpy finished(ed.recheck(), &editing::RecheckService::recheckFinished);
        QSignalSpy count(ed.get(), &editing::SpeakerEditSession::uncertainCountChanged);
        QSignalSpy changed(ed.get(), &editing::SpeakerEditSession::utterancesChanged);
        const editing::RecheckService::RecheckResult r = ed.recheck()->recheckFromConfirmed();
        QVERIFY(r.ran);
        QCOMPARE(r.flagged, 3);
        QCOMPARE(r.speakersWithConfirmedCore, 1);
        QCOMPARE(r.confirmedLines, 3);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(count.last().at(0).toInt(), 3);
        QCOMPARE(changed.last().at(0).toStringList(), uids({3, 6, 9}));

        QCOMPARE(ed->uncertainUtteranceIds(), uids({3, 6, 9}));
        for (int row : {3, 6, 9}) {
            const editing::EditorUtterance u = ed->utterance(uid(row));
            QVERIFY(u.uncertain);
            QVERIFY(u.rechecked);
            QCOMPARE(u.likelySpeakerKey, kB2);
        }
        QVERIFY(!ed->utterance(uid(8)).uncertain);       // Anna's unconfirmed line is fine
        QVERIFY(!ed->utterance(uid(11)).uncertain);      // the noisy line is not flagged
        QCOMPARE(ed->undoText(), QStringLiteral("Beszélők újraellenőrzése"));

        // Persistence: still uncertain after a reload, with the suggestion.
        QCOMPARE(fx.overlayUtterance(uid(3)).value(QStringLiteral("rechecked")).toBool(), true);
        QCOMPARE(fx.overlayUtterance(uid(3)).value(QStringLiteral("recheckHint")).toString(), kB2);
        ed.reset();
        auto again = fx.editor();
        QCOMPARE(again->uncertainUtteranceIds(), uids({3, 6, 9}));
        QCOMPARE(again->uncertainCount(), 3);
        QCOMPARE(again->utterance(uid(6)).likelySpeakerKey, kB2);
    }

    void editor_confirmOrCorrectClears_undoRestores()
    {
        Fixture fx;
        auto ed = fx.editor();
        ed->confirmUtterances(uids({0, 2, 5}));
        ed.recheck()->recheckFromConfirmed();
        QCOMPARE(ed->uncertainCount(), 3);

        // "Fine as is": not uncertain; undo: it is again.
        QVERIFY(ed->confirmUtterances({uid(3)}));
        QVERIFY(!ed->utterance(uid(3)).uncertain);
        QVERIFY(!fx.overlayUtterance(uid(3)).contains(QStringLiteral("rechecked")));
        QCOMPARE(ed->uncertainCount(), 2);
        ed->undo();
        QVERIFY(ed->utterance(uid(3)).uncertain);
        QCOMPARE(ed->uncertainCount(), 3);

        // Fix (move to Béla): it ends; undo: it comes back, with the suggestion.
        QVERIFY(ed->moveUtterances({uid(6)}, kB2));
        QVERIFY(!ed->utterance(uid(6)).uncertain);
        QCOMPARE(ed->uncertainCount(), 2);
        ed->undo();
        QVERIFY(ed->utterance(uid(6)).uncertain);
        QCOMPARE(ed->utterance(uid(6)).likelySpeakerKey, kB2);

        // Recomputing the audio analysis (e.g. fixing another line) does not clear the flag.
        QVERIFY(ed->moveUtterances({uid(8)}, kB2));
        QVERIFY(ed->utterance(uid(9)).uncertain);
        ed->undo();

        // Merging with the suggested speaker: the line is already there, so the flag lets go by itself.
        QVERIFY(ed->mergeSpeakers(kB1, kB2));
        QCOMPARE(ed->uncertainCount(), 0);
        ed->undo();
        QCOMPARE(ed->uncertainCount(), 3);
    }

    void editor_secondRecheckReplacesTheSet()
    {
        Fixture fx;
        auto ed = fx.editor();
        ed->confirmUtterances(uids({0, 2, 5}));
        QCOMPARE(ed.recheck()->recheckFromConfirmed().flagged, 3);

        // We confirm one of the doubtful lines as Anna's: the core now contains the P voice too, so
        // the other two are not doubtful, so the next recheck releases them.
        QVERIFY(ed->confirmUtterances({uid(3)}));
        const editing::RecheckService::RecheckResult r = ed.recheck()->recheckFromConfirmed();
        QCOMPARE(r.flagged, 0);
        QCOMPARE(r.confirmedLines, 4);
        QCOMPARE(ed->uncertainCount(), 0);
        QVERIFY(!fx.overlayUtterance(uid(6)).contains(QStringLiteral("rechecked")));
        QVERIFY(fx.overlayUtterance(uid(9)).isEmpty());  // the default entry disappears too

        // With an unchanged result there is no new undo step.
        const QString undo = ed->undoText();
        ed.recheck()->recheckFromConfirmed();
        QCOMPARE(ed->undoText(), undo);
    }

    void editor_blockers()
    {
        {
            Fixture fx(/*withCache*/ false);
            auto ed = fx.editor(/*withFactory*/ false);
            QVERIFY(!ed.recheck()->canRecheck());
            QVERIFY(ed.recheck()->recheckBlocker().contains(QStringLiteral("hangmodell")));
        }
        {
            Fixture fx(/*withCache*/ false);
            auto ed = fx.editor(/*withFactory*/ true);
            QVERIFY(!ed.recheck()->canRecheck());
            QVERIFY(ed.recheck()->recheckBlocker().contains(QStringLiteral("hang-elemzése még nem")));
        }
        {
            // The cache is enough (even without the model): only the confirmed lines are missing.
            Fixture fx;
            auto ed = fx.editor(/*withFactory*/ false);
            ed->confirmUtterances(uids({0, 2, 5}));
            QVERIFY(ed.recheck()->canRecheck());
        }
    }

    void editor_manualNoisy_confirmAndClear()
    {
        Fixture fx;
        auto ed = fx.editor();
        // "Fine as is, but do not use it as a sample".
        QVERIFY(ed->confirmUtterances({uid(4)}, /*asNoisy*/ true));
        editing::EditorUtterance u = ed->utterance(uid(4));
        QVERIFY(u.confirmed);
        QVERIFY(u.noisy);
        QVERIFY(!u.noisyOverlap);
        QCOMPARE(fx.overlayUtterance(uid(4)).value(QStringLiteral("noisy")).toBool(), true);
        QCOMPARE(ed->undoText(), QStringLiteral("1 sor megerősítése (nem hangminta)"));
        // An already confirmed line can get it too.
        QVERIFY(ed->confirmUtterances({uid(1)}));
        QVERIFY(ed->confirmUtterances({uid(1)}, true));
        QVERIFY(!ed->confirmUtterances({uid(1)}, true));    // already that

        // "Usable as a sample": it overrides on an automatically noisy line too, and persists.
        QVERIFY(ed->setUtterancesNoisy({uid(11)}, false));
        QVERIFY(!ed->utterance(uid(11)).noisy);
        QVERIFY(ed->utterance(uid(11)).noisyOverlap);
        QCOMPARE(fx.overlayUtterance(uid(11)).value(QStringLiteral("noisy")).toBool(true), false);
        ed.reset();
        auto again = fx.editor();
        QVERIFY(!again->utterance(uid(11)).noisy);
        QVERIFY(again->utterance(uid(4)).noisy);
        QVERIFY(again->utterance(uid(12)).noisy);
        again->undo();      // new session: no undo stack, so nothing
        QVERIFY(!again->utterance(uid(11)).noisy);
    }

    // ---- stored voiceprints in the reference (SpeakerPrior) ----

    void pure_prior_pullsFewConfirmedLinesTowardTheVoice_capped()
    {
        // Anna's 3 short confirmed lines have a slightly "drifted" voice (u); her earlier voiceprints
        // carry her clean voice (kA). The voiceprint pulls the reference toward Anna's unconfirmed
        // kA line, but only up to the ceiling (a third of the local weight).
        const QVector<float> u{0.8f, 0.6f, 0.0f};
        PureLines p;
        for (int i = 0; i < 3; ++i) p.add(u, 0, 1500, /*locked*/ true);   // 0–2 (4,5 mp)
        p.add(kA, 0);                                                      // 3
        for (int i = 0; i < 3; ++i) p.add(kB, 1, 4000, true);             // 4-6 Béla
        const QVector<editing::speakeredit::AnalysisLine> al = p.build();

        const RecheckAnalysis plain = computeUncertainRechecked(al, 2);
        const RecheckAnalysis withPrior = computeUncertainRechecked(al, 2, priorFor(2, 0, {kA, kA}));
        QVERIFY(std::abs(plain.lines[3].own - 0.8) < 1e-3);
        QVERIFY(withPrior.lines[3].own > plain.lines[3].own + 0.05);
        // Hand computation: 4.5 s * u + (4.5/3 = 1.5 s) * kA.
        QVERIFY(std::abs(withPrior.lines[3].own - cosToWeighted(kA, {{4.5, u}, {1.5, kA}})) < 1e-6);
        const ReferenceInfo& ref = withPrior.refs[0];
        QVERIFY(ref.refLocal());
        QCOMPARE(ref.lines, 3);
        QCOMPARE(ref.priorPrints, 2);
        QCOMPARE(ref.localPrints, 0);
        QVERIFY(ref.priorCapped);                       // 2 × 4 mp > 1,5 mp
        QVERIFY(std::abs(ref.priorShare - 0.25) < 1e-9);
        QVERIFY(withPrior.refs[1].refLocal());
        QCOMPARE(withPrior.refs[1].priorPrints, 0);
        QCOMPARE(withPrior.coreLines, (QVector<int>{3, 3}));

        // No clipping below the ceiling: with 12 s of local weight a 4 s voiceprint just fits.
        PureLines q = pollutedSpeaker(/*lockAnna*/ true);
        const RecheckAnalysis r = computeUncertainRechecked(q.build(), 2, priorFor(2, 0, {kA}));
        QVERIFY(!r.refs[0].priorCapped);
        QVERIFY(std::abs(r.refs[0].priorShare - 0.25) < 1e-9);
        QCOMPARE(r.flagged(), 3);
    }

    void pure_prior_capKeepsLocalDominance_localPrintNotCapped()
    {
        // Anna's 3 confirmed kA lines, Béla's 3 confirmed kB lines. For Anna an unconfirmed x line
        // that is closer to Béla. Anna's 5 earlier voiceprints (another microphone) are kB-like:
        // without clipping they would tip the verdict, clipped they do not.
        const QVector<float> x{0.3f, 0.9539f, 0.0f};
        PureLines p;
        for (int i = 0; i < 3; ++i) p.add(kA, 0, 4000, true);    // 0–2 (12 mp)
        p.add(x, 0);                                               // 3
        for (int i = 0; i < 3; ++i) p.add(kB, 1, 4000, true);    // 4–6
        const QVector<editing::speakeredit::AnalysisLine> al = p.build();
        const QVector<QVector<float>> five{kB, kB, kB, kB, kB};

        QVERIFY(editing::speakeredit::computeUncertainRechecked(al, 2).lines[3].uncertain);
        const RecheckAnalysis capped = computeUncertainRechecked(al, 2, priorFor(2, 0, five));
        QVERIFY(capped.refs[0].priorCapped);
        QVERIFY(capped.lines[3].uncertain);
        QCOMPARE(capped.lines[3].otherSpeaker, 1);
        // Without clipping (5 x 4 s = 20 s > 12 s) Anna's reference would fit x better than Béla's:
        // the line would not be doubtful.
        const double uncappedOwn = cosToWeighted(x, {{12.0, kA}, {20.0, kB}});
        QVERIFY(uncappedOwn > capped.lines[3].other);
        // Clipped: 12 s * kA + 4 s * kB.
        QVERIFY(std::abs(capped.lines[3].own - cosToWeighted(x, {{12.0, kA}, {4.0, kB}})) < 1e-6);

        // The same as voiceprints made HERE are local evidence: a 10 s weight, no clipping, so the
        // line is Anna's (not doubtful).
        const RecheckAnalysis local = computeUncertainRechecked(al, 2, priorFor(2, 0, {}, five));
        QCOMPARE(local.refs[0].localPrints, 5);
        QVERIFY(!local.refs[0].priorCapped);
        QVERIFY(!local.lines[3].uncertain);
        QVERIFY(std::abs(local.lines[3].own - cosToWeighted(x, {{12.0, kA}, {50.0, kB}})) < 1e-6);
    }

    void pure_prior_formsReferenceWithoutLockedLines()
    {
        // No confirmed line: until now the contaminated regular centroid hid the wrong lines.
        PureLines p = pollutedSpeaker(/*lockAnna*/ false);
        const QVector<editing::speakeredit::AnalysisLine> al = p.build();
        QCOMPARE(editing::speakeredit::computeUncertainRechecked(al, 2).flagged(), 0);
        QCOMPARE(editing::speakeredit::computeUncertainRechecked(al, 2).refs[0].kind, ReferenceKind::Fallback);

        // Anna has an earlier voiceprint, which gives her reference (no local core).
        const QVector<editing::speakeredit::SpeakerPrior> pr = priorFor(2, 0, {kA});
        const RecheckAnalysis r = computeUncertainRechecked(al, 2, pr);
        QVERIFY(!editing::speakeredit::hasTrustedCore(al, 2, pr));            // an earlier voiceprint alone is not a local core
        QCOMPARE(r.refs[0].kind, ReferenceKind::Prior);
        QCOMPARE(r.refs[0].lines, 0);
        QCOMPARE(r.refs[0].priorPrints, 1);
        QVERIFY(!r.refs[0].priorCapped);
        QCOMPARE(r.refs[1].kind, ReferenceKind::Fallback);
        QCOMPARE(r.trustedCore, (QVector<bool>{false, false}));
        QCOMPARE(r.flagged(), 3);
        for (int i = 4; i <= 6; ++i) {
            QVERIFY(r.lines[i].uncertain);
            QCOMPARE(r.lines[i].otherSpeaker, 1);
        }

        // A voiceprint made HERE alone is a local core.
        const QVector<editing::speakeredit::SpeakerPrior> lp = priorFor(2, 0, {}, {kA});
        QVERIFY(editing::speakeredit::hasTrustedCore(al, 2, lp));
        const RecheckAnalysis l = computeUncertainRechecked(al, 2, lp);
        QCOMPARE(l.trustedCore, (QVector<bool>{true, false}));
        QCOMPARE(l.refs[0].kind, ReferenceKind::Local);
        QCOMPARE(l.refs[0].lines, 0);
        QCOMPARE(l.refs[0].localPrints, 1);
        QCOMPARE(l.flagged(), 3);
    }

    void pure_prior_localPrintReplacesItsSampleLine()
    {
        // The sample line (0) of the voiceprint made here is itself confirmed: it counts only once (the voiceprint).
        PureLines p = pollutedSpeaker(/*lockAnna*/ true);
        const QVector<editing::speakeredit::AnalysisLine> al = p.build();
        const RecheckAnalysis r = computeUncertainRechecked(al, 2, priorFor(2, 0, {}, {kA}, {0}));
        QCOMPARE(r.refs[0].lines, 2);
        QCOMPARE(r.refs[0].localPrints, 1);
        QCOMPARE(r.coreLines[0], 2);
        QCOMPARE(r.flagged(), 3);
        // The same for the pairwise review.
        const PairRecheckAnalysis pr = computePairRecheck(al, 0, 1, priorFor(2, 0, {}, {kA}, {0}));
        QCOMPARE(pr.refA.lines, 2);
        QCOMPARE(pr.refA.localPrints, 1);
        QVERIFY(pr.refA.refLocal());
        QVERIFY(pr.valid);
    }

    void pure_pair_priorReplacesFallback()
    {
        // Béla has no confirmed line: until now the fallback (all his lines); with an earlier
        // voiceprint that gives his reference.
        PureLines p = similarPair(/*lockedAnna*/ 3, /*lockedBela*/ 0);
        const QVector<editing::speakeredit::AnalysisLine> al = p.build();
        QVERIFY(editing::speakeredit::computePairRecheck(al, 0, 1).fallbackB);
        const PairRecheckAnalysis r = computePairRecheck(al, 0, 1, priorFor(2, 1, {pb}));
        QVERIFY(r.valid);
        QVERIFY(!r.fallbackB);
        QCOMPARE(r.refB.kind, ReferenceKind::Prior);
        QCOMPARE(r.refLinesB, 0);
        QCOMPARE(r.refB.priorPrints, 1);
        QVERIFY(r.refA.refLocal());
        QCOMPARE(r.refA.priorPrints, 0);
        for (int i : {5, 6, 7, 13}) QVERIFY2(r.lines[i].flagged, qPrintable(QString::number(i)));
        for (int i : {8, 9, 10, 11, 12}) QVERIFY(!r.lines[i].flagged);
        QCOMPARE(r.flagged(), 4);

        // A voiceprint made here as a local core: a valid reference even with few lines.
        PureLines q = similarPair(/*lockedAnna*/ 0, /*lockedBela*/ 3);
        const PairRecheckAnalysis l = computePairRecheck(q.build(), 0, 1, priorFor(2, 0, {}, {pa}));
        QVERIFY(l.valid);
        QVERIFY(l.refA.refLocal());
        QCOMPARE(l.refLinesA, 0);
        QCOMPARE(l.refA.localPrints, 1);
        QVERIFY(!l.fallbackA);
    }

    void editor_recheckUsesStoredVoiceprints()
    {
        Fixture fx;
        // Anna's voiceprint made here from line 0; Béla's earlier voiceprint from another meeting;
        // a voiceprint of another dimension (another model), which must be skipped.
        fx.prints->addPrint(fx.people->ensure(QStringLiteral("Anna")),
                            makePrint(kA, fx.meeting.id, QStringLiteral("audio.ogg#0-4000")));
        fx.prints->addPrint(fx.people->ensure(QStringLiteral("Béla")), makePrint(kB, QStringLiteral("masik-megbeszeles")));
        fx.prints->addPrint(fx.people->ensure(QStringLiteral("Béla")),
                            makePrint({0.0f, 1.0f, 0.0f, 0.0f}, QStringLiteral("regi-modell")));
        auto ed = fx.editor();
        // A voiceprint made here is a local core by itself, so the recheck runs without confirmation too.
        QVERIFY(ed.recheck()->canRecheck());

        QVERIFY(ed->confirmUtterances(uids({0, 2, 5})));
        const editing::RecheckService::RecheckResult r = ed.recheck()->recheckFromConfirmed();
        QVERIFY(r.ran);
        QCOMPARE(r.flagged, 3);
        QCOMPARE(ed->uncertainUtteranceIds(), uids({3, 6, 9}));
        QCOMPARE(ed->utterance(uid(3)).likelySpeakerKey, kB2);
        QCOMPARE(r.references.size(), 2);
        const editing::SpeakerReference& anna = r.references[0];
        QCOMPARE(anna.speakerKey, kB1);
        QCOMPARE(anna.lines, 2);                        // line 0 counts in the voiceprint
        QCOMPARE(anna.localPrints, 1);
        QCOMPARE(anna.priorPrints, 0);
        const editing::SpeakerReference& bela = r.references[1];
        QVERIFY(bela.priorOnly);
        QCOMPARE(bela.priorPrints, 1);
        QCOMPARE(r.confirmedLines, 2);
        QCOMPARE(r.referenceSummary(),
                 QStringLiteral("Referencia: Anna 2 sor + 1 itteni lenyomat, Béla 1 korábbi lenyomat."));
        // The basis of the feedback: confirmed lines, stored samples (of which made here), model.
        QCOMPARE(r.localPrints, 1);
        QCOMPARE(r.priorPrints, 1);
        const QString model = processing::VoiceModelRegistry::spec(processing::VoiceModelRegistry::defaultModelId())->displayName;
        QCOMPARE(r.models, QStringList{model});
        QCOMPARE(r.basisSummary(),
                 QStringLiteral("Alapja: 2 megerősített sor · 2 tárolt minta (ebből 1 erről a megbeszélésről) · modell: %1.")
                     .arg(model));
    }

    // The text of the "two very similar voices" warning (decimal separator, the strongest pair).
    void recheck_similarVoicesNote()
    {
        QVERIFY(editing::RecheckService::similarVoicesNote({}).isEmpty());
        editing::SimilarVoices a;
        a.nameA = QStringLiteral("Fehér Gábor");
        a.nameB = QStringLiteral("Varga Árpád");
        a.similarity = 0.9234;
        QCOMPARE(editing::RecheckService::similarVoicesNote({a}),
                 QStringLiteral("Figyelem: Fehér Gábor és Varga Árpád hangja nagyon hasonló (0,92), "
                                "kettejük sorait érdemes átnézni."));
        editing::SimilarVoices b = a;
        b.similarity = 0.91;
        QVERIFY(editing::RecheckService::similarVoicesNote({a, b}).endsWith(QStringLiteral("(További 1 hasonló pár.)")));
        editing::RecheckService::RecheckResult none;
        QVERIFY(none.basisSummary().isEmpty());     // did not run
    }

    void editor_recheckWithoutPrints_noSummary()
    {
        Fixture fx;
        auto ed = fx.editor();
        QVERIFY(ed->confirmUtterances(uids({0, 2, 5})));
        const editing::RecheckService::RecheckResult r = ed.recheck()->recheckFromConfirmed();
        QCOMPARE(r.references.size(), 1);
        QCOMPARE(r.references[0].lines, 3);
        QVERIFY(r.referenceSummary().isEmpty());
        QCOMPARE(r.storedPrints(), 0);
        QVERIFY(r.basisSummary().startsWith(QStringLiteral("Alapja: 3 megerősített sor · tárolt minta nélkül · modell")));
        QVERIFY(r.similar.isEmpty());       // two well separated voices
    }

    void editor_pairRecheckUsesStoredVoiceprints()
    {
        PairFixture fx;
        fx.prints->addPrint(fx.people->ensure(QStringLiteral("Béla")), makePrint(pb, QStringLiteral("masik-megbeszeles")));
        auto ed = fx.editor();
        QVERIFY(ed->confirmUtterances(pids({0, 2, 5, 1})));     // Béla has only 1 confirmed line
        const editing::RecheckService::PairRecheckResult r = ed.recheck()->recheckPair(kB1, kB2);
        QVERIFY(r.ran);
        QVERIFY(!r.fallbackB);
        QVERIFY(r.refB.priorOnly);
        QCOMPARE(r.refB.priorPrints, 1);
        QCOMPARE(r.refA.lines, 3);
        QCOMPARE(r.referenceSummary(), QStringLiteral("Referencia: Anna 3 sor, Béla 1 korábbi lenyomat."));
        for (int row : {3, 7, 15}) QCOMPARE(ed->utterance(pid(row)).likelySpeakerKey, kB2);
        QCOMPARE(ed->utterance(pid(17)).likelySpeakerKey, kB1);
    }

    void editor_voiceprintMaterialSkipsNoisyLines()
    {
        Fixture fx;
        auto ed = fx.editor();
        // Of Béla's 6 long lines the 11th is noisy (overlap), so 5 are usable.
        const editing::VoiceprintMaterial before = ed->voiceprintMaterial(kB2);
        QCOMPARE(before.usableLines, 5);
        QVERIFY(ed->setUtterancesNoisy({uid(13)}, true));
        QCOMPARE(ed->voiceprintMaterial(kB2).usableLines, 4);
        QVERIFY(ed->setUtterancesNoisy({uid(11)}, false));
        QCOMPARE(ed->voiceprintMaterial(kB2).usableLines, 5);
    }
};

QTEST_GUILESS_MAIN(SpeakerRecheckTest)
#include "test_speaker_recheck.moc"

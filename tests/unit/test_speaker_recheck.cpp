//
// Újraellenőrzés a megerősített sorok alapján + „egymásra beszéltek" (zajos sor) — a tiszta
// elemző-függvények (SpeakerAnalysis) és a SpeakerEditor perzisztens útja. Ideiglenes
// mappában, KITALÁLT adatokkal; az embeddingek közvetlenül a cache-be kerülnek (nincs modell,
// nincs hang, nincs ffmpeg).
//
// A forgatókönyv: a diarizáció „Beszélő 1" (Anna) címkéje alá 3 olyan sor is került, amely
// valójában Béla hangja (P: Béla hangja zajjal — Annához is kicsit hasonlít). Anna centroidját
// ezek elhúzzák, így a rendes ítélet nem jelzi őket. Ha Anna 3 valódi sorát megerősítjük, az
// újraellenőrzés pontosan ezt a 3 sort találja kétesnek, Béla javaslattal.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/Types.h"
#include "tanara/edit/SpeakerAnalysis.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/UtteranceEmbeddings.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/VoiceprintStore.h"

#include <cmath>
#include <memory>

using namespace tanara;
using namespace tanara::speakeredit;

namespace {

const QString kB1 = QStringLiteral("Beszélő 1");
const QString kB2 = QStringLiteral("Beszélő 2");

// A „hangok": A = Anna, B = Béla, P = Béla zajos hangja (cos(P,B) = 0.8, cos(P,A) = 0.6).
const QVector<float> kA{1.0f, 0.0f, 0.0f};
const QVector<float> kB{0.0f, 1.0f, 0.0f};
const QVector<float> kP{0.6f, 0.8f, 0.0f};

struct Row { qint64 startMs; qint64 durMs; QString raw; char voice; };   // voice: A/B/P, '-' = nincs

const QVector<Row>& scenario()
{
    static const QVector<Row> rows{
        {0,     4000, kB1, 'A'},    // 0
        {5000,  4000, kB2, 'B'},    // 1
        {10000, 4000, kB1, 'A'},    // 2
        {15000, 4000, kB1, 'P'},    // 3  valójában Béla
        {20000, 4000, kB2, 'B'},    // 4
        {25000, 4000, kB1, 'A'},    // 5
        {30000, 4000, kB1, 'P'},    // 6  valójában Béla
        {35000, 4000, kB2, 'B'},    // 7
        {40000, 4000, kB1, 'A'},    // 8
        {45000, 4000, kB1, 'P'},    // 9  valójában Béla
        {50000, 4000, kB2, 'B'},    // 10
        {55000, 4000, kB2, 'B'},    // 11 Anna közbeszól („igen, igen") → átfedés
        {56500, 1200, kB1, '-'},    // 12 a közbeszólás (rövid, nincs embedding)
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

class NullEmbedder : public IUtteranceEmbedder {
public:
    bool open(const QString&) override { return false; }
    QVector<float> embed(qint64, qint64) override { return {}; }
};

struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<MeetingStore> store;
    std::unique_ptr<PeopleStore> people;
    std::unique_ptr<VoiceprintStore> prints;
    Meeting meeting;

    explicit Fixture(bool withCache = true)
    {
        store = std::make_unique<MeetingStore>(dir.filePath(QStringLiteral("rec")),
                                               dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
        meeting = store->createMeeting(QStringLiteral("Újraellenőrzés"));
        QJsonArray segs;
        int i = 0;
        for (const Row& r : scenario()) {
            segs.append(QJsonObject{{"startMs", double(r.startMs)}, {"endMs", double(r.startMs + r.durMs)},
                                    {"speaker", r.raw}, {"text", QStringLiteral("Sor %1").arg(i++)}});
        }
        QFile f(segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        meeting.speakerMap = {{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Béla")}};
        store->saveMeeting(meeting);
        if (withCache) writeCache();
    }

    void writeCache()
    {
        const QVector<TranscriptLine> lines = loadTranscriptLines(meeting.folder);
        UtteranceEmbeddingCache c;
        c.fingerprint = transcriptFingerprint(lines);
        for (int i = 0; i < scenario().size(); ++i)
            if (scenario()[i].voice != '-') c.set(QStringLiteral("campplus"), lines[i].id, voiceVec(scenario()[i].voice));
        QVERIFY(c.save(meeting.folder));
    }

    std::unique_ptr<SpeakerEditor> editor(bool withFactory = false)
    {
        auto ed = std::make_unique<SpeakerEditor>(store.get(), people.get(), prints.get(), meeting.id);
        if (withFactory) ed->setEmbedderFactory([] { return std::make_unique<NullEmbedder>(); });
        return ed;
    }

    QJsonObject overlayUtterance(const QString& id) const
    {
        QFile f(overlayPath(meeting.folder));
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(f.readAll()).object()
            .value(QStringLiteral("utterances")).toObject().value(id).toObject();
    }
};

// A tiszta függvényekhez: a forgatókönyv elemző-sorai (Anna = 0, Béla = 1).
struct PureLines {
    QVector<QVector<float>> vecs;
    QVector<AnalysisLine> lines;
    void add(const QVector<float>& v, int speaker, qint64 dur = 4000, bool locked = false, bool noisy = false)
    {
        vecs.append(v);
        AnalysisLine l;
        l.speaker = speaker;
        l.durationMs = dur;
        l.locked = locked;
        l.noisy = noisy;
        lines.append(l);
    }
    // A vektorok címe a végleges tárolóban (az append után már nem mozdul).
    QVector<AnalysisLine> build()
    {
        for (int i = 0; i < lines.size(); ++i) lines[i].embedding = &vecs[i];
        return lines;
    }
};

PureLines pollutedSpeaker(bool lockAnna)
{
    PureLines p;
    for (int i = 0; i < 4; ++i) p.add(kA, 0, 4000, lockAnna && i < 3);   // 0–3 Anna (0–2 megerősítve)
    for (int i = 0; i < 3; ++i) p.add(kP, 0);                            // 4–6 Béla hangja Annánál
    for (int i = 0; i < 4; ++i) p.add(kB, 1);                            // 7–10 Béla
    return p;
}

// ---- páronkénti átnézés: két HASONLÓ hang ----------------------------------
// a = Anna, b = Béla (cos(a,b) = 0.85 — a „hasonló sorok" őre ezt már egy embernek veszi),
// m = a kettő között, Bélához 0.087-tel közelebb (a globális 0.10-es küszöb alatt, a páronkénti
// 0.05 fölött), c = Csaba (más hang), d = ismeretlen hang.
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
        {kB1, 'a'},     // 0  Anna (megerősítendő)
        {kB2, 'b'},     // 1  Béla (megerősítendő)
        {kB1, 'a'},     // 2  Anna (megerősítendő)
        {kB1, 'b'},     // 3  valójában Béla
        {kB2, 'b'},     // 4  Béla (megerősítendő)
        {kB1, 'a'},     // 5  Anna (megerősítendő)
        {kB3, 'c'},     // 6  Csaba (megerősítendő)
        {kB1, 'b'},     // 7  valójában Béla
        {kB2, 'b'},     // 8  Béla (megerősítendő)
        {kB3, 'c'},     // 9  Csaba (megerősítendő)
        {kB1, 'm'},     // 10 a kettő között, Bélához közelebb
        {kB3, 'c'},     // 11 Csaba (megerősítendő)
        {kB3, 'a'},     // 12 valójában Anna — Csabánál (a páros átnézés nem nyúl hozzá)
        {kB1, 'd'},     // 13 ismeretlen hang Annánál
        {kB2, 'b'},     // 14 Béla
        {kB1, 'b'},     // 15 valójában Béla
        {kB3, 'c'},     // 16 Csaba
        {kB2, 'a'},     // 17 valójában Anna — Bélánál
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
    std::unique_ptr<MeetingStore> store;
    std::unique_ptr<PeopleStore> people;
    std::unique_ptr<VoiceprintStore> prints;
    Meeting meeting;

    PairFixture()
    {
        store = std::make_unique<MeetingStore>(dir.filePath(QStringLiteral("rec")),
                                               dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
        meeting = store->createMeeting(QStringLiteral("Páros átnézés"));
        QJsonArray segs;
        for (int i = 0; i < pairScenario().size(); ++i)
            segs.append(QJsonObject{{"startMs", double(i * 5000)}, {"endMs", double(i * 5000 + 4000)},
                                    {"speaker", pairScenario()[i].raw},
                                    {"text", QStringLiteral("Sor %1").arg(i)}});
        QFile f(segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        meeting.speakerMap = {{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Béla")},
                              {kB3, QStringLiteral("Csaba")}};
        store->saveMeeting(meeting);
        const QVector<TranscriptLine> lines = loadTranscriptLines(meeting.folder);
        UtteranceEmbeddingCache c;
        c.fingerprint = transcriptFingerprint(lines);
        for (int i = 0; i < lines.size(); ++i)
            c.set(QStringLiteral("campplus"), lines[i].id, pairVec(pairScenario()[i].voice));
        c.save(meeting.folder);
    }

    std::unique_ptr<SpeakerEditor> editor()
    {
        return std::make_unique<SpeakerEditor>(store.get(), people.get(), prints.get(), meeting.id);
    }

    QJsonObject overlayUtterance(const QString& id) const
    {
        QFile f(overlayPath(meeting.folder));
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(f.readAll()).object()
            .value(QStringLiteral("utterances")).toObject().value(id).toObject();
    }
};

// Tiszta függvényekhez: Anna = 0, Béla = 1. Anna: 3 megerősített + 2 sima a-sor, 3 b-sor
// (Béláé); Béla: 3 megerősített + 2 sima b-sor, 1 a-sor (Annáé).
PureLines similarPair(int lockedAnna = 3, int lockedBela = 3)
{
    PureLines p;
    for (int i = 0; i < 5; ++i) p.add(pa, 0, 4000, i < lockedAnna);    // 0–4
    for (int i = 0; i < 3; ++i) p.add(pb, 0);                           // 5–7 Béla hangja Annánál
    for (int i = 0; i < 5; ++i) p.add(pb, 1, 4000, i < lockedBela);    // 8–12
    p.add(pa, 1);                                                       // 13 Anna hangja Bélánál
    return p;
}

// Lenyomat-prior beszélő-indexenként (a tiszta függvényekhez).
QVector<SpeakerPrior> priorFor(int speakerCount, int speaker, const QVector<QVector<float>>& prior,
                               const QVector<QVector<float>>& local = {}, const QVector<int>& replaced = {})
{
    QVector<SpeakerPrior> out(speakerCount);
    out[speaker].vectors = prior;
    out[speaker].localVectors = local;
    out[speaker].replacedLines = replaced;
    return out;
}

// cos(e, Σ wᵢ·vᵢ) — a teszt kézi ellenőrzéséhez.
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
    // ---- tiszta függvények -------------------------------------------------

    void pure_pollutedCentroidHidesMislabeledLines()
    {
        PureLines p = pollutedSpeaker(/*lockAnna*/ true);
        const QVector<AnalysisLine> al = p.build();
        // A rendes ítélet: Anna (elhúzott) centroidja eltakarja a 3 téves sort.
        const QVector<bool> plain = computeUncertain(al, 2);
        QCOMPARE(int(std::count(plain.cbegin(), plain.cend(), true)), 0);

        const RecheckAnalysis r = computeUncertainRechecked(al, 2);
        QCOMPARE(r.flagged(), 3);
        for (int i = 4; i <= 6; ++i) {
            QVERIFY2(r.lines[i].uncertain, qPrintable(QString::number(i)));
            QCOMPARE(r.lines[i].otherSpeaker, 1);
            QVERIFY(r.lines[i].other - r.lines[i].own >= kUncertainMargin);
        }
        QVERIFY(!r.lines[3].uncertain);                 // Anna megerősítetlen sora rendben
        for (int i = 7; i <= 10; ++i) QVERIFY(!r.lines[i].uncertain);
        QCOMPARE(r.trustedCore, (QVector<bool>{true, false}));
        QCOMPARE(r.coreLines, (QVector<int>{3, 0}));
        QCOMPARE(r.coreSpeakers(), 1);
        QCOMPARE(r.coreLineTotal(), 3);
        QVERIFY(hasTrustedCore(al, 2));
    }

    void pure_withoutEnoughLockedLines_noCore()
    {
        PureLines p = pollutedSpeaker(/*lockAnna*/ false);
        p.lines[0].locked = p.lines[1].locked = true;    // csak 2 megerősített sor
        const QVector<AnalysisLine> al = p.build();
        QVERIFY(!hasTrustedCore(al, 2));
        const RecheckAnalysis r = computeUncertainRechecked(al, 2);
        QCOMPARE(r.coreSpeakers(), 0);
        QCOMPARE(r.flagged(), 0);                       // a rendes centroidokkal ugyanaz, mint ma
    }

    void pure_noisyLinesDoNotPolluteAndAreNotFlagged()
    {
        // Alap: a szennyezett beszélő; utána 3 ZAJOS Béla-hangú sor is Annánál (közbeszólások).
        PureLines base = pollutedSpeaker(true);
        const QVector<AnalysisLine> al0 = base.build();
        const QVector<LineFit> fits0 = computeFits(al0, 2);

        PureLines p = pollutedSpeaker(true);
        for (int i = 0; i < 3; ++i) p.add(kB, 0, 4000, /*locked*/ false, /*noisy*/ true);   // 11–13
        p.add(kB, 0, 4000, /*locked*/ true, /*noisy*/ true);                                // 14 megerősítve, zajos
        const QVector<AnalysisLine> al = p.build();

        // A rendes centroid sem használja őket (Annának van elég tiszta sora).
        const QVector<LineFit> fits = computeFits(al, 2);
        for (int i = 0; i <= 10; ++i) {
            QVERIFY(std::abs(fits[i].own - fits0[i].own) < 1e-9
                    || (std::isnan(fits[i].own) && std::isnan(fits0[i].own)));
        }
        // Az újraellenőrzés magja a tiszta megerősített sorokból áll; a zajos sorokat nem jelöli.
        const RecheckAnalysis r = computeUncertainRechecked(al, 2);
        QCOMPARE(r.coreLines.value(0), 3);
        QCOMPARE(r.flagged(), 3);
        for (int i = 11; i <= 14; ++i) QVERIFY(!r.lines[i].uncertain);
        for (int i = 4; i <= 6; ++i) QVERIFY(r.lines[i].uncertain);
    }

    void pure_noisyLockedLinesCountOnlyWithoutCleanOnes()
    {
        PureLines p = pollutedSpeaker(false);
        p.lines[0].locked = p.lines[1].locked = true;               // 2 tiszta megerősített
        p.add(kA, 0, 4000, /*locked*/ true, /*noisy*/ true);        // + 1 zajos megerősített
        const QVector<AnalysisLine> al = p.build();
        QVERIFY(hasTrustedCore(al, 2));
        const RecheckAnalysis r = computeUncertainRechecked(al, 2);
        QCOMPARE(r.coreLines.value(0), 3);                          // a zajos is beszámít

        // Ha van elég tiszta, a zajos kimarad a magból.
        PureLines q = pollutedSpeaker(true);
        q.add(kB, 0, 4000, true, true);
        const RecheckAnalysis r2 = computeUncertainRechecked(q.build(), 2);
        QCOMPARE(r2.coreLines.value(0), 3);
        QCOMPARE(r2.flagged(), 3);
    }

    void pure_overlapDetection()
    {
        const QVector<TimedLine> tl{
            {0, 4000, 0},           // 0: 1000 ms átfedés → zajos (≥ 1000 ms)
            {3000, 4000, 1},        // 1: teljesen átfedett → zajos
            {5000, 15000, 0},       // 2: 500 ms / 10 s → tiszta
            {9000, 9500, 1},        // 3: teljesen átfedett → zajos
            {20000, 50000, 0},      // 4: hosszú; az átfedés az embedding-ablakon KÍVÜL → tiszta
            {21000, 23000, 1},      // 5: teljesen átfedett → zajos
            {52000, 56000, 0},      // 6: ugyanaz a beszélő fedi át → tiszta
            {53000, 54500, 0},      // 7
            {60000, 61000, 0},      // 8: 400 ms / 1000 ms = 40 % → zajos (arány-szabály)
            {60600, 64000, 1},      // 9: 400 ms / 3400 ms ≈ 12 % → tiszta
        };
        const QVector<bool> noisy = computeOverlapNoisy(tl);
        QCOMPARE(noisy, (QVector<bool>{true, true, false, true, false, true, false, false, true, false}));
    }

    // ---- páronkénti átnézés (tiszta) ----------------------------------------

    void pure_pair_similarVoices_guardBlocksSuggestion_pairFlags()
    {
        PureLines p = similarPair();
        const QVector<AnalysisLine> al = p.build();
        // A „hasonló sorok" javaslat a hasonló hang miatt hallgat — és ezt most meg is mondja.
        const SuggestOutcome sg = suggestSimilarDetailed(al, /*source*/ 0, /*target*/ 1);
        QVERIFY(sg.lines.isEmpty());
        QVERIFY(sg.blockedBySimilarity);
        QVERIFY(sg.centroidSimilarity > kSuggestMaxCentroidSimilarity);
        QVERIFY(suggestSimilar(al, 0, 1).isEmpty());

        // A páros átnézés: nincs őr, a téves sorok mindkét irányban előkerülnek.
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
        // A zárolt sorokat nem ítéli meg (nincs érték), a sima sorokat igen.
        QVERIFY(std::isnan(r.lines[0].toA));
        QVERIFY(std::abs(r.lines[3].toA - 1.0) < 1e-3);
    }

    void pure_pair_marginsAndSkippedLines()
    {
        PureLines p = similarPair();
        p.add(pm, 0);                               // 14 m: 0.087-tel közelebb Bélához → kétes
        p.add(pm, 0, 2000);                         // 15 ugyanez rövid sorban: 0.10 kellene → nem
        p.add(pb, 0, 2000);                         // 16 rövid, 0.15 → kétes
        p.add(pb, 0, 1000);                         // 17 túl rövid → sosem
        p.add(pb, 0, 4000, false, /*noisy*/ true);  // 18 zajos → sosem
        p.add(pb, 0, 4000, /*locked*/ true, true);  // 19 zárolt (és zajos: a referenciába sem kerül)
        const PairRecheckAnalysis r = computePairRecheck(p.build(), 0, 1);
        QCOMPARE(r.refLinesA, 3);
        QVERIFY(r.lines[14].flagged);
        QVERIFY(!r.lines[15].flagged);
        QVERIFY(r.lines[16].flagged);
        QVERIFY(!r.lines[17].flagged);
        QVERIFY(!r.lines[18].flagged);
        QVERIFY(!r.lines[19].flagged);
        // A globális újraellenőrzés (0.10-es küszöb) az m-sort nem jelzi.
        QVERIFY(!computeUncertainRechecked(p.build(), 2).lines[14].uncertain);
    }

    void pure_pair_fallbackWhenFewLockedLines()
    {
        // Annának csak 2 megerősített sora van: a referenciája az összes tiszta sora (8).
        PureLines p = similarPair(/*lockedAnna*/ 2);
        const PairRecheckAnalysis r = computePairRecheck(p.build(), 0, 1);
        QVERIFY(r.valid);
        QVERIFY(r.fallbackA);
        QVERIFY(!r.fallbackB);
        QCOMPARE(r.refLinesA, 8);
        QCOMPARE(r.refLinesB, 3);
        // A szennyezett referenciával is előkerülnek (a sor önmaga nélkül mérve), de közelebbről.
        for (int i : {5, 6, 7}) QVERIFY(r.lines[i].flagged);
        QVERIFY(r.centroidSimilarity > 0.85);

        // Kevés sor (2 tiszta) → nem fut.
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
        // Ugyanaz a beszélő kétszer / érvénytelen index → semmi.
        QVERIFY(!computePairRecheck(p.build(), 1, 1).valid);
        QVERIFY(!computePairRecheck(p.build(), 0, -1).valid);
    }

    // ---- páronkénti átnézés (SpeakerEditor) --------------------------------

    void editor_pairRecheck_replacesOnlyPairMarks_persistsAndUndoes()
    {
        PairFixture fx;
        auto ed = fx.editor();
        QVERIFY(ed->confirmUtterances(pids({0, 2, 5, 1, 4, 8, 6, 9, 11})));

        // Előbb a globális újraellenőrzés: Annánál 3,7,15 (Béla), 13 (ismeretlen hang), Bélánál
        // 17 (Anna), Csabánál 12 (Anna). Az m-sor (10) a globális küszöb alatt marad.
        const SpeakerEditor::RecheckResult g = ed->recheckFromConfirmed();
        QCOMPARE(g.flagged, 6);
        QVERIFY(ed->utterance(pid(13)).rechecked);
        QVERIFY(!ed->utterance(pid(10)).rechecked);
        QCOMPARE(ed->utterance(pid(12)).likelySpeakerKey, kB1);

        QSignalSpy undoSpy(ed.get(), &SpeakerEditor::undoStateChanged);
        const SpeakerEditor::PairRecheckResult r = ed->recheckPair(kB1, kB2);
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
            const EditorUtterance u = ed->utterance(pid(row));
            QVERIFY2(u.rechecked && u.uncertain, qPrintable(QString::number(row)));
            QCOMPARE(u.likelySpeakerKey, kB2);
        }
        QCOMPARE(ed->utterance(pid(17)).likelySpeakerKey, kB1);
        // Annál az A/B-sornál, amelyet ez az átnézés nem talált kétesnek, a régi jelzés lecserélődik…
        QVERIFY(!ed->utterance(pid(13)).rechecked);
        QVERIFY(!fx.overlayUtterance(pid(13)).contains(QStringLiteral("rechecked")));
        // …a többi beszélőé (Csaba) érintetlen.
        QVERIFY(ed->utterance(pid(12)).rechecked);
        QCOMPARE(ed->utterance(pid(12)).likelySpeakerKey, kB1);

        // Perzisztencia.
        QCOMPARE(fx.overlayUtterance(pid(10)).value(QStringLiteral("recheckHint")).toString(), kB2);
        {
            auto again = fx.editor();
            QVERIFY(again->utterance(pid(10)).rechecked);
            QCOMPARE(again->utterance(pid(17)).likelySpeakerKey, kB1);
            QVERIFY(!again->utterance(pid(13)).rechecked);
            QVERIFY(again->utterance(pid(12)).rechecked);
        }

        // Egy visszavonási lépés: a globális állapot jön vissza; újra → a páros.
        ed->undo();
        QVERIFY(ed->utterance(pid(13)).rechecked);
        QVERIFY(!ed->utterance(pid(10)).rechecked);
        QCOMPARE(ed->undoText(), QStringLiteral("Beszélők újraellenőrzése"));
        ed->redo();
        QVERIFY(ed->utterance(pid(10)).rechecked);
        QVERIFY(!ed->utterance(pid(13)).rechecked);

        // Javítás / „Jó így" törli; változatlan újrafuttatás nem új lépés.
        QVERIFY(ed->confirmUtterances({pid(10)}));
        QVERIFY(!ed->utterance(pid(10)).rechecked);
        QVERIFY(ed->moveUtterances({pid(3)}, kB2));
        QVERIFY(!ed->utterance(pid(3)).rechecked);
        ed->recheckPair(kB1, kB2);
        const QString undo = ed->undoText();
        QCOMPARE(ed->recheckPair(kB1, kB2).flagged, 3);
        QCOMPARE(ed->undoText(), undo);
    }

    void editor_pairRecheck_blockers()
    {
        PairFixture fx;
        auto ed = fx.editor();
        QVERIFY(!ed->pairRecheckBlocker(kB1, kB1).isEmpty());
        QVERIFY(!ed->pairRecheckBlocker(kB1, QStringLiteral("nincs ilyen")).isEmpty());
        // Megerősítés nélkül is fut (az összes tiszta sorral — ezt a fallback jelzi).
        QVERIFY(ed->pairRecheckBlocker(kB1, kB2).isEmpty());
        const SpeakerEditor::PairRecheckResult r = ed->recheckPair(kB1, kB2);
        QVERIFY(r.ran);
        QVERIFY(r.fallbackA && r.fallbackB);
        QCOMPARE(r.refLinesA, 8);
        QCOMPARE(r.refLinesB, 5);

        // Kevés sor: Csaba 5 sorából 3 máshova kerül.
        QVERIFY(ed->moveUtterances(pids({6, 9, 11}), kB1));
        const QString why = ed->pairRecheckBlocker(kB3, kB2);
        QVERIFY2(why.startsWith(QStringLiteral("Csaba hangjához kevés a minta")), qPrintable(why));
        QVERIFY(!ed->recheckPair(kB3, kB2).ran);
    }

    void editor_pairOffer_onlyWhenGuardBlocksAndBothHaveConfirmedLines()
    {
        PairFixture fx;
        auto ed = fx.editor();
        // Megerősített sorok nélkül: a javaslat hallgat (őr), de ajánlat sincs.
        QVERIFY(ed->moveUtterances({pid(3)}, kB2));
        QVERIFY(!ed->hasPairOffer());
        ed->undo();

        QVERIFY(ed->confirmUtterances(pids({0, 2, 5, 1, 4, 8})));
        QSignalSpy spy(ed.get(), &SpeakerEditor::suggestionChanged);
        QVERIFY(ed->moveUtterances({pid(3)}, kB2));
        QVERIFY(!ed->hasSuggestion());
        QVERIFY(ed->hasPairOffer());
        QVERIFY(spy.count() > 0);
        const PairRecheckOffer o = ed->pairOffer();
        QCOMPARE(o.sourceSpeakerKey, kB1);
        QCOMPARE(o.targetSpeakerKey, kB2);
        QVERIFY(o.centroidSimilarity > kSuggestMaxCentroidSimilarity);

        // A következő szerkesztés eldobja; „Most nem" után ez a pár nem kerül elő újra.
        QVERIFY(ed->confirmUtterances({pid(13)}));
        QVERIFY(!ed->hasPairOffer());
        QVERIFY(ed->moveUtterances({pid(7)}, kB2));
        QVERIFY(ed->hasPairOffer());
        ed->declinePairOffer();
        QVERIFY(!ed->hasPairOffer());
        QVERIFY(ed->moveUtterances({pid(15)}, kB2));
        QVERIFY(!ed->hasPairOffer());
        // A másik irány ugyanaz a pár.
        QVERIFY(ed->moveUtterances({pid(17)}, kB1));
        QVERIFY(!ed->hasPairOffer());

        // Névtelen cél: nincs ajánlat.
        PairFixture fx2;
        fx2.meeting = fx2.store->load(fx2.meeting.id);
        fx2.meeting.speakerMap.remove(kB2);
        fx2.store->saveMeeting(fx2.meeting);
        auto ed2 = fx2.editor();
        QVERIFY(ed2->confirmUtterances(pids({0, 2, 5, 1, 4, 8})));
        QVERIFY(ed2->moveUtterances({pid(3)}, kB2));
        QVERIFY(!ed2->hasPairOffer());
    }

    // ---- SpeakerEditor -----------------------------------------------------

    void editor_overlapMarksCrossTalkAutomatically()
    {
        Fixture fx;
        auto ed = fx.editor();
        QStringList noisy;
        for (const EditorUtterance& u : ed->utterances())
            if (u.noisy) noisy << u.id;
        // A forgatókönyvben pontosan a közbeszólás és az alatta szóló sor.
        QCOMPARE(noisy, uids({11, 12}));
        QVERIFY(ed->utterance(uid(11)).noisyOverlap);
        // Az automatikus jelzés nem perzisztál.
        QVERIFY(!QFile::exists(overlayPath(fx.meeting.folder)));
    }

    void editor_recheckFlagsExactlyTheMislabeledLines()
    {
        Fixture fx;
        auto ed = fx.editor();
        QCOMPARE(ed->uncertainCount(), 0);              // a rendes ítélet nem látja őket
        QVERIFY(!ed->canRecheck());
        QVERIFY(ed->recheckBlocker().contains(QStringLiteral("Előbb erősíts meg vagy javíts legalább 3 sort egy beszélőnél")));
        QVERIFY(!ed->recheckFromConfirmed().ran);

        QVERIFY(ed->confirmUtterances(uids({0, 2, 5})));
        QVERIFY(ed->canRecheck());
        QVERIFY(ed->recheckBlocker().isEmpty());

        QSignalSpy finished(ed.get(), &SpeakerEditor::recheckFinished);
        QSignalSpy count(ed.get(), &SpeakerEditor::uncertainCountChanged);
        QSignalSpy changed(ed.get(), &SpeakerEditor::utterancesChanged);
        const SpeakerEditor::RecheckResult r = ed->recheckFromConfirmed();
        QVERIFY(r.ran);
        QCOMPARE(r.flagged, 3);
        QCOMPARE(r.speakersWithConfirmedCore, 1);
        QCOMPARE(r.confirmedLines, 3);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(count.last().at(0).toInt(), 3);
        QCOMPARE(changed.last().at(0).toStringList(), uids({3, 6, 9}));

        QCOMPARE(ed->uncertainUtteranceIds(), uids({3, 6, 9}));
        for (int row : {3, 6, 9}) {
            const EditorUtterance u = ed->utterance(uid(row));
            QVERIFY(u.uncertain);
            QVERIFY(u.rechecked);
            QCOMPARE(u.likelySpeakerKey, kB2);
        }
        QVERIFY(!ed->utterance(uid(8)).uncertain);       // Anna megerősítetlen sora rendben
        QVERIFY(!ed->utterance(uid(11)).uncertain);      // a zajos sort nem jelöljük
        QCOMPARE(ed->undoText(), QStringLiteral("Beszélők újraellenőrzése"));

        // Perzisztencia: újratöltés után is bizonytalanok, a javaslattal.
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
        ed->recheckFromConfirmed();
        QCOMPARE(ed->uncertainCount(), 3);

        // „Jó így" → nem bizonytalan; visszavonás → újra az.
        QVERIFY(ed->confirmUtterances({uid(3)}));
        QVERIFY(!ed->utterance(uid(3)).uncertain);
        QVERIFY(!fx.overlayUtterance(uid(3)).contains(QStringLiteral("rechecked")));
        QCOMPARE(ed->uncertainCount(), 2);
        ed->undo();
        QVERIFY(ed->utterance(uid(3)).uncertain);
        QCOMPARE(ed->uncertainCount(), 3);

        // Javítás (áthelyezés Bélához) → megszűnik; visszavonás → visszajön, a javaslattal.
        QVERIFY(ed->moveUtterances({uid(6)}, kB2));
        QVERIFY(!ed->utterance(uid(6)).uncertain);
        QCOMPARE(ed->uncertainCount(), 2);
        ed->undo();
        QVERIFY(ed->utterance(uid(6)).uncertain);
        QCOMPARE(ed->utterance(uid(6)).likelySpeakerKey, kB2);

        // A hang-elemzés újraszámolása (pl. másik sor javítása) nem törli a jelzést.
        QVERIFY(ed->moveUtterances({uid(8)}, kB2));
        QVERIFY(ed->utterance(uid(9)).uncertain);
        ed->undo();

        // Összevonás a javasolt beszélővel: a sor már ott van → a jelzés magától elenged.
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
        QCOMPARE(ed->recheckFromConfirmed().flagged, 3);

        // Az egyik kétes sort Annáénak erősítjük meg: a mag ezzel már a P-hangot is tartalmazza,
        // a másik kettő így nem kétes → az újabb újraellenőrzés elengedi őket.
        QVERIFY(ed->confirmUtterances({uid(3)}));
        const SpeakerEditor::RecheckResult r = ed->recheckFromConfirmed();
        QCOMPARE(r.flagged, 0);
        QCOMPARE(r.confirmedLines, 4);
        QCOMPARE(ed->uncertainCount(), 0);
        QVERIFY(!fx.overlayUtterance(uid(6)).contains(QStringLiteral("rechecked")));
        QVERIFY(fx.overlayUtterance(uid(9)).isEmpty());  // az alapértelmezett bejegyzés el is tűnik

        // Változatlan eredménynél nincs új visszavonási lépés.
        const QString undo = ed->undoText();
        ed->recheckFromConfirmed();
        QCOMPARE(ed->undoText(), undo);
    }

    void editor_blockers()
    {
        {
            Fixture fx(/*withCache*/ false);
            auto ed = fx.editor(/*withFactory*/ false);
            QVERIFY(!ed->canRecheck());
            QVERIFY(ed->recheckBlocker().contains(QStringLiteral("hangmodell")));
        }
        {
            Fixture fx(/*withCache*/ false);
            auto ed = fx.editor(/*withFactory*/ true);
            QVERIFY(!ed->canRecheck());
            QVERIFY(ed->recheckBlocker().contains(QStringLiteral("hang-elemzése még nem")));
        }
        {
            // A cache elég (a modell nélkül is): a megerősített sorok hiányoznak csak.
            Fixture fx;
            auto ed = fx.editor(/*withFactory*/ false);
            ed->confirmUtterances(uids({0, 2, 5}));
            QVERIFY(ed->canRecheck());
        }
    }

    void editor_manualNoisy_confirmAndClear()
    {
        Fixture fx;
        auto ed = fx.editor();
        // „Jó így, de ne használd mintának".
        QVERIFY(ed->confirmUtterances({uid(4)}, /*asNoisy*/ true));
        EditorUtterance u = ed->utterance(uid(4));
        QVERIFY(u.confirmed);
        QVERIFY(u.noisy);
        QVERIFY(!u.noisyOverlap);
        QCOMPARE(fx.overlayUtterance(uid(4)).value(QStringLiteral("noisy")).toBool(), true);
        QCOMPARE(ed->undoText(), QStringLiteral("1 sor megerősítése (nem hangminta)"));
        // Egy már megerősített sor is megkaphatja.
        QVERIFY(ed->confirmUtterances({uid(1)}));
        QVERIFY(ed->confirmUtterances({uid(1)}, true));
        QVERIFY(!ed->confirmUtterances({uid(1)}, true));    // már az

        // „Mintának használható": az automatikusan zajos sornál is felülír, és perzisztál.
        QVERIFY(ed->setUtterancesNoisy({uid(11)}, false));
        QVERIFY(!ed->utterance(uid(11)).noisy);
        QVERIFY(ed->utterance(uid(11)).noisyOverlap);
        QCOMPARE(fx.overlayUtterance(uid(11)).value(QStringLiteral("noisy")).toBool(true), false);
        ed.reset();
        auto again = fx.editor();
        QVERIFY(!again->utterance(uid(11)).noisy);
        QVERIFY(again->utterance(uid(4)).noisy);
        QVERIFY(again->utterance(uid(12)).noisy);
        again->undo();      // új munkamenet: nincs visszavonás-verem → semmi
        QVERIFY(!again->utterance(uid(11)).noisy);
    }

    // ---- tárolt lenyomatok a referenciában (SpeakerPrior) ----------------------

    void pure_prior_pullsFewConfirmedLinesTowardTheVoice_capped()
    {
        // Anna 3 rövid megerősített sora kissé „elcsúszott" hangú (u); a korábbi lenyomatai a
        // tiszta hangját (kA) hordozzák. A lenyomat Anna megerősítetlen kA-sorához húzza a
        // referenciát — de csak a plafonig (a helyi súly harmada).
        const QVector<float> u{0.8f, 0.6f, 0.0f};
        PureLines p;
        for (int i = 0; i < 3; ++i) p.add(u, 0, 1500, /*locked*/ true);   // 0–2 (4,5 mp)
        p.add(kA, 0);                                                      // 3
        for (int i = 0; i < 3; ++i) p.add(kB, 1, 4000, true);             // 4–6 Béla
        const QVector<AnalysisLine> al = p.build();

        const RecheckAnalysis plain = computeUncertainRechecked(al, 2);
        const RecheckAnalysis withPrior = computeUncertainRechecked(al, 2, priorFor(2, 0, {kA, kA}));
        QVERIFY(std::abs(plain.lines[3].own - 0.8) < 1e-3);
        QVERIFY(withPrior.lines[3].own > plain.lines[3].own + 0.05);
        // Kézi számolás: 4,5 mp · u + (4,5/3 = 1,5 mp) · kA.
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

        // Plafon alatt nincs vágás: 12 mp helyi súly mellett egy 4 mp-es lenyomat épp belefér.
        PureLines q = pollutedSpeaker(/*lockAnna*/ true);
        const RecheckAnalysis r = computeUncertainRechecked(q.build(), 2, priorFor(2, 0, {kA}));
        QVERIFY(!r.refs[0].priorCapped);
        QVERIFY(std::abs(r.refs[0].priorShare - 0.25) < 1e-9);
        QCOMPARE(r.flagged(), 3);
    }

    void pure_prior_capKeepsLocalDominance_localPrintNotCapped()
    {
        // Anna 3 megerősített kA-sora, Béla 3 megerősített kB-sora. Annánál egy megerősítetlen
        // x-sor, amely Bélához áll közelebb. Anna 5 korábbi lenyomata (más mikrofon) kB-szerű:
        // vágás nélkül átbillentené az ítéletet, vágva nem.
        const QVector<float> x{0.3f, 0.9539f, 0.0f};
        PureLines p;
        for (int i = 0; i < 3; ++i) p.add(kA, 0, 4000, true);    // 0–2 (12 mp)
        p.add(x, 0);                                               // 3
        for (int i = 0; i < 3; ++i) p.add(kB, 1, 4000, true);    // 4–6
        const QVector<AnalysisLine> al = p.build();
        const QVector<QVector<float>> five{kB, kB, kB, kB, kB};

        QVERIFY(computeUncertainRechecked(al, 2).lines[3].uncertain);
        const RecheckAnalysis capped = computeUncertainRechecked(al, 2, priorFor(2, 0, five));
        QVERIFY(capped.refs[0].priorCapped);
        QVERIFY(capped.lines[3].uncertain);
        QCOMPARE(capped.lines[3].otherSpeaker, 1);
        // Vágás nélkül (5 × 4 mp = 20 mp > 12 mp) Anna referenciája x-hez jobban illene, mint
        // Béláé — a sor nem lenne kétes.
        const double uncappedOwn = cosToWeighted(x, {{12.0, kA}, {20.0, kB}});
        QVERIFY(uncappedOwn > capped.lines[3].other);
        // Vágva: 12 mp · kA + 4 mp · kB.
        QVERIFY(std::abs(capped.lines[3].own - cosToWeighted(x, {{12.0, kA}, {4.0, kB}})) < 1e-6);

        // Ugyanezek ITTENI lenyomatként helyi bizonyítékok: 10 mp-es súly, nincs vágás → a sor
        // Annáé (nem kétes).
        const RecheckAnalysis local = computeUncertainRechecked(al, 2, priorFor(2, 0, {}, five));
        QCOMPARE(local.refs[0].localPrints, 5);
        QVERIFY(!local.refs[0].priorCapped);
        QVERIFY(!local.lines[3].uncertain);
        QVERIFY(std::abs(local.lines[3].own - cosToWeighted(x, {{12.0, kA}, {50.0, kB}})) < 1e-6);
    }

    void pure_prior_formsReferenceWithoutLockedLines()
    {
        // Megerősített sor nincs: eddig a szennyezett rendes centroid eltakarta a téves sorokat.
        PureLines p = pollutedSpeaker(/*lockAnna*/ false);
        const QVector<AnalysisLine> al = p.build();
        QCOMPARE(computeUncertainRechecked(al, 2).flagged(), 0);
        QCOMPARE(computeUncertainRechecked(al, 2).refs[0].kind, ReferenceKind::Fallback);

        // Annának van korábbi lenyomata → az adja a referenciáját (nincs helyi mag).
        const QVector<SpeakerPrior> pr = priorFor(2, 0, {kA});
        const RecheckAnalysis r = computeUncertainRechecked(al, 2, pr);
        QVERIFY(!hasTrustedCore(al, 2, pr));            // korábbi lenyomat egymaga nem helyi mag
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

        // ITTENI lenyomat egymaga is helyi mag.
        const QVector<SpeakerPrior> lp = priorFor(2, 0, {}, {kA});
        QVERIFY(hasTrustedCore(al, 2, lp));
        const RecheckAnalysis l = computeUncertainRechecked(al, 2, lp);
        QCOMPARE(l.trustedCore, (QVector<bool>{true, false}));
        QCOMPARE(l.refs[0].kind, ReferenceKind::Local);
        QCOMPARE(l.refs[0].lines, 0);
        QCOMPARE(l.refs[0].localPrints, 1);
        QCOMPARE(l.flagged(), 3);
    }

    void pure_prior_localPrintReplacesItsSampleLine()
    {
        // Az itteni lenyomat mintasora (0) maga is megerősített: csak egyszer számít (a lenyomat).
        PureLines p = pollutedSpeaker(/*lockAnna*/ true);
        const QVector<AnalysisLine> al = p.build();
        const RecheckAnalysis r = computeUncertainRechecked(al, 2, priorFor(2, 0, {}, {kA}, {0}));
        QCOMPARE(r.refs[0].lines, 2);
        QCOMPARE(r.refs[0].localPrints, 1);
        QCOMPARE(r.coreLines[0], 2);
        QCOMPARE(r.flagged(), 3);
        // Páros átnézésnél ugyanígy.
        const PairRecheckAnalysis pr = computePairRecheck(al, 0, 1, priorFor(2, 0, {}, {kA}, {0}));
        QCOMPARE(pr.refA.lines, 2);
        QCOMPARE(pr.refA.localPrints, 1);
        QVERIFY(pr.refA.refLocal());
        QVERIFY(pr.valid);
    }

    void pure_pair_priorReplacesFallback()
    {
        // Bélának nincs megerősített sora: eddig fallback (az összes sora); korábbi lenyomattal
        // az adja a referenciáját.
        PureLines p = similarPair(/*lockedAnna*/ 3, /*lockedBela*/ 0);
        const QVector<AnalysisLine> al = p.build();
        QVERIFY(computePairRecheck(al, 0, 1).fallbackB);
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

        // Itteni lenyomat helyi magként: kevés sor mellett is érvényes referencia.
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
        // Anna itteni lenyomata a 0. sorból; Béla korábbi lenyomata egy másik megbeszélésből;
        // egy más dimenziójú (más modell) lenyomat, amelyet ki kell hagyni.
        fx.prints->addPrint(QStringLiteral("Anna"),
                            makePrint(kA, fx.meeting.id, QStringLiteral("audio.ogg#0-4000")));
        fx.prints->addPrint(QStringLiteral("Béla"), makePrint(kB, QStringLiteral("masik-megbeszeles")));
        fx.prints->addPrint(QStringLiteral("Béla"),
                            makePrint({0.0f, 1.0f, 0.0f, 0.0f}, QStringLiteral("regi-modell")));
        auto ed = fx.editor();
        // Az itteni lenyomat egymaga helyi mag → az újraellenőrzés megerősítés nélkül is fut.
        QVERIFY(ed->canRecheck());

        QVERIFY(ed->confirmUtterances(uids({0, 2, 5})));
        const SpeakerEditor::RecheckResult r = ed->recheckFromConfirmed();
        QVERIFY(r.ran);
        QCOMPARE(r.flagged, 3);
        QCOMPARE(ed->uncertainUtteranceIds(), uids({3, 6, 9}));
        QCOMPARE(ed->utterance(uid(3)).likelySpeakerKey, kB2);
        QCOMPARE(r.references.size(), 2);
        const SpeakerEditor::SpeakerReference& anna = r.references[0];
        QCOMPARE(anna.speakerKey, kB1);
        QCOMPARE(anna.lines, 2);                        // a 0. sor a lenyomatban számít
        QCOMPARE(anna.localPrints, 1);
        QCOMPARE(anna.priorPrints, 0);
        const SpeakerEditor::SpeakerReference& bela = r.references[1];
        QVERIFY(bela.priorOnly);
        QCOMPARE(bela.priorPrints, 1);
        QCOMPARE(r.confirmedLines, 2);
        QCOMPARE(r.referenceSummary(),
                 QStringLiteral("Referencia: Anna 2 sor + 1 itteni lenyomat, Béla 1 korábbi lenyomat."));
    }

    void editor_recheckWithoutPrints_noSummary()
    {
        Fixture fx;
        auto ed = fx.editor();
        QVERIFY(ed->confirmUtterances(uids({0, 2, 5})));
        const SpeakerEditor::RecheckResult r = ed->recheckFromConfirmed();
        QCOMPARE(r.references.size(), 1);
        QCOMPARE(r.references[0].lines, 3);
        QVERIFY(r.referenceSummary().isEmpty());
    }

    void editor_pairRecheckUsesStoredVoiceprints()
    {
        PairFixture fx;
        fx.prints->addPrint(QStringLiteral("Béla"), makePrint(pb, QStringLiteral("masik-megbeszeles")));
        auto ed = fx.editor();
        QVERIFY(ed->confirmUtterances(pids({0, 2, 5, 1})));     // Bélának csak 1 megerősített sora
        const SpeakerEditor::PairRecheckResult r = ed->recheckPair(kB1, kB2);
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
        // Béla 6 hosszú sora közül a 11. zajos (átfedés) → 5 használható.
        const VoiceprintMaterial before = ed->voiceprintMaterial(kB2);
        QCOMPARE(before.usableLines, 5);
        QVERIFY(ed->setUtterancesNoisy({uid(13)}, true));
        QCOMPARE(ed->voiceprintMaterial(kB2).usableLines, 4);
        QVERIFY(ed->setUtterancesNoisy({uid(11)}, false));
        QCOMPARE(ed->voiceprintMaterial(kB2).usableLines, 5);
    }
};

QTEST_GUILESS_MAIN(SpeakerRecheckTest)
#include "test_speaker_recheck.moc"

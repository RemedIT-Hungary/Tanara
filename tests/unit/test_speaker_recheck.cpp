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
            if (scenario()[i].voice != '-') c.vectors.insert(lines[i].id, voiceVec(scenario()[i].voice));
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

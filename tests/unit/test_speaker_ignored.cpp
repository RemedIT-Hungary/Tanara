//
// „Nem azonosítható" sor-állapot (OverlayUtterance::ignored) + a minta-tanítás átláthatósága
// (Átnézendő-újratervezés, S1 közös alap). KITALÁLT adaton:
//  - a hang-elemzés MINDEN ága kihagyja az ignored sort (SpeakerAnalysis, SideAnalysis,
//    ReviewGroups / jelölt-profilok, hanglenyomat-gyűjtés), és az sosem bizonytalan;
//  - SpeakerEditor: setUtterancesIgnored egy undo-lépés, perzisztál; nem-minta áthelyezés egy
//    lépésben; eltérő-minta jelzés; minta-mérő.
// A „hangok" kis dimenziós egységvektorok; nincs hang, nincs ffmpeg, a sáv-aktivitás szintetikus.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/Types.h"
#include "tanara/edit/ReviewGroups.h"
#include "tanara/edit/SideAnalysis.h"
#include "tanara/edit/SpeakerAnalysis.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/TrackActivity.h"
#include "tanara/edit/UtteranceEmbeddings.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/VoiceprintStore.h"

#include <cmath>
#include <memory>

using namespace tanara;
using namespace tanara::speakeredit;

namespace {

constexpr int kDim = 6;

QVector<float> voice(int k)
{
    QVector<float> v(kDim, 0.0f);
    v[k] = 1.0f;
    return v;
}

// ---- tiszta elemzés --------------------------------------------------------------
struct L { int speaker; int axis; bool locked = false; bool noisy = false; bool ignored = false; };

struct Lines {
    QVector<QVector<float>> vecs;   // a mutatók élettartamához
    QVector<AnalysisLine> al;
};

Lines make(const QVector<L>& spec)
{
    Lines out;
    out.vecs.reserve(spec.size());
    for (const L& s : spec) out.vecs.append(voice(s.axis));
    for (int i = 0; i < spec.size(); ++i) {
        AnalysisLine a;
        a.embedding = &out.vecs[i];
        a.speaker = spec[i].speaker;
        a.durationMs = 4000;
        a.locked = spec[i].locked;
        a.noisy = spec[i].noisy;
        a.ignored = spec[i].ignored;
        out.al.append(a);
    }
    return out;
}

// ---- sáv-aktivitás ----------------------------------------------------------------
TrackActivity activity(const QString& id, TrackKind kind, qint64 totalMs,
                       const QVector<QPair<qint64, qint64>>& spans)
{
    TrackActivity t;
    t.trackId = id;
    t.kind = kind;
    t.frameMs = 50;
    t.dbAboveFloor = QVector<float>(int(totalMs / 50), 0.0f);
    for (const auto& sp : spans)
        for (qint64 ms = sp.first; ms < sp.second; ms += 50)
            if (ms / 50 < t.dbAboveFloor.size()) t.dbAboveFloor[int(ms / 50)] = 30.0f;
    return t;
}

// Sorok 4 mp-esek 5 mp-enként; side: 'L' mic, 'R' loopback, '-' rövid (1 mp, hang nélkül).
struct Spec { QString raw; char side; int axis; bool confirmed = false; bool ignored = false; };

struct Scenario {
    ReviewInput in;
    MeetingActivity act;
};

Scenario scenario(const QVector<Spec>& specs, bool withSides)
{
    Scenario sc;
    Track tm; tm.id = "mic"; tm.kind = TrackKind::Mic;
    Track tl; tl.id = "loop"; tl.kind = TrackKind::Loopback;
    sc.in.meeting.tracks = {tm, tl};
    QVector<QPair<qint64, qint64>> mic, loop;
    for (int i = 0; i < specs.size(); ++i) {
        const Spec& s = specs[i];
        const qint64 st = i * 5000, en = st + (s.side == '-' ? 1000 : 4000);
        TranscriptLine l{QStringLiteral("u%1").arg(st), st, en, s.raw, QStringLiteral("x")};
        sc.in.lines << l;
        (s.side == 'R' ? loop : mic) << qMakePair(st, en);
        if (s.side != '-' && s.axis >= 0) sc.in.embeddings.insert(l.id, voice(s.axis));
        if (s.confirmed) sc.in.overlay.utterances[l.id].confirmed = true;
        if (s.ignored) sc.in.overlay.utterances[l.id].ignored = true;
    }
    if (withSides) {
        const qint64 total = specs.size() * 5000 + 1000;
        sc.act.tracks = {activity("mic", TrackKind::Mic, total, mic),
                         activity("loop", TrackKind::Loopback, total, loop)};
    }
    return sc;
}

const ReviewGroup* group(const QVector<ReviewGroup>& gs, ReviewKind k)
{
    for (const ReviewGroup& g : gs)
        if (g.kind == k) return &g;
    return nullptr;
}

bool inAnyGroup(const QVector<ReviewGroup>& gs, const QString& id)
{
    for (const ReviewGroup& g : gs)
        if (g.utteranceIds.contains(id)) return true;
    return false;
}

// ---- szerkesztő-fixture (mint a test_speaker_review-ban) -----------------------------
struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<MeetingStore> store;
    std::unique_ptr<PeopleStore> people;
    std::unique_ptr<VoiceprintStore> prints;
    Meeting meeting;
    QVector<Spec> specs;
    MeetingActivity act;

    Fixture(const QVector<Spec>& s, const QMap<QString, QString>& names) : specs(s)
    {
        store = std::make_unique<MeetingStore>(dir.filePath("rec"), dir.filePath("meta"));
        people = std::make_unique<PeopleStore>(dir.filePath("meta/people.json"));
        prints = std::make_unique<VoiceprintStore>(dir.filePath("meta/vp.json"));
        meeting = store->createMeeting("Nem azonosítható");
        Track mic; mic.id = "mic"; mic.kind = TrackKind::Mic; mic.file = "track_mic.ogg";
        Track loop; loop.id = "loop"; loop.kind = TrackKind::Loopback; loop.file = "track_loop.ogg";
        meeting.tracks = {mic, loop};
        QJsonArray segs;
        QVector<QPair<qint64, qint64>> micSpans, loopSpans;
        for (int i = 0; i < specs.size(); ++i) {
            const qint64 st = i * 5000, en = st + 4000;
            segs.append(QJsonObject{{"startMs", double(st)}, {"endMs", double(en)}, {"speaker", specs[i].raw},
                                    {"text", QStringLiteral("Sor %1").arg(i)}});
            (specs[i].side == 'L' ? micSpans : loopSpans) << qMakePair(st, en);
        }
        QFile f(segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        meeting.speakerMap = names;
        store->saveMeeting(meeting);

        const QVector<TranscriptLine> lines = loadTranscriptLines(meeting.folder);
        UtteranceEmbeddingCache c;
        c.fingerprint = transcriptFingerprint(lines);
        SpeakerOverlay ov;
        for (int i = 0; i < specs.size(); ++i) {
            c.set(QStringLiteral("campplus"), lines[i].id, voice(specs[i].axis));
            if (specs[i].confirmed) ov.utterances[lines[i].id].confirmed = true;
        }
        c.save(meeting.folder);
        if (!ov.utterances.isEmpty()) {
            ov.transcriptFingerprint = c.fingerprint;
            saveOverlay(meeting.folder, ov);
        }
        const qint64 total = specs.size() * 5000 + 1000;
        act.tracks = {activity("mic", TrackKind::Mic, total, micSpans),
                      activity("loop", TrackKind::Loopback, total, loopSpans)};
    }

    QString uid(int i) const { return QStringLiteral("u%1").arg(i * 5000); }

    std::unique_ptr<SpeakerEditor> editor()
    {
        auto ed = std::make_unique<SpeakerEditor>(store.get(), people.get(), prints.get(), meeting.id);
        ed->setUserSpeakerName("Ádám");
        const MeetingActivity a = act;
        ed->setActivityProvider([a](const Meeting&) { return a; });
        return ed;
    }
};

bool waitReview(SpeakerEditor& ed)
{
    QSignalSpy spy(&ed, &SpeakerEditor::reviewGroupsChanged);
    return spy.wait(5000);
}

// Ádám (B1, saját név → mikrofon) sorai közé 3 loopbackos, Béla-hangú sor került (3, 5, 8); Béla
// (B2) megerősített sorai (1, 4, 7) a loopbackon.
QVector<Spec> sideScenario()
{
    return {{"B1", 'L', 0}, {"B2", 'R', 1, true}, {"B1", 'L', 0}, {"B1", 'R', 1},
            {"B2", 'R', 1, true}, {"B1", 'R', 1}, {"B1", 'L', 0}, {"B2", 'R', 1, true},
            {"B1", 'R', 1}, {"B1", 'L', 0}};
}

const SpeakerSampleStats* statsOf(const QVector<SpeakerSampleStats>& all, const QString& key)
{
    for (const SpeakerSampleStats& s : all)
        if (s.speakerKey == key) return &s;
    return nullptr;
}

} // namespace

class SpeakerIgnoredTest : public QObject {
    Q_OBJECT
private slots:
    // ---- overlay ---------------------------------------------------------------
    void overlayRoundTrip()
    {
        QTemporaryDir dir;
        SpeakerOverlay ov;
        ov.transcriptFingerprint = "fp";
        ov.utterances["u0"].ignored = true;
        OverlayUtterance u;
        QVERIFY(u.isDefault());
        u.ignored = true;
        QVERIFY(!u.isDefault());
        QVERIFY(!(u == OverlayUtterance()));
        QVERIFY(saveOverlay(dir.path(), ov));
        const SpeakerOverlay back = loadOverlay(dir.path());
        QVERIFY(back.utterances.contains("u0"));
        QVERIFY(back.utterances.value("u0").ignored);
        QVERIFY(back.utterances.value("u0").speaker.isEmpty());
    }

    // ---- SpeakerAnalysis: minden ág ------------------------------------------------
    void analysisCentroidAndUncertain()
    {
        // 0. beszélő: 4 sor a 0. hangon + 1 sor az 1. hangon; 1. beszélő: 3 sor az 1. hangon.
        QVector<L> spec{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 1}, {1, 1}};
        Lines plain = make(spec);
        QVERIFY(computeUncertain(plain.al, 2)[4]);                  // kontroll: a kakukktojás kétes
        QVERIFY(computeFits(plain.al, 2)[0].own < 0.99);            // és húzza a centroidot

        spec[4].ignored = true;
        Lines ign = make(spec);
        QVERIFY(!ign.al[4].hasEmbedding());
        const QVector<bool> unc = computeUncertain(ign.al, 2);
        QVERIFY(!unc[4]);                                           // sosem bizonytalan
        const QVector<LineFit> fits = computeFits(ign.al, 2);
        QVERIFY(std::isnan(fits[4].own) && std::isnan(fits[4].other));
        QCOMPARE(fits[0].own, 1.0);                                 // a centroid nélküle épül
    }

    void analysisLockedAllSkipsIgnored()
    {
        // A kModeLockedAll a zárolt ZAJOS sorokat is használja — az ignored onnan is kimarad.
        QVector<L> spec{{0, 0, true, true}, {0, 0, true, true}, {0, 0, true, true}, {0, 0}, {1, 1}};
        QVERIFY(hasTrustedCore(make(spec).al, 2));
        RecheckAnalysis a = computeUncertainRechecked(make(spec).al, 2);
        QVERIFY(a.trustedCore[0]);
        QCOMPARE(a.coreLines[0], 3);

        for (int i = 0; i < 3; ++i) spec[i].ignored = true;
        Lines ign = make(spec);
        QVERIFY(!hasTrustedCore(ign.al, 2));
        a = computeUncertainRechecked(ign.al, 2);
        QVERIFY(!a.trustedCore[0]);
        QCOMPARE(a.coreLines[0], 0);
        for (int i = 0; i < 3; ++i) QVERIFY(!a.lines[i].uncertain);
    }

    void analysisRecheckNeverFlagsIgnored()
    {
        // Megerősített magok: 0 → 0. hang, 1 → 1. hang. A 6. sor az 1. hangon szól a 0-nál.
        QVector<L> spec{{0, 0, true}, {0, 0, true}, {0, 0, true}, {1, 1, true}, {1, 1, true}, {1, 1, true}, {0, 1}};
        QVERIFY(computeUncertainRechecked(make(spec).al, 2).lines[6].uncertain);
        spec[6].ignored = true;
        QVERIFY(!computeUncertainRechecked(make(spec).al, 2).lines[6].uncertain);
    }

    void analysisSuggestAndPairSkipIgnored()
    {
        // Forrás (0): 3× 0. hang + 2× 1. hang; cél (1): 2× 1. hang.
        QVector<L> spec{{0, 0}, {0, 0}, {0, 0}, {0, 1}, {0, 1}, {1, 1}, {1, 1}};
        QCOMPARE(suggestSimilar(make(spec).al, 0, 1), (QVector<int>{3, 4}));
        spec[4].ignored = true;
        QCOMPARE(suggestSimilar(make(spec).al, 0, 1), (QVector<int>{3}));

        // Páros átnézés: a 6. sor (0-nál, 1. hangon) kétes; ignored → nem.
        QVector<L> pair{{0, 0, true}, {0, 0, true}, {0, 0, true}, {1, 1, true}, {1, 1, true}, {1, 1, true}, {0, 1}};
        QVERIFY(computePairRecheck(make(pair).al, 0, 1).lines[6].flagged);
        pair[6].ignored = true;
        const PairRecheckAnalysis p = computePairRecheck(make(pair).al, 0, 1);
        QVERIFY(p.valid);
        QVERIFY(!p.lines[6].flagged);
    }

    // ---- SideAnalysis ----------------------------------------------------------------
    void sidesLearningAndConflictsSkipIgnored()
    {
        // B1 megerősített sorai: 2 mikrofonon, 3 loopbackon (vegyes → nincs egyértelmű oldal).
        QVector<Spec> spec{{"B1", 'L', 0, true}, {"B1", 'L', 0, true}, {"B1", 'R', 0, true},
                           {"B1", 'R', 0, true}, {"B1", 'R', 0, true}, {"B1", 'R', 0}};
        Scenario sc = scenario(spec, true);
        SideReport r = analyzeSides(sc.in.meeting, sc.in.lines, sc.in.overlay, sc.act, QString());
        QVERIFY(r.active);
        QCOMPARE(r.persons.size(), 1);
        QCOMPARE(r.persons[0].remoteLines, 3);
        QVERIFY(r.persons[0].side != Side::Local);

        // A 3 loopbackos megerősített sor „nem azonosítható" → a személy mikrofonos lesz; a nem
        // ignored loopbackos sor ellentmondás, az ignored-ak nem.
        for (int i = 2; i <= 4; ++i) spec[i].ignored = true;
        sc = scenario(spec, true);
        r = analyzeSides(sc.in.meeting, sc.in.lines, sc.in.overlay, sc.act, QString());
        QCOMPARE(r.persons[0].localLines, 2);
        QCOMPARE(r.persons[0].remoteLines, 0);
        QCOMPARE(r.persons[0].allLines.total(), 3);
        QCOMPARE(r.persons[0].side, Side::Local);
        for (int i = 2; i <= 4; ++i) QVERIFY(r.lines[i].ignored);
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.conflicts[0].utteranceId, QStringLiteral("u25000"));
    }

    // ---- ReviewGroups + jelölt-profilok ------------------------------------------------
    void reviewGroupsAndProfilesSkipIgnored()
    {
        // A: 3 megerősített (0. hang), B: 3 megerősített (1. hang); A-nál 2 sor B hangján + 1 rövid.
        QVector<Spec> spec{{"A", 'L', 0, true}, {"A", 'L', 0, true}, {"A", 'L', 0, true},
                           {"B", 'L', 1, true}, {"B", 'L', 1, true}, {"B", 'L', 1, true},
                           {"A", 'L', 1}, {"A", 'L', 1}, {"A", '-', -1}};
        Scenario sc = scenario(spec, false);
        ReviewResult r = analyzeReview(sc.in, sc.act);
        const ReviewGroup* core = group(r.groups, ReviewKind::CoreMismatch);
        QVERIFY(core);
        QCOMPARE(core->utteranceIds.size(), 2);
        QVERIFY(group(r.groups, ReviewKind::ShortLines));
        QCOMPARE(r.context.profile("A")->lines, 6);

        spec[6].ignored = true;
        spec[8].ignored = true;
        sc = scenario(spec, false);
        r = analyzeReview(sc.in, sc.act);
        core = group(r.groups, ReviewKind::CoreMismatch);
        QVERIFY(core);
        QCOMPARE(core->utteranceIds, QStringList{"u35000"});
        QVERIFY(!inAnyGroup(r.groups, "u30000"));
        QVERIFY(!group(r.groups, ReviewKind::ShortLines));          // az ignored rövid sor sem
        QCOMPARE(r.context.profile("A")->lines, 4);                 // a profil sorszámában sincs
        QCOMPARE(r.context.profile("A")->sumCount, 4);

        // Megerősített mag: az ignored zárolt sor sem számít bele (2 < kMinSpeakerLines → nincs mag).
        spec[0].ignored = true;
        sc = scenario(spec, false);
        r = analyzeReview(sc.in, sc.act);
        QCOMPARE(r.context.profile("A")->confirmedLines, 0);
        QVERIFY(r.context.profile("A")->core.isEmpty());
        QVERIFY(!group(r.groups, ReviewKind::CoreMismatch));
    }

    void reviewSideConflictsSkipIgnored()
    {
        Scenario sc = scenario(sideScenario(), true);
        sc.in.userName = "Ádám";
        sc.in.meeting.speakerMap = {{"B1", "Ádám"}, {"B2", "Béla"}};
        ReviewResult r = analyzeReview(sc.in, sc.act);
        QVERIFY(r.sideConflicts.contains("u15000"));
        sc.in.overlay.utterances["u15000"].ignored = true;
        r = analyzeReview(sc.in, sc.act);
        QVERIFY(!r.sideConflicts.contains("u15000"));
        QVERIFY(!inAnyGroup(r.groups, "u15000"));
        QVERIFY(r.sideConflicts.contains("u25000"));
    }

    // ---- SpeakerEditor ---------------------------------------------------------------
    void editorIgnoreUndoRedoPersist()
    {
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QVERIFY(waitReview(*ed));
        QVERIFY(ed->utterance(fx.uid(3)).uncertain);
        const QVector<EditorSpeaker> before = ed->speakers();

        QSignalSpy changed(ed.get(), &SpeakerEditor::utterancesChanged);
        QVERIFY(ed->setUtterancesIgnored({fx.uid(3)}, true));
        QVERIFY(!ed->setUtterancesIgnored({fx.uid(3)}, true));     // már az
        EditorUtterance u = ed->utterance(fx.uid(3));
        QVERIFY(u.ignored);
        QVERIFY(!u.uncertain);
        QVERIFY(u.uncertainReason.isEmpty());
        QCOMPARE(u.speakerKey, QStringLiteral("B1"));               // a név marad
        QVERIFY(!changed.isEmpty());
        QVERIFY(ed->undoText().contains(QStringLiteral("nem azonosítható")));
        // A beszédidő a nevén marad.
        const QVector<EditorSpeaker> after = ed->speakers();
        QCOMPARE(after.size(), before.size());
        for (int i = 0; i < after.size(); ++i) {
            QCOMPARE(after[i].utteranceCount, before[i].utteranceCount);
            QCOMPARE(after[i].talkShare, before[i].talkShare);
        }
        // Az összefoglaló szempontjából nem beszélő-változás.
        QVERIFY(ed->summaryStale().correctedSpeakers == 0);

        // Perzisztál.
        QVERIFY(loadOverlay(fx.meeting.folder).utterances.value(fx.uid(3)).ignored);
        {
            auto other = fx.editor();
            QVERIFY(other->utterance(fx.uid(3)).ignored);
        }

        const int undoDepth = ed->undoSerials().size();
        ed->undo();
        QVERIFY(!ed->utterance(fx.uid(3)).ignored);
        QVERIFY(!loadOverlay(fx.meeting.folder).utterances.value(fx.uid(3)).ignored);
        ed->redo();
        QVERIFY(ed->utterance(fx.uid(3)).ignored);
        QCOMPARE(ed->undoSerials().size(), undoDepth);

        // Visszavonás művelettel is (ignored=false), egy lépés.
        QVERIFY(ed->setUtterancesIgnored({fx.uid(3)}, false));
        QVERIFY(!ed->utterance(fx.uid(3)).ignored);
        QCOMPARE(ed->undoSerials().size(), undoDepth + 1);
        QVERIFY(!loadOverlay(fx.meeting.folder).utterances.contains(fx.uid(3)));   // alapállapot → nincs bejegyzés
    }

    void editorReviewGroupsSkipIgnored()
    {
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QVERIFY(waitReview(*ed));
        const ReviewGroup* side = group(ed->reviewGroups(), ReviewKind::SideConflict);
        QVERIFY(side);
        QCOMPARE(side->utteranceIds, (QStringList{fx.uid(3), fx.uid(5), fx.uid(8)}));

        QVERIFY(ed->setUtterancesIgnored({fx.uid(3), fx.uid(5)}, true));
        QCOMPARE(ed->undoSerials().size(), 1);
        QVERIFY(waitReview(*ed));
        side = group(ed->reviewGroups(), ReviewKind::SideConflict);
        QVERIFY(side);
        QCOMPARE(side->utteranceIds, QStringList{fx.uid(8)});
        QVERIFY(!ed->uncertainUtteranceIds().contains(fx.uid(3)));
        QVERIFY(ed->uncertainUtteranceIds().contains(fx.uid(8)));

        QVERIFY(ed->setUtterancesIgnored({fx.uid(8)}, true));
        QVERIFY(waitReview(*ed));
        QVERIFY(!group(ed->reviewGroups(), ReviewKind::SideConflict));
        for (const QString& id : {fx.uid(3), fx.uid(5), fx.uid(8)})
            QVERIFY(!inAnyGroup(ed->reviewGroups(), id));

        // A jelölt-rangsorban az ignored sor hangja nincs a mostani beszélő összegében, de a sorra
        // továbbra is kérhetők jelöltek (a felhasználó később eldöntheti).
        QVERIFY(!ed->lineCandidates(fx.uid(3)).isEmpty());
    }

    void editorMoveClearsIgnoredAndConfirmToo()
    {
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QVERIFY(ed->setUtterancesIgnored({fx.uid(3), fx.uid(5)}, true));
        QVERIFY(ed->moveUtterances({fx.uid(3)}, "B2"));
        QVERIFY(!ed->utterance(fx.uid(3)).ignored);
        QVERIFY(ed->confirmUtterances({fx.uid(5)}));
        QVERIFY(!ed->utterance(fx.uid(5)).ignored);
        QVERIFY(ed->utterance(fx.uid(5)).confirmed);
    }

    void editorNonSampleMoveIsOneStep()
    {
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QSignalSpy undoSpy(ed.get(), &SpeakerEditor::undoStateChanged);

        QVERIFY(ed->moveUtterances({fx.uid(3)}, "B2", /*noisy*/ true));
        QCOMPARE(ed->undoSerials().size(), 1);
        EditorUtterance u = ed->utterance(fx.uid(3));
        QCOMPARE(u.speakerKey, QStringLiteral("B2"));
        QVERIFY(u.manuallyCorrected);
        QVERIFY(u.noisy);
        QVERIFY(ed->undoText().contains(QStringLiteral("nem hangminta")));
        QVERIFY(!ed->hasSuggestion());
        QVERIFY(!ed->lastSampleDeviation().isValid());
        ed->undo();
        u = ed->utterance(fx.uid(3));
        QCOMPARE(u.speakerKey, QStringLiteral("B1"));
        QVERIFY(!u.manuallyCorrected);
        QVERIFY(!u.noisy);

        // Személyhez (új résztvevő) és új névtelenhez is egy lépésben.
        const QString key = ed->moveUtterancesToPerson({fx.uid(5)}, "Cecil", true);
        QVERIFY(!key.isEmpty());
        QCOMPARE(ed->undoSerials().size(), 1);
        QVERIFY(ed->utterance(fx.uid(5)).noisy && ed->utterance(fx.uid(5)).manuallyCorrected);
        const QString anon = ed->moveUtterancesToNewParticipant({fx.uid(8)}, true);
        QVERIFY(!anon.isEmpty());
        QCOMPARE(ed->undoSerials().size(), 2);
        QVERIFY(ed->utterance(fx.uid(8)).noisy && ed->utterance(fx.uid(8)).manuallyCorrected);

        // Az alapértelmezés (noisy=false) nem nyúl a zajos-jelzéshez.
        QVERIFY(ed->moveUtterances({fx.uid(0)}, "B2"));
        QVERIFY(!ed->utterance(fx.uid(0)).noisy);
    }

    void editorSampleDeviation()
    {
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QSignalSpy spy(ed.get(), &SpeakerEditor::sampleDeviationChanged);
        QVERIFY(!ed->lastSampleDeviation().isValid());

        // Béla hangú sor Bélához: illik.
        QVERIFY(ed->moveUtterances({fx.uid(3)}, "B2"));
        SampleDeviation dv = ed->lastSampleDeviation();
        QVERIFY(dv.isValid());
        QCOMPARE(dv.utteranceId, fx.uid(3));
        QCOMPARE(dv.speakerKey, QStringLiteral("B2"));
        QVERIFY(dv.similarity > 0.99);
        QVERIFY(!dv.deviates);
        QCOMPARE(spy.count(), 1);

        // Ádám hangú sor Bélához (mintaként): eltér.
        QVERIFY(ed->moveUtterances({fx.uid(0)}, "B2"));
        dv = ed->lastSampleDeviation();
        QVERIFY(dv.isValid());
        QCOMPARE(dv.utteranceId, fx.uid(0));
        QVERIFY(dv.similarity < kSampleDeviationFit);
        QVERIFY(dv.deviates);

        // Több sor egy döntésben: a legrosszabb jön vissza.
        ed->undo();
        QVERIFY(!ed->lastSampleDeviation().isValid());               // visszavonás → érvénytelen
        QVERIFY(ed->moveUtterances({fx.uid(5), fx.uid(2)}, "B2"));
        QCOMPARE(ed->lastSampleDeviation().utteranceId, fx.uid(2));
        QVERIFY(ed->lastSampleDeviation().deviates);

        // Minden más szerkesztés érvényteleníti.
        QVERIFY(ed->setUtterancesNoisy({fx.uid(7)}, true));
        QVERIFY(!ed->lastSampleDeviation().isValid());

        // Megerősítés: a sor a saját beszélőjéhez mérve. Ádámnak nincs megerősített sora, így a
        // referencia a többi tiszta sora (0, 8, 9 — a 6. nélkül); a „nem minta" megerősítés nem mér.
        QVERIFY(ed->confirmUtterances({fx.uid(6)}));
        dv = ed->lastSampleDeviation();
        QVERIFY(dv.isValid());
        QCOMPARE(dv.speakerKey, QStringLiteral("B1"));
        QVERIFY(!dv.deviates);
        QVERIFY(ed->confirmUtterances({fx.uid(8)}, /*asNoisy*/ true));
        QVERIFY(!ed->lastSampleDeviation().isValid());
    }

    void editorSampleStats()
    {
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QVector<SpeakerSampleStats> st = ed->sampleStats();
        QCOMPARE(st.size(), 2);
        const SpeakerSampleStats* bela = statsOf(st, "B2");
        QVERIFY(bela);
        QCOMPARE(bela->personName, QStringLiteral("Béla"));
        QCOMPARE(bela->sampleLines, 3);
        QCOMPARE(bela->sampleMs, qint64(12000));
        QCOMPARE(bela->sessionAdded, 0);
        QCOMPARE(statsOf(st, "B1")->sampleLines, 0);

        QSignalSpy spy(ed.get(), &SpeakerEditor::sampleStatsChanged);
        QVERIFY(ed->moveUtterances({fx.uid(3)}, "B2"));
        QCOMPARE(spy.count(), 1);
        st = ed->sampleStats();
        bela = statsOf(st, "B2");
        QCOMPARE(bela->sampleLines, 4);
        QCOMPARE(bela->sessionAdded, 1);
        QCOMPARE(bela->sessionAddedIds, QStringList{fx.uid(3)});

        // Nem-minta áthelyezés: nem nő; ignored minta: kiesik.
        QVERIFY(ed->moveUtterances({fx.uid(5)}, "B2", true));
        QCOMPARE(statsOf(ed->sampleStats(), "B2")->sampleLines, 4);
        QVERIFY(ed->setUtterancesIgnored({fx.uid(1)}, true));
        QCOMPARE(statsOf(ed->sampleStats(), "B2")->sampleLines, 3);
        QCOMPARE(statsOf(ed->sampleStats(), "B2")->sessionAdded, 1);

        // Egy nem változtató művelet (lenyomat-anyag lekérdezése) nem jelez.
        const int n = spy.count();
        ed->voiceprintMaterial("B2");
        QCOMPARE(spy.count(), n);

        // Új szerkesztő = új munkamenet.
        auto other = fx.editor();
        QCOMPARE(statsOf(other->sampleStats(), "B2")->sessionAdded, 0);
        QCOMPARE(statsOf(other->sampleStats(), "B2")->sampleLines, 3);
    }

    void editorVoiceprintCollectionSkipsIgnored()
    {
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QCOMPARE(ed->voiceprintMaterial("B2").usableLines, 3);
        QVERIFY(ed->setUtterancesIgnored({fx.uid(1)}, true));
        QCOMPARE(ed->voiceprintMaterial("B2").usableLines, 2);
        const VoiceprintResult r = ed->createVoiceprintFromLines("B2", {fx.uid(1)});
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("nem azonosítható")));
    }
};

QTEST_GUILESS_MAIN(SpeakerIgnoredTest)
#include "test_speaker_ignored.moc"

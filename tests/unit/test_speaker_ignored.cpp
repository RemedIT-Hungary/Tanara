// The "cannot be identified" line state (OverlayUtterance::ignored) + the transparency of
// sample teaching (shared base of the To review redesign). On MADE-UP data:
//  - EVERY branch of the audio analysis skips the ignored line (SpeakerAnalysis, SideAnalysis,
//    ReviewGroups / candidate profiles, voiceprint collection), and it is never uncertain;
//  - SpeakerEditor: setUtterancesIgnored is one undo step, persists; a not-a-sample move in
//    one step; mismatched-sample warning; sample meter.
// The "voices" are low-dimensional unit vectors; no audio, no ffmpeg, the track activity is synthetic.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/domain/Types.h"
#include "tanara/editing/review/ReviewGroups.h"
#include "tanara/editing/review/SideAnalysis.h"
#include "tanara/editing/review/SpeakerAnalysis.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/library/store/MeetingStore.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"

#include <cmath>
#include <memory>

using namespace tanara;
using namespace tanara::editing::speakeredit;

namespace {

constexpr int kDim = 6;

QVector<float> voice(int k)
{
    QVector<float> v(kDim, 0.0f);
    v[k] = 1.0f;
    return v;
}

// ---- pure analysis ----
struct L { int speaker; int axis; bool locked = false; bool noisy = false; bool ignored = false; };

struct Lines {
    QVector<QVector<float>> vecs;   // for the pointers' lifetime
    QVector<editing::speakeredit::AnalysisLine> al;
};

Lines make(const QVector<L>& spec)
{
    Lines out;
    out.vecs.reserve(spec.size());
    for (const L& s : spec) out.vecs.append(voice(s.axis));
    for (int i = 0; i < spec.size(); ++i) {
        editing::speakeredit::AnalysisLine a;
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

// ---- track activity ----
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

// Lines are 4 s every 5 s; side: 'L' mic, 'R' loopback, '-' short (1 s, no audio).
struct Spec { QString raw; char side; int axis; bool confirmed = false; bool ignored = false; };

struct Scenario {
    editing::ReviewInput in;
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

const editing::ReviewGroup* group(const QVector<editing::ReviewGroup>& gs, editing::ReviewKind k)
{
    for (const editing::ReviewGroup& g : gs)
        if (g.kind == k) return &g;
    return nullptr;
}

bool inAnyGroup(const QVector<editing::ReviewGroup>& gs, const QString& id)
{
    for (const editing::ReviewGroup& g : gs)
        if (g.utteranceIds.contains(id)) return true;
    return false;
}

// ---- editor fixture (as in test_speaker_review) ----
struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<library::MeetingStore> store;
    std::unique_ptr<library::PeopleStore> people;
    std::unique_ptr<library::VoiceprintStore> prints;
    Meeting meeting;
    QVector<Spec> specs;
    MeetingActivity act;

    Fixture(const QVector<Spec>& s, const QMap<QString, QString>& names) : specs(s)
    {
        store = std::make_unique<library::MeetingStore>(dir.filePath("rec"), dir.filePath("meta"));
        people = std::make_unique<library::PeopleStore>(dir.filePath("meta/people.json"));
        prints = std::make_unique<library::VoiceprintStore>(dir.filePath("meta/vp.json"));
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
        QFile f(editing::speakeredit::segmentsPath(meeting.folder));
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(segs).toJson());
        f.close();
        meeting.hasTranscript = true;
        meeting.speakerMap = names;
        store->saveMeeting(meeting);

        const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(meeting.folder);
        processing::UtteranceEmbeddingCache c;
        c.fingerprint = editing::speakeredit::transcriptFingerprint(lines);
        SpeakerOverlay ov;
        for (int i = 0; i < specs.size(); ++i) {
            c.set(QStringLiteral("campplus"), lines[i].id, voice(specs[i].axis));
            if (specs[i].confirmed) ov.utterances[lines[i].id].confirmed = true;
        }
        c.save(meeting.folder);
        if (!ov.utterances.isEmpty()) {
            ov.transcriptFingerprint = c.fingerprint;
            editing::speakeredit::saveOverlay(meeting.folder, ov);
        }
        const qint64 total = specs.size() * 5000 + 1000;
        act.tracks = {activity("mic", TrackKind::Mic, total, micSpans),
                      activity("loop", TrackKind::Loopback, total, loopSpans)};
    }

    QString uid(int i) const { return QStringLiteral("u%1").arg(i * 5000); }

    std::unique_ptr<editing::SpeakerEditor> editor()
    {
        auto ed = std::make_unique<editing::SpeakerEditor>(store.get(), people.get(), prints.get(), meeting.id);
        ed->setUserSpeakerName("Ádám");
        const MeetingActivity a = act;
        ed->setActivityProvider([a](const Meeting&) { return a; });
        return ed;
    }
};

bool waitReview(editing::SpeakerEditor& ed)
{
    QSignalSpy spy(&ed, &editing::SpeakerEditor::reviewGroupsChanged);
    return spy.wait(5000);
}

// Among Ádám's (B1, own name, so microphone) lines 3 loopback lines in Béla's voice got in
// (3, 5, 8); Béla's (B2) confirmed lines (1, 4, 7) are on the loopback.
QVector<Spec> sideScenario()
{
    return {{"B1", 'L', 0}, {"B2", 'R', 1, true}, {"B1", 'L', 0}, {"B1", 'R', 1},
            {"B2", 'R', 1, true}, {"B1", 'R', 1}, {"B1", 'L', 0}, {"B2", 'R', 1, true},
            {"B1", 'R', 1}, {"B1", 'L', 0}};
}

const editing::SpeakerSampleStats* statsOf(const QVector<editing::SpeakerSampleStats>& all, const QString& key)
{
    for (const editing::SpeakerSampleStats& s : all)
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
        QVERIFY(editing::speakeredit::saveOverlay(dir.path(), ov));
        const SpeakerOverlay back = editing::speakeredit::loadOverlay(dir.path());
        QVERIFY(back.utterances.contains("u0"));
        QVERIFY(back.utterances.value("u0").ignored);
        QVERIFY(back.utterances.value("u0").speaker.isEmpty());
    }

    // ---- SpeakerAnalysis: every branch ----
    void analysisCentroidAndUncertain()
    {
        // Speaker 0: 4 lines on voice 0 + 1 line on voice 1; speaker 1: 3 lines on voice 1.
        QVector<L> spec{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 1}, {1, 1}};
        Lines plain = make(spec);
        QVERIFY(editing::speakeredit::computeUncertain(plain.al, 2)[4]);                  // control: the odd one out is doubtful
        QVERIFY(editing::speakeredit::computeFits(plain.al, 2)[0].own < 0.99);            // and pulls the centroid

        spec[4].ignored = true;
        Lines ign = make(spec);
        QVERIFY(!ign.al[4].hasEmbedding());
        const QVector<bool> unc = editing::speakeredit::computeUncertain(ign.al, 2);
        QVERIFY(!unc[4]);                                           // never uncertain
        const QVector<editing::speakeredit::LineFit> fits = editing::speakeredit::computeFits(ign.al, 2);
        QVERIFY(std::isnan(fits[4].own) && std::isnan(fits[4].other));
        QCOMPARE(fits[0].own, 1.0);                                 // the centroid is built without it
    }

    void analysisLockedAllSkipsIgnored()
    {
        // kModeLockedAll also uses the locked NOISY lines; the ignored one is left out from there too.
        QVector<L> spec{{0, 0, true, true}, {0, 0, true, true}, {0, 0, true, true}, {0, 0}, {1, 1}};
        QVERIFY(editing::speakeredit::hasTrustedCore(make(spec).al, 2));
        editing::speakeredit::RecheckAnalysis a = computeUncertainRechecked(make(spec).al, 2);
        QVERIFY(a.trustedCore[0]);
        QCOMPARE(a.coreLines[0], 3);

        for (int i = 0; i < 3; ++i) spec[i].ignored = true;
        Lines ign = make(spec);
        QVERIFY(!editing::speakeredit::hasTrustedCore(ign.al, 2));
        a = editing::speakeredit::computeUncertainRechecked(ign.al, 2);
        QVERIFY(!a.trustedCore[0]);
        QCOMPARE(a.coreLines[0], 0);
        for (int i = 0; i < 3; ++i) QVERIFY(!a.lines[i].uncertain);
    }

    void analysisRecheckNeverFlagsIgnored()
    {
        // Confirmed cores: 0 on voice 0, 1 on voice 1. Line 6 is on voice 1 under 0.
        QVector<L> spec{{0, 0, true}, {0, 0, true}, {0, 0, true}, {1, 1, true}, {1, 1, true}, {1, 1, true}, {0, 1}};
        QVERIFY(editing::speakeredit::computeUncertainRechecked(make(spec).al, 2).lines[6].uncertain);
        spec[6].ignored = true;
        QVERIFY(!editing::speakeredit::computeUncertainRechecked(make(spec).al, 2).lines[6].uncertain);
    }

    void analysisSuggestAndPairSkipIgnored()
    {
        // Source (0): 3x voice 0 + 2x voice 1; target (1): 2x voice 1.
        QVector<L> spec{{0, 0}, {0, 0}, {0, 0}, {0, 1}, {0, 1}, {1, 1}, {1, 1}};
        QCOMPARE(editing::speakeredit::suggestSimilar(make(spec).al, 0, 1), (QVector<int>{3, 4}));
        spec[4].ignored = true;
        QCOMPARE(editing::speakeredit::suggestSimilar(make(spec).al, 0, 1), (QVector<int>{3}));

        // Pairwise review: line 6 (under 0, on voice 1) is doubtful; ignored: not.
        QVector<L> pair{{0, 0, true}, {0, 0, true}, {0, 0, true}, {1, 1, true}, {1, 1, true}, {1, 1, true}, {0, 1}};
        QVERIFY(editing::speakeredit::computePairRecheck(make(pair).al, 0, 1).lines[6].flagged);
        pair[6].ignored = true;
        const PairRecheckAnalysis p = computePairRecheck(make(pair).al, 0, 1);
        QVERIFY(p.valid);
        QVERIFY(!p.lines[6].flagged);
    }

    // ---- SideAnalysis ----------------------------------------------------------------
    void sidesLearningAndConflictsSkipIgnored()
    {
        // B1's confirmed lines: 2 on the microphone, 3 on the loopback (mixed, so no clear side).
        QVector<Spec> spec{{"B1", 'L', 0, true}, {"B1", 'L', 0, true}, {"B1", 'R', 0, true},
                           {"B1", 'R', 0, true}, {"B1", 'R', 0, true}, {"B1", 'R', 0}};
        Scenario sc = scenario(spec, true);
        editing::SideReport r = editing::analyzeSides(sc.in.meeting, sc.in.lines, sc.in.overlay, sc.act, QString());
        QVERIFY(r.active);
        QCOMPARE(r.persons.size(), 1);
        QCOMPARE(r.persons[0].remoteLines, 3);
        QVERIFY(r.persons[0].side != editing::Side::Local);

        // The 3 loopback confirmed lines become "cannot be identified", so the person becomes
        // microphone; the non-ignored loopback line is a contradiction, the ignored ones are not.
        for (int i = 2; i <= 4; ++i) spec[i].ignored = true;
        sc = scenario(spec, true);
        r = editing::analyzeSides(sc.in.meeting, sc.in.lines, sc.in.overlay, sc.act, QString());
        QCOMPARE(r.persons[0].localLines, 2);
        QCOMPARE(r.persons[0].remoteLines, 0);
        QCOMPARE(r.persons[0].allLines.total(), 3);
        QCOMPARE(r.persons[0].side, editing::Side::Local);
        for (int i = 2; i <= 4; ++i) QVERIFY(r.lines[i].ignored);
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.conflicts[0].utteranceId, QStringLiteral("u25000"));
    }

    // ---- ReviewGroups + candidate profiles ----
    void reviewGroupsAndProfilesSkipIgnored()
    {
        // A: 3 confirmed (voice 0), B: 3 confirmed (voice 1); for A 2 lines in B's voice + 1 short.
        QVector<Spec> spec{{"A", 'L', 0, true}, {"A", 'L', 0, true}, {"A", 'L', 0, true},
                           {"B", 'L', 1, true}, {"B", 'L', 1, true}, {"B", 'L', 1, true},
                           {"A", 'L', 1}, {"A", 'L', 1}, {"A", '-', -1}};
        Scenario sc = scenario(spec, false);
        editing::ReviewResult r = analyzeReview(sc.in, sc.act);
        const editing::ReviewGroup* core = group(r.groups, editing::ReviewKind::CoreMismatch);
        QVERIFY(core);
        QCOMPARE(core->utteranceIds.size(), 2);
        QVERIFY(group(r.groups, editing::ReviewKind::ShortLines));
        QCOMPARE(r.context.profile("A")->lines, 6);

        spec[6].ignored = true;
        spec[8].ignored = true;
        sc = scenario(spec, false);
        r = analyzeReview(sc.in, sc.act);
        core = group(r.groups, editing::ReviewKind::CoreMismatch);
        QVERIFY(core);
        QCOMPARE(core->utteranceIds, QStringList{"u35000"});
        QVERIFY(!inAnyGroup(r.groups, "u30000"));
        QVERIFY(!group(r.groups, editing::ReviewKind::ShortLines));          // the ignored short line neither
        QCOMPARE(r.context.profile("A")->lines, 4);                 // nor in the profile's line count
        QCOMPARE(r.context.profile("A")->sumCount, 4);

        // Confirmed core: the ignored locked line does not count either (2 < kMinSpeakerLines, so no core).
        spec[0].ignored = true;
        sc = scenario(spec, false);
        r = analyzeReview(sc.in, sc.act);
        QCOMPARE(r.context.profile("A")->confirmedLines, 0);
        QVERIFY(r.context.profile("A")->core.isEmpty());
        QVERIFY(!group(r.groups, editing::ReviewKind::CoreMismatch));
    }

    void reviewSideConflictsSkipIgnored()
    {
        Scenario sc = scenario(sideScenario(), true);
        sc.in.userName = "Ádám";
        sc.in.meeting.speakerMap = {{"B1", "Ádám"}, {"B2", "Béla"}};
        editing::ReviewResult r = analyzeReview(sc.in, sc.act);
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
        const QVector<editing::EditorSpeaker> before = ed->speakers();

        QSignalSpy changed(ed.get(), &editing::SpeakerEditor::utterancesChanged);
        QVERIFY(ed->setUtterancesIgnored({fx.uid(3)}, true));
        QVERIFY(!ed->setUtterancesIgnored({fx.uid(3)}, true));     // already that
        editing::EditorUtterance u = ed->utterance(fx.uid(3));
        QVERIFY(u.ignored);
        QVERIFY(!u.uncertain);
        QVERIFY(u.uncertainReason.isEmpty());
        QCOMPARE(u.speakerKey, QStringLiteral("B1"));               // the name stays
        QVERIFY(!changed.isEmpty());
        QVERIFY(ed->undoText().contains(QStringLiteral("nem azonosítható")));
        // The talk time stays on the name.
        const QVector<editing::EditorSpeaker> after = ed->speakers();
        QCOMPARE(after.size(), before.size());
        for (int i = 0; i < after.size(); ++i) {
            QCOMPARE(after[i].utteranceCount, before[i].utteranceCount);
            QCOMPARE(after[i].talkShare, before[i].talkShare);
        }
        // It is not a speaker change from the summary's point of view.
        QVERIFY(ed->summaryStale().correctedSpeakers == 0);

        // Persists.
        QVERIFY(editing::speakeredit::loadOverlay(fx.meeting.folder).utterances.value(fx.uid(3)).ignored);
        {
            auto other = fx.editor();
            QVERIFY(other->utterance(fx.uid(3)).ignored);
        }

        const int undoDepth = ed->undoSerials().size();
        ed->undo();
        QVERIFY(!ed->utterance(fx.uid(3)).ignored);
        QVERIFY(!editing::speakeredit::loadOverlay(fx.meeting.folder).utterances.value(fx.uid(3)).ignored);
        ed->redo();
        QVERIFY(ed->utterance(fx.uid(3)).ignored);
        QCOMPARE(ed->undoSerials().size(), undoDepth);

        // Undo with an operation too (ignored=false), one step.
        QVERIFY(ed->setUtterancesIgnored({fx.uid(3)}, false));
        QVERIFY(!ed->utterance(fx.uid(3)).ignored);
        QCOMPARE(ed->undoSerials().size(), undoDepth + 1);
        QVERIFY(!editing::speakeredit::loadOverlay(fx.meeting.folder).utterances.contains(fx.uid(3)));   // the base state: no entry
    }

    void editorReviewGroupsSkipIgnored()
    {
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QVERIFY(waitReview(*ed));
        const editing::ReviewGroup* side = group(ed->reviewGroups(), editing::ReviewKind::SideConflict);
        QVERIFY(side);
        QCOMPARE(side->utteranceIds, (QStringList{fx.uid(3), fx.uid(5), fx.uid(8)}));

        QVERIFY(ed->setUtterancesIgnored({fx.uid(3), fx.uid(5)}, true));
        QCOMPARE(ed->undoSerials().size(), 1);
        QVERIFY(waitReview(*ed));
        side = group(ed->reviewGroups(), editing::ReviewKind::SideConflict);
        QVERIFY(side);
        QCOMPARE(side->utteranceIds, QStringList{fx.uid(8)});
        QVERIFY(!ed->uncertainUtteranceIds().contains(fx.uid(3)));
        QVERIFY(ed->uncertainUtteranceIds().contains(fx.uid(8)));

        QVERIFY(ed->setUtterancesIgnored({fx.uid(8)}, true));
        QVERIFY(waitReview(*ed));
        QVERIFY(!group(ed->reviewGroups(), editing::ReviewKind::SideConflict));
        for (const QString& id : {fx.uid(3), fx.uid(5), fx.uid(8)})
            QVERIFY(!inAnyGroup(ed->reviewGroups(), id));

        // In the candidate ranking the ignored line's voice is not in the current speaker's sum, but
        // candidates can still be requested for the line (the user can decide later).
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
        QSignalSpy undoSpy(ed.get(), &editing::SpeakerEditor::undoStateChanged);

        QVERIFY(ed->moveUtterances({fx.uid(3)}, "B2", /*noisy*/ true));
        QCOMPARE(ed->undoSerials().size(), 1);
        editing::EditorUtterance u = ed->utterance(fx.uid(3));
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

        // To a person (a new participant) and to a new anonymous one in one step too.
        const QString key = ed->moveUtterancesToPerson({fx.uid(5)}, "Cecil", true);
        QVERIFY(!key.isEmpty());
        QCOMPARE(ed->undoSerials().size(), 1);
        QVERIFY(ed->utterance(fx.uid(5)).noisy && ed->utterance(fx.uid(5)).manuallyCorrected);
        const QString anon = ed->moveUtterancesToNewParticipant({fx.uid(8)}, true);
        QVERIFY(!anon.isEmpty());
        QCOMPARE(ed->undoSerials().size(), 2);
        QVERIFY(ed->utterance(fx.uid(8)).noisy && ed->utterance(fx.uid(8)).manuallyCorrected);

        // The default (noisy=false) does not touch the noisy flag.
        QVERIFY(ed->moveUtterances({fx.uid(0)}, "B2"));
        QVERIFY(!ed->utterance(fx.uid(0)).noisy);
    }

    void editorSampleDeviation()
    {
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QSignalSpy spy(ed.get(), &editing::SpeakerEditor::sampleDeviationChanged);
        QVERIFY(!ed->lastSampleDeviation().isValid());

        // A line in Béla's voice to Béla: fits.
        QVERIFY(ed->moveUtterances({fx.uid(3)}, "B2"));
        editing::SampleDeviation dv = ed->lastSampleDeviation();
        QVERIFY(dv.isValid());
        QCOMPARE(dv.utteranceId, fx.uid(3));
        QCOMPARE(dv.speakerKey, QStringLiteral("B2"));
        QVERIFY(dv.similarity > 0.99);
        QVERIFY(!dv.deviates);
        QCOMPARE(spy.count(), 1);

        // A line in Ádám's voice to Béla (as a sample): mismatch.
        QVERIFY(ed->moveUtterances({fx.uid(0)}, "B2"));
        dv = ed->lastSampleDeviation();
        QVERIFY(dv.isValid());
        QCOMPARE(dv.utteranceId, fx.uid(0));
        QVERIFY(dv.similarity < editing::speakeredit::kSampleDeviationFit);
        QVERIFY(dv.deviates);

        // Several lines in one decision: the worst comes back.
        ed->undo();
        QVERIFY(!ed->lastSampleDeviation().isValid());               // undo: invalid
        QVERIFY(ed->moveUtterances({fx.uid(5), fx.uid(2)}, "B2"));
        QCOMPARE(ed->lastSampleDeviation().utteranceId, fx.uid(2));
        QVERIFY(ed->lastSampleDeviation().deviates);

        // Every other edit invalidates it.
        QVERIFY(ed->setUtterancesNoisy({fx.uid(7)}, true));
        QVERIFY(!ed->lastSampleDeviation().isValid());

        // Confirmation: the line measured against its own speaker. Ádám has no confirmed line, so
        // the reference is the other clean lines (0, 8, 9, without the 6th); a "not a sample" confirmation does not measure.
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
        QVector<editing::SpeakerSampleStats> st = ed->sampleStats();
        QCOMPARE(st.size(), 2);
        const editing::SpeakerSampleStats* bela = statsOf(st, "B2");
        QVERIFY(bela);
        QCOMPARE(bela->personName, QStringLiteral("Béla"));
        QCOMPARE(bela->sampleLines, 3);
        QCOMPARE(bela->sampleMs, qint64(12000));
        QCOMPARE(bela->sessionAdded, 0);
        QCOMPARE(statsOf(st, "B1")->sampleLines, 0);

        QSignalSpy spy(ed.get(), &editing::SpeakerEditor::sampleStatsChanged);
        QVERIFY(ed->moveUtterances({fx.uid(3)}, "B2"));
        QCOMPARE(spy.count(), 1);
        st = ed->sampleStats();
        bela = statsOf(st, "B2");
        QCOMPARE(bela->sampleLines, 4);
        QCOMPARE(bela->sessionAdded, 1);
        QCOMPARE(bela->sessionAddedIds, QStringList{fx.uid(3)});

        // A not-a-sample move: does not grow; an ignored sample: drops out.
        QVERIFY(ed->moveUtterances({fx.uid(5)}, "B2", true));
        QCOMPARE(statsOf(ed->sampleStats(), "B2")->sampleLines, 4);
        QVERIFY(ed->setUtterancesIgnored({fx.uid(1)}, true));
        QCOMPARE(statsOf(ed->sampleStats(), "B2")->sampleLines, 3);
        QCOMPARE(statsOf(ed->sampleStats(), "B2")->sessionAdded, 1);

        // A non-changing operation (querying the voiceprint material) does not signal.
        const int n = spy.count();
        ed->voiceprintMaterial("B2");
        QCOMPARE(spy.count(), n);

        // A new editor = a new session.
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
        const editing::VoiceprintResult r = ed->createVoiceprintFromLines("B2", {fx.uid(1)});
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("nem azonosítható")));
    }
};

QTEST_GUILESS_MAIN(SpeakerIgnoredTest)
#include "test_speaker_ignored.moc"

// SpeakerEditor v3: manual track assignment (undo), review queue groups computed in the
// background, executing a group in one undo step, splitting, the "uncertain, track" reason,
// learned track side and the candidates. In a temporary folder, with MADE-UP data: the
// embeddings go straight into the cache, the track activity is synthetic
// (setActivityProvider); no audio, no ffmpeg.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/Types.h"
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

// side: 'L' mic, 'R' loopback; axis: the voice's axis.
struct Spec { QString raw; char side; int axis; bool confirmed = false; };

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
        meeting = store->createMeeting("Átnézendő");
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
        auto track = [&](const QString& id, TrackKind kind, const QVector<QPair<qint64, qint64>>& spans) {
            TrackActivity t;
            t.trackId = id;
            t.kind = kind;
            t.frameMs = 50;
            t.dbAboveFloor = QVector<float>(int(total / 50), 0.0f);
            for (const auto& sp : spans)
                for (qint64 ms = sp.first; ms < sp.second; ms += 50) t.dbAboveFloor[int(ms / 50)] = 30.0f;
            return t;
        };
        act.tracks = {track("mic", TrackKind::Mic, micSpans), track("loop", TrackKind::Loopback, loopSpans)};
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

const ReviewGroup* group(const QVector<ReviewGroup>& gs, ReviewKind k)
{
    for (const ReviewGroup& g : gs)
        if (g.kind == k) return &g;
    return nullptr;
}

// Among Ádám's (B1, own name, so microphone) lines 3 loopback lines in Béla's voice got in;
// Béla's (B2) confirmed lines are on the loopback.
QVector<Spec> sideScenario()
{
    return {{"B1", 'L', 0}, {"B2", 'R', 1, true}, {"B1", 'L', 0}, {"B1", 'R', 1},
            {"B2", 'R', 1, true}, {"B1", 'R', 1}, {"B1", 'L', 0}, {"B2", 'R', 1, true},
            {"B1", 'R', 1}, {"B1", 'L', 0}};
}

} // namespace

class SpeakerReviewTest : public QObject {
    Q_OBJECT
private slots:
    void trackAssignmentIsUndoable()
    {
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QVERIFY(ed->trackAssignment("B1").isEmpty());
        QVERIFY(ed->setTrackAssignment("B1", {"mic", "nincs-ilyen", "mic"}));
        QCOMPARE(ed->trackAssignment("B1"), QStringList{"mic"});
        QVERIFY(!ed->setTrackAssignment("B1", {"mic"}));            // did not change
        QVERIFY(!ed->setTrackAssignment("ismeretlen", {"mic"}));
        QVERIFY(ed->canUndo());
        // Persistent.
        QCOMPARE(loadOverlay(fx.meeting.folder).speakerTracks.value("B1"), QStringList{"mic"});
        ed->undo();
        QVERIFY(ed->trackAssignment("B1").isEmpty());
        ed->redo();
        QCOMPARE(ed->trackAssignment("B1"), QStringList{"mic"});
        QVERIFY(ed->setTrackAssignment("B1", {}));
        QVERIFY(ed->trackAssignment("B1").isEmpty());
    }

    void sideConflictGroupAppliesInOneStep()
    {
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QVERIFY(waitReview(*ed));
        QVERIFY(ed->sideReport().active);
        const QVector<ReviewGroup> groups = ed->reviewGroups();
        const ReviewGroup* g = group(groups, ReviewKind::SideConflict);
        QVERIFY(g);
        QCOMPARE(g->utteranceIds, (QStringList{fx.uid(3), fx.uid(5), fx.uid(8)}));
        QCOMPARE(g->proposedSpeakerKey, QStringLiteral("B2"));

        // The track contradiction is the reason for the uncertainty.
        QCOMPARE(ed->utterance(fx.uid(3)).uncertainReason, QStringLiteral("side"));
        QVERIFY(ed->utterance(fx.uid(3)).uncertain);
        QVERIFY(ed->utterance(fx.uid(0)).uncertainReason.isEmpty());
        QVERIFY(ed->uncertainUtteranceIds().contains(fx.uid(5)));

        // "Why not Ádám?": a track contradiction, a fix link to the track assignment.
        bool sideWhy = false;
        for (const Evidence& e : ed->whyNot(fx.uid(3)))
            sideWhy |= e.kind == EvidenceKind::Side && e.fixTarget == "tracks";
        QVERIFY(sideWhy);
        const QVector<Candidate> cands = ed->lineCandidates(fx.uid(3));
        QVERIFY(!cands.isEmpty());
        QCOMPARE(cands.first().speakerKey, QStringLiteral("B2"));

        // The learned side: Béla's confirmed lines are on the loopback.
        QCOMPARE(fx.people->defaultSide("Béla"), QStringLiteral("remote"));

        const int undoDepth = ed->canUndo() ? 1 : 0;
        QVERIFY(ed->applyReviewGroup(g->id));
        for (int i : {3, 5, 8}) QCOMPARE(ed->utterance(fx.uid(i)).speakerKey, QStringLiteral("B2"));
        QVERIFY(group(ed->reviewGroups(), ReviewKind::SideConflict) == nullptr);
        QVERIFY(!ed->applyReviewGroup(g->id));   // no such one anymore
        // One step back: all three are back.
        ed->undo();
        for (int i : {3, 5, 8}) QCOMPARE(ed->utterance(fx.uid(i)).speakerKey, QStringLiteral("B1"));
        QCOMPARE(ed->canUndo() ? 1 : 0, undoDepth);
        // After the recompute the group comes back.
        QVERIFY(waitReview(*ed));
        QVERIFY(group(ed->reviewGroups(), ReviewKind::SideConflict));
    }

    void manualAssignmentRemovesConflict()
    {
        // If we manually assign Ádám to both tracks (e.g. he speaks on speakerphone), there is no contradiction.
        Fixture fx(sideScenario(), {{"B1", "Ádám"}, {"B2", "Béla"}});
        auto ed = fx.editor();
        QVERIFY(waitReview(*ed));
        QVERIFY(group(ed->reviewGroups(), ReviewKind::SideConflict));
        QVERIFY(ed->setTrackAssignment("B1", {"mic", "loop"}));
        QVERIFY(waitReview(*ed));
        QVERIFY(!group(ed->reviewGroups(), ReviewKind::SideConflict));
        QVERIFY(ed->utterance(fx.uid(3)).uncertainReason != "side");
        // Undo: back.
        ed->undo();
        QVERIFY(waitReview(*ed));
        QVERIFY(group(ed->reviewGroups(), ReviewKind::SideConflict));
    }

    void splitContaminatedCore()
    {
        QVector<Spec> specs;
        for (int i = 0; i < 5; ++i) specs << Spec{"A", 'L', 0, true};
        for (int i = 0; i < 3; ++i) specs << Spec{"A", 'L', 2, true};
        specs << Spec{"C", 'L', 2, true} << Spec{"C", 'L', 2, true} << Spec{"C", 'L', 2, true};
        Fixture fx(specs, {{"A", "Anna"}, {"C", "Cecil"}});
        auto ed = fx.editor();
        QVERIFY(waitReview(*ed));
        const ReviewGroup* g = group(ed->reviewGroups(), ReviewKind::ContaminatedCore);
        QVERIFY(g);
        QCOMPARE(g->proposedSpeakerKey, QStringLiteral("C"));
        QVERIFY(!ed->splitSpeaker("C"));     // C's core is uniform
        QVERIFY(ed->splitSpeaker("A"));
        for (int i : {5, 6, 7}) QCOMPARE(ed->utterance(fx.uid(i)).speakerKey, QStringLiteral("C"));
        QCOMPARE(ed->utterance(fx.uid(0)).speakerKey, QStringLiteral("A"));
        ed->undo();
        for (int i : {5, 6, 7}) QCOMPARE(ed->utterance(fx.uid(i)).speakerKey, QStringLiteral("A"));
    }

    void similarLinesAfterNewPerson()
    {
        Fixture fx({{"A", 'L', 0}, {"A", 'L', 3}, {"A", 'L', 0}, {"A", 'L', 3}, {"A", 'L', 3}, {"A", 'L', 3}},
                   {{"A", "Anna"}});
        auto ed = fx.editor();
        QVERIFY(waitReview(*ed));
        const QString key = ed->moveUtterancesToNewParticipant({fx.uid(4), fx.uid(5)});
        QVERIFY(!key.isEmpty());
        QVERIFY(waitReview(*ed));
        const ReviewGroup* g = group(ed->reviewGroups(), ReviewKind::SimilarToNewPerson);
        QVERIFY(g);
        QCOMPARE(g->proposedSpeakerKey, key);
        QCOMPARE(g->utteranceIds, (QStringList{fx.uid(1), fx.uid(3)}));
        QVERIFY(ed->applyReviewGroup(g->id));
        QCOMPARE(ed->utterance(fx.uid(1)).speakerKey, key);
        QCOMPARE(ed->utterance(fx.uid(3)).speakerKey, key);
        ed->undo();
        QCOMPARE(ed->utterance(fx.uid(1)).speakerKey, QStringLiteral("A"));
        QCOMPARE(ed->utterance(fx.uid(4)).speakerKey, key);   // the previous step stayed
    }

    void tagReasonAndSpeakerEvidence()
    {
        // Tag seam: Anna's tags do not fit the meeting, so "Why them?" shows a contradicting tag.
        Fixture fx({{"A", 'L', 0}, {"A", 'L', 0}, {"A", 'L', 0}}, {{"A", "Anna"}});
        fx.meeting = fx.store->load(fx.meeting.id);
        fx.meeting.tagIds = {"t-projekt"};
        fx.store->saveMeeting(fx.meeting);
        auto ed = fx.editor();
        // The contradiction only with the strict guards: Anna has a manual tag, the meeting's tag
        // matured (5 meetings) and Anna was at none of them.
        PersonTagSnapshot snap;
        QVector<PersonTagRow> rows;
        for (int i = 0; i < 5; ++i)
            rows << PersonTagRow{QStringLiteral("m%1").arg(i), QDateTime(), {"t-projekt"}, {QStringLiteral("Béla")}};
        snap.stats = std::make_shared<PersonTagStats>(PersonTagStats::compute(rows, QString()));
        snap.tagNames = {{"t-projekt", "Projekt"}, {"t-marketing", "Marketing"}};
        snap.manual.insert(persontags::personKey("Anna"), {"t-marketing"});
        ed->setTagEvidenceProvider([snap](const QString& p, const QStringList& tags, const QString& mid) {
            return snap.evidence(p, tags, mid);
        });
        QVERIFY(waitReview(*ed));
        bool tagContra = false;
        for (const Evidence& e : ed->speakerEvidence("A"))
            tagContra |= e.kind == EvidenceKind::Tag && e.polarity == Polarity::Contradict && e.fixTarget == "tags"
                         && e.text == QStringLiteral("más címkék") && e.detail.contains(QStringLiteral("#Marketing"));
        QVERIFY(tagContra);
    }
};

QTEST_GUILESS_MAIN(SpeakerReviewTest)
#include "test_speaker_review.moc"

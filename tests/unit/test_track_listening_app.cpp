// Listening to tracks in AppController: the speech strip's activity for EVERY track (also the
// one left out of the mixdown) computed in the background and cached (trackActivityReady), and
// the speaker-to-track binding (setParticipantTracks / participantTracks): before the
// transcript Participant::tracks, after the transcript the editor's manual track assignment
// (the same one the "Why them?" chips write), the inferred side without a manual binding, and
// when the transcript arrives the participant's binding moves into the speaker's assignment.
// Synthetic tracks (ffmpeg lavfi), ISOLATED TANARA_HOME, without Soniox / LLM. Without ffmpeg QSKIP.
#include "tanara/domain/EvidenceText.h"
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/app/AppController.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/library/store/detail/MeetingJson.h"
#include "tanara/library/store/ParticipantJson.h"
#include "tanara/library/store/MeetingRepository.h"

#include <memory>

using namespace tanara;

namespace {

// A 24 s track: a sine of freq Hz where `on` is true, digital silence elsewhere.
bool makeTrack(const QString& path, int freq, const QString& onExpr)
{
    QProcess p;
    const QString filter = QStringLiteral("sine=frequency=%1:duration=24,volume='if(%2,1,0)':eval=frame")
                               .arg(freq).arg(onExpr);
    p.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i", filter, "-ar", "16000", "-ac", "1",
                                       "-y", path});
    return p.waitForFinished(30000) && p.exitCode() == 0 && QFileInfo::exists(path);
}

} // namespace

class TrackListeningAppTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void activityCoversExcludedTrackAndIsCached();
    void participantTracksSerialize();
    void bindingBeforeTranscript();
    void bindingAfterTranscriptIsTheEditorAssignment();
    void bindingCopiedToOverlayOnTranscript();
    void manualTracksSteerClusterMatchAndEvidence();

private:

    // People by name (people.json): created if missing (pid) / only looked up (idOf); a picker's choice; the name.
    PersonId pid(const QString& name) { return m_app->people()->ensure(name); }
    PersonId idOf(const QString& name) { m_app->people()->refresh(); return m_app->people()->idForName(name); }
    PersonChoice choice(const QString& name) { return m_app->people()->choiceFor(name); }
    QString nameOf(const PersonId& id) { m_app->people()->refresh(); return m_app->people()->displayName(id); }
    Meeting makeMeeting();
    void writeTranscript(Meeting& m);

    bool m_haveFfmpeg = false;
    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<app::AppController> m_app;
};

void TrackListeningAppTest::initTestCase()
{
    m_haveFfmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    qputenv("TANARA_CLOUD", "off");
}

void TrackListeningAppTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    m_app = std::make_unique<app::AppController>();
    QVERIFY(m_app->store()->metadataDir().startsWith(m_home->path()));
    m_app->setUserSpeakerName(QStringLiteral("Ádám"));
}

void TrackListeningAppTest::cleanup()
{
    m_app.reset();
    m_home.reset();
}

// Microphone: 220 Hz at 0-8 and 16-24 s; loopback: 440 Hz at 8-16 s, MANUALLY excluded from the mixdown.
Meeting TrackListeningAppTest::makeMeeting()
{
    Meeting m = m_app->store()->createMeeting(QStringLiteral("Meghallgatás"));
    const QDir dir(m.folder);
    if (m_haveFfmpeg) {
        [&] { QVERIFY(makeTrack(dir.filePath("track_mic.wav"), 220, "not(between(t,8,16))")); }();
        [&] { QVERIFY(makeTrack(dir.filePath("track_loop.wav"), 440, "between(t,8,16)")); }();
    }
    auto add = [&m](const QString& id, TrackKind kind, const QString& file) {
        Track t;
        t.id = id;
        t.deviceName = id;
        t.kind = kind;
        t.file = file;
        m.tracks.append(t);
    };
    add("mic", TrackKind::Mic, "track_mic.wav");
    add("loop", TrackKind::Loopback, "track_loop.wav");
    m.durationMs = 24000;
    Participant me;
    me.id = "v1";
    me.personId = pid("Ádám");
    me.sides = {"mic"};
    me.rawSpeakerIds = {"Beszélő 1"};
    Participant far;
    far.id = "v2";
    far.personId = pid("Béla");
    far.sides = {"loopback"};
    far.rawSpeakerIds = {"Beszélő 2@loopback"};
    m.participants = {me, far};
    m_app->store()->saveMeeting(m);
    return m;
}

void TrackListeningAppTest::writeTranscript(Meeting& m)
{
    const QVector<std::tuple<qint64, qint64, QString>> rows = {
        {500, 7500, "Beszélő 1"}, {8500, 15500, "Beszélő 2"}, {16500, 23500, "Beszélő 1"}};
    QJsonArray toks, segs;
    for (const auto& [s, e, spk] : rows) {
        toks.append(QJsonObject{{"text", "x"}, {"speaker", spk}, {"startMs", double(s)}, {"endMs", double(e)},
                                {"confidence", 1.0}, {"trackId", "mixdown"}});
        segs.append(QJsonObject{{"startMs", double(s)}, {"endMs", double(e)}, {"speaker", spk}, {"text", "x"}});
    }
    QFile tf(QDir(m.folder).filePath("transcript.tokens.json"));
    QVERIFY(tf.open(QIODevice::WriteOnly));
    tf.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
    tf.close();
    QFile sf(QDir(m.folder).filePath("transcript.segments.json"));
    QVERIFY(sf.open(QIODevice::WriteOnly));
    sf.write(QJsonDocument(segs).toJson());
    sf.close();
    m = m_app->store()->load(m.id);
    m.hasTranscript = true;
    m_app->store()->saveMeeting(m);
}

void TrackListeningAppTest::activityCoversExcludedTrackAndIsCached()
{
    if (!m_haveFfmpeg) QSKIP("nincs ffmpeg");
    const Meeting m0 = makeMeeting();
    // The loopback is manually excluded (the mixdown remix is a side effect, we do not wait for it here).
    Meeting m = m_app->store()->load(m0.id);
    m.tracks[1].active = false;
    m.tracks[1].excludedReason = QStringLiteral("manual");
    m_app->store()->saveMeeting(m);

    QSignalSpy ready(m_app.get(), &app::AppController::trackActivityReady);
    MeetingActivity a = m_app->trackActivity(m0.id);
    QVERIFY(a.isEmpty());                       // not there yet: computing in the background
    QVERIFY(m_app->trackActivityComputing(m0.id));
    QVERIFY(m_app->trackActivity(m0.id).isEmpty());   // no second computation starts
    QVERIFY(ready.wait(30000));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.first().at(0).toString(), m0.id);
    QVERIFY(!m_app->trackActivityComputing(m0.id));

    a = m_app->trackActivity(m0.id);
    QCOMPARE(a.tracks.size(), 2);               // the excluded track too
    QCOMPARE(a.tracks[0].trackId, QStringLiteral("mic"));
    QCOMPARE(a.tracks[1].trackId, QStringLiteral("loop"));
    // The loopback plays between 8 and 16 s: at 10 s well above the floor, at 2 s not.
    const TrackActivity& loop = a.tracks[1];
    QVERIFY(loop.dbAboveFloor.at(10000 / loop.frameMs) > 20.0f);
    QVERIFY(loop.dbAboveFloor.at(2000 / loop.frameMs) < 3.0f);
    QVERIFY(QFileInfo::exists(MeetingActivity::viewFilePath(m.folder)));

    // A new AppController (a new process): it comes from disk, without computing.
    m_app = std::make_unique<app::AppController>();
    QSignalSpy ready2(m_app.get(), &app::AppController::trackActivityReady);
    QCOMPARE(m_app->trackActivity(m0.id).tracks.size(), 2);
    QVERIFY(!m_app->trackActivityComputing(m0.id));
    QCOMPARE(ready2.count(), 0);

    // Unknown meeting: empty, no signal.
    QVERIFY(m_app->trackActivity(QStringLiteral("nincs-ilyen")).isEmpty());
    QVERIFY(!m_app->trackActivityComputing(QStringLiteral("nincs-ilyen")));
}

void TrackListeningAppTest::participantTracksSerialize()
{
    Participant p;
    p.id = "v1";
    p.personId = pid("Ádám");
    p.tracks = {"mic", "loop"};
    const Participant back = library::participantFromJson(library::toJson(p));
    QCOMPARE(back.tracks, p.tracks);
    Participant none;
    none.id = "v2";
    QVERIFY(!library::toJson(none).contains(QStringLiteral("tracks")));   // we do not write it out when empty
}

void TrackListeningAppTest::bindingBeforeTranscript()
{
    const Meeting m0 = makeMeeting();
    // Inferred: the audio analysis's side (sides) gives the included tracks.
    ParticipantTrackBinding b = m_app->participantTracks(m0.id, "v1");
    QVERIFY(!b.manual);
    QCOMPARE(b.trackIds, QStringList{"mic"});
    b = m_app->participantTracks(m0.id, "v2");
    QCOMPARE(b.trackIds, QStringList{"loop"});

    QSignalSpy changed(m_app.get(), &app::AppController::participantsChanged);
    // An unknown track id is left out; the binding goes into Participant::tracks.
    QVERIFY(m_app->setParticipantTracks(m0.id, "v2", {"mic", "nincs", "loop"}));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m_app->participants(m0.id).at(1).tracks, (QStringList{"mic", "loop"}));
    b = m_app->participantTracks(m0.id, "v2");
    QVERIFY(b.manual);
    QCOMPARE(b.trackIds, (QStringList{"mic", "loop"}));
    QVERIFY(!m_app->setParticipantTracks(m0.id, "v2", {"mic", "loop"}));   // no change
    QCOMPARE(changed.count(), 1);

    // Empty = back to inference.
    QVERIFY(m_app->setParticipantTracks(m0.id, "v2", {}));
    b = m_app->participantTracks(m0.id, "v2");
    QVERIFY(!b.manual);
    QCOMPARE(b.trackIds, QStringList{"loop"});

    // Unknown participant / speaker: nothing to bind.
    QVERIFY(!m_app->setParticipantTracks(m0.id, "nincs-ilyen", {"mic"}));
}

void TrackListeningAppTest::bindingAfterTranscriptIsTheEditorAssignment()
{
    Meeting m = makeMeeting();
    writeTranscript(m);
    editing::SpeakerEditSession* ed = m_app->editSession(m.id);
    QVERIFY(ed && ed->hasTranscript());

    // Participant id: its raw speaker (without the "@loopback" suffix) in the editor.
    QVERIFY(m_app->setParticipantTracks(m.id, "v2", {"loop"}));
    QCOMPARE(ed->trackAssignment(QStringLiteral("Beszélő 2")), QStringList{"loop"});
    QCOMPARE(m_app->participants(m.id).at(1).tracks, QStringList{"loop"});
    ParticipantTrackBinding b = m_app->participantTracks(m.id, "v2");
    QVERIFY(b.manual);
    QCOMPARE(b.trackIds, QStringList{"loop"});

    // Modified from the editor's ("Why them?") side we see the same: one piece of data.
    QVERIFY(ed->setTrackAssignment(QStringLiteral("Beszélő 2"), {"mic"}));
    b = m_app->participantTracks(m.id, "v2");
    QCOMPARE(b.trackIds, QStringList{"mic"});
    // Undo gives the earlier binding.
    ed->undo();
    QCOMPARE(m_app->participantTracks(m.id, "v2").trackIds, QStringList{"loop"});

    // It can be bound by speaker key too (a post-transcript line, even without a participant); the
    // participant belonging to it gets it too.
    QVERIFY(m_app->setParticipantTracks(m.id, QStringLiteral("Beszélő 1"), {"mic", "loop"}));
    QCOMPARE(ed->trackAssignment(QStringLiteral("Beszélő 1")), (QStringList{"mic", "loop"}));
    QCOMPARE(m_app->participants(m.id).at(0).tracks, (QStringList{"mic", "loop"}));
}

void TrackListeningAppTest::bindingCopiedToOverlayOnTranscript()
{
    Meeting m = makeMeeting();
    QVERIFY(m_app->setParticipantTracks(m.id, "v1", {"mic", "loop"}));
    // Approval before the transcript; the transcript's arrival (applyParticipantsToTranscript) carries it over.
    QVERIFY(m_app->approveParticipants(m.id, m_app->participants(m.id)));
    writeTranscript(m);
    // The transcript-arrival hook is private: re-applying the approval goes the same way.
    QVERIFY(m_app->approveParticipants(m.id, m_app->participants(m.id)));
    editing::SpeakerEditSession* ed = m_app->editSession(m.id);
    QVERIFY(ed);
    QCOMPARE(ed->trackAssignment(QStringLiteral("Beszélő 1")), (QStringList{"mic", "loop"}));
    QCOMPARE(m_app->participantTracks(m.id, "v1").trackIds, (QStringList{"mic", "loop"}));
    QVERIFY(m_app->participantTracks(m.id, "v1").manual);
}

// Pre-transcript analysis: the manual track binding also determines the matching (a cluster
// speaking on another track cannot be them) and the evidence ("by hand: call"); the
// re-analysis keeps the binding.
void TrackListeningAppTest::manualTracksSteerClusterMatchAndEvidence()
{
    Meeting m = makeMeeting();
    m.participants[1].tracks = {"loop"};            // Béla bound to the call track by hand
    m.participants[1].source = ParticipantSource::Voice;
    ClusterSet cs;
    VoiceCluster onMic;
    onMic.trackId = "mic";
    onMic.side = "mic";
    onMic.windows = {{0, 4000}};
    VoiceCluster onLoop = onMic;
    onLoop.trackId = "loop";
    onLoop.side = "loopback";
    onLoop.windows = {{8000, 12000}};
    cs.clusters = {onMic, onLoop};
    processing::participants::CandidateContext ctx;
    // Both clusters resemble Béla best (e.g. echo on the microphone).
    const PersonId belaId = pid(QStringLiteral("Béla"));
    ctx.rank = [belaId](const EmbeddingSet&) { return QVector<VoiceMatch>{{belaId, 0.8}}; };
    const QVector<Participant> out = processing::participants::buildParticipants(cs, m, ctx);
    QVERIFY(cs.clusters[0].matchPersonId.isNull());             // the microphone cluster is not them
    QCOMPARE(cs.clusters[1].matchPersonId, belaId);
    const Participant* bela = nullptr;
    for (const Participant& p : out)
        if (p.personId == belaId) bela = &p;
    QVERIFY(bela);
    QCOMPARE(bela->tracks, QStringList{"loop"});                 // the re-analysis keeps it
    bool manualSide = false;
    for (const Evidence& e : bela->evidence)
        if (e.kind == EvidenceKind::Side && evidenceText(e) == QStringLiteral("kézzel: hívás")
            && e.polarity == Polarity::Support)
            manualSide = true;
    QVERIFY(manualSide);

    // When the binding changes AppController refreshes the evidence too.
    QVERIFY(m_app->setParticipantTracks(m.id, "v1", {"mic", "loop"}));
    bool both = false;
    for (const Evidence& e : m_app->participants(m.id).at(0).evidence)
        both = both || (e.kind == EvidenceKind::Side && evidenceText(e) == QStringLiteral("kézzel: mikrofon és hívás"));
    QVERIFY(both);
}

QTEST_MAIN(TrackListeningAppTest)
#include "test_track_listening_app.moc"

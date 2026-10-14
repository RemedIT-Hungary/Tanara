// Participants in AppController: track speech ratio with synthetic tracks (ffmpeg lavfi) and
// exclusion from the mixdown, "Include" / manual exclusion; pre-transcript audio analysis on
// a background thread with a fake embedder (the audio's frequency is the "voice"), matching
// with the voiceprints; binding to the raw speakers; approval and renaming in one undo step;
// "Was not there"; "Only me". ISOLATED TANARA_HOME, without Soniox / LLM. Without ffmpeg QSKIP.
#include "tanara/domain/EvidenceText.h"
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/app/AppController.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/recording/audio/MixdownPlan.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/editing/undo/MeetingUndo.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/editing/session/TrackSpeech.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <memory>

using namespace tanara;

namespace {

const QString kCamp = QStringLiteral("campplus");

// Fake "model": the frequency estimated from zero crossings decides (220 Hz is dim 0, 440 Hz is dim 1).
processing::PcmEmbedderLoader fakeLoader()
{
    return [](const processing::VoiceModelSpec& spec, const QString&, QString*) -> processing::PcmEmbedder {
        const int dim = spec.dim;
        return [dim](const QVector<float>& pcm) {
            if (pcm.size() < 8000) return QVector<float>();
            int zc = 0;
            for (int i = 1; i < pcm.size(); ++i)
                if ((pcm[i - 1] < 0.0f) != (pcm[i] < 0.0f)) ++zc;
            const double hz = zc / 2.0 / (pcm.size() / 16000.0);
            QVector<float> v(dim, 0.0f);
            v[hz < 330.0 ? 0 : 1] = 1.0f;
            return v;
        };
    };
}

// A 24 s track: a sine of freq Hz where `on` is true (an ffmpeg volume expression), digital silence elsewhere.
bool makeTrack(const QString& path, int freq, const QString& onExpr)
{
    QProcess p;
    const QString filter = freq > 0
        ? QStringLiteral("sine=frequency=%1:duration=24,volume='if(%2,1,0)':eval=frame").arg(freq).arg(onExpr)
        : QStringLiteral("anullsrc=r=16000:cl=mono,atrim=duration=24");
    p.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i", filter, "-ar", "16000", "-ac", "1",
                                       "-y", path});
    return p.waitForFinished(30000) && p.exitCode() == 0 && QFileInfo::exists(path);
}

Voiceprint print(int hot)
{
    Voiceprint vp;
    vp.model = kCamp;
    vp.embedding = QVector<float>(processing::VoiceModelRegistry::spec(kCamp)->dim, 0.0f);
    vp.embedding[hot] = 1.0f;
    vp.sampleRef = QStringLiteral("x.wav#0-1000");
    return vp;
}

const Participant* byName(const QVector<Participant>& ps, const PersonId& person)
{
    for (const Participant& p : ps)
        if (p.personId == person) return &p;
    return nullptr;
}

} // namespace

class ParticipantsAppTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void speechCheckExcludesSilentTrack();
    void analysisApproveUndoUnbindSolo();
    void manualParticipantKeptThroughAnalysis();
    void maybeAnalyzeGates();
    void removeNamedSpeakerWithoutRecord();

private:

    // People by name (people.json): created if missing (pid) / only looked up (idOf); a picker's choice; the name.
    PersonId pid(const QString& name) { return m_app->people()->ensure(name); }
    PersonId idOf(const QString& name) { m_app->people()->refresh(); return m_app->people()->idForName(name); }
    PersonChoice choice(const QString& name) { return m_app->people()->choiceFor(name); }
    QString nameOf(const PersonId& id) { m_app->people()->refresh(); return m_app->people()->displayName(id); }
    Meeting makeMeeting(bool withSilent);
    void writeTranscript(Meeting& m, const QVector<std::tuple<qint64, qint64, QString>>& rows);

    bool m_haveFfmpeg = false;
    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<app::AppController> m_app;
};

void ParticipantsAppTest::initTestCase()
{
    m_haveFfmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    qputenv("TANARA_CLOUD", "off");
}

void ParticipantsAppTest::init()
{
    if (!m_haveFfmpeg) QSKIP("nincs ffmpeg");
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    const QString f = QDir(m_home->path()).filePath("models/" + processing::VoiceModelRegistry::spec(kCamp)->fileName);
    QDir().mkpath(QFileInfo(f).absolutePath());
    QFile file(f);
    QVERIFY(file.open(QIODevice::WriteOnly));   // placeholder: the fake loader does not read it
    file.close();
    m_app = std::make_unique<app::AppController>();
    QVERIFY(m_app->store()->metadataDir().startsWith(m_home->path()));
    m_app->setVoiceEmbedderLoader(fakeLoader());
    m_app->settings()->setEnabledVoiceModels({kCamp});
    QVERIFY(m_app->voiceIdentificationAvailable());
    m_app->setUserSpeakerName(QStringLiteral("Ádám"));
}

void ParticipantsAppTest::cleanup()
{
    m_app.reset();
    m_home.reset();
}

// Microphone: 220 Hz at 0-8 and 16-24 s; loopback: 440 Hz at 8-16 s; (optionally) a silent third track.
Meeting ParticipantsAppTest::makeMeeting(bool withSilent)
{
    Meeting m = m_app->store()->createMeeting(QStringLiteral("Résztvevők"));
    const QDir dir(m.folder);
    [&] { QVERIFY(makeTrack(dir.filePath("track_mic.wav"), 220, "not(between(t,8,16))")); }();
    [&] { QVERIFY(makeTrack(dir.filePath("track_loop.wav"), 440, "between(t,8,16)")); }();
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
    if (withSilent) {
        [&] { QVERIFY(makeTrack(dir.filePath("track_quiet.wav"), 0, {})); }();
        add("quiet", TrackKind::Loopback, "track_quiet.wav");
    }
    m.durationMs = 24000;
    m_app->store()->saveMeeting(m);
    return m;
}

void ParticipantsAppTest::writeTranscript(Meeting& m, const QVector<std::tuple<qint64, qint64, QString>>& rows)
{
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

void ParticipantsAppTest::speechCheckExcludesSilentTrack()
{
    const Meeting m0 = makeMeeting(/*withSilent*/ true);
    QSignalSpy tracks(m_app.get(), &app::AppController::tracksChanged);
    QVERIFY(m_app->checkTrackSpeech(m0.id));
    QVERIFY(tracks.wait(30000));

    Meeting m = m_app->store()->load(m0.id);
    QCOMPARE(m.tracks.size(), 3);
    QVERIFY(m.tracks[0].included());
    QVERIFY(qAbs(m.tracks[0].speechRatio - 2.0 / 3.0) < 0.05);
    QVERIFY(m.tracks[1].included());
    QVERIFY(qAbs(m.tracks[1].speechRatio - 1.0 / 3.0) < 0.05);
    QCOMPARE(m.tracks[2].excludedReason, editing::trackspeech::kNoSpeech);
    QVERIFY(!m.tracks[2].active);
    QCOMPARE(m.tracks[2].speechRatio, 0.0);
    QCOMPARE(recording::MixdownPlan::fromMeeting(m).inputs.size(), 2);
    QVERIFY(!m_app->checkTrackSpeech(m0.id));   // already measured

    // "Include" puts it back into the mixdown (a remix starts).
    QSignalSpy mixed(m_app.get(), &app::AppController::mixdownUpdated);
    m_app->includeTrack(m0.id, "quiet");
    m = m_app->store()->load(m0.id);
    QVERIFY(m.tracks[2].included());
    QCOMPARE(recording::MixdownPlan::fromMeeting(m).inputs.size(), 3);
    QVERIFY(mixed.wait(30000));
    QVERIFY(mixed.last().at(1).toBool());

    // Manual exclusion.
    m_app->excludeTrack(m0.id, "loop");
    m = m_app->store()->load(m0.id);
    QCOMPARE(m.tracks[1].excludedReason, editing::trackspeech::kManual);
    QCOMPARE(recording::MixdownPlan::fromMeeting(m).inputs.size(), 2);
    QVERIFY(mixed.wait(30000));
}

void ParticipantsAppTest::analysisApproveUndoUnbindSolo()
{
    Meeting m = makeMeeting(/*withSilent*/ false);
    m_app->voiceprints()->addPrint(pid(QStringLiteral("Ádám")), print(0));
    m_app->voiceprints()->addPrint(pid(QStringLiteral("Béla")), print(1));

    QSignalSpy done(m_app.get(), &app::AppController::participantAnalysisFinished);
    QVERIFY(m_app->analyzeParticipants(m.id));
    QVERIFY(m_app->participantAnalysisRunning(m.id));
    QVERIFY(!m_app->analyzeParticipants(m.id));   // already running
    QVERIFY(done.wait(60000));
    QVERIFY(!m_app->participantAnalysisRunning(m.id));

    QVector<Participant> ps = m_app->participants(m.id);
    QCOMPARE(ps.size(), 2);
    const Participant* adam = byName(ps, pid(QStringLiteral("Ádám")));
    const Participant* bela = byName(ps, pid(QStringLiteral("Béla")));
    QVERIFY(adam && bela);
    QCOMPARE(adam->sides, QStringList({processing::participants::kSideMic}));
    QCOMPARE(bela->sides, QStringList({processing::participants::kSideLoopback}));
    QCOMPARE(processing::participants::participantGroup(*adam), ParticipantGroup::Sure);    // audio + own microphone
    QCOMPARE(processing::participants::participantGroup(*bela), ParticipantGroup::Doubt);   // audio only
    QVERIFY(QFile::exists(processing::ParticipantAnalysisFile::filePath(m.folder)));
    QVERIFY(m_app->participantApprovalPending(m.id));
    const QString adamId = adam->id, belaId = bela->id;

    // The transcript comes later: binding by time overlap.
    writeTranscript(m, {{1000, 7000, "Beszélő 1"}, {9000, 15000, "Beszélő 2"}, {17000, 23000, "Beszélő 1"}});
    QSignalSpy changed(m_app.get(), &app::AppController::participantsChanged);
    QVERIFY(m_app->bindRawSpeakers(m.id));
    QCOMPARE(changed.count(), 1);
    ps = m_app->participants(m.id);
    QCOMPARE(byName(ps, pid("Ádám"))->rawSpeakerIds, QStringList({"Beszélő 1"}));
    QCOMPARE(byName(ps, pid("Béla"))->rawSpeakerIds, QStringList({"Beszélő 2"}));

    // Approval: the raw speakers to the names, in one undo step.
    QVERIFY(m_app->approveParticipants(m.id, ps));
    m = m_app->store()->load(m.id);
    QCOMPARE(nameOf(m.speakerMap.value("Beszélő 1")), QStringLiteral("Ádám"));
    QCOMPARE(nameOf(m.speakerMap.value("Beszélő 2")), QStringLiteral("Béla"));
    QVERIFY(m.approval.has_value());
    QCOMPARE(m.approval->candidates.size(), 2);
    QVERIFY(!m_app->participantApprovalPending(m.id));
    editing::SpeakerEditSession* ed = m_app->editSession(m.id);
    QVERIFY(ed && ed->canUndo());
    ed->undo();
    m = m_app->store()->load(m.id);
    QVERIFY(m.speakerMap.isEmpty());
    QVERIFY(!ed->canUndo());   // it was a single step
    ed->redo();
    QCOMPARE(m_app->store()->load(m.id).speakerMap.size(), 2);

    // "Was not there": Béla's lines become anonymous, its marking is deleted.
    QVERIFY(m_app->unbindParticipant(m.id, belaId));
    m = m_app->store()->load(m.id);
    QVERIFY(!m.speakerMap.contains("Beszélő 2"));
    QCOMPARE(nameOf(m.speakerMap.value("Beszélő 1")), QStringLiteral("Ádám"));
    QVERIFY(!byName(m.participants, pid("Béla"))->approved);
    QVERIFY(byName(m.participants, pid("Ádám"))->approved);

    // "Only I spoke": every raw speaker to the own name (merged into one speaker).
    QVERIFY(m_app->setSoloMeeting(m.id));
    m = m_app->store()->load(m.id);
    QVERIFY(m.approval && m.approval->solo);
    int named = 0;
    for (const editing::EditorSpeaker& s : ed->speakers())
        if (s.utteranceCount > 0) {
            QCOMPARE(nameOf(s.personId), QStringLiteral("Ádám"));
            ++named;
        }
    QCOMPARE(named, 1);
    QVERIFY(!m_app->analyzeParticipants(m.id));   // no analysis after "only me"
    Q_UNUSED(adamId);
}

void ParticipantsAppTest::manualParticipantKeptThroughAnalysis()
{
    const Meeting m = makeMeeting(/*withSilent*/ false);
    m_app->voiceprints()->addPrint(pid(QStringLiteral("Béla")), print(1));
    const QString id = m_app->addParticipant(m.id, choice(QStringLiteral("Cecília")));
    QVERIFY(id.startsWith("m-"));
    QCOMPARE(m_app->addParticipant(m.id, choice(QStringLiteral("cecília"))), id);   // already there
    QSignalSpy done(m_app.get(), &app::AppController::participantAnalysisFinished);
    QVERIFY(m_app->analyzeParticipants(m.id));
    QVERIFY(done.wait(60000));
    const QVector<Participant> ps = m_app->participants(m.id);
    QCOMPARE(ps.size(), 3);   // Béla, Cecília (manual), the unknown microphone voice
    const Participant* cili = byName(ps, pid(QStringLiteral("Cecília")));
    QVERIFY(cili);
    QCOMPARE(cili->id, id);
    QCOMPARE(cili->source, ParticipantSource::Manual);
    bool noPrint = false;
    for (const Evidence& e : cili->evidence)
        if (e.kind == EvidenceKind::Voice && evidenceText(e) == QStringLiteral("nincs lenyomata")) noPrint = true;
    QVERIFY(noPrint);
    QVERIFY(byName(ps, PersonId()));   // unknown voice (Ádám's voiceprint is not here)

    // "Skip": a decision without a binding.
    QVERIFY(m_app->skipApproval(m.id));
    QVERIFY(!m_app->participantApprovalPending(m.id));
    QVERIFY(m_app->store()->load(m.id).approval->skipped);
}

// The shared starter (maybeAnalyzeParticipants): from the raw tracks, without a mixdown; it
// does not restart on the same tracks, after a decision (approval), nor before the speech
// measurement (the end of the measurement starts it).
void ParticipantsAppTest::maybeAnalyzeGates()
{
    // A track without speech: the measurement first; the end of the measurement starts the analysis by itself.
    const Meeting s0 = makeMeeting(/*withSilent*/ true);
    QVERIFY(!m_app->maybeAnalyzeParticipants(s0.id));
    QVERIFY(!m_app->participantAnalysisRunning(s0.id));
    QSignalSpy started(m_app.get(), &app::AppController::participantAnalysisStarted);
    QSignalSpy finished(m_app.get(), &app::AppController::participantAnalysisFinished);
    QVERIFY(m_app->checkTrackSpeech(s0.id));
    QVERIFY(started.wait(30000));
    QCOMPARE(started.last().at(0).toString(), s0.id);
    QVERIFY(finished.count() > 0 || finished.wait(60000));
    QVERIFY(!m_app->participantAnalysisRunning(s0.id));
    QVERIFY(!m_app->maybeAnalyzeParticipants(s0.id));   // it already ran on these same tracks

    // Tracks with speech, no mixdown: it starts at once; during and after the run it does not restart.
    Meeting m = makeMeeting(/*withSilent*/ false);
    QVERIFY(m.mixdownFile.isEmpty());
    QVERIFY(!m_app->maybeAnalyzeParticipants(m.id));   // the tracks are still before measurement: the end of the measurement calls
    for (Track& t : m.tracks) t.speechRatio = 0.5;     // "measured" (the measurement bypassed)
    m_app->store()->saveMeeting(m);
    finished.clear();
    QVERIFY(m_app->maybeAnalyzeParticipants(m.id));
    QVERIFY(m_app->participantAnalysisRunning(m.id));
    QVERIFY(!m_app->maybeAnalyzeParticipants(m.id));
    QVERIFY(finished.wait(60000));
    QVERIFY(!m_app->maybeAnalyzeParticipants(m.id));
    QVERIFY(!m_app->participants(m.id).isEmpty());

    // After a decision it does not either.
    QVERIFY(m_app->skipApproval(m.id));
    QVERIFY(m_app->store()->load(m.id).approval.has_value());
    QVERIFY(!m_app->maybeAnalyzeParticipants(m.id));
}

// Without a participant record (the meeting got names before the analysis) the deletion goes
// by the speaker's key: its lines become anonymous, undoable; false for an anonymous / unknown key.
void ParticipantsAppTest::removeNamedSpeakerWithoutRecord()
{
    Meeting m = makeMeeting(/*withSilent*/ false);
    writeTranscript(m, {{0, 1000, QStringLiteral("Beszélő 1")}, {1000, 2000, QStringLiteral("Beszélő 2")}});
    m = m_app->store()->load(m.id);
    m.speakerMap.insert(QStringLiteral("Beszélő 1"), pid(QStringLiteral("Béla")));
    m_app->store()->saveMeeting(m);
    QVERIFY(m_app->participants(m.id).isEmpty());

    editing::SpeakerEditSession* ed = m_app->editSession(m.id);
    QVERIFY(ed);
    QString belaKey, anonKey;
    for (const editing::EditorSpeaker& s : ed->speakers()) {
        if (s.personId == pid(QStringLiteral("Béla"))) belaKey = s.key;
        else if (s.anonymous) anonKey = s.key;
    }
    QVERIFY(!belaKey.isEmpty());
    QVERIFY(!anonKey.isEmpty());
    QVERIFY(!m_app->removeParticipant(m.id, anonKey));                 // anonymous: nothing to do
    QVERIFY(!m_app->removeParticipant(m.id, QStringLiteral("nincs")));  // unknown
    QVERIFY(m_app->removeParticipant(m.id, belaKey));
    bool belaLeft = false;
    for (const editing::EditorSpeaker& s : ed->speakers()) belaLeft = belaLeft || s.personId == pid(QStringLiteral("Béla"));
    QVERIFY(!belaLeft);
    QVERIFY(m_app->meetingUndo(m.id)->canUndo());
    m_app->meetingUndo(m.id)->undo();
    bool belaBack = false;
    for (const editing::EditorSpeaker& s : ed->speakers()) belaBack = belaBack || s.personId == pid(QStringLiteral("Béla"));
    QVERIFY(belaBack);
}

QTEST_GUILESS_MAIN(ParticipantsAppTest)
#include "test_participants_app.moc"

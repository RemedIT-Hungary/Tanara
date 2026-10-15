// VoiceAnalysisService::useDiarization (D4): the local diarization's speakers become the participant
// analysis's clusters - candidates matched against the voiceprints, the sample re-pointed to the track's
// file (meeting time), bound to the transcript's raw speakers, the analysis file written (so the window
// analysis does not run again on these tracks); a hand-bound speaker (no centroid) and a decided meeting
// are skipped. A fake voice model; no audio is decoded (no ffmpeg needed). ISOLATED TANARA_HOME.
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/processing/analysis/VoiceAnalysisService.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace tanara;
using processing::VoiceAnalysisService;

namespace {

const QString kCamp = QStringLiteral("campplus");

QVector<float> hot(int i)
{
    QVector<float> v(processing::VoiceModelRegistry::spec(kCamp)->dim, 0.0f);
    v[i] = 1.0f;
    return v;
}

providers::DiarizedSpeaker speaker(const QString& track, int index, int hotDim, const QString& sample)
{
    providers::DiarizedSpeaker sp;
    sp.trackId = track;
    sp.index = index;
    sp.totalMs = 3000;
    sp.centroid.trackId = track;
    sp.centroid.windows = 4;
    sp.centroid.sampleRef = sample;
    if (hotDim >= 0) {
        providers::VoiceVector v;
        v.vector = hot(hotDim);
        v.dim = int(v.vector.size());
        v.modelId = kCamp;
        sp.centroid.vectors << v;
    }
    return sp;
}

} // namespace

class TestDiarizationParticipants : public QObject {
    Q_OBJECT
private slots:
    void diarizedSpeakersBecomeCandidates()
    {
        QTemporaryDir home;
        qputenv("TANARA_HOME", home.path().toUtf8());
        const QDir dir(home.path());
        const QString model = dir.filePath("models/" + processing::VoiceModelRegistry::spec(kCamp)->fileName);
        QDir().mkpath(QFileInfo(model).absolutePath());
        { QFile f(model); QVERIFY(f.open(QIODevice::WriteOnly)); }   // placeholder for the fake loader
        QDir().mkpath(dir.filePath("rec"));
        QDir().mkpath(dir.filePath("meta"));
        library::MeetingRepository store(dir.filePath("rec"), dir.filePath("meta"));
        library::PeopleStore people(dir.filePath("meta/people.json"));
        library::VoiceprintStore prints(dir.filePath("meta/voiceprints.json"));
        library::MeetingJobTracker jobs(&store);

        const PersonId bela = people.ensure(QStringLiteral("Béla"));
        Voiceprint vp;
        vp.model = kCamp;
        vp.embedding = hot(0);
        vp.dim = vp.embedding.size();
        vp.sampleRef = QStringLiteral("x.wav#0-1000");
        prints.addPrint(bela, vp);

        VoiceAnalysisService::Deps deps;
        deps.store = &store;
        deps.voiceprints = &prints;
        deps.people = &people;
        deps.jobs = &jobs;
        deps.modelsDir = dir.filePath("models");
        deps.enabledModels = []() { return QStringList{kCamp}; };
        deps.self = [&people]() { SelfPerson me; me.id = people.selfId(); me.name = QStringLiteral("Ádám"); return me; };
        deps.transcriptLines = [](const QString& folder) { return editing::speakeredit::loadTranscriptLines(folder); };
        deps.transcriptStamp = [](const QString&, const QString&) { return QStringLiteral("t1"); };
        deps.speechCheckPending = [](const Meeting&) { return false; };
        VoiceAnalysisService svc(deps);
        svc.setLoader([](const processing::VoiceModelSpec&, const QString&, QString*) -> processing::PcmEmbedder {
            return [](const QVector<float>&) { return hot(0); };
        });
        svc.applyModels();
        QVERIFY(svc.usable());

        Meeting m = store.createMeeting(QStringLiteral("Diarizált"));
        for (const auto& [id, kind] : {std::pair{QStringLiteral("mic"), TrackKind::Mic},
                                       std::pair{QStringLiteral("loop"), TrackKind::Loopback}}) {
            Track t;
            t.id = id;
            t.kind = kind;
            t.file = QStringLiteral("track_%1.ogg").arg(id);
            m.tracks << t;
            QFile f(QDir(m.folder).filePath(t.file));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        store.saveMeeting(m);
        // The merged transcript: "Beszélő 1" 0-3 s (the loopback's speaker 0), "Beszélő 2" 3-6 s (speaker 1).
        MergedTranscript t;
        for (int i = 0; i < 20; ++i) {
            TranscriptToken tok;
            tok.text = QStringLiteral(" x");
            tok.speaker = i < 10 ? QStringLiteral("Beszélő 1") : QStringLiteral("Beszélő 2");
            tok.startMs = i * 300;
            tok.endMs = tok.startMs + 250;
            t.tokens << tok;
        }
        QVERIFY(library::TranscriptRepository::writeTokens(m.folder, t));
        QVERIFY(library::TranscriptRepository::writeSegments(m.folder, t.segments()));
        m = store.load(m.id);
        m.hasTranscript = true;
        store.saveMeeting(m);

        providers::DiarizeResult r;
        r.embeddingModelIds = {kCamp};
        for (int s = 0; s < 2; ++s) {
            providers::SpeakerTurn turn;
            turn.trackId = QStringLiteral("loop");
            turn.speakerIndex = s;
            turn.startMs = s * 3000;
            turn.endMs = turn.startMs + 3000;
            r.turns << turn;
        }
        r.speakers << speaker("loop", 0, 0, QStringLiteral("/abs/path/track_loop.ogg#0-3000"))
                   << speaker("loop", 1, 5, QStringLiteral("/abs/path/track_loop.ogg#3000-6000"))
                   << speaker("mic", 0, -1, QString());   // a hand-bound track: skipped

        QSignalSpy changed(&svc, &VoiceAnalysisService::participantsChanged);
        QVERIFY(svc.useDiarization(m.id, r));
        QCOMPARE(changed.count(), 1);
        const Meeting after = store.load(m.id);
        QCOMPARE(after.participants.size(), 2);
        const Participant* known = nullptr;
        const Participant* anon = nullptr;
        for (const Participant& p : after.participants) (p.personId == bela ? known : anon) = &p;
        QVERIFY(known && anon);
        QCOMPARE(known->sampleTrackId, QStringLiteral("loop"));
        QCOMPARE(known->sampleRef, QStringLiteral("track_loop.ogg#0-3000"));
        QVERIFY(known->rawSpeakerIds.contains(QStringLiteral("Beszélő 1")));
        QVERIFY(anon->personId.isNull());
        QCOMPARE(anon->sampleRef, QStringLiteral("track_loop.ogg#3000-6000"));
        QVERIFY(anon->rawSpeakerIds.contains(QStringLiteral("Beszélő 2")));
        QVERIFY(known->sides.contains(processing::participants::kSideLoopback));
        const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(m.folder);
        QCOMPARE(file.clusters.size(), 2);
        QCOMPARE(file.modelIds, QStringList{kCamp});
        // The window analysis would not run again on these tracks.
        QVERIFY(!svc.maybeAnalyzeParticipants(m.id));

        // Nothing usable / a decision already made: false.
        providers::DiarizeResult onlyBound;
        onlyBound.speakers << speaker("mic", 0, -1, QString());
        QVERIFY(!svc.useDiarization(m.id, onlyBound));
        Meeting decided = store.load(m.id);
        decided.approval = ParticipantApproval{};
        store.saveMeeting(decided);
        QVERIFY(!svc.useDiarization(m.id, r));
        QVERIFY(!svc.useDiarization(QStringLiteral("nincs"), r));
        svc.shutdown();
    }
};

QTEST_GUILESS_MAIN(TestDiarizationParticipants)
#include "test_diarization_participants.moc"

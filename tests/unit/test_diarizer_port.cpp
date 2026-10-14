// The IDiarizer port (D2): the request / result value types, the capability registration and the
// scripted FakeDiarizer (job semantics, manual binding, cancel). No model, no audio, no network.
#include <QSignalSpy>
#include <QtTest>

#include "fake_diarizer.h"
#include "tanara/providers/core/ProviderRegistry.h"

using namespace tanara;
using providers::DiarizeRequest;
using providers::DiarizeResult;
using providers::DiarizeTrack;
using providers::SpeakerTurn;
using testing::FakeDiarizer;

class TestDiarizerPort : public QObject {
    Q_OBJECT

    static DiarizeTrack fileTrack(const QString& id, qint64 offsetMs = 0)
    {
        DiarizeTrack t;
        t.trackId = id;
        t.path = QStringLiteral("/nowhere/%1.ogg").arg(id);
        t.startOffsetMs = offsetMs;
        return t;
    }

private slots:
    void capabilityKindIsRegisteredAndNamed()
    {
        QCOMPARE(providers::ProviderTraits<providers::IDiarizer>::kind, providers::CapabilityKind::Diarization);
        QCOMPARE(providers::capabilityName(providers::CapabilityKind::Diarization), QStringLiteral("diarization"));
        // Nothing registers a real diarizer yet: the registry knows no provider of this capability.
        providers::ProviderRegistry reg;
        QVERIFY(reg.descriptors(providers::CapabilityKind::Diarization).isEmpty());
    }

    void requestDefaultsAndEmptiness()
    {
        DiarizeRequest req;
        QVERIFY(req.isEmpty());
        QCOMPARE(req.numClusters, -1);
        QVERIFY(req.threshold < 0);
        QVERIFY(req.minDurationOn < 0);
        QVERIFY(req.minDurationOff < 0);
        QVERIFY(req.segmentationModelId.isEmpty());
        QVERIFY(req.knownSpeakers.isEmpty());

        DiarizeTrack t;
        t.trackId = QStringLiteral("mic");
        QVERIFY(!t.hasInput());
        QVERIFY(t.knownPerson.isNull());           // never set from the track kind
        req.tracks.append(t);
        QVERIFY(req.isEmpty());                    // a track without a file or PCM is no input
        req.tracks[0].pcm = QVector<float>(160, 0.0f);
        QVERIFY(!req.isEmpty());
        req.tracks[0].pcm.clear();
        req.tracks[0].path = QStringLiteral("a.wav");
        QVERIFY(!req.isEmpty());
    }

    void resultLookups()
    {
        DiarizeResult r;
        SpeakerTurn a; a.trackId = QStringLiteral("t1"); a.speakerIndex = 0; a.startMs = 0; a.endMs = 500;
        SpeakerTurn b; b.trackId = QStringLiteral("t2"); b.speakerIndex = 1; b.startMs = 100; b.endMs = 900; b.overlap = true;
        r.turns = {a, b};
        providers::DiarizedSpeaker s;
        s.trackId = QStringLiteral("t2");
        s.index = 1;
        s.totalMs = 800;
        r.speakers = {s};
        QCOMPARE(r.turnsOf(QStringLiteral("t1")).size(), 1);
        QCOMPARE(r.turnsOf(QStringLiteral("t2")).first().durationMs(), qint64(800));
        QVERIFY(r.turnsOf(QStringLiteral("zz")).isEmpty());
        QVERIFY(r.speaker(QStringLiteral("t2"), 1) != nullptr);
        QCOMPARE(r.speaker(QStringLiteral("t2"), 1)->totalMs, qint64(800));
        QVERIFY(r.speaker(QStringLiteral("t2"), 0) == nullptr);
    }

    void fakeCompletesInTheNextTurnAndClustersUnboundTracks()
    {
        FakeDiarizer fake;
        DiarizeRequest req;
        req.tracks = {fileTrack(QStringLiteral("loop"), 5000)};
        providers::DiarizeJob* job = fake.diarize(req);
        QSignalSpy finished(job, &providers::CapabilityJob::finished);
        QCOMPARE(finished.count(), 0);              // never synchronously
        QVERIFY(finished.wait(2000));
        QVERIFY(job->isDone());
        const DiarizeResult& r = job->result();
        QCOMPARE(r.turns.size(), 2);
        QCOMPARE(r.turns.at(0).startMs, qint64(5000));   // meeting time: the track offset is added
        QCOMPARE(r.turns.at(1).startMs, qint64(6000));
        QCOMPARE(r.speakers.size(), 2);
        QVERIFY(r.speakers.at(0).person.isNull());       // clustered speakers are anonymous
        // The centroid is IdentifyProbe-compatible: it can go straight into an IdentifyRequest.
        providers::IdentifyRequest idReq;
        idReq.probes.append(r.speakers.at(0).centroid);
        QCOMPARE(idReq.probes.first().trackId, QStringLiteral("loop"));
        QCOMPARE(idReq.probes.first().vectors.first().dim, 2);
        QCOMPARE(fake.requests.size(), 1);
    }

    void aManuallyBoundTrackIsASingleSpeakerWithThePerson()
    {
        FakeDiarizer fake;
        DiarizeRequest req;
        DiarizeTrack bound = fileTrack(QStringLiteral("mic"));
        bound.knownPerson = PersonId::fromString(QStringLiteral("p-1"));
        req.tracks = {bound, fileTrack(QStringLiteral("loop"))};
        providers::DiarizeJob* job = fake.diarize(req);
        QSignalSpy finished(job, &providers::CapabilityJob::finished);
        QVERIFY(finished.wait(2000));
        const DiarizeResult& r = job->result();
        QCOMPARE(r.turnsOf(QStringLiteral("mic")).size(), 1);
        QCOMPARE(r.speaker(QStringLiteral("mic"), 0)->person, bound.knownPerson);
        QCOMPARE(r.turnsOf(QStringLiteral("loop")).size(), 2);          // the unbound track is clustered
        QVERIFY(r.speaker(QStringLiteral("loop"), 0)->person.isNull());
        QVERIFY(r.speaker(QStringLiteral("loop"), 1)->person.isNull());
    }

    void emptyRequestFailsAsInput()
    {
        FakeDiarizer fake;
        providers::DiarizeJob* job = fake.diarize(DiarizeRequest());
        QSignalSpy failed(job, &providers::CapabilityJob::failed);
        QSignalSpy finished(job, &providers::CapabilityJob::finished);
        QVERIFY(failed.wait(2000));
        QCOMPARE(finished.count(), 0);
        QCOMPARE(job->error().kind, Error::Kind::Input);
    }

    void cancelStopsTheJobAndNoSignalFollows()
    {
        FakeDiarizer fake;
        fake.manual = true;
        DiarizeRequest req;
        req.tracks = {fileTrack(QStringLiteral("loop"))};
        providers::DiarizeJob* job = fake.diarize(req);
        QSignalSpy finished(job, &providers::CapabilityJob::finished);
        QSignalSpy failed(job, &providers::CapabilityJob::failed);
        QSignalSpy progress(job, &providers::CapabilityJob::progress);
        emit job->progress(1, 4);
        QCOMPARE(progress.count(), 1);
        job->cancel();
        QVERIFY(job->isCancelled());
        QVERIFY(static_cast<testing::FakeDiarizeJob*>(job)->cancelRequested);
        job->cancel();                                // idempotent
        job->completeLater(FakeDiarizer::defaultResult(req));   // the work finishing late changes nothing
        QTest::qWait(50);
        QCOMPARE(finished.count(), 0);
        QCOMPARE(failed.count(), 0);
    }

    void scriptedResultAndOverlapFlag()
    {
        FakeDiarizer fake;
        fake.script = [](const DiarizeRequest& rq) {
            DiarizeResult r;
            SpeakerTurn t;
            t.trackId = rq.tracks.first().trackId;
            t.startMs = 0;
            t.endMs = 400;
            t.overlap = true;
            r.turns.append(t);
            return r;
        };
        DiarizeRequest req;
        req.tracks = {fileTrack(QStringLiteral("x"))};
        req.numClusters = 3;
        providers::DiarizeJob* job = fake.diarize(req);
        QSignalSpy finished(job, &providers::CapabilityJob::finished);
        QVERIFY(finished.wait(2000));
        QVERIFY(job->result().turns.first().overlap);
        QCOMPARE(fake.requests.first().numClusters, 3);
    }
};

QTEST_GUILESS_MAIN(TestDiarizerPort)
#include "test_diarizer_port.moc"

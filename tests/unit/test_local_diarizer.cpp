// LocalDiarizer (D3 stage: segmentation only) over a FAKE segmentation model: the job runs on a worker
// thread, maps the stitched local speakers to per-track speaker indices, shifts the times by the track's
// startOffsetMs, treats a hand-bound track as one speaker, reports progress, honours cancel(), and
// fails cleanly without a model / without input. Also: registerInto registers the diarizer only when
// a segmentation model file exists.
#include "tanara/processing/ProcessingProviders.h"
#include "tanara/processing/diarization/LocalDiarizer.h"
#include "tanara/processing/voiceid/VoiceModelDownloader.h"
#include "tanara/providers/core/ProviderRegistry.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace tanara;
using namespace tanara::processing;
using namespace tanara::processing::segmentation;

namespace {

constexpr int kSr = 16000;

// Codes: 0.1 = speaker A, 0.2 = speaker B (see test_pyannote_segmenter).
class FakeModel final : public SegmentationModel {
public:
    const ModelInfo& info() const override { return m_info; }
    bool run(const float* samples, int count, QVector<float>* out, QString*) override
    {
        const int F = m_info.framesPerWindow, C = m_info.numClasses;
        out->fill(-10.0f, qsizetype(count) * F * C);
        for (int i = 0; i < count; ++i)
            for (int f = 0; f < F; ++f) {
                const float v = samples[qsizetype(i) * m_info.windowSize + f * m_info.receptiveFieldShift
                                        + m_info.receptiveFieldSize / 2];
                const int code = int(std::lround(v * 10));   // 0 silence, 1 -> {0}, 2 -> {1}
                (*out)[(qsizetype(i) * F + f) * C + (code == 1 ? 1 : code == 2 ? 2 : 0)] = 0.0f;
            }
        return true;
    }

private:
    ModelInfo m_info;
};

QVector<float> twoSpeakers(double seconds = 20.0)
{
    QVector<float> pcm(int(seconds * kSr), 0.0f);
    for (int i = 1 * kSr; i < 6 * kSr; ++i) pcm[i] = 0.1f;     // A 1-6 s
    for (int i = 7 * kSr; i < 12 * kSr; ++i) pcm[i] = 0.2f;    // B 7-12 s
    for (int i = 13 * kSr; i < 18 * kSr; ++i) pcm[i] = 0.1f;   // A 13-18 s
    return pcm;
}

LocalDiarizer::SegmenterLoader fakeLoader(QStringList* asked = nullptr)
{
    return [asked](const QString& id, const SegmenterOptions& opts, QString*) {
        if (asked) asked->append(id);
        return std::make_unique<PyannoteSegmenter>(std::make_unique<FakeModel>(), opts, QStringLiteral("fake"));
    };
}

providers::DiarizeResult runJob(providers::DiarizeJob* job, QList<QPair<qint64, qint64>>* progress = nullptr)
{
    QSignalSpy done(job, &providers::CapabilityJob::finished);
    QSignalSpy failed(job, &providers::CapabilityJob::failed);
    QObject::connect(job, &providers::CapabilityJob::progress, job, [progress](qint64 d, qint64 t) {
        if (progress) progress->append({d, t});
    });
    if (!done.wait(10000)) {
        qWarning() << "job did not finish; failed:" << failed.count();
        return {};
    }
    return job->result();
}

} // namespace

class TestLocalDiarizer : public QObject {
    Q_OBJECT
private slots:
    void segmentsATrackIntoSpeakerTurns()
    {
        QStringList asked;
        LocalDiarizer d(fakeLoader(&asked), nullptr);
        providers::DiarizeRequest req;
        providers::DiarizeTrack t;
        t.trackId = QStringLiteral("remote");
        t.pcm = twoSpeakers();
        t.startOffsetMs = 500;
        req.tracks = {t};
        req.segmentationModelId = QStringLiteral("pyannote-segmentation-3.0-int8");
        QList<QPair<qint64, qint64>> progress;
        const providers::DiarizeResult r = runJob(d.diarize(req), &progress);
        QCOMPARE(asked, QStringList{QStringLiteral("pyannote-segmentation-3.0-int8")});

        QCOMPARE(r.turns.size(), 3);
        QCOMPARE(r.turns[0].speakerIndex, 0);   // first appearance = index 0
        QCOMPARE(r.turns[1].speakerIndex, 1);
        QCOMPARE(r.turns[2].speakerIndex, 0);
        QVERIFY(qAbs(r.turns[0].startMs - 1500) <= 40);   // 1 s + the 500 ms offset
        QVERIFY(qAbs(r.turns[2].endMs - 18500) <= 40);
        for (const providers::SpeakerTurn& x : r.turns) {
            QCOMPARE(x.trackId, QStringLiteral("remote"));
            QVERIFY(!x.overlap);
        }
        QCOMPARE(r.speakers.size(), 2);
        QCOMPARE(r.speakers[0].index, 0);
        QVERIFY(r.speakers[0].centroid.vectors.isEmpty());   // D4 fills the centroids
        QVERIFY(qAbs(r.speakers[0].totalMs - 10000) <= 80);
        QVERIFY(r.speakers[0].person.isNull());

        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last().first, progress.last().second);
    }

    void boundTrackIsOneSpeaker()
    {
        LocalDiarizer d(fakeLoader(), nullptr);
        providers::DiarizeRequest req;
        providers::DiarizeTrack a, b;
        a.trackId = QStringLiteral("a");
        a.pcm = twoSpeakers();
        b.trackId = QStringLiteral("b");
        b.pcm = twoSpeakers();
        b.knownPerson = PersonId::generate();
        req.tracks = {a, b};
        req.minDurationOff = 1.5;   // bridges b's 1 s gaps: one turn 1-18 s
        const providers::DiarizeResult r = runJob(d.diarize(req));
        const QVector<providers::SpeakerTurn> tb = r.turnsOf(QStringLiteral("b"));
        QCOMPARE(tb.size(), 1);
        QCOMPARE(tb[0].speakerIndex, 0);
        const providers::DiarizedSpeaker* sb = r.speaker(QStringLiteral("b"), 0);
        QVERIFY(sb);
        QCOMPARE(sb->person, b.knownPerson);
        QVERIFY(!r.speaker(QStringLiteral("b"), 1));
        // Track a is still two speakers (A's 1 s gaps are not A's: B talks in between).
        QCOMPARE(r.turnsOf(QStringLiteral("a")).size(), 3);
        QVERIFY(r.speaker(QStringLiteral("a"), 1));
    }

    void failuresAndCancel()
    {
        // No input.
        LocalDiarizer d(fakeLoader(), nullptr);
        auto* j1 = d.diarize({});
        QSignalSpy f1(j1, &providers::CapabilityJob::failed);
        QVERIFY(f1.wait(5000));
        QCOMPARE(j1->error().code, QStringLiteral("no_input"));

        // No model.
        LocalDiarizer none([](const QString&, const SegmenterOptions&, QString* e) {
            *e = QStringLiteral("missing");
            return std::unique_ptr<PyannoteSegmenter>();
        }, nullptr);
        providers::DiarizeRequest req;
        providers::DiarizeTrack t;
        t.trackId = QStringLiteral("x");
        t.pcm = twoSpeakers();
        req.tracks = {t};
        auto* j2 = none.diarize(req);
        QSignalSpy f2(j2, &providers::CapabilityJob::failed);
        QVERIFY(f2.wait(5000));
        QCOMPARE(j2->error().code, QStringLiteral("no_segmentation_model"));
        QCOMPARE(j2->error().kind, Error::Kind::Unsupported);

        // A path without a media tool.
        providers::DiarizeRequest byPath;
        providers::DiarizeTrack p;
        p.trackId = QStringLiteral("p");
        p.path = QStringLiteral("/nonexistent.ogg");
        byPath.tracks = {p};
        auto* j3 = d.diarize(byPath);
        QSignalSpy f3(j3, &providers::CapabilityJob::failed);
        QVERIFY(f3.wait(5000));
        QCOMPARE(j3->error().code, QStringLiteral("no_media_tool"));

        // Cancel: neither signal follows; destroying the job reaps the worker.
        t.pcm = twoSpeakers(600.0);   // 10 minutes: long enough to cancel mid-way
        req.tracks = {t};
        auto* j4 = d.diarize(req);
        QSignalSpy done(j4, &providers::CapabilityJob::finished);
        QSignalSpy failed(j4, &providers::CapabilityJob::failed);
        j4->cancel();
        QTest::qWait(300);
        QCOMPARE(done.count(), 0);
        QCOMPARE(failed.count(), 0);
        delete j4;
    }

    void registeredOnlyWithAModelFile()
    {
        QTemporaryDir dir;
        auto registered = [&]() {
            providers::ProviderRegistry reg;
            ProcessingProviderDeps deps;
            deps.modelsDir = dir.path();
            registerInto(reg, deps);
            return reg.has(providers::CapabilityKind::Diarization, LocalDiarizer::Id);
        };
        QVERIFY(!registered());
        const VoiceModelSpec spec = VoiceModelRegistry::segmentation().first();
        const QString path = VoiceModelDownloader::targetPath(spec, dir.path());
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(int(spec.sizeBytes), '\0'));   // right size: usable until the SHA check at open
        f.close();
        QVERIFY(registered());
    }
};

QTEST_GUILESS_MAIN(TestLocalDiarizer)
#include "test_local_diarizer.moc"

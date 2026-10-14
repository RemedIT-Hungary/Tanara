// PyannoteSegmenter: the windowing driver over a FAKE segmentation model (synthetic logits from a coded
// signal: the "audio" carries which speakers talk, the fake model turns it into powerset classes with a
// different local-speaker permutation in every window), the integrity rule of open() (a file that does
// not match its pinned SHA-256 is refused unless the user accepted it), and - only when the real pyannote
// model file is present (TANARA_TEST_SEGMENTATION_MODEL, else ~/.tanara/models/..., read-only) and the
// build has ONNX Runtime - the ORT loading path. Otherwise that case is skipped.
#include "tanara/processing/diarization/PyannoteSegmenter.h"
#include "tanara/processing/voiceid/VoiceModelDownloader.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace tanara::processing;
using namespace tanara::processing::segmentation;

namespace {

constexpr int kSr = 16000;

// Codes in the signal: 0 silence, 0.1 speaker A, 0.2 speaker B, 0.3 both.
void fill(QVector<float>& pcm, double from, double to, float code)
{
    for (qint64 i = qint64(from * kSr); i < qint64(to * kSr) && i < pcm.size(); ++i) pcm[i] = code;
}

// The coded test signal: A 1-8 s, B 8.5-15 s (A joins 14-15 s), A 16-22.5 s, B 23-38 s; 40 s long.
// No silence of a speaker is longer than a window, so stitching keeps one id per speaker.
QVector<float> codedSignal()
{
    QVector<float> pcm(40 * kSr, 0.0f);
    fill(pcm, 1.0, 8.0, 0.1f);
    fill(pcm, 8.5, 15.0, 0.2f);
    fill(pcm, 14.0, 15.0, 0.3f);
    fill(pcm, 16.0, 22.5, 0.1f);
    fill(pcm, 23.0, 38.0, 0.2f);
    return pcm;
}

class FakeModel final : public SegmentationModel {
public:
    const ModelInfo& info() const override { return m_info; }
    bool run(const float* samples, int count, QVector<float>* out, QString*) override
    {
        const int F = m_info.framesPerWindow, C = m_info.numClasses;
        out->fill(-10.0f, qsizetype(count) * F * C);
        const QVector<QVector<int>> ps = powersetClasses(3, 2);
        for (int i = 0; i < count; ++i, ++m_windows) {
            // Local slot of A and B in this window: rotates, so stitching has work to do.
            const int a = m_windows % 3, b = (m_windows + 1) % 3;
            for (int f = 0; f < F; ++f) {
                const float v = samples[qsizetype(i) * m_info.windowSize + f * m_info.receptiveFieldShift
                                        + m_info.receptiveFieldSize / 2];
                const int code = int(std::lround(v * 10));
                QVector<int> set;
                if (code == 1 || code == 3) set.append(a);
                if (code == 2 || code == 3) set.append(b);
                std::sort(set.begin(), set.end());
                (*out)[(qsizetype(i) * F + f) * C + int(ps.indexOf(set))] = 0.0f;
            }
        }
        ++runs;
        return true;
    }
    int runs = 0;

private:
    ModelInfo m_info;
    int m_windows = 0;
};

bool near(double a, double b, double tol = 0.05) { return std::abs(a - b) <= tol; }

QString realModelPath()
{
    const QString env = qEnvironmentVariable("TANARA_TEST_SEGMENTATION_MODEL");
    if (!env.isEmpty()) return env;
    return QDir::home().filePath(QStringLiteral(".tanara/models/pyannote-segmentation-3.0/model.onnx"));
}

} // namespace

class TestPyannoteSegmenter : public QObject {
    Q_OBJECT
private slots:
    void segmentsTheCodedSignal()
    {
        PyannoteSegmenter seg(std::make_unique<FakeModel>(), {}, QStringLiteral("fake"));
        QVector<int> progress;
        const auto r = seg.segment(codedSignal(), [&](int done, int total) {
            QCOMPARE(total, seg.windowCount(40 * kSr));
            progress.append(done);
        });
        QVERIFY(r.has_value());
        QCOMPARE(r->modelId, QStringLiteral("fake"));
        QCOMPARE(r->windows.size(), 32);   // 31 full windows (0 .. 29.9 s) + 1 zero-padded tail
        QCOMPARE(progress.last(), 32);
        QVERIFY(std::is_sorted(progress.begin(), progress.end()));
        QCOMPARE(r->windows[0].classes.size(), 589);
        QCOMPARE(r->windowShift, 15930);
        QCOMPARE(r->windows[3].startFrame, 3 * 59);

        // Expected: A 1-8, B 8.5-15, A 14-15 (overlap), A 16-22.5, B 23-38; A and B keep one id each.
        const QVector<Turn>& t = r->turns;
        QCOMPARE(t.size(), 5);
        const int A = t[0].speaker, B = t[1].speaker;
        QVERIFY(A != B);
        QVERIFY(near(t[0].start, 1.0) && near(t[0].end, 8.0));
        QVERIFY(near(t[1].start, 8.5) && near(t[1].end, 15.0) && t[1].overlap);
        QCOMPARE(t[2].speaker, A);
        QVERIFY(near(t[2].start, 14.0) && near(t[2].end, 15.0) && t[2].overlap);
        QCOMPARE(t[3].speaker, A);
        QVERIFY(near(t[3].start, 16.0) && near(t[3].end, 22.5) && !t[3].overlap);
        QCOMPARE(t[4].speaker, B);
        QVERIFY(near(t[4].start, 23.0) && near(t[4].end, 38.0));
        QCOMPARE(r->numStitched, 2);

        // Per-window local turns: window 0 covers 0-10 s, A in its slot 0.
        const QVector<LocalTurn> lt = r->windowTurns(0);
        QCOMPARE(lt.size(), 2);
        QCOMPARE(lt[0].localSpeaker, 0);
        QVERIFY(near(lt[0].start, 1.0) && near(lt[0].end, 8.0));
        QCOMPARE(r->windows[0].stitched[0], A);
    }

    void batchAndShiftDoNotChangeTheTurns()
    {
        SegmenterOptions batched;
        batched.batchSize = 4;
        auto* model = new FakeModel;
        PyannoteSegmenter seg(std::unique_ptr<SegmentationModel>(model), batched);
        const auto r = seg.segment(codedSignal());
        QVERIFY(r.has_value());
        QCOMPARE(model->runs, 8);   // 32 windows in batches of 4
        QCOMPARE(r->turns.size(), 5);

        SegmenterOptions coarse;
        coarse.windowShiftRatio = 0.5;
        PyannoteSegmenter seg2(std::make_unique<FakeModel>(), coarse);
        const auto r2 = seg2.segment(codedSignal());
        QVERIFY(r2.has_value());
        QCOMPARE(r2->windows.size(), 8);   // shift 4.995 s: 7 full + 1 padded
        QCOMPARE(r2->turns.size(), 5);
        for (int i = 0; i < 5; ++i) {
            QVERIFY(near(r2->turns[i].start, r->turns[i].start));
            QVERIFY(near(r2->turns[i].end, r->turns[i].end));
        }
    }

    void minDurationOptionsApply()
    {
        SegmenterOptions o;
        o.minDurationOff = 1.5;   // bridges the 1 s gap of A (15-16 s)
        o.minDurationOn = 8.0;    // keeps only the long turns
        PyannoteSegmenter seg(std::make_unique<FakeModel>(), o);
        const auto r = seg.segment(codedSignal());
        QVERIFY(r.has_value());
        QCOMPARE(r->turns.size(), 2);   // A 14-22.5 (8.5 s), B 23-38 (15 s)
        QVERIFY(near(r->turns[0].start, 14.0) && near(r->turns[0].end, 22.5));
    }

    void emptyAndCancelled()
    {
        PyannoteSegmenter seg(std::make_unique<FakeModel>());
        const auto e = seg.segment({});
        QVERIFY(e.has_value());
        QVERIFY(e->windows.isEmpty() && e->turns.isEmpty());

        std::atomic<bool> cancel{true};
        QString err;
        QVERIFY(!seg.segment(codedSignal(), {}, &cancel, &err).has_value());
        QCOMPARE(err, QStringLiteral("cancelled"));

        PyannoteSegmenter none(nullptr);
        QVERIFY(!none.segment(codedSignal(), {}, nullptr, &err).has_value());
    }

    void shortAudioIsOnePaddedWindow()
    {
        QVector<float> pcm(3 * kSr, 0.0f);
        fill(pcm, 0.5, 2.5, 0.2f);
        PyannoteSegmenter seg(std::make_unique<FakeModel>());
        const auto r = seg.segment(pcm);
        QVERIFY(r.has_value());
        QCOMPARE(r->windows.size(), 1);
        QCOMPARE(r->turns.size(), 1);
        QVERIFY(near(r->turns[0].start, 0.5) && near(r->turns[0].end, 2.5));
        QVERIFY(r->numFrames <= globalFrameCount(r->info, pcm.size()));
    }

    void openRefusesAMismatchingFileUnlessAccepted()
    {
        QTemporaryDir dir;
        const VoiceModelSpec spec = VoiceModelRegistry::segmentation().first();
        QString err;
        QVERIFY(!PyannoteSegmenter::open(spec, dir.path(), {}, {}, {}, &err));
        QVERIFY2(err.contains("voice-models fetch"), qPrintable(err));
        QVERIFY(!PyannoteSegmenter::isUsable(spec, dir.path(), {}));

        const QString path = VoiceModelDownloader::targetPath(spec, dir.path());
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a model");
        f.close();
        QVERIFY(!PyannoteSegmenter::isUsable(spec, dir.path(), {}));
        err.clear();
        QVERIFY(!PyannoteSegmenter::open(spec, dir.path(), {}, {}, {}, &err));
        QVERIFY2(err.contains("pinned SHA-256"), qPrintable(err));

        // "Use it anyway": accepted -> no longer refused by the integrity rule (it then fails to load,
        // being garbage, but with a load error).
        QVERIFY(VoiceModelDownloader::acceptMismatch(spec, dir.path()));
        QVERIFY(PyannoteSegmenter::isUsable(spec, dir.path(), {}));
        err.clear();
        QVERIFY(!PyannoteSegmenter::open(spec, dir.path(), {}, {}, {}, &err));
        QVERIFY2(!err.contains("pinned SHA-256") && !err.isEmpty(), qPrintable(err));
    }

    void realModelLoadsAndRuns()
    {
        const QString path = realModelPath();
        if (!QFileInfo::exists(path)) QSKIP("the pyannote segmentation model is not present");
        if (onnxExecutionProviders().isEmpty()) QSKIP("the build has no ONNX Runtime");
        QString err;
        std::unique_ptr<SegmentationModel> m = loadOnnxSegmentationModel(path, {}, &err);
        QVERIFY2(m, qPrintable(err));
        const ModelInfo& mi = m->info();
        QCOMPARE(mi.sampleRate, 16000);
        QCOMPARE(mi.windowSize, 160000);
        QCOMPARE(mi.receptiveFieldShift, 270);
        QCOMPARE(mi.receptiveFieldSize, 991);
        QCOMPARE(mi.numSpeakers, 3);
        QCOMPARE(mi.powersetMaxClasses, 2);
        QCOMPARE(mi.numClasses, 7);
        QCOMPARE(mi.framesPerWindow, 589);

        SegmenterOptions o;
        o.batchSize = 2;
        PyannoteSegmenter seg(std::move(m), o);
        const auto silent = seg.segment(QVector<float>(12 * kSr, 0.0f), {}, nullptr, &err);
        QVERIFY2(silent.has_value(), qPrintable(err));
        QCOMPARE(silent->windows.size(), 4);   // 3 full + 1 padded (12 s = 192000 samples)
        QVERIFY(silent->turns.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPyannoteSegmenter)
#include "test_pyannote_segmenter.moc"

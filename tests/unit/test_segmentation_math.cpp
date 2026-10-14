// The pure math of the speaker segmentation (SegmentationMath.h): powerset decoding, windowing, the
// Hungarian assignment, window stitching, aggregation and the min-duration post-processing. Synthetic
// logits / activity matrices only: no model, no audio.
#include "tanara/processing/diarization/SegmentationMath.h"

#include <QtTest>

#include <algorithm>
#include <limits>
#include <random>

using namespace tanara::processing::segmentation;

namespace {

ModelInfo pyannote()
{
    return ModelInfo{};   // the defaults are segmentation-3.0's
}

// A window's 0/1 activity from per-speaker frame ranges [a, b).
Activity activity(int frames, int speakers, const QVector<std::tuple<int, int, int>>& on)
{
    Activity a(frames, speakers);
    for (const auto& [s, from, to] : on)
        for (int f = from; f < to; ++f) a.at(f, s) = 1.0f;
    return a;
}

double bruteForceBest(const QVector<double>& cost, int rows, int cols)
{
    // Minimum over every injective assignment of min(rows, cols) pairs.
    const bool wide = rows <= cols;
    const int n = wide ? rows : cols, m = wide ? cols : rows;
    QVector<int> perm(m);
    std::iota(perm.begin(), perm.end(), 0);
    double best = std::numeric_limits<double>::infinity();
    do {
        double c = 0;
        for (int i = 0; i < n; ++i) c += wide ? cost[i * cols + perm[i]] : cost[perm[i] * cols + i];
        best = std::min(best, c);
    } while (std::next_permutation(perm.begin(), perm.end()));
    return best;
}

} // namespace

class TestSegmentationMath : public QObject {
    Q_OBJECT
private slots:
    void powersetOrderMatchesPyannote()
    {
        const QVector<QVector<int>> p = powersetClasses(3, 2);
        const QVector<QVector<int>> expected{{}, {0}, {1}, {2}, {0, 1}, {0, 2}, {1, 2}};
        QCOMPARE(p, expected);
        QCOMPARE(powersetClasses(4, 2).size(), 1 + 4 + 6);
        QCOMPARE(powersetClasses(3, 3).last(), (QVector<int>{0, 1, 2}));
        QVERIFY(pyannote().isValid());
        QCOMPARE(pyannote().computedFramesPerWindow(), 589);
    }

    void argmaxAndDecode()
    {
        // 4 frames x 7 classes; frame 3 has a tie between classes 1 and 2 -> the first wins.
        QVector<float> logits(4 * 7, -10.0f);
        logits[0 * 7 + 0] = 0;   // silence
        logits[1 * 7 + 2] = 0;   // {1}
        logits[2 * 7 + 5] = 0;   // {0,2}
        logits[3 * 7 + 1] = -1;
        logits[3 * 7 + 2] = -1;
        const QVector<quint8> cls = argmaxClasses(logits.constData(), 4, 7);
        QCOMPARE(cls, (QVector<quint8>{0, 2, 5, 1}));
        const Activity a = decodePowerset(cls, powersetClasses(3, 2), 3);
        QCOMPARE(a.frames, 4);
        QCOMPARE(a.countAt(0), 0);
        QCOMPARE(a.at(1, 1), 1.0f);
        QCOMPARE(a.countAt(1), 1);
        QCOMPARE(a.at(2, 0), 1.0f);
        QCOMPARE(a.at(2, 2), 1.0f);
        QCOMPARE(a.countAt(2), 2);
        QCOMPARE(a.at(3, 0), 1.0f);
        // An unknown class is silence.
        QCOMPARE(decodePowerset({9}, powersetClasses(3, 2), 3).countAt(0), 0);
    }

    void windowShiftIsWholeFrames()
    {
        const ModelInfo mi = pyannote();
        QCOMPARE(windowShiftSamples(mi, 0.1), 59 * 270);    // 0.996 s
        QCOMPARE(windowShiftSamples(mi, 0.25), 148 * 270);
        QCOMPARE(windowShiftSamples(mi, 0.5), 296 * 270);
        QCOMPARE(windowShiftSamples(mi, 0.0), 270);         // at least one frame
        QCOMPARE(windowShiftSamples(mi, 5.0), 592 * 270);   // at most one window
    }

    void windowStartsCoverTheAudio()
    {
        QVERIFY(windowStarts(0, 1000, 100).isEmpty());
        QCOMPARE(windowStarts(10, 1000, 100), (QVector<qint64>{0}));
        QCOMPARE(windowStarts(1000, 1000, 100), (QVector<qint64>{0}));
        QCOMPARE(windowStarts(1001, 1000, 100), (QVector<qint64>{0, 100}));       // padded tail
        QCOMPARE(windowStarts(1200, 1000, 100), (QVector<qint64>{0, 100, 200}));  // exact fit
        QCOMPARE(windowStarts(1250, 1000, 100), (QVector<qint64>{0, 100, 200, 300}));
        const ModelInfo mi = pyannote();
        // 84 s at 0.1: (84*16000 - 160000) / 15930 + 1 = 75.3 -> 76 windows (the last one padded).
        QCOMPARE(windowStarts(84 * 16000, mi.windowSize, windowShiftSamples(mi, 0.1)).size(), 76);
        QCOMPARE(globalFrameCount(mi, 0), 0);
        QCOMPARE(globalFrameCount(mi, mi.windowSize), 591);   // frame centres inside 10 s
        QVERIFY(mi.frameTimeSec(0) > 0.03 && mi.frameTimeSec(0) < 0.032);
    }

    void hungarianSmallCases()
    {
        // The classic 3x3: optimum 0->1, 1->0, 2->2 (cost 1 + 2 + 2 = 5).
        const QVector<double> c{4, 1, 3, 2, 0, 5, 3, 2, 2};
        const QVector<int> a = hungarianAssign(c, 3, 3);
        double sum = 0;
        for (int i = 0; i < 3; ++i) sum += c[i * 3 + a[i]];
        QCOMPARE(sum, 5.0);
        // Wide 2x3 and tall 3x2: one row stays unassigned in the tall case.
        const QVector<int> w = hungarianAssign({5, 1, 9, 1, 5, 9}, 2, 3);
        QCOMPARE(w, (QVector<int>{1, 0}));
        const QVector<int> t = hungarianAssign({5, 1, 1, 5, 0, 0}, 3, 2);
        QCOMPARE(std::count(t.begin(), t.end(), -1), 1);
        QVERIFY(hungarianAssign({}, 0, 3).isEmpty());
        QCOMPARE(hungarianAssign({}, 2, 0), (QVector<int>{-1, -1}));
    }

    void hungarianMatchesBruteForce()
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> d(-5.0, 5.0);
        for (int rows = 1; rows <= 5; ++rows) {
            for (int cols = 1; cols <= 5; ++cols) {
                for (int rep = 0; rep < 20; ++rep) {
                    QVector<double> c(rows * cols);
                    for (double& x : c) x = d(rng);
                    const QVector<int> a = hungarianAssign(c, rows, cols);
                    QCOMPARE(a.size(), rows);
                    QSet<int> usedCols;
                    double sum = 0;
                    int assigned = 0;
                    for (int r = 0; r < rows; ++r) {
                        if (a[r] < 0) continue;
                        QVERIFY(!usedCols.contains(a[r]));
                        usedCols.insert(a[r]);
                        sum += c[r * cols + a[r]];
                        ++assigned;
                    }
                    QCOMPARE(assigned, std::min(rows, cols));
                    QVERIFY(qAbs(sum - bruteForceBest(c, rows, cols)) < 1e-9);
                }
            }
        }
    }

    void stitchingFollowsPermutedLocalSpeakers()
    {
        // Two windows of 100 frames, the second starts at frame 50. Window 0: local 0 = A on 0-40,
        // local 1 = B on 45-100. Window 1 has them swapped: local 2 = B on 0-50 (= global 50-100),
        // local 0 = A on 60-90 (global 110-140; no overlap evidence -> a NEW id), local 1 silent.
        QVector<WindowActivity> w;
        w.append({0, activity(100, 3, {{0, 0, 40}, {1, 45, 100}})});
        w.append({50, activity(100, 3, {{2, 0, 50}, {0, 60, 90}})});
        const StitchResult r = stitchWindows(w);
        QCOMPARE(r.localToStitched[0], (QVector<int>{0, 1, -1}));
        QCOMPARE(r.localToStitched[1][2], 1);    // B kept its id
        QCOMPARE(r.localToStitched[1][1], -1);   // silent
        QCOMPARE(r.localToStitched[1][0], 2);    // A came back without overlap evidence: new id
        QCOMPARE(r.numStitched, 3);
    }

    void stitchingNeedsEnoughAgreement()
    {
        // The second window's speaker overlaps the first one's only on 5 shared frames: too little
        // (minSharedFrames 10) -> a new id. With a lower bar it is the same speaker.
        QVector<WindowActivity> w;
        w.append({0, activity(100, 3, {{0, 0, 55}})});
        w.append({50, activity(100, 3, {{1, 0, 5}, {1, 30, 60}})});
        QCOMPARE(stitchWindows(w).localToStitched[1][1], 1);
        StitchOptions lax;
        lax.minSharedFrames = 3;
        lax.minIoU = 0.0;
        QCOMPARE(stitchWindows(w, lax).localToStitched[1][1], 0);
    }

    void stitchingUsesEveryCoveringWindow()
    {
        // Three windows shifted by 10 frames: A active everywhere, each window names it differently.
        QVector<WindowActivity> w;
        for (int k = 0; k < 3; ++k) w.append({k * 10, activity(40, 3, {{k, 0, 40}})});
        const StitchResult r = stitchWindows(w);
        QCOMPARE(r.numStitched, 1);
        for (int k = 0; k < 3; ++k) QCOMPARE(r.localToStitched[k][k], 0);
    }

    void aggregationKeepsOverlapAndVotes()
    {
        // Two identical windows (same frames): A on 0-30, B on 20-40 (overlap 20-30).
        QVector<WindowActivity> w;
        w.append({0, activity(40, 3, {{0, 0, 30}, {1, 20, 40}})});
        w.append({0, activity(40, 3, {{1, 0, 30}, {0, 20, 40}})});   // permuted
        const StitchResult st = stitchWindows(w);
        QCOMPARE(st.numStitched, 2);
        const FrameLabels l = aggregate(w, st, 40, 2);
        QCOMPARE(l.count(10), 1);
        QCOMPARE(l.count(25), 2);
        QVERIFY(l.has(25, 0) && l.has(25, 1));
        QCOMPARE(l.count(35), 1);
        QVERIFY(l.has(35, 1));
        // Frames past totalFrames are dropped; maxActive caps the count.
        QCOMPARE(aggregate(w, st, 20, 2).frames, 20);
        QCOMPARE(aggregate(w, st, 40, 1).count(25), 1);
    }

    void aggregationRoundsTheMeanCount()
    {
        // Three windows over the same 10 frames: two say "speaker", one says silence -> active.
        QVector<WindowActivity> w;
        w.append({0, activity(10, 3, {{0, 0, 10}})});
        w.append({0, activity(10, 3, {{0, 0, 10}})});
        w.append({0, activity(10, 3, {})});
        FrameLabels l = aggregate(w, stitchWindows(w), 10, 2);
        QCOMPARE(l.count(5), 1);
        // One of three: silence.
        w[1].activity = activity(10, 3, {});
        l = aggregate(w, stitchWindows(w), 10, 2);
        QCOMPARE(l.count(5), 0);
    }

    void minDurationBridgesThenDrops()
    {
        // Gap 0.3 < minOff 0.5 is bridged first, so the two short pieces survive as one 0.5 s span;
        // the lone 0.2 s span is dropped (minOn 0.3).
        const QVector<Span> out = applyMinDuration({{0.0, 0.1}, {0.4, 0.5}, {2.0, 2.2}, {5.0, 6.0}}, 0.3, 0.5);
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].start, 0.0);
        QCOMPARE(out[0].end, 0.5);
        QCOMPARE(out[1].start, 5.0);
        // Zero thresholds keep everything.
        QCOMPARE(applyMinDuration({{0.0, 0.1}, {0.15, 0.2}}, 0, 0).size(), 2);
    }

    void labelsToTurnsAndOverlapFlags()
    {
        const ModelInfo mi = pyannote();
        FrameLabels l;
        l.frames = 300;
        l.maxActive = 2;
        l.ids.fill(-1, 600);
        for (int f = 0; f < 120; ++f) l.ids[f * 2] = 0;           // speaker 0: frames 0-120 (~2 s)
        for (int f = 100; f < 250; ++f) l.ids[f * 2 + 1] = 3;     // speaker 3: frames 100-250 (overlap 100-120)
        for (int f = 280; f < 285; ++f) l.ids[f * 2] = 0;         // 5 frames (~84 ms): dropped by minOn
        const QVector<Turn> t = labelsToTurns(l, mi, 0.3, 0.1);
        QCOMPARE(t.size(), 2);
        QCOMPARE(t[0].speaker, 0);
        QVERIFY(qAbs(t[0].start - mi.frameTimeSec(0)) < 1e-9);
        QVERIFY(qAbs(t[0].end - mi.frameTimeSec(120)) < 1e-9);
        QVERIFY(t[0].overlap && t[1].overlap);
        QCOMPARE(t[1].speaker, 3);

        QVector<Turn> plain{{0, 1, 0, true}, {1, 2, 1, true}, {1.5, 1.8, 1, false}};
        flagOverlaps(plain);   // touching turns do not overlap; one speaker never overlaps itself
        for (const Turn& x : plain) QVERIFY(!x.overlap);
    }

    void windowTurnsPerLocalSpeaker()
    {
        const ModelInfo mi = pyannote();
        const WindowActivity w{100, activity(50, 3, {{0, 0, 20}, {2, 15, 30}, {0, 40, 50}})};
        const QVector<LocalTurn> t = windowTurns(4, w, mi);
        QCOMPARE(t.size(), 3);
        QCOMPARE(t[0].window, 4);
        QCOMPARE(t[0].localSpeaker, 0);
        QCOMPARE(t[0].startFrame, 100);
        QCOMPARE(t[0].endFrame, 120);
        QVERIFY(t[0].overlap);
        QCOMPARE(t[1].localSpeaker, 2);
        QVERIFY(t[1].overlap);
        QCOMPARE(t[2].startFrame, 140);
        QVERIFY(!t[2].overlap);
        QVERIFY(qAbs(t[2].end - mi.frameTimeSec(150)) < 1e-9);
    }
};

QTEST_APPLESS_MAIN(TestSegmentationMath)
#include "test_segmentation_math.moc"

// The clustering step of the local diarization (D4): the complete-linkage math on synthetic embeddings
// (2-4 clusters, threshold vs numClusters, the complete-linkage property, the small-cluster reassignment,
// the cap's subsample, centroid normalisation), LocalDiarizer::clusterTrack over a fake segmentation +
// a fake embedder (a voice that returns after a long silence is one speaker again; offsets; samples), the
// whole LocalDiarizer job with a fake voice model (a hand-bound track stays one speaker; progress).
// A real-model case runs only if the segmentation model and the CAM++ model are present (read-only), and
// with TANARA_TEST_DIARIZATION_WAV (a 16 kHz mono 16-bit WAV) it also checks the speaker count
// (TANARA_TEST_DIARIZATION_SPEAKERS).
#include "tanara/processing/diarization/LocalDiarizer.h"
#include "tanara/processing/diarization/SpeakerClustering.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QtEndian>
#include <QSignalSpy>
#include <QtTest>

#include <cmath>

using namespace tanara;
using namespace tanara::processing;
using namespace tanara::processing::segmentation;

namespace {

constexpr int kSr = 16000;
constexpr double kPi = 3.14159265358979323846;

QVector<float> unit(QVector<float> v)
{
    double n = 0;
    for (float x : v) n += double(x) * x;
    n = std::sqrt(n);
    for (float& x : v) x = float(x / n);
    return v;
}

// k well-separated directions in `dim` dimensions, `per` noisy points around each; truth = the direction.
QVector<QVector<float>> blobs(int k, int per, int dim, double noise, QVector<int>* truth, quint32 seed = 7)
{
    QRandomGenerator rng(seed);
    QVector<QVector<float>> out;
    for (int i = 0; i < per; ++i)
        for (int c = 0; c < k; ++c) {
            QVector<float> v(dim, 0.0f);
            v[c] = 1.0f;
            for (float& x : v) x += float((rng.generateDouble() - 0.5) * 2.0 * noise);
            out.append(unit(v));
            truth->append(c);
        }
    return out;
}

// Same truth <=> same label (permutation-free).
bool samePartition(const QVector<int>& a, const QVector<int>& b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        for (int j = i + 1; j < a.size(); ++j)
            if ((a[i] == a[j]) != (b[i] == b[j])) return false;
    return true;
}

// Codes in the signal: 0.1 / 0.2 / 0.3 = speaker A / B / C alone (local speaker 0 / 1 / 2).
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
                const int code = int(std::lround(v * 10));
                (*out)[(qsizetype(i) * F + f) * C + (code >= 1 && code <= 3 ? code : 0)] = 0.0f;
            }
        return true;
    }

private:
    ModelInfo m_info;
};

void fill(QVector<float>& pcm, double from, double to, float code)
{
    for (int i = int(from * kSr); i < int(to * kSr) && i < pcm.size(); ++i) pcm[i] = code;
}

// A 1-6 s, B 7-12 s, (18 s silence), A 30-35 s, C 36-40 s.
QVector<float> scene()
{
    QVector<float> pcm(42 * kSr, 0.0f);
    fill(pcm, 1, 6, 0.1f);
    fill(pcm, 7, 12, 0.2f);
    fill(pcm, 30, 35, 0.1f);
    fill(pcm, 36, 40, 0.3f);
    return pcm;
}

// The fake voice: the slice's dominant code -> a one-hot direction (plus a small per-slice wobble).
EmbeddingSet fakeEmbed(const QVector<float>& pcm)
{
    if (pcm.size() < 1600) return {};
    int counts[4] = {0, 0, 0, 0};
    for (float x : pcm) {
        const int c = int(std::lround(x * 10));
        if (c >= 1 && c <= 3) ++counts[c];
    }
    const int code = counts[1] >= counts[2] && counts[1] >= counts[3] ? 1 : counts[2] >= counts[3] ? 2 : 3;
    QVector<float> v(4, 0.02f * float(pcm.size() % 7));
    v[code] = 1.0f;
    EmbeddingSet s;
    s.insert(QStringLiteral("fake"), unit(v));
    return s;
}

SegmentationResult segmentScene(const QVector<float>& pcm, SegmenterOptions opts = {})
{
    opts.windowShiftRatio = LocalDiarizer::kDefaultWindowShift;
    PyannoteSegmenter seg(std::make_unique<FakeModel>(), opts, QStringLiteral("fake-seg"));
    return *seg.segment(pcm);
}

PcmEmbedderLoader fakeVoiceLoader()
{
    return [](const VoiceModelSpec&, const QString&, QString*) -> PcmEmbedder {
        return [](const QVector<float>& pcm) { return fakeEmbed(pcm).value(QStringLiteral("fake")); };
    };
}

VoiceEmbedderSet fakeVoiceSet()
{
    VoiceModelEntry e;
    e.spec.id = QStringLiteral("fake");
    e.spec.dim = 4;
    e.path = QStringLiteral("/nonexistent/fake.onnx");
    return VoiceEmbedderSet({e}, fakeVoiceLoader());
}

LocalDiarizer::SegmenterLoader fakeSegLoader()
{
    return [](const QString&, const SegmenterOptions& opts, QString*) {
        return std::make_unique<PyannoteSegmenter>(std::make_unique<FakeModel>(), opts, QStringLiteral("fake-seg"));
    };
}

double norm(const QVector<float>& v)
{
    double n = 0;
    for (float x : v) n += double(x) * x;
    return std::sqrt(n);
}

QVector<float> readWav16(const QString& path, int* rate)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray b = f.readAll();
    if (b.size() < 44 || !b.startsWith("RIFF")) return {};
    int pos = 12;
    int channels = 1, bits = 16;
    *rate = 0;
    QVector<float> out;
    while (pos + 8 <= b.size()) {
        const QByteArray id = b.mid(pos, 4);
        const quint32 len = qFromLittleEndian<quint32>(b.constData() + pos + 4);
        const char* body = b.constData() + pos + 8;
        if (id == "fmt ") {
            channels = qFromLittleEndian<quint16>(body + 2);
            *rate = int(qFromLittleEndian<quint32>(body + 4));
            bits = qFromLittleEndian<quint16>(body + 14);
        } else if (id == "data" && bits == 16) {
            const qsizetype n = qsizetype(std::min<quint32>(len, quint32(b.size() - pos - 8))) / 2 / channels;
            for (qsizetype i = 0; i < n; ++i)
                out.append(qFromLittleEndian<qint16>(body + i * 2 * channels) / 32768.0f);
        }
        pos += 8 + int(len) + int(len & 1);
    }
    return out;
}

} // namespace

class TestDiarizationClustering : public QObject {
    Q_OBJECT
private slots:
    void findsTwoToFourClusters()
    {
        for (int k = 2; k <= 4; ++k) {
            QVector<int> truth;
            const auto vs = blobs(k, 12, 8, 0.25, &truth, quint32(k));
            const QVector<int> labels = clustering::clusterCompleteLinkage(vs, 0.5);
            QCOMPARE(clustering::clusterCount(labels), k);
            QVERIFY(samePartition(labels, truth));
            QCOMPARE(labels.first(), 0);   // dense, in the order of the first item
        }
    }

    void numClustersOverridesTheThreshold()
    {
        QVector<int> truth;
        const auto vs = blobs(4, 6, 8, 0.2, &truth);
        QCOMPARE(clustering::clusterCount(clustering::clusterCompleteLinkage(vs, 0.5, 2)), 2);
        QCOMPARE(clustering::clusterCount(clustering::clusterCompleteLinkage(vs, 0.5, 1)), 1);
        QCOMPARE(clustering::clusterCount(clustering::clusterCompleteLinkage(vs, 0.5, 100)), int(vs.size()));
        // A higher threshold merges everything; zero keeps every item apart.
        QCOMPARE(clustering::clusterCount(clustering::clusterCompleteLinkage(vs, 1.5)), 1);
        QCOMPARE(clustering::clusterCount(clustering::clusterCompleteLinkage(vs, 0.0)), int(vs.size()));
        QVERIFY(clustering::clusterCompleteLinkage({}, 0.5).isEmpty());
        QCOMPARE(clustering::clusterCompleteLinkage({unit({1, 0})}, 0.5), QVector<int>{0});
    }

    void completeNotSingleLinkage()
    {
        // 0, 45 and 90 degrees: neighbours 0.29 apart, the ends 1.0. Single linkage at 0.5 would chain all
        // three; complete linkage keeps the ends apart.
        const QVector<QVector<float>> vs{unit({1, 0}), unit({1, 1}), unit({0, 1})};
        const QVector<clustering::Merge> merges = clustering::completeLinkage(vs);
        QCOMPARE(merges.size(), 2);
        QVERIFY(std::abs(merges[0].height - (1.0 - std::sqrt(0.5))) < 1e-4);
        QVERIFY(std::abs(merges[1].height - 1.0f) < 1e-4);   // max(d(0,90), d(45,90)) = 1
        QCOMPARE(clustering::clusterCount(clustering::cutTree(3, merges, 0.5)), 2);
        // The cut is strict: a merge exactly at the threshold is not applied.
        QCOMPARE(clustering::clusterCount(clustering::cutTree(3, merges, merges[0].height)), 3);
    }

    void smallClustersJoinTheNearestLargeOne()
    {
        QVector<int> truth;
        auto vs = blobs(2, 6, 4, 0.1, &truth);
        // An outlier nearer to direction 1 than to direction 0, but beyond the threshold from both.
        vs.append(unit({0.2f, 0.5f, 0.6f, 0.6f}));
        QVector<int> labels = clustering::clusterCompleteLinkage(vs, 0.5);
        QCOMPARE(clustering::clusterCount(labels), 3);
        labels = clustering::reassignSmallClusters(labels, vs, 3);
        QCOMPARE(clustering::clusterCount(labels), 2);
        QCOMPARE(labels.last(), labels[1]);   // item 1 is a direction-1 point
        // Off (1) or every cluster small: unchanged.
        QCOMPARE(clustering::clusterCount(clustering::reassignSmallClusters(
                     clustering::clusterCompleteLinkage(vs, 0.5), vs, 1)), 3);
        QCOMPARE(clustering::clusterCount(clustering::reassignSmallClusters(
                     clustering::clusterCompleteLinkage(vs, 0.5), vs, 100)), 3);
    }

    void subsampleAndCentroid()
    {
        QCOMPARE(clustering::subsample(5, 10, {}), QVector<int>({0, 1, 2, 3, 4}));
        QCOMPARE(clustering::subsample(5, 0, {}).size(), 5);
        // 10 items, cap 3 -> every 4th; group 7 (only at index 9) keeps one item.
        QVector<int> groups{1, 1, 1, 1, 2, 2, 2, 2, 1, 7};
        QCOMPARE(clustering::subsample(10, 3, groups), QVector<int>({0, 4, 8, 9}));

        const QVector<QVector<float>> vs{{3, 0}, {0, 4}, {}};
        const QVector<float> c = clustering::centroid(vs, {0, 1, 2});
        QCOMPARE(c.size(), 2);
        QVERIFY(std::abs(norm(c) - 1.0) < 1e-6);
        QVERIFY(clustering::centroid(vs, {2}).isEmpty());
    }

    void clusterTrackJoinsAReturningVoice()
    {
        const QVector<float> pcm = scene();
        const SegmentationResult seg = segmentScene(pcm);
        QVERIFY(seg.numStitched >= 4);   // A comes back after 18 s of silence: a new stitched id

        providers::DiarizeTrack track;
        track.trackId = QStringLiteral("remote");
        track.path = QStringLiteral("/meeting/track_remote.ogg");
        track.startOffsetMs = 2000;
        SegmenterOptions opts;
        providers::DiarizeResult out;
        QVector<QPair<int, int>> progress;
        const TrackClusterStats st = LocalDiarizer::clusterTrack(
            track, seg, pcm, opts, ClusteringOptions(), {QStringLiteral("fake")}, fakeEmbed, &out,
            [&](int d, int n) { progress.append({d, n}); return true; });
        QVERIFY(!st.cancelled);
        QVERIFY(!st.subsampled);
        QVERIFY(st.embedded > 0 && st.embedded <= st.spans);
        QCOMPARE(st.speakers, 3);
        QCOMPARE(out.speakers.size(), 3);
        QCOMPARE(out.turns.size(), 4);
        // A, B, A, C: the returning A keeps index 0; times are shifted by the offset.
        QCOMPARE(out.turns[0].speakerIndex, 0);
        QCOMPARE(out.turns[1].speakerIndex, 1);
        QCOMPARE(out.turns[2].speakerIndex, 0);
        QCOMPARE(out.turns[3].speakerIndex, 2);
        QVERIFY(qAbs(out.turns[0].startMs - 3000) <= 40);
        QVERIFY(qAbs(out.turns[2].startMs - 32000) <= 40);
        QVERIFY(qAbs(out.speakers[0].totalMs - 10000) <= 100);
        for (const providers::DiarizedSpeaker& sp : out.speakers) {
            QCOMPARE(sp.trackId, QStringLiteral("remote"));
            QCOMPARE(sp.centroid.trackId, QStringLiteral("remote"));
            QCOMPARE(sp.centroid.vectors.size(), 1);
            QCOMPARE(sp.centroid.vectors[0].modelId, QStringLiteral("fake"));
            QCOMPARE(sp.centroid.vectors[0].dim, 4);
            QVERIFY(std::abs(norm(sp.centroid.vectors[0].vector) - 1.0) < 1e-5);
            QVERIFY(sp.centroid.windows > 0);
            QVERIFY(sp.centroid.sampleRef.startsWith(QStringLiteral("track_remote.ogg#")));
            QVERIFY(sp.person.isNull());
        }
        // A's sample: its longest clean turn (one of its two 5 s turns), in meeting time.
        const QString ref = out.speakers[0].centroid.sampleRef;
        auto refOf = [](const providers::SpeakerTurn& t) {
            return QStringLiteral("track_remote.ogg#%1-%2").arg(t.startMs).arg(t.endMs);
        };
        QVERIFY2(ref == refOf(out.turns[0]) || ref == refOf(out.turns[2]), qPrintable(ref));
        // At most sampleMaxSec long.
        ClusteringOptions shortSample;
        shortSample.sampleMaxSec = 2.0;
        providers::DiarizeResult outS;
        LocalDiarizer::clusterTrack(track, seg, pcm, opts, shortSample, {QStringLiteral("fake")}, fakeEmbed, &outS);
        const QStringList span = outS.speakers[0].centroid.sampleRef.section(QLatin1Char('#'), 1).split(QLatin1Char('-'));
        QCOMPARE(span[1].toLongLong() - span[0].toLongLong(), 2000);
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last().first, progress.last().second);

        // numClusters = 2: B and C (or A) merge.
        ClusteringOptions two;
        two.numClusters = 2;
        providers::DiarizeResult out2;
        LocalDiarizer::clusterTrack(track, seg, pcm, opts, two, {QStringLiteral("fake")}, fakeEmbed, &out2);
        QCOMPARE(out2.speakers.size(), 2);

        // The cap: at most 2 embeddings, yet every stitched id keeps one sampled span -> still 3 speakers.
        ClusteringOptions capped;
        capped.maxEmbeddings = 2;
        capped.minClusterSize = 1;
        providers::DiarizeResult out3;
        const TrackClusterStats st3 = LocalDiarizer::clusterTrack(track, seg, pcm, opts, capped,
                                                                  {QStringLiteral("fake")}, fakeEmbed, &out3);
        QVERIFY(st3.subsampled);
        QVERIFY(st3.embedded < st.embedded);
        QCOMPARE(out3.speakers.size(), 3);
        QCOMPARE(out3.turns.size(), 4);

        // Cancel: nothing appended.
        providers::DiarizeResult out4;
        const TrackClusterStats st4 = LocalDiarizer::clusterTrack(track, seg, pcm, opts, ClusteringOptions(),
                                                                  {QStringLiteral("fake")}, fakeEmbed, &out4,
                                                                  [](int, int) { return false; });
        QVERIFY(st4.cancelled);
        QVERIFY(out4.turns.isEmpty() && out4.speakers.isEmpty());
    }

    void jobClustersUnboundTracksOnly()
    {
        LocalDiarizer d(fakeSegLoader(), nullptr);
        d.setEmbedderSet(fakeVoiceSet());
        providers::DiarizeRequest req;
        providers::DiarizeTrack a, b;
        a.trackId = QStringLiteral("a");
        a.pcm = scene();
        b.trackId = QStringLiteral("b");
        b.pcm = scene();
        b.startOffsetMs = 1000;
        b.knownPerson = PersonId::generate();
        req.tracks = {a, b};
        auto* job = d.diarize(req);
        QSignalSpy done(job, &providers::CapabilityJob::finished);
        QList<QPair<qint64, qint64>> progress;
        connect(job, &providers::CapabilityJob::progress, this, [&](qint64 x, qint64 n) { progress.append({x, n}); });
        QVERIFY(done.wait(10000));
        const providers::DiarizeResult r = job->result();
        QCOMPARE(r.segmentationModelId, QStringLiteral("fake-seg"));
        QCOMPARE(r.embeddingModelIds, QStringList{QStringLiteral("fake")});
        // a: three clustered speakers with centroids; b: one speaker (the bound person), no centroid.
        QVERIFY(r.speaker(QStringLiteral("a"), 2));
        QVERIFY(!r.speaker(QStringLiteral("a"), 0)->centroid.vectors.isEmpty());
        const providers::DiarizedSpeaker* sb = r.speaker(QStringLiteral("b"), 0);
        QVERIFY(sb);
        QCOMPARE(sb->person, b.knownPerson);
        QVERIFY(sb->centroid.vectors.isEmpty());
        QVERIFY(!r.speaker(QStringLiteral("b"), 1));
        for (const providers::SpeakerTurn& t : r.turnsOf(QStringLiteral("b"))) QCOMPARE(t.speakerIndex, 0);
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last().first, progress.last().second);
        for (int i = 1; i < progress.size(); ++i) QVERIFY(progress[i].first >= progress[i - 1].first);

        // Request options reach the clustering: two speakers forced.
        req.tracks = {a};
        req.numClusters = 2;
        auto* job2 = d.diarize(req);
        QSignalSpy done2(job2, &providers::CapabilityJob::finished);
        QVERIFY(done2.wait(10000));
        QCOMPARE(job2->result().speakers.size(), 2);

        // A voice model that cannot be loaded: Unsupported "no_embedding_model".
        LocalDiarizer broken(fakeSegLoader(), nullptr);
        VoiceModelEntry e;
        e.spec.id = QStringLiteral("x");
        broken.setEmbedderSet(VoiceEmbedderSet({e}, [](const VoiceModelSpec&, const QString&, QString* err) {
            *err = QStringLiteral("cannot load");
            return PcmEmbedder();
        }));
        auto* job3 = broken.diarize(req);
        QSignalSpy failed3(job3, &providers::CapabilityJob::failed);
        QVERIFY(failed3.wait(10000));
        QCOMPARE(job3->error().code, QStringLiteral("no_embedding_model"));
    }

    void realModelsEndToEnd()
    {
        const QString segPath = qEnvironmentVariable("TANARA_TEST_SEGMENTATION_MODEL",
            QDir::home().filePath(QStringLiteral(".tanara/models/pyannote-segmentation-3.0/model.onnx")));
        const QString embPath = QDir::home().filePath(QStringLiteral(".tanara/models/campplus_sv_zh_en_16k.onnx"));
        if (!QFileInfo::exists(segPath)) QSKIP("the pyannote segmentation model is not present");
        if (!QFileInfo::exists(embPath)) QSKIP("the CAM++ voice model is not present");
        if (onnxExecutionProviders().isEmpty()) QSKIP("the build has no ONNX Runtime");

        QVector<float> pcm;
        int expected = -1;
        const QString wav = qEnvironmentVariable("TANARA_TEST_DIARIZATION_WAV");
        if (!wav.isEmpty()) {
            int rate = 0;
            pcm = readWav16(wav, &rate);
            QCOMPARE(rate, kSr);
            expected = qEnvironmentVariableIntValue("TANARA_TEST_DIARIZATION_SPEAKERS");
        } else {
            // No speech at hand: a quiet synthetic signal only proves the pipeline runs end to end.
            pcm.resize(30 * kSr);
            for (int i = 0; i < pcm.size(); ++i) pcm[i] = 0.05f * float(std::sin(2 * kPi * 180.0 * i / kSr));
        }
        LocalDiarizer d([segPath](const QString&, const SegmenterOptions& opts, QString* err) -> std::unique_ptr<PyannoteSegmenter> {
            auto m = loadOnnxSegmentationModel(segPath, {}, err);
            if (!m) return nullptr;
            return std::make_unique<PyannoteSegmenter>(std::move(m), opts, QStringLiteral("pyannote-segmentation-3.0"));
        }, nullptr);
        const std::optional<VoiceModelSpec> camp = VoiceModelRegistry::spec(VoiceModelRegistry::defaultModelId());
        QVERIFY(camp);
        d.setEmbedderSet(VoiceEmbedderSet({VoiceModelEntry{*camp, embPath}}));
        providers::DiarizeRequest req;
        providers::DiarizeTrack t;
        t.trackId = QStringLiteral("real");
        t.pcm = pcm;
        req.tracks = {t};
        auto* job = d.diarize(req);
        QSignalSpy done(job, &providers::CapabilityJob::finished);
        QSignalSpy failed(job, &providers::CapabilityJob::failed);
        QVERIFY2(done.wait(300000), qPrintable(failed.isEmpty() ? QString() : job->error().message));
        const providers::DiarizeResult r = job->result();
        QCOMPARE(r.embeddingModelIds, QStringList{camp->id});
        for (const providers::DiarizedSpeaker& sp : r.speakers) {
            QVERIFY(sp.totalMs > 0);
            for (const providers::VoiceVector& v : sp.centroid.vectors)
                QVERIFY(std::abs(norm(v.vector) - 1.0) < 1e-4);
        }
        if (expected > 0) QCOMPARE(int(r.speakers.size()), expected);
        qInfo() << "real models:" << r.speakers.size() << "speakers," << r.turns.size() << "turns";
    }
};

QTEST_GUILESS_MAIN(TestDiarizationClustering)
#include "test_diarization_clustering.moc"

// Multi-model embedding: EmbeddingSet + fusion (fuse / averageCosine), the per-model
// utterance cache (transcript.embeddings.bin v2; v1 is dropped), and, if the CAM++ model file
// exists, the real multi-model embedder with a single decode. It reads no real data; it only
// reads the model file (TANARA_TEST_VOICE_MODEL, otherwise ~/.tanara/models/...).
#include <QtTest>
#include <QDataStream>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/processing/voiceid/EmbeddingSet.h"

#include <cmath>

using namespace tanara;

namespace {

QVector<float> randomVec(QRandomGenerator& rng, int dim)
{
    QVector<float> v(dim);
    for (float& x : v) x = float(rng.generateDouble() * 2.0 - 1.0);
    return v;
}

double cosine(const QVector<float>& a, const QVector<float>& b)
{
    return library::VoiceprintStore::cosineSimilarity(a, b);
}

// 16 kHz mono s16 WAV (a noisy, varying-pitch signal: "speech-like" to the model).
bool writeWav(const QString& path, int seconds)
{
    const int rate = 16000, n = rate * seconds;
    QByteArray pcm;
    pcm.resize(n * 2);
    QRandomGenerator rng(7);
    auto* s = reinterpret_cast<qint16*>(pcm.data());
    for (int i = 0; i < n; ++i) {
        const double t = double(i) / rate;
        const double f = 140.0 + 40.0 * std::sin(2 * M_PI * 3.0 * t);
        const double v = 0.4 * std::sin(2 * M_PI * f * t) + 0.2 * std::sin(2 * M_PI * 2.7 * f * t)
                         + 0.05 * (rng.generateDouble() - 0.5);
        s[i] = qint16(std::clamp(v, -1.0, 1.0) * 32000);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&f);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4); out << quint32(36 + pcm.size());
    out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1)
                                         << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
    out.writeRawData("data", 4); out << quint32(pcm.size());
    out.writeRawData(pcm.constData(), pcm.size());
    return true;
}

QString campplusModel()
{
    const QString env = qEnvironmentVariable("TANARA_TEST_VOICE_MODEL");
    if (!env.isEmpty()) return env;
    // The user's model file (read-only): the real home, independent of TANARA_HOME.
    return QDir::home().filePath(QStringLiteral(".tanara/models/campplus_sv_zh_en_16k.onnx"));
}

} // namespace

class VoiceFusionTest : public QObject {
    Q_OBJECT
private slots:
    void fuse_cosineIsMeanOfPerModelCosines()
    {
        QRandomGenerator rng(42);
        const QStringList ids{"b", "a", "c"};   // the order does not matter (alphabetical)
        for (int round = 0; round < 50; ++round) {
            EmbeddingSet x, y;
            x["a"] = randomVec(rng, 192); y["a"] = randomVec(rng, 192);
            x["b"] = randomVec(rng, 256); y["b"] = randomVec(rng, 256);
            x["c"] = randomVec(rng, 512); y["c"] = randomVec(rng, 512);
            // Closer pairs too (not only ~0 cosines).
            if (round % 2) for (float& v : y["b"]) v = v * 0.2f + 1.0f;
            if (round % 2) for (float& v : x["b"]) v = v * 0.2f + 1.0f;
            const QVector<float> fx = processing::fusion::fuse(x, ids), fy = processing::fusion::fuse(y, ids);
            QCOMPARE(fx.size(), 192 + 256 + 512);
            const double expected = (cosine(x["a"], y["a"]) + cosine(x["b"], y["b"]) + cosine(x["c"], y["c"])) / 3.0;
            QVERIFY2(std::abs(cosine(fx, fy) - expected) < 1e-5,
                     qPrintable(QStringLiteral("%1 vs %2").arg(cosine(fx, fy)).arg(expected)));
            // The fusion vector has unit length; a model that was not requested does not count.
            double norm = 0;
            for (float v : fx) norm += double(v) * v;
            QVERIFY(std::abs(norm - 1.0) < 1e-5);
            int used = 0;
            QVERIFY(std::abs(processing::fusion::averageCosine(x, y, ids, &used) - expected) < 1e-9);
            QCOMPARE(used, 3);
        }
    }

    void fuse_singleModelIsNormalizedVector()
    {
        EmbeddingSet s;
        s["campplus"] = {3, 4};
        const QVector<float> f = processing::fusion::fuse(s, {"campplus"});
        QCOMPARE(f.size(), 2);
        QVERIFY(std::abs(f[0] - 0.6f) < 1e-6 && std::abs(f[1] - 0.8f) < 1e-6);
    }

    void fuse_emptyWhenModelMissing()
    {
        EmbeddingSet s;
        s["a"] = {1, 0};
        s["b"] = {};
        QVERIFY(processing::fusion::fuse(s, {"a", "b"}).isEmpty());     // an empty vector = none
        QVERIFY(processing::fusion::fuse(s, {"a", "c"}).isEmpty());     // a missing key
        QVERIFY(processing::fusion::fuse(s, {}).isEmpty());             // no requested model
        QVERIFY(!processing::fusion::fuse(s, {"a"}).isEmpty());         // an extra model does not interfere
    }

    void averageCosine_partialAndNoOverlap()
    {
        EmbeddingSet a, b;
        a["m1"] = {1, 0}; a["m2"] = {0, 1};
        b["m1"] = {1, 0}; b["m3"] = {1, 0};
        int used = -1;
        QCOMPARE(processing::fusion::averageCosine(a, b, {"m1", "m2", "m3"}, &used), 1.0);   // only m1 is common
        QCOMPARE(used, 1);
        b["m2"] = {1, 0};   // m2: cosine 0
        QCOMPARE(processing::fusion::averageCosine(a, b, {"m1", "m2", "m3"}, &used), 0.5);
        QCOMPARE(used, 2);
        // No common model: -1.
        EmbeddingSet c;
        c["m9"] = {1, 0};
        QCOMPARE(processing::fusion::averageCosine(a, c, {"m1", "m2", "m9"}, &used), -1.0);
        QCOMPARE(used, 0);
        QCOMPARE(processing::fusion::averageCosine(a, b, {}), -1.0);
    }

    void cache_v2_roundTripAndApi()
    {
        QTemporaryDir dir;
        processing::UtteranceEmbeddingCache c;
        c.fingerprint = QStringLiteral("fp1");
        QVERIFY(c.isEmpty());
        c.set("campplus", "u1", {1, 0, 0});
        c.set("campplus", "u2", {});            // tried, did not succeed
        c.set("other", "u1", {0, 1, 0, 0});
        QVERIFY(!c.isEmpty());
        QVERIFY(c.has("campplus", "u2"));
        QVERIFY(!c.has("other", "u2"));
        QCOMPARE(c.missingFor("other", {"u1", "u2", "u3"}), QStringList({"u2", "u3"}));
        QCOMPARE(c.missingFor("nincs", {"u1"}), QStringList{"u1"});
        QCOMPARE(c.setFor("u1", {"campplus", "other", "nincs"}).keys(), QStringList({"campplus", "other"}));
        QVERIFY(c.setFor("u2", {"campplus"}).isEmpty());   // the empty vector does not go into the set
        QVERIFY(c.save(dir.path()));

        const processing::UtteranceEmbeddingCache back = processing::UtteranceEmbeddingCache::load(dir.path(), "fp1");
        QCOMPARE(back.models.keys(), QStringList({"campplus", "other"}));
        QCOMPARE(back.vectors("campplus").value("u1"), (QVector<float>{1, 0, 0}));
        QVERIFY(back.vectors("campplus").contains("u2"));
        QCOMPARE(back.vectors("other").value("u1"), (QVector<float>{0, 1, 0, 0}));
        QVERIFY(back.vectors("nincs").isEmpty());

        // Another transcript: an empty cache.
        QVERIFY(processing::UtteranceEmbeddingCache::load(dir.path(), "fp2").isEmpty());
        processing::UtteranceEmbeddingCache::remove(dir.path());
        QVERIFY(!QFile::exists(processing::UtteranceEmbeddingCache::filePath(dir.path())));
    }

    void cache_v1FileDiscarded()
    {
        QTemporaryDir dir;
        {
            QFile f(processing::UtteranceEmbeddingCache::filePath(dir.path()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QDataStream out(&f);
            out.setVersion(QDataStream::Qt_6_0);
            out.setFloatingPointPrecision(QDataStream::SinglePrecision);
            QHash<QString, QVector<float>> v1;
            v1.insert("u1", {1, 0, 0});
            out << quint32(0x54454d42) << quint32(1) << QStringLiteral("fp") << v1;
        }
        const processing::UtteranceEmbeddingCache c = processing::UtteranceEmbeddingCache::load(dir.path(), "fp");
        QVERIFY(c.isEmpty());
        QCOMPARE(c.fingerprint, QStringLiteral("fp"));
        QCOMPARE(c.missingFor("campplus", {"u1"}), QStringList{"u1"});
    }

    // The base embedAll: the single-model (fake) embedder gives under the default model's key.
    void defaultEmbedAll_mapsToDefaultModel()
    {
        struct One : processing::IUtteranceEmbedder {
            bool open(const QString&) override { return true; }
            QVector<float> embed(qint64, qint64) override { return {1, 2}; }
        } one;
        const EmbeddingSet s = one.embedAll(0, 1000, {"campplus", "other"});
        QCOMPARE(s.keys(), QStringList{"campplus"});
        QCOMPARE(s.value("campplus"), (QVector<float>{1, 2}));
        QVERIFY(one.embedAll(0, 1000, {"other"}).isEmpty());
    }

    // Real ONNX: two model entries for the same file give both from one decode, and the two
    // vectors are identical. A model with a missing file has no key in the result.
    void realModel_multiModelOneDecode()
    {
        const QString model = campplusModel();
        if (!QFileInfo::exists(model)) QSKIP("Nincs CAM++ modellfájl.");
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) QSKIP("Nincs ffmpeg.");
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("a.wav"));
        QVERIFY(writeWav(wav, 4));

        const processing::EmbedderConfig cfg;
        auto emb = voiceUtteranceEmbedderFactory(QVector<processing::UtteranceModel>{
            {"a", model, cfg}, {"b", model, cfg}, {"missing", dir.filePath("nincs.onnx"), cfg}}, media::FfmpegMediaTool::fromSettings({}))();
        if (!emb->open(wav)) QSKIP(qPrintable(QStringLiteral("Voice-ID nélküli build vagy modellhiba: ") + emb->lastError()));
        const EmbeddingSet s = emb->embedAll(500, 3500, {"a", "b", "missing"});
        QCOMPARE(s.keys(), QStringList({"a", "b"}));
        QCOMPARE(s.value("a").size(), 192);
        QVERIFY(cosine(s.value("a"), s.value("b")) > 0.9999);
        // Only the requested models; a slice under half a second gives an empty vector, but the key is there.
        QCOMPARE(emb->embedAll(500, 3500, {"b"}).keys(), QStringList{"b"});
        const EmbeddingSet tiny = emb->embedAll(0, 200, {"a"});
        QVERIFY(tiny.contains("a") && tiny.value("a").isEmpty());

        // The single-model factory gives the same under the default model's key.
        auto single = processing::voiceUtteranceEmbedderFactory(model, media::FfmpegMediaTool::fromSettings({}))();
        QVERIFY(single->open(wav));
        QVERIFY(cosine(single->embed(500, 3500), s.value("a")) > 0.9999);
        QCOMPARE(single->embedAll(500, 3500, {"campplus"}).keys(), QStringList{"campplus"});
    }
};

QTEST_GUILESS_MAIN(VoiceFusionTest)
#include "test_voice_fusion.moc"

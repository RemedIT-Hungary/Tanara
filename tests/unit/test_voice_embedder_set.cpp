// Foundations of multi-model embedding: the built-in model descriptors (license, download,
// fbank parameters), the selection of the models in use (VoiceModelRegistry::active), the
// lazily loading VoiceEmbedderSet with fake "models", and the voice-eval measurement
// (VoiceEval) on made-up vectors. The real ONNX probe runs only if the CAM++ model is there
// (we only read it); otherwise QSKIP.
#include <QtTest>
#include <QTemporaryDir>

#include "fake_media_tool.h"
#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/processing/voiceid/VoiceEmbedder.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/processing/voiceid/VoiceEval.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <cmath>
#include <memory>

using namespace tanara;

namespace {

void touch(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}

// Fake loader: one constant-direction vector per model (scaled by the PCM length; the
// L2-normalized direction is the same). The model with id "bad" cannot be loaded. calls: the number of loads.
processing::PcmEmbedderLoader fakeLoader(std::shared_ptr<int> calls)
{
    return [calls](const processing::VoiceModelSpec& spec, const QString&, QString* error) -> processing::PcmEmbedder {
        ++*calls;
        if (spec.id == QLatin1String("bad")) {
            if (error) *error = QStringLiteral("nem tölthető");
            return {};
        }
        const int dim = spec.dim > 0 ? spec.dim : 3;
        const int hot = int(qHash(spec.id) % uint(dim));
        return [dim, hot](const QVector<float>& pcm) {
            if (pcm.size() < 8000) return QVector<float>();   // "too short"
            QVector<float> v(dim, 0.0f);
            v[hot] = 1.0f;
            return v;
        };
    };
}

processing::VoiceModelSpec fakeSpec(const QString& id, int dim)
{
    processing::VoiceModelSpec s;
    s.id = id;
    s.dim = dim;
    return s;
}

QString realModelPath()
{
    return QDir::home().filePath(QStringLiteral(".tanara/models/campplus_sv_zh_en_16k.onnx"));
}

} // namespace

class VoiceEmbedderSetTest : public QObject {
    Q_OBJECT
private slots:
    void registry_specsAndLicenses()
    {
        const auto camp = processing::VoiceModelRegistry::spec(QStringLiteral("campplus"));
        QVERIFY(camp);
        QCOMPARE(camp->license, QStringLiteral("Apache-2.0"));
        QVERIFY(camp->downloadUrl.startsWith(QStringLiteral(
            "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/")));

        const auto we = processing::VoiceModelRegistry::spec(QStringLiteral("wespeaker-resnet34-lm"));
        QVERIFY(we);
        QCOMPARE(we->fileName, QStringLiteral("wespeaker_en_voxceleb_resnet34_LM.onnx"));
        QCOMPARE(we->dim, 256);
        QCOMPARE(we->license, QStringLiteral("CC-BY-4.0"));
        QCOMPARE(we->sizeBytes, qint64(26530550));
        QVERIFY(we->downloadUrl.endsWith(we->fileName));
        // WeSpeaker infer_onnx.py: int16 scale, hamming, dither 0, CMN, 80 bins.
        QCOMPARE(we->features.waveScale, 32768.0f);
        QCOMPARE(we->features.windowType, QStringLiteral("hamming"));
        QCOMPARE(we->features.dither, 0.0f);
        QVERIFY(we->features.subtractMean);
        QCOMPARE(we->features.numMelBins, 80);

        const auto eres = processing::VoiceModelRegistry::spec(QStringLiteral("eres2netv2"));
        QVERIFY(eres);
        QCOMPARE(eres->dim, 192);
        QCOMPARE(eres->sizeBytes, qint64(71441526));
        QCOMPARE(eres->license, QStringLiteral("Apache-2.0"));
        QCOMPARE(eres->features.windowType, camp->features.windowType);   // like CAM++
        QCOMPARE(eres->features.waveScale, camp->features.waveScale);
        QCOMPARE(camp->features.windowType, QStringLiteral("povey"));
    }

    void registry_activeIsEnabledAndAvailable()
    {
        QTemporaryDir meta;
        const auto we = *processing::VoiceModelRegistry::spec(QStringLiteral("wespeaker-resnet34-lm"));
        const auto camp = *processing::VoiceModelRegistry::spec(QStringLiteral("campplus"));
        const QStringList enabled{"wespeaker-resnet34-lm", "campplus", "nincs-ilyen"};
        QVERIFY(processing::VoiceModelRegistry::active(enabled, meta.path(), QString()).isEmpty());
        touch(QDir(meta.path()).filePath(we.fileName));
        QCOMPARE(processing::VoiceModelRegistry::active(enabled, meta.path(), QString()).size(), 1);
        touch(QDir(meta.path()).filePath(camp.fileName));
        // eres2netv2's file exists but it is not switched on, so it is left out.
        touch(QDir(meta.path()).filePath("3dspeaker_speech_eres2netv2_sv_zh-cn_16k-common.onnx"));
        const auto act = processing::VoiceModelRegistry::active(enabled, meta.path(), QString());
        QCOMPARE(act.size(), 2);
        QCOMPARE(act[0].id, QStringLiteral("campplus"));           // alphabetical order
        QCOMPARE(act[1].id, QStringLiteral("wespeaker-resnet34-lm"));

        const processing::VoiceEmbedderSet set = processing::VoiceEmbedderSet::fromSettings(enabled, meta.path(), QString());
        QCOMPARE(set.modelIds(), QStringList({"campplus", "wespeaker-resnet34-lm"}));
        QVERIFY(!set.isEmpty());
        // Fallback: enabled and available is empty, but campplus exists, so it runs alone.
        QVERIFY(!processing::VoiceEmbedderSet::fromSettings({}, meta.path(), QString()).isEmpty());
        QCOMPARE(processing::VoiceEmbedderSet::fromSettings({}, meta.path(), QString()).modelIds(), QStringList{"campplus"});
        QVERIFY(processing::VoiceEmbedderSet::fromSettings({}, QString("/nincs/ilyen"), QString()).isEmpty());
    }

    void registry_defaultIsWespeakerAndCampplusIsFallback()
    {
        QCOMPARE(processing::VoiceModelRegistry::defaultEnabledIds(), QStringList{"wespeaker-resnet34-lm"});
        QCOMPARE(processing::VoiceModelRegistry::recommendedModelId(), QStringLiteral("wespeaker-resnet34-lm"));
        QCOMPARE(processing::VoiceModelRegistry::defaultModelId(), QStringLiteral("campplus"));   // old paths
        QTemporaryDir meta;
        const QStringList enabled = processing::VoiceModelRegistry::defaultEnabledIds();
        // Nothing is downloaded: no model, no fallback.
        QVERIFY(processing::VoiceModelRegistry::active(enabled, meta.path(), QString()).isEmpty());
        QVERIFY(!processing::VoiceModelRegistry::isFallbackActive(enabled, meta.path(), QString()));
        // Only the campplus file exists: ["campplus"] as a fallback.
        touch(QDir(meta.path()).filePath(processing::VoiceModelRegistry::spec("campplus")->fileName));
        const auto fb = processing::VoiceModelRegistry::active(enabled, meta.path(), QString());
        QCOMPARE(fb.size(), 1);
        QCOMPARE(fb[0].id, QStringLiteral("campplus"));
        QVERIFY(processing::VoiceModelRegistry::isFallbackActive(enabled, meta.path(), QString()));
        // WeSpeaker arrives: it runs alone, the fallback switches off.
        touch(QDir(meta.path()).filePath(processing::VoiceModelRegistry::spec("wespeaker-resnet34-lm")->fileName));
        const auto ok = processing::VoiceModelRegistry::active(enabled, meta.path(), QString());
        QCOMPARE(ok.size(), 1);
        QCOMPARE(ok[0].id, QStringLiteral("wespeaker-resnet34-lm"));
        QVERIFY(!processing::VoiceModelRegistry::isFallbackActive(enabled, meta.path(), QString()));
    }

    void set_lazyLoadAndEmbed()
    {
        auto calls = std::make_shared<int>(0);
        processing::VoiceEmbedderSet set({{fakeSpec("zeta", 4), "/x/z"}, {fakeSpec("alpha", 3), "/x/a"},
                              {fakeSpec("bad", 3), "/x/b"}},
                             fakeLoader(calls));
        QCOMPARE(set.modelIds(), QStringList({"alpha", "bad", "zeta"}));
        QCOMPARE(*calls, 0);                                    // lazy: nothing loaded yet

        const QVector<float> pcm(16000, 0.1f);
        const EmbeddingSet got = set.embedPcm(pcm);
        QCOMPARE(*calls, 3);
        QCOMPARE(got.keys(), QStringList({"alpha", "zeta"}));   // the one that cannot be loaded is left out
        QCOMPARE(got.value("alpha").size(), 3);
        QCOMPARE(got.value("zeta").size(), 4);
        QCOMPARE(set.lastError(), QStringLiteral("nem tölthető"));

        set.embedPcm(pcm);
        QCOMPARE(*calls, 3);                                    // tries to load once per model
        QCOMPARE(set.loadedModelIds(), QStringList({"alpha", "zeta"}));
        QVERIFY(set.ensureLoaded());

        // Too short: the loaded model's key with an empty vector (we tried), the others are left out.
        const EmbeddingSet shortSet = set.embedPcmWith(QVector<float>(100, 0.1f), {"zeta", "bad"});
        QCOMPARE(shortSet.keys(), QStringList({"zeta"}));
        QVERIFY(shortSet.value("zeta").isEmpty());
        QVERIFY(set.embedPcm(QVector<float>(100, 0.1f)).isEmpty());

        // The fresh copy is unloaded (its own instance for the background thread).
        const processing::VoiceEmbedderSet copy = set.freshCopy();
        QCOMPARE(copy.modelIds(), set.modelIds());
        copy.embedPcm(pcm);
        QCOMPARE(*calls, 6);
    }

    void set_onlyBadModels_notUsable()
    {
        auto calls = std::make_shared<int>(0);
        processing::VoiceEmbedderSet set({{fakeSpec("bad", 3), "/x/b"}}, fakeLoader(calls));
        QVERIFY(!set.isEmpty());
        QVERIFY(!set.ensureLoaded());
        QVERIFY(set.embedPcm(QVector<float>(16000, 0.1f)).isEmpty());
        // The real loader on a missing file: cannot be loaded, with an error message.
        processing::VoiceEmbedderSet real({{fakeSpec("campplus", 192), "/nincs/ilyen/modell.onnx"}});
        QVERIFY(!real.ensureLoaded());
        QVERIFY(!real.lastError().isEmpty());
    }

    void utteranceFactory_fromSet_usesAllModels()
    {
        // The factory gives per-model vectors with one decode (fake models, real ffmpeg).
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) QSKIP("nincs ffmpeg");
        QTemporaryDir dir;
        const QString wav = dir.filePath("a.wav");
        QProcess p;
        p.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "sine=frequency=300:duration=3", "-y", wav});
        QVERIFY(p.waitForFinished(30000));
        QVERIFY(QFileInfo::exists(wav));
        auto calls = std::make_shared<int>(0);
        const processing::VoiceEmbedderSet set({{fakeSpec("alpha", 3), "/x/a"}, {fakeSpec("beta", 5), "/x/b"}},
                                   fakeLoader(calls));
        const processing::UtteranceEmbedderFactory f = voiceUtteranceEmbedderFactory(set, media::FfmpegMediaTool::fromSettings({}));
        QVERIFY(f);
        auto e = f();
        QVERIFY(e->open(wav));
        const EmbeddingSet got = e->embedAll(0, 2000, {"beta", "alpha", "gamma"});
        QCOMPARE(got.keys(), QStringList({"alpha", "beta"}));
        QCOMPARE(got.value("beta").size(), 5);
        QVERIFY(!voiceUtteranceEmbedderFactory(processing::VoiceEmbedderSet(), media::FfmpegMediaTool::fromSettings({})));   // an empty set gives an empty factory
    }

    void utteranceFactory_fakeMediaTool()
    {
        // The same without ffmpeg: the media port is a fake (preset PCM), one decode for every model.
        auto tool = std::make_shared<testing::FakeMediaTool>();
        QVector<qint16> pcm(16000 * 3);
        for (int i = 0; i < pcm.size(); ++i) pcm[i] = qint16(8000.0 * std::sin(2.0 * M_PI * 300.0 * i / 16000.0));
        tool->pcm["a.ogg"] = pcm;
        QTemporaryDir dir;
        const QString audio = dir.filePath("a.ogg");
        QFile f(audio);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        auto calls = std::make_shared<int>(0);
        const processing::VoiceEmbedderSet set({{fakeSpec("alpha", 3), "/x/a"}, {fakeSpec("beta", 5), "/x/b"}},
                                   fakeLoader(calls));
        auto e = voiceUtteranceEmbedderFactory(set, tool)();
        QVERIFY(e->open(audio));
        const EmbeddingSet got = e->embedAll(0, 2000, {"beta", "alpha"});
        QCOMPARE(got.keys(), QStringList({"alpha", "beta"}));
        QCOMPARE(got.value("beta").size(), 5);
        QCOMPARE(tool->decodes().size(), 1);
        QCOMPARE(tool->decodes().first().format, media::PcmFormat(16000, 1, media::SampleFormat::S16));
        // An undecodable file: open() fails with an error.
        const QString other = dir.filePath("b.ogg");
        QFile g(other);
        QVERIFY(g.open(QIODevice::WriteOnly));
        g.close();
        auto e2 = voiceUtteranceEmbedderFactory(set, tool)();
        QVERIFY(!e2->open(other));
        QVERIFY(!e2->lastError().isEmpty());
    }

    void realOnnx_campplusDimAndWarning()
    {
        if (!QFileInfo::exists(realModelPath())) QSKIP("a CAM++ modell nincs meg (~/.tanara/models)");
        const auto camp = *processing::VoiceModelRegistry::spec(QStringLiteral("campplus"));
        processing::VoiceEmbedder ok(realModelPath(), camp.features, camp.dim);
        if (!ok.isValid() && ok.lastError().contains(QStringLiteral("TANARA_BUILD_VOICEID")))
            QSKIP("a build nem tartalmaz voice-ID-t");
        QVERIFY2(ok.isValid(), qPrintable(ok.lastError()));
        QVector<float> pcm(16000 * 2);
        for (int i = 0; i < pcm.size(); ++i) pcm[i] = 0.3f * std::sin(i * 0.05f) + 0.05f * std::sin(i * 0.31f);
        const QVector<float> v = ok.embedPcm(pcm);
        QCOMPARE(v.size(), 192);
        QCOMPARE(ok.embeddingDim(), 192);

        // A wrong expected dimension: a warning (at load or at the first inference).
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("output dimension")));
        processing::VoiceEmbedder wrong(realModelPath(), camp.features, 256);
        QVERIFY(wrong.isValid());
        QCOMPARE(wrong.embedPcm(pcm).size(), 192);

        // The hamming window (WeSpeaker setting) runs too, gives another vector.
        processing::EmbedderConfig ham = camp.features;
        ham.windowType = QStringLiteral("hamming");
        processing::VoiceEmbedder h(realModelPath(), ham, 192);
        const QVector<float> hv = h.embedPcm(pcm);
        QCOMPARE(hv.size(), 192);
        QVERIFY(hv != v);
    }

    void eval_coresSplitAndCloser()
    {
        // Two speakers, two models. A: 4 confirmed lines with direction "a" + 1 B-voiced (wrongly on A);
        // B: 3 confirmed lines with direction "b".
        auto set = [](float x, float y) {
            const float n = std::sqrt(x * x + y * y);
            return EmbeddingSet{{"m1", {x / n, y / n}}, {"m2", {x / n, y / n, 0}}};
        };
        QVector<processing::VoiceEvalLine> lines;
        auto add = [&](int spk, bool locked, float x, float y, qint64 dur = 4000) {
            processing::VoiceEvalLine l;
            l.id = QStringLiteral("u%1").arg(lines.size());
            l.durationMs = dur;
            l.speaker = spk;
            l.locked = locked;
            if (dur >= 1500) l.vectors = set(x, y);
            lines.append(l);
        };
        for (int i = 0; i < 4; ++i) add(0, true, 1, 0.05f * i);
        add(0, false, 0, 1);              // B's voice on A
        for (int i = 0; i < 3; ++i) add(1, true, 0.05f * i, 1);
        add(1, false, 0, 1, 800);         // too short: no vector
        const QVector<processing::VoiceEvalSpeaker> spk{{"Beszélő 1", "Anna"}, {"Beszélő 2", "Béla"}};

        const processing::VoiceEvalReport r = evaluateVoices(lines, spk, {"m1", "m2"}, 1500);
        QCOMPARE(r.spaces.size(), 3);
        QCOMPARE(r.spaces[2].id, QStringLiteral("fused"));
        for (const processing::VoiceEvalSpace& sp : r.spaces) {
            QCOMPARE(sp.linesTotal, 9);
            QCOMPARE(sp.linesEligible, 8);
            QCOMPARE(sp.linesCovered, 8);
            QCOMPARE(sp.coreLines, QVector<int>({4, 3}));
            QVERIFY(sp.coreCosine[0][1] < 0.2);
            QVERIFY(std::abs(sp.coreCosine[0][0] - 1.0) < 1e-6);
            // The speaker's 5 lines: 4 + 1 split apart, the two sub-centroids are far apart.
            QCOMPARE(sp.splits[0].lines, 5);
            QCOMPARE(sp.splits[0].sizeA, 4);
            QCOMPARE(sp.splits[0].sizeB, 1);
            QVERIFY(sp.splits[0].centroidCosine < 0.2);
            QVERIFY(std::isnan(sp.splits[1].centroidCosine));   // 3 lines: too few for 2-means
            QCOMPARE(sp.closerToOther, 1);                      // the B-voiced line on A
            // The core's members are measured without themselves; one removed from B's 3-member core is too few, so left out.
            QCOMPARE(sp.closerChecked, 5);
        }
        const QJsonObject j = voiceEvalToJson(r);
        QCOMPARE(j.value("spaces").toArray().size(), 3);
        QCOMPARE(j.value("speakers").toArray().at(1).toObject().value("name").toString(), QStringLiteral("Béla"));

        // One model: no fusion space; a speaker without a core gives NaN in the matrix.
        for (processing::VoiceEvalLine& l : lines) l.locked = l.speaker == 0 && l.locked;
        const processing::VoiceEvalReport one = evaluateVoices(lines, spk, {"m1"}, 1500);
        QCOMPARE(one.spaces.size(), 1);
        QCOMPARE(one.spaces[0].coreLines, QVector<int>({4, 0}));
        QVERIFY(std::isnan(one.spaces[0].coreCosine[0][1]));
    }
};

QTEST_GUILESS_MAIN(VoiceEmbedderSetTest)
#include "test_voice_embedder_set.moc"

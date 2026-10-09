//
// A több modelles beágyazás alapjai: a beépített modell-leírók (licenc, letöltés, fbank-
// paraméterek), a használt modellek kiválasztása (VoiceModelRegistry::active), a lustán töltő
// VoiceEmbedderSet hamis „modellekkel”, és a voice-eval mérés (VoiceEval) kitalált vektorokon.
// A valódi ONNX-próba csak akkor fut, ha a CAM++ modell megvan (csak olvassuk); különben QSKIP.
//
#include <QtTest>
#include <QTemporaryDir>

#include "tanara/edit/UtteranceEmbeddings.h"
#include "tanara/voiceid/VoiceEmbedder.h"
#include "tanara/voiceid/VoiceEmbedderSet.h"
#include "tanara/voiceid/VoiceEval.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

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

// Hamis betöltő: modellenként egy állandó irányú vektor (a PCM hosszával skálázva — az
// L2-normalizált irány ugyanaz). A "bad" id-jű modell nem tölthető be. calls: betöltések száma.
PcmEmbedderLoader fakeLoader(std::shared_ptr<int> calls)
{
    return [calls](const VoiceModelSpec& spec, const QString&, QString* error) -> PcmEmbedder {
        ++*calls;
        if (spec.id == QLatin1String("bad")) {
            if (error) *error = QStringLiteral("nem tölthető");
            return {};
        }
        const int dim = spec.dim > 0 ? spec.dim : 3;
        const int hot = int(qHash(spec.id) % uint(dim));
        return [dim, hot](const QVector<float>& pcm) {
            if (pcm.size() < 8000) return QVector<float>();   // „túl rövid”
            QVector<float> v(dim, 0.0f);
            v[hot] = 1.0f;
            return v;
        };
    };
}

VoiceModelSpec fakeSpec(const QString& id, int dim)
{
    VoiceModelSpec s;
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
        const auto camp = VoiceModelRegistry::spec(QStringLiteral("campplus"));
        QVERIFY(camp);
        QCOMPARE(camp->license, QStringLiteral("Apache-2.0"));
        QVERIFY(camp->downloadUrl.startsWith(QStringLiteral(
            "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/")));

        const auto we = VoiceModelRegistry::spec(QStringLiteral("wespeaker-resnet34-lm"));
        QVERIFY(we);
        QCOMPARE(we->fileName, QStringLiteral("wespeaker_en_voxceleb_resnet34_LM.onnx"));
        QCOMPARE(we->dim, 256);
        QCOMPARE(we->license, QStringLiteral("CC-BY-4.0"));
        QCOMPARE(we->sizeBytes, qint64(26530550));
        QVERIFY(we->downloadUrl.endsWith(we->fileName));
        // WeSpeaker infer_onnx.py: int16-skála, hamming, dither 0, CMN, 80 bin.
        QCOMPARE(we->features.waveScale, 32768.0f);
        QCOMPARE(we->features.windowType, QStringLiteral("hamming"));
        QCOMPARE(we->features.dither, 0.0f);
        QVERIFY(we->features.subtractMean);
        QCOMPARE(we->features.numMelBins, 80);

        const auto eres = VoiceModelRegistry::spec(QStringLiteral("eres2netv2"));
        QVERIFY(eres);
        QCOMPARE(eres->dim, 192);
        QCOMPARE(eres->sizeBytes, qint64(71441526));
        QCOMPARE(eres->license, QStringLiteral("Apache-2.0"));
        QCOMPARE(eres->features.windowType, camp->features.windowType);   // mint a CAM++
        QCOMPARE(eres->features.waveScale, camp->features.waveScale);
        QCOMPARE(camp->features.windowType, QStringLiteral("povey"));
    }

    void registry_activeIsEnabledAndAvailable()
    {
        QTemporaryDir meta;
        const auto we = *VoiceModelRegistry::spec(QStringLiteral("wespeaker-resnet34-lm"));
        const auto camp = *VoiceModelRegistry::spec(QStringLiteral("campplus"));
        const QStringList enabled{"wespeaker-resnet34-lm", "campplus", "nincs-ilyen"};
        QVERIFY(VoiceModelRegistry::active(enabled, meta.path(), QString()).isEmpty());
        touch(QDir(meta.path()).filePath("models/" + we.fileName));
        QCOMPARE(VoiceModelRegistry::active(enabled, meta.path(), QString()).size(), 1);
        touch(QDir(meta.path()).filePath("models/" + camp.fileName));
        // eres2netv2 fájlja megvan, de nincs bekapcsolva → kimarad.
        touch(QDir(meta.path()).filePath("models/3dspeaker_speech_eres2netv2_sv_zh-cn_16k-common.onnx"));
        const auto act = VoiceModelRegistry::active(enabled, meta.path(), QString());
        QCOMPARE(act.size(), 2);
        QCOMPARE(act[0].id, QStringLiteral("campplus"));           // ábécérend
        QCOMPARE(act[1].id, QStringLiteral("wespeaker-resnet34-lm"));

        const VoiceEmbedderSet set = VoiceEmbedderSet::fromSettings(enabled, meta.path(), QString());
        QCOMPARE(set.modelIds(), QStringList({"campplus", "wespeaker-resnet34-lm"}));
        QVERIFY(!set.isEmpty());
        QVERIFY(VoiceEmbedderSet::fromSettings({}, meta.path(), QString()).isEmpty());
    }

    void set_lazyLoadAndEmbed()
    {
        auto calls = std::make_shared<int>(0);
        VoiceEmbedderSet set({{fakeSpec("zeta", 4), "/x/z"}, {fakeSpec("alpha", 3), "/x/a"},
                              {fakeSpec("bad", 3), "/x/b"}},
                             fakeLoader(calls));
        QCOMPARE(set.modelIds(), QStringList({"alpha", "bad", "zeta"}));
        QCOMPARE(*calls, 0);                                    // lusta: még semmi sem töltődött

        const QVector<float> pcm(16000, 0.1f);
        const EmbeddingSet got = set.embedPcm(pcm);
        QCOMPARE(*calls, 3);
        QCOMPARE(got.keys(), QStringList({"alpha", "zeta"}));   // a be nem tölthető kimarad
        QCOMPARE(got.value("alpha").size(), 3);
        QCOMPARE(got.value("zeta").size(), 4);
        QCOMPARE(set.lastError(), QStringLiteral("nem tölthető"));

        set.embedPcm(pcm);
        QCOMPARE(*calls, 3);                                    // egyszer próbál betölteni modellenként
        QCOMPARE(set.loadedModelIds(), QStringList({"alpha", "zeta"}));
        QVERIFY(set.ensureLoaded());

        // Túl rövid: a betöltött modell kulcsa üres vektorral (megpróbáltuk), a többi kimarad.
        const EmbeddingSet shortSet = set.embedPcmWith(QVector<float>(100, 0.1f), {"zeta", "bad"});
        QCOMPARE(shortSet.keys(), QStringList({"zeta"}));
        QVERIFY(shortSet.value("zeta").isEmpty());
        QVERIFY(set.embedPcm(QVector<float>(100, 0.1f)).isEmpty());

        // A friss másolat betöltetlen (saját példány a háttérszálnak).
        const VoiceEmbedderSet copy = set.freshCopy();
        QCOMPARE(copy.modelIds(), set.modelIds());
        copy.embedPcm(pcm);
        QCOMPARE(*calls, 6);
    }

    void set_onlyBadModels_notUsable()
    {
        auto calls = std::make_shared<int>(0);
        VoiceEmbedderSet set({{fakeSpec("bad", 3), "/x/b"}}, fakeLoader(calls));
        QVERIFY(!set.isEmpty());
        QVERIFY(!set.ensureLoaded());
        QVERIFY(set.embedPcm(QVector<float>(16000, 0.1f)).isEmpty());
        // A valódi betöltő hiányzó fájlra: nem tölthető, hibaüzenettel.
        VoiceEmbedderSet real({{fakeSpec("campplus", 192), "/nincs/ilyen/modell.onnx"}});
        QVERIFY(!real.ensureLoaded());
        QVERIFY(!real.lastError().isEmpty());
    }

    void utteranceFactory_fromSet_usesAllModels()
    {
        // A gyár egy dekódolással ad modellenkénti vektort (hamis modellek, valódi ffmpeg).
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) QSKIP("nincs ffmpeg");
        QTemporaryDir dir;
        const QString wav = dir.filePath("a.wav");
        QProcess p;
        p.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "sine=frequency=300:duration=3", "-y", wav});
        QVERIFY(p.waitForFinished(30000));
        QVERIFY(QFileInfo::exists(wav));
        auto calls = std::make_shared<int>(0);
        const VoiceEmbedderSet set({{fakeSpec("alpha", 3), "/x/a"}, {fakeSpec("beta", 5), "/x/b"}},
                                   fakeLoader(calls));
        const UtteranceEmbedderFactory f = voiceUtteranceEmbedderFactory(set);
        QVERIFY(f);
        auto e = f();
        QVERIFY(e->open(wav));
        const EmbeddingSet got = e->embedAll(0, 2000, {"beta", "alpha", "gamma"});
        QCOMPARE(got.keys(), QStringList({"alpha", "beta"}));
        QCOMPARE(got.value("beta").size(), 5);
        QVERIFY(!voiceUtteranceEmbedderFactory(VoiceEmbedderSet()));   // üres készlet → üres gyár
    }

    void realOnnx_campplusDimAndWarning()
    {
        if (!QFileInfo::exists(realModelPath())) QSKIP("a CAM++ modell nincs meg (~/.tanara/models)");
        const auto camp = *VoiceModelRegistry::spec(QStringLiteral("campplus"));
        VoiceEmbedder ok(realModelPath(), camp.features, camp.dim);
        if (!ok.isValid() && ok.lastError().contains(QStringLiteral("TANARA_BUILD_VOICEID")))
            QSKIP("a build nem tartalmaz voice-ID-t");
        QVERIFY2(ok.isValid(), qPrintable(ok.lastError()));
        QVector<float> pcm(16000 * 2);
        for (int i = 0; i < pcm.size(); ++i) pcm[i] = 0.3f * std::sin(i * 0.05f) + 0.05f * std::sin(i * 0.31f);
        const QVector<float> v = ok.embedPcm(pcm);
        QCOMPARE(v.size(), 192);
        QCOMPARE(ok.embeddingDim(), 192);

        // Rossz várt dimenzió → figyelmeztetés (betöltéskor vagy az első inferenciánál).
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("dimenzi")));
        VoiceEmbedder wrong(realModelPath(), camp.features, 256);
        QVERIFY(wrong.isValid());
        QCOMPARE(wrong.embedPcm(pcm).size(), 192);

        // A hamming ablak (WeSpeaker-beállítás) is fut, más vektort ad.
        EmbedderConfig ham = camp.features;
        ham.windowType = QStringLiteral("hamming");
        VoiceEmbedder h(realModelPath(), ham, 192);
        const QVector<float> hv = h.embedPcm(pcm);
        QCOMPARE(hv.size(), 192);
        QVERIFY(hv != v);
    }

    void eval_coresSplitAndCloser()
    {
        // Két beszélő, két modell. A: 4 megerősített sor az „a” iránnyal + 1 B-hangú (tévesen A-n);
        // B: 3 megerősített sor a „b” iránnyal.
        auto set = [](float x, float y) {
            const float n = std::sqrt(x * x + y * y);
            return EmbeddingSet{{"m1", {x / n, y / n}}, {"m2", {x / n, y / n, 0}}};
        };
        QVector<VoiceEvalLine> lines;
        auto add = [&](int spk, bool locked, float x, float y, qint64 dur = 4000) {
            VoiceEvalLine l;
            l.id = QStringLiteral("u%1").arg(lines.size());
            l.durationMs = dur;
            l.speaker = spk;
            l.locked = locked;
            if (dur >= 1500) l.vectors = set(x, y);
            lines.append(l);
        };
        for (int i = 0; i < 4; ++i) add(0, true, 1, 0.05f * i);
        add(0, false, 0, 1);              // B hangja A-n
        for (int i = 0; i < 3; ++i) add(1, true, 0.05f * i, 1);
        add(1, false, 0, 1, 800);         // túl rövid: nincs vektor
        const QVector<VoiceEvalSpeaker> spk{{"Beszélő 1", "Anna"}, {"Beszélő 2", "Béla"}};

        const VoiceEvalReport r = evaluateVoices(lines, spk, {"m1", "m2"}, 1500);
        QCOMPARE(r.spaces.size(), 3);
        QCOMPARE(r.spaces[2].id, QStringLiteral("fused"));
        for (const VoiceEvalSpace& sp : r.spaces) {
            QCOMPARE(sp.linesTotal, 9);
            QCOMPARE(sp.linesEligible, 8);
            QCOMPARE(sp.linesCovered, 8);
            QCOMPARE(sp.coreLines, QVector<int>({4, 3}));
            QVERIFY(sp.coreCosine[0][1] < 0.2);
            QVERIFY(std::abs(sp.coreCosine[0][0] - 1.0) < 1e-6);
            // A beszélő 5 sora: 4 + 1 szétválik, a két al-centroid távoli.
            QCOMPARE(sp.splits[0].lines, 5);
            QCOMPARE(sp.splits[0].sizeA, 4);
            QCOMPARE(sp.splits[0].sizeB, 1);
            QVERIFY(sp.splits[0].centroidCosine < 0.2);
            QVERIFY(std::isnan(sp.splits[1].centroidCosine));   // 3 sor: kevés a 2-középhez
            QCOMPARE(sp.closerToOther, 1);                      // a B-hangú sor A-n
            // A mag tagjai önmaguk nélkül mérődnek; B 3 tagú magjából egy kivéve kevés → kimarad.
            QCOMPARE(sp.closerChecked, 5);
        }
        const QJsonObject j = voiceEvalToJson(r);
        QCOMPARE(j.value("spaces").toArray().size(), 3);
        QCOMPARE(j.value("speakers").toArray().at(1).toObject().value("name").toString(), QStringLiteral("Béla"));

        // Egy modell: nincs fúziós tér; mag nélküli beszélő → NaN a mátrixban.
        for (VoiceEvalLine& l : lines) l.locked = l.speaker == 0 && l.locked;
        const VoiceEvalReport one = evaluateVoices(lines, spk, {"m1"}, 1500);
        QCOMPARE(one.spaces.size(), 1);
        QCOMPARE(one.spaces[0].coreLines, QVector<int>({4, 0}));
        QVERIFY(std::isnan(one.spaces[0].coreCosine[0][1]));
    }
};

QTEST_GUILESS_MAIN(VoiceEmbedderSetTest)
#include "test_voice_embedder_set.moc"

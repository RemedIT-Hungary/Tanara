#include "tanara/processing/voiceid/VoiceEmbedder.h"

#include "tanara/foundation/log/Logging.h"

#include <QFileInfo>

#include <algorithm>
#include <cstring>
#include <vector>

// The speaker embedding (fbank + ONNX) is compiled only if voice ID is enabled
// (TANARA_BUILD_VOICEID=ON -> TANARA_HAVE_VOICEID). Otherwise a stub: isValid()==false, an empty
// embedding, so AppController/CLI automatically skip speaker labeling.
// decodePcm16kMono (the media port) is available in BOTH builds.
#ifdef TANARA_HAVE_VOICEID
#include "tanara/library/store/VoiceprintStore.h"   // l2normalize
#include "kaldi-native-fbank/csrc/online-feature.h"

#include "OnnxRuntimeInclude.h"

#include <string>
#include <cmath>
#include <mutex>
#include <array>
#endif

namespace tanara::processing {

#ifdef TANARA_HAVE_VOICEID

struct VoiceEmbedder::Impl {
    EmbedderConfig cfg;
    QString error;
    QString path;
    mutable int dim = 0;
    int staticDim = 0;            // last dimension of the output from the model (0 = dynamic)
    int expectedDim = 0;          // per the descriptor (0 = not checked)
    mutable bool dimWarned = false;

    void checkDim(int got) const {
        if (expectedDim <= 0 || got <= 0 || got == expectedDim || dimWarned) return;
        dimWarned = true;
        qCWarning(lcVoice).noquote() << "Hangmodell: a kimenet dimenziója" << got
                                     << "eltér a várt" << expectedDim << "értéktől:" << path;
    }

    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "tanara-voiceid"};
    std::unique_ptr<Ort::Session> session;
    std::string inputName;
    std::string outputName;
    mutable std::mutex runMutex;   // Session::Run is not necessarily thread-safe for several calls

    explicit Impl(const EmbedderConfig& c) : cfg(c) {}
};

VoiceEmbedder::VoiceEmbedder(const QString& modelPath, const EmbedderConfig& cfg, int expectedDim)
    : d(std::make_unique<Impl>(cfg)) {
    d->path = modelPath;
    d->expectedDim = expectedDim;
    if (!QFileInfo::exists(modelPath)) {
        d->error = QStringLiteral("Hiányzó modell: %1").arg(modelPath);
        return;
    }
    try {
        Ort::SessionOptions so;
        so.SetIntraOpNumThreads(std::max(1, cfg.intraOpThreads));
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // ORT expects the model path as ORTCHAR_T* (wide on Windows), see ortPath().
        const auto modelPathOrt = ortPath(modelPath);
        d->session = std::make_unique<Ort::Session>(
            d->env, modelPathOrt.c_str(), so);

        Ort::AllocatorWithDefaultOptions alloc;
        d->inputName  = d->session->GetInputNameAllocated(0, alloc).get();
        d->outputName = d->session->GetOutputNameAllocated(0, alloc).get();
        const std::vector<int64_t> outShape =
            d->session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (!outShape.empty() && outShape.back() > 0)
            d->staticDim = static_cast<int>(outShape.back());
        d->checkDim(d->staticDim);
    } catch (const std::exception& e) {
        d->error = QStringLiteral("ONNX betöltés hiba: %1").arg(QString::fromUtf8(e.what()));
        d->session.reset();
    }
}

VoiceEmbedder::~VoiceEmbedder() = default;

bool VoiceEmbedder::isValid() const { return d->session != nullptr; }
QString VoiceEmbedder::lastError() const { return d->error; }
int VoiceEmbedder::embeddingDim() const { return d->dim; }
int VoiceEmbedder::outputDim() const { return d->staticDim; }
EmbedderConfig VoiceEmbedder::config() const { return d->cfg; }

QVector<float> VoiceEmbedder::embedPcm(const QVector<float>& mono16k) const {
    if (!d->session) return {};
    if (mono16k.size() < d->cfg.sampleRate / 2) {   // <0.5 s: unreliable
        d->error = QStringLiteral("Túl rövid hangminta az embeddinghez.");
        return {};
    }

    // --- fbank (kaldi-native-fbank) ---
    knf::FbankOptions opts;
    opts.frame_opts.samp_freq   = static_cast<float>(d->cfg.sampleRate);
    opts.frame_opts.dither      = d->cfg.dither;
    opts.frame_opts.snip_edges  = d->cfg.snipEdges;
    opts.frame_opts.window_type = d->cfg.windowType.toStdString();
    opts.mel_opts.num_bins      = d->cfg.numMelBins;

    knf::OnlineFbank fbank(opts);
    std::vector<float> scaled(mono16k.size());
    for (int i = 0; i < mono16k.size(); ++i)
        scaled[i] = mono16k[i] * d->cfg.waveScale;
    fbank.AcceptWaveform(static_cast<float>(d->cfg.sampleRate),
                         scaled.data(), static_cast<int32_t>(scaled.size()));
    fbank.InputFinished();

    const int T = fbank.NumFramesReady();
    const int D = d->cfg.numMelBins;
    if (T <= 0) {
        d->error = QStringLiteral("Nem keletkezett fbank-keret.");
        return {};
    }

    std::vector<float> feats(static_cast<size_t>(T) * D);
    for (int t = 0; t < T; ++t) {
        const float* f = fbank.GetFrame(t);
        std::copy(f, f + D, feats.begin() + static_cast<size_t>(t) * D);
    }

    // --- CMN: subtracting the per-bin mean over time ---
    if (d->cfg.subtractMean) {
        std::vector<double> mean(D, 0.0);
        for (int t = 0; t < T; ++t)
            for (int j = 0; j < D; ++j)
                mean[j] += feats[static_cast<size_t>(t) * D + j];
        for (int j = 0; j < D; ++j)
            mean[j] /= T;
        for (int t = 0; t < T; ++t)
            for (int j = 0; j < D; ++j)
                feats[static_cast<size_t>(t) * D + j] -= static_cast<float>(mean[j]);
    }

    // --- ONNX inferencia: input [1, T, D] ---
    QVector<float> embedding;
    try {
        std::lock_guard<std::mutex> lock(d->runMutex);
        Ort::MemoryInfo memInfo =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 3> shape{1, T, D};
        Ort::Value input = Ort::Value::CreateTensor<float>(
            memInfo, feats.data(), feats.size(), shape.data(), shape.size());

        const char* inNames[]  = {d->inputName.c_str()};
        const char* outNames[] = {d->outputName.c_str()};
        auto outputs = d->session->Run(Ort::RunOptions{nullptr},
                                       inNames, &input, 1, outNames, 1);
        if (outputs.empty()) {
            d->error = QStringLiteral("Üres ONNX-kimenet.");
            return {};
        }
        const float* out = outputs[0].GetTensorData<float>();
        const auto info = outputs[0].GetTensorTypeAndShapeInfo();
        const size_t n = info.GetElementCount();
        embedding.reserve(static_cast<int>(n));
        for (size_t i = 0; i < n; ++i)
            embedding.append(out[i]);
        d->dim = static_cast<int>(n);
        d->checkDim(d->dim);
    } catch (const std::exception& e) {
        d->error = QStringLiteral("ONNX inferencia hiba: %1").arg(QString::fromUtf8(e.what()));
        return {};
    }

    return library::VoiceprintStore::l2normalize(embedding);
}

QVector<float> VoiceEmbedder::embedFile(const media::MediaTool& media, const QString& audioPath,
                                        qint64 startMs, qint64 endMs) const {
    const QVector<float> pcm = decodePcm16kMono(media, audioPath, startMs, endMs);
    if (pcm.isEmpty()) {
        d->error = QStringLiteral("ffmpeg dekódolás sikertelen: %1").arg(audioPath);
        return {};
    }
    return embedPcm(pcm);
}

#else  // !TANARA_HAVE_VOICEID: a stub (no ONNX/fbank in the build)

struct VoiceEmbedder::Impl {
    EmbedderConfig cfg;
    QString error;
    int dim = 0;
    explicit Impl(const EmbedderConfig& c) : cfg(c) {}
};

VoiceEmbedder::VoiceEmbedder(const QString& /*modelPath*/, const EmbedderConfig& cfg,
                             int /*expectedDim*/)
    : d(std::make_unique<Impl>(cfg)) {
    d->error = QStringLiteral(
        "A beszélő-azonosítás ki van kapcsolva ebben a buildben (TANARA_BUILD_VOICEID=OFF).");
}

VoiceEmbedder::~VoiceEmbedder() = default;

bool VoiceEmbedder::isValid() const { return false; }
QString VoiceEmbedder::lastError() const { return d->error; }
int VoiceEmbedder::embeddingDim() const { return 0; }
int VoiceEmbedder::outputDim() const { return 0; }
EmbedderConfig VoiceEmbedder::config() const { return d->cfg; }

QVector<float> VoiceEmbedder::embedPcm(const QVector<float>& /*mono16k*/) const { return {}; }

QVector<float> VoiceEmbedder::embedFile(const media::MediaTool& /*media*/, const QString& /*audioPath*/,
                                        qint64 /*startMs*/, qint64 /*endMs*/) const { return {}; }

#endif // TANARA_HAVE_VOICEID

// --- Shared (both builds): audio-file segment -> 16 kHz mono float PCM (the media port) -----
QVector<float> VoiceEmbedder::decodePcm16kMono(const media::MediaTool& media, const QString& audioPath,
                                               qint64 startMs, qint64 endMs) {
    return media.decodeMonoF32(audioPath, startMs, endMs, 16000);
}

} // namespace tanara::processing
#include "tanara/voiceid/VoiceEmbedder.h"
#include "tanara/store/VoiceprintStore.h"   // l2normalize

#include "kaldi-native-fbank/csrc/online-feature.h"
#include <onnxruntime_cxx_api.h>

#include <QProcess>
#include <QFileInfo>

#include <vector>
#include <string>
#include <cmath>
#include <mutex>

namespace tanara {

struct VoiceEmbedder::Impl {
    EmbedderConfig cfg;
    QString error;
    mutable int dim = 0;

    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "tanara-voiceid"};
    std::unique_ptr<Ort::Session> session;
    std::string inputName;
    std::string outputName;
    mutable std::mutex runMutex;   // a Session::Run nem feltétlen szálbiztos több hívásra

    explicit Impl(const EmbedderConfig& c) : cfg(c) {}
};

VoiceEmbedder::VoiceEmbedder(const QString& modelPath, const EmbedderConfig& cfg)
    : d(std::make_unique<Impl>(cfg)) {
    if (!QFileInfo::exists(modelPath)) {
        d->error = QStringLiteral("Hiányzó modell: %1").arg(modelPath);
        return;
    }
    try {
        Ort::SessionOptions so;
        so.SetIntraOpNumThreads(1);
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        d->session = std::make_unique<Ort::Session>(
            d->env, modelPath.toUtf8().constData(), so);

        Ort::AllocatorWithDefaultOptions alloc;
        d->inputName  = d->session->GetInputNameAllocated(0, alloc).get();
        d->outputName = d->session->GetOutputNameAllocated(0, alloc).get();
    } catch (const std::exception& e) {
        d->error = QStringLiteral("ONNX betöltés hiba: %1").arg(QString::fromUtf8(e.what()));
        d->session.reset();
    }
}

VoiceEmbedder::~VoiceEmbedder() = default;

bool VoiceEmbedder::isValid() const { return d->session != nullptr; }
QString VoiceEmbedder::lastError() const { return d->error; }
int VoiceEmbedder::embeddingDim() const { return d->dim; }
EmbedderConfig VoiceEmbedder::config() const { return d->cfg; }

QVector<float> VoiceEmbedder::embedPcm(const QVector<float>& mono16k) const {
    if (!d->session) return {};
    if (mono16k.size() < d->cfg.sampleRate / 2) {   // <0.5s → megbízhatatlan
        d->error = QStringLiteral("Túl rövid hangminta az embeddinghez.");
        return {};
    }

    // --- fbank (kaldi-native-fbank) ---
    knf::FbankOptions opts;
    opts.frame_opts.samp_freq   = static_cast<float>(d->cfg.sampleRate);
    opts.frame_opts.dither      = d->cfg.dither;
    opts.frame_opts.snip_edges  = d->cfg.snipEdges;
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

    // --- CMN: bin-enkénti átlag levonása az idő mentén ---
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
    } catch (const std::exception& e) {
        d->error = QStringLiteral("ONNX inferencia hiba: %1").arg(QString::fromUtf8(e.what()));
        return {};
    }

    return VoiceprintStore::l2normalize(embedding);
}

QVector<float> VoiceEmbedder::decodePcm16kMono(const QString& audioPath,
                                               qint64 startMs, qint64 endMs,
                                               const QString& ffmpegPath) {
    QStringList args;
    args << QStringLiteral("-v") << QStringLiteral("error")
         << QStringLiteral("-i") << audioPath;
    if (startMs > 0)
        args << QStringLiteral("-ss") << QString::number(startMs / 1000.0, 'f', 3);
    if (endMs > startMs)
        args << QStringLiteral("-to") << QString::number(endMs / 1000.0, 'f', 3);
    args << QStringLiteral("-ac") << QStringLiteral("1")
         << QStringLiteral("-ar") << QStringLiteral("16000")
         << QStringLiteral("-f")  << QStringLiteral("f32le")
         << QStringLiteral("-");

    QProcess proc;
    proc.start(ffmpegPath, args);
    if (!proc.waitForStarted(5000))
        return {};
    if (!proc.waitForFinished(120000))
        return {};
    const QByteArray raw = proc.readAllStandardOutput();
    if (raw.isEmpty())
        return {};

    const int n = raw.size() / static_cast<int>(sizeof(float));
    QVector<float> pcm(n);
    std::memcpy(pcm.data(), raw.constData(), static_cast<size_t>(n) * sizeof(float));
    return pcm;
}

QVector<float> VoiceEmbedder::embedFile(const QString& audioPath,
                                        qint64 startMs, qint64 endMs,
                                        const QString& ffmpegPath) const {
    const QVector<float> pcm = decodePcm16kMono(audioPath, startMs, endMs, ffmpegPath);
    if (pcm.isEmpty()) {
        d->error = QStringLiteral("ffmpeg dekódolás sikertelen: %1").arg(audioPath);
        return {};
    }
    return embedPcm(pcm);
}

} // namespace tanara

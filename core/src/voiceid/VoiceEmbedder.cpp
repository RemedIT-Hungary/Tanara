#include "tanara/voiceid/VoiceEmbedder.h"

#include <QProcess>
#include <QFileInfo>

#include <cstring>
#include <vector>

// A beszélő-embedding (fbank + ONNX) csak akkor fordul, ha a voice-ID engedélyezett
// (TANARA_BUILD_VOICEID=ON → TANARA_HAVE_VOICEID). Egyébként stub: isValid()==false,
// üres embedding → az AppController/CLI automatikusan kihagyja a beszélő-címkézést.
// A decodePcm16kMono (tisztán ffmpeg/QProcess) MINDKÉT buildben elérhető.
#ifdef TANARA_HAVE_VOICEID
#include "tanara/store/VoiceprintStore.h"   // l2normalize
#include "kaldi-native-fbank/csrc/online-feature.h"

// --- SAL-shim MinGW-hez --------------------------------------------------------
// Az ONNX Runtime C-API fejléce _WIN32 alatt a <specstrings.h>-ra bízza a SAL2
// annotációkat (_In_, _Outptr_, _Frees_ptr_opt_, …). A MinGW specstrings.h-ja
// régi, és több SAL2-makró hiányzik belőle → definiálatlan tokenek miatt a
// függvénymutató-paraméterek „eltűnnek" (implicit int → fordítási hiba).
// Megoldás: MinGW-n (csak ott, MSVC-n NEM) az include ELŐTT üresként definiáljuk
// pontosan azt a készletet, amit a header a nem-Windows ágban maga is használ.
// (#ifndef-guard → a specstrings.h már meglévő definíciói nem ütköznek.)
#if defined(_WIN32) && !defined(_MSC_VER)
#  ifndef _In_
#    define _In_
#  endif
#  ifndef _In_z_
#    define _In_z_
#  endif
#  ifndef _In_opt_
#    define _In_opt_
#  endif
#  ifndef _In_opt_z_
#    define _In_opt_z_
#  endif
#  ifndef _Out_
#    define _Out_
#  endif
#  ifndef _Outptr_
#    define _Outptr_
#  endif
#  ifndef _Out_opt_
#    define _Out_opt_
#  endif
#  ifndef _Inout_
#    define _Inout_
#  endif
#  ifndef _Inout_opt_
#    define _Inout_opt_
#  endif
#  ifndef _Frees_ptr_opt_
#    define _Frees_ptr_opt_
#  endif
#  ifndef _Ret_maybenull_
#    define _Ret_maybenull_
#  endif
#  ifndef _Ret_notnull_
#    define _Ret_notnull_
#  endif
#  ifndef _Check_return_
#    define _Check_return_
#  endif
#  ifndef _Outptr_result_maybenull_
#    define _Outptr_result_maybenull_
#  endif
#  ifndef _In_reads_
#    define _In_reads_(X)
#  endif
#  ifndef _Inout_updates_
#    define _Inout_updates_(X)
#  endif
#  ifndef _Out_writes_
#    define _Out_writes_(X)
#  endif
#  ifndef _Inout_updates_all_
#    define _Inout_updates_all_(X)
#  endif
#  ifndef _Out_writes_bytes_all_
#    define _Out_writes_bytes_all_(X)
#  endif
#  ifndef _Out_writes_all_
#    define _Out_writes_all_(X)
#  endif
#  ifndef _Success_
#    define _Success_(X)
#  endif
#  ifndef _Outptr_result_buffer_maybenull_
#    define _Outptr_result_buffer_maybenull_(X)
#  endif
#endif  // _WIN32 && !_MSC_VER

#include <onnxruntime_cxx_api.h>

#include <string>
#include <cmath>
#include <mutex>
#include <array>
#endif

namespace tanara {

#ifdef TANARA_HAVE_VOICEID

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
        // Az ORT a modell-útvonalat ORTCHAR_T*-ként várja: Windowson wchar_t
        // (wide path), POSIX-on char. Ennek megfelelően alakítjuk a QString-et.
#if defined(_WIN32)
        const std::wstring modelPathOrt = modelPath.toStdWString();
#else
        const std::string modelPathOrt = modelPath.toUtf8().toStdString();
#endif
        d->session = std::make_unique<Ort::Session>(
            d->env, modelPathOrt.c_str(), so);

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

#else  // !TANARA_HAVE_VOICEID — stub (nincs ONNX/fbank a buildben)

struct VoiceEmbedder::Impl {
    EmbedderConfig cfg;
    QString error;
    int dim = 0;
    explicit Impl(const EmbedderConfig& c) : cfg(c) {}
};

VoiceEmbedder::VoiceEmbedder(const QString& /*modelPath*/, const EmbedderConfig& cfg)
    : d(std::make_unique<Impl>(cfg)) {
    d->error = QStringLiteral(
        "A beszélő-azonosítás ki van kapcsolva ebben a buildben (TANARA_BUILD_VOICEID=OFF).");
}

VoiceEmbedder::~VoiceEmbedder() = default;

bool VoiceEmbedder::isValid() const { return false; }
QString VoiceEmbedder::lastError() const { return d->error; }
int VoiceEmbedder::embeddingDim() const { return 0; }
EmbedderConfig VoiceEmbedder::config() const { return d->cfg; }

QVector<float> VoiceEmbedder::embedPcm(const QVector<float>& /*mono16k*/) const { return {}; }

QVector<float> VoiceEmbedder::embedFile(const QString& /*audioPath*/,
                                        qint64 /*startMs*/, qint64 /*endMs*/,
                                        const QString& /*ffmpegPath*/) const { return {}; }

#endif // TANARA_HAVE_VOICEID

// --- Közös (mindkét build): hangfájl-szegmens → 16 kHz mono float PCM (ffmpeg) -----
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

} // namespace tanara

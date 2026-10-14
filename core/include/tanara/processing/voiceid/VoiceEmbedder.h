#pragma once
//
// VoiceEmbedder: speaker-embedding extraction: 16 kHz mono PCM -> fbank (kaldi-native-fbank)
// -> ONNX model (3D-Speaker CAM++) -> an L2-normalized embedding vector. UI-independent (core).
// The fbank/normalization parameters are tunable (empirically tuned for an exact kaldi match).

#include "tanara/media/MediaTool.h"

#include <QString>
#include <QVector>
#include <memory>

namespace tanara::processing {

// Tunable parameters of the feature extraction (fitted to the model).
struct EmbedderConfig {
    int   numMelBins = 80;
    int   sampleRate = 16000;
    bool  snipEdges  = true;
    float dither     = 0.0f;     // 0 = deterministic
    bool  subtractMean = true;   // CMN: subtract the per-bin mean over time
    float waveScale  = 1.0f;     // multiplier for [-1,1] samples (32768 for the kaldi int16 scale)
    // Window function by kaldi-native-fbank's name: "povey" (the kaldi default), "hamming", "hanning" ...
    QString windowType = QStringLiteral("povey");
};

class VoiceEmbedder {
public:
    // expectedDim: the dimension per the model descriptor (0 = not checked). If the ONNX output differs
    // (at load from the static shape, otherwise at the first inference) -> a warning.
    explicit VoiceEmbedder(const QString& modelPath, const EmbedderConfig& cfg = EmbedderConfig(),
                           int expectedDim = 0);
    ~VoiceEmbedder();

    bool    isValid() const;
    QString lastError() const;
    int     embeddingDim() const;     // 0 until an inference has run
    // The last dimension of the output from the model's static shape (0 = dynamic / no model).
    int     outputDim() const;
    EmbedderConfig config() const;

    // 16 kHz mono float PCM ([-1,1]) -> an L2-normalized embedding. An empty vector = error.
    QVector<float> embedPcm(const QVector<float>& mono16k) const;

    // Audio file (or its segment) -> embedding. Decodes with the media tool to 16 kHz mono f32.
    // endMs<=0 = the whole file.
    QVector<float> embedFile(const media::MediaTool& media, const QString& audioPath,
                             qint64 startMs = 0, qint64 endMs = 0) const;

    // Helper: decodes an audio-file segment to 16 kHz mono float PCM (media::MediaTool::decodeMonoF32).
    static QVector<float> decodePcm16kMono(const media::MediaTool& media, const QString& audioPath,
                                           qint64 startMs, qint64 endMs);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara::processing
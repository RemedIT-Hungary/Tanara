#pragma once
//
// VoiceEmbedder — beszélő-embedding kinyerés: 16 kHz mono PCM → fbank (kaldi-native-fbank)
// → ONNX modell (3D-Speaker CAM++) → L2-normalizált embedding-vektor. UI-független (core).
// A fbank/normalizálás paraméterei hangolhatók (a kaldi-pontos egyezéshez empirikusan belőve).
//
#include "tanara/Types.h"

#include <QString>
#include <QVector>
#include <memory>

namespace tanara {

// A feature-kinyerés finomhangolható paraméterei (a modellhez illesztve).
struct EmbedderConfig {
    int   numMelBins = 80;
    int   sampleRate = 16000;
    bool  snipEdges  = true;
    float dither     = 0.0f;     // 0 = determinisztikus
    bool  subtractMean = true;   // CMN: bin-enkénti átlag levonása az idő mentén
    float waveScale  = 1.0f;     // [-1,1] mintára szorzó (kaldi int16-skála esetén 32768)
};

class VoiceEmbedder {
public:
    explicit VoiceEmbedder(const QString& modelPath, const EmbedderConfig& cfg = EmbedderConfig());
    ~VoiceEmbedder();

    bool    isValid() const;
    QString lastError() const;
    int     embeddingDim() const;     // 0, amíg le nem futott egy inferencia
    EmbedderConfig config() const;

    // 16 kHz mono float PCM ([-1,1]) → L2-normalizált embedding. Üres vektor = hiba.
    QVector<float> embedPcm(const QVector<float>& mono16k) const;

    // Hangfájl (vagy szegmense) → embedding. ffmpeg-gel dekódol 16 kHz mono f32-re.
    // endMs<=0 → a teljes fájl. ffmpegPath: alapból a PATH-ról.
    QVector<float> embedFile(const QString& audioPath,
                             qint64 startMs = 0, qint64 endMs = 0,
                             const QString& ffmpegPath = QStringLiteral("ffmpeg")) const;

    // Segéd: hangfájl-szegmens dekódolása 16 kHz mono float PCM-re (ffmpeg, QProcess).
    static QVector<float> decodePcm16kMono(const QString& audioPath,
                                           qint64 startMs, qint64 endMs,
                                           const QString& ffmpegPath = QStringLiteral("ffmpeg"));

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara

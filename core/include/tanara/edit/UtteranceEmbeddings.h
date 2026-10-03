#pragma once
//
// Megszólalásonkénti beszélő-embedding: az absztrakt embedder (hogy a unit-tesztek hamisat
// adhassanak be), a valódi CAM++ megvalósítás gyára, és a meeting-mappában élő cache
// (transcript.embeddings.bin).
//
// A valódi megvalósítás a hangot EGYSZER dekódolja 16 kHz mono-ra (egy ffmpeg-futás), és
// abból szeletel — megszólalásonkénti ffmpeg 400–1500 sornál túl lassú lenne.
//
#include <QHash>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara {

class IUtteranceEmbedder {
public:
    virtual ~IUtteranceEmbedder() = default;
    // A meeting hangjának megnyitása (egyszeri dekódolás). false = nincs modell / hang.
    virtual bool open(const QString& audioPath) = 0;
    // A [startMs,endMs] szelet L2-normalizált embeddingje. Üres vektor = nem sikerült.
    virtual QVector<float> embed(qint64 startMs, qint64 endMs) = 0;
    virtual QString lastError() const { return QString(); }
};

// Gyár: a háttérszál a SAJÁT példányát hozza létre vele (az embedder nem szálak közt megosztott).
using UtteranceEmbedderFactory = std::function<std::unique_ptr<IUtteranceEmbedder>()>;

// A valódi (VoiceEmbedder / CAM++ ONNX) megvalósítás gyára. Ha a modell hiányzik vagy a
// build voice-ID nélküli (TANARA_BUILD_VOICEID=OFF), a létrejövő embedder open()-je false.
UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(
    const QString& modelPath, const QString& ffmpegPath = QStringLiteral("ffmpeg"));

// Megszólalás-id → embedding, a meeting-mappában perzisztálva. Az átirat ujjlenyomatához
// kötött: más átirat → üres cache. Üres vektor = megpróbáltuk, nem sikerült (ne próbáljuk újra).
struct UtteranceEmbeddingCache {
    QString fingerprint;
    QHash<QString, QVector<float>> vectors;

    static QString filePath(const QString& meetingFolder);   // transcript.embeddings.bin
    static UtteranceEmbeddingCache load(const QString& meetingFolder, const QString& fingerprint);
    bool save(const QString& meetingFolder) const;
    static void remove(const QString& meetingFolder);
};

} // namespace tanara

#pragma once
//
// Per-utterance speaker embedding: the abstract embedder (so unit tests can inject a fake), the
// factory of the real (VoiceEmbedder / ONNX) implementation, and the per-model cache living in the
// meeting folder (transcript.embeddings.bin, v2).
//
// The real implementation decodes the audio ONCE to 16 kHz mono (one ffmpeg run) and slices from it:
// an ffmpeg per utterance would be too slow at 400-1500 lines. Even with several models there is one
// decode: every model computes from the same PCM.
#include "tanara/processing/voiceid/EmbeddingSet.h"
#include "tanara/processing/voiceid/VoiceEmbedder.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara::processing {

class IUtteranceEmbedder {
public:
    virtual ~IUtteranceEmbedder() = default;
    // Opens the meeting's audio (a one-time decode). false = no model / audio.
    virtual bool open(const QString& audioPath) = 0;
    // L2-normalized embedding of the [startMs,endMs] slice with the base model
    // (VoiceModelRegistry::defaultModelId()). An empty vector = failed.
    virtual QVector<float> embed(qint64 startMs, qint64 endMs) = 0;
    // Embedding of the slice with the requested models. The result has the key of every requested model
    // this embedder knows (an empty vector = failed, do not retry); a missing key = this model is not
    // available here. Default: only the base model, via embed() (single-model fake embedders keep working unchanged).
    virtual EmbeddingSet embedAll(qint64 startMs, qint64 endMs, const QStringList& modelIds);
    virtual QString lastError() const { return QString(); }
};

// Factory: the background thread creates ITS OWN instance with it (the embedder is not shared between threads).
using UtteranceEmbedderFactory = std::function<std::unique_ptr<IUtteranceEmbedder>()>;

// A model for the real embedder: id + ONNX file + fbank parameters.
struct UtteranceModel {
    QString id;
    QString path;
    EmbedderConfig features;
    int dim = 0;   // expected dimension (0 = not checked; VoiceEmbedder warns on a mismatch)
};

// Factory of the real implementation for several models: ONE decoded PCM, one VoiceEmbedder per model.
// open() succeeds if the audio can be decoded and at least one model can be loaded; the key of an
// unloadable model is missing from the embedAll() result. In a build without voice ID
// (TANARA_BUILD_VOICEID=OFF) open() is false.
UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(
    const QVector<UtteranceModel>& models, const QString& ffmpegPath = QStringLiteral("ffmpeg"));
// The same from a model set (including its loader: a fake model in a test). An empty set -> an empty factory.
// Every embedder created gets the set's own unloaded copy.
UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(
    const VoiceEmbedderSet& set, const QString& ffmpegPath = QStringLiteral("ffmpeg"));
// The given model descriptors (e.g. VoiceModelRegistry::active()) with their resolved file paths.
QVector<UtteranceModel> utteranceModelsFor(const QVector<VoiceModelSpec>& specs,
                                           const QString& modelsDir, const QString& appDir);
// Single-model shorthand: the base model (VoiceModelRegistry::defaultModelId()) from the given file.
UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(
    const QString& modelPath, const QString& ffmpegPath = QStringLiteral("ffmpeg"));

// Model -> (utterance id -> embedding), persisted in the meeting folder (v2). Tied to the
// transcript's fingerprint: another transcript -> an empty cache. A v1 (single-model) file is
// dropped at load. An empty vector = tried, failed (do not retry).
struct UtteranceEmbeddingCache {
    QString fingerprint;
    QMap<QString, QHash<QString, QVector<float>>> models;   // modelId -> utteranceId -> vector

    // All vectors of one model (an empty hash if none).
    QHash<QString, QVector<float>> vectors(const QString& modelId) const { return models.value(modelId); }
    void set(const QString& modelId, const QString& utteranceId, const QVector<float>& vec)
    {
        models[modelId].insert(utteranceId, vec);
    }
    // Whether this line has already been tried (even unsuccessfully) with this model.
    bool has(const QString& modelId, const QString& utteranceId) const;
    // The line's vectors with the requested models (only the non-empty ones).
    EmbeddingSet setFor(const QString& utteranceId, const QStringList& modelIds) const;
    // Of the listed lines, those not yet tried with this model (order kept).
    QStringList missingFor(const QString& modelId, const QStringList& utteranceIds) const;
    // There is no entry at all (with any model).
    bool isEmpty() const;

    static QString filePath(const QString& meetingFolder);   // transcript.embeddings.bin
    static UtteranceEmbeddingCache load(const QString& meetingFolder, const QString& fingerprint);
    bool save(const QString& meetingFolder) const;
    static void remove(const QString& meetingFolder);
};

} // namespace tanara::processing
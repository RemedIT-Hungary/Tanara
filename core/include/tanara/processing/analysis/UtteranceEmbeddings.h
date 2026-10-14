#pragma once
//
// Per-utterance speaker embedding: the factory of the real (VoiceEmbedder / ONNX) implementation of the
// utterance-embedder port (providers::IUtteranceEmbedder). The per-model cache of the meeting folder
// (transcript.embeddings.bin, v2) is library::UtteranceEmbeddingCache.
//
// The real implementation decodes the audio ONCE to 16 kHz mono (one media-tool / ffmpeg run) and slices from it:
// an ffmpeg per utterance would be too slow at 400-1500 lines. Even with several models there is one
// decode: every model computes from the same PCM.
#include "tanara/library/store/UtteranceEmbeddingCache.h"
#include "tanara/media/MediaTool.h"
#include "tanara/providers/voice/IUtteranceEmbedder.h"
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

// The embedder port and its factory are in providers (providers/voice/IUtteranceEmbedder.h), the cache in
// the library (library/store/UtteranceEmbeddingCache.h); these aliases keep the existing callers unchanged.
using IUtteranceEmbedder = providers::IUtteranceEmbedder;
// Factory: the background thread creates ITS OWN instance with it (the embedder is not shared between threads).
using UtteranceEmbedderFactory = providers::UtteranceEmbedderFactory;
using UtteranceEmbeddingCache = library::UtteranceEmbeddingCache;

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
    const QVector<UtteranceModel>& models, media::MediaToolPtr media);
// The same from a model set (including its loader: a fake model in a test). An empty set -> an empty factory.
// Every embedder created gets the set's own unloaded copy.
UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(
    const VoiceEmbedderSet& set, media::MediaToolPtr media);
// The given model descriptors (e.g. VoiceModelRegistry::active()) with their resolved file paths.
QVector<UtteranceModel> utteranceModelsFor(const QVector<VoiceModelSpec>& specs,
                                           const QString& modelsDir, const QString& appDir);
// Single-model shorthand: the base model (VoiceModelRegistry::defaultModelId()) from the given file.
UtteranceEmbedderFactory voiceUtteranceEmbedderFactory(
    const QString& modelPath, media::MediaToolPtr media);

} // namespace tanara::processing
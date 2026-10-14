#pragma once
//
// IUtteranceEmbedder: the SYNCHRONOUS sibling of the voice-embedding port (IVoiceEmbedder.h). One
// instance opens ONE meeting audio (a one-time decode) and then embeds slices of it on the calling
// thread; the editor uses it to fill a few missing line vectors at once (a manual voiceprint) and, through
// an IVoiceEmbedder adapter (editing::FactoryVoiceEmbedder), for the background line embedding.
//
// The real implementation (VoiceEmbedder / ONNX, a single decode for every model) is
// processing::voiceUtteranceEmbedderFactory; unit tests inject a fake through the factory.
// (Moved here from processing/analysis/UtteranceEmbeddings.h, so the editing module can use the port
// without depending on processing; processing keeps aliases.)
#include "tanara/domain/Voice.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara::providers {

class IUtteranceEmbedder {
public:
    virtual ~IUtteranceEmbedder() = default;
    // Opens the meeting's audio (a one-time decode). false = no model / audio.
    virtual bool open(const QString& audioPath) = 0;
    // L2-normalized embedding of the [startMs,endMs] slice with the base model
    // (voicemodels::defaultModelId()). An empty vector = failed.
    virtual QVector<float> embed(qint64 startMs, qint64 endMs) = 0;
    // Embedding of the slice with the requested models. The result has the key of every requested model
    // this embedder knows (an empty vector = failed, do not retry); a missing key = this model is not
    // available here. Default: only the base model, via embed() (single-model fake embedders keep working unchanged).
    virtual EmbeddingSet embedAll(qint64 startMs, qint64 endMs, const QStringList& modelIds);
    virtual QString lastError() const { return QString(); }
};

// Factory: a background thread creates ITS OWN instance with it (the embedder is not shared between threads).
using UtteranceEmbedderFactory = std::function<std::unique_ptr<IUtteranceEmbedder>()>;

} // namespace tanara::providers

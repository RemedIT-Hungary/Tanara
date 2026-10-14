#pragma once
//
// FactoryVoiceEmbedder: an IVoiceEmbedder over a synchronous utterance-embedder factory
// (providers::UtteranceEmbedderFactory, e.g. processing::voiceUtteranceEmbedderFactory or a test fake).
// It lets RecheckService compute the line embeddings through the voice-embedding port while keeping the
// decode-once behaviour of the utterance embedder: the embedder instance (and its decoded audio) is kept
// between the jobs of one FactoryVoiceEmbedder and reopened only when a clip names another file.
// RecheckService creates one per embedding run, so the decoded audio is released when the run ends.
//
// Only clip requests (AudioClipRef) are taken; a PCM request fails (Input, "clips_only"). A job embeds its
// clips on a worker thread, one after the other, with the request's models (empty: the defaults given at
// construction; none: the base model). The result has a VoiceVector for every model key the embedder
// returned (an empty vector = failed for that clip); a model the embedder does not know has no entry.
// If the audio cannot be opened the job fails (Service, "open_failed", the embedder's lastError()).
// Cancelling (or destroying) a job stops it after the clip in progress. stopJobs() instead lets the running
// jobs finish early WITH the vectors computed so far (a cancelled run keeps its partial result).
#include "tanara/providers/voice/IUtteranceEmbedder.h"
#include "tanara/providers/voice/IVoiceEmbedder.h"

#include <QObject>
#include <QStringList>

#include <memory>

namespace tanara::editing {

class FactoryVoiceEmbedder : public QObject, public providers::IVoiceEmbedder {
public:
    explicit FactoryVoiceEmbedder(providers::UtteranceEmbedderFactory factory,
                                  QStringList defaultModels = {}, QObject* parent = nullptr);
    ~FactoryVoiceEmbedder() override;

    QString name() const override;
    QStringList defaultModelIds() const override;
    providers::VoiceEmbeddingJob* embed(const providers::VoiceEmbeddingRequest& req) override;
    // The running (and later) jobs stop after the clip in progress and complete with what they have.
    void stopJobs();

    struct Shared;

private:
    std::shared_ptr<Shared> m_shared;
    QStringList m_defaultModels;
};

} // namespace tanara::editing

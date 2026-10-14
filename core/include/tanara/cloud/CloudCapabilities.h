#pragma once
//
// The Tanara Cloud capability adapters and their registration (P10). Each adapter talks to one
// gateway endpoint (docs/cloud-gateway-api.yaml; the new ones are x-status: draft):
//
//   capability                 id                         adapter                   endpoint
//   Transcription              tanara-cloud               SonioxProvider            /v1/files + /v1/transcriptions*
//   Chat                       tanara-cloud               OpenAiCompatibleProvider  /v1/chat/completions (LLM proxy)
//   TextEmbedding              tanara-hosted-embedding    CloudTextEmbedder         /v1/embeddings
//   VoiceEmbedding             tanara-cloud               CloudVoiceEmbedder        /v1/voice/embeddings (raw Opus clip)
//   ParticipantIdentification  tanara-cloud               CloudParticipantIdentifier /v1/voice/identify (vectors only)
//   Summarization              tanara-cloud               CloudPipelineSummarizer   /v1/summaries + /v1/summaries/{id}
//   TagSuggestion              tanara-cloud               CloudTagSuggester         /v1/tags/suggest
//   ModelCatalog               tanara-cloud               CloudModelCatalog         /v1/models?capability=
//
// Every adapter is created by the registry with a ProviderContext + ProviderConfig: the caller
// (AppController through the CloudRunLedger) gives the run's context (auth, headers, observer,
// cost) and the gateway config (baseUrl = apiBase, model). What the caller leaves empty comes from
// the session (address, key, headers, HTTP client).
//
// Kill criterion: registerInto is called only if the client is compiled in (TANARA_BUILD_CLOUD) AND
// the cloud mode is live (CloudMode::live).
#include "tanara/cloud/detail/GatewayClient.h"
#include "tanara/media/MediaTool.h"
#include "tanara/providers/catalog/IModelCatalog.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/embedding/ITextEmbedder.h"
#include "tanara/providers/summary/ISummarizer.h"
#include "tanara/providers/tags/ITagSuggester.h"
#include "tanara/providers/voice/IParticipantIdentifier.h"
#include "tanara/providers/voice/IVoiceEmbedder.h"

#include <QObject>
#include <QPointer>

#include <functional>

namespace tanara::cloud {

class CloudSession;

// The gateway's model "kinds" a capability uses (stt | llm | text_embedding | voice_embedding).
QStringList gatewayKindsFor(providers::CapabilityKind capability);

// ---- adapters ---------------------------------------------------------------------------

class CloudTextEmbedder : public QObject, public providers::ITextEmbedder {
    Q_OBJECT
public:
    CloudTextEmbedder(const providers::ProviderContext& ctx, const providers::ProviderConfig& cfg,
                      CloudSession* session, QObject* parent = nullptr);
    QString name() const override;
    providers::EmbeddingJob* embed(const providers::EmbeddingRequest& req) override;

private:
    providers::ITextEmbedder* m_inner = nullptr;   // the OpenAI-compatible provider (child)
};

class CloudVoiceEmbedder : public QObject, public providers::IVoiceEmbedder {
    Q_OBJECT
public:
    static constexpr int kOpusKbps = 32;
    // media: decodes the clips and encodes every slice (clip or PCM) to Opus; a null media tool
    // fails every request (Unsupported).
    CloudVoiceEmbedder(const providers::ProviderContext& ctx, const providers::ProviderConfig& cfg,
                       CloudSession* session, media::MediaToolPtr media, QObject* parent = nullptr);
    QString name() const override;
    QStringList defaultModelIds() const override;
    providers::VoiceEmbeddingJob* embed(const providers::VoiceEmbeddingRequest& req) override;

    // The Opus (ogg) bytes of a 16 kHz mono float slice, via the media tool's encoder (empty = failed).
    static QByteArray encodeOpus(const media::MediaTool& media, const QVector<float>& mono16k);

private:
    detail::GatewayClient m_gw;
    media::MediaToolPtr m_media;
};

class CloudParticipantIdentifier : public QObject, public providers::IParticipantIdentifier {
    Q_OBJECT
public:
    CloudParticipantIdentifier(const providers::ProviderContext& ctx, const providers::ProviderConfig& cfg,
                               CloudSession* session, QObject* parent = nullptr);
    QString name() const override;
    providers::IdentifyJob* identify(const providers::IdentifyRequest& req) override;

    // The request body: the candidates as opaque refs ("c0", "c1" ...): names never leave the device.
    static QByteArray requestBody(const providers::IdentifyRequest& req, const QString& model);

private:
    detail::GatewayClient m_gw;
};

class CloudPipelineSummarizer : public QObject, public providers::ISummarizer {
    Q_OBJECT
public:
    // Poll interval of GET /v1/summaries/{id} (ProviderConfig::extra "pollIntervalMs" overrides it).
    static constexpr int kPollIntervalMs = 2000;
    CloudPipelineSummarizer(const providers::ProviderContext& ctx, const providers::ProviderConfig& cfg,
                            CloudSession* session, QObject* parent = nullptr);
    QString name() const override;
    providers::SummarizationJob* summarize(const providers::SummarizationRequest& req) override;

    static QByteArray requestBody(const providers::SummarizationRequest& req, const QString& model);
    // The `result` object of a completed summary.
    static providers::SummarizationResult resultFromJson(const QJsonObject& o, providers::SummarizationMode mode);

private:
    detail::GatewayClient m_gw;
    int m_pollMs = kPollIntervalMs;
};

class CloudTagSuggester : public QObject, public providers::ITagSuggester {
    Q_OBJECT
public:
    CloudTagSuggester(const providers::ProviderContext& ctx, const providers::ProviderConfig& cfg,
                      CloudSession* session, QObject* parent = nullptr);
    QString name() const override;
    providers::TagSuggestJob* suggest(const providers::TagSuggestRequest& req) override;

private:
    detail::GatewayClient m_gw;
};

class CloudModelCatalog : public QObject, public providers::IModelCatalog {
    Q_OBJECT
public:
    CloudModelCatalog(const providers::ProviderContext& ctx, const providers::ProviderConfig& cfg,
                      CloudSession* session, QObject* parent = nullptr);
    QString name() const override;
    providers::ModelListJob* list(providers::CapabilityKind capability) override;
    providers::ModelReadyJob* ensureAvailable(const QString& modelId, providers::ProgressSink progress = {}) override;

private:
    detail::GatewayClient m_gw;
};

// ---- registration -------------------------------------------------------------------------

struct CloudCapabilityDeps {
    // The media tool of the voice-embedding adapter (cut + Opus). A function: the tool may be
    // replaced at run time (Settings); null = voice embedding fails with Unsupported.
    std::function<media::MediaToolPtr()> media;
};

// The common id of the cloud adapters (equal to cloud::ProviderId).
QString capabilityProviderId();

// Registers every cloud adapter of the table above into the registry: the STT + chat providers
// (providers::registerCloudProviders), the text embedder (replacing the factory of the built-in
// "tanara-hosted-embedding" descriptor), and the five P10 capabilities. The session must outlive
// the registry's providers (the factories capture it weakly).
void registerInto(providers::ProviderRegistry& registry, CloudSession& session,
                  CloudCapabilityDeps deps = {});

} // namespace tanara::cloud

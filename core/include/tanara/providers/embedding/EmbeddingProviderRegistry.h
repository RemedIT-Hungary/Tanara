#pragma once
//
// Embedding provider registry (the same shape as the LlmProviderRegistry). The built-in descriptors
// (registered by registerBuiltinProviders()):
//  - "openai-compat-embedding": a local / own endpoint (LM Studio, Ollama, OpenAI): Address (URL),
//    Model (fetchable list); "Test connection": GET /models;
//  - "tanara-hosted-embedding": Tanara Cloud (AuthMode::Login, no fields): the gateway's
//    /v1/embeddings route, with the same sign-in as the LLM path.
#include "tanara/providers/core/ProviderDescriptor.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/domain/Types.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

class QObject;

namespace tanara::providers {

class IEmbeddingProvider;

namespace embeddingproviders {
inline const QString LocalId = QStringLiteral("openai-compat-embedding");
inline const QString CloudId = QStringLiteral("tanara-hosted-embedding");
inline const QString ApiKeySecret = QStringLiteral("embedding.apiKey");   // key of the local endpoint
}

class EmbeddingProviderRegistry {
public:
    using Factory = std::function<IEmbeddingProvider*(const ProviderConfig&, QObject*)>;

    static EmbeddingProviderRegistry& instance();

    void registerProvider(const ProviderDescriptor& desc, Factory factory);
    QVector<ProviderDescriptor> all() const;
    bool has(const QString& id) const;
    ProviderDescriptor descriptor(const QString& id) const;     // an empty descriptor if unknown
    IEmbeddingProvider* create(const QString& id, const ProviderConfig& cfg, QObject* parent) const;

private:
    QHash<QString, QPair<ProviderDescriptor, Factory>> m_map;
};

} // namespace tanara::providers
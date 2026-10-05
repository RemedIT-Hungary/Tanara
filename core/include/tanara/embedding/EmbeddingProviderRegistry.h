#pragma once
//
// Beágyazó provider-registry (ugyanolyan alakú, mint az LlmProviderRegistry). A beépített
// leírók (registerBuiltinProviders() regisztrálja őket):
//  - "openai-compat-embedding" — helyi / saját végpont (LM Studio, Ollama, OpenAI): Cím (URL),
//    Modell (lekérhető lista); „Kapcsolat tesztelése”: GET /models;
//  - "tanara-hosted-embedding" — Tanara Cloud (AuthMode::Login, mezők nélkül): a gateway
//    /v1/embeddings útvonala, ugyanazzal a bejelentkezéssel, mint az LLM-út.
//
#include "tanara/provider/ProviderDescriptor.h"
#include "tanara/Types.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

class QObject;

namespace tanara {

class IEmbeddingProvider;

namespace embeddingproviders {
inline const QString LocalId = QStringLiteral("openai-compat-embedding");
inline const QString CloudId = QStringLiteral("tanara-hosted-embedding");
inline const QString ApiKeySecret = QStringLiteral("embedding.apiKey");   // a helyi végpont kulcsa
}

class EmbeddingProviderRegistry {
public:
    using Factory = std::function<IEmbeddingProvider*(const ProviderConfig&, QObject*)>;

    static EmbeddingProviderRegistry& instance();

    void registerProvider(const ProviderDescriptor& desc, Factory factory);
    QVector<ProviderDescriptor> all() const;
    bool has(const QString& id) const;
    ProviderDescriptor descriptor(const QString& id) const;     // üres descriptor, ha ismeretlen
    IEmbeddingProvider* create(const QString& id, const ProviderConfig& cfg, QObject* parent) const;

private:
    QHash<QString, QPair<ProviderDescriptor, Factory>> m_map;
};

} // namespace tanara

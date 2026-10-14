#pragma once
//
// Text-embedding port (CapabilityKind::TextEmbedding), for the "related topic" level of tag
// suggestions. (Was IEmbeddingProvider; the old name stays as an alias in IEmbeddingProvider.h until P15.)
// One OpenAI-compatible implementation covers the LM Studio / Ollama / OpenAI and Tanara Cloud
// gateway (/v1/embeddings) endpoints.
#include "tanara/foundation/error/Error.h"
#include "tanara/providers/core/ProviderDescriptor.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::providers {

// The built-in text-embedding provider ids (registerBuiltinProviders()):
//  - "openai-compat-embedding": a local / own endpoint (LM Studio, Ollama, OpenAI): Address (URL),
//    Model (fetchable list); "Test connection": GET /models;
//  - "tanara-hosted-embedding": Tanara Cloud (AuthMode::Login, no fields): the gateway's
//    /v1/embeddings route, with the same sign-in as the LLM path.
namespace embeddingproviders {
inline const QString LocalId = QStringLiteral("openai-compat-embedding");
inline const QString CloudId = QStringLiteral("tanara-hosted-embedding");
inline const QString ApiKeySecret = QStringLiteral("embedding.apiKey");   // key of the local endpoint
}

struct EmbeddingRequest {
    QStringList texts;
    QString     model;   // empty = the model in the provider's config
};

class EmbeddingJob : public QObject {
    Q_OBJECT
public:
    explicit EmbeddingJob(QObject* parent = nullptr) : QObject(parent) {}
    ~EmbeddingJob() override = default;
    // Cancels the running request; neither finished nor failed follows.
    virtual void cancel() = 0;

    // On a failed HTTP response (filled before failed).
    int errorStatus() const { return m_errorStatus; }
    const Error& error() const { return m_error; }   // the classified error (filled before failed)

signals:
    // The vectors in the order of the request's texts (one text -> one vector).
    void finished(QVector<QVector<float>> vectors);
    void failed(QString error);

protected:
    int m_errorStatus = 0;
    Error m_error;
};

class ITextEmbedder {
public:
    virtual ~ITextEmbedder() = default;
    virtual QString name() const = 0;
    // The job is the provider's child; the caller may delete it after the signals (deleteLater).
    virtual EmbeddingJob* embed(const EmbeddingRequest& req) = 0;
};

template <> struct ProviderTraits<ITextEmbedder> {
    static constexpr CapabilityKind kind = CapabilityKind::TextEmbedding;
};

} // namespace tanara::providers
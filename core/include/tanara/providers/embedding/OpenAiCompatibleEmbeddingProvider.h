#pragma once
//
// OpenAI-compatible embedding provider: POST {baseUrl}/embeddings {model, input:[...]} ->
// data[].embedding. At most 16 texts per request (a longer list is sent in order, in several requests).
// It uses the same ProviderConfig fields as the LLM path (baseUrl, model, apiKey) and the same
// ProviderContext hooks (auth, headers, observe: the Tanara Cloud gateway).
#include "tanara/providers/embedding/ITextEmbedder.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderContext.h"

#include <QPointer>

class QNetworkReply;

namespace tanara::providers {

class OpenAiCompatibleEmbeddingJob : public EmbeddingJob {
    Q_OBJECT
public:
    static constexpr int kBatchSize = 16;

    OpenAiCompatibleEmbeddingJob(net::HttpClient* http, const ProviderContext& ctx, const ProviderConfig& cfg,
                                 const EmbeddingRequest& req, QObject* parent = nullptr);
    ~OpenAiCompatibleEmbeddingJob() override;

    void start();
    void cancel() override;

private:
    void sendNext();
    void onReply();

    QPointer<net::HttpClient> m_http;   // not owned
    ProviderContext  m_ctx;
    ProviderConfig   m_cfg;
    EmbeddingRequest m_req;
    QVector<QVector<float>> m_out;
    int  m_next = 0;                // index of the next text to send
    int  m_batch = 0;               // size of the batch running now
    bool m_done = false;
    QPointer<QNetworkReply> m_reply;
};

class OpenAiCompatibleEmbeddingProvider : public QObject, public ITextEmbedder {
    Q_OBJECT
public:
    explicit OpenAiCompatibleEmbeddingProvider(const ProviderConfig& cfg, QObject* parent = nullptr);
    OpenAiCompatibleEmbeddingProvider(const ProviderContext& ctx, const ProviderConfig& cfg,
                                      QObject* parent = nullptr);
    ~OpenAiCompatibleEmbeddingProvider() override;

    QString name() const override;   // "openai-compat-embedding"
    EmbeddingJob* embed(const EmbeddingRequest& req) override;

private:
    ProviderContext m_ctx;
    ProviderConfig m_cfg;
    net::HttpClient* m_http;         // m_ctx.http, or an own one (child)
};

} // namespace tanara::providers
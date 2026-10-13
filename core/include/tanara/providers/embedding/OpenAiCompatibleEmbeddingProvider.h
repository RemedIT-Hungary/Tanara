#pragma once
//
// OpenAI-compatible embedding provider: POST {baseUrl}/embeddings {model, input:[...]} ->
// data[].embedding. At most 16 texts per request (a longer list is sent in order, in several requests).
// It uses the same ProviderConfig fields as the LLM path: baseUrl, model, apiKey, extraHeaders,
// onExchange (the Tanara Cloud gateway hooks).
#include "tanara/providers/embedding/IEmbeddingProvider.h"
#include "tanara/domain/Types.h"

#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

namespace tanara::providers {

class OpenAiCompatibleEmbeddingJob : public EmbeddingJob {
    Q_OBJECT
public:
    static constexpr int kBatchSize = 16;

    OpenAiCompatibleEmbeddingJob(QNetworkAccessManager* nam, const ProviderConfig& cfg,
                                 const EmbeddingRequest& req, QObject* parent = nullptr);
    ~OpenAiCompatibleEmbeddingJob() override;

    void start();
    void cancel() override;

private:
    void sendNext();
    void onReply();

    QNetworkAccessManager* m_nam;   // not owned
    ProviderConfig   m_cfg;
    EmbeddingRequest m_req;
    QVector<QVector<float>> m_out;
    int  m_next = 0;                // index of the next text to send
    int  m_batch = 0;               // size of the batch running now
    bool m_done = false;
    QPointer<QNetworkReply> m_reply;
};

class OpenAiCompatibleEmbeddingProvider : public QObject, public IEmbeddingProvider {
    Q_OBJECT
public:
    explicit OpenAiCompatibleEmbeddingProvider(const ProviderConfig& cfg, QObject* parent = nullptr);
    ~OpenAiCompatibleEmbeddingProvider() override;

    QString name() const override;   // "openai-compat-embedding"
    EmbeddingJob* embed(const EmbeddingRequest& req) override;

private:
    ProviderConfig m_cfg;
    QNetworkAccessManager* m_nam;    // owned
};

} // namespace tanara::providers
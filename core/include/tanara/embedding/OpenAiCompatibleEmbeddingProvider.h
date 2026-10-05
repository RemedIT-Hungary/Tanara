#pragma once
//
// OpenAI-kompatibilis beágyazó provider: POST {baseUrl}/embeddings {model, input:[…]} →
// data[].embedding. Legfeljebb 16 szöveg kérésenként (a hosszabb listát sorban, több kérésben
// küldi). Ugyanazokat a ProviderConfig-mezőket használja, mint az LLM-út: baseUrl, model,
// apiKey, extraHeaders, onExchange (a Tanara Cloud gateway-hookjai).
//
#include "tanara/embedding/IEmbeddingProvider.h"
#include "tanara/Types.h"

#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

namespace tanara {

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

    QNetworkAccessManager* m_nam;   // nem birtokolt
    ProviderConfig   m_cfg;
    EmbeddingRequest m_req;
    QVector<QVector<float>> m_out;
    int  m_next = 0;                // a következő elküldendő szöveg indexe
    int  m_batch = 0;               // az épp futó köteg mérete
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
    QNetworkAccessManager* m_nam;    // birtokolt
};

} // namespace tanara

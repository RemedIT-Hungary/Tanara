#pragma once
//
// OpenAI-kompatibilis LLM provider (LM Studio / Ollama / OpenAI / Claude proxy).
// MVP: nem-stream, egyszeri /chat/completions POST.
//
#include "tanara/llm/ILlmProvider.h"
#include "tanara/Types.h"

#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

namespace tanara {

// Egyetlen chat-kérést reprezentáló job. A finished(content) / failed(error)
// signalokat az ILlmProvider::LlmJob bázis deklarálja.
class OpenAiCompatibleJob : public LlmJob {
    Q_OBJECT
public:
    OpenAiCompatibleJob(QNetworkAccessManager* nam,
                        const ProviderConfig& cfg,
                        const LlmRequest& req,
                        QObject* parent = nullptr);
    ~OpenAiCompatibleJob() override;

    void start();
    void cancel() override;

private slots:
    void onFinished();

private:
    QNetworkAccessManager* m_nam;   // not owned
    ProviderConfig m_cfg;
    LlmRequest m_req;
    QPointer<QNetworkReply> m_reply;
    bool m_done = false;
};

class OpenAiCompatibleProvider : public QObject, public ILlmProvider {
    Q_OBJECT
public:
    explicit OpenAiCompatibleProvider(const ProviderConfig& cfg, QObject* parent = nullptr);
    ~OpenAiCompatibleProvider() override;

    QString name() const override;            // "openai-compat"
    bool supportsStreaming() const override;  // false (MVP)
    LlmJob* chat(const LlmRequest& req) override;

private:
    ProviderConfig m_cfg;
    QNetworkAccessManager* m_nam;  // owned (this as parent)
};

} // namespace tanara

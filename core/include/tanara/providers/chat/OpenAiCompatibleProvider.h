#pragma once
//
// OpenAI-compatible LLM provider (LM Studio / Ollama / OpenAI / Claude proxy).
// MVP: non-streaming, a single /chat/completions POST.
#include "tanara/providers/chat/ILlmProvider.h"
#include "tanara/domain/Types.h"

#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;

namespace tanara::providers {

// How "thinking" is turned off in a request (ProviderConfig::reasoning + model name).
enum class ReasoningSwitch {
    None,      // no switch is sent (reasoning = "on": the model's default behavior)
    Effort,    // "reasoning_effort": "none" in the request body (Gemma, unknown model)
    Prefill,   // assistant message prefilled with an empty <think></think> block (Qwen 3+)
};

// The method from the setting ("auto" | "off" | "on") and the model id. LM Studio ignores the other
// known switches (enable_thinking, think:false, /no_think, chat_template_kwargs); measured: only these two work.
ReasoningSwitch reasoningSwitchFor(const QString& setting, const QString& model);

// Body of the /chat/completions request (with the reasoning switch): a testable pure function.
QJsonObject buildChatCompletionBody(const ProviderConfig& cfg, const LlmRequest& req);

// Strips the leftover thinking from the response content: a leading <think>...</think> block (or,
// after a prefill, a lone leading </think>). An unterminated <think> -> empty (all of it was
// thinking, no answer).
QString stripThinking(const QString& content);

// A job representing a single chat request. The finished(content) / failed(error) signals are
// declared by the ILlmProvider::LlmJob base.
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

} // namespace tanara::providers
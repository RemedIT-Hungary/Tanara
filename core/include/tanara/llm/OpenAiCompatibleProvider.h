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
class QJsonObject;

namespace tanara {

// A „gondolkodás” kikapcsolásának módja egy kérésben (ProviderConfig::reasoning + modellnév).
enum class ReasoningSwitch {
    None,      // nem küldünk kapcsolót (reasoning = "on": a modell alapviselkedése)
    Effort,    // "reasoning_effort": "none" a kérés törzsében (Gemma, ismeretlen modell)
    Prefill,   // üres <think></think> blokkal előtöltött asszisztens-üzenet (Qwen 3+)
};

// A módszer a beállítás ("auto" | "off" | "on") és a modell-azonosító alapján. Az LM Studio
// a többi ismert kapcsolót (enable_thinking, think:false, /no_think, chat_template_kwargs)
// figyelmen kívül hagyja — mérés szerint csak ez a kettő hat.
ReasoningSwitch reasoningSwitchFor(const QString& setting, const QString& model);

// A /chat/completions kérés törzse (a reasoning-kapcsolóval együtt) — tesztelhető tiszta függvény.
QJsonObject buildChatCompletionBody(const ProviderConfig& cfg, const LlmRequest& req);

// A válasz tartalmából a gondolkodás-maradék levétele: egy vezető <think>…</think> blokk
// (vagy előtöltés után egy magányos, vezető </think>). Lezáratlan <think> → üres (az egész
// gondolkodás volt, válasz nincs).
QString stripThinking(const QString& content);

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

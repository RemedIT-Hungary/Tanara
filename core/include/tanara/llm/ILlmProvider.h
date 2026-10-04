#pragma once
//
// LLM plugin-szerződés. Egyetlen OpenAI-kompatibilis impl lefedi az
// LM Studio / Ollama / OpenAI / Claude endpointokat. Stream-kész a jövőre.
//
#include "tanara/Types.h"
#include <QObject>

namespace tanara {

struct ChatMessage {
    QString role;       // "system" | "user" | "assistant"
    QString content;
};

struct LlmRequest {
    QVector<ChatMessage> messages;
    QString model;
    double temperature = 0.2;
    int maxTokens = 2048;
    bool stream = false;            // jövőbeli élő összefoglaló
    QVariantMap providerOptions;
};

class LlmJob : public QObject {
    Q_OBJECT
public:
    explicit LlmJob(QObject* parent = nullptr) : QObject(parent) {}
    ~LlmJob() override = default;
    virtual void cancel() = 0;

    // Sikertelen HTTP-válasz esetén (a failed jel előtt kitöltve): a státusz és a nyers
    // hibatörzs — ebből ismerhető fel pl. a „nem fér a kontextusba” hiba (llm/LlmContext.h).
    int errorStatus() const { return m_errorStatus; }
    QByteArray errorBody() const { return m_errorBody; }

signals:
    void delta(const QString& chunk);          // stream==true esetén
    void finished(const QString& fullText);
    void failed(const QString& error);

protected:
    int m_errorStatus = 0;
    QByteArray m_errorBody;
};

class ILlmProvider {
public:
    virtual ~ILlmProvider() = default;
    virtual QString name() const = 0;
    virtual bool supportsStreaming() const = 0;
    virtual LlmJob* chat(const LlmRequest& req) = 0;
};

} // namespace tanara

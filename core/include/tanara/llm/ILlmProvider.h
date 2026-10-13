#pragma once
//
// LLM plugin contract. A single OpenAI-compatible implementation covers the
// LM Studio / Ollama / OpenAI / Claude endpoints. Stream-ready for the future.
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
    bool stream = false;            // future live summary
    QVariantMap providerOptions;
};

class LlmJob : public QObject {
    Q_OBJECT
public:
    explicit LlmJob(QObject* parent = nullptr) : QObject(parent) {}
    ~LlmJob() override = default;
    virtual void cancel() = 0;

    // On a failed HTTP response (filled before the failed signal): the status and the raw error body;
    // from this e.g. the "does not fit the context" error is recognized (llm/LlmContext.h).
    int errorStatus() const { return m_errorStatus; }
    QByteArray errorBody() const { return m_errorBody; }

signals:
    void delta(const QString& chunk);          // if stream==true
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

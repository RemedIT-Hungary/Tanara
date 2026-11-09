#pragma once
//
// Chat (LLM) port: CapabilityKind::Chat. A single OpenAI-compatible implementation covers the
// LM Studio / Ollama / OpenAI / Claude endpoints and the Tanara Cloud LLM proxy. Stream-ready for the
// future. (Was ILlmProvider; the old name stays as an alias in ILlmProvider.h until P15.)
#include "tanara/foundation/error/Error.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderDescriptor.h"

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace tanara::providers {

// The ids of the external chat presets (own key; providers/core/BuiltinProviders.cpp). Both run the
// OpenAI-compatible provider with the preset's ChatDialect; providerKindOf() puts them on the External route.
namespace chatproviders {
inline const QString AnthropicId = QStringLiteral("anthropic");
inline const QString OpenAiId = QStringLiteral("openai");

// How a long meeting is summarized with a chat provider: a per-provider setting (ProviderConfig.extra),
// its default comes from the provider's descriptor field.
//  "parts" - time slices, a note per part, then a merge: for a small local model;
//  "whole" - the entire transcript in one call when it fits the model's context: fewer calls and tokens
//            with a large-context model. A transcript that does not fit is still split.
inline const QString SummarySplitKey = QStringLiteral("summarySplit");
inline const QString SplitParts = QStringLiteral("parts");
inline const QString SplitWhole = QStringLiteral("whole");

// The effective value: the config's, else the descriptor field's default, else "parts".
inline QString summarySplit(const ProviderConfig& cfg, const ProviderDescriptor& descriptor)
{
    QString v = cfg.extra.value(SummarySplitKey).toString();
    if (v.isEmpty())
        for (const ConfigField& f : descriptor.fields)
            if (f.key == SummarySplitKey) v = f.defaultValue;
    return v == SplitWhole ? SplitWhole : SplitParts;
}
}

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
    // from this e.g. the "does not fit the context" error is recognized (providers/chat/LlmContext.h).
    int errorStatus() const { return m_errorStatus; }
    QByteArray errorBody() const { return m_errorBody; }
    // The classified error (filled before the failed signal): Auth / Quota / Transient / Input ...;
    // a "does not fit the model's context" answer is Input with code "context_overflow".
    const Error& error() const { return m_error; }
    // The reply was cut at the reply-size limit (finish_reason "length"); set before finished. The text is
    // still delivered: the caller decides whether a cut reply is usable.
    bool truncated() const { return m_truncated; }

signals:
    void delta(const QString& chunk);          // if stream==true
    void finished(const QString& fullText);
    void failed(const QString& error);

protected:
    int m_errorStatus = 0;
    QByteArray m_errorBody;
    Error m_error;
    bool m_truncated = false;
};

class IChatProvider {
public:
    virtual ~IChatProvider() = default;
    virtual QString name() const = 0;
    virtual bool supportsStreaming() const = 0;
    virtual LlmJob* chat(const LlmRequest& req) = 0;
};

template <> struct ProviderTraits<IChatProvider> {
    static constexpr CapabilityKind kind = CapabilityKind::Chat;
};

} // namespace tanara::providers
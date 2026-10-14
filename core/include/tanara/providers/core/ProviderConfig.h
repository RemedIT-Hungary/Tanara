#pragma once
//
// ProviderConfig: the configuration of one provider (STT / LLM / embedding). Persisted in the
// "providers" settings section (ProviderSettings) WITHOUT the apiKey: secrets live in the
// SecretStore (foundation/secrets).
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

namespace tanara::providers {

struct ProviderConfig {
    QString type;            // "soniox" | "openai-compat" | ...
    QString baseUrl;
    QString apiKey;          // runtime only; filled from the SecretStore, never persisted
    QString model;
    double temperature = 0.2; // LLM sampling temperature (summary); not used by STT
    int maxTokens = 8000;     // max tokens of the LLM reply (plenty for a reasoning model); not used by STT
    // The model's "thinking" (reasoning), LLM only. Measured: it must be OFF for the summary
    // (otherwise the output budget is spent on thinking, and it is 2-5x slower):
    //  "auto" - off; the method depends on the model name (Gemma: reasoning_effort "none";
    //           Qwen 3+: assistant message prefilled with an empty <think> block; unknown:
    //           reasoning_effort "none", which servers that do not know it ignore),
    //  "off"  - same as auto (explicit off),
    //  "on"   - no switch is sent: the model's default behavior (a thinking model thinks).
    QString reasoning{QStringLiteral("auto")};
    // The model's context (in tokens), LLM only. 0 = automatic: at task start Tanara requests
    // the smallest standard step (8k / 16k / 20k / 32k ...) that covers the task (up to the
    // model's maximum). Fixed value: always loaded with that size. Only effective with
    // LM Studio (loaded via its native API); for other servers set the load there.
    int contextLength = 0;
    QVariantMap extra;

    // The runtime per-call hooks (request headers, response observer, auth source, cost, cancel) are
    // NOT here: they travel in the ProviderContext (providers/core/ProviderContext.h).

    // JSON without apiKey.
    QJsonObject toJson() const;
    static ProviderConfig fromJson(const QJsonObject& o);
};

} // namespace tanara::providers

Q_DECLARE_METATYPE(tanara::providers::ProviderConfig)

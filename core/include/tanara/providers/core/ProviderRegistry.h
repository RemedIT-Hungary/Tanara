#pragma once
//
// Provider registry (STT and LLM). AppController resolves and creates a provider by id through it;
// there is no hard-coded `new SonioxProvider`. Adding a provider: 1 descriptor + 1 factory + 1
// registerProvider() call. Headless (core); does NOT link Qt Widgets.
#include "tanara/providers/core/ProviderDescriptor.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/domain/Types.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

class QObject;

namespace tanara::providers {

class ISttProvider;
class ILlmProvider;

class SttProviderRegistry {
public:
    using Factory = std::function<ISttProvider*(const ProviderConfig&, QObject*)>;

    static SttProviderRegistry& instance();

    void registerProvider(const ProviderDescriptor& desc, Factory factory);
    QVector<ProviderDescriptor> all() const;
    bool has(const QString& id) const;
    ProviderDescriptor descriptor(const QString& id) const;     // an empty descriptor if unknown
    ISttProvider* create(const QString& id, const ProviderConfig& cfg, QObject* parent) const; // nullptr if unknown

private:
    QHash<QString, QPair<ProviderDescriptor, Factory>> m_map;
};

class LlmProviderRegistry {
public:
    using Factory = std::function<ILlmProvider*(const ProviderConfig&, QObject*)>;

    static LlmProviderRegistry& instance();

    void registerProvider(const ProviderDescriptor& desc, Factory factory);
    QVector<ProviderDescriptor> all() const;
    bool has(const QString& id) const;
    ProviderDescriptor descriptor(const QString& id) const;
    ILlmProvider* create(const QString& id, const ProviderConfig& cfg, QObject* parent) const;

private:
    QHash<QString, QPair<ProviderDescriptor, Factory>> m_map;
};

// One-time, EXPLICIT registration of the built-in providers (Soniox STT + OpenAI-compatible LLM).
// NOT at static-init time (that is fragile with static libs). Idempotent: calling it repeatedly is
// safe. AppController's constructor calls it.
void registerBuiltinProviders();

// Registers the Tanara Cloud STT + LLM provider ("tanara-cloud", AuthMode::Login).
// A feature flag (kill criterion): called only if the cloud client is compiled in
// (TANARA_BUILD_CLOUD) AND the cloud mode is live (settings.cloud.enabled / TANARA_CLOUD_URL).
// The factory is the existing SonioxProvider / OpenAiCompatibleProvider; AppController fills the gateway
// config (baseUrl, key, model, headers, hook). Idempotent.
void registerCloudProviders();

} // namespace tanara::providers
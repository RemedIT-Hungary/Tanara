#pragma once
//
// THE provider registry: one instance holds the providers of every capability, keyed by
// (CapabilityKind, id). A provider is added with its descriptor and a typed factory, and created by
// id with a ProviderContext + ProviderConfig:
//
//   registry.add<ISttProvider>(desc, [](const ProviderContext& ctx, const ProviderConfig& cfg,
//                                       QObject* parent) -> ISttProvider* { ... });
//   ISttProvider* p = registry.create<ISttProvider>(id, ctx, cfg, parent);   // nullptr if unknown
//
// Type safety: the capability comes from ProviderTraits<Port>::kind (specialized in every port
// header), so a factory can only be reached through the port it was added with. The same id may
// exist under several capabilities ("tanara-cloud" is both a Transcription and a Chat provider;
// "openai-compat" is a Chat provider and has a LocalModelServer handler under the same id).
//
// NOT a singleton: the AppController (later app::Services) owns one, the tests build their own.
// Transitional: defaultInstance() serves the callers that have no owner at hand yet (GUI view models,
// the readiness model); it registers the built-in providers on first use. TODO(P15): remove.
//
// Headless (core); does NOT link Qt Widgets.
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderContext.h"
#include "tanara/providers/core/ProviderDescriptor.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

class QObject;

namespace tanara::providers {

class ProviderRegistry {
public:
    template <class Port>
    using Factory = std::function<Port*(const ProviderContext&, const ProviderConfig&, QObject*)>;

    ProviderRegistry() = default;

    // TODO(P15): remove (the owners pass their registry instead).
    static ProviderRegistry& defaultInstance();

    // Adds (or replaces) a provider; the descriptor's capability is set from the port.
    template <class Port>
    void add(ProviderDescriptor desc, Factory<Port> factory)
    {
        desc.capability = ProviderTraits<Port>::kind;
        ErasedFactory erased;
        if (factory)
            erased = [f = std::move(factory)](const ProviderContext& ctx, const ProviderConfig& cfg,
                                              QObject* parent) -> void* {
                return static_cast<void*>(f(ctx, cfg, parent));
            };
        addErased(desc, std::move(erased));
    }

    // nullptr if the id is unknown under the port's capability (or its factory gives nothing).
    template <class Port>
    Port* create(const QString& id, const ProviderContext& ctx, const ProviderConfig& cfg,
                 QObject* parent) const
    {
        return static_cast<Port*>(createErased(ProviderTraits<Port>::kind, id, ctx, cfg, parent));
    }

    // The descriptors of one capability, in registration order.
    QVector<ProviderDescriptor> descriptors(CapabilityKind kind) const;
    bool has(CapabilityKind kind, const QString& id) const;
    ProviderDescriptor descriptor(CapabilityKind kind, const QString& id) const;   // empty if unknown
    void remove(CapabilityKind kind, const QString& id);
    void clear();

private:
    using ErasedFactory =
        std::function<void*(const ProviderContext&, const ProviderConfig&, QObject*)>;
    using Key = QPair<int, QString>;
    struct Entry { ProviderDescriptor desc; ErasedFactory factory; };

    void addErased(const ProviderDescriptor& desc, ErasedFactory factory);
    void* createErased(CapabilityKind kind, const QString& id, const ProviderContext& ctx,
                       const ProviderConfig& cfg, QObject* parent) const;

    QHash<Key, Entry> m_entries;
    QVector<Key> m_order;   // registration order (stable listing)
};

// Registers the built-in providers into a registry: Soniox + Whisper-compatible STT, the
// OpenAI-compatible chat (+ its LM Studio LocalModelServer handler), and the two text-embedding
// descriptors (local endpoint, Tanara Cloud). The caller calls it once per registry (adding again
// only replaces the same entries).
void registerBuiltinProviders(ProviderRegistry& registry);

// Registers the Tanara Cloud STT + chat provider ("tanara-cloud", AuthMode::Login, Origin::Cloud).
// A feature flag (kill criterion): called only if the cloud client is compiled in
// (TANARA_BUILD_CLOUD) AND the cloud mode is live (settings.cloud.enabled / TANARA_CLOUD_URL).
// The factory is the existing SonioxProvider / OpenAiCompatibleProvider; the caller fills the gateway
// config (baseUrl, model) and the ProviderContext (auth, headers, observer, cost).
void registerCloudProviders(ProviderRegistry& registry);

} // namespace tanara::providers

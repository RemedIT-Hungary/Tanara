#pragma once
//
// Provider-registry (STT és LLM). Az AppController ezen keresztül old fel és hoz
// létre providert id alapján — nincs hardkódolt `new SonioxProvider`. Új provider
// hozzáadása: 1 descriptor + 1 factory + 1 registerProvider() hívás.
// Headless (core); NEM linkel Qt Widgetset.
//
#include "tanara/provider/ProviderDescriptor.h"
#include "tanara/Types.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

class QObject;

namespace tanara {

class ISttProvider;
class ILlmProvider;

class SttProviderRegistry {
public:
    using Factory = std::function<ISttProvider*(const ProviderConfig&, QObject*)>;

    static SttProviderRegistry& instance();

    void registerProvider(const ProviderDescriptor& desc, Factory factory);
    QVector<ProviderDescriptor> all() const;
    bool has(const QString& id) const;
    ProviderDescriptor descriptor(const QString& id) const;     // üres descriptor, ha ismeretlen
    ISttProvider* create(const QString& id, const ProviderConfig& cfg, QObject* parent) const; // nullptr, ha ismeretlen

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

// A beépített providerek (Soniox STT + OpenAI-kompatibilis LLM) egyszeri, EXPLICIT
// regisztrációja. NEM static-init-időben (az törékeny statikus lib-eknél). Idempotens:
// többszöri hívás biztonságos. Az AppController ctora hívja.
void registerBuiltinProviders();

} // namespace tanara

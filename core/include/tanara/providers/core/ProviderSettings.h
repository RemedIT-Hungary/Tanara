#pragma once
//
// The "providers" settings section: per capability (STT, LLM, embedding) the selected provider id
// + the config of every provider used so far (switching does not lose the other's settings).
//
//   "providers": { "stt": { "selected": "soniox", "configs": { "soniox": {...} } },
//                  "llm": {...}, "embedding": {...} }
//
// An empty embedding selection = no embedding provider ("basic" tag suggestions).
#include "tanara/providers/core/ProviderConfig.h"

#include <QJsonObject>
#include <QMap>
#include <QString>

namespace tanara::providers {

struct CapabilityProviders {
    QString selectedId;
    QMap<QString, ProviderConfig> configs;   // id -> config

    ProviderConfig selected() const { return configs.value(selectedId); }

    QJsonObject toJson() const;
    static CapabilityProviders fromJson(const QJsonObject& o);
};

struct ProviderSettings {
    CapabilityProviders stt;
    CapabilityProviders llm;
    CapabilityProviders embedding;

    QJsonObject toJson() const;
    static ProviderSettings fromJson(const QJsonObject& o);
};

} // namespace tanara::providers

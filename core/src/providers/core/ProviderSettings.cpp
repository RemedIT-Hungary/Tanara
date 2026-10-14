#include "tanara/providers/core/ProviderSettings.h"

namespace tanara::providers {

// IMPORTANT: apiKey INTENTIONALLY does NOT go into the JSON (the SecretStore is responsible for it).
QJsonObject ProviderConfig::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("type")]    = type;
    o[QStringLiteral("baseUrl")] = baseUrl;
    o[QStringLiteral("model")]   = model;
    o[QStringLiteral("temperature")] = temperature;
    o[QStringLiteral("maxTokens")]   = maxTokens;
    o[QStringLiteral("reasoning")]   = reasoning;
    o[QStringLiteral("contextLength")] = contextLength;
    if (!extra.isEmpty())
        o[QStringLiteral("extra")] = QJsonObject::fromVariantMap(extra);
    return o;
}

ProviderConfig ProviderConfig::fromJson(const QJsonObject& o)
{
    ProviderConfig p;
    p.type    = o.value(QStringLiteral("type")).toString();
    p.baseUrl = o.value(QStringLiteral("baseUrl")).toString();
    p.model   = o.value(QStringLiteral("model")).toString();
    // For a missing field the ProviderConfig default stays.
    p.temperature = o.value(QStringLiteral("temperature")).toDouble(0.2);
    p.maxTokens   = o.value(QStringLiteral("maxTokens")).toInt(8000);
    p.reasoning   = o.value(QStringLiteral("reasoning")).toString(p.reasoning);
    p.contextLength = qMax(0, o.value(QStringLiteral("contextLength")).toInt(0));
    // apiKey is NEVER read from JSON; the SecretStore fills it at run time.
    if (o.contains(QStringLiteral("extra")))
        p.extra = o.value(QStringLiteral("extra")).toObject().toVariantMap();
    return p;
}

QJsonObject CapabilityProviders::toJson() const
{
    QJsonObject cfgs;
    for (auto it = configs.constBegin(); it != configs.constEnd(); ++it)
        cfgs[it.key()] = it.value().toJson();
    return QJsonObject{{QStringLiteral("selected"), selectedId}, {QStringLiteral("configs"), cfgs}};
}

CapabilityProviders CapabilityProviders::fromJson(const QJsonObject& o)
{
    CapabilityProviders c;
    c.selectedId = o.value(QStringLiteral("selected")).toString();
    const QJsonObject cfgs = o.value(QStringLiteral("configs")).toObject();
    for (auto it = cfgs.constBegin(); it != cfgs.constEnd(); ++it)
        c.configs.insert(it.key(), ProviderConfig::fromJson(it.value().toObject()));
    return c;
}

QJsonObject ProviderSettings::toJson() const
{
    return QJsonObject{{QStringLiteral("stt"), stt.toJson()},
                       {QStringLiteral("llm"), llm.toJson()},
                       {QStringLiteral("embedding"), embedding.toJson()}};
}

ProviderSettings ProviderSettings::fromJson(const QJsonObject& o)
{
    ProviderSettings s;
    s.stt = CapabilityProviders::fromJson(o.value(QStringLiteral("stt")).toObject());
    s.llm = CapabilityProviders::fromJson(o.value(QStringLiteral("llm")).toObject());
    s.embedding = CapabilityProviders::fromJson(o.value(QStringLiteral("embedding")).toObject());
    return s;
}

} // namespace tanara::providers

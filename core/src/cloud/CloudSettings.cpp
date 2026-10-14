#include "tanara/cloud/CloudSettings.h"

namespace tanara::cloud {

QJsonObject CloudSettings::toJson() const
{
    return QJsonObject{
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("baseUrl"), baseUrl},
        {QStringLiteral("sttTier"), sttTier},
        {QStringLiteral("llmTier"), llmTier},
        {QStringLiteral("sttModel"), sttModel},
        {QStringLiteral("llmModel"), llmModel},
        {QStringLiteral("waitlistEmail"), waitlistEmail},
        {QStringLiteral("estimateBeforeRun"), estimateBeforeRun},
    };
}

CloudSettings CloudSettings::fromJson(const QJsonObject& o)
{
    CloudSettings s;
    s.enabled  = o.value(QStringLiteral("enabled")).toBool(s.enabled);
    s.baseUrl  = o.value(QStringLiteral("baseUrl")).toString(s.baseUrl);
    s.sttTier  = o.value(QStringLiteral("sttTier")).toString(s.sttTier);
    s.llmTier  = o.value(QStringLiteral("llmTier")).toString(s.llmTier);
    s.sttModel = o.value(QStringLiteral("sttModel")).toString(s.sttModel);
    s.llmModel = o.value(QStringLiteral("llmModel")).toString(s.llmModel);
    s.waitlistEmail = o.value(QStringLiteral("waitlistEmail")).toString(s.waitlistEmail);
    s.estimateBeforeRun = o.value(QStringLiteral("estimateBeforeRun")).toBool(s.estimateBeforeRun);
    return s;
}

} // namespace tanara::cloud

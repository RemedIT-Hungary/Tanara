#pragma once
//
// The "cloud" settings section (Tanara Cloud; the gateway contract: docs/cloud-gateway-api.yaml).
#include <QJsonObject>
#include <QString>

namespace tanara::cloud {

struct CloudSettings {
    // The sign-in cloud mode is live ("after launch"); false = the Settings Tanara Cloud section
    // shows the "Coming soon" (waitlist) panel. TANARA_CLOUD / TANARA_CLOUD_URL override it.
    bool    enabled = false;
    QString baseUrl;                                  // gateway base URL (without /v1); empty = production
    QString sttTier{QStringLiteral("accurate")};      // fast | accurate
    QString llmTier{QStringLiteral("accurate")};
    QString sttModel;                                 // Expert mode: a concrete catalog id; empty = the tier
    QString llmModel;
    QString waitlistEmail;                            // subscribed from "Coming soon" (non-empty = do not ask)
    // Always show a cost estimate + confirmation before Tanara Cloud processing.
    bool    estimateBeforeRun = true;

    QJsonObject toJson() const;
    static CloudSettings fromJson(const QJsonObject& o);
};

} // namespace tanara::cloud

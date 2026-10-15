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
    // The account-level level of the Settings > Tanara Cloud page: "simple" (Gyors / Pontos only) |
    // "advanced" (the same + the switches below). Only what the page shows; the switches apply either way.
    QString level{QStringLiteral("simple")};
    // Always show a cost estimate + confirmation before Tanara Cloud processing ("Költségbecslés
    // minden futás előtt"; read by the run start: the Overview's split button, ShellActions).
    bool    estimateBeforeRun = true;
    // A recording longer than kLongRecordingMinutes runs at the "fast" tier, whatever the tier is.
    // Stored only: the run start / gateway request consumes it later (see docs/cloud-gateway-api.yaml).
    bool    fastForLongRecordings = false;
    // Ask the gateway to delete the uploaded audio as soon as the transcription is done. Stored only:
    // the gateway request consumes it later (an X-Tanara-* header or a request field; not in the spec yet).
    bool    deleteUploadedAudio = true;

    // The fixed threshold of fastForLongRecordings.
    static constexpr int kLongRecordingMinutes = 90;

    QJsonObject toJson() const;
    static CloudSettings fromJson(const QJsonObject& o);
};

} // namespace tanara::cloud

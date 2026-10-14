#pragma once
//
// CloudMode: in which mode the Tanara Cloud client runs (was AppController's constructor switches
// until P10). One place decides it, from the "cloud" settings section and the environment:
//
//   TANARA_CLOUD=live    - the sign-in cloud client is live (if compiled in: TANARA_BUILD_CLOUD)
//   TANARA_CLOUD=teaser  - only the "Coming soon" panel (if compiled in: TANARA_CLOUD_TEASER)
//   TANARA_CLOUD=off     - everything off
//   (unset)              - settings.cloud.enabled decides live; otherwise teaser if compiled in
//   TANARA_CLOUD_URL     - the gateway base URL (overrides settings.cloud.baseUrl)
//
// The kill criterion stays: the cloud capability adapters are registered only in live mode
// (cloud::registerInto, cloud/CloudCapabilities.h).
#include "tanara/cloud/CloudSettings.h"
#include "tanara/providers/core/ReadinessModel.h"
#include "tanara/providers/core/ProviderSettings.h"

#include <QString>

namespace tanara::cloud {

struct CloudMode {
    bool    live = false;
    bool    teaser = false;
    QString baseUrl;        // gateway base URL (without /v1); empty = the production default

    // From the settings and the process environment (TANARA_CLOUD, TANARA_CLOUD_URL) and the compile
    // switches (clientCompiled / teaserCompiled).
    static CloudMode fromEnvironment(const CloudSettings& settings);
    // The pure rule (tests): the environment values and the compile switches given explicitly.
    static CloudMode resolve(const CloudSettings& settings, const QString& envMode, const QString& envUrl,
                             bool clientCompiled, bool teaserCompiled);

    // The selected provider of a workflow step is the Tanara Cloud (only in live mode).
    bool selects(providers::WorkflowStep step, const providers::ProviderSettings& providers) const;
};

} // namespace tanara::cloud

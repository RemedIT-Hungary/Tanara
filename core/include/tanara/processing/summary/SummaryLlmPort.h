#pragma once
//
// SummaryLlmPort: the app side of the summarization's LLM calls (cleanup phase 2, P12e). The processing
// module knows neither the settings, the secret store nor Tanara Cloud; the app implements this port over
// them (SettingsManager, the LLM API key, cloud::CloudRunLedger), so where the text goes and who pays stays
// wired exactly as before the split:
//  - a local (or own-key) chat provider: the selected LLM config + its API key, no run;
//  - Tanara Cloud: ONE cloud run per summary (one X-Tanara-Job-Id, the per-call charges summed up), the
//    gateway config and context, closed with finishRun / failRun (the cost feedback, the refund).
#include "tanara/domain/Job.h"
#include "tanara/foundation/error/Error.h"
#include "tanara/processing/ProcessingSettings.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderContext.h"
#include "tanara/providers/core/ReadinessModel.h"

#include <QString>

#include <memory>

namespace tanara::processing {

class SummaryLlmPort {
public:
    // An opaque Tanara Cloud run handed out by the port; a null Run = a call outside the cloud.
    struct RunHandle { virtual ~RunHandle() = default; };
    using Run = std::shared_ptr<RunHandle>;

    virtual ~SummaryLlmPort() = default;

    // Whether the summary step can run on the meeting (provider configured, key present, cloud blockers).
    virtual providers::ReadinessResult readiness(const QString& meetingId) const = 0;
    // The summary settings (prompts, target language).
    virtual ProcessingSettings processingSettings() const = 0;
    // The chat provider id of a run on the meeting (its LLM route: the own provider, or the Tanara Cloud
    // adapter); it also names the local model server handler, e.g. LM Studio. meetingId may be empty.
    virtual QString chatProviderId(const QString& meetingId) const = 0;
    // The selected chat provider's own config (the server key of a requested context).
    virtual providers::ProviderConfig localConfig() const = 0;
    // Tanara Cloud handles the summary step of the meeting (its resolved LLM route; meetingId may be empty).
    virtual bool cloudSelected(const QString& meetingId) const = 0;

    // A new cloud run (kind: summary | topics | complex).
    virtual Run newRun(const QString& meetingId, const QString& kind) = 0;
    virtual void setRunKind(const Run& run, const QString& kind) = 0;
    // The LLM config of a call. Local: the selected config + its key (run stays null). Cloud: the gateway
    // config; if run is still null, a new run starts (by kind).
    virtual providers::ProviderConfig config(Run& run, const QString& meetingId, const QString& kind) = 0;
    // The provider context of a call: a run -> the gateway context (auth, X-Tanara-* headers incl.
    // summaryMode, response observer, cost sink); no run -> the shared HTTP client only.
    virtual providers::ProviderContext context(const Run& run, const QString& summaryMode) const = 0;
    // Successful end of a run: the charge total goes to the cost feedback (nothing for a null run).
    virtual void finishRun(const Run& run) = 0;
    // A failed run: refund / structured gateway error report. false = nothing was reported (the caller
    // shows its own fallback message).
    virtual bool failRun(const Run& run) = 0;
    // The run's last gateway error as a classified Error (false: none).
    virtual bool runError(const Run& run, Error* out) const = 0;
    // The gateway's own message of the run's last error (may be empty).
    virtual QString runErrorMessage(const Run& run) const = 0;
    // After an intermediate step of a run (topic gathering): refreshes the account's balance chip if the
    // run was charged.
    virtual void refreshAccount(const Run& run) = 0;
};

} // namespace tanara::processing

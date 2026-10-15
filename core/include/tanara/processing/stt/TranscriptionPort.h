#pragma once
//
// TranscriptionPort: the app side of a transcription's provider call (cleanup phase 2, P12f). The processing
// module knows neither the settings, the secret store nor Tanara Cloud; the app implements this port over
// them (SettingsManager, the STT provider's key slot from its descriptor, cloud::CloudRunLedger), so where the
// audio goes and who pays stays wired exactly as before the split:
//  - own key: the selected STT provider's config + its API key, no run;
//  - Tanara Cloud: ONE cloud run per transcription (job id, charge, write-back, error), the gateway config,
//    and the tier's diarization capability.
#include "tanara/foundation/error/Error.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderContext.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace tanara::processing {

class TranscriptionPort {
public:
    // An opaque Tanara Cloud run handed out by the port; a null Run = a call outside the cloud.
    struct RunHandle { virtual ~RunHandle() = default; };
    using Run = std::shared_ptr<RunHandle>;

    // One transcription's provider setup.
    struct Setup {
        QString providerId;                 // the selected STT provider (also the speed statistics' key)
        providers::ProviderConfig config;   // own key / the cloud gateway config
        Run run;                            // the Tanara Cloud run (null: own key)
        bool diarization = true;            // false: the chosen cloud tier does not diarize
        QStringList languageHints;
    };

    virtual ~TranscriptionPort() = default;

    // The provider setup of a new transcription (a cloud run starts here).
    virtual Setup setup(const QString& meetingId) = 0;
    // The provider context of the call: a run -> the gateway context; no run -> the shared HTTP client and a
    // fresh correlation jobId (the caller adds the meetingId).
    virtual providers::ProviderContext context(const Run& run) const = 0;
    // Successful end of a run: the charge goes to the cost feedback (nothing for a null run).
    virtual void finishRun(const Run& run) = 0;
    // A failed run: refund / structured gateway error report. false = nothing was reported (the caller
    // shows its own fallback message).
    virtual bool failRun(const Run& run) = 0;
    // The run's last gateway error as a classified Error (false: none).
    virtual bool runError(const Run& run, Error* out) const = 0;
    // A cancelled run: the pending transcription is forgotten, the balance refreshed if the run was charged.
    virtual void cancelledRun(const Run& run) = 0;
};

} // namespace tanara::processing

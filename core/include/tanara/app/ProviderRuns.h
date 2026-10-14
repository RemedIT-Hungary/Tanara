#pragma once
//
// app::ProviderRuns: the app side of every provider call (cleanup phase 2, P12f; moved unchanged from
// AppController). Who runs a step and who pays: the readiness check (provider configured, key present, the
// Tanara Cloud blockers), own key vs. a Tanara Cloud run (cloud::CloudRunLedger: one run per task, its
// gateway config and context, closing / refund), and over these the ports of the processing steps:
//  - processing::SummaryLlmPort       (the summary and the topic queue),
//  - processing::TranscriptionPort    (the transcription step),
//  - the tag suggester's LLM session and the text embedder factory (library::TagSuggestionService),
//  - the cloud estimate's request (cloud::CloudEstimator::plan over the meeting's facts).
// It does not own the objects it works with (the facade creates them); the meeting facts come from hooks.
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Person.h"
#include "tanara/domain/Transcript.h"
#include "tanara/library/tags/TagSuggestionService.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderContext.h"
#include "tanara/providers/core/ReadinessModel.h"

#include <QCoreApplication>
#include <QString>

#include <functional>
#include <memory>

namespace tanara { class SecretStore; }
namespace tanara::net { class HttpClient; }
namespace tanara::cloud {
class CloudRunLedger;
class CloudSession;
struct CloudMode;
struct CloudRun;
struct EstimateRequest;
}
namespace tanara::library { class MeetingRepository; }
namespace tanara::providers {
class ITextEmbedder;
class ProviderRegistry;
}
namespace tanara::processing {
class SummaryLlmPort;
class TranscriptionPort;
}

namespace tanara::app {

class SettingsManager;

class ProviderRuns {
    Q_DECLARE_TR_FUNCTIONS(ProviderRuns)
public:
    // A Tanara Cloud processing run (X-Tanara-Job-Id, charge totals, last error): cloud/CloudRunLedger.h.
    using CloudRunPtr = std::shared_ptr<cloud::CloudRun>;

    struct Deps {
        SettingsManager*             settings = nullptr;
        SecretStore*                 secrets = nullptr;
        net::HttpClient*             http = nullptr;     // the shared HTTP client of providers + cloud
        cloud::CloudSession*         cloud = nullptr;
        cloud::CloudRunLedger*       ledger = nullptr;
        providers::ProviderRegistry* registry = nullptr;
        const cloud::CloudMode*      mode = nullptr;     // live / teaser / base URL
        library::MeetingRepository*  store = nullptr;
        QString metaDir;                                 // the prompt overrides (<data>/prompts)
        // The in-memory transcript of a fresh transcription (empty: read from disk) and the display names.
        std::function<MergedTranscript(const QString& meetingId)> cachedTranscript;
        std::function<PersonNames()> personNames;
    };

    explicit ProviderRuns(Deps deps);
    ~ProviderRuns();
    ProviderRuns(const ProviderRuns&) = delete;
    ProviderRuns& operator=(const ProviderRuns&) = delete;

    // ---- readiness and the cloud selection ----
    providers::ReadinessResult canRun(providers::WorkflowStep step, const QString& meetingId) const;
    bool usesCloud(providers::WorkflowStep step) const;
    QString cloudModelFor(providers::WorkflowStep step) const;
    // The meeting's facts for the cost estimate (the planning rule is cloud::CloudEstimator::plan).
    cloud::EstimateRequest estimateRequest(const QString& meetingId, const QString& task,
                                           const QString& summaryMode) const;

    // ---- the ports of the processing steps ----
    processing::SummaryLlmPort&    summaryPort() const;
    processing::TranscriptionPort& transcriptionPort() const;
    // The tag suggester's LLM run (the chat provider from the registry, the cloud run closed on success).
    library::TagSuggestionService::LlmSession tagLlmSession(const QString& meetingId, QObject* parent);
    // A text embedder of the given provider id (own key, or the Tanara Cloud sign-in outside any run).
    providers::ITextEmbedder* makeEmbedder(const QString& providerId, QObject* parent);

    // ---- cloud runs ----
    CloudRunPtr newCloudRun(const QString& meetingId, const QString& kind) const;
    // The step's gateway config (baseUrl, model, max tokens; the key comes from the context's auth).
    providers::ProviderConfig cloudConfig(providers::WorkflowStep step) const;
    // The provider context of a call: a cloud run -> the ledger's gateway context (auth, X-Tanara-*
    // headers incl. summaryMode, response observer, cost sink); no run -> the shared HTTP client only.
    providers::ProviderContext providerContext(const CloudRunPtr& run, const QString& summaryMode) const;
    // Own key: the selected LLM config + key (run stays null). Cloud: gateway config; if the run is
    // still null, a new run starts (by kind).
    providers::ProviderConfig llmConfigFor(CloudRunPtr& run, const QString& meetingId, const QString& kind) const;
    void finishCloudRun(const CloudRunPtr& run);   // the ledger's charged signal

private:
    struct SummaryAdapter;
    struct SttAdapter;

    Deps m_deps;
    std::unique_ptr<SummaryAdapter> m_summary;
    std::unique_ptr<SttAdapter>     m_stt;
};

} // namespace tanara::app

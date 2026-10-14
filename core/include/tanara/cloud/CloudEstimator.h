#pragma once
//
// CloudEstimator: the cost estimate of a Tanara Cloud run (moved out of AppController::
// makeEstimateRequest in P10) and the providers::ICostEstimating face of the gateway's /v1/estimate.
//
// The estimate request describes the run process-independently (docs/cloud-gateway-api.yaml,
// /v1/estimate): the recording for the STT part, the planned LLM calls in groups for the summary
// part. plan() is the pure rule that turns the meeting's facts (EstimateInputs, gathered by the
// caller from the library / the summary planner) into that request; the client never computes prices.
//
// Headless (core).
#include "tanara/cloud/CloudTypes.h"
#include "tanara/providers/core/ICostEstimating.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

namespace tanara::cloud {

class CloudSession;

// What the estimate needs to know about the meeting and the planned run.
struct EstimateInputs {
    QString task;                 // transcribe | summarize | both
    QString summaryMode;          // quick | complex | "" (= quick)
    qint64  durationMs = 0;
    int     tracks = 0;           // active tracks
    QString language;             // "" = not sent
    QString sttModel, llmModel;   // the model ids actually sent (CloudSession::modelFor)

    // The summary part (only if a transcript exists): transcript markdown + the meeting note.
    bool    hasTranscript = false;
    int     transcriptChars = 0;  // markdown + context note
    int     contextNoteChars = 0;

    // Complex: the prompts' sizes; the topic list if it already exists.
    int     topicPromptChars = 0;
    int     analysisPromptChars = 0;
    bool    topicsKnown = false;  // an edited topic list exists
    int     missingAnalyses = 0;  // of its topics, those without an analysis yet

    // Quick: the summary plan (processing::SummaryService::plan) and the prompts' sizes.
    bool    singleCall = false;
    int     parts = 0;
    int     cachedParts = 0;
    QVector<int> partChars;
    int     singlePromptChars = 0;
    int     notesPromptChars = 0;
    int     mergePromptChars = 0;
    int     notesMaxTokens = 3000;
};

class CloudEstimator : public QObject, public providers::ICostEstimating {
    Q_OBJECT
public:
    // max_tokens of the cloud LLM calls: a moderate value because of the coverage rule (priced at
    // prompt + max_tokens); the JSON summary and the topic analysis fit well within it.
    static constexpr int kMaxTokens = 4000;
    static constexpr int kReduceInputChars = 6000;   // the reduce's input: a digest of the analyses
    static constexpr int kNotesCharsPerPart = 3000;  // one part note in the merge's input

    // session: the caller's; its lifetime >= this one's (may be null: plan() only).
    explicit CloudEstimator(CloudSession* session, QObject* parent = nullptr);

    // The estimate request of a run (pure).
    static EstimateRequest plan(const EstimateInputs& in);

    // providers::ICostEstimating: POST /v1/estimate through the session.
    providers::CostEstimateJob* estimateCost(const providers::CostEstimateRequest& req) override;
    // The same with the cloud types (the CLI / the estimate dialog).
    void estimate(const EstimateRequest& req,
                  std::function<void(const EstimateResult&, const CloudError&)> done);

    static EstimateRequest toEstimateRequest(const providers::CostEstimateRequest& req);
    static providers::CostEstimate toCostEstimate(const EstimateResult& r);

private:
    QPointer<CloudSession> m_session;
};

} // namespace tanara::cloud

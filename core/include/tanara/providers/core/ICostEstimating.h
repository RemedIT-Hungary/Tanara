#pragma once
//
// Optional capability interface: a provider that can tell the cost of a run BEFORE it starts (the
// Tanara Cloud: cloud::CloudEstimator). Not a registry capability: the caller asks the object it
// already has (dynamic_cast / the cloud module's own instance).
//
// The request describes the run process-independently: the recording (duration, tracks) for the
// transcription part, and the planned LLM calls in groups (count, input size, output budget) for the
// summary part. Amounts are whole millionths of the currency unit, in the user's VAT mode; the
// client never computes prices itself.
#include "tanara/providers/core/CapabilityJob.h"

#include <QString>
#include <QVector>

namespace tanara::providers {

struct LlmCallPlan {
    int count = 1;          // < 0 = unknown (the estimator uses statistics)
    int inputChars = 0;
    int maxTokens = 1;
};

struct CostEstimateRequest {
    QString task;           // transcribe | summarize | both
    qint64  durationMs = 0;
    int     tracks = 0;
    QString sttModel, llmModel;
    QString summaryMode;    // quick | complex | ""
    QString language;
    int     transcriptChars = -1;   // < 0 = unknown
    QVector<LlmCallPlan> llmCalls;
};

struct CostEstimate {
    qint64  estimateMicros = 0, lowMicros = 0, highMicros = 0;
    qint64  balanceMicros = 0, requiredMicros = 0, balanceAfterMicros = 0;
    QString currency;
    QString vatMode;
    bool    enough = false;
    bool    lowBalanceAfter = false;
};

using CostEstimateJob = ResultJob<CostEstimate>;

class ICostEstimating {
public:
    virtual ~ICostEstimating() = default;
    virtual CostEstimateJob* estimateCost(const CostEstimateRequest& req) = 0;
};

} // namespace tanara::providers

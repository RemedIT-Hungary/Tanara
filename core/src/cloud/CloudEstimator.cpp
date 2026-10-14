#include "tanara/cloud/CloudEstimator.h"
#include "tanara/cloud/CloudSession.h"

namespace tanara::cloud {

CloudEstimator::CloudEstimator(CloudSession* session, QObject* parent)
    : QObject(parent), m_session(session)
{
}

EstimateRequest CloudEstimator::plan(const EstimateInputs& in)
{
    EstimateRequest r;
    r.task = in.task;
    r.durationMs = in.durationMs;
    r.tracks = in.tracks;
    r.language = in.language;
    if (in.task != QLatin1String("summarize"))
        r.sttModel = in.sttModel;
    if (in.task == QLatin1String("transcribe"))
        return r;

    r.llmModel = in.llmModel;
    r.summaryMode = in.summaryMode.isEmpty() ? QStringLiteral("quick") : in.summaryMode;
    if (!in.hasTranscript)
        return r;   // the gateway estimates from statistics (duration, mode)

    const int transcript = in.transcriptChars;
    r.transcriptChars = transcript;
    if (r.summaryMode == QLatin1String("complex")) {
        const int topicIn    = transcript + in.topicPromptChars;
        const int analysisIn = transcript + in.analysisPromptChars + 200;
        // The topic count is unknown before extraction (null -> the gateway's statistics); if an edited
        // topic list already exists, only the not-yet-analyzed topics are requested.
        if (!in.topicsKnown) {
            r.llmCalls.append({ 1, topicIn, kMaxTokens });
            r.llmCalls.append({ -1, analysisIn, kMaxTokens });
        } else if (in.missingAnalyses > 0) {
            r.llmCalls.append({ in.missingAnalyses, analysisIn, kMaxTokens });
        }
        r.llmCalls.append({ 1, kReduceInputChars, kMaxTokens });
        return r;
    }

    // Quick summary: 1 part -> one call; n parts -> the note calls of the parts without a finished
    // note + one merge call (see processing::SummaryService::plan).
    const int ctx = in.contextNoteChars + 200;
    if (in.singleCall) {
        r.llmCalls.append({ 1, transcript + in.singlePromptChars, kMaxTokens });
    } else if (in.parts > 0) {
        const int missing = in.parts - in.cachedParts;
        if (missing > 0) {
            qint64 total = 0;
            for (int c : in.partChars) total += c;
            const int perPart = int((total + in.parts - 1) / in.parts);
            r.llmCalls.append({ missing, perPart + in.notesPromptChars + ctx, in.notesMaxTokens });
        }
        r.llmCalls.append({ 1, in.mergePromptChars + ctx + in.parts * kNotesCharsPerPart, kMaxTokens });
    }
    return r;
}

void CloudEstimator::estimate(const EstimateRequest& req,
                              std::function<void(const EstimateResult&, const CloudError&)> done)
{
    if (!m_session) {
        CloudError e;
        e.kind = CloudErrorKind::Other;
        e.message = QStringLiteral("no cloud session");
        if (done) done(EstimateResult{}, e);
        return;
    }
    m_session->estimate(req, std::move(done));
}

EstimateRequest CloudEstimator::toEstimateRequest(const providers::CostEstimateRequest& q)
{
    EstimateRequest r;
    r.task = q.task;
    r.durationMs = q.durationMs;
    r.tracks = q.tracks;
    r.sttModel = q.sttModel;
    r.llmModel = q.llmModel;
    r.summaryMode = q.summaryMode;
    r.language = q.language;
    r.transcriptChars = q.transcriptChars;
    for (const providers::LlmCallPlan& g : q.llmCalls)
        r.llmCalls.append({ g.count, g.inputChars, g.maxTokens });
    return r;
}

providers::CostEstimate CloudEstimator::toCostEstimate(const EstimateResult& r)
{
    providers::CostEstimate c;
    c.estimateMicros = r.estimate.micros;
    c.lowMicros = r.low.micros;
    c.highMicros = r.high.micros;
    c.balanceMicros = r.balance.micros;
    c.requiredMicros = r.required.micros;
    c.balanceAfterMicros = r.balanceAfter.micros;
    c.currency = r.estimate.currency;
    c.vatMode = r.vatMode;
    c.enough = r.enough;
    c.lowBalanceAfter = r.lowBalanceAfter;
    return c;
}

providers::CostEstimateJob* CloudEstimator::estimateCost(const providers::CostEstimateRequest& req)
{
    auto* job = new providers::CostEstimateJob(this);
    QPointer<providers::CostEstimateJob> guard(job);
    estimate(toEstimateRequest(req), [guard](const EstimateResult& r, const CloudError& e) {
        if (!guard) return;
        if (e.isError() || !r.valid) {
            Error err = toError(e);
            if (!err.isError()) err.kind = Error::Kind::Service;
            guard->failLater(err);
            return;
        }
        guard->completeLater(toCostEstimate(r));
    });
    return job;
}

} // namespace tanara::cloud

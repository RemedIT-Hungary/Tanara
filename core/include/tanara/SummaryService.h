#pragma once
//
// SummaryService: orchestrates the quick summary. From the merged transcript it produces
// notes + executive summary + decisions + open questions + action items (Summary), via LLM.
//
// One code path for every length (see summary/SummaryPipeline.h):
//  - 1 part (short meeting, up to ~22 minutes): ONE call with the "single" prompt (notes + JSON);
//  - n parts: n note calls ("notes") one after another, then one combining call ("merge").
// Finished part notes go to the cache (cachePath, e.g. <meeting>/summary.notes.json):
// a failed / interrupted run resumes from there on restart (finished parts do not rerun;
// if only the merge failed, only the merge reruns). The cache is deleted after a successful run.
// On failure there is NO partial result: only summaryFailed is emitted.
// The provider is NOT owned (never deleted).
#include "tanara/Types.h"
#include "tanara/llm/ILlmProvider.h"
#include "tanara/llm/LlmContext.h"
#include "tanara/summary/SummaryPipeline.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

namespace tanara {

struct SummaryRequest {
    MergedTranscript transcript;
    QString     contextNotes;
    QStringList glossary;
    // The system prompts in FINAL form (override + language applied). Empty = the built-in one,
    // with `language` applied.
    QString     notesPrompt;
    QString     mergePrompt;
    QString     singlePrompt;
    QString     language;                   // target language (the setting's text; for the reminder)
    QString     model;
    double      temperature = 0.2;
    int         maxTokens = 8000;           // single / merge call
    int         notesMaxTokens = 3000;      // per-part notes
    qint64      partMs = summarypipe::kDefaultPartMs;
    QString     cachePath;                  // part-notes cache; empty = none
    // Known context of the model (tokens); 0 = unknown. If known, parts that are too large are
    // split smaller up front (the per-part notes and the output budget must also fit).
    int         contextLimit = 0;
    // On a "does not fit the context" error (if the server reports the context), the remaining
    // parts are split smaller ONCE and the run continues. Off for Tanara Cloud.
    bool        adaptToContext = true;
    // For resolving source references: the transcript's utterances (id + time + speaker, per
    // transcript.segments.json + the speaker corrections). Empty = derived from the transcript's
    // speech blocks (with "u<startMs>" ids).
    QVector<summarysrc::SourceLine> sourceLines;
};

// The run plan (for the task phases and the cost estimate; no LLM call).
struct SummaryPlan {
    int  parts = 0;              // number of parts (0 = empty transcript)
    int  cachedParts = 0;        // of which already finished in the cache
    bool singleCall = false;     // 1 part, no cache: one call ("single")
    int  llmCalls = 0;           // number of remaining LLM calls
    QVector<int> partChars;      // transcript markdown length per part
};

class SummaryService : public QObject {
    Q_OBJECT
public:
    explicit SummaryService(ILlmProvider* provider, QObject* parent = nullptr);
    ~SummaryService() override;

    static SummaryPlan plan(const SummaryRequest& req);
    // Estimated context need (tokens) of the run's largest call: the largest of the remaining note /
    // single-step calls and the merge (input + output budget + margin; see llm/LlmContext.h).
    // 0 for an empty transcript.
    static int contextNeed(const SummaryRequest& req);

    // Starts the run. The result arrives asynchronously via summaryReady / summaryFailed.
    void summarize(const SummaryRequest& req);
    // Cancels the running call; no signal follows. The cache is kept.
    void cancel();

    // The built-in single-step prompt for a short meeting ("single").
    static QString defaultSystemPrompt();

    // The usual cache file name in the meeting's folder.
    static QString cacheFileName() { return QStringLiteral("summary.notes.json"); }

signals:
    // Progress. stage: "notes" (done / total parts finished), "merge" (0/1), "single" (0/1).
    void progress(const QString& stage, int done, int total);
    // The number of parts changed mid-run (re-split adapted to the context): parts is the new
    // part count, cached the number of those already finished.
    void partsChanged(int parts, int cached);
    // We learned the model's context (tokens) from the server's error message; the caller may
    // remember it for later runs (SummaryRequest::contextLimit).
    void contextLimitDetected(int contextTokens);
    void summaryReady(const tanara::Summary& summary);
    void summaryFailed(const QString& error);

private:
    void startNextNotes();
    void startMerge();
    void startSingle();
    void call(const QString& system, const QString& user, int maxTokens,
              std::function<void(const QString&)> onText, bool adaptive = false);
    // Splits the unfinished parts longer than budgetChars into smaller ones (finished parts and
    // their notes stay). false: a single paragraph does not fit by itself, or there is no budget.
    bool fitParts(int budgetChars);
    // Whether per-part notes are needed instead of the single-step call (because of the context).
    bool singleFits(int contextTokens) const;
    // Re-split after a context error; true: continues, false: it does not help.
    bool recoverFromOverflow(const llmctx::ContextOverflow& ov, qint64 failedInputChars);
    void startFlow();
    void fail(const QString& error);
    QString userHeader() const;
    QString reminder() const;
    QString prompt(const QString& given, const char* id) const;
    QString cacheKey(int part) const;
    void loadCache();
    void storeCache(int part, const QString& raw);

    ILlmProvider* m_provider;  // not owned
    SummaryRequest m_req;
    QVector<summarypipe::TranscriptPart> m_parts;
    QVector<summarypipe::PartNotes> m_notes;
    QVector<bool> m_done;
    QStringList m_speakers;
    QVector<summarysrc::SourceLine> m_lines;
    QPointer<LlmJob> m_job;
    bool m_cancelled = false;
    bool m_running = false;
    bool m_forceNotes = false;     // 1 part, but the single-step call does not fit the context
    bool m_resplitDone = false;    // the error-driven re-split happens once per run
};

} // namespace tanara

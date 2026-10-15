#pragma once
//
// SummarizationService: a meeting's summary, moved unchanged from AppController (cleanup phase 2, P12e).
//  - the quick summary (SummaryService: one call for a short meeting, per-part notes + merge for a long
//    one; the part-note cache, the context adaptation, the task's stages and estimate);
//  - the complex (per-topic) summary's interactive flow: TopicQueue (topic extraction, the per-topic
//    analysis queue, the final reduce);
//  - the LLM runs of both: the chat provider from the registry, the local model server's preparation
//    (LM Studio: loading with the needed context), the classified failure, cancelling;
//  - persistence through library::SummaryRepository (summary.md + summary.json + the notes copy).
// Where the text goes and who pays (local / own key / Tanara Cloud run bookkeeping) is the app's
// SummaryLlmPort. The speaker names come from the injected resolvers (the editing module's overlay).
//
// The chat provider is driven directly (not through the whole-summary ISummarizer port): the GUI's flow
// needs the part-note cache, the per-stage progress and the editable topic list between the passes,
// which the ISummarizer request does not carry (see the P12e report).
// The AppController facade delegates here and re-emits the signals.
#include "tanara/domain/Job.h"
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Summary.h"
#include "tanara/domain/Transcript.h"
#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/processing/ProcessingSettings.h"
#include "tanara/processing/ProviderFailureSink.h"
#include "tanara/processing/summary/SummaryLlmPort.h"
#include "tanara/processing/summary/SummaryService.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara::library {
class JobStats;
class MeetingJobTracker;
class MeetingRepository;
}
namespace tanara::providers {
class ILocalModelServer;
class IChatProvider;
class ProviderRegistry;
}

namespace tanara::processing {

class TopicQueue;

class SummarizationService : public QObject {
    Q_OBJECT
public:
    struct Deps {
        library::MeetingRepository*  store = nullptr;
        library::MeetingJobTracker*  jobs = nullptr;       // the Summarize / ExtractTopics / AnalyzeTopics tasks
        library::JobStats*           jobStats = nullptr;   // speed of earlier runs (estimates)
        providers::ProviderRegistry* registry = nullptr;   // chat providers, local model servers
        SummaryLlmPort*              llm = nullptr;        // settings, keys, Tanara Cloud runs (the app)
        QString notesDir;                                  // the copy of a new summary (Markdown vault)
        // The in-memory transcript of a fresh transcription (empty: read from transcript.tokens.json).
        std::function<MergedTranscript(const QString& meetingId)> cachedTranscript;
        // Resolves the transcript's speakers to the current names (the LLM should see the real names).
        std::function<void(MergedTranscript& transcript, const Meeting& m)> resolveSpeakers;
        // The utterances with their current (resolved) speaker: the source references and the summary's
        // speaker snapshot (the targeted staleness compares against it).
        std::function<QVector<tanara::summarysrc::SourceLine>(const Meeting& m)> sourceLines;
    };

    explicit SummarizationService(Deps deps, QObject* parent = nullptr);
    ~SummarizationService() override;

    // The complex summary's topic flow.
    TopicQueue& topics() const { return *m_topics; }

    // The quick summary (summary.md + summary.json + a copy into notesDir): summaryReady, or the task's error.
    void summarize(const QString& meetingId);

    // The meeting's summary in structured form + its provenance (exists = false if there is none).
    library::SummaryDocument document(const QString& meetingId) const;
    // "Not said like this? / Flag": marks the statement (summary.json). summaryStatementsChanged on success.
    bool flagStatement(const QString& meetingId, const QString& statementId);

    // "Load with a larger context": the next preparation of the selected local model uses at least this
    // context (once). No-op for Tanara Cloud.
    void requestLlmContext(int tokens);

    // Cancels the meeting's Summarize / ExtractTopics call or its AnalyzeTopics batch (false: nothing ran).
    bool cancel(const QString& meetingId, JobKind kind);

    // The system prompt actually sent: settings override -> file override (<data>/prompts/<id>.md) ->
    // built-in default (PromptLibrary), with the target language applied.
    // promptsLocked (the LLM route is Tanara Cloud): the client never sends user-written instructions, so
    // both overrides are ignored and the built-in prompt is the fixed template (language still applied).
    static QString resolvedPrompt(const ProcessingSettings& s, const QString& userOverride, const char* id,
                                  bool promptsLocked = false);
    // The quick summary request from the settings: the three prompts, the context, the part-note cache
    // path. model / maxTokens come from the provider config actually used (cloud: a moderate max_tokens).
    // promptsLocked: see resolvedPrompt (the context note stays: it is data, not an instruction).
    static SummaryRequest requestFor(const ProcessingSettings& s, const MergedTranscript& merged,
                                     const Meeting& m, const QString& model, int maxTokens,
                                     bool promptsLocked = false);
    // The user-authored prompts among the given prompt ids (single | notes | merge | topic | analysis |
    // reduce): a non-empty settings text or a prompt file in the data folder. A locked route ignores these.
    static QStringList ownPrompts(const ProcessingSettings& s, const QStringList& ids);

signals:
    void summaryReady(QString meetingId, QString markdownPath);
    void summaryStatementsChanged(QString meetingId);
    void jobProgress(QString meetingId, QString message);
    void errorOccurred(QString message);

private:
    friend class TopicQueue;
    using Run = SummaryLlmPort::Run;

    // A running LLM call (summary / topic gathering / combining / the active topic analysis).
    struct ActiveCall {
        QPointer<QObject> svc; QPointer<QObject> provider; Run cloudRun;
        QPointer<providers::ILocalModelServer> preparer;   // the model preparation (LM Studio) while it lasts
    };
    // A run's context knowledge for classifying errors: the estimated need, and what the server probe /
    // the load said.
    struct ContextState { int need = 0; bool lmStudio = false; int loadedContext = -1; };
    using ContextPtr = std::shared_ptr<ContextState>;

    static QString callKey(const QString& meetingId, JobKind kind);
    static QString serverKey(const providers::ProviderConfig& cfg);

    // Whether the call's prompts are locked: the meeting's LLM route is Tanara Cloud (or the call already
    // belongs to a cloud run). Logs one info line when own prompts among ids are ignored because of it.
    bool promptsLocked(const QString& meetingId, const Run& run, const ProcessingSettings& s,
                       const QStringList& ids) const;
    // The transcript to summarize with the resolved speakers (empty: there is none).
    MergedTranscript transcriptFor(const Meeting& m) const;
    // A chat provider for one call (the context captures the failed exchanges into the sink).
    providers::IChatProvider* createChat(const QString& meetingId, const Run& run, const QString& summaryMode,
                                         const providers::ProviderConfig& cfg, const FailureSinkPtr& sink);
    // Tanara is currently running ANOTHER LLM request too (then the model is not unloaded).
    bool otherLlmBusy() const;
    // Preparing the model before an LLM task. For Cloud (and if there is nothing to do) `go` runs at once;
    // for LM Studio, if needed, a load / reload as the task's "model" stage. The caller binds the returned
    // preparer to the call (for cancelling).
    providers::ILocalModelServer* prepareLlm(const QString& meetingId, JobKind kind,
                                             const providers::ProviderConfig& cfg, bool cloud,
                                             const ContextPtr& ctx, std::function<void()> go,
                                             std::function<void(const JobError&)> onFail);
    // The displayable error of a failed call; also logs it (WARN: provider, address, the raw reason).
    JobError describeFailure(JobKind kind, const QString& raw, const Run& run, const FailureSinkPtr& sink,
                             const ContextPtr& ctx = nullptr) const;
    JobError describeFailureImpl(JobKind kind, const QString& raw, const Run& run, const FailureSinkPtr& sink,
                                 const ContextPtr& ctx) const;
    // Cancelling a running call: the service's signals are detached, it and the provider (with the job and
    // the network request) are deleted; no result arrives.
    void abortCall(const ActiveCall& call);
    // A failed run: refund / structured error report, otherwise errorOccurred(fallback).
    void failRun(const Run& run, const QString& fallbackMessage);
    // Writes a finished summary: summary.md, summary.json, the notes copy, meeting.hasSummary.
    void storeSummary(Meeting& m, const QString& markdown, const library::SummaryDocument& doc);

    Deps m_deps;
    QHash<QString, ActiveCall> m_calls;   // callKey(meetingId, kind) -> call
    // The minimum requested by the user ("Load with a larger context"): the next preparation uses it ONCE.
    // Key: server root|model.
    QHash<QString, int> m_contextFloor;
    // For a server without a native API, the context learned from the error message: the next summary
    // already adapts the parts to it (in memory only).
    QHash<QString, int> m_learnedContext;
    std::unique_ptr<TopicQueue> m_topics;   // last: destroyed first
};

} // namespace tanara::processing

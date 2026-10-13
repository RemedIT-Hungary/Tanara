#pragma once
//
// Domain: STRUCTURED state types of the background tasks (transcription, summary, topic analysis,
// mixdown, identification). Plain value types (no QObject), sendable in queued signals
// (Q_DECLARE_METATYPE below). The library list's status icons, the task strip and the tab
// contents are drawn from these.
//
// The old free-text AppController::jobProgress / errorOccurred signals REMAIN (CLI); this
// layer lives alongside them.
#include <QString>
#include <QVector>
#include <QDateTime>
#include <QMetaType>

namespace tanara {

// State of one STT provider job (one track's transcription at the provider). Not to be confused with
// the meeting-level job state (JobProgress / MeetingProcessingState below).
enum class SttJobState { Idle, Uploading, Queued, Processing, Completed, Failed };

// State of a processing step on a meeting (for the library icons).
//  - for identification (identifyState) Failed does not occur.
enum class StepState { None, Running, Failed, Done };

// Kind of a cancellable background task.
enum class JobKind {
    Transcribe,      // transcription (includes: mixdown for the transcription -> upload -> transcribe -> identify)
    Summarize,       // quick summary, or the final combining step (reduce) of the per-topic analysis
    ExtractTopics,   // suggesting topics (complex pass 1)
    AnalyzeTopics,   // topic-analysis queue (complex pass 2): one task per meeting
    Mixdown,         // standalone mixdown (not part of the transcription)
    Identify,        // identifying participants by voice (after the transcript)
    Import,          // importing audio files into a new meeting: the task runs under the id of the FUTURE meeting
                     // (the meeting is created only on success)
    Export,          // packing the meeting into an archive (*.tanara.zip, store/MeetingArchive.h)
};

enum class StageState { Waiting, Running, Done, Failed, Skipped };

// The remembered description of a failed step. Persisted in the meeting's folder (processing.json),
// so it is visible after selecting another meeting AND after a restart.
struct JobError {
    JobKind   kind = JobKind::Transcribe;
    QString   message;        // human explanation (e.g. "The provider did not accept the API key.")
    QString   detail;         // technical line (e.g. "HTTP 401 | invalid_api_key | ...")
    QString   fixActionHint;  // machine hint for the fix button (e.g. "settings:stt"); may be empty
    QDateTime when;
    bool isValid() const { return !message.isEmpty(); }
};

// One stage of a running task (the stage list).
struct JobStage {
    QString    id;            // stable machine id: mixdown | upload | transcribe | diarize | identify | ...
    QString    label;         // display name (translated)
    StageState state = StageState::Waiting;
    int        percent = -1;  // 0..100, ONLY if it comes from a real measurement; -1 = none (indeterminate)
    QString    detail;        // short addition (e.g. "52 min, 3 tracks", "3 / 5 speakers")
};

// Snapshot of a running task.
struct JobProgress {
    QString   meetingId;
    JobKind   kind = JobKind::Transcribe;
    QString   title;          // e.g. "Transcription in progress"
    QString   message;        // the last free-text state (= the jobProgress message)
    QVector<JobStage> stages; // in order; empty if the task has a single step
    int       percent = -1;   // percentage of the WHOLE task, only if real (e.g. mixdown)
    int       done = -1;      // count progress (e.g. 3 / 5 speakers, 2 / 7 topics); -1 = none
    int       total = -1;
    bool      cancellable = true;
    bool      cancelling = false;       // cancel requested, waiting for completion
    QDateTime startedAt;
    // Estimated total run time in s; -1 = cannot be estimated. Computed only from earlier runs measured
    // with the SAME provider (see JobStats); no invented value.
    int       estimatedTotalSec = -1;

    bool isValid() const { return !meetingId.isEmpty(); }
    // Remaining time in s (estimatedTotalSec - elapsed), min 0; -1 if there is no estimate.
    int etaSeconds(const QDateTime& now = QDateTime::currentDateTime()) const {
        if (estimatedTotalSec < 0 || !startedAt.isValid()) return -1;
        const qint64 left = estimatedTotalSec - startedAt.secsTo(now);
        return left < 0 ? 0 : int(left);
    }
    const JobStage* stage(const QString& id) const {
        for (const JobStage& s : stages) if (s.id == id) return &s;
        return nullptr;
    }
};

// A meeting's full processing state: the library item (3 icons), the task strip and the tabs draw from it.
struct MeetingProcessingState {
    QString   meetingId;
    StepState transcriptState = StepState::None;
    StepState summaryState    = StepState::None;
    StepState identifyState   = StepState::None;   // None | Running | Done
    bool      mixdownRunning  = false;             // a mixdown running standalone OR as part of the transcription
    int       mixdownPercent  = -1;                // 0..100, if running
    // The last error per step. Always valid for Failed; if a RE-run failed while the earlier result
    // exists, the state stays Done but the error shows here (the UI may warn) until a successful run or
    // clearError() clears it.
    JobError  transcriptError;
    JobError  summaryError;
    // The summary is STALE: a speaker assignment changed since it was made (the library's "stale" icon
    // state, the Summary tab's pill, the strip). Can be true only with summaryState==Done. The counter:
    // how many speakers were corrected since.
    bool      summaryStale = false;
    int       staleCorrectedSpeakers = 0;
    QVector<JobProgress> jobs;   // the meeting's currently running tasks (task strip)

    bool busy() const { return !jobs.isEmpty(); }
};

// State of a topic in the per-topic analysis (card pill).
enum class TopicState {
    Waiting,   // no analysis and not queued ("waiting")
    Queued,    // queued
    Running,   // running now
    Done,      // has a finished (persisted) analysis
    Failed,    // the last attempt failed (with a message)
};

struct TopicStatus {
    QString    topicId;
    TopicState state = TopicState::Waiting;
    QString    error;         // Failed: human message
    QString    errorDetail;   // Failed: technical line
    QString    fixActionHint; // Failed: machine hint for the fix button (see JobError)
};

// How a task ended (jobFinished signal).
enum class JobOutcome { Done, Failed, Cancelled };

} // namespace tanara

Q_DECLARE_METATYPE(tanara::StepState)
Q_DECLARE_METATYPE(tanara::JobKind)
Q_DECLARE_METATYPE(tanara::StageState)
Q_DECLARE_METATYPE(tanara::JobError)
Q_DECLARE_METATYPE(tanara::JobStage)
Q_DECLARE_METATYPE(tanara::JobProgress)
Q_DECLARE_METATYPE(tanara::MeetingProcessingState)
Q_DECLARE_METATYPE(tanara::TopicState)
Q_DECLARE_METATYPE(tanara::TopicStatus)
Q_DECLARE_METATYPE(QVector<tanara::TopicStatus>)
Q_DECLARE_METATYPE(tanara::JobOutcome)

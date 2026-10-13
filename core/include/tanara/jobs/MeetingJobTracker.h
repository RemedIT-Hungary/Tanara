#pragma once
//
// MeetingJobTracker: the SINGLE source of truth of the per-meeting processing state.
//
//  - Running tasks (in memory): kind, stage list, real percentage / count progress,
//    cancellability. AppController fills it through the orchestration API (begin/setStage/finish/...).
//  - Remembered errors (on disk): processing.json in the meeting's folder keeps the human +
//    technical description of the last failed step, the per-topic errors and the time identification
//    was done, so it is queryable after selecting another meeting and after a restart.
//  - Derived state: state() gives the library icons' and tabs' state from the Meeting's flags
//    (hasTranscript/hasSummary/speakerMap) + the above.
//
// UI-independent (QtCore). List models can update incrementally on the signals.
#include "tanara/Types.h"
#include "tanara/jobs/JobTypes.h"

#include <QObject>
#include <QHash>
#include <functional>

namespace tanara {

class MeetingStore;

class MeetingJobTracker : public QObject {
    Q_OBJECT
public:
    explicit MeetingJobTracker(MeetingStore* store, QObject* parent = nullptr);
    ~MeetingJobTracker() override;

    // ---- query (any time, cheap) -------------------------------------------------
    // The meeting's full state. The id variant loads the meeting from the store; if the caller already
    // holds the (full, loaded from meeting.json) Meeting, the other is faster.
    MeetingProcessingState state(const QString& meetingId) const;
    MeetingProcessingState state(const Meeting& meeting) const;

    JobProgress job(const QString& meetingId, JobKind kind) const;   // !isValid() if not running
    bool isRunning(const QString& meetingId, JobKind kind) const;
    bool isBusy(const QString& meetingId) const;                     // anything is running on it
    QVector<JobProgress> activeJobs() const;                         // every meeting, in start order

    // The last remembered error. Summarize / ExtractTopics / AnalyzeTopics share one "summary" error
    // slot; Identify has none. Invalid (isValid()==false) if there is no error.
    JobError lastError(const QString& meetingId, JobKind kind) const;
    // Per-topic remembered errors (topicId -> error).
    QHash<QString, JobError> topicErrors(const QString& meetingId) const;
    // Whether voice identification ran on the meeting (not cancelled), and when.
    QDateTime identifiedAt(const QString& meetingId) const;

    // Source of the summary's staleness (provided by the speaker-editor layer; AppController binds it).
    // The probe returns the number of corrected speakers, or -1 if the summary is not stale. Without a
    // probe summaryStale is always false.
    using StaleProbe = std::function<int(const Meeting&)>;
    void setSummaryStaleProbe(StaleProbe probe) { m_staleProbe = std::move(probe); }

    // Name of the state file in the meeting folder.
    static QString stateFileName() { return QStringLiteral("processing.json"); }

public slots:
    // Dismisses the remembered error (e.g. an "OK" button). stateChanged + errorChanged signal.
    void clearError(const QString& meetingId, tanara::JobKind kind);
    // Something affecting the derived state changed OUTSIDE the tracker (e.g. the summary's stale flag)
    // -> stateChanged signal for the models.
    void notifyStateChanged(const QString& meetingId) { emit stateChanged(meetingId); }

public:
    // ---- orchestration API (called by AppController) ---------------------------------
    // A new task starts. If the same kind is already running on the meeting, it is overwritten (a restart).
    void begin(const QString& meetingId, JobKind kind, const QString& title,
               const QVector<JobStage>& stages = {}, bool cancellable = true);
    void setStage(const QString& meetingId, JobKind kind, const QString& stageId,
                  StageState state, int percent = -1, const QString& detail = QString());
    // Replaces the whole stage list during a run (e.g. inserting a "Loading the model" stage, or after
    // the number of parts changes).
    void setStages(const QString& meetingId, JobKind kind, const QVector<JobStage>& stages);
    // Only the stage's percentage / detail (its state stays).
    void setStagePercent(const QString& meetingId, JobKind kind, const QString& stageId, int percent);
    void setStageDetail(const QString& meetingId, JobKind kind, const QString& stageId,
                        const QString& detail);
    void setMessage(const QString& meetingId, JobKind kind, const QString& message);
    void setPercent(const QString& meetingId, JobKind kind, int percent);
    void setCounts(const QString& meetingId, JobKind kind, int done, int total);
    void setEstimate(const QString& meetingId, JobKind kind, int estimatedTotalSec);
    void setCancelling(const QString& meetingId, JobKind kind);

    void finish(const QString& meetingId, JobKind kind);                 // success: the error slot is cleared
    void fail(const QString& meetingId, JobKind kind, const JobError& error);   // error: remembered
    void cancelled(const QString& meetingId, JobKind kind);              // cancelled: no error

    // Records an error without a task (e.g. a start precondition failed).
    void recordError(const QString& meetingId, const JobError& error);

    void setTopicError(const QString& meetingId, const QString& topicId, const JobError& error);
    void clearTopicError(const QString& meetingId, const QString& topicId);
    void markIdentified(const QString& meetingId, bool identified = true);

signals:
    // Anything changed in the meeting's state (icons, errors, running tasks).
    void stateChanged(QString meetingId);
    void jobStarted(QString meetingId, tanara::JobKind kind);
    // A running task's progress changed (stage, percentage, count, message).
    void jobProgressChanged(QString meetingId, tanara::JobProgress progress);
    void jobFinished(QString meetingId, tanara::JobKind kind, tanara::JobOutcome outcome);
    // A remembered error appeared / was cleared.
    void errorChanged(QString meetingId, tanara::JobKind kind);

private:
    struct Persisted;
    Persisted& persisted(const QString& meetingId, const QString& folderHint = QString()) const;
    void savePersisted(const QString& meetingId) const;
    JobProgress* find(const QString& meetingId, JobKind kind);
    const JobProgress* find(const QString& meetingId, JobKind kind) const;
    void closeJob(const QString& meetingId, JobKind kind, JobOutcome outcome);
    QString folderFor(const QString& meetingId) const;

    MeetingStore* m_store = nullptr;
    StaleProbe    m_staleProbe;
    QVector<JobProgress> m_jobs;                       // in start order
    mutable QHash<QString, Persisted*> m_persisted;    // meetingId -> on-disk state (lazy)
};

} // namespace tanara

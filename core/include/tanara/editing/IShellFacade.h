#pragma once
//
// IShellFacade: what the main window's shell (ShellActions: the action surface, gating, shared
// undo, toasts; ShellMeetingModel: the selected meeting's header and task bar) needs from the
// application, as ONE abstract surface. The real implementation wraps the AppController and the
// stores (gui/qml/src/ControllerFacades; transitional until the processing and editing services
// are split out of the AppController); the fictional one (demo/ShellDemoFacade) serves the sample
// library of --demo, --gallery, --qml-shot and the tests.
//
// It is a QObject only because it carries signals. The view models never learn which
// implementation they talk to; the one capability flag is live(): a fictional facade answers every
// question about the sample library, but the actions that would change or start something do
// nothing (the shell asks live() where "nothing happens" must be silent, e.g. the gating).
#include "tanara/domain/Job.h"
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Person.h"
#include "tanara/domain/Summary.h"
#include "tanara/editing/session/SpeakerEditTypes.h"
#include "tanara/providers/core/ReadinessModel.h"
#include "tanara/providers/core/Routes.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <optional>

namespace tanara::editing { class MeetingUndoService; class RecheckService; class SpeakerEditSession; }

namespace tanara::editing {

class IShellFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IShellFacade() override = default;

    // False: a fictional library (see above).
    virtual bool live() const = 0;

    // ---- reading ----
    // The meeting; an empty id if there is no such meeting. (The fictional facade's is empty for
    // everything but the sample meetings' header data.)
    virtual Meeting meeting(const QString& meetingId) const = 0;
    virtual bool meetingExists(const QString& meetingId) const = 0;
    virtual QString meetingTitle(const QString& meetingId) const = 0;
    // The transcript's speaker count (0 if unknown).
    virtual int speakerCount(const Meeting& meeting) const = 0;
    virtual SummaryStaleInfo summaryStale(const Meeting& meeting) const = 0;
    // "Soniox · own key" / "Tanara Cloud": the chosen provider of the step.
    virtual QString providerLabel(providers::WorkflowStep step) const = 0;
    // The running tasks and the step states (the shell's task bar, the tabs' pills).
    virtual MeetingProcessingState processingState(const QString& meetingId) const = 0;
    virtual bool jobRunning(const QString& meetingId, JobKind kind) const = 0;
    virtual JobError lastJobError(const QString& meetingId, JobKind kind) const = 0;
    virtual RecordingState recordingState() const = 0;
    // The shared undo stack of the meeting (editor, tag and participant steps); nullptr if none.
    virtual editing::MeetingUndoService* meetingUndo(const QString& meetingId) = 0;
    // The tag row's backend (a QObject the tag models know how to use); nullptr -> the fictional pool.
    // Transitional: the tag models are ported to the services by their own slice.
    virtual QObject* tagController() const = 0;
    // A design state's sample rows of the task bar (the shell's "demoTask"); empty for a real facade.
    virtual QVector<JobProgress> sampleTasks(const QString& /*meetingId*/) const { return {}; }

    // ---- gating ----
    virtual providers::ReadinessResult canRun(providers::WorkflowStep step, const QString& meetingId) const = 0;
    virtual bool usesCloud(providers::WorkflowStep step) const = 0;
    // ---- S5: the per-run route ----
    // The capability's route setting (may be Ask / Cloud); the defaults: Local, cloud not live.
    virtual providers::Route storedRoute(providers::Capability cap) const { Q_UNUSED(cap); return providers::Route::Local; }
    virtual bool cloudLive() const { return false; }
    // A Cloud run needs the estimate's confirmation first (cloud.estimateBeforeRun).
    virtual bool confirmCloudRuns() const { return true; }
    // The next run of the capability on the meeting takes this route (app::Workflows::chooseRoute);
    // std::nullopt drops a pending choice (the start did not happen).
    virtual void chooseRoute(providers::Capability cap, const QString& meetingId,
                             std::optional<providers::Route> route)
    {
        Q_UNUSED(cap); Q_UNUSED(meetingId); Q_UNUSED(route);
    }

    // ---- processing ----
    virtual void transcribe(const QString& meetingId) = 0;
    virtual void retranscribe(const QString& meetingId, bool keepBackup) = 0;
    virtual editing::RetranscribeImpact retranscribeImpact(const QString& meetingId) const = 0;
    virtual void summarize(const QString& meetingId) = 0;
    virtual QVector<SummaryTopic> topics(const QString& meetingId) const = 0;
    virtual void extractTopics(const QString& meetingId) = 0;
    virtual void generateComplexSummary(const QString& meetingId, const QVector<SummaryTopic>& topics) = 0;
    virtual void analyzeTopic(const QString& meetingId, const SummaryTopic& topic) = 0;
    // False: did not start (the shell works out why from the state below).
    virtual bool identifyAsync(const QString& meetingId) = 0;
    virtual bool voiceIdentificationAvailable() const = 0;
    virtual bool participantApprovalPending(const QString& meetingId) const = 0;
    // The transcript's editor session (its speakers) and its voice analysis (re-check); nullptr for an
    // unknown meeting / without a live backend.
    virtual editing::SpeakerEditSession* editSession(const QString& meetingId) = 0;
    virtual editing::RecheckService* recheckService(const QString& meetingId) = 0;
    // The display name of a person (the people list refreshed first); empty if unknown.
    virtual QString personName(const PersonId& person) = 0;
    virtual void requestLlmContext(int tokens) = 0;
    virtual void cancelJob(const QString& meetingId, JobKind kind) = 0;

    // ---- meeting actions ----
    virtual void renameMeeting(const QString& meetingId, const QString& title) = 0;
    // Deleting a meeting: the shell first cancels the running tasks (they must not write into the
    // deleted folder) and closes the editor session, then deletes.
    virtual void cancelAllJobs(const QString& meetingId) = 0;
    virtual void closeEditSession(const QString& meetingId) = 0;
    virtual void deleteMeeting(const QString& meetingId) = 0;
    virtual void stopRecording() = 0;

    // ---- import / archive ----
    virtual bool importBusy() const = 0;
    // The archive operations run in the background; the end comes as archiveFinished(). False if
    // refused (the refusal text arrives as errorOccurred()).
    virtual bool exportArchive(const QString& meetingId, const QString& zipPath) = 0;
    virtual bool importArchive(const QString& zipPath) = 0;
    virtual bool importFolder(const QString& folder) = 0;

signals:
    void recordingStateChanged(tanara::RecordingState state);
    void recordingFinished(tanara::Meeting meeting);
    void importFinished(tanara::Meeting meeting);
    void transcriptReady(QString meetingId);
    void summaryReady(QString meetingId);
    void topicsReady(QString meetingId);
    void topicAnalysisQueueFinished(QString meetingId, int okCount, int failCount);
    // `cancelled`: the user cancelled (ok is false then).
    void archiveFinished(QString opId, bool ok, bool cancelled, QString meetingId, QString path,
                         QString message);
    void mixdownUpdated(QString meetingId, bool ok);
    void errorOccurred(QString message);
    // The store's: a meeting changed / was removed.
    void meetingUpdated(QString meetingId);
    void meetingRemoved(QString meetingId);
    void speakerMapChanged(QString meetingId);
    void summaryStaleChanged(QString meetingId);
    void tracksChanged(QString meetingId);
    // The task tracker's.
    void jobFinished(QString meetingId, tanara::JobKind kind, tanara::JobOutcome outcome);
    void jobErrorChanged(QString meetingId, tanara::JobKind kind);
    void jobStateChanged(QString meetingId);
    void jobProgressChanged(QString meetingId);
};

} // namespace tanara::editing

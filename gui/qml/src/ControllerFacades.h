#pragma once
//
// The real facades that still stand on the AppController (their processing / editing sides are
// not split into services yet): the shell's (tanara::editing::IShellFacade), the import window's
// (tanara::library::IImportFacade) and the Overview tab's (tanara::library::IOverviewFacade). They
// are TRANSITIONAL adapters: the view models never see the controller, and when a service takes
// over a part, the adapter calls it instead. Defined in AppContext.cpp (the one place of the GUI
// that includes the AppController); one set per controller, built by AppContext::servicesFor().
//
#include "tanara/library/IOverviewFacade.h"
#include "tanara/editing/IShellFacade.h"
#include "tanara/library/IImportFacade.h"

#include <QHash>
#include <QPointer>

namespace tanara::app { class AppController; }

namespace tanara_qml {

class ShellControllerFacade : public tanara::editing::IShellFacade {
    Q_OBJECT
public:
    explicit ShellControllerFacade(tanara::app::AppController* controller);

    bool live() const override { return true; }

    tanara::Meeting meeting(const QString& meetingId) const override;
    bool meetingExists(const QString& meetingId) const override;
    QString meetingTitle(const QString& meetingId) const override;
    int speakerCount(const tanara::Meeting& meeting) const override;
    tanara::SummaryStaleInfo summaryStale(const tanara::Meeting& meeting) const override;
    QString providerLabel(tanara::providers::WorkflowStep step) const override;
    tanara::MeetingProcessingState processingState(const QString& meetingId) const override;
    bool jobRunning(const QString& meetingId, tanara::JobKind kind) const override;
    tanara::JobError lastJobError(const QString& meetingId, tanara::JobKind kind) const override;
    tanara::RecordingState recordingState() const override;
    tanara::editing::MeetingUndoService* meetingUndo(const QString& meetingId) override;
    QObject* tagController() const override;

    tanara::providers::ReadinessResult canRun(tanara::providers::WorkflowStep step,
                                              const QString& meetingId) const override;
    bool usesCloud(tanara::providers::WorkflowStep step) const override;

    void transcribe(const QString& meetingId) override;
    void retranscribe(const QString& meetingId, bool keepBackup) override;
    tanara::editing::RetranscribeImpact retranscribeImpact(const QString& meetingId) const override;
    void summarize(const QString& meetingId) override;
    QVector<tanara::SummaryTopic> topics(const QString& meetingId) const override;
    void extractTopics(const QString& meetingId) override;
    void generateComplexSummary(const QString& meetingId, const QVector<tanara::SummaryTopic>& topics) override;
    void analyzeTopic(const QString& meetingId, const tanara::SummaryTopic& topic) override;
    bool identifyAsync(const QString& meetingId) override;
    bool voiceIdentificationAvailable() const override;
    bool participantApprovalPending(const QString& meetingId) const override;
    tanara::editing::SpeakerEditor* speakerEditor(const QString& meetingId) override;
    QString personName(const tanara::PersonId& person) override;
    void requestLlmContext(int tokens) override;
    void cancelJob(const QString& meetingId, tanara::JobKind kind) override;

    void renameMeeting(const QString& meetingId, const QString& title) override;
    void cancelAllJobs(const QString& meetingId) override;
    void closeSpeakerEditor(const QString& meetingId) override;
    void deleteMeeting(const QString& meetingId) override;
    void stopRecording() override;

    bool importBusy() const override;
    bool exportArchive(const QString& meetingId, const QString& zipPath) override;
    bool importArchive(const QString& zipPath) override;
    bool importFolder(const QString& folder) override;

private:
    QPointer<tanara::app::AppController> m_c;
};

class ImportControllerFacade : public tanara::library::IImportFacade {
    Q_OBJECT
public:
    explicit ImportControllerFacade(tanara::app::AppController* controller);

    bool live() const override { return true; }
    bool canStart(const QString&) const override { return true; }
    void probeAsync(const QString& path) override;
    QString start(const tanara::library::ImportRequest& request) override;
    void cancel(const QString& importId) override;
    tanara::Tag createTag(const QString& name) override;
    void applyTags(const QString& meetingId, const QStringList& tagIds) override;
    QVector<tanara::TagSuggestion> suggestionsFor(const QString& title) const override;
    std::optional<tanara::library::ImportScenario> scenario(const QString&) const override { return std::nullopt; }

private:
    QPointer<tanara::app::AppController> m_c;
};

class OverviewControllerFacade : public tanara::library::IOverviewFacade {
    Q_OBJECT
public:
    explicit OverviewControllerFacade(tanara::app::AppController* controller);

    tanara::library::OverviewSnapshot snapshot(const QString& meetingId) override;
    QString addParticipant(const QString& meetingId, const QString& name) override;
    void removeParticipant(const QString& meetingId, const QString& id) override;
    bool setSolo(const QString& meetingId) override;
    bool reanalyze(const QString& meetingId) override;
    bool nameSpeakerInPlace(const QString&, const QString&, const QString&) override { return false; }
    void setContextNote(const QString& meetingId, const QString& note) override;

private:
    // The audio analysis progress of a meeting (participantAnalysisProgress): track k / N and percent.
    struct AnalysisProgress {
        int done = 0;
        int total = 0;
        int percent = -1;
    };

    QPointer<tanara::app::AppController> m_c;
    QHash<QString, AnalysisProgress> m_progress;
};

} // namespace tanara_qml

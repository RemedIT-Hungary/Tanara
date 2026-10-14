#pragma once
//
// ShellDemoFacade: the fictional tanara::editing::IShellFacade over the sample library
// (LibraryDemoData) for --demo, --gallery, --qml-shot and the tests. It answers the questions
// about the sample meetings (header data, speaker count, the stale summary ...); live() is false,
// so every action that would start or change something is a silent no-op in the shell.
#include "tanara/editing/IShellFacade.h"

namespace tanara_demo {

class ShellDemoFacade : public tanara::editing::IShellFacade {
    Q_OBJECT
public:
    explicit ShellDemoFacade(QObject* parent = nullptr);

    bool live() const override { return false; }

    tanara::Meeting meeting(const QString& meetingId) const override;
    bool meetingExists(const QString& meetingId) const override;
    QString meetingTitle(const QString& meetingId) const override;
    int speakerCount(const tanara::Meeting& meeting) const override;
    tanara::SummaryStaleInfo summaryStale(const tanara::Meeting& meeting) const override;
    QString providerLabel(tanara::providers::WorkflowStep step) const override;
    tanara::MeetingProcessingState processingState(const QString&) const override { return {}; }
    bool jobRunning(const QString&, tanara::JobKind) const override { return false; }
    tanara::JobError lastJobError(const QString&, tanara::JobKind) const override { return {}; }
    tanara::RecordingState recordingState() const override { return tanara::RecordingState::Idle; }
    tanara::editing::MeetingUndoService* meetingUndo(const QString&) override { return nullptr; }
    QObject* tagController() const override { return nullptr; }
    QVector<tanara::JobProgress> sampleTasks(const QString& meetingId) const override;

    tanara::providers::ReadinessResult canRun(tanara::providers::WorkflowStep, const QString&) const override;
    bool usesCloud(tanara::providers::WorkflowStep) const override { return false; }

    void transcribe(const QString&) override {}
    void retranscribe(const QString&, bool) override {}
    tanara::editing::RetranscribeImpact retranscribeImpact(const QString& meetingId) const override;
    void summarize(const QString&) override {}
    QVector<tanara::SummaryTopic> topics(const QString&) const override { return {}; }
    void extractTopics(const QString&) override {}
    void generateComplexSummary(const QString&, const QVector<tanara::SummaryTopic>&) override {}
    void analyzeTopic(const QString&, const tanara::SummaryTopic&) override {}
    bool identifyAsync(const QString&) override { return false; }
    bool voiceIdentificationAvailable() const override { return false; }
    bool participantApprovalPending(const QString&) const override { return false; }
    tanara::editing::SpeakerEditor* speakerEditor(const QString&) override { return nullptr; }
    QString personName(const tanara::PersonId&) override { return {}; }
    void requestLlmContext(int) override {}
    void cancelJob(const QString&, tanara::JobKind) override {}

    void renameMeeting(const QString&, const QString&) override {}
    void cancelAllJobs(const QString&) override {}
    void closeSpeakerEditor(const QString&) override {}
    void deleteMeeting(const QString&) override {}
    void stopRecording() override {}

    bool importBusy() const override { return false; }
    bool exportArchive(const QString&, const QString&) override { return false; }
    bool importArchive(const QString&) override { return false; }
    bool importFolder(const QString&) override { return false; }
};

} // namespace tanara_demo

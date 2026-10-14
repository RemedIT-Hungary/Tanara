#include "TranscribeStepServicesFacade.h"

#include "JobSupport.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>

namespace tanara_qml {

using namespace tanara;

TranscribeStepServicesFacade::TranscribeStepServicesFacade(Deps deps, QObject* parent)
    : ITranscribeStepFacade(parent)
    , m_deps(std::move(deps))
    , m_store(m_deps.store)
    , m_jobs(m_deps.jobs)
    , m_noteController(m_deps.noteController)
{
    if (m_jobs) {
        connect(m_jobs, &library::MeetingJobTracker::stateChanged, this, &ITranscribeStepFacade::changed);
        connect(m_jobs, &library::MeetingJobTracker::jobProgressChanged, this,
                [this](const QString& id, const JobProgress& p) {
            if (p.kind == JobKind::Transcribe) emit transcribeProgress(id, p);
        });
    }
    if (m_store)
        connect(m_store, &library::MeetingRepository::meetingUpdated, this, &ITranscribeStepFacade::changed);
    if (m_deps.settings)
        connect(m_deps.settings, &app::SettingsManager::settingsChanged, this,
                &ITranscribeStepFacade::settingsChanged);
}

processing::TranscribeStepSnapshot TranscribeStepServicesFacade::snapshot(const QString& meetingId) const
{
    processing::TranscribeStepSnapshot s;
    if (meetingId.isEmpty() || !m_store || !m_jobs)
        return s;
    const Meeting m = m_store->load(meetingId);
    s.title = m.title;
    const MeetingProcessingState ps = m_jobs->state(m);
    const JobProgress job = m_jobs->job(meetingId, JobKind::Transcribe);

    if (m_deps.providerLabel) s.providerLabel = m_deps.providerLabel(providers::WorkflowStep::Transcribe);
    s.identifyAvailable = m_deps.identifyAvailable && m_deps.identifyAvailable();
    s.identifyEnabled = s.identifyAvailable && m_deps.identifyAfterTranscription
                        && m_deps.identifyAfterTranscription(meetingId);

    // Mixdown: running / done / stale / missing.
    const QString mixPath = QDir(m.folder).filePath(library::MeetingLayout::mixdownOf(m));
    if (ps.mixdownRunning) {
        s.mixdownState = QStringLiteral("running");
        s.mixdownPercent = ps.mixdownPercent;
        s.mixdownCancellable = m_jobs->isRunning(meetingId, JobKind::Mixdown);
    } else {
        s.mixdownPercent = -1;
        if (m.mixdownFile.isEmpty() || !QFileInfo::exists(mixPath))
            s.mixdownState = QStringLiteral("missing");
        else
            s.mixdownState = m.mixdownDirty ? QStringLiteral("stale") : QStringLiteral("ready");
    }

    // Gating: from the same source of truth as the application's guards.
    if (m_deps.canRun) {
        const providers::ReadinessResult r = m_deps.canRun(providers::WorkflowStep::Transcribe, meetingId);
        s.canStart = r.runnable;
        s.blocker = jobsupport::blockerInfo(nullptr, providers::WorkflowStep::Transcribe, r);
        s.cloudSoon = !r.runnable && r.blockerKind == providers::BlockerKind::ProviderConfig;
    }

    if (job.isValid()) {
        s.state = QStringLiteral("running");
        s.job = job;
    } else if (ps.transcriptState == StepState::Failed && ps.transcriptError.isValid()) {
        s.state = QStringLiteral("failed");
        s.errorMessage = ps.transcriptError.message;
        s.errorDetail = ps.transcriptError.detail;
        if (ps.transcriptError.when.isValid()) {
            const QString when = QLocale().toString(ps.transcriptError.when, tr("MMM d. HH:mm"));
            s.errorDetail = s.errorDetail.isEmpty() ? when : s.errorDetail + QStringLiteral(" · ") + when;
        }
        const jobsupport::FixAction fix = jobsupport::fixActionForError(ps.transcriptError);
        s.fixActionLabel = fix.label;
        s.fixActionPage = fix.page;
    } else {
        s.state = m.id.isEmpty() ? QStringLiteral("none") : QStringLiteral("steps");
    }
    return s;
}

void TranscribeStepServicesFacade::setIdentifyAfterTranscription(const QString& meetingId, bool enabled)
{
    if (!meetingId.isEmpty() && m_deps.setIdentifyAfterTranscription)
        m_deps.setIdentifyAfterTranscription(meetingId, enabled);
}

void TranscribeStepServicesFacade::regenerateMixdown(const QString& meetingId)
{
    if (!meetingId.isEmpty() && m_deps.regenerateMixdown)
        m_deps.regenerateMixdown(meetingId);
}

void TranscribeStepServicesFacade::clearError(const QString& meetingId)
{
    if (!meetingId.isEmpty() && m_jobs)
        m_jobs->clearError(meetingId, JobKind::Transcribe);   // -> stateChanged -> changed()
}

} // namespace tanara_qml

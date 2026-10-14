#include "TracksServicesFacade.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"

using namespace tanara;

namespace tanara_qml {

TracksServicesFacade::TracksServicesFacade(const Deps& deps, QObject* parent)
    : ITracksFacade(parent), m_deps(deps)
{
    if (m_deps.jobs)
        connect(m_deps.jobs, &library::MeetingJobTracker::stateChanged, this, &ITracksFacade::jobsChanged);
    if (m_deps.waveforms) {
        connect(m_deps.waveforms, &library::WaveformService::peaksReady, this,
                [this](const QString& id, const QString& trackId, const library::TrackPeaks& p) {
            emit peaksReady(id, trackId, p);
        });
        connect(m_deps.waveforms, &library::WaveformService::peaksFailed, this,
                [this](const QString& id, const QString& trackId, const QString&) { emit peaksFailed(id, trackId); });
    }
    // Renaming devices (Settings) also changes the tracks' friendly names.
    if (m_deps.settings) {
        m_deviceNames = m_deps.settings->settings().recording.deviceNames;
        connect(m_deps.settings, &app::SettingsManager::settingsChanged, this, [this] {
            const QMap<QString, QString> names = m_deps.settings->settings().recording.deviceNames;
            if (names == m_deviceNames) return;
            m_deviceNames = names;
            emit trackNamesChanged();
        });
    }
}

Meeting TracksServicesFacade::meeting(const QString& meetingId) const
{
    return m_deps.store && !meetingId.isEmpty() ? m_deps.store->load(meetingId) : Meeting();
}

library::MixdownJob TracksServicesFacade::mixdownJob(const QString& meetingId) const
{
    library::MixdownJob out;
    if (!m_deps.jobs || !m_deps.store || meetingId.isEmpty()) return out;
    const MeetingProcessingState ps = m_deps.jobs->state(m_deps.store->load(meetingId));
    out.running = ps.mixdownRunning;
    out.percent = ps.mixdownPercent;
    if (ps.mixdownRunning) {
        const JobProgress job = m_deps.jobs->job(meetingId, JobKind::Mixdown);
        out.cancellable = job.isValid() && job.cancellable;
        out.cancelling = job.isValid() && job.cancelling;
    }
    return out;
}

void TracksServicesFacade::requestWaveforms(const QString& meetingId)
{
    if (m_deps.hooks.requestWaveforms) m_deps.hooks.requestWaveforms(meetingId);
}

void TracksServicesFacade::cancelWaveforms(const QString& meetingId)
{
    if (m_deps.waveforms) m_deps.waveforms->cancel(meetingId);
}

bool TracksServicesFacade::renameTrack(const QString& meetingId, const QString& trackId, const QString& name)
{
    return m_deps.tracks && m_deps.tracks->renameTrack(meetingId, trackId, name);
}

void TracksServicesFacade::restoreTrack(const QString& meetingId, const QString& trackId)
{
    if (m_deps.hooks.restoreTrack) m_deps.hooks.restoreTrack(meetingId, trackId);
}

void TracksServicesFacade::setTrackIncluded(const QString& meetingId, const QString& trackId, bool included)
{
    if (m_deps.hooks.setTrackIncluded) m_deps.hooks.setTrackIncluded(meetingId, trackId, included);
}

bool TracksServicesFacade::relocateTrack(const QString& meetingId, const QString& trackId, const QString& filePath,
                                         QString* error)
{
    return m_deps.tracks && m_deps.tracks->relocateTrack(meetingId, trackId, filePath, error);
}

int TracksServicesFacade::deleteDroppedTracks(const QString& meetingId)
{
    return m_deps.tracks ? m_deps.tracks->deleteDroppedTracks(meetingId) : 0;
}

void TracksServicesFacade::deleteTrack(const QString& meetingId, const QString& trackId)
{
    if (m_deps.hooks.deleteTrack) m_deps.hooks.deleteTrack(meetingId, trackId);
}

void TracksServicesFacade::regenerateMixdown(const QString& meetingId)
{
    if (m_deps.hooks.regenerateMixdown) m_deps.hooks.regenerateMixdown(meetingId);
}

} // namespace tanara_qml

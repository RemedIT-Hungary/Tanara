#pragma once
//
// TracksServicesFacade: the real tanara::library::ITracksFacade over the meeting repository,
// TrackCatalog, WaveformService, the job tracker and SettingsManager (the device names). The track
// operations that still live on the AppController (restore, include / exclude, delete, remix, the
// waveform request) come in as hooks, and its tracksChanged / mixdown notifications are forwarded
// by AppContext (report...). One per controller, built by AppContext::servicesFor().
//
#include "tanara/library/tracks/ITracksFacade.h"

#include <QMap>

#include <functional>

namespace tanara {
namespace app { class SettingsManager; }
namespace library { class MeetingJobTracker; class MeetingRepository; }
}

namespace tanara_qml {

class TracksServicesFacade : public tanara::library::ITracksFacade {
    Q_OBJECT
public:
    struct Hooks {
        std::function<void(const QString& meetingId)> requestWaveforms;
        std::function<void(const QString& meetingId, const QString& trackId)> restoreTrack;
        std::function<void(const QString& meetingId, const QString& trackId, bool included)> setTrackIncluded;
        std::function<void(const QString& meetingId, const QString& trackId)> deleteTrack;
        std::function<void(const QString& meetingId)> regenerateMixdown;
    };
    struct Deps {
        tanara::library::MeetingRepository* store = nullptr;
        tanara::library::TrackCatalog* tracks = nullptr;
        tanara::library::WaveformService* waveforms = nullptr;
        tanara::library::MeetingJobTracker* jobs = nullptr;
        tanara::app::SettingsManager* settings = nullptr;
        Hooks hooks;
    };
    explicit TracksServicesFacade(const Deps& deps, QObject* parent = nullptr);

    // The controller's notifications, forwarded by AppContext.
    void reportTracksChanged(const QString& meetingId) { emit tracksChanged(meetingId); }
    void reportMixdownUpdated(const QString& meetingId, bool ok) { emit mixdownUpdated(meetingId, ok); }
    void reportMixdownProgress(const QString& meetingId, int percent) { emit mixdownProgress(meetingId, percent); }

    tanara::Meeting meeting(const QString& meetingId) const override;
    tanara::library::MixdownJob mixdownJob(const QString& meetingId) const override;
    void requestWaveforms(const QString& meetingId) override;
    void cancelWaveforms(const QString& meetingId) override;
    bool renameTrack(const QString& meetingId, const QString& trackId, const QString& name) override;
    void restoreTrack(const QString& meetingId, const QString& trackId) override;
    void setTrackIncluded(const QString& meetingId, const QString& trackId, bool included) override;
    bool relocateTrack(const QString& meetingId, const QString& trackId, const QString& filePath,
                       QString* error) override;
    int deleteDroppedTracks(const QString& meetingId) override;
    void deleteTrack(const QString& meetingId, const QString& trackId) override;
    void regenerateMixdown(const QString& meetingId) override;

private:
    Deps m_deps;
    QMap<QString, QString> m_deviceNames;   // last seen state of the device renames
};


} // namespace tanara_qml

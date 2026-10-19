#pragma once
//
// RecordingService: the recording orchestration around RecordingSession.
//
//  - start / stop, adding and switching off a device's track during the recording;
//  - hot-plug: a recorded device missing from TWO consecutive enumerations is closed safely, a
//    detached device present again in TWO consecutive enumerations is recorded again (new span);
//  - live level monitoring (DeviceMonitor) for the recorder and other consumers (retain/release),
//    during a recording only on the devices NOT on a track;
//  - the call-end watch (watcher::CallMonitor, subscribed while recording) and the silence watch:
//    both only SIGNAL (callEnded / silenceDetected), stopping is the user's decision;
//  - the last used device names (state.json) and the tags given during the recording;
//  - keeping the machine awake while recording (platform::IPowerInhibitor, setting
//    recording.keepAwake): acquired right before the session starts, released when it finishes,
//    fails or the service is destroyed.
// What happens after the recording (speech check, mixdown, voice analysis) is the owner's job:
// readyForProcessing(meeting) tells when. UI-independent (QtCore).
#include "tanara/domain/Audio.h"
#include "tanara/domain/Meeting.h"
#include "tanara/media/MediaTool.h"
#include "tanara/recording/RecordingSettings.h"
#include "tanara/watcher/WatcherSettings.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara::library {
class MeetingRepository;
class TagService;
}
namespace tanara::watcher { class CallMonitor; }
namespace tanara::platform { class IPowerInhibitor; }

namespace tanara::recording {

class AudioEngine;
class DeviceManager;

class RecordingService : public QObject {
    Q_OBJECT
public:
    // The settings the service reads (fresh at every use).
    struct Config {
        RecordingSettings recording;
        watcher::WatcherSettings watcher;
    };
    struct Deps {
        library::MeetingRepository* store = nullptr;   // the finished meeting is saved here
        library::TagService* tags = nullptr;           // optional: validates the recording's tags
        media::MediaToolPtr media;                     // the track encoders
        watcher::CallMonitor* callMonitor = nullptr;   // optional: call-end watch while recording
        QString audioDir;                              // new meeting folders go here
        QString statePath;                             // state.json (last used devices); empty = not persisted
        std::function<Config()> config;
        // Optional: keeps the machine out of idle sleep while recording. nullptr = no protection
        // (keepAwakeState() reports "unavailable" when the setting is on).
        std::shared_ptr<platform::IPowerInhibitor> powerInhibitor;
    };

    explicit RecordingService(Deps deps, QObject* parent = nullptr);
    ~RecordingService() override;

    DeviceManager* devices() const;

    RecordingState state() const;
    QString currentFolder() const;   // the folder currently or last recorded

    // Starts recording with the given devices (empty = the auto-record set). Errors: error().
    void start(const QString& title, const QVector<AudioDeviceInfo>& devices = {});
    void stop();

    // During the recording: a further device's track starts / a device's track is switched off
    // (the last open track stays). False if not possible.
    bool addDevice(const AudioDeviceInfo& device);
    bool stopDevice(const QString& deviceName);
    QStringList liveDeviceNames() const;           // the open tracks' devices (track order)
    QStringList disconnectedDeviceNames() const;   // detached mid-run (not switched off by the user)

    // The last used devices (persistent).
    QStringList lastUsedDeviceNames() const;
    void setLastUsedDeviceNames(const QStringList& names);

    // Tags given during the recording: they go onto the meeting when it ends.
    void setTags(const QStringList& tagIds);
    QStringList tags() const;

    // Level monitoring: the start/stop pair (the recorder) and counted consumers (retain/release);
    // runs while any of them wants it. A destroyed owner releases automatically.
    void startLevelMonitoring();
    void stopLevelMonitoring();
    void retainLevelMonitoring(QObject* owner);
    void releaseLevelMonitoring(QObject* owner);

    // Keep-awake while recording: "held" (the inhibitor holds), "off" (setting recording.keepAwake
    // is false) or "unavailable" (no inhibitor, its backend is "none", or the acquire failed).
    // Meaningful during a recording; outside one nothing is held, so it reads "off" or "unavailable".
    QString keepAwakeState() const;
    // The inhibitor's mechanism ("logind", "portal", "windows", "macos-iopm", "none").
    QString powerInhibitorBackend() const;

    // Test seams.
    void setEngineFactory(std::function<std::unique_ptr<AudioEngine>()> factory);
    // The silence watch's monotonic clock (ms) and check interval. Defaults: wall clock, 10 s.
    void setClock(std::function<qint64()> nowMs);
    void setSilenceCheckIntervalMs(int ms);

signals:
    void stateChanged(tanara::RecordingState state);
    void elapsedChanged(qint64 ms);
    // Live level by device name: the recorded tracks and (from the level monitor) the other devices.
    void levelPeak(QString deviceName, float rms, float peak);
    void trackAdded(QString deviceName);
    void trackClosed(QString deviceName);
    void deviceReturned(QString deviceName);
    // The recording ended and the meeting is saved (before stateChanged(Idle)).
    void finished(tanara::Meeting meeting);
    // After finished() and stateChanged(Idle): post-processing may start.
    void readyForProcessing(tanara::Meeting meeting);
    void error(QString message);
    void callEnded(QString appName);
    void silenceDetected(int minutes);
    // keepAwakeState() changed (acquired at start, released at the end).
    void keepAwakeStateChanged();

private:
    void restartLevelMonitor(bool force);
    void beginLevelMonitoring();
    void endLevelMonitoring();
    void handleDeviceSetChange();
    void startWatches();
    void stopWatches();
    void acquireKeepAwake();
    void releaseKeepAwake();

    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara::recording

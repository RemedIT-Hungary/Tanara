#pragma once
//
// IRecorderFacade: what the floating recorder's view model (and the Settings device list) need
// from the recording side, as ONE abstract surface. The real implementation composes the
// recording / library services (gui/qml/src/RecorderServicesFacade); the fictional one
// (demo/RecorderDemoFacade) serves the design states for --demo, --gallery, --qml-shot and tests.
//
// It is a QObject only because it carries signals; there is no behaviour here. The view model
// never learns which implementation it talks to: everything the fixtures have to pin (a device
// row's level, the question being open, the finished state ...) is part of this surface.
#include "tanara/domain/Audio.h"
#include "tanara/library/tags/TagBackend.h"

#include <QDateTime>
#include <QMap>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace tanara::recording {

// A tag as the recorder shows it.
struct RecorderTag {
    QString id;
    QString name;
    bool isValid() const { return !id.isEmpty(); }
};

// A device row whose level and status are fixed by the source (fixtures / design states): the view
// model then does not compute them from the live signal.
struct DevicePin {
    int level = 0;            // 0..14 segments
    int peakSegment = -1;     // peak-hold segment index, -1 = none
    QString status;           // "" | "noSignal" | "signalUnrecorded" | "silentWarn" | "disconnected"
    QString statusText;
};

// The session as it stands when the view model attaches (or the source reloads it): normally empty
// (a new session starts); the fixtures preset a title, tags, the elapsed time, an open question or
// the finished state.
struct RecorderSession {
    QString title;                 // preset title; empty -> the automatic name
    QString automaticApp;          // the automatic name's app ("" -> "Meeting · ...")
    QDateTime automaticWhen;       // the automatic name's time; invalid -> now
    QVector<RecorderTag> tags;
    qint64 elapsedMs = 0;
    QString askText;               // non-empty: the "End of the meeting?" question is open
    bool done = false;             // the finished state ("Saved")
    QString doneSummary;
    QString doneMeetingId;
};

// The recording ended and the meeting is saved; the track files are already checked.
struct RecordingOutcome {
    QString meetingId;
    QString title;
    qint64 durationMs = 0;
    int tracksOk = 0;              // tracks whose audio file exists
    int tracksMissing = 0;
};

class IRecorderFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IRecorderFacade() override = default;

    // ---- devices ----
    virtual QVector<AudioDeviceInfo> captureDevices() const = 0;
    // The user's name for the device, otherwise the abbreviated one.
    virtual QString friendlyName(const QString& deviceName) const = 0;
    virtual std::optional<DevicePin> pinnedDisplay(const QString& deviceName) const
    {
        Q_UNUSED(deviceName);
        return std::nullopt;
    }
    // The application currently playing to this output ("" if none or unknown).
    virtual QString appForOutput(const QString& deviceName) const
    {
        Q_UNUSED(deviceName);
        return {};
    }
    virtual bool autoRecordAllDevices() const = 0;
    // The devices the recorder has already seen (to recognise a new one).
    virtual QStringList knownDevices() const = 0;
    virtual void setKnownDevices(const QStringList& names) = 0;
    virtual QStringList lastUsedDeviceNames() const = 0;
    virtual void setLastUsedDeviceNames(const QStringList& names) = 0;
    virtual void refreshDevices() = 0;
    // Watching the playback routes and hot-plug while a recorder is open (the platform's routing
    // where there is one, otherwise a timed re-enumeration). Default: nothing to watch.
    virtual void startWatching() {}
    virtual void stopWatching() {}
    virtual void startLevelMonitoring() = 0;
    virtual void stopLevelMonitoring() = 0;

    // ---- recording ----
    virtual RecordingState state() const = 0;
    virtual QString currentFolder() const = 0;
    virtual QStringList recordingDeviceNames() const = 0;          // the open tracks' devices
    virtual QStringList disconnectedDeviceNames() const = 0;       // detached mid-run
    // Recorded tracks delivering no data: device name -> recording-clock ms of the last data.
    virtual QMap<QString, qint64> stalledTracks() const { return {}; }
    // Lost tracks being re-opened (after a sleep / a lost device): their device names.
    virtual QStringList reopeningDeviceNames() const { return {}; }
    virtual void start(const QString& title, const QVector<AudioDeviceInfo>& devices) = 0;
    virtual void stop() = 0;
    virtual bool addDevice(const AudioDeviceInfo& device) = 0;     // false: not possible
    virtual bool stopDevice(const QString& deviceName) = 0;        // false: the last open track stays
    virtual void setRecordingTags(const QStringList& tagIds) = 0;
    // Keep-awake while recording (RecordingService::keepAwakeState): "held" | "off" | "unavailable";
    // empty = not known (no line in the recorder).
    virtual QString keepAwakeState() const { return {}; }
    // The user starts the next recording after a finished one (a fixture returns to its start state).
    virtual void beginNewSession() {}

    // ---- the finished meeting ----
    virtual void renameMeeting(const QString& meetingId, const QString& title) = 0;
    virtual void setMeetingContextNote(const QString& meetingId, const QString& note) = 0;
    virtual void setMeetingDetectedCall(const QString& meetingId, const QString& appName) = 0;

    // ---- tags ----
    // Creates the tag for a name (the existing one for an existing key); invalid: failed.
    virtual RecorderTag createTag(const QString& name) = 0;
    virtual std::optional<RecorderTag> tag(const QString& id) const = 0;   // nullopt: no such tag
    // The tag input's data (the whole pool, the recent ones, the suggestions); the recorder window's
    // TagInputModel attaches to it. nullptr: no tag input data.
    virtual library::TagBackend* tagBackend() const { return nullptr; }

    // ---- session ----
    virtual RecorderSession session() const { return {}; }

    // ---- hosting ----
    // Where the window's pin / expanded / position are remembered; empty: nothing is remembered.
    virtual QString windowStatePath() const { return {}; }
    // The recording.lock file the window host maintains; empty: no lock.
    virtual QString lockFilePath() const { return {}; }

signals:
    void devicesChanged();
    void deviceNamesChanged();                 // a device was renamed in Settings
    void routesChanged();
    void deviceLevel(QString deviceName, float rms, float peak);
    void stateChanged(tanara::RecordingState state);
    void elapsedChanged(qint64 ms);
    void trackAdded(QString deviceName);
    void trackClosed(QString deviceName);
    void deviceReturned(QString deviceName);
    // A recorded track delivers no data since sinceMs (recording clock, as elapsedChanged) / again.
    void trackStalled(QString deviceName, qint64 sinceMs);
    void trackResumed(QString deviceName);
    // A recorded track was lost (reason "sleep" | "device-lost") and is being re-opened; success comes
    // as deviceReturned (a new segment), giving up as trackReopenFailed + error.
    void trackReopening(QString deviceName, QString reason);
    void trackReopenFailed(QString deviceName, QString reason);
    void recordingFinished(tanara::recording::RecordingOutcome outcome);
    void callEnded(QString appName);
    void silenceDetected(int minutes);
    void keepAwakeStateChanged();
    void error(QString message);
    void tagsChanged();
    // The fixture loaded another session: the view model re-reads everything.
    void sessionChanged();
};

} // namespace tanara::recording

Q_DECLARE_METATYPE(tanara::recording::RecordingOutcome)

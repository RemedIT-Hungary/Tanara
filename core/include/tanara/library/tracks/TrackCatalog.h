#pragma once
//
// TrackCatalog: the backend of the "Tracks" tab: a friendly display name per track based on the
// device's ROLE, user rename, detecting and locating a missing audio file, permanently deleting the
// dropped tracks in one step. UI-independent.
//
// (Restoring / individually deleting a track and the mixdown live in AppController:
//  restoreTrack / deleteTrack / regenerateMixdown. The waveform: WaveformService.)
#include "tanara/domain/Meeting.h"

#include <QMap>
#include <QObject>
#include <QVector>

namespace tanara::library {

class MeetingStore;

// The device's role in the recording; the friendly name comes from it.
enum class TrackRole {
    OwnMic,        // own input -> "Own microphone"
    CallAudio,     // monitor / loopback of the call's device -> "Call audio"
    SystemAudio,   // another monitor / loopback -> "System audio"
    Other,         // line-in, AUX, unknown -> the device's (shortened) name
};

struct TrackView {
    Track     track;              // the raw track (id, file, kind, active, peakLevel ...)
    TrackRole role = TrackRole::Other;
    QString   displayName;        // what the UI shows: the user's name, otherwise the friendly name
    QString   friendlyName;       // the default name derived from the role (to undo a rename)
    QString   rawDeviceName;      // the raw device name (the UI shows it small underneath)
    bool      renamed = false;    // the user renamed it
    QString   absolutePath;       // full path of the audio file
    bool      fileMissing = false;// the audio file is not in place ("file missing" -> "Locate...")
    qint64    fileSize = 0;
    // Several spans of a device (a device switched on and off during recording -> several files): the
    // span's number from 1 (in offset order), or 0 if the device has a single file. The spans form one
    // logical track: shared name and role, one after another.
    int       segment = 0;
    int       segmentCount = 1;
    // The track's length in ms: from the waveform cache (the exact, decoded length) if it is already
    // done and valid; otherwise -1 (WaveformService::peaksReady gives it).
    qint64    durationMs = -1;
};

// ---- user-given names of audio devices --------------------------------------------------
// The user can rename their devices in Settings (AppSettings::deviceNames: raw OS name -> friendly
// name). THIS is the one place where the name is resolved: the recorder, the "Tracks" tab
// (tracknames::friendlyNames), the watcher's notification and Settings all ask here.
// The table is filled by the SettingsManager (at load and on every setSettings), process-wide;
// thread-safe.
namespace devicenames {

void setOverrides(const QMap<QString, QString>& rawToFriendly);
QMap<QString, QString> overrides();
// Whether the user has their own name for this device.
bool hasOverride(const QString& rawDeviceName);
// The name to display: the user's name, otherwise tracknames::shortDeviceName(raw) (if that is
// empty, the raw name itself). The monitor-prefixed form ("Monitor of X") is named under its own key:
// the output and the input are separate devices.
QString displayName(const QString& rawDeviceName);
// The same with a given table (for Settings' not yet saved draft).
QString displayName(const QString& rawDeviceName, const QMap<QString, QString>& overrides);

} // namespace devicenames

namespace tracknames {

// Whether the name points to the call (communication) device, e.g. the "Communication" / "Chat" /
// "Hands-Free" profile of headsets, which call apps use.
bool looksLikeCallDevice(const QString& deviceName);

// The short device name without the monitor/loopback prefix and the profile part
// ("Monitor of Kanto YU4 - Optical Digital stereo (IEC958)" -> "Kanto YU4").
QString shortDeviceName(const QString& deviceName);

// A device's spans. The files of the same device (same, non-empty deviceName) are spans of one logical
// track if ALL started with different offsets (startOffsetMs); the recorder keeps a device open only
// once at a time, so the spans of switching on and off always begin at different times. The same
// offset (e.g. two identically named microphones at once) -> separate logical tracks.
struct SegmentInfo {
    int leader = -1;   // index in `all` of the logical track's first (earliest) span
    int segment = 0;   // 1..count in offset order; 0 if the track is a single file
    int count = 1;
};
QVector<SegmentInfo> segments(const QVector<Track>& all);

// A track's role knowing ALL of the meeting's tracks:
//  - Mic -> OwnMic; Other -> Other;
//  - Loopback: if its name points to the call's device -> CallAudio. If no loopback's name points to
//    it: the single active loopback is CallAudio (the recorded call plays on it); among several active
//    ones the loudest (peakLevel). The rest are SystemAudio. A device's spans (segments) count together:
//    they share the role (active if any is active; their peak is the largest).
TrackRole classify(const Track& track, const QVector<Track>& all);

// Friendly names for all of the meeting's tracks (in the order of `all`). For tracks of the same role
// the short device name tells them apart ("System audio (Kanto YU4)"), as a last resort a number. A
// device's spans get the same name (the Tracks tab marks the span on the second line).
// If the user renamed the device (devicenames), the track gets that name instead of the role name:
// what they gave is what they see everywhere.
QStringList friendlyNames(const QVector<Track>& all);

} // namespace tracknames

class TrackCatalog : public QObject {
    Q_OBJECT
public:
    explicit TrackCatalog(MeetingStore* store, QObject* parent = nullptr);

    // The meeting's tracks ready for display: per logical track (in order of first occurrence), within
    // it the spans by offset. Without spans = the meeting.json order.
    QVector<TrackView> tracks(const QString& meetingId) const;
    static QVector<TrackView> tracks(const Meeting& meeting);

    // How many dropped (inactive) tracks there are, for the "delete permanently" button and the
    // confirmation's number.
    int droppedTrackCount(const QString& meetingId) const;

public slots:
    // Renames a track (persistent: meeting.json -> track.customName). An empty name goes back to the
    // friendly name. Every span of a device gets the new name together. true if changed. tracksChanged signal.
    bool renameTrack(const QString& meetingId, const QString& trackId, const QString& name);

    // Locates a missing audio file ("Locate..."): COPIES the chosen file into the meeting's folder (the
    // original stays untouched; if it is already in the folder, it only references it), and points the track
    // at it. For an active track the mixdown is marked stale. On error false and *error filled (no change).
    // tracksChanged signal.
    bool relocateTrack(const QString& meetingId, const QString& trackId,
                       const QString& newFilePath, QString* error = nullptr);

    // PERMANENT deletion of EVERY dropped (inactive) track in one step: the audio files and their waveform
    // cache are deleted, the tracks leave the meeting. Does not touch the active tracks or the mixdown.
    // Returns the number of tracks deleted. A single tracksChanged signal.
    int deleteDroppedTracks(const QString& meetingId);

signals:
    void tracksChanged(QString meetingId);

private:
    MeetingStore* m_store = nullptr;
};

} // namespace tanara::library
Q_DECLARE_METATYPE(tanara::library::TrackRole)
Q_DECLARE_METATYPE(tanara::library::TrackView)

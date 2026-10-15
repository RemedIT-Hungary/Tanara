#pragma once
//
// Control of one recording session:
//   capture (AudioEngine) -> per device an encoder (media::MediaTool::startEncoder, ffmpeg) raw PCM stdin -> Opus,
//   then at stop the per-track encoders are closed and a finished tanara::Meeting is produced.
//   The mixed .mp3 (mixdown) is NOT made here; AppController makes it asynchronously so that
//   stopping does not freeze the UI (see AppController::regenerateMixdown()).
#include "tanara/domain/Audio.h"
#include "tanara/domain/Meeting.h"
#include "tanara/media/MediaTool.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara::recording {

class AudioEngine;

class RecordingSession : public QObject {
    Q_OBJECT
public:
    // audioDir: the root of recordings (the meeting folder goes under it).
    // title: the meeting's title (for the folder name slug and Meeting.title).
    // userSpeakerName: the fixed speaker name of the first mic track (e.g. "Adam").
    // opusKbps: per-track Opus bitrate (from the audio quality setting; see opusBitrateKbps()).
    // media: the encoders' tool (shared; it must not be null).
    RecordingSession(media::MediaToolPtr media,
                              QString audioDir,
                              QString title,
                              QString userSpeakerName = QString(),
                              int opusKbps = 64,
                              QObject* parent = nullptr);
    ~RecordingSession() override;

    // Factory of the capture engine (call before start()). Empty = the real AudioEngine (miniaudio).
    // Tests drive the recorder with a fake engine, without an audio device.
    using EngineFactory = std::function<std::unique_ptr<AudioEngine>()>;
    void setEngineFactory(EngineFactory factory);
    // Turns the filling of silent capture gaps with silence (see tracktiming::silenceToInsert) on/off for
    // every track. By default per platform: only on Windows, only for a loopback track. For tests.
    void setFillCaptureGaps(bool on);

    RecordingState state() const;
    QString folder() const;          // absolute path of the meeting folder created

    // The tracks' devices in track-index order (those added during recording at the end).
    QVector<AudioDeviceInfo> trackDevices() const;
    // Whether the track is still alive (false if the device was detached and the track closed).
    bool trackOpen(int trackIndex) const;

public slots:
    // Creates the meeting folder, starts capture and, per device, the ffmpeg encoder + the drain
    // worker. Does not throw; emits failed() on error.
    void start(const QVector<AudioDeviceInfo>& devices);

    // Stops: flush + closeWriteChannel on every ffmpeg, waits for the per-track encoders to end (a
    // fast tail flush), then emits finished(Meeting) (or failed()). Does NOT block: the shutdown runs on
    // the worker's thread, and finished() comes when EVERY track file is closed on disk (until then the
    // state is Stopping -> Encoding). It NO LONGER makes the mixdown; `mixdownFile` stays empty;
    // AppController makes the mix later, asynchronously (in auto or manual mode).
    void stop();

    // Starts another device's track DURING recording. The track's file runs from the moment of opening
    // (NO silence padding); the open time relative to the recording's zero point is the track's
    // Track::startOffsetMs, from which the mixdown and every other consumer shift it into place.
    // Re-adding a previously closed device gives a new file (track_<slug>-2.ogg ...): a newer SPAN of the
    // same device (see TrackCatalog). false if no recording is running, the device is already on an (open)
    // track, or cannot be opened. On success: trackAdded().
    bool addDevice(const tanara::AudioDeviceInfo& device);

    // Safely closes a track during recording (the device was detached): capture stops, the remaining
    // audio is written out, the encoder closes the file. The recording continues with the other tracks; the
    // closed track stays part of the meeting (its file runs until the close). trackClosed().
    void closeTrack(const QString& deviceName);

signals:
    void stateChanged(tanara::RecordingState state);
    void levelMeterUpdated(int trackIndex, float rms);   // ~30 Hz, queued
    void trackLevel(int trackIndex, float rms, float peak);   // the same, with peak
    void trackAdded(int trackIndex, QString deviceName);
    void trackClosed(int trackIndex, QString deviceName);
    void elapsedChanged(qint64 ms);
    void finished(tanara::Meeting meeting);
    void failed(QString error);

private:
    void onFinalized(const QStringList& failedFiles);   // the worker closed the track files
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tanara::recording
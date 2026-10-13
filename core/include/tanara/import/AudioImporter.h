#pragma once
//
// AudioImporter: a new meeting from existing audio (or video) files, "as if recorded on an
// external interface": one track per source or, on request, a multichannel file split into one track
// per channel. UI-independent (QtCore).
//
//  - It does NOT touch the source files (only reads them); the tracks go through ffmpeg into the
//    same on-disk form as recorded tracks: track_<slug>.ogg, Opus, 48 kHz, at the configured
//    bitrate (opusBitrateKbps). So TrackCatalog, the mixdown, the waveform, voice identification
//    and the transcript editor work unchanged.
//  - Several files: all start at time 0 (no offset / synchronization).
//  - Per file the FIRST audio stream is taken (for video, its audio).
//  - The imported track's kind is TrackKind::Other, fixedSpeaker = false (all its audio goes
//    through speaker separation), its name is the file name without extension, or for a split
//    channel "Left channel" / "Right channel" / "channel N" (Hungarian captions in the UI). One track
//    can be marked as the user's own microphone: it becomes TrackKind::Mic, fixedSpeaker = true, its
//    speaker is the user's name (if unset: "Microphone 1", as for recorded tracks).
//  - A half-made meeting is NEVER visible: encoding goes into a hidden <audioDir>/.import-<id>
//    folder (orphan recovery skips dot-prefixed folders); only after full success does it get its
//    final name and meeting.json. After an error / cancel the folder is deleted.
//  - The importer does NOT make a mixdown: the caller (AppController) starts it per mixdownMode,
//    exactly as at the end of a recording.
#include "tanara/Types.h"

#include <QObject>
#include <QVector>
#include <memory>

namespace tanara {

class MeetingStore;

// ffprobe result of one source file.
struct ImportFileInfo {
    QString   path;               // absolute path
    bool      ok = false;         // contains a decodable audio stream
    QString   error;              // !ok: human explanation
    qint64    durationMs = 0;     // 0 = the container does not say (progress is then indeterminate)
    int       channels = 0;
    int       sampleRate = 0;
    QString   codec;              // e.g. "pcm_s16le", "aac", "opus"
    bool      hasVideo = false;   // a video container (only its audio is imported)
    qint64    sizeBytes = 0;
    QDateTime createdAt;          // creation time embedded in the file; invalid if none
    QDateTime modifiedAt;         // file modification time

    // Default for the meeting's start: the embedded time, otherwise the modification time.
    QDateTime bestDate() const { return createdAt.isValid() ? createdAt : modifiedAt; }
};

struct ImportSource {
    QString path;
    bool    splitChannels = false;   // channels become separate tracks (effective only with channels >= 2)
};

// A future track (derived from the request and the file info; the UI uses it to tell in advance how
// many tracks there will be, and "own microphone" can be chosen among them).
struct ImportPlannedTrack {
    int     sourceIndex = 0;
    int     channel = -1;         // -1 = the whole file; otherwise the source's channel (from 0)
    int     channels = 1;         // channel count of the finished track (1 or 2)
    QString name;                 // display name
};

struct ImportRequest {
    QVector<ImportSource> sources;
    QString   title;              // empty = audioimport::defaultTitle
    QDateTime startedAt;          // invalid = audioimport::defaultStart
    int       ownTrack = -1;      // index among the planned tracks of the own microphone; -1 = none
};

namespace audioimport {

// ffprobe, BLOCKING (CLI, tests). The UI uses AudioImporter::probeAsync.
ImportFileInfo probe(const QString& path, int timeoutMs = 20000);

// Whether to split into channels by default: yes for an AUDIO file with more than two channels (a
// multichannel interface recording); no for plain stereo and for video (the user decides).
bool splitByDefault(const ImportFileInfo& info);

// The future tracks in source order. `files` is parallel to `sources`.
QVector<ImportPlannedTrack> planTracks(const QVector<ImportSource>& sources,
                                       const QVector<ImportFileInfo>& files);

// Title: the file name without extension; for several files the common start of the names (if
// meaningful), otherwise the first file's name.
QString defaultTitle(const QStringList& paths);

// Start: the earliest valid of the sources' bestDate(); if none, the current time.
QDateTime defaultStart(const QVector<ImportFileInfo>& files);

// Channel name: for 2 channels "Left channel" / "Right channel", otherwise "channel N" (Hungarian captions in the UI).
QString channelName(int channel, int channelCount);

} // namespace audioimport

class AudioImporter : public QObject {
    Q_OBJECT
public:
    explicit AudioImporter(MeetingStore* store, QObject* parent = nullptr);
    ~AudioImporter() override;

    // The tracks' Opus bitrate (kbps) and the speaker name of the "own microphone" track.
    void setOpusBitrateKbps(int kbps);
    void setUserSpeakerName(const QString& name);

    bool    busy() const;          // an import is running (one at a time)
    QString currentId() const;     // id of the running import (= the future meeting)

    // Fetches a file's info in the background -> probed(). Several may run at once.
    void probeAsync(const QString& path);

    // Starts the import. Returns the future meeting's id; empty if one is already running.
    // Every further result comes in signals (started -> progress... -> finished | failed | cancelled).
    QString start(const ImportRequest& request);

public slots:
    // Cancels the running import: ffmpeg stops, the half-made folder is deleted -> cancelled().
    void cancel();

signals:
    void probed(tanara::ImportFileInfo info);
    void started(QString importId, QString title);
    // percent: 0..100 of the total work, -1 = not measurable; fileIndex: the running source (from 0).
    void progress(QString importId, int percent, int fileIndex, int fileCount);
    void finished(QString importId, tanara::Meeting meeting);
    void failed(QString importId, QString message, QString detail);
    void cancelled(QString importId);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::ImportFileInfo)
Q_DECLARE_METATYPE(tanara::ImportRequest)

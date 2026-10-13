#pragma once
//
// Meeting archive (export / import in a single file).
//
// Format: a plain ZIP (no encryption), base name "<folder name>.tanara.zip". Contents:
//   tanara-archive.json          - manifest (see ArchiveManifest; "version": 1)
//   <folder name>/...            - the files of the meeting's folder, names unchanged
// The folder name is exactly the name under the recordings folder (yyyy-MM-dd_HHmm_...), so it is
// created with the same name on the other machine. Left out: recording.lock, *.tmp, transcript-backup-*
// folders (large, not needed), *.embeddings.bin and profile.json (derived data, rebuilt by itself).
// Kept: audio, meeting.json (including the tag ids), mixdown, transcript, speaker corrections,
// summary, waveform cache, processing.json.
//
// The tag set lives per machine (<metadataDir>/tags.json), so the manifest also carries the
// NAMES of the meeting's tags; the layer doing the import (AppController) resolves / creates the
// local tags from them.
//
// The import only unpacks + validates; MeetingStore::adoptMeetingFolder puts it among the recordings
// (moved from the temporary .import-* folder, which is then cleaned up).
//
// UI-independent, thread-safe (no QObject, no shared state), so it can run on a worker thread.
#include "tanara/Types.h"
#include "tanara/tags/TagTypes.h"

#include <QDateTime>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>

namespace tanara {

struct ArchiveManifest {
    int       version = 0;
    QString   app;            // version of the exporting Tanara
    QDateTime exportedAt;
    QString   meetingId;
    QString   title;
    QDateTime startedAt;
    qint64    durationMs = 0;
    int       trackCount = 0;
    QVector<Tag> tags;        // the meeting's tags (id + name), in the order applied
};

class MeetingArchive {
public:
    static constexpr int kFormatVersion = 1;
    using Progress = std::function<void(int percent)>;

    static QString manifestFileName() { return QStringLiteral("tanara-archive.json"); }
    static QString fileSuffix() { return QStringLiteral(".tanara.zip"); }
    // Suggested file name: "<folder name>.tanara.zip".
    static QString suggestedFileName(const Meeting& m);
    // Whether the relative (slash-separated) path inside the folder is left out of the archive.
    static bool isExcluded(const QString& relativePath);

    // Packs the meeting's folder into zipPath (first into a ".part" file, renamed at the end;
    // overwrites an existing target). tags: the meeting's tags for the manifest.
    // cancel: if it turns true, the half-written file is deleted and false + "Cancelled." is returned.
    static bool exportMeeting(const Meeting& m, const QString& zipPath, QString* error,
                              Progress progress = {}, const QVector<Tag>& tags = {},
                              const std::atomic<bool>* cancel = nullptr);

    // Unpacks under tempRoot/.import-<uuid>/ (tempRoot = the recordings folder, so adoption works by
    // rename). Checks: a manifest exists, version == 1, exactly one top-level folder, in it
    // meeting.json or track_*.ogg, no absolute / ".." path, no encrypted entry. Returns the path of the
    // unpacked meeting folder; on error empty + *error, and the temporary folder is deleted.
    static QString importArchive(const QString& zipPath, const QString& tempRoot, QString* error,
                                 Progress progress = {}, ArchiveManifest* manifest = nullptr);

    // Prefix of the temporary unpack folders (a hidden folder under the recordings folder).
    static QString tempDirPrefix() { return QStringLiteral(".import-"); }
};

} // namespace tanara

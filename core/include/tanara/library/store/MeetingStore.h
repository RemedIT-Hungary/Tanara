#pragma once
//
// Meeting store.
//
// The DISK is the source of truth: every meeting is a folder under audioDir
//   yyyy-MM-dd_HHmm_<titleslug>_<shortid>
// containing meeting.json (JsonSerialization). <metadataDir>/index.db (Qt6::Sql,
// SQLite) only caches the metadata for fast listing and can be rebuilt from disk any time.
#include "tanara/domain/Meeting.h"
#include "tanara/media/MediaTool.h"
#include <QObject>
#include <QVector>

namespace tanara::library {


class MeetingStore : public QObject {
    Q_OBJECT
public:
    // audioDir: the (primary) recordings root; metadataDir: the data folder (index.db). Typically
    // AppPaths::primaryRecordingRoot() / dataDir().
    explicit MeetingStore(const QString& audioDir,
                          const QString& metadataDir,
                          QObject* parent = nullptr);

    ~MeetingStore() override;

    // Creates a new meeting: folder + empty meeting.json, written to the index.
    Meeting createMeeting(const QString& title);

    // Writes meeting.json into the meeting's folder + updates the index.
    void saveMeeting(const Meeting& m);

    // Recovers orphan recording folders (track_*.ogg PRESENT, meeting.json ABSENT, e.g. a crash during
    // recording): rebuilds the Meeting from the tracks (length via ffprobe), writes meeting.json and
    // indexes it. Returns the number of folders recovered. Skipped: a folder still being written to:
    // the recording.lock (live PID) folder, or a folder where any track file changed in the last few minutes.
    int recoverOrphanRecordings();
    // Adopts a recording folder (e.g. from another machine, a USB stick): if it is not under the
    // recordings folder, it is COPIED there (same name; an error if it already exists), meeting.json is
    // fixed with the actual location, and only THIS one meeting is added to the index; other folders are
    // not looked at (deleted / test recordings do not come back). Without meeting.json it is built from
    // the track_*.ogg tracks, titled "(recovered)". Error: an empty Meeting + *error.
    // For an archive import (MeetingArchive) the source is in the temporary .import-<uuid> folder under
    // the recordings folder: then it is MOVED (not copied), and the temporary folder is deleted
    // regardless of success. Also an error if the same id is already in the library in another folder.
    Meeting adoptMeetingFolder(const QString& sourceDir, QString* error = nullptr);
    // Copies an external folder to the temporary .import-<uuid>/<name> location under the recordings
    // folder (callable from a background thread: touches only the file system). adoptMeetingFolder
    // moves the result to its final place and cleans up the temp folder. Error: empty + *error.
    QString stageFolderForImport(const QString& sourceDir, QString* error = nullptr) const;

    // All meetings from the (disk cache) index, descending by startedAt.
    QVector<Meeting> loadAll();

    // Loads one meeting by id (from disk).
    Meeting load(const QString& id);

    // Reads the meeting.json of a meeting folder (without the index). Callable from any thread: touches
    // only the file system. Error / missing file: an empty Meeting.
    static Meeting readMeetingFolder(const QString& folder);
    // The meeting's folder from the index (only the index query, no file reading).
    QString folderOf(const QString& id);

    // PERMANENT deletion of a meeting: the folder (audio + transcript + summary) + the index entry.
    // meetingRemoved signal. true if there was something to delete.
    // The folder STAYS on disk (only the index row is deleted) if another listed meeting points to it,
    // its meeting.json contains a different id, or a live recording is going into it.
    bool deleteMeeting(const QString& id);

    // Full index rebuild by scanning the folders of audioDir.
    void rebuildIndexFromDisk();

    QString audioDir() const { return m_audioDir; }
    QString metadataDir() const { return m_metadataDir; }

    // The media tool for the track lengths of recovered folders (probe). Without one the recovered
    // meeting's length is 0 (the tracks still play).
    void setMediaTool(media::MediaToolPtr media) { m_media = std::move(media); }

signals:
    void meetingAdded(QString id);
    void meetingUpdated(QString id);
    void meetingRemoved(QString id);

private:
    void openDb();
    void ensureSchema();
    void upsertIndex(const Meeting& m);
    Meeting meetingFromTrackFolder(const QString& folder) const;
    QString dbConnectionName() const;
    QString meetingJsonPath(const QString& folder) const;
    bool folderOwnedOnlyBy(const QString& id, const QString& folder) const;
    bool isLiveRecordingFolder(const QString& folder) const;

    QString m_audioDir;
    QString m_metadataDir;
    QString m_connName;   // unique QSqlDatabase connection name
    media::MediaToolPtr m_media;
};

} // namespace tanara::library
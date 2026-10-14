#pragma once
//
// MeetingRepository: the meetings of the library, across every recording root.
//
// The DISK is the source of truth: every meeting is a folder directly under one of the recording
// roots (AppPaths::recordingRoots(), the first is the primary)
//   yyyy-MM-dd_HHmm_<titleslug>_<shortid>
// containing meeting.json (MeetingLayout::meetingJson()). <metadataDir>/index.db (Qt6::Sql, SQLite)
// only caches the metadata for fast listing and can be rebuilt from disk any time (reindex).
//
// The index does not store a meeting's absolute folder: a row holds the folder's root and its name
// inside the root, so moving a root (moveMeetings) only rewrites the root column. meeting.json does
// not store the folder either (it is where the file is). The meeting id does not depend on the root.
// New meetings always go into the primary root.
#include "tanara/domain/Meeting.h"
#include "tanara/media/MediaTool.h"

#include <QObject>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

namespace tanara::library {

class MeetingRepository : public QObject {
    Q_OBJECT
public:
    // roots: the recording roots, the first is the primary (new recordings); metadataDir: the data
    // folder (index.db). Typically AppPaths::recordingRoots() / dataDir().
    explicit MeetingRepository(const QStringList& roots,
                               const QString& metadataDir,
                               QObject* parent = nullptr);
    // A single root.
    MeetingRepository(const QString& audioDir, const QString& metadataDir, QObject* parent = nullptr);

    ~MeetingRepository() override;

    // ---- roots ------------------------------------------------------------------------------
    QStringList roots() const { return m_roots; }
    QString primaryRoot() const { return m_roots.value(0); }
    QString audioDir() const { return primaryRoot(); }   // the primary root (new recordings, imports)
    QString metadataDir() const { return m_metadataDir; }
    // Replaces the root list (the first is the primary). The index keeps rows of a dropped root
    // until the next reindex. Empty or duplicate entries are ignored.
    void setRoots(const QStringList& roots);
    // The root a meeting folder lies in (its parent folder), cleaned; empty for an empty folder.
    static QString rootOf(const QString& meetingFolder);
    // How many times the index was rebuilt automatically (a root had meeting.json folders but no index
    // rows: deleted index, restored backup). Mostly for tests.
    int autoReindexCount() const { return m_autoReindexCount; }
    // Number of meeting folders (meeting.json or track files) directly in a root, from DISK.
    static int meetingFolderCount(const QString& root);

    // The media tool for the track lengths of recovered folders (probe). Without one the recovered
    // meeting's length is 0 (the tracks still play).
    void setMediaTool(media::MediaToolPtr media) { m_media = std::move(media); }

    // ---- meetings ---------------------------------------------------------------------------
    // Creates a new meeting in the primary root: folder + meeting.json, written to the index.
    Meeting createMeeting(const QString& title);

    // Writes meeting.json into the meeting's folder + updates the index.
    void saveMeeting(const Meeting& m);
    // Writes only meeting.json (no index, no signal): for tools working on a bare folder (CLI).
    static bool writeMeetingFile(const Meeting& m);

    // Recovers orphan recording folders in EVERY root (track_*.ogg PRESENT, meeting.json ABSENT, e.g. a
    // crash during recording): rebuilds the Meeting from the tracks (length via ffprobe), writes
    // meeting.json and indexes it. Returns the number of folders recovered. Skipped: a folder still being
    // written to: the recording.lock (live PID) folder, or a folder where any track file changed in the
    // last few minutes.
    int recoverOrphanRecordings();
    // Adopts a recording folder (e.g. from another machine, a USB stick): if it is not directly under one
    // of the roots, it is COPIED into the primary root (same name; an error if it already exists),
    // and only THIS one meeting is added to the index; other folders are not looked at (deleted / test
    // recordings do not come back). Without meeting.json it is built from the track_*.ogg tracks, titled
    // "(recovered)". Error: an empty Meeting + *error.
    // For an archive import (MeetingArchive) the source is in the temporary .import-<uuid> folder under
    // the primary root: then it is MOVED (not copied), and the temporary folder is deleted regardless of
    // success. Also an error if the same id is already in the library in another folder.
    Meeting adoptMeetingFolder(const QString& sourceDir, QString* error = nullptr);
    // Copies an external folder to the temporary .import-<uuid>/<name> location under the primary root
    // (callable from a background thread: touches only the file system). adoptMeetingFolder
    // moves the result to its final place and cleans up the temp folder. Error: empty + *error.
    QString stageFolderForImport(const QString& sourceDir, QString* error = nullptr) const;
    // Deletes the temporary folders of interrupted archive imports older than maxAgeSecs in every root.
    void removeStaleImportFolders(qint64 maxAgeSecs);

    // All meetings from the (disk cache) index, descending by startedAt.
    QVector<Meeting> loadAll();

    // Loads one meeting by id (from disk).
    Meeting load(const QString& id);

    // Reads the meeting.json of a meeting folder (without the index); the folder is filled in.
    // Callable from any thread: touches only the file system. Error / missing file: an empty Meeting.
    static Meeting readMeetingFolder(const QString& folder);
    // The meeting's folder from the index (only the index query, no file reading).
    QString folderOf(const QString& id);

    // PERMANENT deletion of a meeting: the folder (audio + transcript + summary) + the index entry.
    // meetingRemoved signal. true if there was something to delete.
    // The folder STAYS on disk (only the index row is deleted) if another listed meeting points to it,
    // its meeting.json contains a different id, or a live recording is going into it.
    bool deleteMeeting(const QString& id);

    // Index rebuild from the meeting.json files of every root (no orphan recovery).
    void reindex();
    // Full rebuild: orphan recovery + reindex.
    void rebuildIndexFromDisk();

    // ---- moving a root ----------------------------------------------------------------------
    struct MovePlan {
        QStringList folders;        // folder names to move (directly under fromRoot)
        QStringList skipped;        // folder names left in place (live recording)
        qint64 bytes = 0;           // what has to be copied (0 if a rename does it: same file system)
        qint64 bytesAvailable = -1; // free space at toRoot (-1 = unknown)
        bool sameFileSystem = false;
        QString error;              // a reason the move cannot start (empty = it can)
    };
    struct MoveResult {
        QStringList moved;          // folder names moved (index updated)
        QStringList skipped;
        QStringList failed;         // "name: reason"
        bool cancelled = false;
        QString error;              // the reason it did not start
        bool ok() const { return error.isEmpty() && failed.isEmpty() && !cancelled; }
    };
    // progress(done folders, total folders, current folder name)
    using MoveProgress = std::function<void(int, int, const QString&)>;

    // What moveMeetings would do (nothing is changed).
    MovePlan planMove(const QString& fromRoot, const QString& toRoot) const;
    // Moves every meeting folder of fromRoot to toRoot, one folder at a time: a rename on the same file
    // system, otherwise copy -> verify (file list + sizes) -> delete the source. Each moved folder is
    // re-indexed at once (meetingUpdated). Before starting it checks the free space. The folder of a live
    // recording (recording.lock) is skipped. cancel is checked between folders and between files; an
    // interrupted copy leaves the source intact and its partial copy is removed. Running it again
    // continues where it stopped (a complete copy left by an interruption is recognized and finished).
    // Neither root needs to be in roots(); updating the settings is the caller's business.
    // mode Copy: copy -> verify -> delete even on the same file system (never a bare rename).
    enum class MoveMode { Auto, Copy };
    MoveResult moveMeetings(const QString& fromRoot, const QString& toRoot,
                            const std::atomic<bool>* cancel = nullptr,
                            const MoveProgress& progress = MoveProgress(),
                            MoveMode mode = MoveMode::Auto);

signals:
    void meetingAdded(QString id);
    void meetingUpdated(QString id);
    void meetingRemoved(QString id);

private:
    void openDb();
    // Rebuilds the index if any root holds meeting.json folders yet has 0 indexed rows; emits
    // meetingAdded for every meeting then. true if it rebuilt.
    bool reindexIfRootsUnindexed();
    void ensureSchema();
    void upsertIndex(const Meeting& m);
    Meeting meetingFromTrackFolder(const QString& folder) const;
    QString dbConnectionName() const;
    QString folderFromIndex(const QString& id) const;
    bool folderOwnedOnlyBy(const QString& id, const QString& folder) const;
    bool isRoot(const QString& path) const;
    int recoverOrphansIn(const QString& root);

    QStringList m_roots;      // cleaned absolute paths; [0] = primary
    QString m_metadataDir;
    QString m_connName;       // unique QSqlDatabase connection name
    media::MediaToolPtr m_media;
    int m_autoReindexCount = 0;
};

} // namespace tanara::library

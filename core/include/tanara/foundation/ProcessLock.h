#pragma once
//
// ProcessLock (formerly RecordingLock): a PID-checked marker file between processes, today the
// "is a recording going on" lock (<state>/recording.lock).
// The recorder (tanara --record) takes it at startup and releases it at shutdown; the watcher
// reads it so it does not offer to record if one is already running. Headless (core).
//
// The lock stores the recorder's PID + the meeting folder (JSON). A stale (post-crash) lock is
// detected by checking whether the PID is alive (POSIX kill(pid,0) / Win OpenProcess): if the PID no
// longer lives, the lock counts as inactive.
#include <QString>

namespace tanara {

// ---- instance scope (sandbox isolation) ------------------------------------------
// With TANARA_HOME "another Tanara" runs (test / sandbox): with its own metadata folder, its own lock
// files AND its own recorder singleton name. So a sandbox instance never receives a request from the
// user's real recorder, never forwards to it, and never blocks it.
//
// The scope suffix: empty if there is no TANARA_HOME (the real instance's name is unchanged);
// otherwise "-<first 12 hex digits of the SHA-1 of the resolved folder>". For local socket / IPC names.
QString instanceScopeSuffix();
// The lock files live in AppPaths' state folder (settings-independent), so every process agrees,
// and with TANARA_HOME they are in the sandbox.
QString recordingLockPath();
QString watcherLockPath();

class ProcessLock {
public:
    explicit ProcessLock(const QString& lockPath);

    // At recording start: writes its own PID + the meeting folder. true on success.
    bool acquire(const QString& meetingFolder);
    // At recording end: deletes the lock (only if ours; a foreign PID's lock is not touched).
    void release();

    struct Info {
        bool    active = false;      // there is a LIVE recorder (the PID exists)
        qint64  pid = 0;
        QString meetingFolder;
        QString startedAt;           // ISO-8601
    };
    // The watcher calls this: is a live recording running? A stale (dead PID) lock -> {active:false}.
    static Info read(const QString& lockPath);

private:
    QString m_path;
    bool    m_owned = false;         // whether we took it (release deletes only then)
};

// The old name; kept until the module folders move (P5).
using RecordingLock = ProcessLock;

} // namespace tanara

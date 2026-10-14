#pragma once
//
// foundation::platform: the small OS facts and calls the common code needs, behind one header.
//
// The declarations here are platform-neutral; the definitions live in
// core/src/foundation/platform/<os>/ and core/CMakeLists.txt compiles only the current platform's
// folders (Linux: linux + posix, Windows: windows, otherwise: posix + unsupported). Common code
// calls these instead of writing `#if Q_OS_*` (the platform rule of tools/check_modules.py).
#include <QString>
#include <QtGlobal>

namespace tanara::platform {

// Stable OS id: "linux" | "windows" | "macos" | "unknown" (e.g. the Cloud client header).
QString osId();

// An executable's file name from its base name: "tanara-watcher" -> "tanara-watcher.exe" on
// Windows, unchanged elsewhere.
QString executableName(const QString& baseName);

// The OS account's display name as the OS reports it (Windows: NameDisplay, then %USERNAME%;
// POSIX: the first gecos field, then the login name, then $USER). Empty if none is known.
// Not trimmed or capitalized; the caller decides how to present it.
QString userDisplayName();

// A suffix that makes a per-user resource name unique among the users of the machine:
// "-<uid>" on POSIX (local sockets share one namespace), empty on Windows (named pipes are
// already per session).
QString perUserSuffix();

// Whether a process with this PID is alive (stale-lock detection). false for pid <= 0.
bool processAlive(qint64 pid);

// Sends one log line (without its newline) to the platform debugger channel (Windows:
// OutputDebugString, with a newline appended). No-op elsewhere.
void debugOutput(const QString& line);

} // namespace tanara::platform

namespace tanara::paths {

// Whether file names on the platform's default file system differ by case (Linux: yes; Windows
// and macOS: no). Used where two file names must not refer to the same file.
bool caseSensitiveFileSystem();

} // namespace tanara::paths

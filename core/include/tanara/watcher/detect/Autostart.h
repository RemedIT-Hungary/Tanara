#pragma once
//
// Autostart: starting the call watcher (tanara-watcher) at login.
//
// Linux: ~/.config/autostart/tanara-watcher.desktop (XDG autostart).
// Windows: HKCU\Software\Microsoft\Windows\CurrentVersion\Run, the "Tanara Watcher" value =
// the quoted path of tanara-watcher.exe. macOS: not implemented yet (managed() is false).
//
// PROTECTION: with TANARA_HOME (sandbox / test) and in Qt test mode it writes and deletes NOTHING;
// the user's real autostart entry can never change from a trial run. This rule is kept in one
// place, here (the watcher and Settings both call it).
#include <QString>

namespace tanara::watcher::autostart {

// The autostart entry's path on this platform (empty if unsupported). On Windows a human-readable
// registry path ("HKEY_CURRENT_USER\...\Run\Tanara Watcher"), not a file.
QString watcherEntryPath();

// Whether the entry is on now (Linux: the .desktop file exists; Windows: the Run value exists). Read
// only, so the sandbox protection is not needed here.
bool isWatcherEnabled();

// Whether the entry can be managed now: a supported platform, no TANARA_HOME, not test mode.
bool managed();

// The watcher's executable relative to the given folder (next to `tanara`, or ../watcher/ in the
// build tree). Empty if not found.
QString findWatcherExecutable(const QString& applicationDir);

// Turns the entry on / off. false and NOTHING happens if !managed(), or if on enabling the
// executable path is empty.
bool applyWatcher(bool on, const QString& watcherExecutable);

// Writes the .desktop file to a given path (WITHOUT the protection; only applyWatcher and the tests
// call it, the tests with a temporary folder).
bool writeEntry(const QString& entryPath, const QString& watcherExecutable);

// Windows Run-key operations on a key under HKCU (subKey e.g. kRunSubKey), WITHOUT the protection;
// applyWatcher and the tests call them (the tests with their own key, deleted afterwards).
// On another platform: false / empty.
inline constexpr const char* kRunSubKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr const char* kRunValueName = "Tanara Watcher";
bool writeRunValue(const QString& subKey, const QString& valueName, const QString& watcherExecutable);
QString readRunValue(const QString& subKey, const QString& valueName);   // empty if none
bool removeRunValue(const QString& subKey, const QString& valueName);    // true if (already) gone

} // namespace tanara::watcher::autostart
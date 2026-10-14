#pragma once
//
// The Windows autostart entry: a value under HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
#include <QString>

namespace tanara::watcher::autostart {

// Windows Run-key operations on a key under HKCU (subKey e.g. kRunSubKey), WITHOUT the protection;
// the Windows IAutostart and the tests call them (the tests with their own key, deleted afterwards).
inline constexpr const char* kRunSubKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr const char* kRunValueName = "Tanara Watcher";
bool writeRunValue(const QString& subKey, const QString& valueName, const QString& watcherExecutable);
QString readRunValue(const QString& subKey, const QString& valueName);   // empty if none
bool removeRunValue(const QString& subKey, const QString& valueName);    // true if (already) gone

} // namespace tanara::watcher::autostart

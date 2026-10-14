#pragma once
//
// The Linux autostart entry (XDG): $XDG_CONFIG_HOME/autostart/tanara-watcher.desktop.
#include <QString>

namespace tanara::watcher::autostart {

// Writes the .desktop file to a given path (WITHOUT the protection; only the Linux IAutostart and the
// tests call it, the tests with a temporary folder). false if a path is empty or the write fails.
bool writeEntry(const QString& entryPath, const QString& watcherExecutable);

} // namespace tanara::watcher::autostart

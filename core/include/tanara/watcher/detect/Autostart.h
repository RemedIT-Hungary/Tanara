#pragma once
//
// Autostart: starting the call watcher (tanara-watcher) at login.
//
// The OS mechanism is a port, watcher::IAutostart, implemented in core/src/watcher/platform/<os>/:
//   Linux:   ~/.config/autostart/tanara-watcher.desktop (XDG autostart; platform/linux/XdgAutostart.h).
//   Windows: HKCU\Software\Microsoft\Windows\CurrentVersion\Run, the "Tanara Watcher" value = the
//            quoted path of tanara-watcher.exe (platform/windows/RunKey.h).
//   Otherwise (macOS until docs/macos.md is implemented): unsupported, entryPath() is empty.
//
// PROTECTION: with TANARA_HOME (sandbox / test) and in Qt test mode it writes and deletes NOTHING;
// the user's real autostart entry can never change from a trial run. This rule is kept in one
// place, the guarded functions below (the watcher, Settings, onboarding and the CLI all call them);
// the IAutostart implementations themselves are unguarded.
#include <QString>

#include <memory>

namespace tanara::watcher {

class IAutostart {
public:
    virtual ~IAutostart() = default;

    // The entry's location, for display (Linux: the .desktop file; Windows: a human-readable registry
    // path "HKEY_CURRENT_USER\...\Run\Tanara Watcher"). Empty = autostart is not supported here.
    virtual QString entryPath() const = 0;
    // Whether the entry is on now. Read only.
    virtual bool isEnabled() const = 0;
    // Turns the entry on for the given executable (false if it is empty or the write fails) / off
    // (true if it is gone, also if it was not there). UNGUARDED: call through autostart::applyWatcher().
    virtual bool enable(const QString& watcherExecutable) = 0;
    virtual bool disable() = 0;
};

// This platform's implementation (core/src/watcher/platform/<os>/).
std::unique_ptr<IAutostart> createPlatformAutostart();

namespace autostart {

// The process-wide platform implementation (created on first use).
IAutostart& platformAutostart();

// The autostart entry's path on this platform (empty if unsupported).
QString watcherEntryPath();

// Whether the entry is on now. Read only, so the sandbox protection is not needed here.
bool isWatcherEnabled();

// Whether the entry can be managed now: a supported platform, no TANARA_HOME, not test mode.
bool managed();
bool managed(const IAutostart& impl);

// The watcher's executable relative to the given folder (next to `tanara`, or ../watcher/ in the
// build tree). Empty if not found.
QString findWatcherExecutable(const QString& applicationDir);

// Turns the entry on / off. false and NOTHING happens if !managed(), or if on enabling the
// executable path is empty. The overload with `impl` is the same guard over another implementation.
bool applyWatcher(bool on, const QString& watcherExecutable);
bool applyWatcher(IAutostart& impl, bool on, const QString& watcherExecutable);

} // namespace autostart

} // namespace tanara::watcher

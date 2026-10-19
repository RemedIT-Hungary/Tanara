#pragma once
//
// platform::IPowerInhibitor: keeps the machine from going to idle sleep while a recording runs
// (a sleeping laptop disables the microphone and the rest of the meeting is lost). The display may
// still turn off. A port; the implementations are in core/src/foundation/platform/<os>/:
//   Linux:   logind Manager.Inhibit("sleep:idle", ..., "block") over the system bus; the returned
//            file descriptor is the hold (closing it releases). Without logind (e.g. inside a
//            Flatpak) the desktop portal org.freedesktop.portal.Inhibit (suspend | idle); the hold
//            is the returned request handle, released with Request.Close.
//   Windows: SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) on the calling thread
//            (not ES_DISPLAY_REQUIRED); release resets it to ES_CONTINUOUS. The flag belongs to the
//            THREAD, so acquire() and release() must run on the same, long-lived thread (the main
//            thread is fine).
//   macOS:   IOPMAssertionCreateWithName(PreventUserIdleSystemSleep) / IOPMAssertionRelease
//            (compiled in the unsupported folder under __APPLE__, untested; see docs/macos.md).
//   Otherwise: a no-op, held() stays false, backendName() is "none".
//
// Every hold dies with the process (the fd closes, the D-Bus connection drops, the thread state
// goes away), so a crash never leaves the machine unable to sleep. Not thread-safe: one owner.
#include <QString>

#include <memory>

namespace tanara::platform {

class IPowerInhibitor {
public:
    virtual ~IPowerInhibitor() = default;

    // Starts holding the machine awake; `reason` is shown by the OS tools (e.g. systemd-inhibit
    // --list). Idempotent: a second call while held keeps the existing hold. true = held now.
    virtual bool acquire(const QString& reason) = 0;
    // Ends the hold. Idempotent (no-op when nothing is held).
    virtual void release() = 0;
    virtual bool held() const = 0;
    // The mechanism, for the log and the diagnostics: "logind", "portal", "windows", "macos-iopm",
    // "none". On Linux it is the one that took effect at the last acquire(); before the first
    // acquire it is the one that will be tried first.
    virtual QString backendName() const = 0;
};

// This platform's implementation (core/src/foundation/platform/<os>/).
std::unique_ptr<IPowerInhibitor> makePowerInhibitor();

} // namespace tanara::platform

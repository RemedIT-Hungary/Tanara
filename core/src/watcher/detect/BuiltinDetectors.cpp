#include "tanara/watcher/detect/DetectorRegistry.h"
#include "tanara/watcher/detect/IMeetingDetector.h"

// The concrete headers of the built-in, platform-specific detectors appear ONLY here (under their
// own platform #if); the registry itself stays platform-agnostic.
#if defined(Q_OS_LINUX)
#include "tanara/watcher/detect/LinuxCaptureDetector.h"
#endif
#if defined(Q_OS_WIN)
#include "tanara/watcher/detect/WindowsCaptureDetector.h"
#endif

namespace tanara::watcher {

namespace {

#if defined(Q_OS_LINUX)
DetectorDescriptor linuxCaptureDescriptor()
{
    DetectorDescriptor d;
    d.id             = QStringLiteral("linux-capture");
    d.displayName    = QStringLiteral("Linux — mikrofon-capture (PipeWire)");
    d.platform       = QStringLiteral("linux");
    d.derivesAppName = true;
    return d;
}
#endif

#if defined(Q_OS_WIN)
DetectorDescriptor windowsWasapiDescriptor()
{
    DetectorDescriptor d;
    d.id             = QStringLiteral("windows-wasapi");
    d.displayName    = QStringLiteral("Windows — mikrofon-session (WASAPI)");
    d.platform       = QStringLiteral("windows");
    d.derivesAppName = true;
    return d;
}
#endif

} // namespace

void registerBuiltinDetectors()
{
    // Idempotent: repeated calls (watcher + the recorder's --record mode) must not duplicate.
    static bool registered = false;
    if (registered)
        return;
    registered = true;

#if defined(Q_OS_LINUX)
    MeetingDetectorRegistry::instance().registerDetector(
        linuxCaptureDescriptor(),
        []() -> IMeetingDetector* { return new LinuxCaptureDetector(); });
#endif
#if defined(Q_OS_WIN)
    MeetingDetectorRegistry::instance().registerDetector(
        windowsWasapiDescriptor(),
        []() -> IMeetingDetector* { return new WindowsCaptureDetector(); });
#endif
    // macOS (CoreAudio) detector: a later round.
}

} // namespace tanara::watcher
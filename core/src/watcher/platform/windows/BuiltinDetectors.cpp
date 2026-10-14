// The built-in meeting detectors on Windows.
#include "tanara/watcher/detect/DetectorRegistry.h"
#include "tanara/watcher/detect/IMeetingDetector.h"
#include "tanara/watcher/platform/windows/WindowsCaptureDetector.h"

namespace tanara::watcher {

namespace {

DetectorDescriptor windowsWasapiDescriptor()
{
    DetectorDescriptor d;
    d.id             = QStringLiteral("windows-wasapi");
    d.displayName    = QStringLiteral("Windows — mikrofon-session (WASAPI)");
    d.platform       = QStringLiteral("windows");
    d.derivesAppName = true;
    return d;
}

} // namespace

void registerBuiltinDetectors(MeetingDetectorRegistry& registry)
{
    registry.registerDetector(
        windowsWasapiDescriptor(),
        []() -> IMeetingDetector* { return new WindowsCaptureDetector(); });
}

} // namespace tanara::watcher

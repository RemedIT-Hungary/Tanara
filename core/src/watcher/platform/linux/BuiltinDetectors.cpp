// The built-in meeting detectors on Linux.
#include "tanara/watcher/detect/DetectorRegistry.h"
#include "tanara/watcher/detect/IMeetingDetector.h"
#include "tanara/watcher/platform/linux/LinuxCaptureDetector.h"

namespace tanara::watcher {

namespace {

DetectorDescriptor linuxCaptureDescriptor()
{
    DetectorDescriptor d;
    d.id             = QStringLiteral("linux-capture");
    d.displayName    = QStringLiteral("Linux — mikrofon-capture (PipeWire)");
    d.platform       = QStringLiteral("linux");
    d.derivesAppName = true;
    return d;
}

} // namespace

void registerBuiltinDetectors(MeetingDetectorRegistry& registry)
{
    registry.registerDetector(
        linuxCaptureDescriptor(),
        []() -> IMeetingDetector* { return new LinuxCaptureDetector(); });
}

} // namespace tanara::watcher

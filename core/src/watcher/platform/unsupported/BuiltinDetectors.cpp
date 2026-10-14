// No built-in meeting detector yet on this platform. macOS: a Core Audio based MacCaptureDetector
// (kAudioProcessPropertyIsRunningInput, 14.2+) is planned, see docs/macos.md.
#include "tanara/watcher/detect/DetectorRegistry.h"

namespace tanara::watcher {

void registerBuiltinDetectors(MeetingDetectorRegistry& registry)
{
    Q_UNUSED(registry);
}

} // namespace tanara::watcher

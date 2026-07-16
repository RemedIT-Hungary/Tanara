#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/IMeetingDetector.h"

// A beépített, platform-specifikus detektorok konkrét fejlécei KIZÁRÓLAG itt jelennek
// meg (a saját platform-#if-jük alatt) — a registry maga platform-agnosztikus marad.
#if defined(Q_OS_LINUX)
#include "tanara/detect/LinuxCaptureDetector.h"
#endif

namespace tanara {

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

} // namespace

void registerBuiltinDetectors()
{
    // Idempotens: a többszöri hívás (figyelő + felvevő --record mód) ne duplikáljon.
    static bool registered = false;
    if (registered)
        return;
    registered = true;

#if defined(Q_OS_LINUX)
    MeetingDetectorRegistry::instance().registerDetector(
        linuxCaptureDescriptor(),
        []() -> IMeetingDetector* { return new LinuxCaptureDetector(); });
#endif
    // Windows (WASAPI) és macOS (CoreAudio) detektorok: Fázis 5.
}

} // namespace tanara

// IPowerInhibitor on macOS: an IOKit power assertion that prevents idle system sleep while held
// (the display may still sleep). The assertion dies with the process. Compiled only in the macos
// platform folder (core/CMakeLists.txt); UNTESTED on hardware so far.
#include "tanara/foundation/platform/PowerInhibitor.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

namespace tanara::platform {

namespace {

class MacPowerInhibitor final : public IPowerInhibitor {
public:
    ~MacPowerInhibitor() override { release(); }

    bool acquire(const QString& reason) override
    {
        if (m_held) return true;
        CFStringRef name = reason.toCFString();
        m_held = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
                                             name, &m_id) == kIOReturnSuccess;
        CFRelease(name);
        return m_held;
    }
    void release() override
    {
        if (!m_held) return;
        IOPMAssertionRelease(m_id);
        m_held = false;
    }
    bool held() const override { return m_held; }
    QString backendName() const override { return QStringLiteral("macos-iopm"); }

private:
    IOPMAssertionID m_id = 0;
    bool m_held = false;
};

} // namespace

std::unique_ptr<IPowerInhibitor> makePowerInhibitor()
{
    return std::make_unique<MacPowerInhibitor>();
}

} // namespace tanara::platform

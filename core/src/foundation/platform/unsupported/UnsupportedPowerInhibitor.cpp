// IPowerInhibitor where there is no dedicated platform folder. On macOS (which compiles this folder
// until docs/macos.md is implemented) an IOKit power assertion; UNTESTED, there is no Mac to try it
// on. Elsewhere a no-op: held() stays false, backendName() is "none".
#include "tanara/foundation/platform/PowerInhibitor.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#endif

namespace tanara::platform {

namespace {

#if defined(__APPLE__)

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

using PlatformPowerInhibitor = MacPowerInhibitor;

#else

class NoPowerInhibitor final : public IPowerInhibitor {
public:
    bool acquire(const QString&) override { return false; }
    void release() override {}
    bool held() const override { return false; }
    QString backendName() const override { return QStringLiteral("none"); }
};

using PlatformPowerInhibitor = NoPowerInhibitor;

#endif

} // namespace

std::unique_ptr<IPowerInhibitor> makePowerInhibitor()
{
    return std::make_unique<PlatformPowerInhibitor>();
}

} // namespace tanara::platform

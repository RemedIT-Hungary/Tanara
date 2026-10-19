// IPowerInhibitor on Windows: SetThreadExecutionState. ES_SYSTEM_REQUIRED keeps the system out of
// idle sleep; ES_DISPLAY_REQUIRED is deliberately left out (the display may turn off). The state
// belongs to the calling thread, so acquire() and release() must run on the same thread.
#include "tanara/foundation/platform/PowerInhibitor.h"

#include <windows.h>

namespace tanara::platform {

namespace {

class WindowsPowerInhibitor final : public IPowerInhibitor {
public:
    ~WindowsPowerInhibitor() override { release(); }

    bool acquire(const QString&) override
    {
        if (m_held) return true;
        m_held = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) != 0;
        return m_held;
    }
    void release() override
    {
        if (!m_held) return;
        SetThreadExecutionState(ES_CONTINUOUS);
        m_held = false;
    }
    bool held() const override { return m_held; }
    QString backendName() const override { return QStringLiteral("windows"); }

private:
    bool m_held = false;
};

} // namespace

std::unique_ptr<IPowerInhibitor> makePowerInhibitor()
{
    return std::make_unique<WindowsPowerInhibitor>();
}

} // namespace tanara::platform

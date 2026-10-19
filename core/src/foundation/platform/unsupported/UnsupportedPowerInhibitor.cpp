// IPowerInhibitor where there is no dedicated platform folder: a no-op. held() stays false and
// backendName() is "none", so the recorder shows its "the machine may fall asleep" warning.
// (macOS has its own implementation in platform/macos/MacPowerInhibitor.cpp.)
#include "tanara/foundation/platform/PowerInhibitor.h"

namespace tanara::platform {

namespace {

class NoPowerInhibitor final : public IPowerInhibitor {
public:
    bool acquire(const QString&) override { return false; }
    void release() override {}
    bool held() const override { return false; }
    QString backendName() const override { return QStringLiteral("none"); }
};

} // namespace

std::unique_ptr<IPowerInhibitor> makePowerInhibitor()
{
    return std::make_unique<NoPowerInhibitor>();
}

} // namespace tanara::platform

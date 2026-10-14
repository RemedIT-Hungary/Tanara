// The watcher ports on a platform without its own folder yet (macOS until docs/macos.md is
// implemented): no autostart, no playback routing, no process list. The meeting detectors are in
// BuiltinDetectors.cpp next to this file.
#include "tanara/watcher/PlaybackRouting.h"
#include "tanara/watcher/RunningApps.h"
#include "tanara/watcher/detect/Autostart.h"

namespace tanara::watcher {

namespace {

class UnsupportedAutostart final : public IAutostart {
public:
    QString entryPath() const override { return QString(); }
    bool isEnabled() const override { return false; }
    bool enable(const QString&) override { return false; }
    bool disable() override { return false; }
};

} // namespace

std::unique_ptr<IAutostart> createPlatformAutostart()
{
    return std::make_unique<UnsupportedAutostart>();
}

std::unique_ptr<IPlaybackRouting> createPlatformPlaybackRouting()
{
    return createUnsupportedPlaybackRouting();
}

bool RunningApps::supported()
{
    return false;
}

QVector<RunningProcess> RunningApps::processes()
{
    return {};
}

} // namespace tanara::watcher

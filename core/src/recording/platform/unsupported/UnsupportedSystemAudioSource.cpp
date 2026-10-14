// ISystemAudioSource where there is no system-audio mechanism yet (macOS: Core Audio process tap or
// ScreenCaptureKit, see docs/macos.md). Capture devices are classified by name only (e.g. a
// user-installed virtual "loopback" device still counts as system audio).
#include "tanara/recording/audio/SystemAudioSource.h"

#include "tanara/domain/AudioDeviceNames.h"

namespace tanara::recording {

namespace {

class UnsupportedSystemAudioSource final : public ISystemAudioSource {
public:
    QString id() const override { return QStringLiteral("none"); }
    TrackKind classifyCapture(const QString& deviceName) const override
    {
        return audionames::classifyCaptureName(deviceName);
    }
    QVector<AudioDeviceInfo> loopbackDevices(const QVector<PlaybackEndpoint>&) const override { return {}; }
    bool opensFromPlayback(TrackKind) const override { return false; }
};

} // namespace

std::unique_ptr<ISystemAudioSource> createSystemAudioSource()
{
    return std::make_unique<UnsupportedSystemAudioSource>();
}

} // namespace tanara::recording

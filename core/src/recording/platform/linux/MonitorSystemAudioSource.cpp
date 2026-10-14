// ISystemAudioSource on Linux: PulseAudio / PipeWire monitor sources.
#include "tanara/recording/audio/SystemAudioSource.h"

#include "tanara/domain/AudioDeviceNames.h"

namespace tanara::recording {

namespace {

class MonitorSystemAudioSource final : public ISystemAudioSource {
public:
    QString id() const override { return QStringLiteral("pulse-monitor"); }
    TrackKind classifyCapture(const QString& deviceName) const override
    {
        // The outputs' "Monitor of ..." sources show up among the capture devices: Loopback by name.
        return audionames::classifyCaptureName(deviceName);
    }
    QVector<AudioDeviceInfo> loopbackDevices(const QVector<PlaybackEndpoint>&) const override
    {
        return {};   // already among the capture devices
    }
    bool opensFromPlayback(TrackKind) const override { return false; }
};

} // namespace

std::unique_ptr<ISystemAudioSource> createSystemAudioSource()
{
    return std::make_unique<MonitorSystemAudioSource>();
}

} // namespace tanara::recording

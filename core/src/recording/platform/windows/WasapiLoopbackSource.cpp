// ISystemAudioSource on Windows: WASAPI loopback of the playback (output) devices.
#include "tanara/recording/audio/SystemAudioSource.h"

#include "tanara/domain/AudioDeviceNames.h"

namespace tanara::recording {

namespace {

class WasapiLoopbackSource final : public ISystemAudioSource {
public:
    QString id() const override { return QStringLiteral("wasapi-loopback"); }
    TrackKind classifyCapture(const QString& deviceName) const override
    {
        // Real captures (microphones, line-in); the same name rules as everywhere.
        return audionames::classifyCaptureName(deviceName);
    }
    QVector<AudioDeviceInfo> loopbackDevices(const QVector<PlaybackEndpoint>& playback) const override
    {
        // Windows (WASAPI): system audio ("loopback") does NOT appear as a capture device (unlike the
        // PulseAudio/PipeWire monitor sources). We take the output (playback) devices as Loopback; AudioEngine
        // opens them with ma_device_type_loopback (it records the playback device's sound). That way the remote
        // participants' audio gets a track too.
        QVector<AudioDeviceInfo> result;
        for (int i = 0; i < playback.size(); ++i) {
            const PlaybackEndpoint& info = playback.at(i);

            AudioDeviceInfo dev;
            dev.name = info.name;
            // The "loopback:" prefix is human-readable + does not collide with capture ids.
            // (AudioEngine matches by name from the playback list; the id is only a key.)
            dev.id = QStringLiteral("loopback:") + QString::number(i) +
                     QStringLiteral(":") + dev.name;
            dev.isDefault = info.isDefault;
            dev.kind = TrackKind::Loopback;

            dev.sampleRate = 48000;
            dev.channels = 2;   // system audio is typically stereo
            if (info.nativeChannels > 0)
                dev.channels = info.nativeChannels;

            result.push_back(dev);
        }
        return result;
    }
    bool opensFromPlayback(TrackKind kind) const override { return kind == TrackKind::Loopback; }
    // WASAPI loopback delivers no packet while nothing plays (see tracktiming::fillsCaptureGaps).
    bool deliversNothingWhenIdle(TrackKind kind) const override { return kind == TrackKind::Loopback; }
};

} // namespace

std::unique_ptr<ISystemAudioSource> createSystemAudioSource()
{
    return std::make_unique<WasapiLoopbackSource>();
}

} // namespace tanara::recording

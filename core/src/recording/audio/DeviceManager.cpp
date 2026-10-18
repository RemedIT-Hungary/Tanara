#include "tanara/recording/audio/DeviceManager.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/recording/audio/SystemAudioSource.h"
#include <QElapsedTimer>

#include "miniaudio.h"

#include <QString>

namespace tanara::recording {

struct DeviceManagerPrivate {
    QVector<AudioDeviceInfo> capture;
    std::optional<QVector<AudioDeviceInfo>> override;   // test seam
};

DeviceManager::DeviceManager(QObject* parent)
    : QObject(parent), d_(std::make_unique<DeviceManagerPrivate>()) {
    refresh();
}

DeviceManager::~DeviceManager() = default;

QVector<AudioDeviceInfo> DeviceManager::captureDevices() const {
    return d_->capture;
}

QVector<AudioDeviceInfo> DeviceManager::autoRecordDevices() const {
    // The auto-record ("every device") set: the real microphones + loopbacks, but WITHOUT line-in/AUX
    // (Other) inputs (those can be checked by hand).
    QVector<AudioDeviceInfo> out;
    for (const auto& d : d_->capture)
        if (d.kind != TrackKind::Other)
            out.push_back(d);
    return out;
}

void DeviceManager::setDeviceListOverride(std::optional<QVector<AudioDeviceInfo>> devices) {
    d_->override = std::move(devices);
}

void DeviceManager::refresh() {
    QElapsedTimer perfTimer;
    perfTimer.start();
    if (d_->override) {
        d_->capture = *d_->override;
        emit devicesChanged();
        return;
    }
    QVector<AudioDeviceInfo> result;
    // Before the enumeration: on macOS the first use creates the process-tap input device.
    const ISystemAudioSource& sysAudio = systemAudioSource();

    ma_context context;
    // Default backend list (the platform's best). If the context init fails (no audio system), we
    // return an empty list and do not crash.
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        d_->capture = result;
        emit devicesChanged();
        return;
    }

    ma_device_info* playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* captureInfos = nullptr;
    ma_uint32 captureCount = 0;

    if (ma_context_get_devices(&context, &playbackInfos, &playbackCount,
                               &captureInfos, &captureCount) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < captureCount; ++i) {
            const ma_device_info& info = captureInfos[i];

            AudioDeviceInfo dev;
            dev.name = QString::fromUtf8(info.name);
            // We store the device identifier as a stable key not meant for humans: the index + name is enough
            // to reopen (AudioEngine looks up the actual ma_device_id itself from the enumeration).
            dev.id = QString::number(i) + QStringLiteral(":") + dev.name;
            dev.isDefault = info.isDefault ? true : false;
            // Mic / Loopback (e.g. a Linux "Monitor of ..." source) / Other: the platform's adapter decides.
            dev.kind = sysAudio.classifyCapture(dev.name);

            // At enumeration nativeDataFormats is not guaranteed to be filled; we work with a fixed 48 kHz and
            // (if known) the first native channel count, otherwise mono. The recording will be s16 / 48 kHz anyway.
            dev.sampleRate = 48000;
            dev.channels = 1;
            if (info.nativeDataFormatCount > 0 &&
                info.nativeDataFormats[0].channels > 0) {
                dev.channels = static_cast<int>(info.nativeDataFormats[0].channels);
            }

            result.push_back(dev);
        }

        // System audio derived from the outputs (Windows: WASAPI loopback; elsewhere none, the
        // monitors are already among the captures).
        QVector<PlaybackEndpoint> playback;
        playback.reserve(int(playbackCount));
        for (ma_uint32 i = 0; i < playbackCount; ++i) {
            const ma_device_info& info = playbackInfos[i];
            PlaybackEndpoint ep;
            ep.name = QString::fromUtf8(info.name);
            ep.isDefault = info.isDefault ? true : false;
            if (info.nativeDataFormatCount > 0 && info.nativeDataFormats[0].channels > 0)
                ep.nativeChannels = static_cast<int>(info.nativeDataFormats[0].channels);
            playback.push_back(ep);
        }
        result += sysAudio.loopbackDevices(playback);
    }

    ma_context_uninit(&context);

    d_->capture = result;
    qCDebug(lcPerf).noquote() << QStringLiteral("DeviceManager::refresh: %1 eszköz, %2 ms (ma_context, szinkron)")
                                 .arg(captureDevices().size()).arg(perfTimer.elapsed());
    emit devicesChanged();
}

} // namespace tanara::recording
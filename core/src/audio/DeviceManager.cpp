#include "tanara/audio/DeviceManager.h"
#include "tanara/Logging.h"
#include <QElapsedTimer>

#include "miniaudio.h"

#include <QString>

namespace tanara {

struct DeviceManagerPrivate {
    QVector<AudioDeviceInfo> capture;
    std::optional<QVector<AudioDeviceInfo>> override;   // test seam
};

namespace {

// Heuristic: on Linux (PulseAudio/PipeWire) the outputs' "monitor" sources also show up as capture
// devices. We classify these as Loopback, line-in/AUX-like inputs as Other (a real capture, but NOT a
// microphone, so auto-record skips it by default; see autoRecordDevices()), the rest as Mic.
TrackKind classifyByName(const QString& nameLower) {
    if (nameLower.contains(QStringLiteral("monitor")) ||
        nameLower.contains(QStringLiteral("loopback"))) {
        return TrackKind::Loopback;
    }
    // Line-in / AUX: typically nothing is plugged in (it would record silence), so it is not a
    // microphone. It can still be checked by hand in the recorder's "Other" group.
    if (nameLower.contains(QStringLiteral("line-in")) ||
        nameLower.contains(QStringLiteral("line in")) ||
        nameLower.contains(QStringLiteral("vonalbemenet")) ||
        nameLower.contains(QStringLiteral("aux"))) {
        return TrackKind::Other;
    }
    return TrackKind::Mic;
}

} // namespace

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
            dev.kind = classifyByName(dev.name.toLower());

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

#if defined(_WIN32)
        // Windows (WASAPI): system audio ("loopback") does NOT appear as a capture device (unlike the
        // PulseAudio/PipeWire monitor sources). We take the output (playback) devices as Loopback; AudioEngine
        // opens them with ma_device_type_loopback (it records the playback device's sound). That way the remote
        // participants' audio gets a track too.
        for (ma_uint32 i = 0; i < playbackCount; ++i) {
            const ma_device_info& info = playbackInfos[i];

            AudioDeviceInfo dev;
            dev.name = QString::fromUtf8(info.name);
            // The "loopback:" prefix is human-readable + does not collide with capture ids.
            // (AudioEngine matches by name from the playback list; the id is only a key.)
            dev.id = QStringLiteral("loopback:") + QString::number(i) +
                     QStringLiteral(":") + dev.name;
            dev.isDefault = info.isDefault ? true : false;
            dev.kind = TrackKind::Loopback;

            dev.sampleRate = 48000;
            dev.channels = 2;   // system audio is typically stereo
            if (info.nativeDataFormatCount > 0 &&
                info.nativeDataFormats[0].channels > 0) {
                dev.channels = static_cast<int>(info.nativeDataFormats[0].channels);
            }

            result.push_back(dev);
        }
#endif
    }

    ma_context_uninit(&context);

    d_->capture = result;
    qCDebug(lcPerf).noquote() << QStringLiteral("DeviceManager::refresh: %1 eszköz, %2 ms (ma_context, szinkron)")
                                 .arg(captureDevices().size()).arg(perfTimer.elapsed());
    emit devicesChanged();
}

} // namespace tanara

#include "tanara/audio/DeviceManager.h"

#include "miniaudio.h"

#include <QString>

namespace tanara {

struct DeviceManagerPrivate {
    QVector<AudioDeviceInfo> capture;
};

namespace {

// Heurisztika: Linuxon (PulseAudio/PipeWire) a kimenetek "monitor" forrásai is
// capture eszközként jelennek meg. Ezeket Loopback-nek soroljuk, a többit Mic-nek.
TrackKind classifyByName(const QString& nameLower) {
    if (nameLower.contains(QStringLiteral("monitor")) ||
        nameLower.contains(QStringLiteral("loopback"))) {
        return TrackKind::Loopback;
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

void DeviceManager::refresh() {
    QVector<AudioDeviceInfo> result;

    ma_context context;
    // Alapértelmezett backend-lista (a platform legjobbja). Ha a context-init
    // elhasal (nincs hangrendszer), üres listát adunk és nem crashelünk.
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
            // Az eszköz-azonosítót stabil, ember által nem értelmezett kulcsként
            // tároljuk: a sorszám + név elég az újra-megnyitáshoz (a tényleges
            // ma_device_id-t az AudioEngine az enumeráció alapján maga keresi ki).
            dev.id = QString::number(i) + QStringLiteral(":") + dev.name;
            dev.isDefault = info.isDefault ? true : false;
            dev.kind = classifyByName(dev.name.toLower());

            // Az enumerációkor a nativeDataFormats nincs garantáltan kitöltve;
            // fix 48 kHz-cel és (ha ismert) az első natív csatornaszámmal dolgozunk,
            // különben mono. A felvétel mindenképp s16 / 48 kHz lesz.
            dev.sampleRate = 48000;
            dev.channels = 1;
            if (info.nativeDataFormatCount > 0 &&
                info.nativeDataFormats[0].channels > 0) {
                dev.channels = static_cast<int>(info.nativeDataFormats[0].channels);
            }

            result.push_back(dev);
        }
    }

    ma_context_uninit(&context);

    d_->capture = result;
    emit devicesChanged();
}

} // namespace tanara

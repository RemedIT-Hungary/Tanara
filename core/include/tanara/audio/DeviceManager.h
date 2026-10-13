#pragma once
//
// Enumeration of capture (microphone + monitor/loopback) devices with miniaudio.
#include "tanara/Types.h"

#include <QObject>
#include <QVector>

#include <memory>
#include <optional>

namespace tanara {

// We hide miniaudio's details (ma_context) from the header so the header can also be compiled
// into Qt-only translation units without a miniaudio include.
struct DeviceManagerPrivate;

class DeviceManager : public QObject {
    Q_OBJECT
public:
    explicit DeviceManager(QObject* parent = nullptr);
    ~DeviceManager() override;

    // Result of the latest refresh(). An empty list if no backend is available.
    QVector<AudioDeviceInfo> captureDevices() const;

    // The "auto-record every device" set: captureDevices() WITHOUT the line-in/AUX (TrackKind::Other)
    // inputs. Those must be checked by hand in the recorder.
    QVector<AudioDeviceInfo> autoRecordDevices() const;

    // Test seam: non-empty = refresh() returns this list instead of the real enumeration (to simulate
    // plugging / unplugging a device). std::nullopt = the real enumeration again.
    void setDeviceListOverride(std::optional<QVector<AudioDeviceInfo>> devices);

public slots:
    // Re-enumerates the devices. Robust: if the backend is unavailable, it sets an empty list and does not throw.
    void refresh();

signals:
    void devicesChanged();

private:
    std::unique_ptr<DeviceManagerPrivate> d_;
};

} // namespace tanara

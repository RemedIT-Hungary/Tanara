#pragma once
//
// Tanara — capture (mikrofon + monitor/loopback) eszközök felsorolása miniaudióval.
//
#include "tanara/Types.h"

#include <QObject>
#include <QVector>

#include <memory>

namespace tanara {

// A miniaudio részleteit (ma_context) elrejtjük a fejlécből, hogy a fejléc
// Qt-only fordítási egységekbe is befordulhasson miniaudio-include nélkül.
struct DeviceManagerPrivate;

class DeviceManager : public QObject {
    Q_OBJECT
public:
    explicit DeviceManager(QObject* parent = nullptr);
    ~DeviceManager() override;

    // A legutóbbi refresh() eredménye. Üres lista, ha nincs elérhető backend.
    QVector<AudioDeviceInfo> captureDevices() const;

public slots:
    // Újra felsorolja az eszközöket. Robusztus: ha a backend nem elérhető,
    // üres listát állít be és nem dob.
    void refresh();

signals:
    void devicesChanged();

private:
    std::unique_ptr<DeviceManagerPrivate> d_;
};

} // namespace tanara

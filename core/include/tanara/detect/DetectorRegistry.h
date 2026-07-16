#pragma once
//
// Meeting-detektor registry. A figyelő ezen keresztül old fel és hoz létre
// detektort id alapján — nincs hardkódolt `new LinuxCaptureDetector`. Új platform:
// 1 descriptor + 1 factory + 1 registerDetector() hívás (a platform #if alatt).
// A ProviderRegistry mintáját tükrözi. Headless (core); NEM linkel Qt Widgetset.
//
// A detektorok NEM QObject-ek (szinkron poll) → a factory parent nélküli, és a
// create()/createBest() a hívó tulajdonába adja a példányt (unique_ptr ajánlott).
//
#include "tanara/detect/DetectorDescriptor.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

namespace tanara {

class IMeetingDetector;

class MeetingDetectorRegistry {
public:
    using Factory = std::function<IMeetingDetector*()>;

    static MeetingDetectorRegistry& instance();

    void registerDetector(const DetectorDescriptor& desc, Factory factory);
    QVector<DetectorDescriptor> all() const;
    bool has(const QString& id) const;
    DetectorDescriptor descriptor(const QString& id) const;   // üres descriptor, ha ismeretlen
    IMeetingDetector* create(const QString& id) const;        // nullptr, ha ismeretlen; a hívó törli

    // Az első ELÉRHETŐ (isAvailable()) detektor létrehozása — a figyelő ezt hívja,
    // ha a settings nem rögzít konkrét detectorId-t. nullptr, ha egy sem elérhető.
    IMeetingDetector* createBest() const;

private:
    QHash<QString, QPair<DetectorDescriptor, Factory>> m_map;
};

// A beépített (platform-specifikus) detektorok egyszeri, EXPLICIT regisztrációja.
// NEM static-init-időben. Idempotens: többszöri hívás biztonságos. A figyelő és a
// felvevő --record mód hívja; az AppController NEM (a frugális határ megőrzése).
void registerBuiltinDetectors();

} // namespace tanara

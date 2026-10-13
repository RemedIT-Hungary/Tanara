#pragma once
//
// Meeting-detector registry. The watcher resolves and creates a detector by id through it; there is no
// hard-coded `new LinuxCaptureDetector`. A new platform: 1 descriptor + 1 factory + 1 registerDetector()
// call (under the platform #if). It mirrors the ProviderRegistry pattern. Headless (core); does NOT link Qt Widgets.
//
// Detectors are NOT QObjects (synchronous poll), so the factory has no parent and create()/createBest()
// hand ownership of the instance to the caller (a unique_ptr is recommended).
#include "tanara/watcher/detect/DetectorDescriptor.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

namespace tanara::watcher {

class IMeetingDetector;

class MeetingDetectorRegistry {
public:
    using Factory = std::function<IMeetingDetector*()>;

    static MeetingDetectorRegistry& instance();

    void registerDetector(const DetectorDescriptor& desc, Factory factory);
    QVector<DetectorDescriptor> all() const;
    bool has(const QString& id) const;
    DetectorDescriptor descriptor(const QString& id) const;   // an empty descriptor if unknown
    IMeetingDetector* create(const QString& id) const;        // nullptr if unknown; the caller deletes

    // Creates the first AVAILABLE (isAvailable()) detector; the watcher calls this if the settings do not
    // pin a concrete detectorId. nullptr if none is available.
    IMeetingDetector* createBest() const;

private:
    QHash<QString, QPair<DetectorDescriptor, Factory>> m_map;
};

// One-time, EXPLICIT registration of the built-in (platform-specific) detectors. NOT at static-init
// time. Idempotent: calling it repeatedly is safe. The watcher and the recorder's --record mode call it;
// AppController does NOT (to preserve the lean boundary).
void registerBuiltinDetectors();

} // namespace tanara::watcher
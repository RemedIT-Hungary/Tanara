#pragma once
//
// Meeting-detector registry. The watcher resolves and creates a detector by id through it; there is no
// hard-coded `new LinuxCaptureDetector`. A new platform: 1 descriptor + 1 factory + 1 registerDetector()
// call in its core/src/watcher/platform/<os>/BuiltinDetectors.cpp. It mirrors the ProviderRegistry
// pattern. Headless (core); does NOT link Qt Widgets.
//
// An INSTANCE, not a process-wide singleton: app::Services owns the app's registry (the recorder, the
// Settings window's live check and the CLI use it), and the tanara-watcher executable builds its own.
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

    // An empty registry (tests, or a caller that registers its own detectors).
    MeetingDetectorRegistry() = default;
    // A registry with this platform's built-in detectors (registerBuiltinDetectors()).
    static MeetingDetectorRegistry withBuiltins();

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

// EXPLICIT registration of this platform's built-in detectors into `registry` (NOT at static-init
// time). Defined in core/src/watcher/platform/<os>/BuiltinDetectors.cpp, so the registry itself stays
// platform-agnostic. Idempotent: a repeated call re-registers the same ids and does not duplicate.
void registerBuiltinDetectors(MeetingDetectorRegistry& registry);

} // namespace tanara::watcher
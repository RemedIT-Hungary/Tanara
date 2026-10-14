#pragma once
//
// CallMonitor: call-end watching (e.g. during a recording), on the same detector core the
// tanara-watcher uses. It polls an IMeetingDetector at the configured interval and emits
// callEnded(appName) when a call that was seen active becomes inactive in 2 consecutive polls
// (a momentary microphone release does not count). It re-arms when a call becomes active again.
// It only SIGNALS; whether to stop the recording is the user's decision.
// UI-independent (QtCore).
#include "tanara/watcher/WatcherSettings.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace tanara::watcher {

class IMeetingDetector;

class CallMonitor : public QObject {
    Q_OBJECT
public:
    // Creates the detector by id (empty = the best available one). Null = none on this platform.
    using DetectorFactory = std::function<std::unique_ptr<IMeetingDetector>(const QString& detectorId)>;

    explicit CallMonitor(QObject* parent = nullptr);
    ~CallMonitor() override;

    // Test seam: empty = the built-in detectors (MeetingDetectorRegistry).
    void setDetectorFactory(DetectorFactory factory);
    // Polls only while this returns true (e.g. while the recording is in the Recording state).
    // Empty = always.
    void setPollGate(std::function<bool()> gate);

    // (Re)starts watching with the settings' detector, call-app list and interval, and polls once
    // at once. False (and not watching) if no detector is available.
    bool start(const WatcherSettings& settings);
    void stop();
    bool active() const;

    // One poll (the timer drives it while watching).
    void poll();

signals:
    void callEnded(QString appName);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara::watcher

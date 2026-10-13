#pragma once
//
// DeviceMonitor: live level monitoring on the NOT recorded devices: it opens the given capture
// devices and emits the current RMS and peak per device at ~30 Hz, so the UI can show with a VU bar
// which device has sound right now.
// Before recording it watches every device; during recording (if AppController asks) only the devices
// not on a track, since the recorded ones are owned and measured by the RecordingSession.
//
// Opening, measuring and closing the devices runs on its own background thread: start() / stop()
// return at once, levels come after the open. active() mirrors the request (true during startup too;
// false after a failed open), deviceNames() fills after the open. After stop() stale levels already
// sent do not get out.
#include "tanara/domain/Audio.h"
#include <QObject>
#include <QStringList>
#include <QVector>
#include <memory>

namespace tanara {

class DeviceMonitor : public QObject {
    Q_OBJECT
public:
    explicit DeviceMonitor(QObject* parent = nullptr);
    ~DeviceMonitor() override;

    bool active() const;
    // Names of the devices actually opened (those that did not open are left out; empty during startup).
    QStringList deviceNames() const;

public slots:
    void start(const QVector<AudioDeviceInfo>& devices);
    void stop();

signals:
    // deviceName is the stable key (the UI matches rows by it); rms 0..1.
    void level(const QString& deviceName, float rms);
    // The same with peak: peak = the largest sample since the previous signal (0..1).
    void levelPeak(const QString& deviceName, float rms, float peak);

public:
    // Internal: feedback from the background thread (runs on the main thread).
    bool d_isCurrent(quint64 gen) const;
    void d_started(quint64 gen, bool ok, const QStringList& names);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace tanara

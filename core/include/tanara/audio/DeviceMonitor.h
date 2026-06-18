#pragma once
//
// DeviceMonitor — felvétel ELŐTTI élő szintfigyelés: megnyitja a megadott
// capture-eszközöket, és ~20 Hz-cel kiadja eszközönként az aktuális RMS-t,
// hogy a UI VU-sávval mutathassa, melyik eszközön van épp hang.
// (Felvétel alatt NEM fut — akkor a RecordingSession birtokolja az eszközöket.)
//
#include "tanara/Types.h"
#include <QObject>
#include <QVector>
#include <memory>

namespace tanara {

class DeviceMonitor : public QObject {
    Q_OBJECT
public:
    explicit DeviceMonitor(QObject* parent = nullptr);
    ~DeviceMonitor() override;

    bool active() const;

public slots:
    void start(const QVector<AudioDeviceInfo>& devices);
    void stop();

signals:
    // deviceName a stabil kulcs (a UI eszerint párosítja a sorokat); rms 0..1.
    void level(const QString& deviceName, float rms);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace tanara

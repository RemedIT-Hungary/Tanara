#pragma once
//
// DeviceMonitor — élő szintfigyelés a NEM rögzített eszközökön: megnyitja a megadott
// capture-eszközöket, és ~30 Hz-cel kiadja eszközönként az aktuális RMS-t és csúcsot,
// hogy a UI VU-sávval mutathassa, melyik eszközön van épp hang.
// Felvétel előtt minden eszközt figyel; felvétel alatt (ha az AppController kéri) csak a
// sávra nem kerülő eszközöket — a rögzítetteket a RecordingSession birtokolja és méri.
//
#include "tanara/Types.h"
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
    // A ténylegesen megnyitott eszközök nevei (a meg nem nyíltak kimaradnak).
    QStringList deviceNames() const;

public slots:
    void start(const QVector<AudioDeviceInfo>& devices);
    void stop();

signals:
    // deviceName a stabil kulcs (a UI eszerint párosítja a sorokat); rms 0..1.
    void level(const QString& deviceName, float rms);
    // Ugyanez csúccsal: peak = az előző jel óta mért legnagyobb minta (0..1).
    void levelPeak(const QString& deviceName, float rms, float peak);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace tanara

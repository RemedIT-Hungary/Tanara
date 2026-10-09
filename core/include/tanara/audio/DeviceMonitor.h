#pragma once
//
// DeviceMonitor — élő szintfigyelés a NEM rögzített eszközökön: megnyitja a megadott
// capture-eszközöket, és ~30 Hz-cel kiadja eszközönként az aktuális RMS-t és csúcsot,
// hogy a UI VU-sávval mutathassa, melyik eszközön van épp hang.
// Felvétel előtt minden eszközt figyel; felvétel alatt (ha az AppController kéri) csak a
// sávra nem kerülő eszközöket — a rögzítetteket a RecordingSession birtokolja és méri.
//
// Az eszközök megnyitása, mérése és lezárása a saját háttérszálán fut: a start() / stop()
// azonnal visszatér, a szintek a megnyitás után jönnek. Az active() a kérést tükrözi (az
// indítás alatt is igaz; sikertelen megnyitás után hamis), a deviceNames() a megnyitás után
// telik meg. A stop() után a már elküldött, elavult szintek nem jutnak ki.
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
    // A ténylegesen megnyitott eszközök nevei (a meg nem nyíltak kimaradnak; indítás alatt üres).
    QStringList deviceNames() const;

public slots:
    void start(const QVector<AudioDeviceInfo>& devices);
    void stop();

signals:
    // deviceName a stabil kulcs (a UI eszerint párosítja a sorokat); rms 0..1.
    void level(const QString& deviceName, float rms);
    // Ugyanez csúccsal: peak = az előző jel óta mért legnagyobb minta (0..1).
    void levelPeak(const QString& deviceName, float rms, float peak);

public:
    // Belső: a háttérszál visszajelzései (a fő szálon futnak).
    bool d_isCurrent(quint64 gen) const;
    void d_started(quint64 gen, bool ok, const QStringList& names);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace tanara

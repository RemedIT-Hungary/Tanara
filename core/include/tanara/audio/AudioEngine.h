#pragma once
//
// Tanara — több-eszközös capture motor. Eszközönként egy ma_device, mindegyik
// valós idejű callbackje CSAK a saját körpufferébe másol + frissít egy atomi
// RMS/peak értéket. Semmi allokáció / lock / Qt a callbackben.
//
// A metódusok virtuálisak: a tesztek egy hamis motorral (valódi hangeszköz nélkül) hajtják
// meg a RecordingSession-t (lásd RecordingSession::setEngineFactory).
//
#include "tanara/Types.h"
#include "tanara/audio/RingBuffer.h"

#include <QVector>

#include <atomic>
#include <memory>

namespace tanara {

class AudioEngine {
public:
    AudioEngine();
    virtual ~AudioEngine();

    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Megnyit eszközönként egy capture ma_device-t (s16, 48000 Hz, eszközönkénti
    // csatornaszám). Igaz, ha legalább egy eszköz elindult. Részleges sikerre is
    // igazat ad: a be nem indult eszközöket kihagyja (count() ennek megfelelő).
    // Hiba/üres bemenet → false, és tisztán visszaáll (stop()).
    virtual bool start(const QVector<AudioDeviceInfo>& devices);

    // Leállít és felszabadít minden eszközt és puffert.
    virtual void stop();

    // Egy TOVÁBBI eszköz megnyitása a már futó motorban (felvétel közbeni sáv-hozzáadás).
    // Visszaadja az új sáv indexét, hibára -1-et. A meglévő indexek nem változnak. Csak a
    // motort birtokló szálról hívható (a callbackek és az olvasó szál közben futhatnak).
    virtual int addDevice(const AudioDeviceInfo& device);

    // Egy eszköz lezárása menet közben (pl. leválasztották): a capture leáll, de a slot és a
    // körpuffere megmarad (az index stabil, a maradék adat még kiolvasható).
    virtual void closeDevice(int trackIndex);
    virtual bool isOpen(int trackIndex) const;

    // Legfeljebb ennyi eszköz nyitható (a slot-tömb rögzített méretű, hogy az olvasó szál
    // zár nélkül, biztonságosan érhesse el, miközben új eszköz nyílik).
    static constexpr int kMaxDevices = 64;

    virtual int count() const;

    // A trackIndex-edik (elindult) eszköz körpuffere. Érvénytelen indexre egy
    // belső üres-puffer referenciát ad (sosem null), hogy a hívó ne crasheljen.
    virtual RingBuffer& buffer(int trackIndex);

    // Az adott sáv legutóbbi RMS-e (0..~1, s16-ot normalizálva). Érvénytelen
    // indexre 0.
    virtual float rms(int trackIndex) const;

    // Az adott sáv legutóbbi csúcsértéke (0..~1). Érvénytelen indexre 0.
    virtual float peak(int trackIndex) const;

    // A legutóbbi hívás óta mért legnagyobb csúcs (0..~1), majd nullázza — a szintmérő
    // csúcstartásához (a peak() csak az utolsó blokkot látja, a rövid tüskék kimaradnának).
    virtual float takePeak(int trackIndex);

    // Az adott elindult eszköz csatornaszáma (a callback ezzel másol).
    virtual int channels(int trackIndex) const;

    // Az adott elindult eszközhöz tartozó AudioDeviceInfo (a felvétel-szervezőnek).
    virtual AudioDeviceInfo deviceInfo(int trackIndex) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tanara

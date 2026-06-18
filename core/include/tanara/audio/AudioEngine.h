#pragma once
//
// Tanara — több-eszközös capture motor. Eszközönként egy ma_device, mindegyik
// valós idejű callbackje CSAK a saját körpufferébe másol + frissít egy atomi
// RMS/peak értéket. Semmi allokáció / lock / Qt a callbackben.
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
    ~AudioEngine();

    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Megnyit eszközönként egy capture ma_device-t (s16, 48000 Hz, eszközönkénti
    // csatornaszám). Igaz, ha legalább egy eszköz elindult. Részleges sikerre is
    // igazat ad: a be nem indult eszközöket kihagyja (count() ennek megfelelő).
    // Hiba/üres bemenet → false, és tisztán visszaáll (stop()).
    bool start(const QVector<AudioDeviceInfo>& devices);

    // Leállít és felszabadít minden eszközt és puffert.
    void stop();

    int count() const;

    // A trackIndex-edik (elindult) eszköz körpuffere. Érvénytelen indexre egy
    // belső üres-puffer referenciát ad (sosem null), hogy a hívó ne crasheljen.
    RingBuffer& buffer(int trackIndex);

    // Az adott sáv legutóbbi RMS-e (0..~1, s16-ot normalizálva). Érvénytelen
    // indexre 0.
    float rms(int trackIndex) const;

    // Az adott sáv legutóbbi csúcsértéke (0..~1). Érvénytelen indexre 0.
    float peak(int trackIndex) const;

    // Az adott elindult eszköz csatornaszáma (a callback ezzel másol).
    int channels(int trackIndex) const;

    // Az adott elindult eszközhöz tartozó AudioDeviceInfo (a felvétel-szervezőnek).
    AudioDeviceInfo deviceInfo(int trackIndex) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tanara

#pragma once
//
// Tanara — a sávok időzítése: megbeszélés-idő ↔ sávfájl-idő, és a capture-rések pótlása.
//
// Egy sáv fájlja a TÉNYLEGESEN felvett szakaszt tartalmazza: ha a sáv később indult (felvétel
// közben bekapcsolt eszköz, ki-be kapcsolt eszköz második szakasza), a fájl 0-pontja a
// megbeszélés `Track::startOffsetMs`-ánál van. Minden időtartomány, amit a program tárol
// (átirat-időbélyegek, lenyomat-minták „fájl#start-end” hivatkozása), MEGBESZÉLÉS-időben
// értendő; a sávfájlból olvasás előtt kell fájl-időre váltani (meetingToFileRange).
//
#include "tanara/Types.h"

#include <QtGlobal>

namespace tanara::tracktiming {

// Egy fájlon belüli időtartomány (ms). Érvénytelen (valid() == false), ha a kért megbeszélés-
// tartomány teljesen a sáv kezdete elé esik, vagy üres.
struct FileRange {
    qint64 startMs = -1;
    qint64 endMs = -1;
    bool valid() const { return startMs >= 0 && endMs > startMs; }
};

// Megbeszélés-idő [startMs, endMs) → a sávfájl ideje. A sáv kezdete előtti rész levágódik
// (a fájl 0-pontja előtt nincs hang); ha fileDurationMs >= 0, a fájl vége utáni rész is.
// Ha semmi nem marad, érvénytelen tartományt ad.
FileRange meetingToFileRange(qint64 offsetMs, qint64 startMs, qint64 endMs,
                             qint64 fileDurationMs = -1);

// Ugyanez egy sávra (a startOffsetMs-ével).
inline FileRange fileRange(const Track& track, qint64 startMs, qint64 endMs,
                           qint64 fileDurationMs = -1)
{
    return meetingToFileRange(track.startOffsetMs, startMs, endMs, fileDurationMs);
}

// ---- capture-rések pótlása --------------------------------------------------------------
//
// A Windows WASAPI loopback (rendszerhang) NEM ad csomagot, amíg semmi nem szól a kimeneten:
// a miniaudio ilyenkor egyszerűen tovább vár (lásd miniaudio.h, ma_device_read__wasapi:
// „Keep waiting in loopback mode”), a callback nem fut, a fájlba nem kerül semmi. A csendes
// szakaszok így kimaradnának, a sáv rövidebb lenne és a hang „előre csúszna”. A felvevő ezért
// az ilyen sávoknál a fali órához méri a kapott keretek számát, és a hiányt csenddel pótolja.

// Hány keretnyi csend hiányzik: a fali óra szerint várt (expected) és a ténylegesen kiírt
// (delivered) keretek különbsége, ha az meghaladja a tűréshatárt (tolerance — a normál
// pufferelési késés ne okozzon beszúrást); különben 0. Sosem negatív.
qint64 framesOwed(qint64 expectedFrames, qint64 deliveredFrames, qint64 toleranceFrames);

// A fali óra szerint eddig várt keretek száma (elapsedMs alatt, sampleRate mellett).
inline qint64 expectedFrames(qint64 elapsedMs, int sampleRate)
{
    return elapsedMs <= 0 ? 0 : elapsedMs * sampleRate / 1000;
}

// Mikor pótolunk: csak ha az eszköz legalább kGapIdleMs óta nem adott adatot (tényleg „néma”
// a loopback) — így beszéd közben sosem kerül csend a hangba, a fali óra és az eszköz órája
// közti lassú elcsúszás pedig a csendes szakaszokon igazodik ki. A pótlás után a fájl legfeljebb
// kGapToleranceMs-mal marad el a fali órától.
constexpr qint64 kGapIdleMs = 100;
constexpr qint64 kGapToleranceMs = 20;

// A most beszúrandó csend keretekben: 0, ha az eszköz kGapIdleMs-nál rövidebb ideje hallgat;
// különben framesOwed(a sáv megnyitása óta várt keretek, a kiírt keretek, kGapToleranceMs).
qint64 silenceToInsert(qint64 elapsedSinceOpenMs, qint64 idleMs, qint64 deliveredFrames,
                       int sampleRate);

// Kell-e az adott fajtájú sávnál a réseket pótolni ezen a platformon. Csak Windowson, csak
// loopback sávnál (a Linux PipeWire/PulseAudio monitor csendben is folyamatosan ad mintát,
// a mikrofon pedig mindig).
constexpr bool fillsCaptureGaps(TrackKind kind)
{
#if defined(_WIN32)
    return kind == TrackKind::Loopback;
#else
    (void)kind;
    return false;
#endif
}

} // namespace tanara::tracktiming

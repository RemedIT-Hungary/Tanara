#pragma once
//
// Sávonkénti beszéd-ellenőrzés (felvétel / import után): a sáv-aktivitásból (TrackActivity.h)
// a beszédszint-közeli keretek aránya (Track::speechRatio). Ha egy sávon gyakorlatilag nincs
// beszéd, kimarad a lekeverésből (Track::excludedReason = "noSpeech", active = false); a
// felhasználó a „Beemelem" művelettel visszaveheti. Tiszta függvények, I/O nélkül.
//
#include "tanara/Types.h"
#include "tanara/edit/TrackActivity.h"

#include <QStringList>

namespace tanara::trackspeech {

// Egy keret beszéd, ha a zajpadló felett legalább ennyi dB ÉS abszolút szintje (dBFS) is
// eléri a kSpeechMinAbsDb-t (a digitális csend kattanásai így nem számítanak beszédnek).
inline constexpr float kSpeechAboveFloorDb = 12.0f;
inline constexpr float kSpeechMinAbsDb = -55.0f;
// Ennél kisebb beszédarány → „nincs beszéd".
inline constexpr double kMinSpeechRatio = 0.02;

inline const QString kNoSpeech = QStringLiteral("noSpeech");
inline const QString kManual = QStringLiteral("manual");

// Beszéd-keret-e (NaN = nincs adat → nem).
bool isSpeechFrame(const TrackActivity& t, int frame);
// A beszéd-keretek aránya a lefedett keretek közt; nincs lefedett keret → -1.
double speechRatio(const TrackActivity& t);

// Van-e még nem mért (speechRatio < 0), bevont sáv.
bool needsSpeechCheck(const Meeting& m);

struct SpeechCheckResult {
    QStringList measured;   // a most mért sávok id-i
    QStringList excluded;   // a most „nincs beszéd" miatt kivett sávok id-i
    bool changed() const { return !measured.isEmpty(); }
};

// A még nem mért, bevont sávokra beírja a speechRatio-t (egy eszköz szakaszai a logikai sáv
// közös értékét kapják), és a küszöb alattiakat kiveszi ("noSpeech", active = false). Ha az
// összes bevont sáv kiesne, egyik sem esik ki (inkább maradjon meg minden). A már mért sávot
// (pl. kézzel visszavett) nem bántja.
SpeechCheckResult applySpeechRatios(Meeting& m, const MeetingActivity& activity);

} // namespace tanara::trackspeech

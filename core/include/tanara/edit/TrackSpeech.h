#pragma once
//
// Per-track speech check (after recording / import): from the track activity (TrackActivity.h),
// the share of frames near speech level (Track::speechRatio). If a track has practically no speech,
// it is left out of the mixdown (Track::excludedReason = "noSpeech", active = false); the user can take
// it back with the "Include" action. Pure functions, no I/O.
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Activity.h"

#include <QStringList>

namespace tanara::trackspeech {

// A frame is speech if it is at least this many dB above the noise floor AND its absolute level (dBFS)
// also reaches kSpeechMinAbsDb (so clicks of digital silence do not count as speech).
inline constexpr float kSpeechAboveFloorDb = 12.0f;
inline constexpr float kSpeechMinAbsDb = -55.0f;
// A speech ratio below this = "no speech".
inline constexpr double kMinSpeechRatio = 0.02;

inline const QString kNoSpeech = QStringLiteral("noSpeech");
inline const QString kManual = QStringLiteral("manual");

// Whether it is a speech frame (NaN = no data -> not).
bool isSpeechFrame(const TrackActivity& t, int frame);
// Share of speech frames among the covered frames; no covered frame -> -1.
double speechRatio(const TrackActivity& t);

// Whether there is still an unmeasured (speechRatio < 0), included track.
bool needsSpeechCheck(const Meeting& m);

struct SpeechCheckResult {
    QStringList measured;   // ids of the tracks measured now
    QStringList excluded;   // ids of the tracks excluded now for "no speech"
    bool changed() const { return !measured.isEmpty(); }
};

// Writes speechRatio for the unmeasured, included tracks (a device's spans get the logical track's
// shared value), and excludes those below the threshold ("noSpeech", active = false). If every
// included track would drop out, none does (better to keep everything). A track already measured
// (e.g. taken back manually) is not touched.
SpeechCheckResult applySpeechRatios(Meeting& m, const MeetingActivity& activity);

} // namespace tanara::trackspeech

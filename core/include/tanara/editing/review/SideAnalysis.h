#pragma once
//
// Track-side check: on which SIDE (local microphone / remote loopback) a transcript line was
// spoken, and whether that contradicts the line's speaker. Pure functions over data (track
// activity: TrackActivity.h; transcript and overlay: SpeakerOverlay.h); no I/O, unit-testable.
//
// The recording is one file per track: the mic track(s) carry the local, the loopback track(s)
// the remote participants' voices. If under a line only the microphone has signal, a local
// speaker said it; if only the loopback, a remote one. Soniox diarization and the voice models
// do not see this, while track energy separates it trivially.
//
// The side is SPEECH-LEVEL-normalized: per track, the speech level is the 90th percentile of the
// active (above kActiveDb) frames (= 0 dB); a line's level per track kind is the strongest
// track's value relative to it. The user's voice returns on the loopback as an echo (~8 dB below
// real remote speech); a plain power ratio saw that as remote. The decision (classifyLevels):
//   * mic at speech level (>= kSpeechDb)                    -> Local or Mixed, NEVER Remote;
//   * both at speech level: loopback >= kEchoDb below its own level -> Local (echo),
//     otherwise Mixed (talking over each other);
//   * loopback at speech level, mic below speech level (noise / bleed) -> Remote;
//   * neither (e.g. mic in between: speaker bleed)           -> Unknown.
// Unknown also if one track kind does not cover the line. A single-track meeting (mic only or
// loopback only, e.g. an import) -> everything Unknown, no conflict (the feature is silently inactive).
//
// The old (power-ratio) classification is kept for diagnostics (LineSide::legacySide):
// localShare = P_mic / (P_mic + P_loop): the linear power ABOVE the noise floor
// (10^(dB/10) - 1, from windowEnergyDb) summed per track kind. Side by thresholds:
// >= hiLocal -> Local, <= loRemote -> Remote, in between Mixed.
//
// A person's side (in order of precedence): manual track assignment (SpeakerOverlay::speakerTracks) >
// inferred from the confirmed lines > the default learned in people.json > own name = Local
// (or the speaker bound to a track). The inferred side overrides the default only with a clear
// majority (kOverrideMinLines / kOverrideMajority).
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Participant.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/processing/analysis/TrackActivity.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::editing {

enum class Side { Local, Remote, Mixed, Unknown };

QString sideName(Side s);   // "local" | "remote" | "mixed" | "unknown"
Side sideFromName(const QString& name);   // unknown -> Unknown

namespace sides {

// A side is "sounding" if the window's mean energy is this far above the noise floor (dB).
inline constexpr float kActiveDb = 3.0f;
// Base thresholds (for a unimodal distribution).
inline constexpr float kDefaultLoRemote = 0.3f;
inline constexpr float kDefaultHiLocal = 0.7f;
// Adaptive threshold: the 1D 2-means of the localShare values counts as bimodal if there are at
// least kAdaptiveMinLines lines, at least max(kAdaptiveMinCluster, 5%) lines in each group, the two
// centers are at least kAdaptiveMinGap apart, and 0.5 falls between them.
inline constexpr int kAdaptiveMinLines = 6;
inline constexpr int kAdaptiveMinCluster = 3;
inline constexpr double kAdaptiveMinClusterShare = 0.05;
inline constexpr float kAdaptiveMinGap = 0.35f;
// A person's side from confirmed lines: at least this many Local+Remote lines and this majority.
inline constexpr int kPersonMinLines = 2;
inline constexpr float kPersonMajority = 0.75f;
// The default (own name / track-bound speaker) is overridden only by clear counter-evidence.
inline constexpr int kOverrideMinLines = 3;
inline constexpr float kOverrideMajority = 0.8f;
// Confidence of a side that came from the default (without data).
inline constexpr float kDefaultConfidence = 0.5f;

// ---- speech-level-normalized classification ----
// The track's speech level: this percentile of the active (>= kActiveDb) frames.
inline constexpr double kSpeechPercentile = 0.90;
// "At speech level": the line's level is at most this far below the track's speech level (dB).
inline constexpr float kSpeechDb = -10.0f;
// "Low": deeper below the speech level than this (or below kActiveDb).
inline constexpr float kLowDb = -20.0f;
// Both at speech level: if the loopback is at least this far below its own level -> echo.
inline constexpr float kEchoDb = -5.0f;

struct Thresholds {
    float loRemote = kDefaultLoRemote;
    float hiLocal = kDefaultHiLocal;
    bool adaptive = false;          // computed from the bimodal distribution
    float centerRemote = qQNaN();   // the two centers of the 2-means (for adaptive)
    float centerLocal = qQNaN();
};

// The adaptive threshold. For a bimodal distribution (see above) the Mixed band is the MIDDLE
// HALF of the span between the two centers: m = (cR+cL)/2, w = (cL-cR)/4 -> loRemote = m-w,
// hiLocal = m+w. So with mic bleed (remote lines also show some mic energy, the remote peak
// sits at e.g. 0.35) the boundary moves between the two peaks. The 2-means is exact in 1D: the
// best split of the sorted sequence (min SSE). Otherwise the base 0.3 / 0.7.
Thresholds adaptiveThresholds(const QVector<float>& shares);

Side classifyShare(float localShare, const Thresholds& th);

// localShare from the two sides' dB above the noise floor (with several tracks the caller sums the power).
float localShareFromDb(float micDb, float loopDb);

// The track's speech level (dB above the floor): the kSpeechPercentile percentile of the active
// frames. NaN if there is no active frame.
float speechLevelDb(const TrackActivity& t);

// The decision from the two sides' values relative to speech level (dB, typically <= 0). -inf = the
// side is silent (below kActiveDb); NaN = no data (the track kind does not cover the line) -> Unknown.
Side classifyLevels(float micRelDb, float loopRelDb);

} // namespace sides

struct LineSide {
    QString utteranceId;
    qint64  startMs = 0;
    qint64  endMs = 0;
    QString speakerKey;     // the resolved speaker key
    QString rawLabel;       // the raw diarization label
    float   localShare = qQNaN();   // NaN = no data
    Side    side = Side::Unknown;   // the speech-level-normalized classification
    Side    legacySide = Side::Unknown;   // the old power-ratio classification (diagnostics)
    float   micDb = qQNaN();        // combined energy of the mic tracks above the noise floor (NaN = no coverage)
    float   loopDb = qQNaN();
    // Level of the track kind's strongest track relative to its own speech level (dB); -inf = silence,
    // NaN = no coverage.
    float   micRelDb = qQNaN();
    float   loopRelDb = qQNaN();
    bool    locked = false;         // confirmed / manually corrected
    bool    noisy = false;          // "talked over each other" (overlay or overlap rule)
    // "Unidentifiable" (OverlayUtterance::ignored): the line's side is classified (informational), but
    // it does not enter the person's side learning (allLines, confirmed counters) or the conflicts.
    bool    ignored = false;
};

struct SideCounts {
    int local = 0, remote = 0, mixed = 0, unknown = 0;
    void add(Side s);
    int total() const { return local + remote + mixed + unknown; }
};

struct PersonSide {
    QString speakerKey;
    QString personName;         // the bound person (empty = anonymous)
    QString displayName;
    Side    side = Side::Unknown;
    // Sides of the confirmed/corrected, non-noisy lines (the verdict comes from these).
    int localLines = 0, remoteLines = 0, mixedLines = 0;
    SideCounts allLines;        // all the speaker's lines (informational; without "unidentifiable" lines)
    float   confidence = 0.0f;  // 0..1: the majority's share, or kDefaultConfidence for a default
    Side    defaultSide = Side::Unknown;   // from the learned default / own name / track-bound speaker
    Side    learnedSide = Side::Unknown;   // the default learned in people.json (SideHints)
    QStringList manualTracks;   // the manual track assignment (empty = no constraint)
    Side    manualSide = Side::Unknown;    // side of the manual assignment's tracks
    // "manual" | "lines" | "learned" | "user-name" | "fixed-track" | "override" | "none"
    QString basis;
};

struct SideConflict {
    QString utteranceId;
    qint64  startMs = 0;
    QString speakerKey;
    Side    lineSide = Side::Unknown;
    Side    personSide = Side::Unknown;
    float   localShare = qQNaN();
};

struct SideReport {
    bool active = false;                 // there is mic AND loopback activity (otherwise everything is Unknown)
    sides::Thresholds thresholds;        // thresholds of the old classification (legacySide)
    QMap<QString, float> speechLevels;   // track id -> speech level (dB above the floor; NaN = none)
    SideCounts legacyTotals;             // totals of the old classification
    QVector<LineSide> lines;             // in input order
    QVector<PersonSide> persons;         // in order of first appearance
    QVector<SideConflict> conflicts;     // in time order
    QMap<QString, SideCounts> rawLabels; // per RAW diarization label
    SideCounts totals;
    QVector<int> histogram;              // localShare in 10 buckets (without NaNs)
};

// External knowledge about the people's sides.
struct SideHints {
    // Manual track assignment: speaker key -> track ids (SpeakerOverlay::speakerTracks). If empty,
    // the overlay's applies.
    QHash<QString, QStringList> speakerTracks;
    // The default learned in people.json: person name (toCaseFolded) -> side.
    QHash<QString, Side> learnedSides;
};

// The full analysis. userName: the user's own name (SettingsManager); the speaker bound to it and
// the speaker matching a Track.fixedSpeaker track's speakerLabel stand by default on that track's
// side (mic -> Local, loopback -> Remote). A learned default takes precedence over this; the
// default is overridden by clear contradiction from the confirmed lines (logged); the manual
// track assignment overrides everything.
SideReport analyzeSides(const Meeting& m, const QVector<TranscriptLine>& lines,
                        const SpeakerOverlay& ov, const MeetingActivity& activity,
                        const QString& userName, const SideHints& hints = {});

namespace sides {
// Side of the tracks per the meeting's tracks (mic -> Local, loopback -> Remote; mixed -> Mixed;
// unknown ids -> Unknown).
Side sideOfTracks(const Meeting& m, const QStringList& trackIds);
// What people.json can learn from an analysis: the named people whose side came from data
// (confirmed lines) or a manual assignment, and are Local / Remote.
// Key: the person's name (as in the speakerMap).
QHash<QString, Side> learnedDefaults(const SideReport& r);

// ---- track veto ----
// The manual track assignment EXCLUDES (the person cannot be a candidate for the line) when all hold:
//   1. every NAMED speaker of the meeting has a manual track assignment (anonymous ones do not
//      count); this is vetoActive;
//   2. the line's side is unambiguous (Local / Remote; Mixed and Unknown do not exclude);
//   3. none of the person's manual tracks is on the line's side (manualSide is opposite the
//      line's side; one assigned on several sides, Mixed, is never excluded).
// Every other side source is only a score deduction (CandidateRanker kSidePenalty).

// Condition 1 for the meeting's speakers: the SideReport's named people and the overlay's named
// participants without lines all have a manual assignment (its tracks have a known side), and
// there is at least one such. False for an inactive track analysis (single-track meeting).
bool allNamedSpeakersBound(const Meeting& m, const SideReport& r, const SpeakerOverlay& ov,
                           const SideHints& hints = {});
// Condition 1 for the approval's participants: the checked (acceptedIds), named participants all
// have manual tracks (Participant::tracks, tracks with a known side), and there is at least one such.
bool allNamedParticipantsBound(const Meeting& m, const QVector<Participant>& all, const QStringList& acceptedIds);
// Conditions 2 + 3 (together with vetoActive): whether the line's side excludes the person.
bool vetoes(bool vetoActive, Side personManualSide, Side lineSide);
// Sides of the lines (the speech-level-normalized classification) from track activity only; the
// same as the LineSide::side field of analyzeSides. With single-track activity everything is Unknown.
QVector<Side> classifyLineSides(const MeetingActivity& activity, const QVector<TranscriptLine>& lines);
} // namespace sides

} // namespace tanara::editing
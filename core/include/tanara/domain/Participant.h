#pragma once
//
// Domain: meeting participants ("Who was there?"), the evidence behind a suggestion, and the
// pre-transcript speaker guesses.
#include "tanara/domain/Person.h"

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace tanara {

// ---- evidence -----------------------------------------------------------------
// Every suggestion shows WHY (voice %, track side, tag, number of lines here, a similar voice...), and
// every reason tells WHERE it can be fixed (fixTarget). The candidate ranking (CandidateRanker.h), the
// "To review" groups (ReviewGroups.h) and the participant approval use it.
enum class EvidenceKind { Voice, Side, Tag, LineCount, Similarity, Calendar /* later */, Manual };
enum class Polarity { Support, Contradict, Neutral };

struct Evidence {
    EvidenceKind kind = EvidenceKind::Voice;
    Polarity polarity = Polarity::Neutral;
    double value = 0.0;
    QString text;       // transient display text (Hungarian source) of an Evidence made and shown within one run; empty when key is set
    QString detail;     // likewise (the tooltip)
    QString fixTarget;  // "tracks" | "samples" | "tags" | "pair:<key>" | ""
    // Language-neutral identity + parameters (EvidenceKeys.h). A persisted evidence has a key and no text:
    // evidenceText()/evidenceDetail() (EvidenceText.h) render it at display time in the active language.
    QString key;
    QVariantMap params;
};

struct Candidate {
    QString speakerKey;         // the meeting's speaker; empty = a known person outside the meeting
    PersonId personId;          // the candidate person (null = an anonymous speaker of the meeting)
    QString displayName;        // for display only
    double score = 0.0;
    QVector<Evidence> evidence;
    bool otherSide = false;     // the candidate speaks on the other track (ranked lower, not removed)
    // Excluded by the manual track assignment (SideAnalysis.h sides::vetoes): without a score (0), at the END
    // of the list; never a suggestion, but selectable by hand. Always otherSide too.
    bool vetoed = false;
    int linesHere = 0;
};

// ---- participants ("Who was there?") ------------------------------------------
// Where a participant came from: added by hand, voice analysis, tag suggestion, calendar (later).
enum class ParticipantSource { Manual, Voice, Tag, Calendar };

// A binding for the participant approval (SpeakerEditSession::applyBindings): the raw speaker (or
// only its given lines) goes to a person; null personId = set anonymous.
struct SpeakerBinding {
    QString     rawLabel;
    PersonId    personId;
    QStringList utteranceIds;   // empty = the whole raw speaker
};

// Groups of the approval dialog: Certain (voice + another source), Doubtful (voice only, or
// conflicting evidence), Invited but not heard (calendar, no voice).
enum class ParticipantGroup { Sure, Doubt, InvitedNotHeard };

struct Participant {
    QString id;                     // stable id within the meeting
    PersonId personId;              // null = anonymous (unknown voice)
    QStringList rawSpeakerIds;      // bound raw STT speakers ("Speaker 1")
    ParticipantSource source = ParticipantSource::Voice;
    bool approved = false;          // approved by the user ("Who was there?")
    QStringList sides;              // "mic" | "loopback": which side it was heard on
    // Manual speaker -> track binding (track ids, "Which track do they speak on?"); empty = the
    // inferred side (sides) applies. After transcription SpeakerOverlay::speakerTracks is its
    // counterpart (the core keeps them in sync).
    QStringList tracks;
    QVector<Evidence> evidence;
    double talkShare = 0.0;         // 0..1, share of speaking time
};

// A participant's / speaker's track binding for the UI: the manual tracks (manual), otherwise
// the tracks of the inferred side (leading ids of the mic / call tracks included in the mixdown).
struct ParticipantTrackBinding {
    QStringList trackIds;
    bool manual = false;
};

// State of a candidate at the moment of the decision.
struct ParticipantApprovalEntry {
    QString participantId;
    PersonId personId;
    ParticipantGroup group = ParticipantGroup::Doubt;
    bool checked = false;
    QStringList rawSpeakerIds;
};

struct ParticipantApproval {
    QString at;                     // ISO-8601
    QStringList modelIds;           // voice models of the analysis the decision was based on
    QVector<ParticipantApprovalEntry> candidates;
    bool skipped = false;           // "Skip": no binding, the markers stay
    bool solo = false;              // "Only I spoke": every raw speaker maps to the user's own name
};

// A speaker candidate recognized (locally) BEFORE transcription on a track: the voice cluster
// + the best DB match (or an empty name = unknown). The representative segment can be
// listened to / named (= training) before transcription.
struct ParticipantGuess {
    QString trackId;
    QString deviceName;
    PersonId personId;     // the DB match, or null if unknown (below threshold)
    QString displayName;   // the match's name at the time of the guess (display only)
    double  score = -1.0;  // cosine score of the match
    int     windows = 0;   // how many voice windows the cluster consisted of (reliability)
    QString sampleRef;     // "track_x.ogg#startMs-endMs": representative sample (in meeting time)
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::Evidence)
Q_DECLARE_METATYPE(tanara::Candidate)
Q_DECLARE_METATYPE(tanara::Participant)
Q_DECLARE_METATYPE(QVector<tanara::Participant>)

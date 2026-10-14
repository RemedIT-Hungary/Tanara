#pragma once
//
// Value types of the transcript editor (speaker correction): the backend <-> UI contract.
// Plain data structs (no QObject); they can also be sent in queued signals.
//
// Concepts:
//  - UTTERANCE: one line of transcript.segments.json. No cutting within a line.
//  - SPEAKER: a participant of the meeting, with a stable key. Either a raw diarization
//    label ("Speaker 1": the key is the label itself) or a manually added participant
//    ("participant:N"). Can be bound to a person (a PersonId) or anonymous.
#include "tanara/domain/Participant.h"
#include "tanara/domain/Summary.h"
#include "tanara/domain/Voice.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMetaType>

namespace tanara::editing {

// The RESOLVED state of an utterance (one row of the UI list model).
struct EditorUtterance {
    QString id;                 // stable id (until the transcript is regenerated)
    int     index = -1;         // index in transcript.segments.json (time order)
    qint64  startMs = 0;
    qint64  endMs = 0;
    QString text;
    QString speakerKey;         // key of the resolved speaker (EditorSpeaker::key)
    QString rawLabel;           // the raw diarization label (untouched)
    bool    uncertain = false;          // fits its speaker's voice poorly (hatched)
    bool    manuallyCorrected = false;  // reassigned by hand ("corrected")
    bool    confirmed = false;          // confirmed by the user ("Fine as is")
    // "Talked over each other": not used as a voice sample (centroid, voiceprint). Automatic
    // (overlap with another speaker) or manual; the manual override applies in both directions.
    bool    noisy = false;
    bool    noisyOverlap = false;       // noisy per the overlap rule (regardless of the manual override)
    bool    rechecked = false;          // marked uncertain by the recheck
    // "Unidentifiable" (SpeakerOverlay: OverlayUtterance::ignored): the name stays, but the voice
    // analysis ignores it; never uncertain, not a sample, not put in a "To review" group.
    bool    ignored = false;
    // Key of the speaker that fits the voice better (the recheck's suggestion); empty = none.
    QString likelySpeakerKey;
    // Why it is uncertain (the pill's label): "side" (the line was on a track opposite its
    // speaker's side), "tag" (doubtful by voice, and the speaker's tags do not fit the meeting),
    // "voice" (doubtful by voice); empty if not uncertain.
    QString uncertainReason;
};

// "Deviating sample" flag (SpeakerEditor::lastSampleDeviation): after a positive, sample-giving
// decision (a confirmation, or a move as a sample), the decided line farthest from the target
// speaker's core. deviates = the similarity is below the threshold (speakeredit::kSampleDeviationFit).
struct SampleDeviation {
    QString utteranceId;        // empty = not measured (no valid flag)
    QString speakerKey;         // the target speaker
    double  similarity = 0.0;   // cosine to the speaker's core (without the decided lines), around 0..1
    bool    deviates = false;
    bool isValid() const { return !utteranceId.isEmpty(); }
    bool operator==(const SampleDeviation& o) const {
        return utteranceId == o.utteranceId && speakerKey == o.speakerKey
            && similarity == o.similarity && deviates == o.deviates;
    }
    bool operator!=(const SampleDeviation& o) const { return !(*this == o); }
};

// Sample meter (SpeakerEditor::sampleStats): per speaker, the lines counting as voice samples. A sample
// line = locked (confirmed / corrected), not "talked over each other", not "unidentifiable", and at least
// speakeredit::kMinEmbedMs long (embeddable). The "session" runs since the editor was opened (or the
// last new transcript).
struct SpeakerSampleStats {
    QString speakerKey;
    PersonId personId;          // null = anonymous
    QString displayName;
    int     sampleLines = 0;
    qint64  sampleMs = 0;       // total length of the sample lines
    int     sessionAdded = 0;   // of these, became a sample of this speaker in this session
    QStringList sessionAddedIds;    // those lines (in time order), for the "+N now" filter
    bool operator==(const SpeakerSampleStats& o) const {
        return speakerKey == o.speakerKey && personId == o.personId && displayName == o.displayName
            && sampleLines == o.sampleLines && sampleMs == o.sampleMs && sessionAdded == o.sessionAdded
            && sessionAddedIds == o.sessionAddedIds;
    }
};

// SpeakerBinding: domain/Participant.h.

// A speaker of the meeting (the track/column in the editor).
struct EditorSpeaker {
    QString key;                // stable key: raw label OR "participant:N"
    QString displayName;        // person name, or an anonymous label ("Speaker 2", "New speaker 1")
    PersonId personId;          // the bound person; null = anonymous
    QString rawLabel;           // raw label (empty for a manually added participant)
    bool    anonymous = true;   // not bound to a person
    bool    added = false;      // manually added participant (does not come from diarization)
    bool    isSelf = false;     // the user's own person (people.json "self")
    int     colorIndex = 0;     // order of first appearance in the meeting; independent of editing
    int     utteranceCount = 0;
    qint64  talkTimeMs = 0;
    double  talkShare = 0.0;    // 0..1, share of the total speaking time
    bool    hasVoiceprint = false;      // the bound person has a voiceprint
    double  voiceConfidence = -1.0;     // cosine score of the voice identification; <0 = unknown
};

// Suggestion after a manual reassignment: lines left at the source speaker that are closer
// to the target by voice ("14 more lines sound like this voice. Move them?").
struct SpeakerSuggestion {
    QString     sourceSpeakerKey;
    QString     targetSpeaker;      // key of the target speaker
    QString     anchorUtteranceId;  // the manually reassigned line (the UI puts the box under it)
    QStringList utteranceIds;
    bool isValid() const { return !targetSpeaker.isEmpty() && !utteranceIds.isEmpty(); }
};

// After a manual reassignment, when the "similar lines" suggestion stays silent because the two
// voices are too similar (a "not two people" guard), but both speakers are named and each has at least
// 3 confirmed / corrected (embedded, clean) lines: the editor offers a review between the two
// ("A's and B's voices are similar. Review their lines...?").
struct PairRecheckOffer {
    QString sourceSpeakerKey;       // where the line(s) came from
    QString targetSpeakerKey;       // where they went
    double  centroidSimilarity = 0.0;   // cosine between the two voices (the 2-means of the "similar lines")
    bool isValid() const { return !sourceSpeakerKey.isEmpty() && !targetSpeakerKey.isEmpty(); }
};

// A known person for the person-picker panel.
struct PersonInfo {
    PersonId personId;
    QString name;                   // display name
    bool    hasVoiceprint = false;
    int     voiceprintCount = 0;
    int     meetingCount = 0;       // in how many meetings they appear
    QStringList aliases;            // nicknames (people.json); search matches these too
    QString matchedAlias;           // after filtering: if the match came from a nickname, that nickname
    QDateTime lastSeen;             // the latest meeting they appear in (tells same-named people apart)
};

// Composition of one speaker's voice reference in a recheck (for the feedback text; RecheckService).
struct SpeakerReference {
    QString speakerKey;
    QString name;               // displayed name
    int  lines = 0;             // confirmed / corrected lines (for a fallback: all its lines)
    int  localPrints = 0;       // voiceprints made in this meeting
    int  priorPrints = 0;       // earlier voiceprints (from other meetings)
    bool priorOnly = false;     // no local core: only earlier voiceprints
    bool fallback = false;      // neither core nor voiceprint: all of the speaker's lines
    bool priorCapped = false;   // the weight of earlier voiceprints was cut by the cap
    bool usesPrints() const { return localPrints + priorPrints > 0; }
};

// Two speakers with very similar voices (cosine of their confirmed cores >= ranking::kSimilarWarn);
// the same value shown by the "Why them?" "Warning:" line (ranking::similarityWarnings).
struct SimilarVoices {
    QString keyA, nameA;
    QString keyB, nameB;
    double  similarity = 0.0;
};

// The voiceprint material / result types live in domain/Voice.h (the People window's library service
// uses them through the VoiceprintMaterialSource port); the editing names stay as aliases.
using VoiceprintMaterial = tanara::VoiceprintMaterial;
using VoiceprintResult = tanara::VoiceprintResult;

// SummaryStaleInfo: domain/Summary.h.

// What is lost on re-transcription (for the confirmation dialog).
struct RetranscribeImpact {
    int correctedUtterances = 0;    // manually reassigned lines
    int confirmedUtterances = 0;    // "Fine as is" lines (that are not reassigned)
    int namedSpeakers = 0;          // named speakers (speakerMap + named participants)
    int addedParticipants = 0;      // manually added participants
    int manualCorrections() const { return correctedUtterances + confirmedUtterances; }
    bool any() const { return manualCorrections() > 0 || namedSpeakers > 0 || addedParticipants > 0; }
};

} // namespace tanara::editing
Q_DECLARE_METATYPE(tanara::editing::EditorUtterance)
Q_DECLARE_METATYPE(tanara::editing::EditorSpeaker)
Q_DECLARE_METATYPE(tanara::editing::SpeakerSuggestion)
Q_DECLARE_METATYPE(tanara::editing::PersonInfo)
Q_DECLARE_METATYPE(tanara::editing::RetranscribeImpact)
Q_DECLARE_METATYPE(tanara::editing::SampleDeviation)
Q_DECLARE_METATYPE(tanara::editing::SpeakerSampleStats)

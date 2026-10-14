#pragma once
//
// Domain: the DATA of a meeting's manual speaker-correction layer (<meeting folder>/transcript.speakers.json)
// and the transcript lines it refers to. The operations (load / save / resolution) are in the editor
// (editing/session/SpeakerOverlay.h).
#include "tanara/domain/Person.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace tanara {

// One line of transcript.segments.json + the stable id derived for it.
struct TranscriptLine {
    QString id;         // "u<startMs>" (on collision a "-2", "-3" ... suffix in file order)
    qint64  startMs = 0;
    qint64  endMs = 0;
    QString rawLabel;   // raw diarization label
    QString text;
};

// A manually added participant (one the diarization did not separate).
struct OverlayParticipant {
    QString key;        // "participant:N"
    PersonId personId;  // the bound person; null = anonymous
    QString label;      // anonymous label ("New speaker 1")
    int     colorIndex = 0;
};

// Override / flags of one utterance.
struct OverlayUtterance {
    QString speaker;            // overridden speaker key; empty = no override
    bool    corrected = false;  // reassigned by hand
    bool    confirmed = false;  // "Fine as is"
    // Manual override of "talked over each other": true = not a voice sample ("Fine, but do not
    // use as a sample"), false = usable as a sample (despite the automatic flag); no value = the
    // automatic flag computed from overlap applies (it is not persisted).
    std::optional<bool> noisy;
    // The recheck found it doubtful: it stays uncertain until a correction / confirmation comes
    // (or a newer recheck clears it).
    bool    rechecked = false;
    QString recheckHint;        // key of the speaker that fits the voice better (suggestion); may be empty
    // "Unidentifiable": the line keeps its name (speaking time, transcript, summary), but the
    // voice-based analysis IGNORES it completely: not a sample, not evidence (centroid, core,
    // 2-means, side learning, candidate profile, voiceprint), never uncertain, and not put in a
    // "To review" group. Stronger than noisy (a locked core may still use a noisy line).
    bool    ignored = false;
    bool isDefault() const {
        return speaker.isEmpty() && !corrected && !confirmed && !noisy.has_value() && !rechecked
            && !ignored;
    }
    bool operator==(const OverlayUtterance& o) const {
        return speaker == o.speaker && corrected == o.corrected && confirmed == o.confirmed
            && noisy == o.noisy && rechecked == o.rechecked && recheckHint == o.recheckHint
            && ignored == o.ignored;
    }
};

struct OverlayIdentification {
    PersonId personId;
    double  score = -1.0;
};

// The overlay entries one person-reference rewrite touched (merge of two people; for its undo).
struct OverlayPersonRefs {
    QStringList participantKeys;    // "participant:N" whose person changed
    QStringList identifiedLabels;   // raw labels whose identification changed
    bool isEmpty() const { return participantKeys.isEmpty() && identifiedLabels.isEmpty(); }
};

struct SpeakerOverlay {
    QString transcriptFingerprint;                  // which transcript it belongs to (see below)
    QVector<OverlayParticipant> participants;
    int nextParticipant = 1;                        // number of the next "participant:N"
    int nextAnonymous = 1;                          // number of the next "New speaker N"
    QMap<QString, QString> merged;                  // merged raw label -> target speaker key
    // Manual track assignment ("Which track do they speak on?"): speaker key -> track ids. Missing /
    // empty = no constraint. The single truth for the person's side when given (SideAnalysis).
    QHash<QString, QStringList> speakerTracks;
    QStringList removedRaw;                         // removed (empty) raw speaker columns
    QMap<QString, OverlayUtterance> utterances;     // utterance id -> override
    QMap<QString, OverlayIdentification> identified;// raw label -> result of the voice identification
    QStringList changedSinceSummary;                // keys of the speakers corrected since the summary
    int summaryEpoch = 0;                           // grows when the summary is regenerated / accepted

    // Whether there is a line-/speaker-level correction (identification and the stale flag do not count).
    bool hasEdits() const {
        return !participants.isEmpty() || !merged.isEmpty() || !removedRaw.isEmpty()
            || !utterances.isEmpty() || !speakerTracks.isEmpty();
    }
    bool isEmpty() const {
        return !hasEdits() && identified.isEmpty() && changedSinceSummary.isEmpty()
            && summaryEpoch == 0;
    }
    const OverlayParticipant* participant(const QString& key) const;
    OverlayParticipant* participant(const QString& key);
};

} // namespace tanara

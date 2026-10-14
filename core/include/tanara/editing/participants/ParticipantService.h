#pragma once
//
// ParticipantService: the participant operations of a meeting ("Who was there?"), moved unchanged from
// AppController (cleanup phase 2, P12d): approve / skip / "Only I spoke", "They were not there", add /
// remove a participant, the speaker -> track binding, binding the voice candidates to the transcript's raw
// speakers, and committing a voice identification. Every operation that changes the participant state is
// one step of the meeting's shared undo (ParticipantUndoScope); the bindings go through the meeting's
// transcript-editor session (SpeakerEditSession::applyBindings, one editor step absorbed by the shared step).
//
// The voice-analysis side (evidence, the analysis file's clusters, the approval bindings) comes through
// ParticipantAnalysisPort, which app::Services implements over processing::VoiceAnalysisService: editing
// does not depend on processing (cleanup plan T 1.2). The AppController facade delegates here and re-emits
// the signals.
#include "tanara/domain/Activity.h"
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Overlay.h"
#include "tanara/domain/Participant.h"
#include "tanara/domain/Person.h"
#include "tanara/domain/Voice.h"
#include "tanara/editing/participants/ParticipantSample.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace tanara::library {
class MeetingRepository;
class PeopleService;
class PeopleStore;
}

namespace tanara::editing {

class MeetingUndoRegistry;
class SpeakerEditSession;

// The voice-analysis side of the participant operations.
class ParticipantAnalysisPort {
public:
    virtual ~ParticipantAnalysisPort() = default;
    // The participant's evidence recomputed (heard: whether we heard them; voiceScore: the voice score, < 0 none).
    virtual QVector<Evidence> evidence(const Participant& p, bool heard, double voiceScore, const Meeting& m) = 0;
    virtual ParticipantGroup group(const Participant& p) = 0;
    // The models of the meeting's last voice analysis (participant-analysis file).
    virtual QStringList analysisModelIds(const QString& meetingFolder) = 0;
    // Binds the analysis clusters to the transcript's raw speakers. false: no analysis, or nothing changed.
    virtual bool bindRawSpeakers(QVector<Participant>& participants, const QString& meetingFolder,
                                 const QVector<TranscriptLine>& lines) = 0;
    // The approval's bindings (the analysis clusters and track activity of the meeting are loaded here);
    // trackVeto: the meeting is passed for the manual-track veto.
    virtual QVector<SpeakerBinding> approvalBindings(const QVector<Participant>& all, const QStringList& acceptedIds,
                                                     const QVector<TranscriptLine>& lines, const Meeting& m,
                                                     bool trackVeto) = 0;
    // Whether the meeting's pre-transcript voice analysis is running; cancelling its result.
    virtual bool analysisRunning(const QString& meetingId) const = 0;
    virtual void cancelAnalysis(const QString& meetingId) = 0;
    // The clusters of the meeting's last voice analysis as saved (the "same voice" merges NOT applied;
    // participantsample::withMerges does that). Empty: no analysis.
    virtual QVector<VoiceCluster> analysisClusters(const QString& meetingFolder)
    {
        Q_UNUSED(meetingFolder);
        return {};
    }
};

class ParticipantService : public QObject {
    Q_OBJECT
public:
    struct Deps {
        library::MeetingRepository* store = nullptr;
        library::PeopleStore*       people = nullptr;
        library::PeopleService*     peopleService = nullptr;   // the own person, display names
        MeetingUndoRegistry*        undo = nullptr;
        ParticipantAnalysisPort*    analysis = nullptr;
        // The meeting's transcript-editor session: created on demand / only an open one.
        std::function<SpeakerEditSession*(const QString& meetingId)> editorFor;
        std::function<SpeakerEditSession*(const QString& meetingId)> existingEditor;
    };

    explicit ParticipantService(const Deps& deps, QObject* parent = nullptr);
    ~ParticipantService() override;

    // ---- reading ----
    QVector<Participant> participants(const QString& meetingId) const;
    // The analysis is done but there is no decision yet (approve / skip / "only me"): the banner shows.
    bool participantApprovalPending(const QString& meetingId) const;
    // Participants with a name that are NOT bound to the transcript's speakers (see AppController).
    QVector<Participant> unheardParticipants(const QString& meetingId) const;

    // ---- speaker -> track binding ----
    bool setParticipantTracks(const QString& meetingId, const QString& id, const QStringList& trackIds);
    ParticipantTrackBinding participantTracks(const QString& meetingId, const QString& id);
    // When the transcript arrives: the participants' manual track binding into the still-empty speaker assignments.
    void copyParticipantTracksToOverlay(const QString& meetingId);

    // ---- binding to the transcript and the decision ----
    bool bindRawSpeakers(const QString& meetingId);
    // The transcript arrived: binding + applying the decision (approve / "only me"). true if the decision
    // covered the speakers (automatic identification is skipped).
    bool applyParticipantsToTranscript(const QString& meetingId);
    bool approveParticipants(const QString& meetingId, const QVector<Participant>& accepted);
    bool skipApproval(const QString& meetingId);
    bool unbindParticipant(const QString& meetingId, const QString& participantId);
    QString addParticipant(const QString& meetingId, const PersonChoice& person);
    bool setSoloMeeting(const QString& meetingId);
    bool removeParticipant(const QString& meetingId, const QString& participantId);

    // ---- the "Who was there?" card's row operations (UA1) ----
    // Each is ONE step of the meeting's shared undo; false: nothing changed (unknown meeting / row, no-op).
    // `participantId` is a participant record's id or, after the transcript, also a speaker key (a row of the
    // transcript's speakers without a participant record).
    //
    // Names a row. Before the transcript the record (the voice cluster) gets the person, its approval entry
    // follows; after it the speaker's raw labels are bound to the person through the editor session (the
    // speakers merge if the person already speaks). If the person already has a manual, voiceless row in this
    // meeting, the two rows merge: the manual row disappears, the named row carries the person (and the manual
    // row's track binding / approval). If another VOICED row already carries the person, the named row is
    // merged into that one ("same voice", see mergeParticipantInto). After a decision (approved, not
    // skipped / "only me") the named row counts as approved. Undo text: "Elnevezve: <name>".
    bool nameParticipant(const QString& meetingId, const QString& participantId, const PersonChoice& person);
    // "Same voice as ..." (meeting-local; NOT the global PeopleService::mergePeople). Before the transcript the
    // `from` record joins `into`: raw speakers, sides, speech share, the clusters (Participant::mergedFrom), the
    // larger cluster's sample, the person if `into` is anonymous; the evidence is recomputed and the `from`
    // record and its approval entry disappear. After the transcript the speakers of `from` are bound to
    // `into`'s person (or merged into `into`'s speaker if it is anonymous) through the editor session, and the
    // records merge the same way. Undo text: "Összevonva: <from> → <into>".
    bool mergeParticipantInto(const QString& meetingId, const QString& fromId, const QString& intoId);
    // The doubtful row's "Was there" (true) / its undo (false): sets the record's `approved` and the approval
    // entry's `checked` (if there is a decision), without the approval's other effects. Confirming needs a
    // named row. After the transcript, with a decision, a confirmation binds the row's speakers (one step).
    bool confirmParticipant(const QString& meetingId, const QString& participantId, bool confirmed);
    // "This is me": names the row with the own person (PeopleService::ensureSelf / selfPerson), merging with an
    // existing own row like nameParticipant. false + whyNot (translated, for the UI) if no own name is set.
    bool setSelfParticipant(const QString& meetingId, const QString& participantId, QString* whyNot = nullptr);

    // ---- listening ----
    // The listenable sample of each row (participant id or, after the transcript, speaker key; see
    // participantsample): before the transcript the record's persisted sample, failing that its largest
    // cluster's; after it the speaker's longest clean line (capped at 15 s) on the track of the record's
    // best-overlapping cluster. Invalid: nothing to play (a manual row without a voice, a speaker without a
    // participant record). Same order as `ids`.
    QVector<ParticipantSample> participantSamples(const QString& meetingId, const QStringList& ids) const;
    ParticipantSample participantSample(const QString& meetingId, const QString& id) const;

    // ---- voice identification of the transcript's speakers ----
    // Saves the matches (raw label -> person, score): meeting.json speakerMap, the overlay's
    // "recognized by voice" record, transcript.md with the resolved names, the summary's stale mark.
    // speakerMapChanged (+ summaryStaleChanged). No-op for an empty list / unknown meeting.
    void commitIdentification(const QString& meetingId, const QVector<SpeakerVoiceMatch>& matches);

signals:
    void participantsChanged(QString meetingId);
    void peopleChanged();
    void speakerMapChanged(QString meetingId);
    void summaryStaleChanged(QString meetingId);

private:
    // The speaker keys of a participant (id) or speaker key in the editor (after the transcript).
    QStringList speakerKeysFor(const Meeting& m, const QString& id) const;
    // nameParticipant with the undo text already chosen.
    bool nameParticipantImpl(const QString& meetingId, const QString& participantId, const PersonId& person,
                             const QString& undoText);
    // The record `from` merged into `into` (in memory; see mergeParticipantInto); `from` is removed.
    void mergeRecords(Meeting& m, int fromIdx, int intoIdx);
    // The record's evidence recomputed with its current person (the voice score kept).
    void refreshEvidence(Participant& p, const Meeting& m);
    // The record behind a row id: the participant itself, or (a speaker key) the one bound to that speaker.
    int recordIndexFor(const Meeting& m, const QString& id) const;
    // The bindings through the editor, in one undo step.
    bool applySpeakerBindings(const QString& meetingId, const QVector<SpeakerBinding>& bindings,
                              const QString& undoText);
    SpeakerEditSession* editorFor(const QString& meetingId) const;
    SpeakerEditSession* existingEditor(const QString& meetingId) const;

    Deps m_deps;
};

} // namespace tanara::editing

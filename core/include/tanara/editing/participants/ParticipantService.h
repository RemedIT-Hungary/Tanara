#pragma once
//
// ParticipantService: the participant operations of a meeting ("Who was there?"), moved unchanged from
// AppController (cleanup phase 2, P12d): approve / skip / "Only I spoke", "They were not there", add /
// remove a participant, the speaker -> track binding, binding the voice candidates to the transcript's raw
// speakers, and committing a voice identification. Every operation that changes the participant state is
// one step of the meeting's shared undo (ParticipantUndoScope); the bindings go through the meeting's
// transcript-editor session (SpeakerEditor::applyBindings, one editor step absorbed by the shared step).
//
// The voice-analysis side (evidence, the analysis file's clusters, the approval bindings) comes through
// ParticipantAnalysisPort, which app::Services implements over processing::VoiceAnalysisService: editing
// does not depend on processing (cleanup plan T 1.2). The AppController facade delegates here and re-emits
// the signals.
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Overlay.h"
#include "tanara/domain/Participant.h"
#include "tanara/domain/Person.h"
#include "tanara/domain/Voice.h"

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
class SpeakerEditor;

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
        std::function<SpeakerEditor*(const QString& meetingId)> editorFor;
        std::function<SpeakerEditor*(const QString& meetingId)> existingEditor;
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
    QStringList speakerKeysFor(const Meeting& m, const QString& id);
    // The bindings through the editor, in one undo step.
    bool applySpeakerBindings(const QString& meetingId, const QVector<SpeakerBinding>& bindings,
                              const QString& undoText);
    SpeakerEditor* editorFor(const QString& meetingId) const;
    SpeakerEditor* existingEditor(const QString& meetingId) const;

    Deps m_deps;
};

} // namespace tanara::editing

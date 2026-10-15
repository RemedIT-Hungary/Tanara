#pragma once
//
// IParticipantsFacade: what the "Who was there?" card's decision view model (ParticipantsViewModel), the
// speaker -> track chips (ParticipantTrackBinder) and the person pickers need from the
// participants side, as ONE abstract surface: a meeting's candidate snapshot, the decision
// (approve / skip / unbind), the learning question, the track
// binding, and the person directory.
//
// The real implementation composes editing::ParticipantService, the meeting store, the people store,
// the tag service and the voice analysis (gui/qml/src/ParticipantsServicesFacade); the fictional one
// (demo/ParticipantsDemoFacade) serves --demo, --gallery, --qml-shot and the tests. It is a QObject
// only because it carries signals.
#include "tanara/domain/Participant.h"
#include "tanara/domain/Person.h"
#include "tanara/editing/participants/ParticipantSample.h"
#include "tanara/editing/session/SpeakerEditTypes.h"
#include "tanara/library/tags/PersonTags.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::editing {

// One candidate of the meeting as the card starts from it.
struct ParticipantCandidate {
    QString id;                         // the participant's id within the meeting
    PersonId personId;                  // null: anonymous (unknown voice)
    QString name;                       // display name; empty = anonymous
    ParticipantGroup group = ParticipantGroup::Doubt;
    // The check the card starts with: the earlier decision, else the default rule (an invited but
    // not heard candidate is unchecked).
    bool checked = false;
    bool approved = false;              // the record's approved / confirmed flag (ParticipantService)
    bool manual = false;                // added manually
    QVector<Evidence> evidence;
    QStringList rawIds;                 // the bound raw speakers
    QStringList sides;                  // "mic" | "loopback": heard on
    ParticipantSample sample;           // invalid: nothing to listen to (manual, no audio)
};

struct ParticipantsState {
    bool found = false;                 // the meeting exists
    bool running = false;               // the voice analysis runs
    bool approvalPending = false;       // the analysis is done, there is no decision yet
    bool hasTranscript = false;
    QString selfName;                   // the user's own name (settings)
    QStringList tagIds;                 // the meeting's tags
    QStringList analysisModelIds;       // the models of the last voice analysis (empty: no analysis)
    QDateTime analysisAt;               // when it ran
    QStringList rawLabels;              // the transcript's raw speakers (empty without a transcript)
    QVector<ParticipantCandidate> candidates;
};

// A row the user approved. A named row without a person id has a null personId: the facade
// resolves it by name (a known person, else a new one).
struct ApprovedRow {
    QString id;                         // empty: a row without a stored participant
    PersonId personId;
    QString name;
    QStringList rawIds;
    QStringList sides;
};

struct ApprovalOutcome {
    bool ok = false;
    // The learning question (asked once per meeting): the largest group first.
    QVector<library::PersonTagQuestion> questions;
};

// A track the speaker -> track chips can bind to.
struct ParticipantTrackChoice {
    QString id;
    QString name;
    QString icon;                       // "mic" | "phone" | "audio-lines"
};

class IParticipantsFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IParticipantsFacade() override = default;

    // True: fictional data, the decision does not reach anything.
    virtual bool fictional() const { return false; }
    // Design-state hook of the fictional facade (screenshots): allSure | doubts | invitedNotHeard |
    // running | noTranscriptYet | learnQuestion | learnNewPerson.
    virtual void applyDemoState(const QString& state) { Q_UNUSED(state); }

    // ---- the decision ----
    virtual ParticipantsState state(const QString& meetingId) const = 0;
    // The known people, for the pickers (name-sorted).
    virtual QVector<PersonInfo> peopleDirectory() const = 0;
    virtual ApprovalOutcome approve(const QString& meetingId, const QVector<ApprovedRow>& accepted) = 0;
    virtual bool skip(const QString& meetingId) = 0;
    virtual bool unbind(const QString& meetingId, const QString& participantId) = 0;
    // The answer to a learning question (Yes: the tag onto the people; No: a lasting rejection).
    virtual void answerLearning(const library::PersonTagQuestion& question, bool yes) = 0;

    // ---- the speaker -> track binding ----
    // The tracks of the meeting the chips can choose from (the logical tracks in the mixdown).
    virtual QVector<ParticipantTrackChoice> selectableTracks(const QString& meetingId) const = 0;
    virtual ParticipantTrackBinding participantTracks(const QString& meetingId, const QString& id) const = 0;
    virtual bool setParticipantTracks(const QString& meetingId, const QString& id, const QStringList& trackIds) = 0;

    // ---- the card's row operations (ParticipantService, each one undo step; false: nothing changed) ----
    // `id`: a candidate's id, or after the transcript a speaker key. The defaults do nothing (test doubles).
    // Names the row (before the transcript too); merges with the person's manual voiceless row.
    virtual bool nameParticipant(const QString& meetingId, const QString& id, const PersonChoice& person)
    {
        Q_UNUSED(meetingId); Q_UNUSED(id); Q_UNUSED(person);
        return false;
    }
    // "Same voice as ...": `fromId` joins `intoId` (meeting-local).
    virtual bool mergeParticipantInto(const QString& meetingId, const QString& fromId, const QString& intoId)
    {
        Q_UNUSED(meetingId); Q_UNUSED(fromId); Q_UNUSED(intoId);
        return false;
    }
    // "Was there" (true) / its undo (false).
    virtual bool confirmParticipant(const QString& meetingId, const QString& id, bool confirmed)
    {
        Q_UNUSED(meetingId); Q_UNUSED(id); Q_UNUSED(confirmed);
        return false;
    }
    // "This is me". false + whyNot (translated) when no own name is set.
    virtual bool setSelfParticipant(const QString& meetingId, const QString& id, QString* whyNot = nullptr)
    {
        Q_UNUSED(meetingId); Q_UNUSED(id); Q_UNUSED(whyNot);
        return false;
    }

signals:
    void participantsChanged(QString meetingId);
    void participantAnalysisStarted(QString meetingId);
    void participantAnalysisFinished(QString meetingId);
    void tracksChanged(QString meetingId);
    void speakerMapChanged(QString meetingId);
};

} // namespace tanara::editing

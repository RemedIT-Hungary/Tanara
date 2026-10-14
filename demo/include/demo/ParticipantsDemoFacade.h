#pragma once
//
// ParticipantsDemoFacade: the fictional implementation of tanara::editing::IParticipantsFacade for
// --demo, --gallery, --qml-shot and the tests: the candidates of the "Who was there?" dialog's design
// states, the made-up person directory of the pickers, the tag-based suggestion and the learning
// questions after "Continue", and the speaker -> track chips (two tracks, bound in memory).
//
// One per view model (the state is the view model's demoState): applyDemoState() = allSure | doubts |
// invitedNotHeard | addPerson | running | noTranscriptYet | learnQuestion | learnNewPerson; empty =
// doubts. Nothing reaches anything: approve / skip / unbind only report success.
#include "tanara/editing/participants/IParticipantsFacade.h"

#include <QHash>

namespace tanara_demo {

class ParticipantsDemoFacade : public tanara::editing::IParticipantsFacade {
    Q_OBJECT
public:
    explicit ParticipantsDemoFacade(QObject* parent = nullptr) : IParticipantsFacade(parent) {}

    bool fictional() const override { return true; }
    void applyDemoState(const QString& state) override;

    tanara::editing::ParticipantsState state(const QString& meetingId) const override;
    QVector<tanara::editing::PersonInfo> peopleDirectory() const override;
    QVector<tanara::editing::PersonInfo> dialogPeople() const override;
    tanara::editing::TagPeopleHint suggestPeople(const QStringList& tagIds,
                                                 const QVector<tanara::PersonId>& presentIds,
                                                 const QStringList& presentNames, int limit) const override;
    tanara::editing::ApprovalOutcome approve(const QString& meetingId,
                                             const QVector<tanara::editing::ApprovedRow>& accepted) override;
    bool skip(const QString&) override { return true; }
    bool unbind(const QString&, const QString&) override { return true; }
    void answerLearning(const tanara::library::PersonTagQuestion&, bool) override {}

    QVector<tanara::editing::ParticipantTrackChoice> selectableTracks(const QString& meetingId) const override;
    tanara::ParticipantTrackBinding participantTracks(const QString& meetingId, const QString& id) const override;
    bool setParticipantTracks(const QString& meetingId, const QString& id, const QStringList& trackIds) override;

private:
    QString m_state;
    QHash<QString, QStringList> m_manualTracks;   // participant id -> tracks (p3: bound to both at first)
    bool m_tracksSeeded = false;
};

} // namespace tanara_demo

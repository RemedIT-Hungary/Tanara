#pragma once
//
// ParticipantsDemoFacade: the fictional implementation of tanara::editing::IParticipantsFacade for
// --demo, --gallery, --qml-shot and the tests: the candidates of the "Who was there?" design
// states, the made-up person directory of the pickers and the learning
// questions after "Approve", and the speaker -> track chips (two tracks, bound in memory).
//
// One per view model (the state is the view model's demoState): applyDemoState() = allSure | doubts |
// invitedNotHeard | running | noTranscriptYet | learnQuestion | learnNewPerson; empty =
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
    tanara::editing::ApprovalOutcome approve(const QString& meetingId,
                                             const QVector<tanara::editing::ApprovedRow>& accepted) override;
    bool skip(const QString&) override { return true; }
    bool unbind(const QString&, const QString&) override { return true; }
    void answerLearning(const tanara::library::PersonTagQuestion&, bool) override {}

    QVector<tanara::editing::ParticipantTrackChoice> selectableTracks(const QString& meetingId) const override;
    tanara::ParticipantTrackBinding participantTracks(const QString& meetingId, const QString& id) const override;
    bool setParticipantTracks(const QString& meetingId, const QString& id, const QStringList& trackIds) override;

    // The row operations in memory (applied over the design state's candidates).
    bool nameParticipant(const QString& meetingId, const QString& id, const tanara::PersonChoice& person) override;
    bool mergeParticipantInto(const QString& meetingId, const QString& fromId, const QString& intoId) override;
    bool confirmParticipant(const QString& meetingId, const QString& id, bool confirmed) override;
    bool setSelfParticipant(const QString& meetingId, const QString& id, QString* whyNot = nullptr) override;

private:
    tanara::editing::ParticipantsState baseState() const;
    bool hasCandidate(const QString& id) const;

    QString m_state;
    QHash<QString, QStringList> m_manualTracks;   // participant id -> tracks (p3: bound to both at first)
    bool m_tracksSeeded = false;
    QHash<QString, QString> m_named;        // candidate id -> the name given on the card
    QHash<QString, QString> m_mergedInto;   // merged-away candidate id -> the one it joined
    QHash<QString, bool> m_confirmed;       // candidate id -> "Was there"
};

} // namespace tanara_demo

#pragma once
//
// ParticipantsServicesFacade: the real tanara::editing::IParticipantsFacade, composed from the
// participant service, the meeting store, the people / voiceprint stores, the tag service and the
// voice analysis (no AppController). One per controller, built by AppContext::servicesFor().
//
#include "tanara/editing/participants/IParticipantsFacade.h"

namespace tanara {
namespace app { class SettingsManager; }
namespace editing { class ParticipantService; }
namespace library {
class MeetingRepository;
class PeopleService;
class PeopleStore;
class TagService;
class VoiceprintStore;
}
namespace processing { class VoiceAnalysisService; }
}

namespace tanara_qml {

class ControllerEvents;

class ParticipantsServicesFacade : public tanara::editing::IParticipantsFacade {
    Q_OBJECT
public:
    struct Deps {
        tanara::library::MeetingRepository* store = nullptr;
        tanara::library::PeopleStore* people = nullptr;
        tanara::library::PeopleService* peopleService = nullptr;   // the own person
        tanara::library::VoiceprintStore* voiceprints = nullptr;
        tanara::library::TagService* tags = nullptr;
        tanara::app::SettingsManager* settings = nullptr;
        tanara::editing::ParticipantService* participants = nullptr;
        tanara::processing::VoiceAnalysisService* voice = nullptr;
        ControllerEvents* events = nullptr;
    };
    explicit ParticipantsServicesFacade(const Deps& deps, QObject* parent = nullptr);

    tanara::editing::ParticipantsState state(const QString& meetingId) const override;
    QVector<tanara::editing::PersonInfo> peopleDirectory() const override;
    tanara::editing::TagPeopleHint suggestPeople(const QStringList& tagIds,
                                                 const QVector<tanara::PersonId>& presentIds,
                                                 const QStringList& presentNames, int limit) const override;
    tanara::editing::ApprovalOutcome approve(const QString& meetingId,
                                             const QVector<tanara::editing::ApprovedRow>& accepted) override;
    bool skip(const QString& meetingId) override;
    bool unbind(const QString& meetingId, const QString& participantId) override;
    void answerLearning(const tanara::library::PersonTagQuestion& question, bool yes) override;

    QVector<tanara::editing::ParticipantTrackChoice> selectableTracks(const QString& meetingId) const override;
    tanara::ParticipantTrackBinding participantTracks(const QString& meetingId, const QString& id) const override;
    bool setParticipantTracks(const QString& meetingId, const QString& id, const QStringList& trackIds) override;

private:
    Deps m_d;
};

} // namespace tanara_qml

#pragma once
//
// TranscriptEditorServicesFacade: the real tanara::editing::ITranscriptEditorFacade, over the
// editor sessions (editing::EditorSessions), the stores, the tag service and the settings (no
// AppController). One per controller, built by AppContext::servicesFor(), which also relays the
// people / voiceprint changes into peopleChanged().
//
#include "tanara/editing/ITranscriptEditorFacade.h"

#include <QPointer>

namespace tanara::app { class SettingsManager; }
namespace tanara::editing { class EditorSessions; }
namespace tanara::library {
class MeetingRepository;
class PeopleStore;
class TagService;
class UiStateRepository;
class VoiceprintStore;
}

namespace tanara_qml {

class TranscriptEditorServicesFacade : public tanara::editing::ITranscriptEditorFacade {
    Q_OBJECT
public:
    struct Deps {
        tanara::editing::EditorSessions*     sessions = nullptr;
        tanara::library::MeetingRepository*  store = nullptr;
        tanara::library::UiStateRepository*  uiState = nullptr;      // may be null: the rail is not remembered
        tanara::library::PeopleStore*        people = nullptr;       // may be null
        tanara::library::VoiceprintStore*    voiceprints = nullptr;  // may be null
        tanara::library::TagService*         tags = nullptr;         // may be null
        tanara::app::SettingsManager*        settings = nullptr;     // may be null: auto-play in memory
    };
    explicit TranscriptEditorServicesFacade(const Deps& deps, QObject* parent = nullptr);

    bool live() const override { return true; }
    Session open(const QString& meetingId) override;
    QVector<tanara::Track> tracks(const QString& meetingId) const override;
    bool railVisible(const QString& meetingId) const override;
    void setRailVisible(const QString& meetingId, bool visible) override;
    QVector<tanara::editing::PersonInfo> people() const override;
    QString tagName(const QString& tagId) const override;
    tanara::PersonId selfId() const override;
    bool addPersonTag(const tanara::PersonId& person, const QString& tagId) override;
    QVector<tanara::library::PersonTagQuestion> newPersonTagQuestions(const QString& meetingId,
                                                                      const tanara::PersonId& person,
                                                                      const QString& name) override;
    void answerTagQuestion(const tanara::library::PersonTagQuestion& question, bool yes) override;
    void undoTagAnswer() override;
    bool reviewAutoPlay() const override;
    void setReviewAutoPlay(bool on) override;

private:
    QPointer<tanara::editing::EditorSessions> m_sessions;
    QPointer<tanara::library::MeetingRepository> m_store;
    tanara::library::UiStateRepository* m_uiState = nullptr;
    tanara::library::PeopleStore* m_people = nullptr;
    tanara::library::VoiceprintStore* m_voiceprints = nullptr;
    QPointer<tanara::library::TagService> m_tags;
    QPointer<tanara::app::SettingsManager> m_settings;
    bool m_reviewAutoPlay = true;   // without settings
};

} // namespace tanara_qml

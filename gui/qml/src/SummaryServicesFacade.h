#pragma once
//
// SummaryServicesFacade: the real tanara::processing::ISummaryFacade, composed from the
// summarization service (+ its topic queue), the job tracker, the meeting store and the settings.
// What still lives on the AppController facade (the gating, the speaker editor, the stale
// information, the targeted statements) comes in as small hooks that AppContext wires: when those
// move into their own services only the hooks change. One per controller, built by
// AppContext::servicesFor().
//
#include "tanara/domain/Meeting.h"
#include "tanara/processing/summary/ISummaryFacade.h"

#include <QHash>
#include <QPointer>

#include <functional>

namespace tanara {
namespace app { class SettingsManager; }
namespace editing { class SpeakerEditor; }
namespace library { class MeetingJobTracker; class MeetingRepository; }
namespace processing { class SummarizationService; }
}

namespace tanara_qml {

class SummaryServicesFacade : public tanara::processing::ISummaryFacade {
    Q_OBJECT
public:
    struct Hooks {
        std::function<tanara::providers::ReadinessResult(const QString& meetingId)> canRun;
        std::function<bool()> usesCloud;
        std::function<tanara::editing::SpeakerEditor*(const QString& meetingId)> speakerEditor;
        std::function<tanara::SummaryStaleInfo(const tanara::Meeting& meeting)> staleOfMeeting;
        std::function<tanara::SummaryStaleInfo(const QString& meetingId)> staleOfId;
        std::function<QVector<tanara::SummaryStatement>(const QString& meetingId)> statements;
        std::function<void(const QString& meetingId)> dismissStale;
    };
    struct Deps {
        tanara::processing::SummarizationService* summaries = nullptr;
        tanara::library::MeetingJobTracker* jobs = nullptr;
        tanara::library::MeetingRepository* store = nullptr;
        tanara::app::SettingsManager* settings = nullptr;
        QObject* noteController = nullptr;   // transitional, see ISummaryFacade::noteController()
        Hooks hooks;
    };
    explicit SummaryServicesFacade(const Deps& deps, QObject* parent = nullptr);

    // The controller's signals that are not on a service, forwarded by AppContext.
    void reportChanged(const QString& meetingId) { emit changed(meetingId); }
    void reportSpeakersChanged(const QString& meetingId) { emit speakersChanged(meetingId); }

    tanara::processing::SummaryData load(const QString& meetingId) const override;
    tanara::processing::SummaryJobs jobs(const QString& meetingId) const override;
    tanara::processing::SummarySpeakers speakers(const QString& meetingId) const override;
    tanara::processing::SummaryQuote quote(const QString& meetingId, const QString& utteranceId) const override;
    QString chatProviderName(const QString& providerId) const override;
    bool topicsResumable(const QString& meetingId) const override;

    bool flagStatement(const QString& meetingId, const QString& statementId) override;
    void dismissStale(const QString& meetingId) override;
    void clearError(const QString& meetingId) override;

    QVector<tanara::SummaryTopic> topics(const QString& meetingId) const override;
    QVector<tanara::TopicStatus> topicStatuses(const QString& meetingId) const override;
    QVector<tanara::TopicAnalysis> topicAnalyses(const QString& meetingId) const override;
    void setTopics(const QString& meetingId, const QVector<tanara::SummaryTopic>& topics) override;
    void cancelTopic(const QString& meetingId, const QString& topicId) override;

    QObject* noteController() const override { return m_noteController; }

private:
    // The editor's speakersChanged goes out as ours for its meeting (once per editor).
    void watchEditor(const QString& meetingId, tanara::editing::SpeakerEditor* editor) const;

    Deps m_deps;
    QPointer<QObject> m_noteController;
    mutable QHash<QString, QPointer<tanara::editing::SpeakerEditor>> m_watched;
};

} // namespace tanara_qml

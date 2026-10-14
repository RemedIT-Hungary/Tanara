#pragma once
//
// ISummaryFacade: what the Summary tab's view model (and its per-topic workspace) needs from the
// summary side, as ONE abstract surface. The real implementation composes the summarization
// service, the job tracker, the speaker editor and the settings (gui/qml/src/SummaryServicesFacade);
// the fictional one (demo/SummaryDemoFacade) serves the design states for --demo, --gallery,
// --qml-shot and the tests.
//
// It is a QObject only because it carries signals. The view model never learns which
// implementation it talks to: what a fixture has to pin (the generation date text, the open topic
// workspace, the note block ...) is part of this surface as an optional "pin" that the real
// facade leaves empty.
//
// The data are plain domain values: the view model turns them into what QML shows (labels, the
// gating banner, the speaker chips, the memo sections ...).
#include "tanara/domain/Job.h"
#include "tanara/domain/Summary.h"
#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/providers/core/ReadinessModel.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace tanara::processing {

// A speaker of the meeting, as the summary's participants and owner chips show it.
struct SummarySpeaker {
    QString name;          // the shown name (the person's, or an anonymous label)
    QString personName;    // the bound person's name; empty for an anonymous speaker
    int colorIndex = 0;
    double share = 0.0;    // 0..1 of the speaking time
};

// The meeting's speakers, in the order the participants list shows them (by speaking time).
struct SummarySpeakers {
    QVector<SummarySpeaker> list;
    // The numbers the "The transcript is ready, N speakers, M utterances" line and the generation
    // line quote. speakerCount < 0: the list's size.
    int speakerCount = -1;
    int utteranceCount = 0;
};

// An utterance quoted as the source of a statement. Empty text and name: not found.
struct SummaryQuote {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    QString name;
    int colorIndex = -1;
};

// The running tasks of a meeting: the summary / topic suggestion that the tab shows, and the topic
// analysis queue. Invalid (JobProgress::isValid) = not running.
struct SummaryJobs {
    JobProgress shown;
    JobProgress analyze;
};

// A fixture's note block content (the meeting note beside the summary).
struct SummaryNotePin {
    QString note;
    QString detectedCall;
    bool suggestions = false;   // the template suggestions of earlier meetings are shown
};

// Everything the tab reads for one meeting in one go.
struct SummaryData {
    bool valid = false;                 // the meeting exists
    qint64 durationMs = 0;

    // Gating of the summary step and the provider it would use.
    providers::ReadinessResult readiness;
    bool usesCloud = false;
    QString providerName;               // display name of the selected chat provider; empty: unknown
    // "Tanara Cloud - coming soon": the step lacks a provider setting (a hint under the start button).
    bool cloudSoon = false;

    // The last attempt's leftover error, and whether its finished partial notes are kept.
    JobError summaryError;
    bool errorKeptParts = false;

    library::SummaryDocument document;
    SummaryStaleInfo stale;
    // The quick summary's statements per the current speaker state (empty for an old / per-topic summary).
    QVector<SummaryStatement> statements;
    SummarySpeakers speakers;
    SummaryJobs jobs;

    // ---- fixture pins (the real facade leaves them empty) ----
    QString generatedText;                  // the generation date as shown, instead of meta.createdAt
    QString providerText;                   // the provider as shown, instead of the one of meta.providerId
    std::optional<bool> topicsOpen;         // the per-topic workspace is open / closed
    std::optional<SummaryNotePin> note;     // the note block's content
};

// The tab's initial UI state for a fixture (screenshots).
struct SummaryUiPreset {
    bool memo = false;                  // the memo section is shown
    bool noteOpen = false;
    bool sourcesVisible = true;
    QString activeStatementId;
    int activeSection = -1;
};

class ISummaryFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ISummaryFacade() override = default;

    // True for the fictional implementation (the UI may mark sample data; never branch on it).
    virtual bool fictional() const { return false; }
    virtual std::optional<SummaryUiPreset> uiPreset() const { return std::nullopt; }

    virtual SummaryData load(const QString& meetingId) const = 0;
    virtual SummaryJobs jobs(const QString& meetingId) const = 0;
    virtual SummarySpeakers speakers(const QString& meetingId) const = 0;
    virtual SummaryQuote quote(const QString& meetingId, const QString& utteranceId) const = 0;
    // The chat provider's display name for a provider id; empty: unknown.
    virtual QString chatProviderName(const QString& providerId) const = 0;
    // A topic list exists but no summary yet: an interrupted per-topic analysis the workspace resumes.
    virtual bool topicsResumable(const QString& meetingId) const = 0;

    // "Not said like this? Report": marks the statement; false: no such meeting / statement.
    virtual bool flagStatement(const QString& meetingId, const QString& statementId) = 0;
    // "Fine as it is": dismisses the stale marker without regenerating.
    virtual void dismissStale(const QString& meetingId) = 0;
    // Dismisses the leftover error of the last summary attempt.
    virtual void clearError(const QString& meetingId) = 0;

    // ---- the per-topic workspace ----
    virtual QVector<SummaryTopic> topics(const QString& meetingId) const = 0;
    virtual QVector<TopicStatus> topicStatuses(const QString& meetingId) const = 0;
    virtual QVector<TopicAnalysis> topicAnalyses(const QString& meetingId) const = 0;
    // Saves the list in the given order (new topics get an id, empty titles are dropped). topicsChanged.
    virtual void setTopics(const QString& meetingId, const QVector<SummaryTopic>& topics) = 0;
    virtual void cancelTopic(const QString& meetingId, const QString& topicId) = 0;

    // Transitional: the meeting note model (MeetingNoteModel, shared with the pre-transcript
    // view) still reads the controller itself; the real facade hands it over. nullptr for a fixture.
    virtual QObject* noteController() const { return nullptr; }

signals:
    // Something of the meeting changed: the tab reloads. An empty id: every meeting (settings).
    void changed(const QString& meetingId);
    // The running tasks' progress changed.
    void jobsChanged(const QString& meetingId);
    // The speakers changed (rename, bind, ...).
    void speakersChanged(const QString& meetingId);
    // The meeting's summary is done / the suggested topics arrived.
    void summaryArrived(const QString& meetingId);
    void topicsArrived(const QString& meetingId);
    // The meeting note (meeting.json) was updated elsewhere.
    void noteUpdated(const QString& meetingId);
    // The topic workspace.
    void topicsChanged(const QString& meetingId);
    void topicStatusChanged(const QString& meetingId, const QString& topicId);
    void topicAnalysisReady(const QString& meetingId, const tanara::TopicAnalysis& analysis);
    void topicAnalysisFailed(const QString& meetingId, const QString& topicId, const QString& error);
};

} // namespace tanara::processing

#pragma once
//
// ShellMeetingModel: the selected meeting's shell-level state: header (title, meta line),
// whether it has a transcript (-> tabs or the pre-transcript view), whether its summary is
// stale (-> pill on the tab), and its cancelable running tasks for the task bar
// (MeetingJobTracker). The data comes from a tanara::editing::IShellFacade (the real one, or the fictional
// sample library in demo mode); the model never branches on which.
//
// The header's tag row (TagRow) gets the `tags` model: the selected meeting's tags and
// suggestions. It requests a suggestion when a transcribed meeting appears and when its
// transcript is (re)made; co-occurring ones are requested by MeetingTagsModel after a tag is
// attached; the LLM suggestion comes by itself after the summary. In the demo the fictional
// tag pool (demoState from QML).
//
#include "MeetingTagsModel.h"

#include "tanara/domain/Job.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara::editing { class IShellFacade; }

namespace tanara_qml {

class ShellMeetingModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(bool exists READ exists NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    // "Oct 1, 2026 · 1:16:04 · 6 speakers" (without a transcript the track count).
    Q_PROPERTY(QString metaText READ metaText NOTIFY changed)
    Q_PROPERTY(bool hasTranscript READ hasTranscript NOTIFY changed)
    Q_PROPERTY(bool summaryStale READ summaryStale NOTIFY changed)
    // Whether it has a (quick or per-topic) summary: whether the Executive summary / Memo tab is available.
    Q_PROPERTY(bool hasSummary READ hasSummary NOTIFY changed)
    // Running transcription / summary for the tabs' pills: -1 = not running; otherwise 0..100
    // (0 for indeterminate progress).
    Q_PROPERTY(int transcribePercent READ transcribePercent NOTIFY tasksChanged)
    Q_PROPERTY(int summarizePercent READ summarizePercent NOTIFY tasksChanged)
    // The summary's provider ("LM Studio · own key"): the tab's tooltip panel says it.
    Q_PROPERTY(QString summaryProviderLabel READ summaryProviderLabel NOTIFY changed)
    // Whether there is at least one active audio track (the precondition of identification).
    Q_PROPERTY(bool canIdentify READ canIdentify NOTIFY changed)
    Q_PROPERTY(bool identifyRunning READ identifyRunning NOTIFY tasksChanged)
    // The task bar's rows: [{ kind, title, detail, eta, iconName, percent (0..100 | -1),
    //                        cancellable, cancelling }]
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged)
    // The header tag row's model (bound to the selected meeting).
    Q_PROPERTY(tanara_qml::MeetingTagsModel* tags READ tags CONSTANT)
    // Demo / screenshot only: a sample task in the bar.
    Q_PROPERTY(bool demoTask READ demoTask WRITE setDemoTask NOTIFY tasksChanged)

public:
    // From QML: follows the App singleton's services (AppContext::services().shell).
    explicit ShellMeetingModel(QObject* parent = nullptr);
    // With a given facade (tests, hosts); it is not taken over.
    explicit ShellMeetingModel(tanara::editing::IShellFacade* facade, QObject* parent = nullptr);

    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    bool exists() const { return m_exists; }
    QString title() const { return m_title; }
    QString metaText() const { return m_meta; }
    bool hasTranscript() const { return m_hasTranscript; }
    bool summaryStale() const { return m_summaryStale; }
    bool hasSummary() const { return m_hasSummary; }
    int transcribePercent() const { return m_transcribePercent; }
    int summarizePercent() const { return m_summarizePercent; }
    QString summaryProviderLabel() const { return m_summaryProvider; }
    bool canIdentify() const { return m_canIdentify; }
    bool identifyRunning() const { return m_identifyRunning; }
    QVariantList tasks() const { return m_tasks; }
    bool demoTask() const { return m_demoTask; }
    void setDemoTask(bool on);
    MeetingTagsModel* tags() const { return m_tags; }

    // A running task's description in the bar (static: testable).
    static QVariantMap describeJob(const tanara::JobProgress& job);

signals:
    void meetingIdChanged();
    void changed();
    void tasksChanged();

private:
    void attach();
    void reload();
    void reloadTasks();
    void onMeetingTouched(const QString& id);
    void syncTags();

    QString m_meetingId;
    bool m_exists = false;
    QString m_title;
    QString m_meta;
    bool m_hasTranscript = false;
    bool m_summaryStale = false;
    bool m_hasSummary = false;
    int m_transcribePercent = -1;
    int m_summarizePercent = -1;
    QString m_summaryProvider;
    bool m_canIdentify = false;
    bool m_identifyRunning = false;
    bool m_demoTask = false;
    QVariantList m_tasks;

    MeetingTagsModel* m_tags = nullptr;
    QPointer<tanara::editing::IShellFacade> m_facade;
    QPointer<tanara::editing::IShellFacade> m_attached;
    bool m_injected = false;
    QString m_tagsRequestedFor;   // a suggestion was already requested for this meeting (once per selection)
};

} // namespace tanara_qml

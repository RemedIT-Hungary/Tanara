#pragma once
//
// Tanara QML: view model of the pre-transcript view (PreTranscriptView.qml). It serves the
// three states of a meeting without a transcript from the processing state:
//   "steps"   : steps: context, preparation, transcription with honest gating (canRun);
//   "running" : the transcription task's phases with REAL progress (JobProgress);
//   "failed"  : the leftover error's human message + technical line + fix action.
//
// Step 1's tags (before a transcript the header has no tag row): `tags` (the field's chips)
// and the suggestion row. The suggestion comes from the meeting's regular suggestions if there
// are any; until there is a transcript, from the title (AppController::draftTagSuggestions),
// without the already attached and the rejected ones. Accepting / rejecting is one undoable step each (toast).
//
// It does NOT start the transcription (that is the shell's job: shell.startTranscription:
// gating); it only provides state and saves the meeting's settings (context,
// identification switch).
//
// Without a controller or with App.demo it shows built-in FICTIONAL sample data; demoState
// selects the state ("steps" | "ready" | "running" | "uploading" | "failed" |
// "note": steps with template suggestions and a detected call | "readyNote": can start, with
// template suggestions and a detected call; the Overview's demo).
//
#include "MeetingNoteModel.h"
#include "MeetingTagsModel.h"

#include "tanara/jobs/JobTypes.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class PreTranscriptViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Defaults to the App singleton's controller; injectable in tests.
    Q_PROPERTY(QObject* controller READ controllerObject WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    Q_PROPERTY(bool demo READ demo NOTIFY changed)

    // "none" (no meeting) | "steps" | "running" | "failed"
    Q_PROPERTY(QString state READ state NOTIFY changed)

    // ---- context ----
    // Editing the note (draft, save, template suggestions, detected call): the
    // MeetingNoteEditor gets this. contextNote is its saved value (shorthand).
    Q_PROPERTY(tanara_qml::MeetingNoteModel* note READ note CONSTANT)
    Q_PROPERTY(QString contextNote READ contextNote WRITE setContextNote NOTIFY contextNoteChanged)
    // Step 1's tag field (the meeting's tags).
    Q_PROPERTY(tanara_qml::MeetingTagsModel* tags READ tags CONSTANT)
    // The suggestion row: [{ index, id, name, isNew, source }] (at most 3).
    Q_PROPERTY(QVariantList tagSuggestions READ tagSuggestions NOTIFY tagSuggestionsChanged)
    // Short reason for the first suggestion: "similar title: "..."" | "shared participant: ..." | "shared terms: ...".
    Q_PROPERTY(QString tagSuggestionReason READ tagSuggestionReason NOTIFY tagSuggestionsChanged)
    Q_PROPERTY(bool identifyEnabled READ identifyEnabled WRITE setIdentifyEnabled NOTIFY changed)
    Q_PROPERTY(bool identifyAvailable READ identifyAvailable NOTIFY changed)
    // "ready" | "missing" | "stale" | "running"
    Q_PROPERTY(QString mixdownState READ mixdownState NOTIFY changed)
    Q_PROPERTY(int mixdownPercent READ mixdownPercent NOTIFY mixdownPercentChanged)
    // A standalone mixdown is running (cancelable with shell.cancelJob(..., JobKinds.Mixdown)).
    Q_PROPERTY(bool mixdownCancellable READ mixdownCancellable NOTIFY changed)
    Q_PROPERTY(bool canStart READ canStart NOTIFY changed)
    // Empty if it can start; otherwise { title, text, actionLabel, actionPage, reason, kind }.
    Q_PROPERTY(QVariantMap blocker READ blocker NOTIFY changed)
    Q_PROPERTY(QString providerLabel READ providerLabel NOTIFY changed)

    // "Tanara Cloud - coming soon" hint: shown while the step lacks a provider setting.
    Q_PROPERTY(bool cloudSoon READ cloudSoon NOTIFY changed)

    // ---- M04 ----
    Q_PROPERTY(QString jobTitle READ jobTitle NOTIFY jobChanged)
    Q_PROPERTY(QString etaText READ etaText NOTIFY jobChanged)
    // [{ id, label, state, percent, detail }] — state: waiting | running | done | failed | skipped
    Q_PROPERTY(QVariantList stages READ stages NOTIFY jobChanged)
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY jobChanged)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY jobChanged)
    Q_PROPERTY(QString footerLine READ footerLine NOTIFY changed)

    // ---- M05 ----
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY changed)
    Q_PROPERTY(QString fixActionLabel READ fixActionLabel NOTIFY changed)
    Q_PROPERTY(QString fixActionPage READ fixActionPage NOTIFY changed)

public:
    explicit PreTranscriptViewModel(QObject* parent = nullptr);

    QObject* controllerObject() const;
    void setController(QObject* controller);
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    bool demo() const;

    QString state() const { return m_state; }

    MeetingNoteModel* note() const { return m_note; }
    QString contextNote() const { return m_note->note(); }
    void setContextNote(const QString& note);
    // The field's content while typing: remembered TOGETHER WITH the meeting it was written for.
    // The delayed save (and a meeting switch) writes it out, always into its own meeting, even
    // if the selection changed meanwhile (e.g. a recording ended).
    // (MeetingNoteModel's draft / commitDraft operations.)
    Q_INVOKABLE void draftContextNote(const QString& note) { m_note->draft(note); }
    Q_INVOKABLE void commitContextDraft() { m_note->commitDraft(); }
    MeetingTagsModel* tags() const { return m_tags; }
    QVariantList tagSuggestions() const;
    QString tagSuggestionReason() const;
    // Attaching / rejecting one item of the suggestion row (undoable step + toast).
    Q_INVOKABLE void acceptTagSuggestion(int index);
    Q_INVOKABLE void rejectTagSuggestion(int index);
    bool identifyEnabled() const { return m_identifyEnabled; }
    void setIdentifyEnabled(bool enabled);
    bool identifyAvailable() const { return m_identifyAvailable; }
    QString mixdownState() const { return m_mixdownState; }
    int mixdownPercent() const { return m_mixdownPercent; }
    bool mixdownCancellable() const { return m_mixdownCancellable; }
    bool canStart() const { return m_canStart; }
    QVariantMap blocker() const { return m_blocker; }
    QString providerLabel() const { return m_providerLabel; }

    bool cloudSoon() const { return m_cloudSoon; }

    QString jobTitle() const { return m_jobTitle; }
    QString etaText() const;
    QVariantList stages() const { return m_stages; }
    bool cancellable() const { return m_cancellable; }
    bool cancelling() const { return m_cancelling; }
    QString footerLine() const;

    QString errorMessage() const { return m_errorMessage; }
    QString errorDetail() const { return m_errorDetail; }
    QString fixActionLabel() const { return m_fixActionLabel; }
    QString fixActionPage() const { return m_fixActionPage; }

    // Re-reads gating and state (e.g. after Settings closes).
    Q_INVOKABLE void refresh();
    // Starts the mixdown now (no need to wait for the transcription).
    Q_INVOKABLE void startMixdown();
    // Dismisses the leftover error -> back to the steps (change context / preparation).
    Q_INVOKABLE void clearError();

signals:
    void controllerChanged();
    void meetingIdChanged();
    void demoStateChanged();
    void contextNoteChanged();
    void tagSuggestionsChanged();
    // Notice after a tag step (the view shows it with the shell's toast, with "Undo").
    void toast(const QString& text, bool undoable);
    void mixdownPercentChanged();
    void jobChanged();
    void changed();

private:
    tanara::AppController* app() const;
    void connectController();
    void reload();
    void loadDemo();
    void applyJob(const tanara::JobProgress& job);
    void reloadTagSuggestions();

    QPointer<QObject> m_injected;
    QPointer<tanara::AppController> m_connected;
    QList<QMetaObject::Connection> m_connections;

    QString m_meetingId;
    QString m_demoState;
    QString m_state = QStringLiteral("none");

    MeetingNoteModel* m_note = nullptr;
    MeetingTagsModel* m_tags = nullptr;
    QString m_title;                               // for the draft suggestion
    QVector<TagSuggestionItem> m_tagSuggestions;   // the suggestion row
    bool m_tagSuggestionsFromModel = false;        // the regular suggestions (not the title-based draft)
    bool m_identifyEnabled = true;
    bool m_identifyAvailable = true;
    QString m_mixdownState = QStringLiteral("ready");
    int m_mixdownPercent = -1;
    bool m_mixdownCancellable = false;
    bool m_canStart = false;
    QVariantMap m_blocker;
    QString m_providerLabel;

    bool m_cloudSoon = false;

    QString m_jobTitle;
    QVariantList m_stages;
    bool m_cancellable = true;
    bool m_cancelling = false;
    tanara::JobProgress m_job;      // for the ETA (startedAt + estimate)
    QString m_demoEta;
    QTimer m_etaTimer;

    QString m_errorMessage;
    QString m_errorDetail;
    QString m_fixActionLabel;
    QString m_fixActionPage;
};

} // namespace tanara_qml

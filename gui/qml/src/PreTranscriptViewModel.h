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
// What it shows comes from the transcription step's facade (tanara::processing::ITranscribeStepFacade)
// as a snapshot: from QML the App singleton's services (AppContext::services().transcribeStep), from
// C++ the given services. With App.demo / without a controller that is the FICTIONAL facade of
// tanara_demo; demoState selects its design state ("steps" | "ready" | "mixing" | "running" |
// "uploading" | "failed" | "note": steps with template suggestions and a detected call |
// "readyNote": can start, with template suggestions and a detected call; the Overview's demo).
//
#include "MeetingNoteModel.h"
#include "MeetingTagsModel.h"
#include "UiServices.h"

#include "tanara/domain/Job.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace tanara::processing { class ITranscribeStepFacade; struct TranscribeStepSnapshot; }

namespace tanara_qml {

class PreTranscriptViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

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
    // Whisper's context budget (providers/stt/WhisperPrompt.h), only when the transcriber is
    // whisper-compat: contextBudgetState "" (another transcriber, or well within) | "hint" (above 70 %
    // of the budget) | "over" (the start of the note will be cut); the estimate of the full prompt
    // (note + terms), the budget, and the preview of what the model gets (rich text: the dropped start
    // of the note struck through, then the kept part and the terms).
    Q_PROPERTY(QString contextBudgetState READ contextBudgetState NOTIFY contextBudgetChanged)
    Q_PROPERTY(int contextTokenEstimate READ contextTokenEstimate NOTIFY contextBudgetChanged)
    Q_PROPERTY(int contextTokenBudget READ contextTokenBudget CONSTANT)
    Q_PROPERTY(QString contextPreview READ contextPreview NOTIFY contextBudgetChanged)
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
    // From QML: follows the App singleton's services (App.controller / App.demo).
    explicit PreTranscriptViewModel(QObject* parent = nullptr);
    // On the given services (tests, hosts); they must outlive the view model.
    explicit PreTranscriptViewModel(const UiServices& services, QObject* parent = nullptr);

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
    QString contextBudgetState() const { return m_contextBudgetState; }
    int contextTokenEstimate() const { return m_contextTokenEstimate; }
    int contextTokenBudget() const;
    QString contextPreview() const { return m_contextPreview; }
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
    void meetingIdChanged();
    void demoStateChanged();
    void contextNoteChanged();
    void contextBudgetChanged();
    void tagSuggestionsChanged();
    // Notice after a tag step (the view shows it with the shell's toast, with "Undo").
    void toast(const QString& text, bool undoable);
    void mixdownPercentChanged();
    void jobChanged();
    void changed();

private:
    void init();
    // The facade of the current services and the design state (a fictional facade serves one state).
    void resolveFacade();
    void attachFacade(tanara::processing::ITranscribeStepFacade* facade);
    bool live() const;
    void reload();
    void applySnapshot(const tanara::processing::TranscribeStepSnapshot& s);
    void applyJob(const tanara::JobProgress& job);
    void reloadTagSuggestions();
    void recomputeContextBudget();

    const UiServices* m_services = nullptr;        // given services (null: the App singleton's)
    const UiServices* m_facadeSet = nullptr;       // the set the facade came from
    QPointer<tanara::processing::ITranscribeStepFacade> m_facade;

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
    QString m_sttProviderId;
    QStringList m_sttTerms;
    QString m_contextBudgetState;
    int m_contextTokenEstimate = 0;
    QString m_contextPreview;

    bool m_cloudSoon = false;

    QString m_jobTitle;
    QVariantList m_stages;
    bool m_cancellable = true;
    bool m_cancelling = false;
    tanara::JobProgress m_job;      // for the ETA (startedAt + estimate)
    int m_fixedEta = -1;            // a fixed remaining time (design state); -1: from m_job
    QTimer m_etaTimer;

    QString m_errorMessage;
    QString m_errorDetail;
    QString m_fixActionLabel;
    QString m_fixActionPage;
};

} // namespace tanara_qml

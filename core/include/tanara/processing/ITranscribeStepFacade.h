#pragma once
//
// ITranscribeStepFacade: what the pre-transcript view's model (PreTranscriptViewModel: a meeting
// without a transcript: the transcription step, its run and its leftover error) needs, as the
// finished snapshot of what it shows plus the few actions on it. The real implementation
// (gui/qml/src/TranscribeStepServicesFacade) builds the snapshot from the stores, the task tracker
// and the gating (one shared instance); the fictional one (demo/TranscribeStepDemoFacade) serves one
// design state ("steps", "ready", "running", "failed" ...: its scenario; one instance per view
// model) for --demo, --gallery, --qml-shot and the tests.
//
// The texts of the snapshot are display-ready (the view model does not format them again), except
// the running task's phases (job), which the view model turns into its stage rows.
#include "tanara/domain/Job.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace tanara::processing {

struct TranscribeStepSnapshot {
    // "none" (no meeting) | "steps" | "running" | "failed"
    QString state = QStringLiteral("none");
    QString title;                  // the meeting's title (the title-based tag suggestion)
    QString providerLabel;          // "Soniox · own key"
    bool identifyAvailable = true;
    bool identifyEnabled = true;
    // "ready" | "missing" | "stale" | "running"
    QString mixdownState = QStringLiteral("ready");
    int mixdownPercent = -1;
    bool mixdownCancellable = false;
    bool canStart = false;
    // Empty if it can start; otherwise { title, text, actionLabel, actionPage, reason, kind }.
    QVariantMap blocker;
    bool cloudSoon = false;         // the step lacks a provider setting ("Tanara Cloud - coming soon")
    // The running transcription (state "running"): title, phases, cancel state, estimate.
    JobProgress job;
    int fixedEtaSeconds = -1;       // a fixed remaining time (s); -1: computed from `job`
    // The leftover error (state "failed").
    QString errorMessage;
    QString errorDetail;
    QString fixActionLabel;
    QString fixActionPage;

    // ---- the sub-models' fictional content (a fictional facade only) ----
    // The tag field's design state (MeetingTagsModel::setDemoState); empty: the meeting's real tags.
    QString tagsDemoState;
    // The note editor's sample content instead of the stored note.
    bool noteDemo = false;
    QString noteText;
    QString noteDetectedCallApp;
    bool noteSuggestions = false;   // the sample template suggestions
};

class ITranscribeStepFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ITranscribeStepFacade() override = default;

    // False: a fictional design state (the actions do nothing).
    virtual bool live() const = 0;

    // The meeting's snapshot (an empty id: the "none" state of a real facade).
    virtual TranscribeStepSnapshot snapshot(const QString& meetingId) const = 0;

    // ---- actions ----
    virtual void setIdentifyAfterTranscription(const QString& meetingId, bool enabled) = 0;
    // Starts the mixdown now (no need to wait for the transcription).
    virtual void regenerateMixdown(const QString& meetingId) = 0;
    // Dismisses the leftover transcription error.
    virtual void clearError(const QString& meetingId) = 0;

    // Transitional: the object the note editor model (MeetingNoteModel::setController) is handed;
    // nullptr: its fictional mode. Goes away when that model is ported to the services.
    virtual QObject* noteController() const = 0;

signals:
    // Something of the meeting's step changed (task state, mixdown, tracks, the meeting itself).
    void changed(QString meetingId);
    // Settings changed (gating, provider): every meeting's snapshot may differ.
    void settingsChanged();
    // The running transcription's progress (only the phase list refreshes; no disk read).
    void transcribeProgress(QString meetingId, tanara::JobProgress progress);
    // The standalone mixdown's progress (percent).
    void mixdownProgress(QString meetingId, int percent);
};

} // namespace tanara::processing

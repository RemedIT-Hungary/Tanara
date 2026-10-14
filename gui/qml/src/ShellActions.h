#pragma once
//
// ShellActions: the shell's action surface (gui/qml/CONTRACT.md): everything that needs a
// Widgets window, a cloud estimate / sign-in / error path, or a content switch.
//
// Content components receive it as the `shell` property and NEVER call the application
// processing starters directly: the gating runs here:
//   canRun (ReadinessModel) -> cloud blocker (sign-in / upload / refresh) ->
//   Settings on the right page -> estimate confirmation -> start.
// Steps that need Widgets go through the ShellBridge (App.bridge); QML-hosted dialogs and the
// notice bar are requested from Main.qml via signals.
//
#include "PlayerController.h"

#include "tanara/domain/Meeting.h"
#include "tanara/domain/Job.h"
#include "tanara/providers/core/ReadinessModel.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
namespace editing { class IShellFacade; }
namespace editing { class MeetingUndoService; }
}

namespace tanara_qml {

class ShellBridge;

class ShellActions : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Selected meeting ("" = none) and active tab (0 Overview, 1 Transcript,
    // 2 Executive summary, 3 Memo).
    Q_PROPERTY(QString currentMeetingId READ currentMeetingId WRITE setCurrentMeetingId
               NOTIFY currentMeetingIdChanged)
    Q_PROPERTY(int currentTab READ currentTab WRITE setCurrentTab NOTIFY currentTabChanged)
    // Main.qml's player (seekTo drives it).
    Q_PROPERTY(tanara_qml::PlayerController* player READ player WRITE setPlayer NOTIFY playerChanged)
    // Whether a recording is running (for close protection). Always false in the demo.
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    // Increases when step runnability may have changed (settings, cloud sign-in/out):
    // bound views can re-evaluate canRun.
    Q_PROPERTY(int readinessRevision READ readinessRevision NOTIFY readinessRevisionChanged)
    // Shared undo stack of the selected meeting (IShellFacade::meetingUndo ->
    // tanara::MeetingUndoService: editor, tag steps and participant actions, in time order).
    // Main's single Ctrl+Z and the change bar's "Undo" call undoMeeting. hasMeetingUndo: whether
    // such a stack exists (not without a controller / in the demo).
    Q_PROPERTY(bool hasMeetingUndo READ hasMeetingUndo NOTIFY meetingUndoChanged)
    Q_PROPERTY(bool canUndoMeeting READ canUndoMeeting NOTIFY meetingUndoChanged)
    Q_PROPERTY(QString meetingUndoText READ meetingUndoText NOTIFY meetingUndoChanged)

public:
    // From QML: follows the App singleton's services (AppContext::services().shell) and bridge.
    explicit ShellActions(QObject* parent = nullptr);
    // With a given facade (tests, hosts); it is not taken over.
    explicit ShellActions(tanara::editing::IShellFacade* facade, QObject* parent = nullptr);

    // Defaults to the App singleton's bridge; injectable in tests.
    void setBridge(ShellBridge* bridge);

    QString currentMeetingId() const { return m_currentMeetingId; }
    void setCurrentMeetingId(const QString& id);
    int currentTab() const { return m_currentTab; }
    void setCurrentTab(int index);
    PlayerController* player() const { return m_player; }
    void setPlayer(PlayerController* player);
    bool recording() const;
    int readinessRevision() const { return m_readinessRevision; }
    bool hasMeetingUndo() const { return !m_meetingUndo.isNull(); }
    bool canUndoMeeting() const;
    QString meetingUndoText() const;

    // ---- contract actions ----
    // page: "" | "providers" | "watcher" | "cloud" | "summary" | "recording";
    // focusField: "" | "stt" | "llm": deep link to the missing provider.
    Q_INVOKABLE void openSettings(const QString& page = QString(),
                                  const QString& focusField = QString());
    // person: select this person in the People window (empty: no request);
    // section: scroll to this section ("tags": the Tags section; empty: no request).
    Q_INVOKABLE void openPeople(const QString& person = QString(), const QString& section = QString());
    // The Tags window; tagId: select this tag (empty: no request).
    Q_INVOKABLE void openTags(const QString& tagId = QString());
    // The "Getting started" window (File > "Getting started...").
    Q_INVOKABLE void openOnboarding();
    Q_INVOKABLE void openRecorder();
    Q_INVOKABLE void startTranscription(const QString& meetingId);
    Q_INVOKABLE void retranscribe(const QString& meetingId);
    Q_INVOKABLE void startQuickSummary(const QString& meetingId);
    Q_INVOKABLE void startTopicExtraction(const QString& meetingId);
    Q_INVOKABLE void startTopicAnalysis(const QString& meetingId);
    Q_INVOKABLE void analyzeTopic(const QString& meetingId, const QString& topicId);
    Q_INVOKABLE void identifyParticipants(const QString& meetingId);
    // Re-check based on the confirmed / corrected lines' audio (header "..." menu, the
    // transcript's "Uncertain 0" button): confirm dialog -> RecheckService::recheckFromConfirmed ->
    // feedback; if it cannot run, it says why. "Identify participants" ends up here too once
    // every speaker has a name.
    Q_INVOKABLE void recheckSpeakers(const QString& meetingId);
    Q_INVOKABLE void cancelJob(const QString& meetingId, int jobKind);
    // "Load with larger context" (LM Studio): before the next LLM task the model is reloaded
    // with at least this many tokens of context. The caller restarts the task.
    Q_INVOKABLE void requestLlmContext(int tokens);
    Q_INVOKABLE void revealInFolder(const QString& meetingId);
    Q_INVOKABLE QString pickAudioFile();
    Q_INVOKABLE QStringList pickAudioFiles();
    // "Import audio file...": without files the native picker opens first (nothing happens on
    // cancel); with given files (e.g. dropped on the window) the dialog opens right away.
    // While an import runs, the progress window appears instead.
    Q_INVOKABLE void openImport(const QVariantList& files = {});
    // Meeting archive (*.tanara.zip, see library/store/MeetingArchive.h).
    // "Export to archive...": native save dialog (default "<folder name>.tanara.zip" in the last
    // used folder, or ~/Tanara), then runs in the background: progress in the task bar, and a
    // toast with a "Show in folder" button at the end.
    Q_INVOKABLE void exportArchive(const QString& meetingId);
    // "Import meeting from archive...": with an empty path the native picker opens first; at the
    // end the new meeting is selected + toast; errors show in a toast. (A dropped .zip lands here too.)
    Q_INVOKABLE void importArchive(const QVariant& pathOrUrl = QVariant());
    // "Import meeting from folder...": a copied recording folder (no zip); empty path opens the
    // native folder picker. Ends like the archive import (selection + toast / error toast).
    Q_INVOKABLE void importFolder(const QVariant& pathOrUrl = QVariant());
    // Whether it looks like an archive (to sort dropped files): *.zip.
    Q_INVOKABLE bool isArchiveFile(const QVariant& pathOrUrl) const;
    // Whether it is a folder (to sort dropped items): a dropped meeting folder is importable.
    Q_INVOKABLE bool isDirectory(const QVariant& pathOrUrl) const;
    // Opens a file's folder in the file manager (the toast's "Show in folder" button).
    Q_INVOKABLE void revealFile(const QString& path);
    Q_INVOKABLE bool confirm(const QString& title, const QString& text,
                             const QString& confirmLabel, bool danger);
    Q_INVOKABLE void showMeeting(const QString& meetingId);
    Q_INVOKABLE void showTab(int index);
    Q_INVOKABLE void seekTo(const QString& meetingId, int ms);
    // undoKey: non-empty -> the toast gets an "Undo" button that calls undoFromToast(undoKey)
    // (the key's owner, e.g. "tags", undoes on the undoRequested signal).
    Q_INVOKABLE void toast(const QString& text, const QString& undoKey = QString());

    // ---- the shell's own actions (beyond the contract) ----
    // Data for the re-transcribe confirm dialog: { title, text, corrections, any }.
    Q_INVOKABLE QVariantMap retranscribeImpact(const QString& meetingId) const;
    // The confirm dialog's "Re-transcribe" button: gating + estimate + start.
    Q_INVOKABLE void confirmRetranscribe(const QString& meetingId, bool keepBackup);
    // Delete: asks for a confirm dialog (deleteDialogRequested); deleteMeeting after confirmation.
    Q_INVOKABLE void requestDelete(const QString& meetingId);
    Q_INVOKABLE void deleteMeeting(const QString& meetingId);
    Q_INVOKABLE void renameMeeting(const QString& meetingId, const QString& title);
    // Edit the header title in place (also from the library context menu).
    Q_INVOKABLE void requestRename(const QString& meetingId);
    Q_INVOKABLE void stopRecording();
    // Brings the main window to the front (e.g. the recorder's "Open in analyzer" button:
    // showMeeting(id) + activateWindow()).
    Q_INVOKABLE void activateWindow();
    // The answer of confirm()'s QML dialog.
    Q_INVOKABLE void resolveConfirm(bool accepted);
    // Last result of the pre-transcript (audio-based) participant hint for this meeting.
    Q_INVOKABLE QString participantsGuess(const QString& meetingId) const;
    // "Who was there?": opens the approval dialog (participantsDialogRequested; the
    // ParticipantsDialog opens by itself). Called by the banner, toolbar, menu and editor.
    Q_INVOKABLE void openParticipants(const QString& meetingId);
    // On switching to the Transcript tab: if the meeting awaits approval and was not yet offered
    // in this session (nor opened), opens the dialog. True if it opened.
    Q_INVOKABLE bool maybeOfferParticipants(const QString& meetingId);
    // From the Overview participant badge, the editor's "Why this one?" panel for a speaker:
    // selects the meeting, the Transcript tab, then speakerWhyRequested (Main -> TranscriptTab).
    Q_INVOKABLE void openSpeakerWhy(const QString& meetingId, const QString& speakerKey);
    // The toast's "Undo" button: undoRequested(undoKey). The "meeting" key is the shared stack
    // (undoMeeting), which the shell handles itself, without a signal.
    Q_INVOKABLE void undoFromToast(const QString& undoKey);
    // Undoes the newest step of the shared stack (Ctrl+Z, change bar). False: nothing to undo.
    // Afterwards meetingChangeUndone(text, source).
    Q_INVOKABLE bool undoMeeting();
    // Filters the library to a tag (header chip, Tags window): tagFilterRequested.
    Q_INVOKABLE void filterByTag(const QString& tagId);
    // Whether the meeting has a transcript / exists (quick QML queries).
    Q_INVOKABLE bool meetingExists(const QString& meetingId) const;

signals:
    void currentMeetingIdChanged();
    void currentTabChanged();
    void playerChanged();
    void recordingChanged();
    void readinessRevisionChanged();
    // The shared stack was replaced (another meeting) or changed state (canUndo / undoText).
    void meetingUndoChanged();
    // New step on top of the shared stack (merged within one event-loop turn): the change bar /
    // toast message "<text> - Undo". source: tanara::MeetingUndoService::Source
    // (0 Speakers: editor, 1 Tags, 2 Participants: participant actions).
    void meetingChangePosted(const QString& text, int source);
    // One step of the shared stack was undone (any path): "Undone: <text>".
    void meetingChangeUndone(const QString& text, int source);

    // The editor scrolls to the utterance at the given time (contract).
    void transcriptPositionRequested(int ms);

    // ---- for Main.qml ----
    // tone: "" (neutral) | "danger"; requestId / usageLink: for cloud notices.
    // undoKey: see toast(). revealPath: non-empty -> "Show in folder" button (revealFile).
    void toastRequested(const QString& text, const QString& tone, const QString& requestId,
                        bool usageLink, const QString& undoKey = QString(),
                        const QString& revealPath = QString());
    void undoRequested(const QString& undoKey);
    // Main.qml puts the tag filter on the library model.
    void tagFilterRequested(const QString& tagId);
    void confirmRequested(const QString& title, const QString& text,
                          const QString& confirmLabel, bool danger);
    void retranscribeDialogRequested(const QString& meetingId);
    void deleteDialogRequested(const QString& meetingId, const QString& title);
    void renameRequested(const QString& meetingId);
    void importDialogRequested(const QVariantList& files);
    void windowActivationRequested();
    // The pre-transcript participant hint is ready (PreTranscriptView can show it).
    void participantsGuessed(const QString& meetingId, const QString& summary);
    // Request for the "Who was there?" dialog (ParticipantsDialog.openFor).
    void participantsDialogRequested(const QString& meetingId);
    // Request for the "Why this one?" panel for a speaker (openSpeakerWhy; Transcript tab is visible).
    void speakerWhyRequested(const QString& meetingId, const QString& speakerKey);

private:
    void attachFacade();
    void attachBridge();
    ShellBridge* bridge() const;
    tanara::Meeting meeting(const QString& meetingId) const;
    // Step runnability; if it cannot run, leads to the right remedy. True = can start.
    bool gate(tanara::providers::WorkflowStep step, const QString& meetingId);
    bool estimateOk(const QString& meetingId, const QString& task, const QString& mode);
    void bumpReadiness();
    void onRetry(const QString& meetingId, const QString& kind);
    void onJobFinished(const QString& meetingId, tanara::JobKind kind, tanara::JobOutcome outcome);
    QString speakerSummary(const QString& meetingId) const;
    void runRecheck(const QString& meetingId, const QString& title, const QString& text);
    // (Re)attaches the selected meeting's shared stack; its current content is not a "new step".
    void attachMeetingUndo();
    void checkMeetingUndo();

    QPointer<tanara::editing::IShellFacade> m_facade;
    QPointer<tanara::editing::IShellFacade> m_attached;
    QPointer<ShellBridge> m_bridge;
    QPointer<ShellBridge> m_attachedBridge;
    bool m_injected = false;
    bool m_bridgeInjected = false;
    QPointer<PlayerController> m_player;

    QString m_currentMeetingId;
    int m_currentTab = 0;
    int m_readinessRevision = 0;
    QSet<QString> m_identifyRequested;              // the user requested identification
    QHash<QString, QString> m_participantGuesses;   // meetingId -> summary sentence (session)
    QSet<QString> m_participantsOffered;            // "Who was there?" already opened (session)
    // Message of the task error just recorded and VISIBLE in the open meeting's error bar
    // (errorOccurred arriving in the same event turn does not make a second toast).
    QString m_errorInBanner;
    QString m_lastArchiveDir;   // target folder of the last export (within the session)

    // Shared undo: the watched stack and its last seen state (to detect a new step: more
    // entries, or the same count with a different newest text).
    QPointer<tanara::editing::MeetingUndoService> m_meetingUndo;
    QList<QMetaObject::Connection> m_meetingUndoConns;
    QMetaObject::Connection m_undoStoreConn;
    int m_undoSeenCount = 0;
    QString m_undoSeenTop;
    bool m_undoCheckPending = false;

    // confirm(): nested event loop until the QML dialog answers.
    class QEventLoop* m_confirmLoop = nullptr;
    bool m_confirmResult = false;
};

} // namespace tanara_qml

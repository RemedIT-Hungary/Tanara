#pragma once
//
// ShellBridge: what the QML shell asks of the Qt Widgets world (App.bridge).
//
// The implementation lives in gui/src (QmlShellBridge: it knows the Settings / People /
// recorder / Tanara Cloud Widgets classes) and main.cpp installs it:
//   AppContext::instance()->setBridge(bridge)
// Only the abstract interface is here, so ShellActions (and tests with a fake bridge) can
// be used without linking Widgets. Without a bridge (demo, screenshot) ShellActions skips
// these actions.
//
#include "tanara/provider/ReadinessModel.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace tanara_qml {

class ShellBridge : public QObject {
    Q_OBJECT

    // ---- Tanara Cloud chrome: balance chip + bars ----
    Q_PROPERTY(bool cloudChipVisible READ cloudChipVisible NOTIFY cloudChromeChanged)
    Q_PROPERTY(QString cloudChipText READ cloudChipText NOTIFY cloudChromeChanged)
    // "normal" | "warn" (low balance) | "danger" (used up)
    Q_PROPERTY(QString cloudChipTone READ cloudChipTone NOTIFY cloudChromeChanged)
    Q_PROPERTY(QString cloudChipToolTip READ cloudChipToolTip NOTIFY cloudChromeChanged)
    // [{ key, level: "critical"|"warning"|"info", text, cta, closable }]
    Q_PROPERTY(QVariantList cloudBanners READ cloudBanners NOTIFY cloudChromeChanged)

public:
    using QObject::QObject;

    virtual bool cloudChipVisible() const = 0;
    virtual QString cloudChipText() const = 0;
    virtual QString cloudChipTone() const = 0;
    virtual QString cloudChipToolTip() const = 0;
    virtual QVariantList cloudBanners() const = 0;

    // ---- Widgets dialogs ----
    // page: "" | "providers" | "watcher" | "cloud" | "summary" | "recording"
    Q_INVOKABLE virtual void openSettings(const QString& page) = 0;
    // The same with a deep link: focusField "stt" | "llm": the missing provider's card is
    // highlighted, after a successful save back to the meeting. The default is plain
    // openSettings (tests' fake bridges need not know about it).
    Q_INVOKABLE virtual void openSettingsAt(const QString& page, const QString& focusField)
    {
        Q_UNUSED(focusField);
        openSettings(page);
    }
    Q_INVOKABLE virtual void openPeople() = 0;
    // The same with a person selected (empty name: the earlier selection stays). The default is
    // plain openPeople (tests' fake bridges need not know about it).
    Q_INVOKABLE virtual void openPeopleAt(const QString& person)
    {
        Q_UNUSED(person);
        openPeople();
    }
    // The same scrolled to a section (section: "tags": the "Tags" fix link). The default is
    // plain openPeopleAt.
    Q_INVOKABLE virtual void openPeopleAtSection(const QString& person, const QString& section)
    {
        Q_UNUSED(section);
        openPeopleAt(person);
    }
    // The Tags window (TagsWindow.qml); tagId: select this tag (empty: the earlier selection
    // stays). The default does nothing (tests' fake bridges need not know about it).
    Q_INVOKABLE virtual void openTags() {}
    Q_INVOKABLE virtual void openTagsAt(const QString& tagId)
    {
        Q_UNUSED(tagId);
        openTags();
    }
    // The "Getting started" window (OnboardingWindow.qml): manual reopen (File menu). The
    // default does nothing (tests' fake bridges need not know about it).
    Q_INVOKABLE virtual void openOnboarding() {}
    Q_INVOKABLE virtual void openRecorder() = 0;
    // Native file picker for an audio file; empty if the user cancelled.
    Q_INVOKABLE virtual QString pickAudioFile() = 0;
    // The same for several files (import): audio and video files; empty list = cancelled.
    Q_INVOKABLE virtual QStringList pickAudioFiles() = 0;
    // Native save dialog (proposedPath: suggested folder + file name; filter: QFileDialog filter);
    // empty if cancelled. The default is empty (tests' fake bridges need not know about it).
    Q_INVOKABLE virtual QString pickSaveFile(const QString& title, const QString& proposedPath,
                                             const QString& filter)
    {
        Q_UNUSED(title);
        Q_UNUSED(proposedPath);
        Q_UNUSED(filter);
        return {};
    }
    // Pick a meeting archive (*.tanara.zip / *.zip) to import; empty = cancelled.
    Q_INVOKABLE virtual QString pickArchiveFile() { return {}; }
    // Pick a meeting folder (track_*.ogg + meeting.json) to import; empty = cancelled.
    Q_INVOKABLE virtual QString pickMeetingFolder() { return {}; }

    // ---- Tanara Cloud gates (called by ShellActions before starting processing) ----
    // The cloud remedy of a blocked step (sign-in / upload / refresh). True = the bridge
    // handled it (the caller must not open Settings). False for a non-cloud blocker.
    virtual bool handleCloudBlocker(const tanara::ReadinessResult& blocker) = 0;
    // Cost estimate + confirmation before a cloud run. True = can start. For an own-key (BYO)
    // step, true immediately. task: transcribe | summarize; mode: "" | quick | complex.
    virtual bool confirmCloudEstimate(const QString& meetingId, const QString& task,
                                      const QString& mode) = 0;

    // ---- Participant guessing BEFORE THE TRANSCRIPT (audio clusters + voiceprint DB) ----
    // Runs with a modal, cancelable progress window. Returns a human summary sentence; on
    // cancel it is empty and *cancelled = true.
    virtual QString identifyParticipantsPreview(const QString& meetingId, bool* cancelled) = 0;

    // ---- track actions ----
    Q_INVOKABLE virtual void cloudBannerAction(const QString& key) = 0;
    Q_INVOKABLE virtual void cloudBannerDismiss(const QString& key) = 0;

    // ---- recording and window lifecycle ----
    // Opens the floating recorder, and the main window can be hidden (recording runs in the background).
    Q_INVOKABLE virtual void continueRecordingInBackground() = 0;
    // Stops the recording; when encoding is done (Idle), quitRequested() arrives.
    Q_INVOKABLE virtual void stopRecordingAndQuit() = 0;
    // Before the main window really closes: closes the recorder window, singleton, level monitoring.
    Q_INVOKABLE virtual void shutdown() = 0;
    // The main window became visible (first time: cloud startup checks).
    Q_INVOKABLE virtual void windowShown() = 0;
    // The main window became active again (refreshes the cloud balance after a top-up).
    Q_INVOKABLE virtual void windowActivated() = 0;

signals:
    void cloudChromeChanged();
    // Non-modal notice (cost, write-back ...). requestId: copyable error id; usageLink: "Log on
    // the web" link (see openUsageLog).
    void toastRequested(const QString& text, const QString& requestId, bool usageLink);
    // "Retry" / "Resume" in a cloud error window: the same step must be restarted.
    // kind: transcribe | summary | topics | complex.
    void retryRequested(const QString& meetingId, const QString& kind);
    // Something changed that affects steps' runnability (settings, sign-in/out, models);
    // views should re-evaluate canRun.
    void readinessChanged();
    // The theme saved in Settings ("system" | "light" | "dark"): the main window remembers it
    // (ui-state.json) so it applies on the next start too.
    void themeModeSaved(const QString& mode);
    // During recording the call ended / there is silence: the shell asks "End of the meeting?".
    // With the QML recorder the bridge does NOT send it: the recorder's own box asks, so the
    // user is asked once. (It stays for a bridge without a recorder.)
    void stopPromptRequested(const QString& reason);
    // A meeting must be selected and the main window brought to the front (the recorder's "Open
    // in analyzer" button, `tanara --meeting <id>`). Empty id: only the window comes forward.
    void showMeetingRequested(const QString& meetingId);
    // From the Tags window: filter the library to this tag, main window to the front
    // ("Show in library as a filter").
    void tagFilterRequested(const QString& tagId);
    // The main window must be shown again (background recording ended, recorder closed).
    void showWindowRequested();
    // After "Stop and quit" the recording closed: the main window can be closed.
    void quitRequested();

public:
    Q_INVOKABLE virtual void openUsageLog() = 0;
};

} // namespace tanara_qml

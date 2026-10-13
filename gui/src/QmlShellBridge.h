#pragma once
// QmlShellBridge: the bridge between the (Qt Quick) main window and the Qt Widgets world (App.bridge).
//
// Behavior tied to Widgets or a separate window lives here:
//  - Settings (QML: tanara_qml::SettingsWindowHost, a non-modal separate window; the native
//    folder picker comes from SettingsWidgetsDialogs), the
//    People window (QML: tanara_qml::PeopleWindowHost), the Tags window (QML:
//    tanara_qml::TagsWindowHost; its filter and meeting requests go to the shell), and the
//    "Getting started" window (QML: tanara_qml::OnboardingWindowHost; opens once by itself on
//    first launch, after the main window appears; by hand from the File menu and Settings);
//  - opening the recorder and the forwarded `tanara --record` requests, through
//    ShellRecorderHost (which wraps the QML recorder), the recorder's requests (open in the
//    analyzer, recording settings), and the window handling at the end of a recording
//    (bringing the hidden main window back, "Stop and quit");
// The QML side reaches it through the tanara_qml::ShellBridge interface (gui/qml/src/ShellBridge.h).
#include "ShellBridge.h"

#include <QPointer>
#include <QStringList>

class QWindow;
class QWidget;

namespace tanara {
namespace app { class AppController; }
}

namespace tanara_qml {
class SettingsWindowHost;
class PeopleWindowHost;
class TagsWindowHost;
class OnboardingWindowHost;
}

namespace tanara_gui {

class ShellRecorderHost;
class SettingsWidgetsDialogs;

class QmlShellBridge : public tanara_qml::ShellBridge {
    Q_OBJECT
public:
    explicit QmlShellBridge(tanara::app::AppController* controller, QObject* parent = nullptr);
    ~QmlShellBridge() override;

    // The QML main window: the Widgets windows become its children (as the window manager sees
    // it), and from it we know whether the main window is currently visible.
    void setMainWindow(QWindow* window);
    // Receives the forwarded `tanara --record` requests (called by main.cpp; not in QA mode).
    void startRecorderListening();
    // Selects a meeting + raises the main window: the recorder's "Open in the analyzer" button
    // and `tanara --meeting <id>` (passed at startup / from another process). With an empty id
    // only the main window comes forward.
    void showMeeting(const QString& meetingId);
    // The recorder window (RecorderWindow.qml: .visible, .vm ...); nullptr until it has opened.
    // For QA scripts (--shell-script): App.bridge.recorderWindow().
    Q_INVOKABLE QObject* recorderWindow() const;
    // The Settings window (SettingsWindow.qml: .visible, .vm ...); nullptr until it has opened.
    // For QA scripts: App.bridge.settingsWindow().
    Q_INVOKABLE QObject* settingsWindow() const;
    // The People window (PeopleWindow.qml: .visible, .vm ...); nullptr until it has opened.
    // For QA scripts: App.bridge.peopleWindow().
    Q_INVOKABLE QObject* peopleWindow() const;
    // The Tags window (TagsWindow.qml: .visible, .vm ...); nullptr until it has opened.
    // For QA scripts: App.bridge.tagsWindow().
    Q_INVOKABLE QObject* tagsWindow() const;
    // The "Getting started" window (OnboardingWindow.qml: .visible, .vm ...); nullptr until it
    // has opened. For QA scripts: App.bridge.onboardingWindow().
    Q_INVOKABLE QObject* onboardingWindow() const;
    // False: first launch does not open it by itself (QA script mode). True by default.
    void setAutoOnboarding(bool on) { m_autoOnboarding = on; }

    // ---- ShellBridge ----
    void openSettings(const QString& page) override;
    void openSettingsAt(const QString& page, const QString& focusField) override;
    void openPeople() override;
    void openPeopleAt(const QString& person) override;
    void openPeopleAtSection(const QString& person, const QString& section) override;
    void openTags() override;
    void openTagsAt(const QString& tagId) override;
    void openOnboarding() override;
    void openRecorder() override;
    QString pickAudioFile() override;
    QStringList pickAudioFiles() override;
    QString pickSaveFile(const QString& title, const QString& proposedPath, const QString& filter) override;
    QString pickArchiveFile() override;
    QString pickMeetingFolder() override;
    QString identifyParticipantsPreview(const QString& meetingId, bool* cancelled) override;
    void continueRecordingInBackground() override;
    void stopRecordingAndQuit() override;
    void shutdown() override;
    void windowShown() override;

protected:
    // Parents the parentless Widgets dialogs to the QML main window (transient parent).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool mainWindowHidden() const;

    void maybeOpenOnboarding();

    tanara::app::AppController* m_controller = nullptr;
    QPointer<QWindow> m_window;

    ShellRecorderHost* m_recorder = nullptr;   // the recorder binding
    SettingsWidgetsDialogs* m_settingsDialogs = nullptr;
    tanara_qml::SettingsWindowHost* m_settings = nullptr;   // host of the QML Settings window
    tanara_qml::PeopleWindowHost* m_people = nullptr;       // host of the QML People window
    tanara_qml::TagsWindowHost* m_tags = nullptr;           // host of the QML Tags window
    tanara_qml::OnboardingWindowHost* m_onboarding = nullptr;   // host of "Getting started"
    bool m_autoOnboarding = true;
    bool m_quitAfterStop = false;
    bool m_shutDown = false;

    bool m_startupDone = false;
};

} // namespace tanara_gui

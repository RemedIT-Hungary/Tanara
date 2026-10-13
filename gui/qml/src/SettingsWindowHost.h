#pragma once
//
// SettingsWindowHost: the C++ host of the QML Settings window (SettingsWindow.qml).
//
// ONE instance per process is enough; it lives in three places:
//  - in the main application QmlShellBridge creates it (ShellActions.openSettings -> open());
//  - in the standalone recorder (`tanara --record`, "Recording settings");
//  - in `tanara --settings [page]` mode (the tray watcher's "Settings..." menu item), if no
//    main window runs to pass the request to.
// It uses its own QML engine (like the recorder's host); the theme is shared through the `App` singleton.
// It has no Widgets dependency: the native folder picker and Tanara Cloud's Widgets windows
// are requested through the SettingsDialogs interface (gui/src: SettingsWidgetsDialogs).
//
// The window is non-modal: the main window can be used beside it. On closing with unsaved
// changes it asks (Save / Discard / Cancel); QML handles that.
//
#include <QObject>
#include <QPointer>
#include <QString>

class QQmlEngine;
class QQuickWindow;
class QWindow;

namespace tanara {
class AppController;
}

namespace tanara_qml {

class SettingsDialogs;
class SettingsViewModel;

class SettingsWindowHost : public QObject {
    Q_OBJECT
public:
    SettingsWindowHost(tanara::AppController* controller, SettingsDialogs* dialogs,
                       QObject* parent = nullptr);
    ~SettingsWindowHost() override;

    // Opens / raises the window on the given page. page: "" | "general" | "recording" |
    // "watcher" | "providers" | "cloud" | "speakers" | "summary"; focusField: "" | "stt" | "llm".
    // If the window was closed, it reloads the settings. False: QML error.
    bool open(const QString& page = QString(), const QString& focusField = QString());
    // Closes without asking (when the process shuts down); the unsaved draft is lost.
    void closeNow();
    bool isVisible() const;
    QQuickWindow* window() const;
    SettingsViewModel* viewModel() const;

    // The window should belong to this one per the window manager (stays above it, aligns to it).
    void setTransientParent(QWindow* parent);
    // False: the caller handles saving the theme on the themeModeSaved signal (the main window's
    // ui-state); true (default): the host writes it to <meta folder>/ui-state.json itself.
    void setPersistTheme(bool on) { m_persistTheme = on; }

signals:
    void saved();                           // the settings were saved
    void themeModeSaved(const QString& mode);
    void returnRequested();                 // back to the meeting (the window has already closed)
    void closed();                          // the window closed (with or without saving)

private:
    bool ensureWindow();

    QPointer<tanara::AppController> m_controller;
    QPointer<SettingsDialogs> m_dialogs;
    QQmlEngine* m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    QPointer<SettingsViewModel> m_vm;
    QPointer<QWindow> m_transientParent;
    bool m_persistTheme = true;
};

} // namespace tanara_qml

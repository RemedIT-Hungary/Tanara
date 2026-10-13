#pragma once
//
// RecorderWindowHost: the C++ host of the floating QML recorder (RecorderWindow.qml).
//
// ONE instance per process is enough. It can be used two ways:
//  - standalone (`tanara --record ...`): main.cpp creates it, passes the command-line request
//    with request(), and quits on the closed() signal;
//  - in the main application: gui/src's ShellRecorderHost creates it the same way (with its
//    own engine there too; the theme is shared through the `App` singleton), calls show() on
//    the "Record" button and request() on a forwarded `--record` request.
//
// The host handles what QML cannot: always-on-top, remembering the position per screen,
// edge snapping of the pill, sending to the background, the recording.lock, and that the
// recording question (R06) brings up the window and asks for a system notification when the
// window is hidden / a pill. It has no Widgets dependency (the tray icon and the
// notification come from the enclosing process on the signals).
//
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QQmlEngine;
class QQuickWindow;

namespace tanara {
namespace app { class AppController; }
class ProcessLock;
}

namespace tanara_qml {

class RecorderViewModel;

// A recorder request (command line: --record [--title T | --app A] [--context C]
// [--device IDX]... [--no-start] [--stop]).
struct RecorderRequest {
    QString title;              // explicit title (not the "automatic name")
    QString appName;            // the detected call app -> automatic name: "Teams call · ..."
    QString context;            // the meeting's context note
    QList<int> deviceIndexes;   // the capture list's row numbers; empty = the saved selection
    bool start = true;          // start immediately (if idle)
    bool stop = false;          // stop the running recording (without bringing up the window)
};

class RecorderWindowHost : public QObject {
    Q_OBJECT
public:
    // engine: the QML engine the window is created in; nullptr -> it makes its own engine.
    explicit RecorderWindowHost(tanara::app::AppController* controller, QQmlEngine* engine = nullptr,
                                QObject* parent = nullptr);
    ~RecorderWindowHost() override;

    // Shows the window and brings it to the front (creating it on the first call). False: QML error.
    bool show();
    void hide();
    bool isVisible() const;

    // Executes a request: shows, sets the title / sources, and (start) starts.
    void request(const RecorderRequest& request);

    bool recording() const;                 // whether a recording runs (or is closing)
    QQuickWindow* window() const;           // nullptr until created
    RecorderViewModel* viewModel() const;

    // False: the "To background" button only minimizes to the taskbar (nothing would bring the window back).
    void setHideToTrayEnabled(bool on) { m_hideEnabled = on; }
    // Handling of recording.lock (taken when a recording starts, released at the end). On by default.
    void setManageLock(bool on) { m_manageLock = on; }

    // In the standalone recorder process, to call BEFORE the QApplication: Qt Quick settings
    // (style, text rendering) + the TANARA_RECORDER_X11=1 switch (Wayland -> XWayland).
    static void prepareProcess();

    // Whether the platform lets the client do always-on-top and window positioning (X11,
    // Windows: yes; Wayland: no, there the window manager decides).
    static bool platformCanPosition();

signals:
    void closed();                               // the user closed it (no recording running)
    void hiddenToTray();                         // sent to the background (the recording goes on)
    void shown();
    void openMeetingRequested(QString meetingId);   // "Open in analyzer"
    void settingsRequested();                    // "Recording settings"
    void notificationRequested(QString title, QString text);   // R06 for a hidden / pill window
    void recordingStarted(QString meetingFolder);
    void recordingFinished(QString meetingId);
    void stateChanged(QString state);            // the view model's state (for the tray icon)

private slots:
    // RecorderWindow.qml's signals / property changes.
    void onHideRequested();
    void onCloseRequested();
    void onUserMoved();
    void onPillChanged();
    void onPrefsChanged();

private:
    bool ensureWindow();
    void restoreGeometry();
    void savePosition();
    void snapPill();
    QString iniPath() const;

    QPointer<tanara::app::AppController> m_controller;
    QQmlEngine* m_engine = nullptr;
    bool m_ownEngine = false;
    QPointer<QQuickWindow> m_window;
    QPointer<RecorderViewModel> m_vm;
    std::unique_ptr<tanara::ProcessLock> m_lock;
    bool m_hideEnabled = true;
    bool m_manageLock = true;
    bool m_restoring = false;
    bool m_positioned = false;
};

} // namespace tanara_qml

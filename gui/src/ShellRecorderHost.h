#pragma once
// ShellRecorderHost: ALL the new main window's recorder bindings in one place.
//
// It wraps the new QML recorder (tanara_qml::RecorderWindowHost: RecorderWindow.qml +
// RecorderViewModel) and RecorderSingleton (the forwarded `tanara --record ...` requests).
// QmlShellBridge knows only this interface:
//   open()            - raises the recorder / brings it to the front (ShellActions.openRecorder)
//   startListening()  - listens on the singleton socket (if another live instance holds it, it
//                       does not listen and does NOT take it over)
//   shutdown()        - before quitting: closes the recorder window, singleton, level monitoring
//   hidden()          - the recorder window disappeared (sent to background or closed); the
//                       bridge decides whether to bring back the hidden main window
// The recorder gets its OWN QML engine (as in the standalone `tanara --record` process), so
// its lifetime does not depend on the main window's engine. The theme is shared, because the
// `App` singleton is process-wide (AppContext::instance()): both engines' Theme follow the same.
//
// What the recorder handles itself (not needed here again): recording.lock (taken when a
// recording starts, released at the end), the forwarded --context on the meeting, renaming
// during a recording, the "End of the meeting?" question, and closing during a recording.
// Automatic mixdown stays ON in the analyzer (AppController default); only the standalone
// recorder process turns it off.
#include <QObject>
#include <QStringList>

namespace tanara { class AppController; }
namespace tanara_qml {
class RecorderWindowHost;
struct RecorderRequest;
}

namespace tanara_gui {

class RecorderSingleton;
struct RecorderArgs;

// The `--record` arguments to the QML recorder request (main.cpp recorder mode + forwarded request).
tanara_qml::RecorderRequest toRecorderRequest(const RecorderArgs& args);

class ShellRecorderHost : public QObject {
    Q_OBJECT
public:
    explicit ShellRecorderHost(tanara::AppController* controller, QObject* parent = nullptr);
    ~ShellRecorderHost() override;

    void open();
    void startListening();
    void retryListening();          // after a recording ends: if someone else held the name until now
    void refreshFromSettings();     // after Settings is accepted
    void shutdown();

    bool isVisible() const;
    // The recorder window (RecorderWindow.qml; nullptr until it has opened), for QA scripts.
    QObject* window() const;

signals:
    // The recorder window disappeared (sent to the background or closed).
    void hidden();
    void openMeetingRequested(const QString& meetingId);   // "Open in the analyzer"
    void settingsRequested();                              // "Recording settings"

private:
    void ensureHost();
    void handleRequest(const QStringList& args);
    void releaseMonitorIfUnused();

    tanara::AppController* m_controller = nullptr;
    tanara_qml::RecorderWindowHost* m_host = nullptr;
    RecorderSingleton* m_singleton = nullptr;
    bool m_shutDown = false;
};

} // namespace tanara_gui

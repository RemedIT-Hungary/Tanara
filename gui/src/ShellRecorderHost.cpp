#include "ShellRecorderHost.h"

#include "RecorderSingleton.h"
#include "RecorderViewModel.h"
#include "RecorderWindowHost.h"

#include "tanara/AppController.h"
#include "tanara/Logging.h"

#include <QQuickWindow>
#include <QTimer>

namespace tanara_gui {

tanara_qml::RecorderRequest toRecorderRequest(const RecorderArgs& ra)
{
    tanara_qml::RecorderRequest r;
    r.title = ra.title;
    r.appName = ra.app;
    r.context = ra.context;
    r.deviceIndexes = ra.deviceIdx;
    r.start = !ra.noStart;
    r.stop = ra.stop;
    return r;
}

ShellRecorderHost::ShellRecorderHost(tanara::AppController* controller, QObject* parent)
    : QObject(parent), m_controller(controller)
{
    // Recording ended with a hidden recorder (e.g. stopped from the tray): core restarts level
    // monitoring at the end (the recorder asked for it); if nobody is watching, we release the
    // microphone. Queued, because the restart happens AFTER the signal.
    connect(m_controller, &tanara::AppController::recordingStateChanged, this,
            [this](tanara::RecordingState st) {
                if (st == tanara::RecordingState::Idle)
                    QTimer::singleShot(0, this, &ShellRecorderHost::releaseMonitorIfUnused);
            });
}

ShellRecorderHost::~ShellRecorderHost()
{
    shutdown();
}

void ShellRecorderHost::startListening()
{
    // Singleton recorder: while the analyzer runs, EVERY `tanara --record` request comes here
    // (the watcher / tray never opens a second recorder). If another live instance holds the
    // name, that one stays the recorder and we do not listen (and do not take it from it).
    if (!m_singleton) {
        m_singleton = new RecorderSingleton(this);
        connect(m_singleton, &RecorderSingleton::requestReceived,
                this, &ShellRecorderHost::handleRequest);
    }
    m_singleton->listen();
}

void ShellRecorderHost::retryListening()
{
    if (m_singleton && !m_singleton->isListening())
        m_singleton->listen();
}

void ShellRecorderHost::ensureHost()
{
    if (m_host)
        return;
    // Own QML engine (nullptr): the recorder's lifetime must not depend on the main window's
    // engine. The host manages recording.lock (default); the watcher must see a recording that
    // runs in the analyzer too. "Send to background" hides the window: the main window's
    // "Recording in progress" button (or the watcher's tray icon) brings it back.
    m_host = new tanara_qml::RecorderWindowHost(m_controller, nullptr, this);
    m_host->setManageLock(true);
    m_host->setHideToTrayEnabled(true);
    using Host = tanara_qml::RecorderWindowHost;
    connect(m_host, &Host::hiddenToTray, this, &ShellRecorderHost::hidden);
    connect(m_host, &Host::closed, this, [this] {
        // Closed (no recording running): do not hold the microphone while the recorder is not
        // visible; level monitoring restarts on reopen.
        releaseMonitorIfUnused();
        emit hidden();
    });
    connect(m_host, &Host::openMeetingRequested, this, &ShellRecorderHost::openMeetingRequested);
    connect(m_host, &Host::settingsRequested, this, &ShellRecorderHost::settingsRequested);
    // notificationRequested (hidden / pill window): the host raises the recorder itself, and the
    // question appears there; the main window does NOT ask again.
    // recordingFinished: the library refreshes and selects on AppController::recordingFinished
    // (ShellActions); there is no second handling here.
}

void ShellRecorderHost::open()
{
    if (m_shutDown)
        return;
    ensureHost();
    // Instead of the "Saved" state of an earlier recording (that ended with a hidden window),
    // the reopened recorder is ready for a new recording.
    if (!m_host->isVisible() && m_host->viewModel()
        && m_host->viewModel()->state() == QLatin1String("done"))
        m_host->viewModel()->newRecording();
    if (!m_host->show()) {
        qCCritical(tanara::lcApp) << "A felvevő felülete nem tölthető be.";
        return;
    }
    // When the recorder is hidden we stop level monitoring (do not hold the microphone while we
    // are just looking back); on reopen, when idle, it restarts.
    if (m_controller->recordingState() == tanara::RecordingState::Idle)
        m_controller->startLevelMonitoring();
}

void ShellRecorderHost::handleRequest(const QStringList& args)
{
    if (m_shutDown)
        return;
    const RecorderArgs ra = parseRecorderArgs(args);
    if (ra.stop) {
        // Tray menu "Stop recording": without raising the window.
        if (m_host)
            m_host->request(toRecorderRequest(ra));
        return;
    }
    open();   // raise / bring to front: always the SAME recorder
    if (m_host && m_host->isVisible())
        m_host->request(toRecorderRequest(ra));   // title / app / context / sources / start
}

void ShellRecorderHost::releaseMonitorIfUnused()
{
    if (m_shutDown || isVisible())
        return;
    if (m_controller->recordingState() == tanara::RecordingState::Idle)
        m_controller->stopLevelMonitoring();
}

void ShellRecorderHost::refreshFromSettings()
{
    // The recorder's device list reflects the new device policy (devicesChanged triggers a rebuild).
    if (m_host)
        m_controller->refreshDevices();
}

bool ShellRecorderHost::isVisible() const
{
    return m_host && m_host->isVisible();
}

QObject* ShellRecorderHost::window() const
{
    return m_host ? m_host->window() : nullptr;
}

void ShellRecorderHost::shutdown()
{
    if (m_shutDown)
        return;
    m_shutDown = true;
    if (m_singleton)
        m_singleton->close();
    // We close the recorder window (standalone top-level) by hand, otherwise it would stay up
    // after the main window closes. The host's destructor also releases recording.lock.
    if (m_host) {
        m_host->disconnect(this);
        delete m_host;
        m_host = nullptr;
    }
    // Make sure the capture devices are released on exit.
    m_controller->stopLevelMonitoring();
}

} // namespace tanara_gui

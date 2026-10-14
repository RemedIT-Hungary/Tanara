#include "TrayWatcher.h"
#include "RecorderTrayIcon.h"   // tanara_gui::makeTrayIcon (header-only, from gui/common via tanara_shell_common)

#include "tanara/app/SettingsManager.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/foundation/platform/Platform.h"
#include "tanara/watcher/detect/Autostart.h"
#include "tanara/watcher/detect/DetectorRegistry.h"
#include "tanara/watcher/detect/IMeetingDetector.h"
#include "tanara/foundation/ProcessLock.h"
#include "tanara/watcher/PlaybackRouting.h"
#include "tanara/library/tracks/TrackCatalog.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QDateTime>
#include <QFont>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>

#if defined(TANARA_HAVE_DBUS)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif

namespace tanara_watcher {

TrayWatcher::TrayWatcher(QObject* parent)
    : QObject(parent)
    , m_settings(new tanara::app::SettingsManager(QString(), this))
    , m_timer(new QTimer(this))
    , m_stateTimer(new QTimer(this))
{
}

TrayWatcher::~TrayWatcher()
{
    delete m_detector;   // not a QObject, so deleted by hand
}

bool TrayWatcher::start()
{
    const tanara::app::AppSettings s = m_settings->settings();

    // Detector: the settings detectorId, or the first available one. None: nothing to watch.
    m_detector = s.watcher.detectorId.isEmpty()
        ? m_detectors.createBest()
        : m_detectors.create(s.watcher.detectorId);
    if (!m_detector)
        return false;
    m_detector->configure(s.watcher.knownCallApps, QStringLiteral("tanara"));

    applyAutostart(s.watcher.autostart);
    m_autostartApplied = s.watcher.autostart;
    m_settingsStamp = QFileInfo(m_settings->settingsFilePath()).lastModified();

    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return false;

    // --- tray icon + menu ---
    m_tray = new QSystemTrayIcon(tanara_gui::makeTrayIcon(tanara_gui::TrayState::Watching), this);
    auto* menu = new QMenu();
    m_headerAction = menu->addAction(QString());           // "Recording · 00:12:47"
    m_headerAction->setEnabled(false);
    { QFont f = m_headerAction->font(); f.setBold(true); m_headerAction->setFont(f); }
    m_showAction  = menu->addAction(tr("Felvevő megjelenítése"), this, &TrayWatcher::openRecorder);
    m_startAction = menu->addAction(tr("Felvétel indítása"), this, &TrayWatcher::startRecordingNow);
    m_stopAction  = menu->addAction(tr("Felvétel leállítása"), this, &TrayWatcher::stopRecording);
    menu->addAction(tr("Elemző megnyitása"), this, &TrayWatcher::openAnalyzer);
    menu->addAction(tr("Beállítások…"), this, &TrayWatcher::openSettings);
    menu->addSeparator();
    menu->addAction(tr("Kilépés…"), this, &TrayWatcher::quitRequested);
    connect(menu, &QMenu::aboutToShow, this, &TrayWatcher::refreshState);
    m_tray->setContextMenu(menu);
    refreshState();
    m_tray->show();

    // The icon, balloon (elapsed time) and menu refresh every second: a recording can start /
    // stop independently of the poll interval.
    connect(m_stateTimer, &QTimer::timeout, this, &TrayWatcher::refreshState);
    // After Settings is saved the watcher reconfigures without a restart.
    connect(m_stateTimer, &QTimer::timeout, this, &TrayWatcher::reloadSettingsIfChanged);
    m_stateTimer->start(1000);

    // Clicking the notification OPENS the recorder (the user names + starts). This is the
    // showMessage fallback path (works on Windows); on Linux the D-Bus notification with real
    // action buttons is the main path (showCallNotification).
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &TrayWatcher::openRecorder);

    // Left click on the tray icon (SNI: "Activate") opens / raises the recorder. The right click
    // is the menu, handled by setContextMenu; other reasons than Trigger are left alone.
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger)
                    openRecorder();
            });

#if defined(TANARA_HAVE_DBUS)
    // Feedback from notification actions on the session bus. Filtering is by id
    // (onNotifyActionInvoked), so other apps' notifications do not interfere.
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("ActionInvoked"),
        this, SLOT(onNotifyActionInvoked(uint,QString)));
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("NotificationClosed"),
        this, SLOT(onNotifyClosed(uint,uint)));
#endif

    // --- poll loop ---
    connect(m_timer, &QTimer::timeout, this, &TrayWatcher::poll);
    const int interval = qMax(1, s.watcher.detectorIntervalSec) * 1000;
    if (s.watcher.detectorEnabled) {
        m_timer->start(interval);
        poll();   // do not wait for the first interval
    }
    return true;
}

void TrayWatcher::poll()
{
    const tanara::app::AppSettings s = m_settings->settings();
    if (!s.watcher.detectorEnabled || !m_detector)
        return;

    const tanara::watcher::MeetingSignal sig = m_detector->poll();
    const bool recording = tanara::RecordingLock::read(lockPath()).active;
    m_callActive = sig.active;

    if (sig.active) {
        m_detAppName     = sig.appName;
        m_detWindowTitle = sig.windowTitle;

        // Offer once per meeting: rising edge OR new session; NEVER during a recording.
        const bool newSession = !m_wasActive || sig.sourceRef != m_lastOfferedRef;
        if (newSession && !recording) {
            m_lastOfferedRef = sig.sourceRef;
            showCallNotification(sig.appName);
        }
    } else {
        m_lastOfferedRef.clear();   // inactive: the next call can be offered again
    }
    m_wasActive = sig.active;
    refreshState();
}

void TrayWatcher::reloadSettingsIfChanged()
{
    const QDateTime stamp = QFileInfo(m_settings->settingsFilePath()).lastModified();
    if (!stamp.isValid() || stamp == m_settingsStamp)
        return;
    // Check first that it parses: SettingsManager::load() would fall back to defaults on a bad
    // file (losing the running state). On a bad file the stamp is not advanced either, so we retry.
    QFile f(m_settings->settingsFilePath());
    if (!f.open(QIODevice::ReadOnly) || !QJsonDocument::fromJson(f.readAll()).isObject())
        return;
    f.close();
    m_settingsStamp = stamp;
    m_settings->load();
    applySettings();
}

void TrayWatcher::applySettings()
{
    const tanara::app::AppSettings s = m_settings->settings();
    if (m_detector)
        m_detector->configure(s.watcher.knownCallApps, QStringLiteral("tanara"));
    if (s.watcher.autostart != m_autostartApplied) {
        applyAutostart(s.watcher.autostart);
        m_autostartApplied = s.watcher.autostart;
    }
    const int interval = qMax(1, s.watcher.detectorIntervalSec) * 1000;
    if (s.watcher.detectorEnabled) {
        if (!m_timer->isActive() || m_timer->interval() != interval)
            m_timer->start(interval);
        poll();
    } else {
        m_timer->stop();
        m_callActive = false;
        m_wasActive = false;
        m_lastOfferedRef.clear();
        refreshState();
    }
}

QString TrayWatcher::configuredUiLanguage()
{
    return tanara::app::SettingsManager().settings().ui.language;
}

QString TrayWatcher::formatElapsed(qint64 seconds)
{
    const qint64 s = qMax<qint64>(0, seconds);
    return QStringLiteral("%1:%2:%3")
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

void TrayWatcher::refreshState()
{
    if (!m_tray)
        return;
    const tanara::RecordingLock::Info lock = tanara::RecordingLock::read(lockPath());
    const bool recording = lock.active;
    QString elapsed;
    if (recording) {
        const QDateTime started = QDateTime::fromString(lock.startedAt, Qt::ISODate);
        elapsed = formatElapsed(started.isValid() ? started.secsTo(QDateTime::currentDateTime()) : 0);
    }

    // Icon: Recording > Call detected > Watching. Swapped only on a change.
    const tanara_gui::TrayState st = recording ? tanara_gui::TrayState::Recording
        : m_callActive ? tanara_gui::TrayState::CallDetected : tanara_gui::TrayState::Watching;
    if (m_iconState != static_cast<int>(st)) {
        m_iconState = static_cast<int>(st);
        m_tray->setIcon(tanara_gui::makeTrayIcon(st));
    }

    // Balloon: the app, or the elapsed time.
    if (recording)
        m_tray->setToolTip(tr("Tanara — felvétel fut · %1").arg(elapsed));
    else if (m_callActive && !m_detAppName.isEmpty())
        m_tray->setToolTip(tr("Tanara — hívás észlelve: %1").arg(m_detAppName));
    else
        m_tray->setToolTip(tr("Tanara — figyel · nincs hívás"));

    // Menu: header + stop while recording, otherwise start.
    m_headerAction->setVisible(recording);
    m_headerAction->setText(tr("Felvétel · %1").arg(elapsed));
    m_stopAction->setVisible(recording);
    m_startAction->setVisible(!recording);
}

QStringList TrayWatcher::recordArgs(bool immediate) const
{
    QStringList args{ QStringLiteral("--record") };
    if (!immediate)
        args << QStringLiteral("--no-start");   // only opens; the user names + starts
    if (m_callActive && !m_detAppName.isEmpty()) {
        // The recorder derives an automatic name from the app name ("Teams call · Oct 3, 14:02"),
        // which the user can change any time and which also goes into the meeting's "detected call"
        // field. The notes stay empty: they belong to the user (names, terms, corrections).
        args << QStringLiteral("--app") << m_detAppName;
    }
    return args;
}

void TrayWatcher::startRecordingNow()
{
    // Do not start a second one if a recording is already running.
    if (tanara::RecordingLock::read(lockPath()).active) {
        if (m_tray)
            m_tray->showMessage(tr("Már folyik felvétel"),
                                tr("Egy rögzítés már fut."),
                                QSystemTrayIcon::Information, 4000);
        return;
    }
    launch(recordArgs(/*immediate*/ true));
}

void TrayWatcher::openRecorder()
{
    // Opens / raises the recorder window (does not start). If a recorder is already running (even
    // in the background), it receives the `--record` request and shows itself.
    launch(recordArgs(/*immediate*/ false));
}

void TrayWatcher::stopRecording()
{
    // Explicit user action from the menu. The request goes to the running recorder (singleton
    // forwarding); if no recorder is running, the command does nothing.
    launch({ QStringLiteral("--record"), QStringLiteral("--stop") });
}

void TrayWatcher::openAnalyzer()
{
    launch({});   // plain tanara: main window
}

void TrayWatcher::openSettings()
{
    // If the main window is running, it opens Settings; otherwise a standalone Settings window starts.
    launch({ QStringLiteral("--settings"), QStringLiteral("watcher") });
}

void TrayWatcher::quitRequested()
{
    // Ask while recording. Quitting the watcher does NOT stop the recording (a separate
    // process); we say so, and offer stopping on a separate button.
    if (tanara::RecordingLock::read(lockPath()).active) {
        QMessageBox box(QMessageBox::Question, tr("Tanara — kilépés"),
                        tr("A felvétel még fut."), QMessageBox::NoButton);
        box.setInformativeText(tr("A figyelő kilépése nem állítja le a felvételt: a felvevő tovább "
                                  "rögzít, de a tálca-ikon eltűnik. Mit tegyek?"));
        QPushButton* quitOnly = box.addButton(tr("Kilépés, a felvétel fusson"), QMessageBox::AcceptRole);
        QPushButton* stopQuit = box.addButton(tr("Leállítás és kilépés"), QMessageBox::DestructiveRole);
        box.addButton(tr("Mégse"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == stopQuit)
            stopRecording();
        else if (box.clickedButton() != quitOnly)
            return;
    }
    QCoreApplication::quit();
}

QString TrayWatcher::tanaraBinary() const
{
    const QString dir = QCoreApplication::applicationDirPath();
    const QString exe = tanara::platform::executableName(QStringLiteral("tanara"));
    // 1) installed: next to the watcher; 2) developer build: ../gui/; 3) PATH.
    for (const QString& cand : { QDir(dir).filePath(exe),
                                 QDir(dir).filePath(QStringLiteral("../gui/") + exe) }) {
        if (QFileInfo::exists(cand))
            return QFileInfo(cand).absoluteFilePath();
    }
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("tanara"));
    return onPath.isEmpty() ? exe : onPath;
}

QString TrayWatcher::lockPath() const
{
    // In the state folder (with TANARA_HOME in the sandbox); see tanara::recordingLockPath.
    return tanara::recordingLockPath();
}

void TrayWatcher::launch(const QStringList& args) const
{
    QProcess::startDetached(tanaraBinary(), args);
}

QString TrayWatcher::callNotificationBody(const QString& appName, const QString& outputName)
{
    QString body = tr("Elindítsam a felvételt?");
    if (!outputName.isEmpty())
        body += QLatin1Char(' ') + tr("%1 most ezen a kimeneten szól: %2.").arg(appName, outputName);
    return body;
}

void TrayWatcher::showCallNotification(const QString& appName)
{
    // Which output is the call app playing to right now? (Linux/PipeWire; empty elsewhere, so omitted.)
    // The output name is the user-given name (Settings > Recording), if any.
    const QString rawOutput = tanara::watcher::queryAudioGraph().outputForApp(appName);
    // The recorder sees the output through its monitor ("Monitor of <description>"): the rename lives under that key.
    const QString monitorKey = QStringLiteral("Monitor of ") + rawOutput;
    const QString output = rawOutput.isEmpty() ? QString()
        : tanara::library::devicenames::hasOverride(monitorKey) ? tanara::library::devicenames::displayName(monitorKey)
                                                       : tanara::library::devicenames::displayName(rawOutput);
    const QString title = tr("Hívást észleltem: %1").arg(appName);
    const QString body = callNotificationBody(appName, output);

#if defined(TANARA_HAVE_DBUS)
    // freedesktop Notifications directly: clicking QSystemTrayIcon::showMessage does not work
    // on Plasma (no default action) and it cannot show buttons. Here three real action buttons
    // are sent (the system's native notification instead of the design's custom 420 px card),
    // and a body click is the "default" action.
    QDBusInterface iface(QStringLiteral("org.freedesktop.Notifications"),
                         QStringLiteral("/org/freedesktop/Notifications"),
                         QStringLiteral("org.freedesktop.Notifications"));
    if (iface.isValid()) {
        const QStringList actions{
            QStringLiteral("default"),       tr("Felvevő megnyitása"),
            QStringLiteral("record-now"),    tr("Felvétel indítása"),
            QStringLiteral("open-recorder"), tr("Felvevő megnyitása"),
            QStringLiteral("dismiss"),       tr("Nem most"),
        };
        QVariantMap hints;
        hints.insert(QStringLiteral("urgency"), 1);   // normal
        const QDBusReply<uint> reply = iface.call(
            QStringLiteral("Notify"),
            QStringLiteral("Tanara"),
            m_notifyId,                                   // replaces_id: replaces the previous one
            QStringLiteral("audio-input-microphone"),     // theme icon name
            title, body, actions, hints, 12000);
        if (reply.isValid()) {
            m_notifyId = reply.value();
            return;
        }
        // invalid reply: showMessage fallback below
    }
#endif
    // Fallback (Windows / no D-Bus): no buttons; a click is handled by messageClicked (opens
    // the recorder), where the platform forwards it.
    m_tray->showMessage(title,
                        body + QLatin1Char(' ') + tr("Kattints ide a felvevő megnyitásához."),
                        QSystemTrayIcon::Information, 12000);
}

void TrayWatcher::onNotifyActionInvoked(uint id, const QString& actionKey)
{
#if defined(TANARA_HAVE_DBUS)
    if (id != m_notifyId || m_notifyId == 0)
        return;   // not our notification
    if (actionKey == QStringLiteral("record-now"))
        startRecordingNow();
    else if (actionKey == QStringLiteral("dismiss"))
        return;   // "Not now": nothing (this call is not offered again)
    else   // "default" (body click) and "open-recorder" both open
        openRecorder();
#else
    Q_UNUSED(id); Q_UNUSED(actionKey);
#endif
}

void TrayWatcher::onNotifyClosed(uint id, uint reason)
{
    Q_UNUSED(reason);
#if defined(TANARA_HAVE_DBUS)
    if (id == m_notifyId)
        m_notifyId = 0;   // closed: the next Notify must not "replace" a dead id
#else
    Q_UNUSED(id);
#endif
}

void TrayWatcher::applyAutostart(bool on) const
{
    // The rule lives in ONE place (tanara/detect/Autostart.h): with TANARA_HOME (sandbox / test)
    // it NEVER touches the real autostart entry. Linux: .desktop, Windows: Run key.
    tanara::watcher::autostart::applyWatcher(on, QCoreApplication::applicationFilePath());
}

} // namespace tanara_watcher

#include "TrayWatcher.h"
#include "AppIcon.h"   // tanara_gui::makeTanaraIcon (header-only, gui/src az include-path-on)

#include "tanara/SettingsManager.h"
#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/IMeetingDetector.h"
#include "tanara/detect/RecordingLock.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    , m_settings(new tanara::SettingsManager(QString(), this))
    , m_timer(new QTimer(this))
{
}

TrayWatcher::~TrayWatcher()
{
    delete m_detector;   // nem QObject → kézi
}

bool TrayWatcher::start()
{
    tanara::registerBuiltinDetectors();   // idempotens

    const tanara::AppSettings s = m_settings->settings();

    // Detektor: a settings detectorId-ja, vagy az első elérhető. Nincs → nincs mit figyelni.
    m_detector = s.detectorId.isEmpty()
        ? tanara::MeetingDetectorRegistry::instance().createBest()
        : tanara::MeetingDetectorRegistry::instance().create(s.detectorId);
    if (!m_detector)
        return false;
    m_detector->configure(s.knownCallApps, QStringLiteral("tanara"));

    applyAutostart(s.watcherAutostart);

    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return false;

    // --- tálca-ikon + menü ---
    m_tray = new QSystemTrayIcon(tanara_gui::makeTanaraIcon(), this);
    auto* menu = new QMenu();
    menu->addAction(tr("Rögzítés azonnali indítása"), this, &TrayWatcher::startRecordingNow);
    menu->addAction(tr("Rögzítő megnyitása…"), this, &TrayWatcher::openRecorder);
    menu->addAction(tr("Elemző megnyitása"), this, &TrayWatcher::openAnalyzer);
    menu->addSeparator();
    menu->addAction(tr("Kilépés"), qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(menu);
    updateTrayTooltip(false, QString());
    m_tray->show();

    // Az értesítésre kattintva NYISSA meg a rögzítőt (a user keresztel + indít). Ez a
    // showMessage-fallback útja (Windowson működik); Linuxon a D-Bus-os notification a fő
    // út (showCallNotification), valódi akció-gombokkal.
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &TrayWatcher::openRecorder);

#if defined(TANARA_HAVE_DBUS)
    // A notification-akciók visszajelzései a session-busról. A szűrés id-alapú
    // (onNotifyActionInvoked), így más appok értesítései nem zavarnak be.
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

    // --- poll-hurok ---
    connect(m_timer, &QTimer::timeout, this, &TrayWatcher::poll);
    const int interval = qMax(1, s.detectorIntervalSec) * 1000;
    if (s.detectorEnabled) {
        m_timer->start(interval);
        poll();   // ne várjunk az első intervallumot
    }
    return true;
}

void TrayWatcher::poll()
{
    const tanara::AppSettings s = m_settings->settings();
    if (!s.detectorEnabled || !m_detector)
        return;

    const tanara::MeetingSignal sig = m_detector->poll();
    const bool recording = tanara::RecordingLock::read(lockPath()).active;
    updateTrayTooltip(recording, sig.active ? sig.appName : QString());

    if (sig.active) {
        m_detAppName     = sig.appName;
        m_detWindowTitle = sig.windowTitle;

        // Ajánlás meetingenként egyszer: rising edge VAGY új session; felvétel közben SOHA.
        const bool newSession = !m_wasActive || sig.sourceRef != m_lastOfferedRef;
        if (newSession && !recording) {
            m_lastOfferedRef = sig.sourceRef;
            showCallNotification(sig.appName, sig.windowTitle);
        }
    } else {
        m_lastOfferedRef.clear();   // inaktív → a következő hívás újra ajánlható
    }
    m_wasActive = sig.active;
}

QStringList TrayWatcher::recordArgs(bool immediate) const
{
    QStringList args{ QStringLiteral("--record") };
    if (!immediate)
        args << QStringLiteral("--no-start");   // csak megnyit, a user keresztel + indít
    if (!m_detAppName.isEmpty()) {
        QString title = m_detAppName;
        if (!m_detWindowTitle.isEmpty() && m_detWindowTitle != m_detAppName)
            title += QStringLiteral(" — ") + m_detWindowTitle;
        args << QStringLiteral("--title") << title;
        args << QStringLiteral("--context")
             << QStringLiteral("Automatikusan észlelt hívás: %1").arg(m_detAppName);
    }
    return args;
}

void TrayWatcher::startRecordingNow()
{
    // Ne indítsunk másodikat, ha már megy felvétel.
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
    // A rögzítő ablakot nyitja meg (nem indít) — a user elkeresztel és maga indít.
    // Felvétel közben is megnyitható (a futó felvevő ablakát a --record maga hozza elő,
    // de biztonságból itt nem indítunk másodikat: --no-start úgyis csak megnyit).
    launch(recordArgs(/*immediate*/ false));
}

void TrayWatcher::openAnalyzer()
{
    launch({});   // sima tanara → MainWindow
}

QString TrayWatcher::tanaraBinary() const
{
    const QString dir = QCoreApplication::applicationDirPath();
#if defined(Q_OS_WIN)
    const QString exe = QStringLiteral("tanara.exe");
#else
    const QString exe = QStringLiteral("tanara");
#endif
    // 1) telepített: a watcher mellett; 2) fejlesztői build: ../gui/; 3) PATH.
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
    QString metaDir = m_settings->settings().metadataDir;
    if (metaDir.isEmpty())
        metaDir = QDir(QDir::homePath()).filePath(QStringLiteral(".tanara"));
    else if (metaDir.startsWith(QLatin1Char('~')))
        metaDir = QDir::homePath() + metaDir.mid(1);
    return QDir(metaDir).filePath(QStringLiteral("recording.lock"));
}

void TrayWatcher::launch(const QStringList& args) const
{
    QProcess::startDetached(tanaraBinary(), args);
}

void TrayWatcher::showCallNotification(const QString& appName, const QString& windowTitle)
{
#if defined(TANARA_HAVE_DBUS)
    // freedesktop Notifications közvetlenül: a QSystemTrayIcon::showMessage kattintása
    // Plasmán nem működik (nincs default akció), és gombokat sem tud. Itt két valódi
    // akció-gomb megy ki, a test-kattintás pedig a "default" akció.
    QDBusInterface iface(QStringLiteral("org.freedesktop.Notifications"),
                         QStringLiteral("/org/freedesktop/Notifications"),
                         QStringLiteral("org.freedesktop.Notifications"));
    if (iface.isValid()) {
        const QStringList actions{
            QStringLiteral("default"),       tr("Rögzítő megnyitása"),
            QStringLiteral("record-now"),    tr("Rögzítés azonnali indítása"),
            QStringLiteral("open-recorder"), tr("Rögzítő megnyitása…"),
        };
        QVariantMap hints;
        hints.insert(QStringLiteral("urgency"), 1);   // normal
        const QDBusReply<uint> reply = iface.call(
            QStringLiteral("Notify"),
            QStringLiteral("Tanara"),
            m_notifyId,                                   // replaces_id: az előzőt cseréli
            QStringLiteral("audio-input-microphone"),     // téma-ikon név
            tr("Hívás észlelve — %1").arg(appName),
            windowTitle.trimmed().isEmpty() ? tr("Aktív hívást észleltem.") : windowTitle,
            actions, hints, 8000);
        if (reply.isValid()) {
            m_notifyId = reply.value();
            return;
        }
        // érvénytelen válasz → showMessage-fallback lent
    }
#endif
    // Fallback (Windows / nincs D-Bus): a kattintást a messageClicked kezeli, ahol működik.
    m_tray->showMessage(
        tr("Hívás észlelve — %1").arg(appName),
        tr("A Tanara tálca-ikonra kattintva indíthatod a rögzítést "
           "(azonnali indítás vagy a rögzítő megnyitása)."),
        QSystemTrayIcon::Information, 8000);
}

void TrayWatcher::onNotifyActionInvoked(uint id, const QString& actionKey)
{
#if defined(TANARA_HAVE_DBUS)
    if (id != m_notifyId || m_notifyId == 0)
        return;   // nem a mi értesítésünk
    if (actionKey == QStringLiteral("record-now"))
        startRecordingNow();
    else   // "default" (test-kattintás) és "open-recorder" is megnyit
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
        m_notifyId = 0;   // lezárult → a következő Notify ne "cseréljen" halott id-t
#else
    Q_UNUSED(id);
#endif
}

void TrayWatcher::updateTrayTooltip(bool recording, const QString& detectedApp)
{
    if (!m_tray)
        return;
    if (recording)
        m_tray->setToolTip(tr("Tanara — felvétel folyamatban"));
    else if (!detectedApp.isEmpty())
        m_tray->setToolTip(tr("Tanara — hívás észlelve: %1").arg(detectedApp));
    else
        m_tray->setToolTip(tr("Tanara — figyel"));
}

void TrayWatcher::applyAutostart(bool on) const
{
#if defined(Q_OS_LINUX)
    // Freedesktop autostart (KDE/GNOME honorálja): ~/.config/autostart/tanara-watcher.desktop
    const QString dir = QDir(QDir::homePath()).filePath(QStringLiteral(".config/autostart"));
    const QString path = QDir(dir).filePath(QStringLiteral("tanara-watcher.desktop"));
    if (!on) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(dir);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        const QString exec = QCoreApplication::applicationFilePath();
        QTextStream(&f)
            << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=Tanara Watcher\n"
            << "Comment=Meeting-figyelő a rendszertálcán\n"
            << "Exec=" << exec << "\n"
            << "Terminal=false\n"
            << "X-GNOME-Autostart-enabled=true\n";
    }
#else
    Q_UNUSED(on);   // Windows (HKCU\...\Run) / macOS (LaunchAgent): későbbi kör
#endif
}

} // namespace tanara_watcher

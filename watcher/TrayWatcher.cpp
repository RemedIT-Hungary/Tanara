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
    menu->addAction(QStringLiteral("Felvétel indítása"), this, &TrayWatcher::startRecordingNow);
    menu->addAction(QStringLiteral("Elemző megnyitása"), this, &TrayWatcher::openAnalyzer);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Kilépés"), qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(menu);
    updateTrayTooltip(false, QString());
    m_tray->show();

    // Az értesítésre kattintva induljon a felvétel.
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &TrayWatcher::startRecordingNow);

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
            m_tray->showMessage(
                QStringLiteral("Hívás észlelve — %1").arg(sig.appName),
                QStringLiteral("Kattints ide a rögzítés indításához (vagy a tálca-ikon menüjéből)."),
                QSystemTrayIcon::Information, 8000);
        }
    } else {
        m_lastOfferedRef.clear();   // inaktív → a következő hívás újra ajánlható
    }
    m_wasActive = sig.active;
}

void TrayWatcher::startRecordingNow()
{
    // Ne indítsunk másodikat, ha már megy felvétel.
    if (tanara::RecordingLock::read(lockPath()).active) {
        if (m_tray)
            m_tray->showMessage(QStringLiteral("Már folyik felvétel"),
                                QStringLiteral("Egy rögzítés már fut."),
                                QSystemTrayIcon::Information, 4000);
        return;
    }
    QStringList args{ QStringLiteral("--record") };
    if (!m_detAppName.isEmpty()) {
        QString title = m_detAppName;
        if (!m_detWindowTitle.isEmpty() && m_detWindowTitle != m_detAppName)
            title += QStringLiteral(" — ") + m_detWindowTitle;
        args << QStringLiteral("--title") << title;
        args << QStringLiteral("--context")
             << QStringLiteral("Automatikusan észlelt hívás: %1").arg(m_detAppName);
    }
    launch(args);
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

void TrayWatcher::updateTrayTooltip(bool recording, const QString& detectedApp)
{
    if (!m_tray)
        return;
    if (recording)
        m_tray->setToolTip(QStringLiteral("Tanara — felvétel folyamatban"));
    else if (!detectedApp.isEmpty())
        m_tray->setToolTip(QStringLiteral("Tanara — hívás észlelve: %1").arg(detectedApp));
    else
        m_tray->setToolTip(QStringLiteral("Tanara — figyel"));
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

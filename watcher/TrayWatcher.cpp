#include "TrayWatcher.h"
#include "RecorderTrayIcon.h"   // tanara_gui::makeTrayIcon (header-only, gui/src az include-path-on)

#include "tanara/SettingsManager.h"
#include "tanara/Paths.h"
#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/IMeetingDetector.h"
#include "tanara/detect/RecordingLock.h"
#include "tanara/audio/PlaybackRouting.h"
#include "tanara/audio/TrackCatalog.h"

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
    , m_stateTimer(new QTimer(this))
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

    // --- tálca-ikon + menü (R11) ---
    m_tray = new QSystemTrayIcon(tanara_gui::makeTrayIcon(tanara_gui::TrayState::Watching), this);
    auto* menu = new QMenu();
    m_headerAction = menu->addAction(QString());           // „Felvétel · 00:12:47”
    m_headerAction->setEnabled(false);
    { QFont f = m_headerAction->font(); f.setBold(true); m_headerAction->setFont(f); }
    m_showAction  = menu->addAction(tr("Felvevő megjelenítése"), this, &TrayWatcher::openRecorder);
    m_startAction = menu->addAction(tr("Felvétel indítása"), this, &TrayWatcher::startRecordingNow);
    m_stopAction  = menu->addAction(tr("Felvétel leállítása"), this, &TrayWatcher::stopRecording);
    menu->addAction(tr("Elemző megnyitása"), this, &TrayWatcher::openAnalyzer);
    menu->addSeparator();
    menu->addAction(tr("Kilépés…"), this, &TrayWatcher::quitRequested);
    connect(menu, &QMenu::aboutToShow, this, &TrayWatcher::refreshState);
    m_tray->setContextMenu(menu);
    refreshState();
    m_tray->show();

    // Az ikon, a buborék (eltelt idő) és a menü másodpercenként frissül: a felvétel a
    // poll-intervallumtól függetlenül indulhat / állhat le.
    connect(m_stateTimer, &QTimer::timeout, this, &TrayWatcher::refreshState);
    m_stateTimer->start(1000);

    // Az értesítésre kattintva NYISSA meg a rögzítőt (a user keresztel + indít). Ez a
    // showMessage-fallback útja (Windowson működik); Linuxon a D-Bus-os notification a fő
    // út (showCallNotification), valódi akció-gombokkal.
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &TrayWatcher::openRecorder);

    // Bal katt a tálca-ikonon (SNI: "Activate") → a felvevő megnyitása / előhozása. A jobb
    // katt a menü, azt a setContextMenu kezeli; a Trigger-en kívüli reasonökhöz nem nyúlunk.
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger)
                    openRecorder();
            });

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
    m_callActive = sig.active;

    if (sig.active) {
        m_detAppName     = sig.appName;
        m_detWindowTitle = sig.windowTitle;

        // Ajánlás meetingenként egyszer: rising edge VAGY új session; felvétel közben SOHA.
        const bool newSession = !m_wasActive || sig.sourceRef != m_lastOfferedRef;
        if (newSession && !recording) {
            m_lastOfferedRef = sig.sourceRef;
            showCallNotification(sig.appName);
        }
    } else {
        m_lastOfferedRef.clear();   // inaktív → a következő hívás újra ajánlható
    }
    m_wasActive = sig.active;
    refreshState();
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

    // Ikon: Felvétel fut > Hívás észlelve > Figyel. Csak váltáskor cseréljük.
    const tanara_gui::TrayState st = recording ? tanara_gui::TrayState::Recording
        : m_callActive ? tanara_gui::TrayState::CallDetected : tanara_gui::TrayState::Watching;
    if (m_iconState != static_cast<int>(st)) {
        m_iconState = static_cast<int>(st);
        m_tray->setIcon(tanara_gui::makeTrayIcon(st));
    }

    // Buborék: az app, ill. az eltelt idő.
    if (recording)
        m_tray->setToolTip(tr("Tanara — felvétel fut · %1").arg(elapsed));
    else if (m_callActive && !m_detAppName.isEmpty())
        m_tray->setToolTip(tr("Tanara — hívás észlelve: %1").arg(m_detAppName));
    else
        m_tray->setToolTip(tr("Tanara — figyel · nincs hívás"));

    // Menü: felvétel közben fejléc + leállítás, különben indítás.
    m_headerAction->setVisible(recording);
    m_headerAction->setText(tr("Felvétel · %1").arg(elapsed));
    m_stopAction->setVisible(recording);
    m_startAction->setVisible(!recording);
}

QStringList TrayWatcher::recordArgs(bool immediate) const
{
    QStringList args{ QStringLiteral("--record") };
    if (!immediate)
        args << QStringLiteral("--no-start");   // csak megnyit, a user keresztel + indít
    if (m_callActive && !m_detAppName.isEmpty()) {
        // Az app-névből a felvevő automatikus nevet képez („Teams-hívás · okt. 3. 14:02”),
        // amit a user bármikor átírhat.
        args << QStringLiteral("--app") << m_detAppName;
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
    // A felvevő ablakot nyitja meg / hozza elő (nem indít). Ha már fut felvevő (akár
    // háttérbe küldve), a `--record` kérést az kapja meg és megmutatja magát.
    launch(recordArgs(/*immediate*/ false));
}

void TrayWatcher::stopRecording()
{
    // Kifejezett felhasználói művelet a menüből. A kérést a futó felvevő kapja meg
    // (singleton-továbbítás); ha nincs futó felvevő, a parancs semmit nem csinál.
    launch({ QStringLiteral("--record"), QStringLiteral("--stop") });
}

void TrayWatcher::openAnalyzer()
{
    launch({});   // sima tanara → főablak
}

void TrayWatcher::quitRequested()
{
    // Felvétel közben rákérdezünk. A figyelő kilépése a felvételt NEM állítja le (az külön
    // folyamat) — ezt ki is mondjuk, és a leállítást külön gombon kínáljuk.
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
    // A metaadat-mappában (TANARA_HOME mellett a sandboxban) — lásd tanara::recordingLockPath.
    return tanara::recordingLockPath(m_settings->settings().metadataDir);
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
    // Melyik kimenetre szól épp a hívás-app? (Linux/PipeWire; máshol üres → kimarad.)
    const QString output = tanara::tracknames::shortDeviceName(
        tanara::queryAudioGraph().outputForApp(appName));
    const QString title = tr("Hívást észleltem: %1").arg(appName);
    const QString body = callNotificationBody(appName, output);

#if defined(TANARA_HAVE_DBUS)
    // freedesktop Notifications közvetlenül: a QSystemTrayIcon::showMessage kattintása
    // Plasmán nem működik (nincs default akció), és gombokat sem tud. Itt három valódi
    // akció-gomb megy ki (a design 420 px-es egyedi kártyája helyett a rendszer natív
    // értesítése), a test-kattintás pedig a "default" akció.
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
            m_notifyId,                                   // replaces_id: az előzőt cseréli
            QStringLiteral("audio-input-microphone"),     // téma-ikon név
            title, body, actions, hints, 12000);
        if (reply.isValid()) {
            m_notifyId = reply.value();
            return;
        }
        // érvénytelen válasz → showMessage-fallback lent
    }
#endif
    // Fallback (Windows / nincs D-Bus): gombok nincsenek; a kattintást a messageClicked
    // kezeli (a felvevő megnyitása), ahol a platform továbbítja.
    m_tray->showMessage(title,
                        body + QLatin1Char(' ') + tr("Kattints ide a felvevő megnyitásához."),
                        QSystemTrayIcon::Information, 12000);
}

void TrayWatcher::onNotifyActionInvoked(uint id, const QString& actionKey)
{
#if defined(TANARA_HAVE_DBUS)
    if (id != m_notifyId || m_notifyId == 0)
        return;   // nem a mi értesítésünk
    if (actionKey == QStringLiteral("record-now"))
        startRecordingNow();
    else if (actionKey == QStringLiteral("dismiss"))
        return;   // „Nem most”: semmi (erre a hívásra többet nem ajánlunk)
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

void TrayWatcher::applyAutostart(bool on) const
{
    // Sandbox / teszt-példány (TANARA_HOME): a felhasználó VALÓDI automatikus indítását
    // (~/.config/autostart) nem írjuk felül és nem töröljük.
    if (!tanara::paths::homeOverride().isEmpty())
        return;
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

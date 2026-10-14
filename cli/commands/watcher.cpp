// tanara-cli, watcher module: detect, autostart, running-apps. All output is English, no tr().
#include "Commands.h"

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/watcher/detect/Autostart.h"
#include "tanara/watcher/detect/DetectorRegistry.h"
#include "tanara/watcher/detect/IMeetingDetector.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QTimer>

#include <memory>

namespace tanara::cli {

namespace {

int cmdDetect(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    QCoreApplication& qapp = ctx.qapp();
    // Smoke test: the watcher ENGINE without a separate process or tray. A single poll, or a
    // timed one with --watch (using the settings interval or --interval N seconds).
    watcher::registerBuiltinDetectors();
    const app::AppSettings s = app.settings()->settings();
    std::unique_ptr<watcher::IMeetingDetector> det(
        s.watcher.detectorId.isEmpty()
            ? watcher::MeetingDetectorRegistry::instance().createBest()
            : watcher::MeetingDetectorRegistry::instance().create(s.watcher.detectorId));
    if (!det) {
        err() << QStringLiteral("No meeting detector available on this platform (pw-dump?).") << "\n";
        err().flush();
        return 1;
    }
    bool watch = false;
    int interval = s.watcher.detectorIntervalSec;
    QStringList apps = s.watcher.knownCallApps;
    for (int i = 2; i < args.size(); ++i) {
        if (args[i] == "--watch") watch = true;
        else if (args[i] == "--interval" && i + 1 < args.size()) interval = args[++i].toInt();
        // For this run only: the settings list does not change.
        else if (args[i] == "--app" && i + 1 < args.size()) apps << args[++i].trimmed().toLower();
    }
    if (interval < 1) interval = 1;
    det->configure(apps, QStringLiteral("tanara"));

    out() << QStringLiteral("Detector: %1  (known apps: %2)").arg(det->id(), apps.join(QStringLiteral(", "))) << "\n";
    out().flush();

    auto pollOnce = [&det]() {
        const watcher::MeetingSignal sig = det->poll();
        if (sig.active)
            out() << QStringLiteral("  ● MEETING: %1  [appId=%2, window=\"%3\", source=%4]")
                         .arg(sig.appName, sig.appId, sig.windowTitle, sig.sourceRef)
                  << "\n";
        else
            out() << QStringLiteral("  ○ no active call") << "\n";
        out().flush();
    };

    if (!watch) { pollOnce(); return 0; }

    pollOnce();
    out() << QStringLiteral("(checking every %1 s — Ctrl-C to stop)").arg(interval) << "\n";
    out().flush();
    auto* timer = new QTimer(&qapp);
    QObject::connect(timer, &QTimer::timeout, &qapp, [&pollOnce]() { pollOnce(); });
    timer->start(interval * 1000);
    return qapp.exec();
}

int autostartUsage()
{
    err() << "Usage: autostart status | on | off\n";
    return 2;
}

int cmdAutostart(CliContext& ctx, const QStringList& args)
{
    namespace as = watcher::autostart;
    const QString sub = args.value(2);
    if (sub == "status") {
        out() << "Watcher autostart: " << (as::isWatcherEnabled() ? "on" : "off") << "\n"
              << "Entry: " << (as::watcherEntryPath().isEmpty() ? QStringLiteral("(unsupported platform)") : as::watcherEntryPath()) << "\n"
              << "Manageable here: " << (as::managed() ? "yes" : "no (unsupported platform, TANARA_HOME is set, or test mode)") << "\n";
        out().flush();
        return 0;
    }
    if (sub != "on" && sub != "off") return autostartUsage();
    const bool on = sub == "on";
    // The protection lives in applyWatcher()/managed(): a sandbox or test run never writes an entry.
    if (!as::managed()) {
        err() << "Autostart is not managed in this run (unsupported platform, TANARA_HOME is set, or test mode); nothing changed.\n";
        return 1;
    }
    const QString exe = as::findWatcherExecutable(QCoreApplication::applicationDirPath());
    if (on && exe.isEmpty()) { err() << "ERROR: tanara-watcher was not found next to tanara-cli.\n"; return 1; }
    if (!as::applyWatcher(on, exe)) { err() << "ERROR: could not change the autostart entry.\n"; return 1; }
    // The setting follows the entry (as in the Settings window).
    app::SettingsManager* settings = ctx.app().settings();
    app::AppSettings s = settings->settings();
    s.watcher.autostart = on;
    settings->setSettings(s);
    out() << "Watcher autostart: " << (on ? "on" : "off") << "\n";
    out().flush();
    return 0;
}

using ProcessMap = QMap<QString, qint64>;   // lowercase name -> one pid

// /proc (Linux); empty where there is none.
ProcessMap procfsProcesses()
{
    ProcessMap names;
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& pid : pids) {
        bool numeric = false;
        const qint64 n = pid.toLongLong(&numeric);
        if (!numeric) continue;
        QFile f(QStringLiteral("/proc/%1/comm").arg(pid));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString name = QString::fromLocal8Bit(f.readAll()).trimmed().toLower();
        if (!name.isEmpty() && !names.contains(name)) names.insert(name, n);
    }
    return names;
}

// `tasklist` (Windows).
ProcessMap tasklistProcesses()
{
    ProcessMap names;
    QProcess p;
    p.start(QStringLiteral("tasklist"), {QStringLiteral("/fo"), QStringLiteral("csv"), QStringLiteral("/nh")});
    if (!p.waitForFinished(10000)) return names;
    for (const QString& line : QString::fromLocal8Bit(p.readAllStandardOutput()).split('\n')) {
        const QStringList cols = line.trimmed().split(QStringLiteral("\",\""));
        if (cols.size() < 2) continue;
        QString name = cols.at(0);
        name.remove(QLatin1Char('"'));
        names.insert(name.toLower(), cols.at(1).toLongLong());
    }
    return names;
}

// The running processes; empty if the platform has no source (no platform macros: a runtime check).
ProcessMap runningProcesses()
{
    return QOperatingSystemVersion::currentType() == QOperatingSystemVersion::Windows ? tasklistProcesses()
                                                                                       : procfsProcesses();
}

int cmdRunningApps(CliContext& ctx, const QStringList&)
{
    // Read-only: which of the watched call apps (settings) are running now. The watcher's real
    // signal is an app holding the microphone (see `detect`), not a running process.
    const QStringList known = ctx.app().settings()->settings().watcher.knownCallApps;
    const ProcessMap procs = runningProcesses();
    out() << "Watched call apps (" << known.size() << "): "
          << known.join(QStringLiteral(", ")) << "\n";
    if (procs.isEmpty()) {
        out() << "(the process list is not available on this platform)\n";
        out().flush();
        return 0;
    }
    int running = 0;
    for (auto it = procs.constBegin(); it != procs.constEnd(); ++it) {
        for (const QString& k : known) {
            const QString frag = k.trimmed().toLower();
            if (frag.isEmpty() || !it.key().contains(frag)) continue;
            out() << "  " << it.key() << "  (pid " << it.value() << ", matches \"" << frag << "\")\n";
            ++running;
            break;
        }
    }
    if (running == 0) out() << "  (none running)\n";
    out().flush();
    return 0;
}

} // namespace

void registerWatcherCommands(CommandRegistry& r)
{
    r.add({"detect", "watcher", "Active-call detection (smoke test of the watcher engine)",
           "detect [--watch] [--interval N] [--app NAME]...", true, cmdDetect});
    r.add({"autostart", "watcher", "Show or change the watcher's start at login",
           "autostart status | on | off", false, cmdAutostart});
    r.add({"running-apps", "watcher", "List the watched call apps that are running now",
           "running-apps", false, cmdRunningApps});
}

} // namespace tanara::cli

// tanara-watcher: lightweight background meeting watcher (system tray).
// No window: lives in the tray icon and launches the `tanara --record` / `tanara` processes.
#include "TrayWatcher.h"

#include "tanara/foundation/i18n/Localization.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/foundation/ProcessLock.h"
#include "RecorderTrayIcon.h"

#include <QApplication>
#include <QLockFile>
#include <QDir>
#include <QFile>

int main(int argc, char** argv)
{
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i)
        rawArgs << QFile::decodeName(argv[i]);   // NFC on macOS (QProcess sends NFD)
    tanara::initLogging(tanara::parseLogOptions(rawArgs));

    QApplication app(argc, argv);
    // Organization "Tanara" in all three executables. The app name stays "Tanara Watcher" (the tray
    // item's identity); the data folders do not depend on either (foundation/paths/AppPaths.h).
    QApplication::setApplicationName(QStringLiteral("Tanara Watcher"));
    QApplication::setOrganizationName(QStringLiteral("Tanara"));
    // No main window, so the tray icon alone keeps the process alive.
    QApplication::setQuitOnLastWindowClosed(false);

    // Developer QA: renders the three tray icons to PNG (for dark and light panels), then exits.
    //   QT_QPA_PLATFORM=offscreen tanara-watcher --render-icons <folder>
    if (const int i = rawArgs.indexOf(QStringLiteral("--render-icons")); i >= 0 && i + 1 < rawArgs.size()) {
        const QDir out(rawArgs.at(i + 1));
        out.mkpath(QStringLiteral("."));
        const char* names[] = {"watching", "call", "recording"};
        for (int st = 0; st < 3; ++st)
            for (bool dark : {true, false})
                for (int px : {22, 64})
                    tanara_gui::drawTrayPixmap(static_cast<tanara_gui::TrayState>(st), px, dark)
                        .save(out.filePath(QStringLiteral("tray-%1-%2-%3.png")
                            .arg(QLatin1String(names[st]), dark ? QStringLiteral("dark") : QStringLiteral("light"))
                            .arg(px)));
        return 0;
    }

    // Single instance: PID-based lock (QLockFile handles a stale/dead PID itself).
    // The lock lives in the state folder (with TANARA_HOME in the sandbox), so a sandbox watcher
    // does not block the user's real watcher, and vice versa.
    QDir().mkpath(tanara::paths::current().stateDir());
    QLockFile instanceLock(tanara::watcherLockPath());
    instanceLock.setStaleLockTime(0);   // PID-based stale detection only
    if (!instanceLock.tryLock(100)) {
        qCInfo(tanara::lcApp).noquote() << "tanara-watcher is already running — exiting.";
        return 0;
    }

    // UI language (settings.json uiLanguage), before the tray menu / notification strings.
    tanara::installAppTranslator(tanara_watcher::TrayWatcher::configuredUiLanguage());

    tanara_watcher::TrayWatcher watcher;
    if (!watcher.start()) {
        qCWarning(tanara::lcApp).noquote()
            << "Cannot start the watcher: no detector or system tray available.";
        return 1;
    }

    return app.exec();
}

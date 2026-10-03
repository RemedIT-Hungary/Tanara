// tanara-watcher — pehelysúlyú háttér meeting-figyelő (rendszertálca).
// Nincs ablak: a tálca-ikonon él, és a `tanara --record` / `tanara` folyamatokat indítja.
#include "TrayWatcher.h"

#include "tanara/Localization.h"
#include "tanara/Logging.h"
#include "tanara/Paths.h"
#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/RecordingLock.h"
#include "RecorderTrayIcon.h"

#include <QApplication>
#include <QLockFile>
#include <QDir>

int main(int argc, char** argv)
{
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i)
        rawArgs << QString::fromLocal8Bit(argv[i]);
    tanara::initLogging(tanara::parseLogOptions(rawArgs));

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Tanara Watcher"));
    QApplication::setOrganizationName(QStringLiteral("RemedIT"));
    // Nincs fő ablak → a tálca-ikon önmagában tartja életben a folyamatot.
    QApplication::setQuitOnLastWindowClosed(false);

    // Fejlesztői QA: a három tálca-ikon PNG-be (sötét és világos panelre), majd kilépés.
    //   QT_QPA_PLATFORM=offscreen tanara-watcher --render-icons <mappa>
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

    // Egy-példány: PID-alapú lock (a QLockFile a stale/halott PID-et maga kezeli).
    // A lock a metaadat-mappában él (~/.tanara, ill. TANARA_HOME mellett a sandboxban), így
    // egy sandbox-figyelő nem blokkolja a felhasználó valódi figyelőjét, és fordítva.
    const QString metaDir = tanara::paths::defaultMetadataDir();
    QDir().mkpath(metaDir);
    QLockFile instanceLock(tanara::watcherLockPath());
    instanceLock.setStaleLockTime(0);   // csak PID-alapú stale-detektálás
    if (!instanceLock.tryLock(100)) {
        qCInfo(tanara::lcApp).noquote() << "A tanara-watcher már fut — kilépés.";
        return 0;
    }

    // UI-nyelv (settings.json uiLanguage) — a tray-menü/notification szövegek előtt.
    tanara::installAppTranslator();

    tanara::registerBuiltinDetectors();

    tanara_watcher::TrayWatcher watcher;
    if (!watcher.start()) {
        qCWarning(tanara::lcApp).noquote()
            << "Nem indítható a figyelő: nincs elérhető detektor vagy rendszertálca.";
        return 1;
    }

    return app.exec();
}

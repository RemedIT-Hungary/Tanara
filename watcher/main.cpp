// tanara-watcher — pehelysúlyú háttér meeting-figyelő (rendszertálca).
// Nincs ablak: a tálca-ikonon él, és a `tanara --record` / `tanara` folyamatokat indítja.
#include "TrayWatcher.h"

#include "tanara/Logging.h"
#include "tanara/detect/DetectorRegistry.h"

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

    // Egy-példány: PID-alapú lock (a QLockFile a stale/halott PID-et maga kezeli).
    const QString metaDir = QDir(QDir::homePath()).filePath(QStringLiteral(".tanara"));
    QDir().mkpath(metaDir);
    QLockFile instanceLock(QDir(metaDir).filePath(QStringLiteral("watcher.lock")));
    instanceLock.setStaleLockTime(0);   // csak PID-alapú stale-detektálás
    if (!instanceLock.tryLock(100)) {
        qCInfo(tanara::lcApp).noquote() << "A tanara-watcher már fut — kilépés.";
        return 0;
    }

    tanara::registerBuiltinDetectors();

    tanara_watcher::TrayWatcher watcher;
    if (!watcher.start()) {
        qCWarning(tanara::lcApp).noquote()
            << "Nem indítható a figyelő: nincs elérhető detektor vagy rendszertálca.";
        return 1;
    }

    return app.exec();
}

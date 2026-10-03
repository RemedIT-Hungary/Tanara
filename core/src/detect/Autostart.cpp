#include "tanara/detect/Autostart.h"
#include "tanara/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace tanara::autostart {

QString watcherEntryPath()
{
#if defined(Q_OS_LINUX)
    return QDir(QDir::homePath()).filePath(QStringLiteral(".config/autostart/tanara-watcher.desktop"));
#else
    return QString();
#endif
}

bool managed()
{
    if (!paths::homeOverride().isEmpty()) return false;      // homokozó: a valódi bejegyzés tabu
    if (QStandardPaths::isTestModeEnabled()) return false;
    return !watcherEntryPath().isEmpty();
}

QString findWatcherExecutable(const QString& applicationDir)
{
#if defined(Q_OS_WIN)
    const QString exe = QStringLiteral("tanara-watcher.exe");
#else
    const QString exe = QStringLiteral("tanara-watcher");
#endif
    for (const QString& cand : { QDir(applicationDir).filePath(exe),
                                 QDir(applicationDir).filePath(QStringLiteral("../watcher/") + exe) }) {
        if (QFileInfo(cand).isFile())
            return QFileInfo(cand).absoluteFilePath();
    }
    return QString();
}

bool writeEntry(const QString& entryPath, const QString& watcherExecutable)
{
    if (entryPath.isEmpty() || watcherExecutable.isEmpty()) return false;
    QDir().mkpath(QFileInfo(entryPath).absolutePath());
    QFile f(entryPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;
    QTextStream(&f)
        << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=Tanara Watcher\n"
        << "Comment=Meeting-figyelő a rendszertálcán\n"
        << "Exec=" << watcherExecutable << "\n"
        << "Terminal=false\n"
        << "X-GNOME-Autostart-enabled=true\n";
    return true;
}

bool applyWatcher(bool on, const QString& watcherExecutable)
{
    if (!managed()) return false;
    const QString path = watcherEntryPath();
    if (!on) {
        QFile::remove(path);
        return true;
    }
    return writeEntry(path, watcherExecutable);
}

} // namespace tanara::autostart

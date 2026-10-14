// RunningApps on Linux: /proc.
#include "tanara/watcher/RunningApps.h"
#include "tanara/watcher/platform/linux/Procfs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace tanara::watcher {

namespace procfs {

QVector<RunningProcess> readProcesses(const QString& procRoot)
{
    QVector<RunningProcess> out;
    const QDir proc(procRoot);
    const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& pid : pids) {
        bool isNumber = false;
        const qint64 n = pid.toLongLong(&isNumber);
        if (!isNumber) continue;
        QFile cmd(proc.filePath(pid + QStringLiteral("/cmdline")));
        if (!cmd.open(QIODevice::ReadOnly)) continue;
        const QByteArray first = cmd.readAll().split('\0').value(0);
        if (first.isEmpty()) continue;   // a kernel thread
        const QString name = QFileInfo(QString::fromLocal8Bit(first)).fileName().toLower();
        if (!name.isEmpty()) out.append(RunningProcess{n, name});
    }
    return out;
}

} // namespace procfs

bool RunningApps::supported()
{
    return true;
}

QVector<RunningProcess> RunningApps::processes()
{
    return procfs::readProcesses(QStringLiteral("/proc"));
}

} // namespace tanara::watcher

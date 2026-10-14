#include "tanara/watcher/detect/Autostart.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/foundation/platform/Platform.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace tanara::watcher::autostart {

IAutostart& platformAutostart()
{
    static const std::unique_ptr<IAutostart> impl = createPlatformAutostart();
    return *impl;
}

QString watcherEntryPath()
{
    return platformAutostart().entryPath();
}

bool isWatcherEnabled()
{
    return platformAutostart().isEnabled();
}

bool managed(const IAutostart& impl)
{
    if (!paths::homeOverride().isEmpty()) return false;      // sandbox: the real entry is taboo
    if (QStandardPaths::isTestModeEnabled()) return false;
    return !impl.entryPath().isEmpty();
}

bool managed()
{
    return managed(platformAutostart());
}

QString findWatcherExecutable(const QString& applicationDir)
{
    const QString exe = platform::executableName(QStringLiteral("tanara-watcher"));
    for (const QString& cand : { QDir(applicationDir).filePath(exe),
                                 QDir(applicationDir).filePath(QStringLiteral("../watcher/") + exe) }) {
        if (QFileInfo(cand).isFile())
            return QFileInfo(cand).absoluteFilePath();
    }
    return QString();
}

bool applyWatcher(IAutostart& impl, bool on, const QString& watcherExecutable)
{
    if (!managed(impl)) return false;
    return on ? impl.enable(watcherExecutable) : impl.disable();
}

bool applyWatcher(bool on, const QString& watcherExecutable)
{
    return applyWatcher(platformAutostart(), on, watcherExecutable);
}

} // namespace tanara::watcher::autostart

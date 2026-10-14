// IAutostart on Linux: an XDG autostart .desktop file.
#include "tanara/watcher/detect/Autostart.h"
#include "tanara/watcher/platform/linux/XdgAutostart.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace tanara::watcher {

namespace autostart {

bool writeEntry(const QString& entryPath, const QString& watcherExecutable)
{
    if (entryPath.isEmpty() || watcherExecutable.isEmpty()) return false;
    QDir().mkpath(QFileInfo(entryPath).absolutePath());
    QFile f(entryPath);
    // .desktop file: needs LF line endings (without Text mode).
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
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

} // namespace autostart

namespace {

class XdgAutostart final : public IAutostart {
public:
    QString entryPath() const override
    {
        // XDG autostart: $XDG_CONFIG_HOME/autostart (default ~/.config/autostart).
        return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
            .filePath(QStringLiteral("autostart/tanara-watcher.desktop"));
    }
    bool isEnabled() const override
    {
        const QString path = entryPath();
        return !path.isEmpty() && QFile::exists(path);
    }
    bool enable(const QString& watcherExecutable) override
    {
        return autostart::writeEntry(entryPath(), watcherExecutable);
    }
    bool disable() override
    {
        QFile::remove(entryPath());
        return true;
    }
};

} // namespace

std::unique_ptr<IAutostart> createPlatformAutostart()
{
    return std::make_unique<XdgAutostart>();
}

} // namespace tanara::watcher

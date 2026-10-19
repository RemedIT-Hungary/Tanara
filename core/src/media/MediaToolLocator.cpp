#include "tanara/media/MediaToolLocator.h"

#include "tanara/foundation/log/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSysInfo>

namespace tanara::media {

namespace {

QString expandHome(const QString& p)
{
    if (p == QLatin1String("~")) return QDir::homePath();
    if (p.startsWith(QLatin1String("~/"))) return QDir::homePath() + p.mid(1);
    return p;
}

bool usable(const QString& path)
{
    const QFileInfo fi(path);
    return fi.isFile() && fi.isExecutable();
}

} // namespace

LocatorEnv LocatorEnv::systemEnv()
{
    LocatorEnv env;
    if (QCoreApplication::instance()) env.appDir = QCoreApplication::applicationDirPath();
    env.appImageDir = qEnvironmentVariable("APPDIR");
    // Runtime check (platform macros live under */platform/<os>/ only).
    const QString os = QSysInfo::productType();
    env.windows = os == QLatin1String("windows");
    env.macos = os == QLatin1String("macos") || os == QLatin1String("osx");
    return env;
}

QString MediaToolLocator::executableName(const QString& tool, bool windows)
{
    if (windows && !tool.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) return tool + QStringLiteral(".exe");
    return tool;
}

QString MediaToolLocator::sourceName(ToolSource s)
{
    switch (s) {
    case ToolSource::Settings: return QStringLiteral("settings");
    case ToolSource::AppDir:   return QStringLiteral("app-dir");
    case ToolSource::Bundle:   return QStringLiteral("bundle");
    case ToolSource::Path:     return QStringLiteral("path");
    case ToolSource::None:     break;
    }
    return QStringLiteral("-");
}

QStringList MediaToolLocator::candidates(const QString& tool, const QString& configured, const LocatorEnv& env)
{
    const QString exe = executableName(tool, env.windows);
    QStringList out;
    if (!configured.trimmed().isEmpty()) out << QDir::cleanPath(expandHome(configured.trimmed()));
    if (!env.appDir.isEmpty()) out << QDir(env.appDir).filePath(exe);
    if (env.macos && !env.appDir.isEmpty()) {
        out << QDir::cleanPath(QDir(env.appDir).filePath(QStringLiteral("../Resources/bin/") + exe));
        out << QDir::cleanPath(QDir(env.appDir).filePath(QStringLiteral("../MacOS/") + exe));
    }
    if (!env.appImageDir.isEmpty()) out << QDir(env.appImageDir).filePath(QStringLiteral("usr/bin/") + exe);
    return out;
}

ToolResolution MediaToolLocator::locate(const QString& tool, const QString& configured, const LocatorEnv& env)
{
    const QString exe = executableName(tool, env.windows);
    const bool hasConfigured = !configured.trimmed().isEmpty();
    const QStringList cands = candidates(tool, configured, env);
    for (int i = 0; i < cands.size(); ++i) {
        const QString& c = cands.at(i);
        if (!usable(c)) {
            if (i == 0 && hasConfigured)
                qCWarning(lcAudio).noquote() << "Configured media tool cannot be run, searching automatically:" << c;
            continue;
        }
        ToolSource src = ToolSource::Bundle;
        if (i == 0 && hasConfigured) src = ToolSource::Settings;
        else if (!env.appDir.isEmpty() && c == QDir(env.appDir).filePath(exe)) src = ToolSource::AppDir;
        return {QFileInfo(c).absoluteFilePath(), src};
    }
    const QString onPath = env.pathDirs.isEmpty() ? QStandardPaths::findExecutable(exe)
                                                  : QStandardPaths::findExecutable(exe, env.pathDirs);
    if (!onPath.isEmpty()) return {onPath, ToolSource::Path};
    return {};
}

} // namespace tanara::media

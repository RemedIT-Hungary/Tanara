#include "tanara/Paths.h"

#include <QDir>
#include <QFileInfo>

namespace tanara {
namespace paths {

QString expandHome(const QString& path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir(QDir::homePath()).filePath(path.mid(2));
    return path;
}

QString homeOverride()
{
    const QString raw = qEnvironmentVariable("TANARA_HOME").trimmed();
    if (raw.isEmpty())
        return QString();
    // Relatív út → az indítási munkakönyvtárhoz képest abszolút.
    return QDir::cleanPath(QDir(expandHome(raw)).absolutePath());
}

QString defaultMetadataDir()
{
    const QString over = homeOverride();
    if (!over.isEmpty())
        return over;
    return QDir(QDir::homePath()).filePath(QStringLiteral(".tanara"));
}

QString resolveMetadataDir(const QString& configured)
{
    const QString over = homeOverride();
    if (!over.isEmpty())
        return over;
    const QString c = configured.trimmed();
    if (c.isEmpty())
        return defaultMetadataDir();
    return expandHome(c);
}

QString metadataFile(const QString& relative, const QString& configuredDir)
{
    return QDir(resolveMetadataDir(configuredDir)).filePath(relative);
}

QString defaultAudioDir()
{
    const QString over = homeOverride();
    if (!over.isEmpty())
        return QDir(over).filePath(QStringLiteral("recordings"));
    return QDir(QDir::homePath()).filePath(QStringLiteral("Tanara/recordings"));
}

QString defaultNotesDir()
{
    const QString over = homeOverride();
    if (!over.isEmpty())
        return QDir(over).filePath(QStringLiteral("notes"));
    return QDir(QDir::homePath()).filePath(QStringLiteral("Tanara/notes"));
}

QString voiceModelFileName()
{
    return QStringLiteral("campplus_sv_zh_en_16k.onnx");
}

QString resolveVoiceModelPath(const QString& metaDir, const QString& appDir)
{
    const QString rel = QStringLiteral("models/") + voiceModelFileName();
    const QString user = QDir(metaDir).filePath(rel);
    if (QFileInfo::exists(user))
        return user;
    if (!appDir.isEmpty()) {
        const QString bundled = QDir(appDir).filePath(rel);
        if (QFileInfo::exists(bundled))
            return bundled;
    }
    return user;
}

} // namespace paths
} // namespace tanara

#include "tanara/voiceid/VoiceModelRegistry.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace tanara {

QVector<VoiceModelSpec> VoiceModelRegistry::builtin()
{
    VoiceModelSpec campplus;
    campplus.id = defaultModelId();
    campplus.displayName = QStringLiteral("CAM++ (3D-Speaker)");
    campplus.fileName = QStringLiteral("campplus_sv_zh_en_16k.onnx");
    campplus.features = EmbedderConfig();   // a VoiceEmbedder alapértelmezése erre a modellre hangolt
    campplus.dim = 192;
    campplus.license = QStringLiteral("Apache-2.0");
    campplus.sourceUrl = QStringLiteral(
        "https://modelscope.cn/models/iic/speech_campplus_sv_zh_en_16k-common_advanced");
    return {campplus};
}

std::optional<VoiceModelSpec> VoiceModelRegistry::spec(const QString& id)
{
    for (const VoiceModelSpec& s : builtin())
        if (s.id == id) return s;
    return std::nullopt;
}

QString VoiceModelRegistry::resolvePath(const VoiceModelSpec& spec, const QString& metaDir,
                                        const QString& appDir)
{
    const QString rel = QStringLiteral("models/") + spec.fileName;
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

QVector<VoiceModelSpec> VoiceModelRegistry::available(const QString& metaDir, const QString& appDir)
{
    QVector<VoiceModelSpec> out;
    for (const VoiceModelSpec& s : builtin())
        if (QFileInfo::exists(resolvePath(s, metaDir, appDir))) out.append(s);
    return out;
}

QStringList VoiceModelRegistry::normalizeIds(const QStringList& ids)
{
    QStringList out;
    for (const QString& id : ids) {
        const QString t = id.trimmed();
        if (!t.isEmpty() && !out.contains(t)) out << t;
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace tanara

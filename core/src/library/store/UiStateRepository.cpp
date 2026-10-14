#include "tanara/library/store/UiStateRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace tanara::library {

namespace {
const QString kRail = QStringLiteral("rail");

QJsonObject readRoot(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
} // namespace

bool UiStateRepository::railVisible(const QString& meetingId) const
{
    if (m_path.isEmpty() || meetingId.isEmpty()) return false;
    return readRoot(m_path).value(kRail).toObject().value(meetingId).toBool(false);
}

bool UiStateRepository::setRailVisible(const QString& meetingId, bool visible)
{
    if (m_path.isEmpty() || meetingId.isEmpty()) return false;
    QJsonObject root = readRoot(m_path);
    QJsonObject rail = root.value(kRail).toObject();
    if (visible) rail.insert(meetingId, true);
    else rail.remove(meetingId);
    root.insert(kRail, rail);
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile out(m_path);
    if (!out.open(QIODevice::WriteOnly)) return false;
    out.write(QJsonDocument(root).toJson());
    return out.commit();
}

} // namespace tanara::library

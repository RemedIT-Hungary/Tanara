#include "tanara/jobs/JobStats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace tanara {

namespace {
constexpr double kAlpha = 0.3;   // mozgóátlag súlya az új mintára
}

JobStats::JobStats(const QString& filePath) : m_filePath(filePath)
{
    load();
}

void JobStats::load()
{
    m_entries.clear();
    if (m_filePath.isEmpty()) return;
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.ratio   = o.value(QStringLiteral("ratio")).toDouble();
        e.samples = o.value(QStringLiteral("samples")).toInt();
        if (e.samples > 0 && e.ratio > 0.0)
            m_entries.insert(it.key(), e);
    }
}

void JobStats::save() const
{
    if (m_filePath.isEmpty()) return;
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QJsonObject root;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        QJsonObject o;
        o[QStringLiteral("ratio")]   = it.value().ratio;
        o[QStringLiteral("samples")] = it.value().samples;
        root[it.key()] = o;
    }
    QSaveFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void JobStats::addSample(const QString& key, double wallSec, double audioSec)
{
    // Túl rövid hangból / mérésből nem tanulunk (a fix költség torzítana).
    if (key.isEmpty() || audioSec < 10.0 || wallSec <= 0.0) return;
    const double ratio = wallSec / audioSec;
    Entry& e = m_entries[key];
    e.ratio = (e.samples == 0) ? ratio : (kAlpha * ratio + (1.0 - kAlpha) * e.ratio);
    ++e.samples;
    save();
}

int JobStats::estimateSec(const QString& key, double audioSec) const
{
    const auto it = m_entries.constFind(key);
    if (it == m_entries.constEnd() || it->samples <= 0 || audioSec <= 0.0) return -1;
    return int(std::lround(it->ratio * audioSec));
}

int JobStats::sampleCount(const QString& key) const
{
    return m_entries.value(key).samples;
}

} // namespace tanara

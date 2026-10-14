#include "tanara/library/store/UtteranceEmbeddingCache.h"
#include "tanara/library/MeetingLayout.h"

#include <QDataStream>
#include <QFile>
#include <QSaveFile>

namespace tanara::library {

namespace {

constexpr quint32 kCacheMagic = 0x54454d42;   // "TEMB"
constexpr quint32 kCacheVersion = 2;   // v2: per-model vectors (v1 dropped)

} // namespace

bool UtteranceEmbeddingCache::has(const QString& modelId, const QString& utteranceId) const
{
    const auto it = models.constFind(modelId);
    return it != models.constEnd() && it->contains(utteranceId);
}

EmbeddingSet UtteranceEmbeddingCache::setFor(const QString& utteranceId, const QStringList& modelIds) const
{
    EmbeddingSet out;
    for (const QString& m : modelIds) {
        const auto it = models.constFind(m);
        if (it == models.constEnd()) continue;
        const auto v = it->constFind(utteranceId);
        if (v != it->constEnd() && !v->isEmpty()) out.insert(m, v.value());
    }
    return out;
}

QStringList UtteranceEmbeddingCache::missingFor(const QString& modelId, const QStringList& utteranceIds) const
{
    QStringList out;
    const auto it = models.constFind(modelId);
    for (const QString& id : utteranceIds)
        if (it == models.constEnd() || !it->contains(id)) out << id;
    return out;
}

bool UtteranceEmbeddingCache::isEmpty() const
{
    for (const auto& h : models)
        if (!h.isEmpty()) return false;
    return true;
}

QString UtteranceEmbeddingCache::filePath(const QString& meetingFolder)
{
    return MeetingLayout::path(meetingFolder, MeetingLayout::utteranceEmbeddings());
}

UtteranceEmbeddingCache UtteranceEmbeddingCache::load(const QString& meetingFolder,
                                                      const QString& fingerprint)
{
    UtteranceEmbeddingCache cache;
    cache.fingerprint = fingerprint;
    QFile f(filePath(meetingFolder));
    if (!f.open(QIODevice::ReadOnly)) return cache;
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_6_0);
    in.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 magic = 0, version = 0;
    QString fp;
    in >> magic >> version >> fp;
    if (magic != kCacheMagic || version != kCacheVersion || fp != fingerprint)
        return cache;   // another transcript / old (v1) or unknown format -> an empty cache (we recompute)
    QMap<QString, QHash<QString, QVector<float>>> models;
    in >> models;
    if (in.status() == QDataStream::Ok)
        cache.models = models;
    return cache;
}

bool UtteranceEmbeddingCache::save(const QString& meetingFolder) const
{
    QSaveFile f(filePath(meetingFolder));
    if (!f.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_0);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out << kCacheMagic << kCacheVersion << fingerprint << models;
    return f.commit();
}

void UtteranceEmbeddingCache::remove(const QString& meetingFolder)
{
    QFile::remove(filePath(meetingFolder));
}

} // namespace tanara::library

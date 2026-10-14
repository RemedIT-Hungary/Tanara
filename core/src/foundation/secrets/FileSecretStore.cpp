#include "tanara/foundation/secrets/SecretStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace tanara {

FileSecretStore::FileSecretStore(const QString& filePath)
    : m_filePath(filePath)
{
    load();
}

void FileSecretStore::load()
{
    m_cache.clear();
    QFile f(m_filePath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject o = doc.object();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        m_cache.insert(it.key(), it.value().toString());
}

bool FileSecretStore::persist() const
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());

    QJsonObject o;
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it)
        o.insert(it.key(), it.value());

    // Atomic write: if the write is interrupted, the earlier keys survive. The temporary file is
    // already 0600 BEFORE the commit, so the keys are never readable by others even for a moment.
    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit())
        return false;

    // Only the owner may read / write it (on POSIX this is chmod 0600).
    return QFile::setPermissions(m_filePath, QFile::ReadOwner | QFile::WriteOwner);
}

QString FileSecretStore::get(const QString& key) const { return m_cache.value(key); }

bool FileSecretStore::set(const QString& key, const QString& value)
{
    m_cache.insert(key, value);
    return persist();
}

bool FileSecretStore::remove(const QString& key)
{
    if (m_cache.remove(key) > 0)
        return persist();
    return true;
}

bool FileSecretStore::contains(const QString& key) const { return m_cache.contains(key); }

QJsonObject SecretSettings::toJson() const
{
    return QJsonObject{{QStringLiteral("backend"), backend}};
}

SecretSettings SecretSettings::fromJson(const QJsonObject& o)
{
    SecretSettings s;
    const QString b = o.value(QStringLiteral("backend")).toString(s.backend).trimmed().toLower();
    if (b == QLatin1String("auto") || b == QLatin1String("keychain") || b == QLatin1String("file"))
        s.backend = b;
    return s;
}

} // namespace tanara

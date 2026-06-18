#include "tanara/store/KeyStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#  include <sys/stat.h>
#endif

namespace tanara {

namespace {

QString defaultSecretsPath()
{
    // ~/.tanara/secrets.json — összhangban a SettingsManager default metadataDir-rel.
    const QString home = QDir::homePath();
    return QDir(home).filePath(QStringLiteral(".tanara/secrets.json"));
}

} // namespace

KeyStore::KeyStore(const QString& filePath)
    : m_filePath(filePath.isEmpty() ? defaultSecretsPath() : filePath)
{
    load();
}

void KeyStore::load()
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

void KeyStore::persist() const
{
    QFileInfo fi(m_filePath);
    QDir().mkpath(fi.absolutePath());

    QJsonObject o;
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it)
        o.insert(it.key(), it.value());

    QFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.close();

    // Titkos fájl: csak a tulajdonos olvashassa/írhassa (chmod 600).
    QFile::setPermissions(m_filePath,
                          QFile::ReadOwner | QFile::WriteOwner);
#ifdef Q_OS_UNIX
    // Biztos, ami biztos: POSIX 0600 explicit is.
    ::chmod(m_filePath.toLocal8Bit().constData(), S_IRUSR | S_IWUSR);
#endif
}

QString KeyStore::get(const QString& key) const
{
    return m_cache.value(key);
}

void KeyStore::set(const QString& key, const QString& value)
{
    m_cache.insert(key, value);
    persist();
}

void KeyStore::remove(const QString& key)
{
    if (m_cache.remove(key) > 0)
        persist();
}

bool KeyStore::contains(const QString& key) const
{
    return m_cache.contains(key);
}

} // namespace tanara

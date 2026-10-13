#include "tanara/store/KeyStore.h"
#include "tanara/foundation/paths/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#  include <sys/stat.h>
#endif

namespace tanara {

namespace {

QString defaultSecretsPath()
{
    // <metadata folder>/secrets.json: ~/.tanara or TANARA_HOME (tanara/Paths.h).
    return paths::metadataFile(QStringLiteral("secrets.json"));
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

    // Atomic write: if the write is interrupted, the earlier keys survive. The temporary file is
    // already 0600 BEFORE the commit, so the keys are never readable by others even for a moment.
    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit())
        return;

    // Secret file: only the owner may read/write it (chmod 600).
    QFile::setPermissions(m_filePath,
                          QFile::ReadOwner | QFile::WriteOwner);
#ifdef Q_OS_UNIX
    // To be extra safe: an explicit POSIX 0600 too.
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

#include "tanara/foundation/settings/SettingsStore.h"
#include "tanara/foundation/log/Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace tanara {

SettingsStore::SettingsStore(const QString& filePath, QObject* parent)
    : QObject(parent), m_path(filePath)
{
}

SettingsStore::LoadResult SettingsStore::load()
{
    m_sections = QJsonObject();
    m_setAsideOnSave = false;
    QFile f(m_path);
    if (!f.exists())
        return m_last = LoadResult::Missing;
    if (!f.open(QIODevice::ReadOnly)) {
        // A file EXISTS but cannot be opened: empty sections, we do NOT touch the file.
        qCWarning(lcApp).noquote() << "settings.json cannot be read — defaults in memory,"
                                      " file untouched:" << m_path << f.errorString();
        m_setAsideOnSave = true;
        return m_last = LoadResult::Unreadable;
    }
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // A corrupt / half-written file: defaults in memory, but it is NOT silently overwritten (the
        // user's settings are in it); only a later explicit save sets it aside.
        qCWarning(lcApp).noquote() << "settings.json cannot be parsed (" << err.errorString()
                                   << ") — defaults in memory, file untouched:" << m_path;
        m_setAsideOnSave = true;
        return m_last = LoadResult::Unreadable;
    }
    QJsonObject o = doc.object();
    if (o.value(QStringLiteral("version")).toInt(0) != kVersion) {
        // No backward compatibility (D8): an old (flat) file means defaults. It stays readable for
        // `tanara-cli settings import-legacy` (set aside, not overwritten, on the next save).
        qCWarning(lcApp).noquote() << "settings.json is in the old format (not version 2) — defaults;"
                                      " to import it: tanara-cli settings import-legacy." << m_path;
        m_setAsideOnSave = true;
        return m_last = LoadResult::OldFormat;
    }
    o.remove(QStringLiteral("version"));
    m_sections = o;
    return m_last = LoadResult::Ok;
}

void SettingsStore::setSection(const QString& name, const QJsonObject& value)
{
    if (m_sections.value(name).toObject() == value && m_sections.contains(name)) return;
    m_sections.insert(name, value);
    emit sectionChanged(name);
}

QJsonObject SettingsStore::document() const
{
    QJsonObject o = m_sections;
    o.insert(QStringLiteral("version"), kVersion);
    return o;
}

bool SettingsStore::save()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());

    // An unreadable / old file is set aside BEFORE saving (not destroyed): what the user configured
    // can still be rescued from it.
    if (m_setAsideOnSave && QFile::exists(m_path)) {
        const QString tag = m_last == LoadResult::OldFormat ? QStringLiteral(".v1-") : QStringLiteral(".corrupt-");
        const QString aside = m_path + tag
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        if (!QFile::rename(m_path, aside)) {
            qCWarning(lcApp).noquote() << "settings.json: the old file cannot be set aside — save"
                                          " skipped, file untouched:" << m_path;
            return false;
        }
        qCWarning(lcApp).noquote() << "settings.json: old file set aside:" << aside;
    }
    m_setAsideOnSave = false;

    // Atomic write (temporary file + rename): several processes (analyzer, recorder, watcher) read this
    // same file, none may see truncated content; after an interrupted write the earlier file stays.
    const QByteArray data = QJsonDocument(document()).toJson(QJsonDocument::Indented);
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        qCWarning(lcApp).noquote() << "settings.json: save failed:" << m_path << f.errorString();
        return false;
    }
    return true;
}

} // namespace tanara

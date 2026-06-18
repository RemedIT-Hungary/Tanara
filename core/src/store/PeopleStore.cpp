#include "tanara/store/PeopleStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace tanara {

PeopleStore::PeopleStore(const QString& filePath)
{
    m_filePath = filePath.isEmpty()
        ? QDir(QDir::homePath()).filePath(QStringLiteral(".tanara/people.json"))
        : filePath;
    load();
}

QStringList PeopleStore::names() const { return m_names; }

void PeopleStore::add(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || m_names.contains(n, Qt::CaseInsensitive))
        return;
    m_names << n;
    m_names.sort(Qt::CaseInsensitive);
    persist();
}

void PeopleStore::rename(const QString& oldName, const QString& newName)
{
    const QString o = oldName.trimmed(), n = newName.trimmed();
    if (o.isEmpty() || n.isEmpty() || o == n)
        return;
    m_names.removeAll(o);
    if (!m_names.contains(n, Qt::CaseInsensitive))
        m_names << n;
    m_names.sort(Qt::CaseInsensitive);
    persist();
}

void PeopleStore::remove(const QString& name)
{
    if (m_names.removeAll(name.trimmed()) > 0)
        persist();
}

void PeopleStore::load()
{
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr =
        QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("people")).toArray();
    m_names.clear();
    for (const QJsonValue& v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty())
            m_names << s;
    }
}

void PeopleStore::persist() const
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QJsonArray arr;
    for (const QString& n : m_names)
        arr.append(n);
    QJsonObject root;
    root[QStringLiteral("people")] = arr;
    QSaveFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

} // namespace tanara

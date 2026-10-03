#include "tanara/store/PeopleStore.h"
#include "tanara/Logging.h"
#include "tanara/Paths.h"

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
        ? paths::metadataFile(QStringLiteral("people.json"))
        : filePath;
    load();
}

QStringList PeopleStore::names() const { return m_names; }

// Minden módosítás: folyamatközi zár → a lemez friss állapotának visszaolvasása (ha egy
// másik folyamat közben írt) → a módosítás → kiírás. Így a teljes fájl újraírása nem dobja
// el, amit egy másik folyamat (felvevő / figyelő / másik ablak) időközben hozzáadott.

void PeopleStore::add(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    if (m_names.contains(n, Qt::CaseInsensitive))
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
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    m_names.removeAll(o);
    if (!m_names.contains(n, Qt::CaseInsensitive))
        m_names << n;
    m_names.sort(Qt::CaseInsensitive);
    persist();
}

void PeopleStore::remove(const QString& name)
{
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    if (m_names.removeAll(name.trimmed()) > 0)
        persist();
}

void PeopleStore::reloadIfChanged()
{
    if (FileStamp::of(m_filePath) != m_stamp)
        load();
}

void PeopleStore::load()
{
    m_stamp = FileStamp::of(m_filePath);
    m_corrupt = false;
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // Értelmezhetetlen fájl: a memóriabeli lista marad, a fájlt a következő mentés
        // félreteszi (nem írjuk felül nyom nélkül).
        qCWarning(lcStore).noquote() << "people.json nem értelmezhető:" << m_filePath << err.errorString();
        m_corrupt = true;
        return;
    }
    const QJsonArray arr = doc.object().value(QStringLiteral("people")).toArray();
    m_names.clear();
    for (const QJsonValue& v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty())
            m_names << s;
    }
}

void PeopleStore::persist()
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    if (m_corrupt && !setAsideCorrupt(m_filePath)) {
        qCWarning(lcStore).noquote() << "people.json: a sérült fájl nem tehető félre — a mentés kimarad:"
                                     << m_filePath;
        return;
    }
    m_corrupt = false;
    QJsonArray arr;
    for (const QString& n : m_names)
        arr.append(n);
    QJsonObject root;
    root[QStringLiteral("people")] = arr;
    QSaveFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (f.commit())
            m_stamp = FileStamp::of(m_filePath);   // a saját írásunkat nem kell visszaolvasni
    }
}

} // namespace tanara

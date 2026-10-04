#include "tanara/store/PersonDetailsStore.h"
#include "tanara/Logging.h"
#include "tanara/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace tanara {

namespace {

bool sameName(const QString& a, const QString& b)
{
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

bool containsAlias(const QStringList& list, const QString& alias)
{
    return std::any_of(list.cbegin(), list.cend(),
                       [&](const QString& a) { return sameName(a, alias); });
}

} // namespace

PersonDetailsStore::PersonDetailsStore(const QString& filePath)
{
    m_filePath = filePath.isEmpty()
        ? paths::metadataFile(QStringLiteral("people-details.json"))
        : filePath;
    load();
}

void PersonDetailsStore::refresh() { reloadIfChanged(); }

int PersonDetailsStore::indexOf(const QString& name) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (sameName(m_entries[i].d.name, name)) return i;
    return -1;
}

PersonDetailsStore::Entry& PersonDetailsStore::ensure(const QString& name)
{
    const int i = indexOf(name);
    if (i >= 0) return m_entries[i];
    Entry e;
    e.d.name = name.trimmed();
    m_entries.append(e);
    return m_entries.last();
}

PersonDetails PersonDetailsStore::details(const QString& name) const
{
    const int i = indexOf(name);
    if (i >= 0) return m_entries[i].d;
    PersonDetails d;
    d.name = name.trimmed();
    return d;
}

QVector<PersonDetails> PersonDetailsStore::all() const
{
    QVector<PersonDetails> out;
    out.reserve(m_entries.size());
    for (const Entry& e : m_entries) out.append(e.d);
    return out;
}

// Minden módosítás: folyamatközi zár → a lemez friss állapota → a változás → kiírás.

bool PersonDetailsStore::addAlias(const QString& name, const QString& alias)
{
    const QString n = name.trimmed(), a = alias.trimmed();
    if (n.isEmpty() || a.isEmpty() || sameName(n, a)) return false;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    Entry& e = ensure(n);
    if (containsAlias(e.d.aliases, a)) return false;
    e.d.aliases << a;
    persist();
    return true;
}

int PersonDetailsStore::removeAlias(const QString& name, const QString& alias)
{
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const int i = indexOf(name);
    if (i < 0) return -1;
    QStringList& list = m_entries[i].d.aliases;
    for (int k = 0; k < list.size(); ++k) {
        if (!sameName(list[k], alias)) continue;
        list.removeAt(k);
        persist();
        return k;
    }
    return -1;
}

bool PersonDetailsStore::insertAlias(const QString& name, const QString& alias, int index)
{
    const QString n = name.trimmed(), a = alias.trimmed();
    if (n.isEmpty() || a.isEmpty() || sameName(n, a)) return false;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    Entry& e = ensure(n);
    if (containsAlias(e.d.aliases, a)) return false;
    e.d.aliases.insert(std::clamp<qsizetype>(index, 0, e.d.aliases.size()), a);
    persist();
    return true;
}

void PersonDetailsStore::setNote(const QString& name, const QString& note)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const int i = indexOf(n);
    if (i < 0 && note.trimmed().isEmpty()) return;
    Entry& e = ensure(n);
    if (e.d.note == note) return;
    e.d.note = note;
    persist();
}

void PersonDetailsStore::rename(const QString& oldName, const QString& newName, bool oldNameAsAlias)
{
    const QString o = oldName.trimmed(), n = newName.trimmed();
    if (o.isEmpty() || n.isEmpty() || o == n) return;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const bool caseOnly = sameName(o, n);
    const int from = indexOf(o);
    if (caseOnly) {
        if (from < 0) return;
        m_entries[from].d.name = n;
        persist();
        return;
    }
    Entry src;
    if (from >= 0) src = m_entries.takeAt(from);
    if (src.d.isEmpty() && !oldNameAsAlias && indexOf(n) < 0) {
        if (from >= 0) persist();   // csak egy üres bejegyzés tűnt el
        return;
    }
    Entry& dst = ensure(n);
    for (const QString& a : std::as_const(src.d.aliases))
        if (!sameName(a, n) && !containsAlias(dst.d.aliases, a)) dst.d.aliases << a;
    if (oldNameAsAlias && !containsAlias(dst.d.aliases, o)) dst.d.aliases << o;
    // Az új név nem lehet a saját beceneve.
    dst.d.aliases.erase(std::remove_if(dst.d.aliases.begin(), dst.d.aliases.end(),
                                       [&](const QString& a) { return sameName(a, n); }),
                        dst.d.aliases.end());
    if (dst.d.note.trimmed().isEmpty()) dst.d.note = src.d.note;
    else if (!src.d.note.trimmed().isEmpty() && src.d.note != dst.d.note)
        dst.d.note = dst.d.note + QLatin1Char('\n') + src.d.note;
    // A forrás ismeretlen mezői a célba, ha ott nincsenek.
    for (auto it = src.raw.constBegin(); it != src.raw.constEnd(); ++it)
        if (!dst.raw.contains(it.key())) dst.raw.insert(it.key(), it.value());
    persist();
}

void PersonDetailsStore::remove(const QString& name)
{
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const int i = indexOf(name);
    if (i < 0) return;
    m_entries.removeAt(i);
    persist();
}

void PersonDetailsStore::set(const PersonDetails& details)
{
    const QString n = details.name.trimmed();
    if (n.isEmpty()) return;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const int i = indexOf(n);
    if (details.isEmpty()) {
        if (i < 0) return;
        m_entries.removeAt(i);
    } else {
        Entry& e = ensure(n);
        e.d = details;
        e.d.name = n;
    }
    persist();
}

void PersonDetailsStore::reloadIfChanged()
{
    if (FileStamp::of(m_filePath) != m_stamp)
        load();
}

void PersonDetailsStore::load()
{
    m_stamp = FileStamp::of(m_filePath);
    m_corrupt = false;
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (m_stamp.mtimeMs < 0) m_entries.clear();   // a fájl eltűnt: nincs adat
        return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // Értelmezhetetlen fájl: a memóriabeli adat marad, a fájlt a következő mentés félreteszi.
        qCWarning(lcStore).noquote() << "people-details.json nem értelmezhető:" << m_filePath
                                     << err.errorString();
        m_corrupt = true;
        return;
    }
    m_rootExtra = doc.object();
    const QJsonArray arr = m_rootExtra.take(QStringLiteral("people")).toArray();
    m_rootExtra.remove(QStringLiteral("version"));
    m_entries.clear();
    for (const QJsonValue& v : arr) {
        Entry e;
        e.raw = v.toObject();
        e.d.name = e.raw.take(QStringLiteral("name")).toString().trimmed();
        for (const QJsonValue& a : e.raw.take(QStringLiteral("aliases")).toArray()) {
            const QString s = a.toString().trimmed();
            if (!s.isEmpty() && !containsAlias(e.d.aliases, s)) e.d.aliases << s;
        }
        e.d.note = e.raw.take(QStringLiteral("note")).toString();
        if (e.d.name.isEmpty() || indexOf(e.d.name) >= 0) continue;
        m_entries.append(e);
    }
}

void PersonDetailsStore::persist()
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    if (m_corrupt && !setAsideCorrupt(m_filePath)) {
        qCWarning(lcStore).noquote() << "people-details.json: a sérült fájl nem tehető félre — a mentés kimarad:"
                                     << m_filePath;
        return;
    }
    m_corrupt = false;
    QJsonArray arr;
    for (const Entry& e : std::as_const(m_entries)) {
        if (e.d.isEmpty() && e.raw.isEmpty()) continue;   // üres bejegyzést nem tárolunk
        QJsonObject o = e.raw;
        o[QStringLiteral("name")] = e.d.name;
        o[QStringLiteral("aliases")] = QJsonArray::fromStringList(e.d.aliases);
        o[QStringLiteral("note")] = e.d.note;
        arr.append(o);
    }
    QJsonObject root = m_rootExtra;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("people")] = arr;
    QSaveFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (f.commit())
            m_stamp = FileStamp::of(m_filePath);   // a saját írásunkat nem kell visszaolvasni
    }
}

} // namespace tanara

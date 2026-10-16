#include "tanara/library/store/PeopleStore.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/paths/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace tanara::library {

namespace {

const QLatin1String kVersionKey("version");
const QLatin1String kSelfKey("self");
const QLatin1String kPeopleKey("people");
const QLatin1String kNameKey("name");
const QLatin1String kAliasesKey("aliases");
const QLatin1String kNoteKey("note");
const QLatin1String kTagsKey("tags");
const QLatin1String kSideKey("defaultSide");
const QLatin1String kSideManualKey("defaultSideManual");

using persons::sameName;

bool containsAlias(const QStringList& list, const QString& alias)
{
    return std::any_of(list.cbegin(), list.cend(), [&](const QString& a) { return sameName(a, alias); });
}

// One record from the JSON object: the known fields extracted, the rest stays in `raw`.
void readRecord(const PersonId& id, QJsonObject obj, Person* p, QJsonObject* raw)
{
    p->id = id;
    p->displayName = obj.take(kNameKey).toString().trimmed();
    for (const QJsonValue& a : obj.take(kAliasesKey).toArray()) {
        const QString s = a.toString().trimmed();
        if (!s.isEmpty() && !sameName(s, p->displayName) && !containsAlias(p->aliases, s)) p->aliases << s;
    }
    p->note = obj.take(kNoteKey).toString();
    for (const QJsonValue& t : obj.take(kTagsKey).toArray()) {
        const QString tag = t.toString().trimmed();
        if (!tag.isEmpty() && !p->tags.contains(tag)) p->tags << tag;
    }
    p->defaultSide = obj.take(kSideKey).toString();
    p->defaultSideManual = obj.take(kSideManualKey).toBool() && !p->defaultSide.isEmpty();
    *raw = obj;
}

QJsonObject writeRecord(const Person& p, const QJsonObject& raw)
{
    QJsonObject o = raw;
    o[kNameKey] = p.displayName;
    o[kAliasesKey] = QJsonArray::fromStringList(p.aliases);
    o[kNoteKey] = p.note;
    if (!p.tags.isEmpty()) o[kTagsKey] = QJsonArray::fromStringList(p.tags);
    if (!p.defaultSide.isEmpty()) o[kSideKey] = p.defaultSide;
    if (p.defaultSideManual && !p.defaultSide.isEmpty()) o[kSideManualKey] = true;
    return o;
}

QStringList cleanTags(const QStringList& tagIds)
{
    QStringList clean;
    for (const QString& t : tagIds)
        if (!t.trimmed().isEmpty() && !clean.contains(t.trimmed())) clean << t.trimmed();
    return clean;
}

} // namespace

PeopleStore::PeopleStore(const QString& filePath)
{
    m_filePath = filePath.isEmpty()
        ? paths::current().dataFile(paths::files::People)
        : filePath;
    load();
}

int PeopleStore::indexOf(const PersonId& id) const
{
    if (id.isNull()) return -1;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].p.id == id) return i;
    return -1;
}

void PeopleStore::sortEntries()
{
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        const int c = a.p.displayName.compare(b.p.displayName, Qt::CaseInsensitive);
        if (c != 0) return c < 0;
        return a.p.id < b.p.id;
    });
}

QVector<Person> PeopleStore::people() const
{
    QVector<Person> out;
    out.reserve(m_entries.size());
    for (const Entry& e : m_entries) out.append(e.p);
    return out;
}

PersonIds PeopleStore::ids() const
{
    PersonIds out;
    out.reserve(m_entries.size());
    for (const Entry& e : m_entries) out.append(e.p.id);
    return out;
}

bool PeopleStore::contains(const PersonId& id) const { return indexOf(id) >= 0; }

Person PeopleStore::person(const PersonId& id) const
{
    const int i = indexOf(id);
    return i < 0 ? Person() : m_entries[i].p;
}

QString PeopleStore::displayName(const PersonId& id) const
{
    const int i = indexOf(id);
    return i < 0 ? QString() : m_entries[i].p.displayName;
}

PersonNames PeopleStore::names() const
{
    PersonNames out;
    out.reserve(m_entries.size());
    for (const Entry& e : m_entries) out.insert(e.p.id, e.p.displayName);
    return out;
}

PersonIds PeopleStore::findByName(const QString& name) const
{
    PersonIds out;
    if (name.trimmed().isEmpty()) return out;
    for (const Entry& e : m_entries)
        if (sameName(e.p.displayName, name)) out.append(e.p.id);
    return out;
}

PersonId PeopleStore::idForName(const QString& name) const
{
    const PersonIds all = findByName(name);
    return all.isEmpty() ? PersonId() : all.first();
}

PersonChoice PeopleStore::choiceFor(const QString& idOrName) const
{
    const QString v = idOrName.trimmed();
    if (v.isEmpty()) return {};
    const PersonId asId = PersonId::fromString(v);
    if (contains(asId)) return PersonChoice::existing(asId);
    const PersonId byName = idForName(v);
    return byName.isNull() ? PersonChoice::create(v) : PersonChoice::existing(byName);
}

// Every modification: inter-process lock -> reread the fresh state from disk (if another process
// wrote meanwhile) -> the modification -> write out. A legacy (old-format) file is never written.
// Without the lock nothing changes, neither in memory nor on disk (an unlocked write could silently
// overwrite another process's change); the method returns its failure value and lockFailed() is true.

bool PeopleStore::acquired(const SharedFileLock& lock, const char* operation)
{
    m_lockFailed = !lock.locked();
    if (m_lockFailed)
        qCWarning(lcStore).noquote()
            << QStringLiteral("people.json %1: another process holds the lock, the change was not written: %2")
                   .arg(QLatin1String(operation), m_filePath);
    return !m_lockFailed;
}

void PeopleStore::setSelfId(const PersonId& id)
{
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "setSelfId")) return;
    reloadIfChanged();
    if (m_legacy || m_self == id) return;
    m_self = id;
    persist();
}

PersonId PeopleStore::create(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return {};
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "create")) return {};
    reloadIfChanged();
    if (m_legacy) return {};
    Entry e;
    e.p.id = PersonId::generate();
    e.p.displayName = n;
    m_entries.append(e);
    sortEntries();
    persist();
    return e.p.id;
}

PersonId PeopleStore::ensure(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return {};
    reloadIfChanged();
    const PersonId have = idForName(n);
    return have.isNull() ? create(n) : have;
}

PersonId PeopleStore::resolve(const PersonChoice& choice)
{
    if (!choice.id.isNull()) return choice.id;
    return create(choice.newName);
}

bool PeopleStore::restore(const Person& person)
{
    if (person.id.isNull()) return false;
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "restore")) return false;
    reloadIfChanged();
    if (m_legacy) return false;
    const int i = indexOf(person.id);
    if (i >= 0) m_entries[i].p = person;
    else m_entries.append(Entry{person, {}});
    sortEntries();
    return persist();
}

bool PeopleStore::rename(const PersonId& id, const QString& newName)
{
    const QString n = newName.trimmed();
    if (n.isEmpty()) return false;
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "rename")) return false;
    reloadIfChanged();
    const int i = indexOf(id);
    if (i < 0 || m_entries[i].p.displayName == n) return false;
    Person& p = m_entries[i].p;
    p.displayName = n;
    // A name cannot be its own nickname.
    p.aliases.removeIf([&](const QString& a) { return sameName(a, n); });
    sortEntries();
    return persist();
}

bool PeopleStore::remove(const PersonId& id)
{
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "remove")) return false;
    reloadIfChanged();
    const int i = indexOf(id);
    if (i < 0) return false;
    m_entries.removeAt(i);
    if (m_self == id) m_self = PersonId();
    return persist();
}

bool PeopleStore::merge(const PersonId& keep, const PersonId& drop, bool dropNameAsAlias)
{
    if (keep == drop) return false;
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "merge")) return false;
    reloadIfChanged();
    const int k = indexOf(keep), dr = indexOf(drop);
    if (k < 0 || dr < 0) return false;
    const Entry src = m_entries[dr];
    Person& dst = m_entries[k].p;
    QJsonObject& dstRaw = m_entries[k].raw;
    QStringList extra = src.p.aliases;
    if (dropNameAsAlias) extra.append(src.p.displayName);   // after its nicknames (as the old rename-merge did)
    for (const QString& a : std::as_const(extra))
        if (!a.trimmed().isEmpty() && !sameName(a, dst.displayName) && !containsAlias(dst.aliases, a))
            dst.aliases << a.trimmed();
    if (dst.note.trimmed().isEmpty()) dst.note = src.p.note;
    else if (!src.p.note.trimmed().isEmpty() && src.p.note != dst.note)
        dst.note = dst.note + QLatin1Char('\n') + src.p.note;
    for (const QString& t : src.p.tags)
        if (!dst.tags.contains(t)) dst.tags << t;
    if (dst.defaultSide.isEmpty() && !src.p.defaultSide.isEmpty()) {
        dst.defaultSide = src.p.defaultSide;
        dst.defaultSideManual = src.p.defaultSideManual;
    }
    for (auto it = src.raw.constBegin(); it != src.raw.constEnd(); ++it)
        if (!dstRaw.contains(it.key())) dstRaw.insert(it.key(), it.value());
    m_entries.removeAt(dr);
    if (m_self == drop) m_self = keep;
    sortEntries();
    return persist();
}

bool PeopleStore::addAlias(const PersonId& id, const QString& alias)
{
    const QString a = alias.trimmed();
    if (a.isEmpty()) return false;
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "addAlias")) return false;
    reloadIfChanged();
    const int i = indexOf(id);
    if (i < 0) return false;
    Person& p = m_entries[i].p;
    if (sameName(p.displayName, a) || containsAlias(p.aliases, a)) return false;
    p.aliases << a;
    return persist();
}

int PeopleStore::removeAlias(const PersonId& id, const QString& alias)
{
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "removeAlias")) return -1;
    reloadIfChanged();
    const int i = indexOf(id);
    if (i < 0) return -1;
    QStringList& list = m_entries[i].p.aliases;
    for (int k = 0; k < list.size(); ++k) {
        if (!sameName(list[k], alias)) continue;
        list.removeAt(k);
        persist();
        return k;
    }
    return -1;
}

bool PeopleStore::insertAlias(const PersonId& id, const QString& alias, int index)
{
    const QString a = alias.trimmed();
    if (a.isEmpty()) return false;
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "insertAlias")) return false;
    reloadIfChanged();
    const int i = indexOf(id);
    if (i < 0) return false;
    Person& p = m_entries[i].p;
    if (sameName(p.displayName, a) || containsAlias(p.aliases, a)) return false;
    p.aliases.insert(std::clamp<qsizetype>(index, 0, p.aliases.size()), a);
    return persist();
}

bool PeopleStore::setNote(const PersonId& id, const QString& note)
{
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "setNote")) return false;
    reloadIfChanged();
    const int i = indexOf(id);
    if (i < 0 || m_entries[i].p.note == note) return false;
    m_entries[i].p.note = note;
    return persist();
}

bool PeopleStore::setTags(const PersonId& id, const QStringList& tagIds)
{
    const QStringList clean = cleanTags(tagIds);
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "setTags")) return false;
    reloadIfChanged();
    const int i = indexOf(id);
    if (i < 0 || m_entries[i].p.tags == clean) return false;
    m_entries[i].p.tags = clean;
    return persist();
}

QHash<PersonId, QStringList> PeopleStore::allTags() const
{
    QHash<PersonId, QStringList> out;
    for (const Entry& e : m_entries)
        if (!e.p.tags.isEmpty()) out.insert(e.p.id, e.p.tags);
    return out;
}

bool PeopleStore::setDefaultSide(const PersonId& id, const QString& side, bool manual)
{
    const SharedFileLock lock(m_filePath);
    if (!acquired(lock, "setDefaultSide")) return false;
    reloadIfChanged();
    const int i = indexOf(id);
    if (i < 0) return false;
    Person& p = m_entries[i].p;
    // The manual value is protected from learning.
    if (!manual && p.defaultSideManual) return false;
    const bool flag = manual && !side.isEmpty();
    if (p.defaultSide == side && p.defaultSideManual == flag) return false;
    p.defaultSide = side;
    p.defaultSideManual = flag;
    return persist();
}

QHash<PersonId, QString> PeopleStore::defaultSides() const
{
    QHash<PersonId, QString> out;
    for (const Entry& e : m_entries)
        if (!e.p.defaultSide.isEmpty()) out.insert(e.p.id, e.p.defaultSide);
    return out;
}

void PeopleStore::reloadIfChanged()
{
    if (FileStamp::of(m_filePath) != m_stamp)
        load();
}

// Loading:
//  - missing / cannot be opened: the in-memory state stays (empty at startup);
//  - unparseable: the in-memory state stays, the next save sets the file aside;
//  - an older format (no version / version < 3, or a "people" array): legacy, empty, read-only;
//  - otherwise memory becomes the file's content.
void PeopleStore::load()
{
    m_stamp = FileStamp::of(m_filePath);
    m_corrupt = false;
    m_legacy = false;

    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcStore).noquote() << "people.json nem értelmezhető:" << m_filePath << err.errorString();
        m_corrupt = true;
        return;
    }
    QJsonObject root = doc.object();
    m_version = root.take(kVersionKey).toInt(0);
    const QJsonValue peopleValue = root.take(kPeopleKey);
    m_entries.clear();
    m_self = PersonId();
    if (m_version < kFormatVersion || !peopleValue.isObject()) {
        m_legacy = true;
        m_rootExtra = {};
        qCCritical(lcStore).noquote()
            << QStringLiteral("people.json is in an old, name-keyed format (version %1); the app does not read it."
                              " Convert it once (with a backup): run: tanara-cli migrate person-ids  (%2)")
                   .arg(m_version).arg(m_filePath);
        return;
    }
    if (m_version > kFormatVersion)
        qCWarning(lcStore).noquote() << "people.json újabb verziójú, mint amit ez a build ismer:"
                                     << m_version << m_filePath;
    m_self = PersonId::fromString(root.take(kSelfKey).toString());
    m_rootExtra = root;
    const QJsonObject people = peopleValue.toObject();
    for (auto it = people.constBegin(); it != people.constEnd(); ++it) {
        const PersonId id = PersonId::fromString(it.key());
        if (id.isNull()) continue;
        Entry e;
        readRecord(id, it.value().toObject(), &e.p, &e.raw);
        m_entries.append(e);
    }
    sortEntries();
}

bool PeopleStore::persist()
{
    if (m_legacy) {
        qCCritical(lcStore).noquote() << "people.json: old format on disk, not overwritten — run: tanara-cli migrate person-ids";
        return false;
    }
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    if (m_corrupt && !setAsideCorrupt(m_filePath)) {
        qCWarning(lcStore).noquote() << "people.json: a sérült fájl nem tehető félre — a mentés kimarad:"
                                     << m_filePath;
        return false;
    }
    m_corrupt = false;
    QJsonObject people;
    for (const Entry& e : std::as_const(m_entries))
        people.insert(e.p.id.toString(), writeRecord(e.p, e.raw));
    QJsonObject root = m_rootExtra;
    root[kVersionKey] = std::max(m_version, kFormatVersion);
    if (!m_self.isNull()) root[kSelfKey] = m_self.toString();
    root[kPeopleKey] = people;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);

    QSaveFile f(m_filePath);
    const bool ok = f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
    if (!ok) {
        qCWarning(lcStore).noquote() << "people.json nem írható:" << m_filePath << f.errorString();
        return false;
    }
    m_stamp = FileStamp::of(m_filePath);   // no need to reread our own write
    m_version = std::max(m_version, kFormatVersion);
    return true;
}

} // namespace tanara::library

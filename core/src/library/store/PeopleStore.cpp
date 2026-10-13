#include "tanara/library/store/PeopleStore.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/paths/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <algorithm>

namespace tanara::library {

namespace {

constexpr int kFormatVersion = 2;

const QLatin1String kVersionKey("version");
const QLatin1String kPeopleKey("people");
const QLatin1String kUnlistedKey("unlisted");
const QLatin1String kNameKey("name");
const QLatin1String kAliasesKey("aliases");
const QLatin1String kNoteKey("note");
const QLatin1String kTagsKey("tags");

bool sameName(const QString& a, const QString& b)
{
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

bool containsAlias(const QStringList& list, const QString& alias)
{
    return std::any_of(list.cbegin(), list.cend(),
                       [&](const QString& a) { return sameName(a, alias); });
}

// One record from the JSON object: the known fields extracted, the rest stays in `raw`.
void readRecord(QJsonObject obj, PersonDetails* d, QJsonObject* raw)
{
    d->name = obj.take(kNameKey).toString();
    for (const QJsonValue& a : obj.take(kAliasesKey).toArray()) {
        const QString s = a.toString().trimmed();
        if (!s.isEmpty() && !sameName(s, d->name) && !containsAlias(d->aliases, s)) d->aliases << s;
    }
    d->note = obj.take(kNoteKey).toString();
    for (const QJsonValue& t : obj.take(kTagsKey).toArray()) {
        const QString id = t.toString().trimmed();
        if (!id.isEmpty() && !d->tags.contains(id)) d->tags << id;
    }
    *raw = obj;
}

// Merging `src` into `dst`: union of nicknames (the target's name cannot be its own nickname);
// note: the target's if non-empty, the source's if the target's is empty, and if both exist and
// differ, one under the other; the source's unknown fields go to the target if absent there.
void absorb(PersonDetails& dst, QJsonObject& dstRaw, const PersonDetails& src, const QJsonObject& srcRaw)
{
    for (const QString& a : src.aliases)
        if (!sameName(a, dst.name) && !containsAlias(dst.aliases, a)) dst.aliases << a;
    if (dst.note.trimmed().isEmpty()) dst.note = src.note;
    else if (!src.note.trimmed().isEmpty() && src.note != dst.note)
        dst.note = dst.note + QLatin1Char('\n') + src.note;
    for (const QString& t : src.tags)
        if (!dst.tags.contains(t)) dst.tags << t;
    for (auto it = srcRaw.constBegin(); it != srcRaw.constEnd(); ++it)
        if (!dstRaw.contains(it.key())) dstRaw.insert(it.key(), it.value());
}

QJsonObject writeRecord(const PersonDetails& d, const QJsonObject& raw)
{
    QJsonObject o = raw;
    o[kNameKey] = d.name;
    o[kAliasesKey] = QJsonArray::fromStringList(d.aliases);
    o[kNoteKey] = d.note;
    if (!d.tags.isEmpty()) o[kTagsKey] = QJsonArray::fromStringList(d.tags);
    return o;
}

// Renaming the file to "<file>.<suffix>-<timestamp>" (we do not delete what we did not merge in).
// true if the file is no longer at its original place.
bool setAsideAs(const QString& path, const QString& suffix)
{
    if (!QFileInfo::exists(path)) return true;
    const QString aside = path + QLatin1Char('.') + suffix + QLatin1Char('-')
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    return QFile::rename(path, aside);
}

} // namespace

PeopleStore::PeopleStore(const QString& filePath)
{
    m_filePath = filePath.isEmpty()
        ? paths::metadataFile(QStringLiteral("people.json"))
        : filePath;
    load();
    migrateIfNeeded();
}

QString PeopleStore::legacyDetailsPath() const
{
    return QFileInfo(m_filePath).absoluteDir().filePath(QStringLiteral("people-details.json"));
}

int PeopleStore::indexOf(const QString& name) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (sameName(m_entries[i].d.name, name)) return i;
    return -1;
}

PeopleStore::Entry& PeopleStore::ensure(const QString& name)
{
    const int i = indexOf(name);
    if (i >= 0) return m_entries[i];
    Entry e;
    e.d.name = name.trimmed();
    e.listed = false;
    m_entries.append(e);
    return m_entries.last();
}

// The name list sorted by name first, the off-list records after it (among themselves in their
// existing order).
void PeopleStore::sortEntries()
{
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        if (a.listed != b.listed) return a.listed;
        if (!a.listed) return false;
        return a.d.name.compare(b.d.name, Qt::CaseInsensitive) < 0;
    });
}

QStringList PeopleStore::names() const
{
    QStringList out;
    for (const Entry& e : m_entries)
        if (e.listed) out << e.d.name;
    return out;
}

QStringList PeopleStore::unlistedNames() const
{
    QStringList out;
    for (const Entry& e : m_entries)
        if (!e.listed && !(e.d.isEmpty() && e.raw.isEmpty())) out << e.d.name;
    return out;
}

PersonDetails PeopleStore::details(const QString& name) const
{
    const int i = indexOf(name);
    if (i >= 0) return m_entries[i].d;
    PersonDetails d;
    d.name = name.trimmed();
    return d;
}

// Every modification: inter-process lock -> reread the fresh state from disk (if another process
// wrote meanwhile) -> the modification -> write out. So rewriting the whole file does not discard what
// another process (recorder / watcher / another window) added in the meantime.

void PeopleStore::add(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const int i = indexOf(n);
    if (i >= 0) {
        if (m_entries[i].listed)
            return;
        m_entries[i].listed = true;    // the off-list record (nicknames, note) comes back
    } else {
        Entry e;
        e.d.name = n;
        m_entries.append(e);
    }
    sortEntries();
    persist();
}

void PeopleStore::rename(const QString& oldName, const QString& newName, bool oldNameAsAlias)
{
    const QString o = oldName.trimmed(), n = newName.trimmed();
    if (o.isEmpty() || n.isEmpty() || o == n)
        return;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const int from = indexOf(o);
    if (sameName(o, n)) {
        // Only the spelling changes: the same record.
        Entry& e = from >= 0 ? m_entries[from] : ensure(n);
        e.d.name = n;
        e.listed = true;
    } else {
        Entry src;
        if (from >= 0) src = m_entries.takeAt(from);
        Entry& dst = ensure(n);
        dst.listed = true;
        absorb(dst.d, dst.raw, src.d, src.raw);
        if (oldNameAsAlias && !containsAlias(dst.d.aliases, o)) dst.d.aliases << o;
    }
    sortEntries();
    persist();
}

void PeopleStore::remove(const QString& name)
{
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const int i = indexOf(name);
    if (i < 0)
        return;
    m_entries.removeAt(i);
    persist();
}

void PeopleStore::unlist(const QString& name)
{
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const int i = indexOf(name);
    if (i < 0 || !m_entries[i].listed)
        return;
    m_entries[i].listed = false;    // the save drops the empty record
    sortEntries();
    persist();
}

bool PeopleStore::addAlias(const QString& name, const QString& alias)
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

int PeopleStore::removeAlias(const QString& name, const QString& alias)
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

bool PeopleStore::insertAlias(const QString& name, const QString& alias, int index)
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

void PeopleStore::setNote(const QString& name, const QString& note)
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

bool PeopleStore::setTags(const QString& name, const QStringList& tagIds)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return false;
    QStringList clean;
    for (const QString& t : tagIds)
        if (!t.trimmed().isEmpty() && !clean.contains(t.trimmed())) clean << t.trimmed();
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const int i = indexOf(n);
    if (i < 0 && clean.isEmpty()) return false;
    Entry& e = ensure(n);
    if (e.d.tags == clean) return false;
    e.d.tags = clean;
    persist();
    return true;
}

QString PeopleStore::defaultSide(const QString& name) const
{
    const int i = indexOf(name);
    return i < 0 ? QString() : m_entries[i].raw.value(QStringLiteral("defaultSide")).toString();
}

bool PeopleStore::defaultSideManual(const QString& name) const
{
    const int i = indexOf(name);
    return i >= 0 && m_entries[i].raw.value(QStringLiteral("defaultSideManual")).toBool();
}

bool PeopleStore::setDefaultSide(const QString& name, const QString& side, bool manual)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return false;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const int i = indexOf(n);
    if (i < 0 && side.isEmpty()) return false;
    const QString key = QStringLiteral("defaultSide");
    const QString manualKey = QStringLiteral("defaultSideManual");
    // The manual value is protected from learning.
    if (!manual && i >= 0 && m_entries[i].raw.value(manualKey).toBool()) return false;
    Entry& e = ensure(n);
    const bool flag = manual && !side.isEmpty();
    if (e.raw.value(key).toString() == side && e.raw.value(manualKey).toBool() == flag) return false;
    if (side.isEmpty()) e.raw.remove(key);
    else e.raw.insert(key, side);
    if (flag) e.raw.insert(manualKey, true);
    else e.raw.remove(manualKey);
    persist();
    return true;
}

QHash<QString, QStringList> PeopleStore::allTags() const
{
    QHash<QString, QStringList> out;
    for (const Entry& e : m_entries)
        if (!e.d.tags.isEmpty()) out.insert(e.d.name, e.d.tags);
    return out;
}

QHash<QString, QString> PeopleStore::defaultSides() const
{
    QHash<QString, QString> out;
    for (const Entry& e : m_entries) {
        const QString s = e.raw.value(QStringLiteral("defaultSide")).toString();
        if (!s.isEmpty()) out.insert(e.d.name, s);
    }
    return out;
}

void PeopleStore::reloadIfChanged()
{
    if (FileStamp::of(m_filePath) != m_stamp)
        load();
}

// Loading. Three cases for people.json:
//  - missing / cannot be opened: the in-memory state stays (empty at startup);
//  - unparseable: the in-memory state stays, the next save sets the file aside;
//  - parseable: memory becomes the file's content. Old shape (no "version", or the "people" items
//    are plain names) -> migration is needed (m_migrationPending).
// Then the old people-details.json, if it exists:
//  - if people.json is ALREADY in the new shape, the details file is a leftover of an earlier migration
//    (its deletion failed): we do not merge it again (it would resurrect nicknames deleted since), the
//    save / migration step sets it aside as ".leftover-<time>";
//  - otherwise it merges into memory: for a name on the list into its record, for an unknown name (an
//    orphan entry) as an off-list record; the file is deleted AFTER a successful save;
//  - if unparseable: the name list still migrates, and the details file is set aside after a
//    successful save as ".corrupt-<time>" (not deleted, can be rescued by hand).
// This function does NOT write to disk.
void PeopleStore::load()
{
    m_stamp = FileStamp::of(m_filePath);
    m_corrupt = false;
    m_migrationPending = false;
    m_legacy = LegacyDetails::None;

    bool parsed = false;
    QFile f(m_filePath);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            // Unparseable file: the in-memory list stays, the next save sets the file aside (we do not overwrite
            // it without a trace).
            qCWarning(lcStore).noquote() << "people.json nem értelmezhető:" << m_filePath << err.errorString();
            m_corrupt = true;
        } else {
            parsed = true;
            m_rootExtra = doc.object();
            m_version = m_rootExtra.take(kVersionKey).toInt(0);
            const QJsonArray listed = m_rootExtra.take(kPeopleKey).toArray();
            const QJsonArray unlisted = m_rootExtra.take(kUnlistedKey).toArray();
            m_entries.clear();
            bool plainNames = false;
            auto read = [&](const QJsonArray& arr, bool isListed) {
                for (const QJsonValue& v : arr) {
                    Entry e;
                    e.listed = isListed;
                    if (v.isString()) {             // old shape: a plain name
                        e.d.name = v.toString();
                        plainNames = true;
                    } else {
                        readRecord(v.toObject(), &e.d, &e.raw);
                    }
                    if (e.d.name.trimmed().isEmpty()) continue;
                    const int have = indexOf(e.d.name);
                    if (have < 0) {
                        m_entries.append(e);
                        continue;
                    }
                    // The same name twice (case-insensitively): it becomes one record.
                    absorb(m_entries[have].d, m_entries[have].raw, e.d, e.raw);
                    m_entries[have].listed = m_entries[have].listed || isListed;
                }
            };
            read(listed, true);
            read(unlisted, false);
            if (m_version > kFormatVersion)
                qCWarning(lcStore).noquote() << "people.json újabb verziójú, mint amit ez a build ismer:"
                                             << m_version << m_filePath;
            m_migrationPending = m_version < kFormatVersion || plainNames;
        }
    }

    const QString legacyPath = legacyDetailsPath();
    if (!QFileInfo::exists(legacyPath))
        return;
    if (parsed && !m_migrationPending) {
        m_legacy = LegacyDetails::SetAsideLeftover;
        return;
    }
    m_migrationPending = true;
    QFile lf(legacyPath);
    QByteArray bytes;
    const bool readable = lf.open(QIODevice::ReadOnly);
    if (readable) bytes = lf.readAll();
    if (readable && bytes.trimmed().isEmpty()) {
        m_legacy = LegacyDetails::Remove;       // empty file: nothing to take over
        return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (!readable || err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcStore).noquote() << "people-details.json nem értelmezhető — a becenevek és megjegyzések"
                                        " nem vehetők át belőle, a fájl félre lesz téve:"
                                     << legacyPath << (readable ? err.errorString() : lf.errorString());
        m_legacy = LegacyDetails::SetAsideCorrupt;
        return;
    }
    m_legacy = LegacyDetails::Remove;
    QJsonObject legacyRoot = doc.object();
    const QJsonArray arr = legacyRoot.take(kPeopleKey).toArray();
    legacyRoot.remove(kVersionKey);
    for (auto it = legacyRoot.constBegin(); it != legacyRoot.constEnd(); ++it)
        if (!m_rootExtra.contains(it.key())) m_rootExtra.insert(it.key(), it.value());
    QStringList orphans;
    for (const QJsonValue& v : arr) {
        PersonDetails d;
        QJsonObject raw;
        readRecord(v.toObject(), &d, &raw);
        d.name = d.name.trimmed();
        if (d.name.isEmpty() || (d.isEmpty() && raw.isEmpty())) continue;
        const bool known = indexOf(d.name) >= 0;
        Entry& e = ensure(d.name);      // unknown name -> an off-list record
        absorb(e.d, e.raw, d, raw);
        if (!known) orphans << d.name;
    }
    if (!orphans.isEmpty())
        qCInfo(lcStore).noquote() << "people-details.json:" << orphans.size()
                                  << "bejegyzés neve nincs a névlistán — listán kívüli rekordként megmarad";
}

// One-time migration from the old shape (the constructor calls it after loading). It rereads under
// lock (another process may have migrated meanwhile) and writes only if still needed. If the lock cannot
// be obtained, people.json is unparseable, or the write fails: the old files stay untouched, the store
// works in memory, and the next successful save (persist) completes the migration.
void PeopleStore::migrateIfNeeded()
{
    if (!m_migrationPending && m_legacy == LegacyDetails::None)
        return;
    if (m_corrupt)
        return;
    const SharedFileLock lock(m_filePath);
    if (!lock.locked()) {
        qCWarning(lcStore).noquote() << "people.json: az átálláshoz nem szerezhető zár — később újra:" << m_filePath;
        return;
    }
    load();     // the fresh state under the lock
    if (m_corrupt)
        return;
    if (m_migrationPending) {
        if (persist())
            qCInfo(lcStore).noquote() << "people.json átállítva az új alakra:" << m_filePath;
    } else if (m_legacy != LegacyDetails::None) {
        finishLegacyDetails();
    }
}

// Removing the old people-details.json: called ONLY when the new-shape people.json is surely already on disk.
void PeopleStore::finishLegacyDetails()
{
    const QString legacyPath = legacyDetailsPath();
    bool done = true;
    switch (m_legacy) {
    case LegacyDetails::None:
        return;
    case LegacyDetails::Remove:
        done = !QFileInfo::exists(legacyPath) || QFile::remove(legacyPath);
        break;
    case LegacyDetails::SetAsideCorrupt:
        done = setAsideAs(legacyPath, QStringLiteral("corrupt"));
        break;
    case LegacyDetails::SetAsideLeftover:
        qCWarning(lcStore).noquote() << "people-details.json maradt egy korábbi átállásból — félretéve:" << legacyPath;
        done = setAsideAs(legacyPath, QStringLiteral("leftover"));
        break;
    }
    if (done)
        m_legacy = LegacyDetails::None;
    else
        qCWarning(lcStore).noquote() << "people-details.json nem távolítható el:" << legacyPath;
}

bool PeopleStore::persist()
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    if (m_corrupt && !setAsideCorrupt(m_filePath)) {
        qCWarning(lcStore).noquote() << "people.json: a sérült fájl nem tehető félre — a mentés kimarad:"
                                     << m_filePath;
        return false;
    }
    m_corrupt = false;
    // An empty, off-list record carries no data: we do not store it.
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [](const Entry& e) {
                        return !e.listed && e.d.isEmpty() && e.raw.isEmpty();
                    }),
                    m_entries.end());
    QJsonArray listed, unlisted;
    for (const Entry& e : std::as_const(m_entries))
        (e.listed ? listed : unlisted).append(writeRecord(e.d, e.raw));
    QJsonObject root = m_rootExtra;
    root[kVersionKey] = std::max(m_version, kFormatVersion);
    root[kPeopleKey] = listed;
    if (!unlisted.isEmpty())
        root[kUnlistedKey] = unlisted;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);

    QSaveFile f(m_filePath);
    bool ok = f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
    if (ok && m_legacy != LegacyDetails::None) {
        // Before removing the old details file: is what is on disk really what we wrote?
        QFile check(m_filePath);
        ok = check.open(QIODevice::ReadOnly) && check.readAll() == bytes;
    }
    if (!ok) {
        qCWarning(lcStore).noquote() << "people.json nem írható:" << m_filePath << f.errorString()
                                     << (m_migrationPending ? "— az átállás elmaradt, a régi fájlok érintetlenek" : "");
        return false;
    }
    m_stamp = FileStamp::of(m_filePath);   // no need to reread our own write
    m_version = std::max(m_version, kFormatVersion);
    m_migrationPending = false;
    finishLegacyDetails();
    return true;
}

} // namespace tanara::library
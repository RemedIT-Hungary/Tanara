#include "tanara/store/PeopleStore.h"
#include "tanara/Logging.h"
#include "tanara/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <algorithm>

namespace tanara {

namespace {

constexpr int kFormatVersion = 2;

const QLatin1String kVersionKey("version");
const QLatin1String kPeopleKey("people");
const QLatin1String kUnlistedKey("unlisted");
const QLatin1String kNameKey("name");
const QLatin1String kAliasesKey("aliases");
const QLatin1String kNoteKey("note");

bool sameName(const QString& a, const QString& b)
{
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

bool containsAlias(const QStringList& list, const QString& alias)
{
    return std::any_of(list.cbegin(), list.cend(),
                       [&](const QString& a) { return sameName(a, alias); });
}

// Egy rekord a JSON-objektumból: az ismert mezők kiemelve, a többi `raw`-ban marad.
void readRecord(QJsonObject obj, PersonDetails* d, QJsonObject* raw)
{
    d->name = obj.take(kNameKey).toString();
    for (const QJsonValue& a : obj.take(kAliasesKey).toArray()) {
        const QString s = a.toString().trimmed();
        if (!s.isEmpty() && !sameName(s, d->name) && !containsAlias(d->aliases, s)) d->aliases << s;
    }
    d->note = obj.take(kNoteKey).toString();
    *raw = obj;
}

// `src` beolvasztása `dst`-be: becenevek uniója (a cél neve nem lehet saját beceneve);
// megjegyzés: a célé, ha üres, a forrásé, ha mindkettő van és eltér, egymás alá; a forrás
// ismeretlen mezői a célba, ha ott nincsenek.
void absorb(PersonDetails& dst, QJsonObject& dstRaw, const PersonDetails& src, const QJsonObject& srcRaw)
{
    for (const QString& a : src.aliases)
        if (!sameName(a, dst.name) && !containsAlias(dst.aliases, a)) dst.aliases << a;
    if (dst.note.trimmed().isEmpty()) dst.note = src.note;
    else if (!src.note.trimmed().isEmpty() && src.note != dst.note)
        dst.note = dst.note + QLatin1Char('\n') + src.note;
    for (auto it = srcRaw.constBegin(); it != srcRaw.constEnd(); ++it)
        if (!dstRaw.contains(it.key())) dstRaw.insert(it.key(), it.value());
}

QJsonObject writeRecord(const PersonDetails& d, const QJsonObject& raw)
{
    QJsonObject o = raw;
    o[kNameKey] = d.name;
    o[kAliasesKey] = QJsonArray::fromStringList(d.aliases);
    o[kNoteKey] = d.note;
    return o;
}

// A fájl átnevezése „<fájl>.<utótag>-<időbélyeg>” névre (nem törlünk olyat, amit nem
// olvasztottunk be). true, ha a fájl már nincs az eredeti helyén.
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

// A névlista név szerint rendezve elöl, a listán kívüli rekordok utána (egymás közt a
// meglévő sorrendben).
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
    const int i = indexOf(n);
    if (i >= 0) {
        if (m_entries[i].listed)
            return;
        m_entries[i].listed = true;    // a listán kívüli rekord (becenevek, megjegyzés) visszatér
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
        // Csak az írásmód változik: ugyanaz a rekord.
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
    m_entries[i].listed = false;    // az üres rekordot a mentés eldobja
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

QString PeopleStore::defaultSide(const QString& name) const
{
    const int i = indexOf(name);
    return i < 0 ? QString() : m_entries[i].raw.value(QStringLiteral("defaultSide")).toString();
}

bool PeopleStore::setDefaultSide(const QString& name, const QString& side)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return false;
    const SharedFileLock lock(m_filePath);
    reloadIfChanged();
    const int i = indexOf(n);
    if (i < 0 && side.isEmpty()) return false;
    Entry& e = ensure(n);
    const QString key = QStringLiteral("defaultSide");
    if (e.raw.value(key).toString() == side) return false;
    if (side.isEmpty()) e.raw.remove(key);
    else e.raw.insert(key, side);
    persist();
    return true;
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

// Betöltés. Három eset a people.json-ra:
//  - nincs / nem nyitható: a memóriabeli állapot marad (induláskor üres);
//  - értelmezhetetlen: a memóriabeli állapot marad, a fájlt a következő mentés félreteszi;
//  - értelmezhető: a memória a fájl tartalma lesz. Régi alak (nincs "version", vagy a
//    "people" elemei sima nevek) → átállás kell (m_migrationPending).
// Ezután a régi people-details.json, ha létezik:
//  - ha a people.json MÁR új alakú, a details-fájl egy korábbi átállás maradéka (a törlése
//    nem sikerült): nem olvasztjuk be újra (feltámasztana azóta törölt beceneveket), hanem
//    a mentés / átállás lépése „.leftover-<idő>” néven félreteszi;
//  - különben beolvad a memóriába: a névlistán szereplő névhez a rekordjába, az ismeretlen
//    névhez (árva bejegyzés) listán kívüli rekordként; a fájl a sikeres mentés UTÁN törlődik;
//  - ha értelmezhetetlen: a névlista ettől még átáll, a details-fájlt pedig a sikeres mentés
//    után „.corrupt-<idő>” néven félretesszük (nem töröljük, kézzel menthető belőle).
// Ez a függvény a lemezre NEM ír.
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
            // Értelmezhetetlen fájl: a memóriabeli lista marad, a fájlt a következő mentés
            // félreteszi (nem írjuk felül nyom nélkül).
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
                    if (v.isString()) {             // régi alak: sima név
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
                    // Ugyanaz a név kétszer (kisbetű-függetlenül): egy rekord lesz belőle.
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
        m_legacy = LegacyDetails::Remove;       // üres fájl: nincs mit átvenni
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
        Entry& e = ensure(d.name);      // ismeretlen név → listán kívüli rekord
        absorb(e.d, e.raw, d, raw);
        if (!known) orphans << d.name;
    }
    if (!orphans.isEmpty())
        qCInfo(lcStore).noquote() << "people-details.json:" << orphans.size()
                                  << "bejegyzés neve nincs a névlistán — listán kívüli rekordként megmarad";
}

// Egyszeri átállás a régi alakról (a konstruktor hívja a betöltés után). Zár alatt újraolvas
// (egy másik folyamat közben már átállíthatta), és csak akkor ír, ha még mindig kell. Ha a
// zár nem szerezhető meg, a people.json értelmezhetetlen, vagy az írás nem sikerül: a régi
// fájlok érintetlenek maradnak, a tár a memóriában dolgozik, és a következő sikeres mentés
// (persist) fejezi be az átállást.
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
    load();     // a zár alatti, friss állapot
    if (m_corrupt)
        return;
    if (m_migrationPending) {
        if (persist())
            qCInfo(lcStore).noquote() << "people.json átállítva az új alakra:" << m_filePath;
    } else if (m_legacy != LegacyDetails::None) {
        finishLegacyDetails();
    }
}

// A régi people-details.json eltávolítása — CSAK akkor hívjuk, amikor az új alakú people.json
// már biztosan a lemezen van.
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
    // Az üres, listán kívüli rekord nem hordoz adatot: nem tároljuk.
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
        // A régi details-fájl eltávolítása előtt: tényleg az van a lemezen, amit írtunk?
        QFile check(m_filePath);
        ok = check.open(QIODevice::ReadOnly) && check.readAll() == bytes;
    }
    if (!ok) {
        qCWarning(lcStore).noquote() << "people.json nem írható:" << m_filePath << f.errorString()
                                     << (m_migrationPending ? "— az átállás elmaradt, a régi fájlok érintetlenek" : "");
        return false;
    }
    m_stamp = FileStamp::of(m_filePath);   // a saját írásunkat nem kell visszaolvasni
    m_version = std::max(m_version, kFormatVersion);
    m_migrationPending = false;
    finishLegacyDetails();
    return true;
}

} // namespace tanara

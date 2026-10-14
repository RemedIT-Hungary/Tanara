#include "LegacyPeople.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QHash>

namespace tanara::cli::legacy {

QString nameKey(const QString& name) { return name.trimmed().toCaseFolded(); }

QJsonObject readObject(const QString& path, bool* exists, QString* error)
{
    if (exists) *exists = QFileInfo::exists(path);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error && QFileInfo::exists(path)) *error = QStringLiteral("cannot read %1").arg(QDir::toNativeSeparators(path));
        return {};
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("not a JSON object: %1").arg(QDir::toNativeSeparators(path));
        return {};
    }
    if (error) error->clear();
    return doc.object();
}

namespace {

bool containsName(const QStringList& list, const QString& name)
{
    for (const QString& s : list)
        if (nameKey(s) == nameKey(name)) return true;
    return false;
}

// Merges `src` into `dst` (the old PeopleStore's absorb: nickname union, note joined, tags union,
// unknown fields kept where absent).
void absorb(LegacyPerson& dst, const LegacyPerson& src)
{
    for (const QString& a : src.aliases)
        if (nameKey(a) != nameKey(dst.name) && !containsName(dst.aliases, a)) dst.aliases << a;
    if (dst.note.trimmed().isEmpty()) dst.note = src.note;
    else if (!src.note.trimmed().isEmpty() && src.note != dst.note) dst.note += QLatin1Char('\n') + src.note;
    for (const QString& t : src.tags)
        if (!dst.tags.contains(t)) dst.tags << t;
    for (auto it = src.extra.constBegin(); it != src.extra.constEnd(); ++it)
        if (!dst.extra.contains(it.key())) dst.extra.insert(it.key(), it.value());
    dst.listed = dst.listed || src.listed;
}

LegacyPerson recordOf(const QJsonValue& v, bool listed)
{
    LegacyPerson p;
    p.listed = listed;
    if (v.isString()) {
        p.name = v.toString().trimmed();
        return p;
    }
    QJsonObject o = v.toObject();
    p.name = o.take(QStringLiteral("name")).toString().trimmed();
    for (const QJsonValue& a : o.take(QStringLiteral("aliases")).toArray()) {
        const QString s = a.toString().trimmed();
        if (!s.isEmpty() && nameKey(s) != nameKey(p.name) && !containsName(p.aliases, s)) p.aliases << s;
    }
    p.note = o.take(QStringLiteral("note")).toString();
    for (const QJsonValue& t : o.take(QStringLiteral("tags")).toArray()) {
        const QString id = t.toString().trimmed();
        if (!id.isEmpty() && !p.tags.contains(id)) p.tags << id;
    }
    p.extra = o;
    return p;
}

} // namespace

LegacyPeopleFile readPeople(const QString& peopleJson)
{
    LegacyPeopleFile out;
    QJsonObject root = readObject(peopleJson, &out.exists, &out.error);
    if (!out.exists) {
        out.parsed = true;      // no file = no people
    } else if (!out.error.isEmpty()) {
        return out;
    } else {
        out.parsed = true;
        out.version = root.take(QStringLiteral("version")).toInt(0);
        const QJsonValue people = root.take(QStringLiteral("people"));
        if (out.version >= 3 && people.isObject()) {
            out.isCurrent = true;
            return out;
        }
        const QJsonArray unlisted = root.take(QStringLiteral("unlisted")).toArray();
        out.rootExtra = root;
        QHash<QString, int> index;
        auto add = [&](const LegacyPerson& p) {
            if (p.name.isEmpty()) return;
            const QString k = nameKey(p.name);
            const auto it = index.constFind(k);
            if (it == index.constEnd()) {
                index.insert(k, int(out.people.size()));
                out.people.append(p);
            } else {
                absorb(out.people[it.value()], p);
            }
        };
        for (const QJsonValue& v : people.toArray()) add(recordOf(v, true));
        for (const QJsonValue& v : unlisted) add(recordOf(v, false));
    }
    // v1: nicknames / notes in the sibling people-details.json.
    const QString details = QFileInfo(peopleJson).absoluteDir().filePath(QStringLiteral("people-details.json"));
    if (QFileInfo::exists(details) && out.version < 2) {
        bool ex = false;
        QString err;
        const QJsonObject d = readObject(details, &ex, &err);
        QHash<QString, int> index;
        for (int i = 0; i < out.people.size(); ++i) index.insert(nameKey(out.people[i].name), i);
        for (const QJsonValue& v : d.value(QStringLiteral("people")).toArray()) {
            LegacyPerson p = recordOf(v, false);
            if (p.name.isEmpty()) continue;
            const auto it = index.constFind(nameKey(p.name));
            if (it == index.constEnd()) {
                index.insert(nameKey(p.name), int(out.people.size()));
                out.people.append(p);
            } else {
                absorb(out.people[it.value()], p);
            }
        }
    }
    return out;
}

LegacyVoiceprintFile readVoiceprints(const QString& voiceprintsJson)
{
    LegacyVoiceprintFile out;
    const QJsonObject root = readObject(voiceprintsJson, &out.exists, &out.error);
    if (!out.exists) { out.parsed = true; return out; }
    if (!out.error.isEmpty()) return out;
    out.parsed = true;
    const QJsonValue people = root.value(QStringLiteral("people"));
    if (root.value(QStringLiteral("version")).toInt(0) >= 3 && people.isObject()) {
        out.isCurrent = true;
        return out;
    }
    QHash<QString, int> index;
    for (const QJsonValue& v : people.toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(QStringLiteral("name")).toString().trimmed();
        const QJsonArray prints = o.value(QStringLiteral("prints")).toArray();
        if (name.isEmpty() || prints.isEmpty()) continue;
        const auto it = index.constFind(nameKey(name));
        if (it == index.constEnd()) {
            index.insert(nameKey(name), int(out.owners.size()));
            out.owners.append({name, prints});
        } else {
            for (const QJsonValue& p : prints) out.owners[it.value()].prints.append(p);
        }
    }
    return out;
}

} // namespace tanara::cli::legacy

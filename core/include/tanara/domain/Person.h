#pragma once
//
// Domain: a person and their stable identifier.
//
// A person is identified by a PersonId, never by the name. The name (displayName) is only for
// display and search: renaming a person writes one field of their record (people.json), and every
// reference (voiceprints.json, tags.json, meeting.json speakerMap / participants, the speaker
// overlay) keeps pointing at the same id. Two people may carry the same name.
//
// File formats that store PersonIds: people.json v3, voiceprints.json v3, tags.json v3,
// meeting.json v3, transcript.speakers.json v3 (see the stores / repositories).
#include <QHash>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

// Stable person identifier: a UUID v4 string, hyphenated, lower-case, without braces
// (e.g. "3f1c2a8e-5d6b-4c1e-9a7f-0b2c4d6e8f10"). Any non-empty string read from a file is accepted
// as is (fromString); generate() makes new ones.
class PersonId {
public:
    PersonId() = default;
    // The string as stored (trimmed). Empty = null.
    static PersonId fromString(const QString& s);
    static PersonId generate();

    bool isNull() const { return m_value.isEmpty(); }
    const QString& toString() const { return m_value; }

    friend bool operator==(const PersonId& a, const PersonId& b) { return a.m_value == b.m_value; }
    friend bool operator!=(const PersonId& a, const PersonId& b) { return a.m_value != b.m_value; }
    friend bool operator<(const PersonId& a, const PersonId& b) { return a.m_value < b.m_value; }

private:
    QString m_value;
};

inline size_t qHash(const PersonId& id, size_t seed = 0) { return qHash(id.toString(), seed); }

using PersonIds = QVector<PersonId>;

// The current display name of every known person (people.json). A value snapshot: cheap to copy,
// safe to read on any thread. Unknown id -> empty name.
using PersonNames = QHash<PersonId, QString>;

// The display name of `id`, or `fallback` if the id is null or unknown.
inline QString personName(const PersonNames& names, const PersonId& id, const QString& fallback = QString())
{
    if (id.isNull()) return fallback;
    const auto it = names.constFind(id);
    return it == names.constEnd() || it->isEmpty() ? fallback : *it;
}

// One record of people.json.
struct Person {
    PersonId    id;
    QString     displayName;
    QStringList aliases;            // nicknames (search matches these too)
    QString     note;
    QStringList tags;               // manual tags (tagId), in the order applied
    QString     defaultSide;        // learned track side: "local" | "remote" | empty
    bool        defaultSideManual = false;   // set by the user (learning does not overwrite it)
    bool isValid() const { return !id.isNull(); }
};

// The user's own person: the id (people.json "self") and the name (settings). The name also recognises an
// UNMAPPED raw label equal to it (a track labelled with the user's name, e.g. an imported own microphone).
struct SelfPerson {
    PersonId id;
    QString  name;
    bool is(const PersonId& person) const { return !id.isNull() && person == id; }
};

// A person chosen in a picker: an existing person (id), or a new one to create (newName, null id).
// Both empty = anonymous.
struct PersonChoice {
    PersonId id;
    QString  newName;
    static PersonChoice existing(const PersonId& id) { return PersonChoice{id, {}}; }
    static PersonChoice create(const QString& name) { return PersonChoice{{}, name.trimmed()}; }
    bool isAnonymous() const { return id.isNull() && newName.trimmed().isEmpty(); }
};

namespace persons {

// Comparison form of a person name (trimmed, case-insensitive) for name lookups.
QString nameKey(const QString& name);
bool sameName(const QString& a, const QString& b);

} // namespace persons

} // namespace tanara

Q_DECLARE_METATYPE(tanara::PersonId)
Q_DECLARE_METATYPE(tanara::Person)

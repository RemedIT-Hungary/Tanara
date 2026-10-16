#pragma once
//
// PeopleStore: the global store of people (<metadata folder>/people.json): ONE record per person,
// keyed by a stable PersonId (domain/Person.h). Every other file refers to people by this id
// (voiceprints.json, tags.json, meeting.json, the speaker overlay); the name is a field of the record,
// so a rename writes exactly one field. Two people may have the same name.
//
// File shape (version 3):
//   {
//     "version": 3,
//     "self": "<personId>",                         // the user's own person (only if set)
//     "people": {
//       "<personId>": {"name": "...", "aliases": ["..."], "note": "...", "tags": ["<tagId>"],
//                      "defaultSide": "local" | "remote", "defaultSideManual": true}, ...
//     }
//   }
// - "tags": ids of the tags set by hand on the person (from the tags.json set; only if any).
// - "defaultSide": the learned track side (SideAnalysis); "defaultSideManual" protects a value the
//   user set from being overwritten by learning.
// - Unknown (future) fields are preserved, both in the root and in the records.
//
// Older shapes (version 1/2: name-keyed lists) are NOT read: the store then stays empty and READ-ONLY
// (nothing is written over the old file) and logs that the data must be converted once with
// `tanara-cli migrate person-ids` (legacy() == true).
//
// Several processes may write it: every modification rereads the fresh state from disk under lock (if
// it changed), applies the change to that, and writes atomically (see foundation/SharedFile.h).
#include "tanara/domain/Person.h"
#include "tanara/foundation/SharedFile.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::library {

class PeopleStore {
public:
    static constexpr int kFormatVersion = 3;

    explicit PeopleStore(const QString& filePath = QString());  // empty = <metadata folder>/people.json

    // ---- reading ----
    // Every person, sorted by name (case-insensitive), then by id.
    QVector<Person> people() const;
    PersonIds ids() const;
    bool contains(const PersonId& id) const;
    // The record; an invalid Person (null id) for an unknown id.
    Person person(const PersonId& id) const;
    QString displayName(const PersonId& id) const;   // unknown = empty
    PersonNames names() const;                        // id -> name, every person
    // People whose NAME matches (case-insensitive, trimmed; nicknames do not count), in people() order.
    PersonIds findByName(const QString& name) const;
    // The first person with this name (people() order); null if none.
    PersonId idForName(const QString& name) const;
    // Boundary adapter for pickers / text fields that hand over either a PersonId or a typed name: a known
    // id -> that person; otherwise the first person with that name; otherwise a new person to create.
    PersonChoice choiceFor(const QString& idOrName) const;

    // ---- the user's own person ----
    PersonId selfId() const { return m_self; }
    void setSelfId(const PersonId& id);

    // ---- writing ----
    // A new person (always a new id). Empty name = null id, nothing written.
    PersonId create(const QString& name);
    // The first person with this name, or a new one.
    PersonId ensure(const QString& name);
    // The chosen person: the existing id, or a newly created one (null for an anonymous choice).
    PersonId resolve(const PersonChoice& choice);
    // Puts a record back as it was (undo of a merge / delete). false if the id is null.
    bool restore(const Person& person);
    // Renames: writes the "name" field. false if unknown / empty name / unchanged.
    bool rename(const PersonId& id, const QString& newName);
    // Deletes the record. Clears "self" if it was the user's. true if it existed.
    bool remove(const PersonId& id);
    // Merges `drop` into `keep` (both records must exist): union of nicknames (the dropped name becomes a
    // nickname of `keep` when dropNameAsAlias and it differs), note (keep's; drop's if keep has none; both
    // one under the other), manual tags (union), learned side (keep's; drop's if keep has none). The
    // `drop` record is deleted; "self" moves to `keep` if it was `drop`. false if not both exist.
    bool merge(const PersonId& keep, const PersonId& drop, bool dropNameAsAlias = true);

    // ---- nicknames, note ----
    QStringList aliases(const PersonId& id) const { return person(id).aliases; }
    // Adds a nickname. false: unknown person, empty, equal to the name, or already present (case-insensitive).
    bool addAlias(const PersonId& id, const QString& alias);
    // Deletes a nickname. Returns its position in the list (-1 = there was none).
    int removeAlias(const PersonId& id, const QString& alias);
    // Puts a nickname back at its original place (undo).
    bool insertAlias(const PersonId& id, const QString& alias, int index);
    bool setNote(const PersonId& id, const QString& note);

    // ---- manual tags (tagIds; handled by TagService, see library/tags/TagService.h) ----
    QStringList tags(const PersonId& id) const { return person(id).tags; }
    // Replaces the tags (no repeats, order kept). true if changed.
    bool setTags(const PersonId& id, const QStringList& tagIds);
    // Every person with tags.
    QHash<PersonId, QStringList> allTags() const;

    // ---- learned track side (from confirmed lines / manual track assignment; SideAnalysis) ----
    QString defaultSide(const PersonId& id) const { return person(id).defaultSide; }
    bool defaultSideManual(const PersonId& id) const { return person(id).defaultSideManual; }
    // An empty side deletes the value. true if changed. manual: written by the user (People window); the
    // manual flag protects it from learned overwrite (a call without manual then changes nothing); a manual
    // clear also clears the flag.
    bool setDefaultSide(const PersonId& id, const QString& side, bool manual = false);
    QHash<PersonId, QString> defaultSides() const;

    // If the file changed on disk (another process wrote it), rereads it. Call before reading.
    void refresh() { reloadIfChanged(); }

    // The file on disk is in an older format: the store is empty and read-only until
    // `tanara-cli migrate person-ids` converts it.
    bool legacy() const { return m_legacy; }
    int fileVersion() const { return m_version; }

    QString filePath() const { return m_filePath; }
    // The last modification was refused because another process held the file's lock for too long
    // (SharedFileLock); nothing was changed. Reset by the next modification that gets the lock.
    bool lockFailed() const { return m_lockFailed; }

private:
    struct Entry {
        Person      p;
        QJsonObject raw;            // unknown fields of the record
    };

    int indexOf(const PersonId& id) const;
    void sortEntries();
    void load();
    void reloadIfChanged();
    bool persist();
    bool acquired(const SharedFileLock& lock, const char* operation);

    QString        m_filePath;
    QVector<Entry> m_entries;
    PersonId       m_self;
    QJsonObject    m_rootExtra;                 // unknown fields of the root
    int            m_version = 0;               // version of the file read (0 = none / unknown)
    FileStamp      m_stamp;                     // state of the file at the last load / save
    bool           m_corrupt = false;           // the file on disk is unparseable (set aside on save)
    bool           m_legacy = false;            // old format on disk: read-only
    bool           m_lockFailed = false;        // the last modification could not get the lock
};

} // namespace tanara::library

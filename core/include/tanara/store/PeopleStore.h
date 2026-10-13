#pragma once
//
// PeopleStore: the global store of people (<metadata folder>/people.json): ONE record per
// person (name, nicknames, free-text note), reused across meetings for speaker rename and the
// person pickers. The meeting-specific raw -> name mapping lives in Meeting.speakerMap, not here;
// voiceprints are in voiceprints.json, bound to the name.
//
// File shape (version 2):
//   {
//     "version": 2,
//     "people":   [ {"name": "...", "aliases": ["..."], "note": "...", "tags": ["<tagId>"]}, ... ],   // the name list
//     "unlisted": [ {"name": "...", "aliases": ["..."], "note": "..."}, ... ]    // only if there are any
//   }
// - "tags": ids of the tags set by hand on the person (from the tags.json set; only if any).
//   They go with the record on rename / merge (a union on merge).
// - The person's identifier is the NAME (case-insensitive), as in voiceprints.json.
// - Unknown (future) fields are preserved, both in the root and in the records.
// - "unlisted": data of a name that is NOT on the name list, e.g. it only has a voiceprint, or its
//   name was rewritten from outside and its nicknames / note stayed under the old name. These are not
//   lost: details() finds them by name, and if the name is put on the list (again) (add / rename), the
//   record returns to the "people" array. An empty such record is not stored.
//
// One-time, automatic migration from the old shape (at load): the old people.json was a plain name
// list ({"people": ["A", "B"]}), and nicknames and notes lived in a sibling file, people-details.json.
// At load the two merge, the new shape is written to disk under lock, atomically, and people-details.json
// is deleted ONLY after that. If the write fails, both old files stay untouched, the store keeps
// working in memory, and the next successful save completes the migration. Details in the .cpp.
//
// Several processes may write it: every modification rereads the fresh state from disk under lock (if
// it changed), applies the change to that, and writes atomically (see store/SharedFile.h).
#include "tanara/store/SharedFile.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

struct PersonDetails {
    QString     name;
    QStringList aliases;
    QString     note;
    QStringList tags;       // manual tags (tagId), in the order applied
    bool isEmpty() const { return aliases.isEmpty() && note.isEmpty() && tags.isEmpty(); }
};

class PeopleStore {
public:
    explicit PeopleStore(const QString& filePath = QString());  // empty = <metadata folder>/people.json

    // ---- name list ----
    QStringList names() const;
    // Adds it if not there yet (case-insensitive). If an unlisted record belongs to the name, it returns
    // to the list (with its nicknames and note).
    void add(const QString& name);
    // Rename: the WHOLE record moves to the new name. If the new name already has a record, the two
    // merge (union of nicknames; note: the target's, or the source's if empty; if both exist, one
    // under the other). oldNameAsAlias: the old name becomes a nickname (not if it differs from the new
    // one only in case). The new name always goes on the list.
    void rename(const QString& oldName, const QString& newName, bool oldNameAsAlias = false);
    // Deleting the person: the whole record (nicknames, note) goes away.
    void remove(const QString& name);
    // Only takes it off the name list (e.g. undoing a "new person" step): if it has nicknames / a note,
    // they stay as an unlisted record.
    void unlist(const QString& name);

    // ---- nicknames, note (for any name, even if not on the list) ----
    PersonDetails details(const QString& name) const;   // unknown name = empty (name filled in)
    QStringList aliases(const QString& name) const { return details(name).aliases; }
    // Adds a nickname. false: empty, equal to the name, or already present (case-insensitive).
    bool addAlias(const QString& name, const QString& alias);
    // Deletes a nickname. Returns the deleted nickname's position in the list (-1 = there was none).
    int removeAlias(const QString& name, const QString& alias);
    // Puts a nickname back at its original place (undo).
    bool insertAlias(const QString& name, const QString& alias, int index);
    void setNote(const QString& name, const QString& note);

    // ---- manual tags (tagIds; handled by TagService, see tags/TagService.h) ----
    QStringList tags(const QString& name) const { return details(name).tags; }
    // Replaces the tags (no repeats, order kept). For an unknown name it creates an unlisted record.
    // true if changed.
    bool setTags(const QString& name, const QStringList& tagIds);
    // Every record with tags (name -> tags), the unlisted ones too.
    QHash<QString, QStringList> allTags() const;
    // ---- learned track side (from confirmed lines / manual track assignment; SideAnalysis) ----
    // The record's "defaultSide" field: "local" | "remote"; empty = no learned value.
    QString defaultSide(const QString& name) const;
    // An empty side deletes the field. true if changed. manual: written by the user (People window);
    // the "defaultSideManual" flag protects it from learned overwrite (a call without manual then changes
    // nothing); a manual clear also clears the flag (learning may write it again).
    bool setDefaultSide(const QString& name, const QString& side, bool manual = false);
    bool defaultSideManual(const QString& name) const;
    // Every learned value: name -> side.
    QHash<QString, QString> defaultSides() const;

    // Names of the records that are off the list but carry data (see above: "unlisted").
    QStringList unlistedNames() const;

    // If the file changed on disk (another process wrote it), rereads it. Call before reading.
    void refresh() { reloadIfChanged(); }

    // True while the migration from the old shape is not on disk yet (the write failed): the store works
    // in memory, the old files are untouched.
    bool migrationPending() const { return m_migrationPending; }

    QString filePath() const { return m_filePath; }
    // Location of the old sibling file (people-details.json) in the same folder.
    QString legacyDetailsPath() const;

private:
    struct Entry {
        PersonDetails d;
        QJsonObject   raw;            // unknown fields of the record
        bool          listed = true;  // is on the name list
    };
    // Fate of the old people-details.json after the next successful save.
    enum class LegacyDetails { None, Remove, SetAsideCorrupt, SetAsideLeftover };

    int indexOf(const QString& name) const;
    Entry& ensure(const QString& name);     // if missing: creates it as an unlisted record
    void sortEntries();
    void load();
    void reloadIfChanged();
    bool persist();
    void migrateIfNeeded();
    void finishLegacyDetails();

    QString        m_filePath;
    QVector<Entry> m_entries;
    QJsonObject    m_rootExtra;                 // unknown fields of the root
    int            m_version = 0;               // version of the file read (0 = old shape / none)
    FileStamp      m_stamp;                     // state of the file at the last load / save
    bool           m_corrupt = false;           // the file on disk is unparseable (set aside on save)
    bool           m_migrationPending = false;  // the old shape is still on disk
    LegacyDetails  m_legacy = LegacyDetails::None;
};

} // namespace tanara

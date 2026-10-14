// tanara-cli migrate person-ids. All output is English, no tr().
#include "MigrateCommands.h"

#include "legacy/LegacyPeople.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/domain/Person.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/store/PeopleStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace tanara::cli {

namespace {

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

QString native(const QString& p) { return QDir::toNativeSeparators(p); }

QString optionValue(const QStringList& args, const QString& name)
{
    const int i = args.indexOf(name);
    return i >= 0 ? args.value(i + 1) : QString();
}

QByteArray jsonBytes(const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Indented); }

const QLatin1String kSelf("self");
const QLatin1String kVersion("version");

// ---- the person directory ---------------------------------------------------------------------

struct DirPerson {
    PersonId    id;
    QString     name;
    QStringList aliases;
    QString     note;
    QStringList tags;
    QJsonObject extra;          // other record fields, written verbatim (defaultSide, defaultSideManual ...)
    QString     source;         // "people.json" | "voiceprints.json" | "settings (own name)" | "meetings"
};

struct Unmatched {
    QString name;
    QString where;              // the first file it was found in
};

class Directory {
public:
    QVector<DirPerson> people;
    PersonId self;
    bool fromCurrent = false;   // people.json was already v3
    int created = 0;            // people added in this run (not from the old people.json)
    QVector<Unmatched> unmatched;       // names with no person: a new person was created for them
    QStringList ambiguous;              // a nickname of several people (the first was taken)

    int indexOf(const PersonId& id) const { return m_byId.value(id, -1); }
    bool knows(const PersonId& id) const { return m_byId.contains(id); }

    PersonId add(DirPerson p)
    {
        if (p.id.isNull()) p.id = PersonId::generate();
        m_byId.insert(p.id, int(people.size()));
        const QString k = legacy::nameKey(p.name);
        if (!k.isEmpty() && !m_byName.contains(k)) m_byName.insert(k, p.id);
        for (const QString& a : p.aliases) m_byAlias[legacy::nameKey(a)].append(p.id);
        people.append(p);
        return p.id;
    }

    // Only an existing person: by its own name, else by a nickname (several -> the first, reported).
    PersonId find(const QString& name)
    {
        const QString k = legacy::nameKey(name);
        if (k.isEmpty()) return {};
        if (const auto it = m_byName.constFind(k); it != m_byName.constEnd()) return *it;
        const PersonIds viaAlias = m_byAlias.value(k);
        if (viaAlias.isEmpty()) return {};
        if (viaAlias.size() > 1 && !ambiguous.contains(name.trimmed())) ambiguous << name.trimmed();
        return viaAlias.first();
    }

    // A name from the data: an existing person, else a new one (reported as unmatched).
    PersonId resolve(const QString& name, const QString& where)
    {
        const QString n = name.trimmed();
        if (n.isEmpty()) return {};
        const PersonId have = find(n);
        if (!have.isNull()) return have;
        DirPerson p;
        p.name = n;
        p.source = QStringLiteral("meetings");
        ++created;
        unmatched.append({n, where});
        return add(p);
    }

    // A value read from an ALREADY converted file: a known id stays; a leftover name is resolved.
    PersonId repair(const QString& value, const QString& where, bool* changed)
    {
        const PersonId asId = PersonId::fromString(value);
        if (asId.isNull() || knows(asId)) return asId;
        *changed = true;
        return resolve(value, where);
    }

private:
    QHash<PersonId, int> m_byId;
    QHash<QString, PersonId> m_byName;
    QHash<QString, PersonIds> m_byAlias;
};

QJsonObject peopleJson(const Directory& dir, const QJsonObject& rootExtra)
{
    QJsonObject people;
    for (const DirPerson& p : dir.people) {
        QJsonObject o = p.extra;
        o[QStringLiteral("name")] = p.name;
        o[QStringLiteral("aliases")] = QJsonArray::fromStringList(p.aliases);
        o[QStringLiteral("note")] = p.note;
        if (!p.tags.isEmpty()) o[QStringLiteral("tags")] = QJsonArray::fromStringList(p.tags);
        people.insert(p.id.toString(), o);
    }
    QJsonObject root = rootExtra;
    root[kVersion] = library::PeopleStore::kFormatVersion;
    if (!dir.self.isNull()) root[kSelf] = dir.self.toString();
    root[QStringLiteral("people")] = people;
    return root;
}

// ---- planned changes ------------------------------------------------------------------------------

struct FileChange {
    QString path;           // absolute
    QString backupRel;      // path inside the backup folder
    QByteArray content;     // the new content
};

struct Stats {
    int meetings = 0, meetingsConverted = 0, meetingsRepaired = 0, meetingsCurrent = 0, meetingsUnreadable = 0;
    int speakerMapEntries = 0, droppedSelfMappings = 0, fixedTrackMappings = 0;
    int participants = 0, approvalEntries = 0;
    int overlays = 0, overlayPeople = 0, droppedIdentifications = 0;
    int analyses = 0, backupsConverted = 0;
    int voiceprintPeople = 0, voiceprints = 0;
    int rejections = 0, droppedRejections = 0;
};

class Converter {
public:
    Directory dir;
    Stats st;
    QVector<FileChange> changes;
    QStringList notes;

    // A meeting.json (old -> v3, or a repair of leftover names in a v3 file).
    void meetingJson(const QString& folder, const QString& backupBase)
    {
        const QString path = library::MeetingLayout::path(folder, library::MeetingLayout::meetingJson());
        bool exists = false;
        QString error;
        QJsonObject o = legacy::readObject(path, &exists, &error);
        if (!error.isEmpty()) { ++st.meetingsUnreadable; notes << error; return; }
        ++st.meetings;
        const QString where = QFileInfo(folder).fileName();
        const bool current = o.value(kVersion).toInt(0) >= 3;
        bool changed = false;
        QJsonObject sm = o.value(QStringLiteral("speakerMap")).toObject();
        QJsonObject newMap;
        for (auto it = sm.constBegin(); it != sm.constEnd(); ++it) {
            const QString value = it.value().toString().trimmed();
            if (current) {
                bool rep = false;
                const PersonId id = dir.repair(value, where, &rep);
                changed = changed || rep;
                if (!id.isNull()) newMap.insert(it.key(), id.toString());
                continue;
            }
            changed = true;
            if (value.isEmpty()) continue;
            PersonId id;
            if (legacy::nameKey(value) == legacy::nameKey(it.key())) {
                // raw -> itself: the old app showed it as anonymous unless it was a person's (fixed) name.
                id = dir.find(value);
                if (id.isNull()) { ++st.droppedSelfMappings; continue; }
            } else {
                id = dir.resolve(value, where);
            }
            newMap.insert(it.key(), id.toString());
            ++st.speakerMapEntries;
        }
        if (!current) {
            // A fixed-speaker track labelled with a person's name (e.g. an imported own microphone): the label
            // was the person; now the speakerMap says so.
            for (const QJsonValue& tv : o.value(QStringLiteral("tracks")).toArray()) {
                const QJsonObject t = tv.toObject();
                const QString label = t.value(QStringLiteral("speakerLabel")).toString().trimmed();
                if (!t.value(QStringLiteral("fixedSpeaker")).toBool() || label.isEmpty() || newMap.contains(label)) continue;
                const PersonId id = dir.find(label);
                if (id.isNull()) continue;
                newMap.insert(label, id.toString());
                ++st.fixedTrackMappings;
            }
        }
        o[QStringLiteral("speakerMap")] = newMap;

        QJsonArray ps = o.value(QStringLiteral("participants")).toArray();
        for (int i = 0; i < ps.size(); ++i) {
            QJsonObject p = ps.at(i).toObject();
            if (p.contains(QStringLiteral("personName"))) {
                const PersonId id = dir.resolve(p.take(QStringLiteral("personName")).toString(), where);
                if (!id.isNull()) p[QStringLiteral("personId")] = id.toString();
                ++st.participants;
                changed = true;
            } else if (p.contains(QStringLiteral("personId"))) {
                bool rep = false;
                const PersonId id = dir.repair(p.value(QStringLiteral("personId")).toString(), where, &rep);
                if (rep) { p[QStringLiteral("personId")] = id.toString(); changed = true; }
            }
            ps[i] = p;
        }
        if (o.contains(QStringLiteral("participants"))) o[QStringLiteral("participants")] = ps;

        if (o.value(QStringLiteral("participantApproval")).isObject()) {
            QJsonObject ap = o.value(QStringLiteral("participantApproval")).toObject();
            QJsonArray cs = ap.value(QStringLiteral("candidates")).toArray();
            for (int i = 0; i < cs.size(); ++i) {
                QJsonObject c = cs.at(i).toObject();
                if (!c.contains(QStringLiteral("personName"))) continue;
                const PersonId id = dir.resolve(c.take(QStringLiteral("personName")).toString(), where);
                if (!id.isNull()) c[QStringLiteral("personId")] = id.toString();
                cs[i] = c;
                ++st.approvalEntries;
                changed = true;
            }
            ap[QStringLiteral("candidates")] = cs;
            o[QStringLiteral("participantApproval")] = ap;
        }
        if (!current) { o[kVersion] = 3; changed = true; }
        if (current && !changed) { ++st.meetingsCurrent; return; }
        if (current) ++st.meetingsRepaired;
        else ++st.meetingsConverted;
        changes.append({path, backupBase + library::MeetingLayout::meetingJson(), jsonBytes(o)});
    }

    // transcript.speakers.json (old: "person" names) -> v3 ("personId").
    bool overlay(const QString& path, const QString& backupRel, const QString& where)
    {
        bool exists = false;
        QString error;
        QJsonObject o = legacy::readObject(path, &exists, &error);
        if (!exists) return false;
        if (!error.isEmpty()) { notes << error; return false; }
        if (o.value(kVersion).toInt(0) >= 3) return false;
        QJsonArray ps = o.value(QStringLiteral("participants")).toArray();
        for (int i = 0; i < ps.size(); ++i) {
            QJsonObject p = ps.at(i).toObject();
            const QString name = p.take(QStringLiteral("person")).toString();
            const PersonId id = dir.resolve(name, where);
            if (!id.isNull()) { p[QStringLiteral("personId")] = id.toString(); ++st.overlayPeople; }
            ps[i] = p;
        }
        o[QStringLiteral("participants")] = ps;
        QJsonObject ident = o.value(QStringLiteral("identified")).toObject();
        QJsonObject newIdent;
        for (auto it = ident.constBegin(); it != ident.constEnd(); ++it) {
            QJsonObject e = it.value().toObject();
            // An identification came from the voiceprint DB: an unknown person is dropped, not created.
            const PersonId id = dir.find(e.take(QStringLiteral("person")).toString());
            if (id.isNull()) { ++st.droppedIdentifications; continue; }
            e[QStringLiteral("personId")] = id.toString();
            newIdent.insert(it.key(), e);
        }
        o[QStringLiteral("identified")] = newIdent;
        o[kVersion] = 3;
        changes.append({path, backupRel, jsonBytes(o)});
        return true;
    }

    // participants.analysis.json: clusters[].matchName -> matchPersonId.
    void analysis(const QString& folder, const QString& backupBase)
    {
        const QString file = library::MeetingLayout::participantsAnalysisJson();
        const QString path = library::MeetingLayout::path(folder, file);
        bool exists = false;
        QString error;
        QJsonObject o = legacy::readObject(path, &exists, &error);
        if (!exists || !error.isEmpty()) return;
        QJsonArray cs = o.value(QStringLiteral("clusters")).toArray();
        bool changed = false;
        for (int i = 0; i < cs.size(); ++i) {
            QJsonObject c = cs.at(i).toObject();
            if (!c.contains(QStringLiteral("matchName"))) continue;
            const PersonId id = dir.find(c.take(QStringLiteral("matchName")).toString());
            if (!id.isNull()) c[QStringLiteral("matchPersonId")] = id.toString();
            cs[i] = c;
            changed = true;
        }
        if (!changed) return;
        o[QStringLiteral("clusters")] = cs;
        ++st.analyses;
        changes.append({path, backupBase + file, jsonBytes(o)});
    }

    // transcript-backup-*/: the speakerMap.json copy and the overlay copy.
    void transcriptBackups(const QString& folder, const QString& backupBase)
    {
        const QDir dirOf(folder);
        const QString prefix = library::MeetingLayout::transcriptBackupPrefix();
        for (const QFileInfo& fi : dirOf.entryInfoList({prefix + QLatin1Char('*')}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString sub = fi.fileName() + QLatin1Char('/');
            const QString where = QFileInfo(folder).fileName() + QLatin1Char('/') + fi.fileName();
            const QString smPath = QDir(fi.absoluteFilePath()).filePath(library::MeetingLayout::backupSpeakerMapJson());
            bool exists = false;
            QString error;
            QJsonObject o = legacy::readObject(smPath, &exists, &error);
            if (exists && error.isEmpty() && o.value(kVersion).toInt(0) < 3) {
                QJsonObject sm = o.value(QStringLiteral("speakerMap")).toObject(), newMap;
                for (auto it = sm.constBegin(); it != sm.constEnd(); ++it) {
                    const QString v = it.value().toString().trimmed();
                    if (v.isEmpty() || legacy::nameKey(v) == legacy::nameKey(it.key())) continue;
                    newMap.insert(it.key(), dir.resolve(v, where).toString());
                }
                o[QStringLiteral("speakerMap")] = newMap;
                o[kVersion] = 3;
                changes.append({smPath, backupBase + sub + library::MeetingLayout::backupSpeakerMapJson(), jsonBytes(o)});
                ++st.backupsConverted;
            }
            const QString ovPath = QDir(fi.absoluteFilePath()).filePath(library::MeetingLayout::overlayJson());
            if (overlay(ovPath, backupBase + sub + library::MeetingLayout::overlayJson(), where)) ++st.backupsConverted;
        }
    }
};

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) return false;
    QFile check(path);
    return check.open(QIODevice::ReadOnly) && check.readAll() == bytes;
}

bool copyVerified(const QString& from, const QString& to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QFileInfo::exists(to) && !QFile::remove(to)) return false;
    if (!QFile::copy(from, to)) return false;
    QFile a(from), b(to);
    return a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly) && a.readAll() == b.readAll();
}

int usage()
{
    err() << "Usage: migrate person-ids [--dry-run] [--backup-dir <dir>]\n";
    return 2;
}

} // namespace

int runMigrateCommand(const QStringList& args)
{
    if (args.value(2) != QLatin1String("person-ids")) return usage();
    for (int i = 3; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == QLatin1String("--dry-run")) continue;
        if (a == QLatin1String("--backup-dir") && i + 1 < args.size()) { ++i; continue; }
        err() << "Unknown argument: " << a << "\n";
        return usage();
    }
    const bool dryRun = args.contains(QStringLiteral("--dry-run"));
    const QString backupOpt = optionValue(args, QStringLiteral("--backup-dir"));

    const app::SettingsManager sm;
    const paths::AppPaths& ap = sm.paths();
    const QString dataDir = ap.dataDir();
    const QString peoplePath = ap.dataFile(paths::files::People);
    const QString voicePath = ap.dataFile(paths::files::Voiceprints);
    const QString tagsPath = ap.dataFile(paths::files::Tags);
    const QStringList roots = ap.recordingRoots();
    const QString selfName = sm.settings().recording.userSpeakerName.trimmed();

    out() << (dryRun ? "DRY RUN - nothing is written.\n" : "")
          << "Close Tanara (the app and the watcher) before converting.\n"
          << "data folder:     " << native(dataDir) << "\n";
    for (const QString& r : roots) out() << "recording root:  " << native(r) << "\n";
    out() << "\n";

    Converter cv;
    Directory& dir = cv.dir;
    QJsonObject peopleRootExtra;
    bool peopleChanged = false;

    // ---- 1. people.json -> the directory ----
    const legacy::LegacyPeopleFile lp = legacy::readPeople(peoplePath);
    if (!lp.parsed) { err() << "ERROR: " << lp.error << "\n"; return 1; }
    if (lp.isCurrent) {
        dir.fromCurrent = true;
        bool ex = false;
        QString e;
        QJsonObject root = legacy::readObject(peoplePath, &ex, &e);
        root.remove(kVersion);
        dir.self = PersonId::fromString(root.take(kSelf).toString());
        const QJsonObject people = root.take(QStringLiteral("people")).toObject();
        peopleRootExtra = root;
        for (auto it = people.constBegin(); it != people.constEnd(); ++it) {
            QJsonObject o = it.value().toObject();
            DirPerson p;
            p.id = PersonId::fromString(it.key());
            if (p.id.isNull()) continue;
            p.name = o.take(QStringLiteral("name")).toString();
            for (const QJsonValue& a : o.take(QStringLiteral("aliases")).toArray()) p.aliases << a.toString();
            p.note = o.take(QStringLiteral("note")).toString();
            for (const QJsonValue& t : o.take(QStringLiteral("tags")).toArray()) p.tags << t.toString();
            p.extra = o;
            p.source = QStringLiteral("people.json");
            dir.add(p);
        }
        out() << "people.json:     already converted (version " << lp.version << "), " << dir.people.size()
              << " people\n";
    } else {
        peopleChanged = lp.exists;
        peopleRootExtra = lp.rootExtra;
        for (const legacy::LegacyPerson& l : lp.people) {
            DirPerson p;
            p.name = l.name;
            p.aliases = l.aliases;
            p.note = l.note;
            p.tags = l.tags;
            p.extra = l.extra;
            p.source = QStringLiteral("people.json");
            dir.add(p);
        }
        out() << "people.json:     " << (lp.exists ? QStringLiteral("old format (version %1), %2 people").arg(lp.version).arg(lp.people.size())
                                                  : QStringLiteral("missing (no people yet)"))
              << "\n";
    }

    // ---- 2. voiceprints.json: an owner without a record becomes a person ----
    const legacy::LegacyVoiceprintFile lv = legacy::readVoiceprints(voicePath);
    if (!lv.parsed) { err() << "ERROR: " << lv.error << "\n"; return 1; }
    QJsonObject voiceOut;
    bool voiceChanged = false;
    if (lv.isCurrent) {
        out() << "voiceprints.json: already converted\n";
    } else if (lv.exists) {
        QJsonObject byId;
        for (const legacy::LegacyVoiceprintOwner& o : lv.owners) {
            PersonId id = dir.find(o.name);
            if (id.isNull()) {
                DirPerson p;
                p.name = o.name;
                p.source = QStringLiteral("voiceprints.json");
                id = dir.add(p);
                ++dir.created;
            }
            QJsonArray prints = byId.value(id.toString()).toArray();
            for (const QJsonValue& v : o.prints) prints.append(v);
            byId.insert(id.toString(), prints);
            ++cv.st.voiceprintPeople;
            cv.st.voiceprints += int(o.prints.size());
        }
        voiceOut = QJsonObject{{kVersion, 3}, {QStringLiteral("people"), byId}};
        voiceChanged = true;
        out() << "voiceprints.json: old format, " << cv.st.voiceprints << " voiceprints of "
              << cv.st.voiceprintPeople << " people\n";
    } else {
        out() << "voiceprints.json: missing (no voiceprints yet)\n";
    }

    // ---- 3. the own person (settings' name) ----
    if (dir.self.isNull() || !dir.knows(dir.self)) {
        if (!selfName.isEmpty()) {
            PersonId id = dir.find(selfName);
            if (id.isNull()) {
                DirPerson p;
                p.name = selfName;
                p.source = QStringLiteral("settings (own name)");
                id = dir.add(p);
                ++dir.created;
            }
            dir.self = id;
            peopleChanged = true;
        }
    }

    // ---- 4. meetings of every root ----
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH-mm-ss"));
    for (int ri = 0; ri < roots.size(); ++ri) {
        const QDir root(roots.at(ri));
        if (!root.exists()) continue;
        for (const QFileInfo& fi : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString folder = fi.absoluteFilePath();
            if (!QFileInfo::exists(library::MeetingLayout::path(folder, library::MeetingLayout::meetingJson()))) continue;
            const QString base = QStringLiteral("roots/%1/%2/").arg(ri).arg(fi.fileName());
            cv.meetingJson(folder, base);
            if (cv.overlay(library::MeetingLayout::path(folder, library::MeetingLayout::overlayJson()),
                           base + library::MeetingLayout::overlayJson(), fi.fileName()))
                ++cv.st.overlays;
            cv.analysis(folder, base);
            cv.transcriptBackups(folder, base);
        }
    }

    // ---- 5. tags.json: person rejections by name -> by id ----
    QJsonObject tagsOut;
    bool tagsChanged = false;
    {
        bool ex = false;
        QString e;
        QJsonObject t = legacy::readObject(tagsPath, &ex, &e);
        if (ex && !e.isEmpty()) { err() << "ERROR: " << e << "\n"; return 1; }
        if (ex && t.value(kVersion).toInt(0) < 2) {
            QJsonArray rej = t.value(QStringLiteral("rejected")).toArray(), newRej;
            for (const QJsonValue& v : rej) {
                QJsonObject r = v.toObject();
                if (r.contains(QStringLiteral("person"))) {
                    const PersonId id = dir.find(r.take(QStringLiteral("person")).toString());
                    if (id.isNull()) { ++cv.st.droppedRejections; continue; }
                    r[QStringLiteral("personId")] = id.toString();
                    ++cv.st.rejections;
                }
                newRej.append(r);
            }
            t[QStringLiteral("rejected")] = newRej;
            t[kVersion] = 2;
            tagsOut = t;
            tagsChanged = true;
        }
    }
    if (dir.created > 0) peopleChanged = true;

    // ---- the plan ----
    QVector<FileChange> all;
    if (peopleChanged) all.append({peoplePath, QStringLiteral("data/people.json"), jsonBytes(peopleJson(dir, peopleRootExtra))});
    if (voiceChanged) all.append({voicePath, QStringLiteral("data/voiceprints.json"), jsonBytes(voiceOut)});
    if (tagsChanged) all.append({tagsPath, QStringLiteral("data/tags.json"), jsonBytes(tagsOut)});
    all += cv.changes;

    const Stats& s = cv.st;
    out() << "tags.json:       " << (tagsChanged ? QStringLiteral("%1 person rejections converted, %2 dropped (unknown person)")
                                                       .arg(s.rejections).arg(s.droppedRejections)
                                                 : QStringLiteral("nothing to convert")) << "\n";
    out() << "\nPeople after conversion: " << dir.people.size() << " (" << dir.created << " new)\n";
    out() << "  own person:    " << (dir.self.isNull() ? QStringLiteral("(none - set your name in Settings)")
                                                       : QStringLiteral("%1  [%2]").arg(dir.people.value(dir.indexOf(dir.self)).name, dir.self.toString()))
          << "\n";
    for (const DirPerson& p : dir.people)
        if (p.source != QLatin1String("people.json"))
            out() << "  new person from " << p.source << ": " << p.name << "\n";
    out() << "\nMeetings: " << s.meetings << " scanned, " << s.meetingsConverted << " converted, "
          << s.meetingsRepaired << " repaired (already v3, leftover names), " << s.meetingsCurrent
          << " already converted" << (s.meetingsUnreadable ? QStringLiteral(", %1 unreadable").arg(s.meetingsUnreadable) : QString())
          << "\n";
    out() << "  speakerMap entries: " << s.speakerMapEntries << " (+" << s.fixedTrackMappings
          << " own-track labels mapped, " << s.droppedSelfMappings << " unnamed raw->raw entries dropped)\n";
    out() << "  participants: " << s.participants << ", approval entries: " << s.approvalEntries << "\n";
    out() << "  speaker overlays: " << s.overlays << " (people " << s.overlayPeople << ", "
          << s.droppedIdentifications << " identifications of unknown people dropped)\n";
    out() << "  participant analyses: " << s.analyses << ", transcript-backup copies: " << s.backupsConverted << "\n";
    if (!dir.unmatched.isEmpty()) {
        out() << "\nNames not found in people.json / nicknames (a new person was created for each):\n";
        for (const Unmatched& u : dir.unmatched) out() << "  " << u.name << "   (first seen: " << u.where << ")\n";
    }
    if (!dir.ambiguous.isEmpty())
        out() << "\nNicknames of several people (the first person was used): " << dir.ambiguous.join(QStringLiteral(", ")) << "\n";
    for (const QString& n : std::as_const(cv.notes)) out() << "warning: " << n << "\n";
    out() << "\nFiles to write: " << all.size() << "\n";
    if (all.isEmpty()) {
        out() << "Nothing to convert: the data is already in the PersonId format.\n";
        out().flush();
        return 0;
    }
    if (dryRun) {
        for (const FileChange& c : std::as_const(all)) out() << "  " << native(c.path) << "\n";
        out().flush();
        return 0;
    }

    // ---- backup (every file that will change), verified, BEFORE any write ----
    QString backupDir = backupOpt.isEmpty() ? QDir(dataDir).filePath(QStringLiteral("backup-person-ids-") + stamp)
                                            : paths::normalize(backupOpt, QDir::currentPath());
    if (QFileInfo::exists(backupDir) && !QDir(backupDir).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        err() << "ERROR: the backup folder is not empty: " << native(backupDir) << "\n";
        return 1;
    }
    QJsonArray manifest;
    QStringList extra;   // unchanged but related files copied for safety
    const QString details = QDir(dataDir).filePath(QStringLiteral("people-details.json"));
    if (QFileInfo::exists(details)) extra << details;
    for (const FileChange& c : std::as_const(all)) {
        if (!QFileInfo::exists(c.path)) continue;
        const QString to = QDir(backupDir).filePath(c.backupRel);
        if (!copyVerified(c.path, to)) {
            err() << "ERROR: backup failed: " << native(c.path) << " -> " << native(to) << "\nNothing was converted.\n";
            return 1;
        }
        manifest.append(QJsonObject{{"original", c.path}, {"backup", c.backupRel}});
    }
    for (const QString& f : std::as_const(extra)) {
        const QString rel = QStringLiteral("data/") + QFileInfo(f).fileName();
        if (!copyVerified(f, QDir(backupDir).filePath(rel))) {
            err() << "ERROR: backup failed: " << native(f) << "\nNothing was converted.\n";
            return 1;
        }
        manifest.append(QJsonObject{{"original", f}, {"backup", rel}});
    }
    QJsonArray rootList;
    for (const QString& r : roots) rootList.append(r);
    if (!writeFile(QDir(backupDir).filePath(QStringLiteral("manifest.json")),
                   jsonBytes(QJsonObject{{"command", "migrate person-ids"},
                                         {"createdAt", QDateTime::currentDateTime().toString(Qt::ISODate)},
                                         {"dataDir", dataDir}, {"roots", rootList}, {"files", manifest}}))) {
        err() << "ERROR: cannot write the backup manifest in " << native(backupDir) << "\nNothing was converted.\n";
        return 1;
    }
    out() << "\nbackup:  " << native(backupDir) << "  (" << manifest.size() << " files, see manifest.json)\n";

    // ---- write: people.json first (a rerun after an interruption then reuses the same ids) ----
    int written = 0;
    for (const FileChange& c : std::as_const(all)) {
        if (!writeFile(c.path, c.content)) {
            err() << "ERROR: cannot write " << native(c.path) << " (" << written << " of " << all.size()
                  << " files written). The backup is in " << native(backupDir)
                  << "; running the command again continues where it stopped.\n";
            return 1;
        }
        ++written;
    }
    out() << "written: " << written << " files.\n"
          << "\nCheck in the app: People (names, nicknames, samples), a meeting's transcript speakers, tags.\n"
          << "To undo: copy the files back from the backup (manifest.json lists each original path).\n";
    out().flush();
    return 0;
}

} // namespace tanara::cli

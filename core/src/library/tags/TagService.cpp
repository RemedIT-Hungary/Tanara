#include "tanara/library/tags/TagService.h"
#include "tanara/library/tags/MeetingProfiles.h"
#include "tanara/library/tags/TagNames.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/foundation/text/Text.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/foundation/SharedFile.h"
#include "tanara/foundation/log/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <optional>

namespace tanara::library {

namespace {

constexpr int kUndoLimit = 50;

struct TagRec {
    Tag         tag;
    QDateTime   lastUsedAt;   // time of the latest application
    QStringList aliases;      // old names of merged tags
};

struct Rejected {
    QString meetingId;   // empty = person-level, lasting (person filled in)
    PersonId person;     // non-null = a (person, tag) rejection; without tagId (with meetingId):
                         // the person's suggestion rejected for that meeting
    QString tagId;     // empty = by name (e.g. the LLM's new-name idea)
    QString name;      // the original name (the key is computed from it)
    QString key;
};

struct Row {
    QString     title;
    QDateTime   startedAt;
    QStringList tagIds;
    PersonIds   participants;
    qint64      durationMs = 0;
};

struct UndoStep {
    QString label;
    quint64 serial = 0;           // the step's serial number (for the shared undo adapter)
    QStringList meetingIds;       // which meetings the step belongs to (MeetingUndoService)
    bool    hasSet = false;
    QVector<TagRec>   tags;
    QVector<Rejected> rejected;
    QHash<QString, QStringList> meetings;   // meetingId -> its tags before
    QHash<PersonId, QStringList> persons;   // person -> manual tags before
    bool isEmpty() const { return !hasSet && meetings.isEmpty() && persons.isEmpty(); }
};

// The meeting's named participants for the person <-> tag computation (UX plan 3: M(p)): the speaker
// map and the own track (MeetingLibrary::participantsOf), plus manually added, calendar-sourced and
// approved ("Who was there?") participants, so also before transcription.
PersonIds namedParticipants(const Meeting& m)
{
    PersonIds out = MeetingLibrary::participantsOf(m);
    for (const Participant& p : m.participants) {
        if (p.personId.isNull() || out.contains(p.personId)) continue;
        if (p.approved || p.source == ParticipantSource::Manual || p.source == ParticipantSource::Calendar)
            out << p.personId;
    }
    return out;
}

} // namespace

struct TagService::Impl {
    TagService*   q = nullptr;
    MeetingRepository* store = nullptr;
    MeetingProfiles* profiles = nullptr;
    PeopleStore*  people = nullptr;
    std::function<PersonId()> selfProvider;
    QString       path;
    FileStamp     stamp;

    QVector<TagRec>   tags;
    QVector<Rejected> rejected;

    mutable bool rowsLoaded = false;
    mutable QHash<QString, Row> rows;

    QVector<UndoStep> undo;
    UndoStep current;
    int  groupDepth = 0;
    bool undoing = false;
    quint64 nextSerial = 0;

    // ---- learned association (person <-> tag) ----
    mutable std::shared_ptr<const PersonTagStats> stats = std::make_shared<PersonTagStats>();
    mutable bool statsDirty = true;
    bool statsAsync = false;
    bool statsRunning = false;
    bool statsAgain = false;
    QSet<QString> askedLearning;   // learning questions already asked (meeting, personId, tag)
    int  statsGen = 0;
    std::shared_ptr<std::atomic_bool> alive = std::make_shared<std::atomic_bool>(true);

    // ---- file ----
    void load() {
        tags.clear();
        rejected.clear();
        QFile f(path);
        stamp = FileStamp::of(path);
        if (!f.open(QIODevice::ReadOnly)) return;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        for (const QJsonValue& v : root.value(QStringLiteral("tags")).toArray()) {
            const QJsonObject o = v.toObject();
            TagRec r;
            r.tag.id = o.value(QStringLiteral("id")).toString();
            r.tag.name = normalizeTagName(o.value(QStringLiteral("name")).toString());
            r.tag.createdAt = QDateTime::fromString(o.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs);
            r.lastUsedAt = QDateTime::fromString(o.value(QStringLiteral("lastUsedAt")).toString(), Qt::ISODateWithMs);
            for (const QJsonValue& a : o.value(QStringLiteral("aliases")).toArray())
                if (!a.toString().trimmed().isEmpty()) r.aliases << a.toString();
            if (r.tag.id.isEmpty() || r.tag.name.isEmpty()) continue;
            tags.append(r);
        }
        for (const QJsonValue& v : root.value(QStringLiteral("rejected")).toArray()) {
            const QJsonObject o = v.toObject();
            Rejected r;
            r.meetingId = o.value(QStringLiteral("meetingId")).toString();
            // A name-keyed "person" item (before version 2) is not read: it has no PersonId.
            if (o.contains(QStringLiteral("person")) && !o.contains(QStringLiteral("personId"))) continue;
            r.person = PersonId::fromString(o.value(QStringLiteral("personId")).toString());
            r.tagId = o.value(QStringLiteral("tagId")).toString();
            r.name = o.value(QStringLiteral("name")).toString();
            r.key = tagKey(r.name);
            if (r.meetingId.isEmpty() && r.person.isNull()) continue;
            if (!r.person.isNull() && r.tagId.isEmpty() && r.meetingId.isEmpty()) continue;
            if (r.person.isNull() && r.tagId.isEmpty() && r.key.isEmpty()) continue;
            rejected.append(r);
        }
    }
    bool save() {
        QJsonArray ta;
        for (const TagRec& r : std::as_const(tags)) {
            QJsonObject o{{QStringLiteral("id"), r.tag.id},
                          {QStringLiteral("name"), r.tag.name},
                          {QStringLiteral("createdAt"), r.tag.createdAt.toString(Qt::ISODateWithMs)}};
            if (r.lastUsedAt.isValid())
                o.insert(QStringLiteral("lastUsedAt"), r.lastUsedAt.toString(Qt::ISODateWithMs));
            if (!r.aliases.isEmpty())
                o.insert(QStringLiteral("aliases"), QJsonArray::fromStringList(r.aliases));
            ta.append(o);
        }
        QJsonArray ra;
        for (const Rejected& r : std::as_const(rejected)) {
            QJsonObject o;
            if (!r.meetingId.isEmpty()) o.insert(QStringLiteral("meetingId"), r.meetingId);
            if (!r.person.isNull()) o.insert(QStringLiteral("personId"), r.person.toString());
            if (!r.tagId.isEmpty()) o.insert(QStringLiteral("tagId"), r.tagId);
            if (!r.name.isEmpty()) o.insert(QStringLiteral("name"), r.name);
            ra.append(o);
        }
        const QJsonObject root{{QStringLiteral("version"), 2},
                               {QStringLiteral("tags"), ta},
                               {QStringLiteral("rejected"), ra}};
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile f(path);
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
        const bool ok = f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
        stamp = FileStamp::of(path);
        return ok;
    }
    // Every modification of tags.json runs under SharedFileLock; without the lock (another process held it
    // for too long) the modification is dropped, neither memory nor disk changes: an unlocked write could
    // silently overwrite the other process's change.
    bool lockOk(const SharedFileLock& lock, const char* operation) const {
        if (lock.locked()) return true;
        qCWarning(lcStore).noquote()
            << QStringLiteral("tags.json %1: another process holds the lock, the change was not written: %2")
                   .arg(QLatin1String(operation), path);
        return false;
    }
    // Before a modification: if another process wrote the file meanwhile, we build on its state.
    void syncFromDisk() {
        if (FileStamp::of(path) != stamp) load();
    }

    // ---- search ----
    int indexOf(const QString& id) const {
        for (int i = 0; i < tags.size(); ++i)
            if (tags.at(i).tag.id == id) return i;
        return -1;
    }
    int indexOfKey(const QString& key, bool withAliases) const {
        if (key.isEmpty()) return -1;
        for (int i = 0; i < tags.size(); ++i)
            if (tagKey(tags.at(i).tag.name) == key) return i;
        if (withAliases)
            for (int i = 0; i < tags.size(); ++i)
                for (const QString& a : tags.at(i).aliases)
                    if (tagKey(a) == key) return i;
        return -1;
    }

    // ---- meeting cache ----
    void ensureRows() const {
        if (rowsLoaded || !store) return;
        rows.clear();
        for (const Meeting& idx : store->loadAll()) {
            Meeting m = store->load(idx.id);
            if (m.id.isEmpty()) m = idx;
            rows.insert(m.id, rowOf(m));
        }
        rowsLoaded = true;
    }
    static Row rowOf(const Meeting& m) {
        return Row{ m.title, m.startedAt, m.tagIds, namedParticipants(m), m.durationMs };
    }
    void reloadRow(const QString& id) {
        if (rowsLoaded && store) {
            const Meeting m = store->load(id);
            if (m.id.isEmpty()) rows.remove(id);
            else rows.insert(id, rowOf(m));
        }
        invalidateStats();   // from the refreshed rows
    }
    QHash<QString, int> counts() const {
        ensureRows();
        QHash<QString, int> c;
        for (const Row& r : std::as_const(rows))
            for (const QString& id : r.tagIds) c[id]++;
        return c;
    }
    TagUsage usageOf(const TagRec& rec) const {
        ensureRows();
        TagUsage u;
        u.tag = rec.tag;
        for (const Row& r : std::as_const(rows)) {
            if (!r.tagIds.contains(rec.tag.id)) continue;
            ++u.meetingCount;
            if (!u.firstUsedAt.isValid() || r.startedAt < u.firstUsedAt) u.firstUsedAt = r.startedAt;
            if (!u.lastUsedAt.isValid() || r.startedAt > u.lastUsedAt) u.lastUsedAt = r.startedAt;
        }
        if (rec.lastUsedAt.isValid() && (!u.lastUsedAt.isValid() || rec.lastUsedAt > u.lastUsedAt))
            u.lastUsedAt = rec.lastUsedAt;
        return u;
    }

    // ---- undo ----
    void snapshotSet() {
        if (undoing || current.hasSet) return;
        current.hasSet = true;
        current.tags = tags;
        current.rejected = rejected;
    }
    void snapshotMeeting(const QString& meetingId) {
        if (undoing || current.meetings.contains(meetingId)) return;
        markMeeting(meetingId);
        ensureRows();
        current.meetings.insert(meetingId, rows.value(meetingId).tagIds);
    }
    void markMeeting(const QString& meetingId) {
        if (!undoing && groupDepth > 0 && !meetingId.isEmpty() && !current.meetingIds.contains(meetingId))
            current.meetingIds << meetingId;
    }
    void openStep(const QString& label) {
        if (groupDepth++ == 0) {
            current = UndoStep{};
            current.label = label;
        }
    }
    void closeStep() {
        if (groupDepth == 0) return;
        if (--groupDepth > 0) return;
        if (undoing || current.isEmpty()) { current = UndoStep{}; return; }
        current.serial = ++nextSerial;
        undo.append(current);
        while (undo.size() > kUndoLimit) undo.removeFirst();
        current = UndoStep{};
        emit q->undoChanged();
    }

    // Writing the meeting's tags (meeting.json). true if changed.
    bool writeMeetingTags(const QString& meetingId, const QStringList& ids) {
        if (!store) return false;
        Meeting m = store->load(meetingId);
        if (m.id.isEmpty()) return false;
        QStringList clean;
        for (const QString& id : ids)
            if (!id.isEmpty() && !clean.contains(id)) clean << id;
        if (m.tagIds == clean) return false;
        snapshotMeeting(meetingId);
        m.tagIds = clean;
        store->saveMeeting(m);   // meetingUpdated: the row reloads
        ensureRows();
        rows[meetingId].tagIds = clean;
        invalidateStats();
        emit q->meetingTagsChanged(meetingId);
        return true;
    }
    // ---- people ----
    PersonId self() const { return selfProvider ? selfProvider() : PersonId(); }
    bool isSelf(const PersonId& person) const {
        const PersonId s = self();
        return !s.isNull() && s == person;
    }
    QString nameOf(const PersonId& person) const {
        if (!people) return {};
        return people->displayName(person);
    }
    void snapshotPerson(const PersonId& person) {
        if (undoing || !people || current.persons.contains(person)) return;
        current.persons.insert(person, people->tags(person));
    }
    // Writing the person's manual tags (people.json). true if changed.
    bool writePersonTags(const PersonId& person, const QStringList& ids) {
        if (!people) return false;
        people->refresh();
        if (!people->contains(person)) return false;
        QStringList clean;
        for (const QString& id : ids)
            if (!id.isEmpty() && !clean.contains(id)) clean << id;
        if (people->tags(person) == clean) return false;
        snapshotPerson(person);
        if (!people->setTags(person, clean)) return false;
        emit q->personTagsChanged(person.toString());
        return true;
    }
    // On every person: tag `from` replaced by `to` (an empty `to` = removal).
    void replacePersonTag(const QString& from, const QString& to) {
        if (!people) return;
        people->refresh();
        const QHash<PersonId, QStringList> all = people->allTags();
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            QStringList t = it.value();
            const int pos = t.indexOf(from);
            if (pos < 0) continue;
            if (to.isEmpty() || t.contains(to)) t.removeAt(pos);
            else t[pos] = to;
            writePersonTags(it.key(), t);
        }
    }

    // ---- statistics ----
    QVector<PersonTagRow> statRows() const {
        QVector<PersonTagRow> out;
        out.reserve(rows.size());
        for (auto it = rows.constBegin(); it != rows.constEnd(); ++it)
            out.append(PersonTagRow{ it.key(), it->startedAt, it->tagIds, it->participants });
        return out;
    }
    // The rows from disk (on a background thread; file reading only).
    static QVector<PersonTagRow> readRows(const QVector<QPair<QString, QString>>& folders) {
        QVector<PersonTagRow> out;
        for (const auto& f : folders) {
            const Meeting m = MeetingRepository::readMeetingFolder(f.second);
            if (m.id.isEmpty() && m.title.isEmpty()) continue;
            out.append(PersonTagRow{ f.first, m.startedAt, m.tagIds, namedParticipants(m) });
        }
        return out;
    }
    void invalidateStats() {
        statsDirty = true;
        ++statsGen;
        if (statsAsync) startStats();
        else emit q->personStatsChanged();
    }
    void computeStatsNow() const {
        ensureRows();
        stats = std::make_shared<PersonTagStats>(PersonTagStats::compute(statRows(), self()));
        statsDirty = false;
    }
    void startStats() {
        if (statsRunning) { statsAgain = true; return; }
        if (!QCoreApplication::instance()) { computeStatsNow(); emit q->personStatsChanged(); return; }
        statsRunning = true;
        statsAgain = false;
        const int gen = statsGen;
        const PersonId selfName = self();
        // A copy of the rows already loaded; otherwise the background thread reads the meeting.json files.
        QVector<PersonTagRow> copied;
        QVector<QPair<QString, QString>> folders;
        if (rowsLoaded) copied = statRows();
        else if (store)
            for (const Meeting& m : store->loadAll())
                if (!m.id.isEmpty() && !m.folder.isEmpty()) folders.append({ m.id, m.folder });
        const bool fromDisk = !rowsLoaded;
        const auto guard = alive;
        QThreadPool::globalInstance()->start([this, gen, selfName, copied, folders, fromDisk, guard] {
            const QVector<PersonTagRow> list = fromDisk ? readRows(folders) : copied;
            auto result = std::make_shared<const PersonTagStats>(PersonTagStats::compute(list, selfName));
            // The context is the application's (the TagService may be gone by then; the guard signals it).
            QCoreApplication* app = QCoreApplication::instance();
            if (!app || !guard->load()) return;
            QMetaObject::invokeMethod(app, [this, gen, result, guard] {
                if (!guard->load()) return;
                statsRunning = false;
                if (gen == statsGen) {
                    stats = result;
                    statsDirty = false;
                    emit q->personStatsChanged();
                } else {
                    statsAgain = true;
                }
                if (statsAgain) startStats();
            }, Qt::QueuedConnection);
        });
    }
    std::shared_ptr<const PersonTagStats> currentStats() const {
        if (statsDirty && !statsAsync) computeStatsNow();
        return stats;
    }
    PersonTagStat statOf(const PersonTagStats& st, const PersonId& person, const QString& tagId) const {
        PersonTagStat r;
        r.personId = person;
        r.name = nameOf(person);
        r.tagId = tagId;
        r.shared = st.shared(person, tagId);
        r.tagTotal = st.tagTotal(tagId);
        r.personTotal = st.personTotal(person);
        r.lastShared = st.lastShared(person, tagId);
        return r;
    }
    bool personRejected(const PersonId& person, const QString& tagId, const QString& meetingId) const {
        for (const Rejected& r : rejected) {
            if (r.person.isNull() || r.tagId != tagId || r.person != person) continue;
            if (r.meetingId.isEmpty() || r.meetingId == meetingId) return true;
        }
        return false;
    }

    void touch(const QString& tagId) {
        const int i = indexOf(tagId);
        if (i >= 0) tags[i].lastUsedAt = QDateTime::currentDateTime();
    }
    void dropRejected(const QString& meetingId, const QString& tagId, const QString& name) {
        const QString key = tagKey(name);
        const auto before = rejected.size();
        rejected.erase(std::remove_if(rejected.begin(), rejected.end(), [&](const Rejected& r) {
            if (r.meetingId != meetingId || !r.person.isNull()) return false;
            return (!tagId.isEmpty() && r.tagId == tagId) || (!key.isEmpty() && r.key == key);
        }), rejected.end());
        if (rejected.size() != before) emit q->rejectedChanged();
    }
};

// One public call = one undo step (or part of the open group).
class StepScope {
public:
    StepScope(TagService* s, const QString& label) : m_s(s) { m_s->beginGroup(label); }
    ~StepScope() { m_s->endGroup(); }
private:
    TagService* m_s;
};

TagService::TagService(MeetingRepository* store, const QString& tagsFile, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    d->store = store;
    d->path = tagsFile;
    d->load();
    if (store) {
        auto refresh = [this](const QString& id) { d->reloadRow(id); };
        connect(store, &MeetingRepository::meetingUpdated, this, refresh);
        connect(store, &MeetingRepository::meetingAdded, this, refresh);
        connect(store, &MeetingRepository::meetingRemoved, this, [this](const QString& id) {
            const bool had = d->rowsLoaded && d->rows.remove(id) > 0;
            d->invalidateStats();
            auto ofMeeting = [&](const Rejected& r) { return r.meetingId == id; };
            if (std::any_of(d->rejected.cbegin(), d->rejected.cend(), ofMeeting)) {
                SharedFileLock lock(d->path);
                if (d->lockOk(lock, "meetingRemoved")) {
                    d->syncFromDisk();
                    d->rejected.erase(std::remove_if(d->rejected.begin(), d->rejected.end(), ofMeeting),
                                      d->rejected.end());
                    d->save();
                    emit rejectedChanged();
                }
            }
            if (had) emit tagsChanged();   // the counts changed
        });
    }
}

TagService::~TagService()
{
    d->alive->store(false);
}

void TagService::setProfiles(MeetingProfiles* profiles) { d->profiles = profiles; }

void TagService::reload()
{
    d->load();
    d->rowsLoaded = false;
    d->rows.clear();
    d->invalidateStats();
    emit tagsChanged();
    emit rejectedChanged();
}

// ---- set ----------------------------------------------------------------------------

QVector<TagUsage> TagService::all(Sort sort) const
{
    QVector<TagUsage> out;
    out.reserve(d->tags.size());
    for (const TagRec& r : std::as_const(d->tags)) out.append(d->usageOf(r));
    std::stable_sort(out.begin(), out.end(), [sort](const TagUsage& a, const TagUsage& b) {
        switch (sort) {
        case Sort::Alpha:
            break;
        case Sort::MostUsed:
            if (a.meetingCount != b.meetingCount) return a.meetingCount > b.meetingCount;
            break;
        case Sort::LastUsed:
            if (a.lastUsedAt != b.lastUsedAt) {
                if (!a.lastUsedAt.isValid()) return false;
                if (!b.lastUsedAt.isValid()) return true;
                return a.lastUsedAt > b.lastUsedAt;
            }
            break;
        }
        return text::fold(a.tag.name).localeAwareCompare(text::fold(b.tag.name)) < 0;
    });
    return out;
}

Tag TagService::tag(const QString& id) const
{
    const int i = d->indexOf(id);
    return i < 0 ? Tag{} : d->tags.at(i).tag;
}

Tag TagService::byName(const QString& name) const
{
    const int i = d->indexOfKey(tagKey(name), true);
    return i < 0 ? Tag{} : d->tags.at(i).tag;
}

QVector<Tag> TagService::similarNames(const QString& name, int limit) const
{
    QVector<Tag> out;
    const QString key = tagKey(name);
    if (key.isEmpty()) return out;
    for (const TagRec& r : std::as_const(d->tags)) {
        if (out.size() >= limit) break;
        if (tagKey(r.tag.name) == key) continue;
        if (nearDuplicate(r.tag.name, name)) out.append(r.tag);
    }
    return out;
}

int TagService::meetingCount(const QString& tagId) const
{
    return d->counts().value(tagId);
}

int TagService::totalMeetings() const
{
    d->ensureRows();
    return int(d->rows.size());
}

QVector<TagInputRow> TagService::inputRows(const QString& typed, int limit) const
{
    QVector<TagInputRow> rows;
    const QHash<QString, int> counts = d->counts();
    const QString norm = normalizeTagName(typed);
    if (norm.isEmpty()) {
        for (const Tag& t : recent(limit)) {
            TagInputRow r;
            r.kind = TagInputRow::Recent;
            r.tag = t;
            r.meetingCount = counts.value(t.id);
            rows.append(r);
        }
        return rows;
    }

    const QString fq = text::fold(norm);
    const QString key = tagKey(norm);
    const int exactIdx = d->indexOfKey(key, true);

    struct Cand { int idx; int tier; int pos; };
    QVector<Cand> cands;
    for (int i = 0; i < d->tags.size(); ++i) {
        if (i == exactIdx) continue;
        const QString fn = text::fold(d->tags.at(i).tag.name);
        const int pos = fn.indexOf(fq);
        int tier = -1;
        if (pos == 0) tier = 0;
        else if (pos > 0) tier = fn.at(pos - 1).isLetterOrNumber() ? 2 : 1;   // word start -> "prefix"
        else if (!key.isEmpty() && tagKey(d->tags.at(i).tag.name).contains(key)) tier = 3;
        if (tier >= 0) cands.append({ i, tier, pos });
    }
    std::stable_sort(cands.begin(), cands.end(), [&](const Cand& a, const Cand& b) {
        if (a.tier != b.tier) return a.tier < b.tier;
        const int ca = counts.value(d->tags.at(a.idx).tag.id), cb = counts.value(d->tags.at(b.idx).tag.id);
        if (ca != cb) return ca > cb;
        return d->tags.at(a.idx).tag.name.localeAwareCompare(d->tags.at(b.idx).tag.name) < 0;
    });

    QSet<QString> shown;
    auto addMatch = [&](int idx, int pos) {
        TagInputRow r;
        r.kind = TagInputRow::Match;
        r.tag = d->tags.at(idx).tag;
        r.meetingCount = counts.value(r.tag.id);
        if (pos >= 0) { r.matchStart = pos; r.matchLen = int(fq.size()); }
        rows.append(r);
        shown.insert(r.tag.id);
    };
    if (exactIdx >= 0) {
        const int pos = text::fold(d->tags.at(exactIdx).tag.name).indexOf(fq);
        addMatch(exactIdx, pos);
    }
    for (const Cand& c : std::as_const(cands)) {
        if (rows.size() >= limit) break;
        addMatch(c.idx, c.pos);
    }
    if (exactIdx >= 0) return rows;   // exact match: no "New tag" row

    bool near = false;
    for (const Tag& t : similarNames(norm, 2)) {
        if (shown.contains(t.id)) continue;
        TagInputRow r;
        r.kind = TagInputRow::NearDuplicate;
        r.tag = t;
        r.meetingCount = counts.value(t.id);
        rows.append(r);
        near = true;
    }
    TagInputRow n;
    n.kind = near ? TagInputRow::ForceNew : TagInputRow::New;
    n.tag.name = norm;
    rows.append(n);
    return rows;
}

int TagService::preferredRow(const QVector<TagInputRow>& rows)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).kind == TagInputRow::NearDuplicate) return i;
    return rows.isEmpty() ? -1 : 0;
}

Tag TagService::create(const QString& name)
{
    const QString norm = normalizeTagName(name);
    if (tagKey(norm).isEmpty()) return {};
    StepScope step(this, tr("Címke létrehozása"));
    SharedFileLock lock(d->path);
    if (!d->lockOk(lock, "create")) return {};
    d->syncFromDisk();
    const int existing = d->indexOfKey(tagKey(norm), true);
    if (existing >= 0) return d->tags.at(existing).tag;
    d->snapshotSet();
    TagRec r;
    r.tag.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.tag.name = norm;
    r.tag.createdAt = QDateTime::currentDateTime();
    d->tags.append(r);
    d->save();
    emit tagsChanged();
    return r.tag;
}

bool TagService::rename(const QString& id, const QString& name)
{
    const QString norm = normalizeTagName(name);
    if (tagKey(norm).isEmpty()) return false;
    StepScope step(this, tr("Címke átnevezése"));
    SharedFileLock lock(d->path);
    if (!d->lockOk(lock, "rename")) return false;
    d->syncFromDisk();
    const int i = d->indexOf(id);
    if (i < 0) return false;
    const int other = d->indexOfKey(tagKey(norm), false);
    if (other >= 0 && other != i) return false;
    if (d->tags.at(i).tag.name == norm) return true;
    d->snapshotSet();
    // Was the old name another tag's alias? Then it is removed from there (this tag carries it).
    for (TagRec& r : d->tags)
        r.aliases.erase(std::remove_if(r.aliases.begin(), r.aliases.end(),
                                       [&](const QString& a) { return tagKey(a) == tagKey(norm); }),
                        r.aliases.end());
    d->tags[i].tag.name = norm;
    d->save();
    emit tagsChanged();
    return true;
}

void TagService::merge(const QString& fromId, const QString& keepId)
{
    if (fromId == keepId) return;
    StepScope step(this, tr("Címkék összevonása"));
    SharedFileLock lock(d->path);
    if (!d->lockOk(lock, "merge")) return;
    d->syncFromDisk();
    const int fi = d->indexOf(fromId), ki = d->indexOf(keepId);
    if (fi < 0 || ki < 0) return;
    d->snapshotSet();
    d->ensureRows();
    // Re-tagging every meeting (the kept tag goes in the old one's place if not on it yet).
    const QStringList ids = d->rows.keys();
    for (const QString& mid : ids) {
        QStringList t = d->rows.value(mid).tagIds;
        const int pos = t.indexOf(fromId);
        if (pos < 0) continue;
        if (t.contains(keepId)) t.removeAt(pos);
        else t[pos] = keepId;
        d->writeMeetingTags(mid, t);
    }
    // Rejections: those of the disappearing tag pass to the kept one (without repeats).
    QVector<Rejected> rej;
    for (Rejected r : std::as_const(d->rejected)) {
        if (r.tagId == fromId) r.tagId = keepId;
        const bool dup = std::any_of(rej.cbegin(), rej.cend(), [&](const Rejected& x) {
            return x.meetingId == r.meetingId && x.person == r.person
                && x.tagId == r.tagId && x.key == r.key;
        });
        if (!dup) rej.append(r);
    }
    d->rejected = rej;
    d->replacePersonTag(fromId, keepId);
    TagRec from = d->tags.at(fi);
    TagRec& keep = d->tags[ki];
    for (const QString& a : QStringList{from.tag.name} + from.aliases)
        if (!keep.aliases.contains(a) && tagKey(a) != tagKey(keep.tag.name)) keep.aliases << a;
    if (from.lastUsedAt > keep.lastUsedAt) keep.lastUsedAt = from.lastUsedAt;
    d->tags.removeAt(fi);
    d->save();
    emit tagsChanged();
    emit rejectedChanged();
}

void TagService::remove(const QString& id)
{
    StepScope step(this, tr("Címke törlése"));
    SharedFileLock lock(d->path);
    if (!d->lockOk(lock, "remove")) return;
    d->syncFromDisk();
    const int i = d->indexOf(id);
    if (i < 0) return;
    d->snapshotSet();
    d->ensureRows();
    const QStringList ids = d->rows.keys();
    for (const QString& mid : ids) {
        QStringList t = d->rows.value(mid).tagIds;
        if (t.removeAll(id) > 0) d->writeMeetingTags(mid, t);
    }
    d->rejected.erase(std::remove_if(d->rejected.begin(), d->rejected.end(),
                                     [&](const Rejected& r) { return r.tagId == id; }),
                      d->rejected.end());
    d->replacePersonTag(id, QString());
    d->tags.removeAt(i);
    d->save();
    emit tagsChanged();
    emit rejectedChanged();
}

// ---- meetings --------------------------------------------------------------------------

QStringList TagService::tagsOf(const QString& meetingId) const
{
    d->ensureRows();
    QStringList out;
    for (const QString& id : d->rows.value(meetingId).tagIds)
        if (d->indexOf(id) >= 0) out << id;
    return out;
}

Tag TagService::addTag(const QString& meetingId, const QString& nameOrId, TagSource source)
{
    Q_UNUSED(source)
    d->ensureRows();
    if (!d->rows.contains(meetingId)) {
        d->reloadRow(meetingId);
        if (!d->rows.contains(meetingId)) return {};
    }
    StepScope step(this, tr("Címke hozzáadása"));
    Tag t = tag(nameOrId);
    if (!t.isValid()) t = create(nameOrId);
    if (!t.isValid()) return {};
    QStringList ids = d->rows.value(meetingId).tagIds;
    if (!ids.contains(t.id)) {
        ids << t.id;
        SharedFileLock lock(d->path);
        if (!d->lockOk(lock, "addTag")) return {};
        d->syncFromDisk();
        d->snapshotSet();
        d->touch(t.id);
        d->dropRejected(meetingId, t.id, t.name);
        d->save();
        d->writeMeetingTags(meetingId, ids);
        emit tagsChanged();
    }
    return t;
}

void TagService::removeTag(const QString& meetingId, const QString& id)
{
    StepScope step(this, tr("Címke levétele"));
    QStringList ids = tagsOf(meetingId);
    if (ids.removeAll(id) == 0) return;
    if (d->writeMeetingTags(meetingId, ids)) emit tagsChanged();
}

void TagService::setTags(const QString& meetingId, const QStringList& ids, TagSource source)
{
    Q_UNUSED(source)
    StepScope step(this, tr("Címkék módosítása"));
    QStringList clean;
    for (const QString& id : ids)
        if (d->indexOf(id) >= 0 && !clean.contains(id)) clean << id;
    const QStringList before = tagsOf(meetingId);
    SharedFileLock lock(d->path);   // before the meeting is touched: all or nothing
    if (!d->lockOk(lock, "setTags")) return;
    if (!d->writeMeetingTags(meetingId, clean)) return;
    d->syncFromDisk();
    d->snapshotSet();
    for (const QString& id : clean)
        if (!before.contains(id)) { d->touch(id); d->dropRejected(meetingId, id, tag(id).name); }
    d->save();
    emit tagsChanged();
}

void TagService::bulkAdd(const QStringList& meetingIds, const QString& id)
{
    const Tag t = tag(id);
    if (!t.isValid()) return;
    StepScope step(this, tr("„%1” hozzáadása %n megbeszéléshez", nullptr, int(meetingIds.size())).arg(t.name));
    for (const QString& mid : meetingIds) addTag(mid, id, TagSource::Bulk);
}

void TagService::bulkRemove(const QStringList& meetingIds, const QString& id)
{
    const Tag t = tag(id);
    if (!t.isValid()) return;
    StepScope step(this, tr("„%1” levétele %n megbeszélésről", nullptr, int(meetingIds.size())).arg(t.name));
    for (const QString& mid : meetingIds) removeTag(mid, id);
}

QVector<Tag> TagService::recent(int limit) const
{
    QVector<Tag> out;
    for (const TagUsage& u : all(Sort::LastUsed)) {
        if (out.size() >= limit) break;
        out.append(u.tag);
    }
    return out;
}

QStringList TagService::meetingsWith(const QString& tagId) const
{
    d->ensureRows();
    QVector<QPair<QDateTime, QString>> hits;
    for (auto it = d->rows.constBegin(); it != d->rows.constEnd(); ++it)
        if (it->tagIds.contains(tagId)) hits.append({ it->startedAt, it.key() });
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });
    QStringList out;
    for (const auto& h : std::as_const(hits)) out << h.second;
    return out;
}

QHash<QString, QStringList> TagService::taggedMeetings() const
{
    d->ensureRows();
    QHash<QString, QStringList> out;
    for (auto it = d->rows.constBegin(); it != d->rows.constEnd(); ++it) {
        QStringList ids;
        for (const QString& id : it->tagIds)
            if (d->indexOf(id) >= 0) ids << id;
        if (!ids.isEmpty()) out.insert(it.key(), ids);
    }
    return out;
}

MeetingRef TagService::meetingRef(const QString& meetingId) const
{
    d->ensureRows();
    const auto it = d->rows.constFind(meetingId);
    if (it == d->rows.constEnd()) return { meetingId, QString(), QDateTime(), 0 };
    return { meetingId, it->title, it->startedAt, it->durationMs };
}

// ---- derived ---------------------------------------------------------------------------

QVector<QPair<QString, int>> TagService::cooccurring(const QString& id) const
{
    d->ensureRows();
    QHash<QString, int> c;
    for (const Row& r : std::as_const(d->rows)) {
        if (!r.tagIds.contains(id)) continue;
        for (const QString& o : r.tagIds)
            if (o != id && d->indexOf(o) >= 0) c[o]++;
    }
    QVector<QPair<QString, int>> out;
    for (auto it = c.constBegin(); it != c.constEnd(); ++it) out.append({ it.key(), it.value() });
    std::sort(out.begin(), out.end(), [this](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return tag(a.first).name.localeAwareCompare(tag(b.first).name) < 0;
    });
    return out;
}

TagProfile TagService::profile(const QString& id) const
{
    TagProfile p;
    p.tagId = id;
    if (d->indexOf(id) < 0) return p;
    const QStringList mids = meetingsWith(id);
    p.meetingCount = int(mids.size());
    QHash<PersonId, int> people;
    for (const QString& mid : mids) {
        const Row r = d->rows.value(mid);
        p.meetings.append({ mid, r.title, r.startedAt, r.durationMs });
        for (const PersonId& n : r.participants) people[n]++;
    }
    for (auto it = people.constBegin(); it != people.constEnd(); ++it) {
        const QString name = d->nameOf(it.key());
        if (!name.isEmpty()) p.topParticipants.append({ name, it.value() });
    }
    std::sort(p.topParticipants.begin(), p.topParticipants.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first.localeAwareCompare(b.first) < 0;
    });
    if (p.topParticipants.size() > 8) p.topParticipants.resize(8);
    if (d->profiles) p.topTerms = d->profiles->topTerms(mids, 12);
    p.cooccurring = cooccurring(id);
    return p;
}

QString TagService::profileLine(const QString& id) const
{
    const TagProfile p = profile(id);
    QStringList parts;
    QStringList people;
    for (int i = 0; i < std::min<qsizetype>(3, p.topParticipants.size()); ++i)
        people << p.topParticipants.at(i).first;
    if (!people.isEmpty()) parts << QStringLiteral("people: %1").arg(people.join(QStringLiteral(", ")));
    if (!p.topTerms.isEmpty())
        parts << QStringLiteral("terms: %1").arg(p.topTerms.mid(0, 5).join(QStringLiteral(", ")));
    QStringList titles;
    for (int i = 0; i < std::min<qsizetype>(2, p.meetings.size()); ++i)
        titles << QStringLiteral("\"%1\"").arg(p.meetings.at(i).title);
    if (!titles.isEmpty()) parts << QStringLiteral("e.g. %1").arg(titles.join(QStringLiteral(", ")));
    parts << QStringLiteral("%1 meetings").arg(p.meetingCount);
    return parts.join(QStringLiteral("; "));
}

// ---- rejection ---------------------------------------------------------------------------

void TagService::reject(const QString& meetingId, const TagSuggestion& suggestion)
{
    if (meetingId.isEmpty()) return;
    if (isRejected(meetingId, suggestion.tagId.isEmpty() ? suggestion.name : suggestion.tagId)) return;
    StepScope step(this, tr("Javaslat elutasítása"));
    d->markMeeting(meetingId);
    SharedFileLock lock(d->path);
    if (!d->lockOk(lock, "reject")) return;
    d->syncFromDisk();
    d->snapshotSet();
    Rejected r;
    r.meetingId = meetingId;
    r.tagId = suggestion.tagId;
    r.name = suggestion.name;
    r.key = tagKey(r.name);
    d->rejected.append(r);
    d->save();
    emit rejectedChanged();
}

bool TagService::isRejected(const QString& meetingId, const QString& tagIdOrName) const
{
    const int idx = d->indexOf(tagIdOrName);
    const QString key = idx >= 0 ? tagKey(d->tags.at(idx).tag.name) : tagKey(tagIdOrName);
    const QString id = idx >= 0 ? tagIdOrName : QString();
    for (const Rejected& r : std::as_const(d->rejected)) {
        if (r.meetingId != meetingId || !r.person.isNull()) continue;
        if (!id.isEmpty() && r.tagId == id) return true;
        if (!r.key.isEmpty() && r.key == key) return true;
    }
    return false;
}

int TagService::rejectedCount() const { return int(d->rejected.size()); }

void TagService::clearRejected()
{
    if (d->rejected.isEmpty()) return;
    StepScope step(this, tr("Elutasított javaslatok visszaállítása"));
    SharedFileLock lock(d->path);
    if (!d->lockOk(lock, "clearRejected")) return;
    d->syncFromDisk();
    d->snapshotSet();
    d->rejected.clear();
    d->save();
    emit rejectedChanged();
}

// ---- undo --------------------------------------------------------------------------

QString TagService::beginGroup(const QString& label)
{
    d->openStep(label);
    return d->current.label;
}

QString TagService::beginGroup(const QString& label, const QString& meetingId)
{
    d->openStep(label);
    d->markMeeting(meetingId);
    return d->current.label;
}

void TagService::endGroup() { d->closeStep(); }

QVector<quint64> TagService::undoSerials() const
{
    QVector<quint64> out;
    out.reserve(d->undo.size());
    for (const UndoStep& s : std::as_const(d->undo)) out << s.serial;
    return out;
}

QStringList TagService::undoMeetingIds(quint64 serial) const
{
    for (const UndoStep& s : std::as_const(d->undo))
        if (s.serial == serial) return s.meetingIds;
    return {};
}

bool TagService::canUndo() const { return !d->undo.isEmpty(); }

QString TagService::undoLabel() const { return d->undo.isEmpty() ? QString() : d->undo.last().label; }

void TagService::undo()
{
    if (d->undo.isEmpty() || d->groupDepth > 0) return;
    // The set's lock first: without it the step stays on the stack and nothing changes.
    std::optional<SharedFileLock> lock;
    if (d->undo.last().hasSet) {
        lock.emplace(d->path);
        if (!d->lockOk(*lock, "undo")) return;
    }
    const UndoStep step = d->undo.takeLast();
    d->undoing = true;
    if (step.hasSet) {
        d->tags = step.tags;
        d->rejected = step.rejected;
        d->save();
    }
    for (auto it = step.meetings.constBegin(); it != step.meetings.constEnd(); ++it)
        d->writeMeetingTags(it.key(), it.value());
    for (auto it = step.persons.constBegin(); it != step.persons.constEnd(); ++it)
        d->writePersonTags(it.key(), it.value());
    d->undoing = false;
    emit tagsChanged();
    if (step.hasSet) emit rejectedChanged();
    emit undoChanged();
}

// ---- people: manual tags ------------------------------------------------------------------

void TagService::setPeopleStore(PeopleStore* people) { d->people = people; }

void TagService::setSelfProvider(std::function<PersonId()> provider)
{
    d->selfProvider = std::move(provider);
    d->invalidateStats();
}

PersonId TagService::selfId() const { return d->self(); }

bool TagService::isSelf(const PersonId& person) const { return d->isSelf(person); }

PersonNames TagService::personNames() const
{
    if (!d->people) return {};
    d->people->refresh();
    return d->people->names();
}

QStringList TagService::tagsOfPerson(const PersonId& person) const
{
    QStringList out;
    if (!d->people || person.isNull() || d->isSelf(person)) return out;
    d->people->refresh();
    for (const QString& id : d->people->tags(person))
        if (d->indexOf(id) >= 0) out << id;
    return out;
}

bool TagService::setPersonTags(const PersonId& person, const QStringList& ids)
{
    if (!d->people || person.isNull() || d->isSelf(person)) return false;
    StepScope step(this, tr("Személy címkéinek módosítása"));
    QStringList clean;
    for (const QString& id : ids)
        if (d->indexOf(id) >= 0 && !clean.contains(id)) clean << id;
    // Ids that have since vanished from the set also come off the record.
    const QStringList before = tagsOfPerson(person);
    SharedFileLock lock(d->path);   // before the person is touched: all or nothing
    if (!d->lockOk(lock, "setPersonTags")) return false;
    if (!d->writePersonTags(person, clean)) return true;
    d->syncFromDisk();
    d->snapshotSet();
    for (const QString& id : clean)
        if (!before.contains(id)) d->touch(id);
    d->save();
    emit tagsChanged();
    return true;
}

Tag TagService::addPersonTag(const PersonId& person, const QString& nameOrId)
{
    if (!d->people || person.isNull() || d->isSelf(person)) return {};
    StepScope step(this, tr("Címke a személyen"));
    Tag t = tag(nameOrId);
    if (!t.isValid()) t = create(nameOrId);
    if (!t.isValid()) return {};
    QStringList ids = tagsOfPerson(person);
    if (!ids.contains(t.id)) setPersonTags(person, ids << t.id);
    return t;
}

void TagService::removePersonTag(const PersonId& person, const QString& id)
{
    StepScope step(this, tr("Címke levétele a személyről"));
    QStringList ids = tagsOfPerson(person);
    if (ids.removeAll(id) == 0) return;
    setPersonTags(person, ids);
}

PersonIds TagService::peopleWith(const QString& tagId) const
{
    PersonIds out;
    if (!d->people || d->indexOf(tagId) < 0) return out;
    d->people->refresh();
    const QHash<PersonId, QStringList> all = d->people->allTags();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
        if (it.value().contains(tagId) && !d->isSelf(it.key())) out << it.key();
    const PersonNames names = d->people->names();
    std::sort(out.begin(), out.end(), [&](const PersonId& a, const PersonId& b) {
        const int c = text::fold(names.value(a)).localeAwareCompare(text::fold(names.value(b)));
        return c != 0 ? c < 0 : a < b;
    });
    return out;
}

// ---- people: learned association + suggestions ------------------------------------------------

void TagService::ensurePersonStats()
{
    d->statsAsync = true;
    if (d->statsDirty) d->startStats();
}

bool TagService::personStatsBusy() const { return d->statsRunning; }

void TagService::invalidatePersonStats() { d->invalidateStats(); }

void TagService::refreshPersonStatsNow()
{
    ++d->statsGen;   // the running background result is stale
    d->computeStatsNow();
    emit personStatsChanged();
}

std::shared_ptr<const PersonTagStats> TagService::personStats() const { return d->currentStats(); }

PersonTagSnapshot TagService::personTagSnapshot() const
{
    PersonTagSnapshot snap;
    snap.stats = d->currentStats();
    snap.self = d->self();
    for (const TagRec& r : std::as_const(d->tags)) snap.tagNames.insert(r.tag.id, r.tag.name);
    if (d->people) {
        d->people->refresh();
        snap.names = d->people->names();
        const QHash<PersonId, QStringList> all = d->people->allTags();
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            QStringList ids;
            for (const QString& id : it.value())
                if (snap.tagNames.contains(id)) ids << id;
            if (!ids.isEmpty()) snap.manual.insert(it.key(), ids);
        }
    }
    return snap;
}

namespace {

// Manual first, then the number of shared meetings, the latest shared, finally name.
bool statLess(const PersonTagStat& a, const PersonTagStat& b)
{
    if (a.manual != b.manual) return a.manual;
    if (a.shared != b.shared) return a.shared > b.shared;
    if (a.lastShared != b.lastShared) {
        if (!a.lastShared.isValid()) return false;
        if (!b.lastShared.isValid()) return true;
        return a.lastShared > b.lastShared;
    }
    const int c = text::fold(a.name).localeAwareCompare(text::fold(b.name));
    return c != 0 ? c < 0 : a.personId < b.personId;
}

} // namespace

QVector<PersonTagStat> TagService::peopleStats(const QString& tagId, int limit) const
{
    QVector<PersonTagStat> out;
    if (d->indexOf(tagId) < 0) return out;
    const auto st = d->currentStats();
    PersonIds people = peopleWith(tagId);
    for (const PersonId& n : st->peopleOf(tagId)) {
        if (d->isSelf(n) || people.contains(n)) continue;
        if (d->people && !d->people->contains(n)) continue;   // a person no longer known
        if (persontags::learnedLink(st->shared(n, tagId), st->tagTotal(tagId))) people << n;
    }
    for (const PersonId& n : std::as_const(people)) {
        PersonTagStat s = d->statOf(*st, n, tagId);
        s.manual = d->people && d->people->tags(n).contains(tagId);
        s.rejected = d->personRejected(n, tagId, QString());
        out.append(s);
    }
    std::sort(out.begin(), out.end(), statLess);
    if (limit > 0 && out.size() > limit) out.resize(limit);
    return out;
}

QVector<PersonTagStat> TagService::personTagStats(const PersonId& person) const
{
    QVector<PersonTagStat> out;
    if (person.isNull() || d->isSelf(person)) return out;
    const auto st = d->currentStats();
    const QStringList manual = tagsOfPerson(person);
    QStringList ids = manual;
    for (const QString& t : st->tagsOf(person))
        if (!ids.contains(t) && d->indexOf(t) >= 0) ids << t;
    for (const QString& t : std::as_const(ids)) {
        PersonTagStat s = d->statOf(*st, person, t);
        s.manual = manual.contains(t);
        s.rejected = d->personRejected(person, t, QString());
        out.append(s);
    }
    std::sort(out.begin(), out.end(), [this](const PersonTagStat& a, const PersonTagStat& b) {
        if (a.manual != b.manual) return a.manual;
        if (a.shared != b.shared) return a.shared > b.shared;
        return text::fold(tag(a.tagId).name).localeAwareCompare(text::fold(tag(b.tagId).name)) < 0;
    });
    return out;
}

QVector<PersonTagStat> TagService::suggestPeopleForTag(const QString& tagId, const PersonIds& exclude,
                                                       int limit) const
{
    QVector<PersonTagStat> out;
    for (const PersonTagStat& s : peopleStats(tagId, 0)) {
        if (s.manual || s.rejected) continue;
        if (exclude.contains(s.personId)) continue;
        if (!persontags::learnedLink(s.shared, s.tagTotal)) continue;
        out.append(s);
        if (limit > 0 && out.size() >= limit) break;
    }
    return out;
}

QVector<PersonTagStat> TagService::suggestTagsForPerson(const PersonId& person, int limit) const
{
    QVector<PersonTagStat> out;
    for (const PersonTagStat& s : personTagStats(person)) {
        if (s.manual || s.rejected) continue;
        if (!persontags::suggestableForPerson(s.shared, s.tagTotal, s.personTotal)) continue;
        out.append(s);
    }
    // The more shared meetings, then the tag's larger share (2/3 before 2/20).
    std::stable_sort(out.begin(), out.end(), [](const PersonTagStat& a, const PersonTagStat& b) {
        if (a.shared != b.shared) return a.shared > b.shared;
        const qint64 l = qint64(a.shared) * b.tagTotal, r = qint64(b.shared) * a.tagTotal;
        if (l != r) return l > r;
        return a.lastShared > b.lastShared;
    });
    if (limit > 0 && out.size() > limit) out.resize(limit);
    return out;
}

QVector<TagSuggestion> TagService::suggestTagsForPeople(const PersonIds& input, const QString& meetingId) const
{
    QVector<TagSuggestion> out;
    PersonIds people;
    for (const PersonId& n : input)
        if (!n.isNull() && !d->isSelf(n) && !people.contains(n)) people << n;
    if (people.isEmpty()) return out;
    const QStringList onMeeting = meetingId.isEmpty() ? QStringList() : tagsOf(meetingId);
    QHash<QString, QStringList> carriers;   // tagId -> the names of the participants carrying the tag
    QStringList order;
    for (const PersonId& p : std::as_const(people))
        for (const QString& t : tagsOfPerson(p)) {
            if (!carriers.contains(t)) order << t;
            carriers[t] << d->nameOf(p);
        }
    const int need = people.size() == 1 ? 1 : 2;
    for (const QString& t : std::as_const(order)) {
        const QStringList who = carriers.value(t);
        if (who.size() < need || onMeeting.contains(t)) continue;
        if (!meetingId.isEmpty() && isRejected(meetingId, t)) continue;
        TagSuggestion s;
        s.tagId = t;
        s.name = tag(t).name;
        s.source = SuggestionSource::People;
        s.score = double(who.size()) / double(people.size());
        s.reasons.append({ ReasonKind::PeopleTags, who });
        out.append(s);
    }
    std::stable_sort(out.begin(), out.end(), [](const TagSuggestion& a, const TagSuggestion& b) {
        return a.score > b.score;
    });
    return out;
}

QVector<TagSuggestion> TagService::suggestMeetingTagsFromParticipants(const QString& meetingId) const
{
    d->ensureRows();
    return suggestTagsForPeople(d->rows.value(meetingId).participants, meetingId);
}

TagEvidence TagService::tagEvidence(const PersonId& person, const QStringList& meetingTagIds,
                                    const QString& meetingId) const
{
    return personTagSnapshot().evidence(person, meetingTagIds, meetingId);
}

QVector<PersonTagQuestion> TagService::learningQuestions(const QString& meetingId, const PersonIds& people) const
{
    if (meetingId.isEmpty()) return {};
    const QStringList meetingTags = tagsOf(meetingId);
    if (meetingTags.isEmpty()) return {};
    const PersonTagSnapshot snap = personTagSnapshot();
    // At startup, until the first background computation is done, everyone would be "new": better not to ask.
    if (snap.stats->meetingCount() == 0 && personStatsBusy()) return {};
    QVector<PersonTagQuestion> out = snap.learningQuestions(
        meetingId, meetingTags, people,
        [this](const PersonId& person, const QString& tagId) { return isRejectedForPerson(person, tagId); });
    // It asks once: an already asked (meeting, person, tag) triple is dropped.
    for (PersonTagQuestion& q : out) {
        PersonIds keep;
        QStringList keepNames;
        for (int i = 0; i < q.people.size(); ++i) {
            if (d->askedLearning.contains(meetingId + QChar(0x1f) + q.people.at(i).toString() + QChar(0x1f) + q.tagId))
                continue;
            keep << q.people.at(i);
            keepNames << q.names.value(i);
        }
        q.people = keep;
        q.names = keepNames;
    }
    out.removeIf([](const PersonTagQuestion& q) { return q.people.isEmpty(); });
    return out;
}

void TagService::markLearningAsked(const QString& meetingId, const QVector<PersonTagQuestion>& questions)
{
    for (const PersonTagQuestion& q : questions)
        for (const PersonId& person : q.people)
            d->askedLearning.insert(meetingId + QChar(0x1f) + person.toString() + QChar(0x1f) + q.tagId);
}

void TagService::answerLearning(const PersonTagQuestion& question, bool accept)
{
    if (question.tagId.isEmpty() || question.people.isEmpty() || !tag(question.tagId).isValid()) return;
    // A question is one undo step (both "Yes" and "No").
    beginGroup(accept ? tr("Címke a személyeken: #%1").arg(tag(question.tagId).name)
                      : tr("Javaslat elutasítása"));
    for (const PersonId& person : question.people) {
        if (accept) addPersonTag(person, question.tagId);
        else rejectPersonTag(person, question.tagId);
    }
    endGroup();
}

void TagService::rejectPersonTag(const PersonId& person, const QString& tagId, const QString& meetingId)
{
    if (person.isNull() || d->indexOf(tagId) < 0) return;
    if (d->personRejected(person, tagId, meetingId)) return;
    StepScope step(this, tr("Javaslat elutasítása"));
    d->markMeeting(meetingId);
    SharedFileLock lock(d->path);
    if (!d->lockOk(lock, "rejectPersonTag")) return;
    d->syncFromDisk();
    d->snapshotSet();
    Rejected r;
    r.person = person;
    r.meetingId = meetingId;
    r.tagId = tagId;
    r.name = tag(tagId).name;
    r.key = tagKey(r.name);
    d->rejected.append(r);
    d->save();
    emit rejectedChanged();
}

bool TagService::isRejectedForPerson(const PersonId& person, const QString& tagId, const QString& meetingId) const
{
    return d->personRejected(person, tagId, meetingId);
}

// ---- person suggestion for a meeting ---------------------------------------------------------

QVector<PersonTagStat> TagService::suggestPeopleForMeeting(const QString& meetingId, const PersonIds& exclude,
                                                           int limit) const
{
    QVector<PersonTagStat> out;
    if (meetingId.isEmpty()) return out;
    PersonIds seen = exclude;
    for (const QString& tagId : tagsOf(meetingId)) {
        for (const PersonTagStat& st : suggestPeopleForTag(tagId, seen, 0)) {
            if (d->isSelf(st.personId) || isPersonRejectedForMeeting(st.personId, meetingId)
                || d->personRejected(st.personId, tagId, meetingId))
                continue;
            out.append(st);
            seen << st.personId;
            if (limit > 0 && out.size() >= limit) return out;
        }
    }
    return out;
}

void TagService::rejectPersonForMeeting(const PersonId& person, const QString& meetingId)
{
    if (person.isNull() || meetingId.isEmpty() || isPersonRejectedForMeeting(person, meetingId)) return;
    StepScope step(this, tr("Javaslat elutasítása: %1").arg(d->nameOf(person)));
    d->markMeeting(meetingId);
    SharedFileLock lock(d->path);
    if (!d->lockOk(lock, "rejectPersonForMeeting")) return;
    d->syncFromDisk();
    d->snapshotSet();
    Rejected r;
    r.person = person;
    r.meetingId = meetingId;
    d->rejected.append(r);
    d->save();
    emit rejectedChanged();
}

bool TagService::isPersonRejectedForMeeting(const PersonId& person, const QString& meetingId) const
{
    if (person.isNull() || meetingId.isEmpty()) return false;
    for (const Rejected& r : std::as_const(d->rejected))
        if (r.person == person && r.tagId.isEmpty() && r.meetingId == meetingId)
            return true;
    return false;
}

QVector<PersonRejectionMove> TagService::movePersonRefs(const PersonId& drop, const PersonId& keep)
{
    QVector<PersonRejectionMove> moves;
    if (drop.isNull() || keep.isNull() || drop == keep) return moves;
    // The undo stack's snapshots of the dropped person: they must not overwrite the merged record.
    for (UndoStep& s : d->undo) {
        s.persons.remove(drop);
        for (Rejected& r : s.rejected)
            if (r.person == drop) r.person = keep;
    }
    SharedFileLock lock(d->path);
    if (!d->lockOk(lock, "movePersonRefs")) return moves;
    d->syncFromDisk();
    auto has = [&](const PersonId& who, const QString& tagId, const QString& meetingId) {
        return std::any_of(d->rejected.cbegin(), d->rejected.cend(), [&](const Rejected& x) {
            return x.person == who && x.tagId == tagId && x.meetingId == meetingId;
        });
    };
    QVector<Rejected> rej;
    for (const Rejected& r : std::as_const(d->rejected)) {
        if (r.person != drop) { rej.append(r); continue; }
        PersonRejectionMove m{r.tagId, r.meetingId, has(keep, r.tagId, r.meetingId)};
        moves.append(m);
        if (m.keepHadIt) continue;
        Rejected moved = r;
        moved.person = keep;
        rej.append(moved);
    }
    if (!moves.isEmpty()) {
        d->rejected = rej;
        d->save();
        emit rejectedChanged();
    }
    d->invalidateStats();
    emit personTagsChanged(keep.toString());
    return moves;
}

void TagService::undoMovePersonRefs(const PersonId& keep, const PersonId& drop,
                                    const QVector<PersonRejectionMove>& moves)
{
    if (keep.isNull() || drop.isNull()) return;
    SharedFileLock lock(d->path);
    if (!d->lockOk(lock, "undoMovePersonRefs")) return;
    d->syncFromDisk();
    for (const PersonRejectionMove& m : moves) {
        if (!m.keepHadIt) {
            d->rejected.erase(std::remove_if(d->rejected.begin(), d->rejected.end(), [&](const Rejected& r) {
                return r.person == keep && r.tagId == m.tagId && r.meetingId == m.meetingId;
            }), d->rejected.end());
        }
        Rejected back;
        back.person = drop;
        back.tagId = m.tagId;
        back.meetingId = m.meetingId;
        if (!m.tagId.isEmpty()) {
            back.name = tag(m.tagId).name;
            back.key = tagKey(back.name);
        }
        d->rejected.append(back);
    }
    if (!moves.isEmpty()) {
        d->save();
        emit rejectedChanged();
    }
    d->invalidateStats();
    emit personTagsChanged(QString());
}

void TagService::forgetPerson(const PersonId& person)
{
    if (person.isNull()) return;
    for (UndoStep& s : d->undo) {
        s.persons.remove(person);
        s.rejected.erase(std::remove_if(s.rejected.begin(), s.rejected.end(), [&](const Rejected& r) {
            return r.person == person;
        }), s.rejected.end());
    }
    SharedFileLock lock(d->path);
    if (!d->lockOk(lock, "forgetPerson")) return;
    d->syncFromDisk();
    const auto before = d->rejected.size();
    d->rejected.erase(std::remove_if(d->rejected.begin(), d->rejected.end(), [&](const Rejected& r) {
        return r.person == person;
    }), d->rejected.end());
    if (d->rejected.size() != before) {
        d->save();
        emit rejectedChanged();
    }
    d->invalidateStats();
    emit personTagsChanged(person.toString());
}

} // namespace tanara::library

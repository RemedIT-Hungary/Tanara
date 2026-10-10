#include "tanara/tags/TagService.h"
#include "tanara/tags/MeetingProfiles.h"
#include "tanara/tags/TagNames.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/TextFold.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/SharedFile.h"

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

namespace tanara {

namespace {

constexpr int kUndoLimit = 50;

struct TagRec {
    Tag         tag;
    QDateTime   lastUsedAt;   // a legutóbbi felrakás ideje
    QStringList aliases;      // összevont címkék régi nevei
};

struct Rejected {
    QString meetingId;   // üres → személy-szintű, tartós (person kitöltve)
    QString person;      // nem üres → (személy, címke) elutasítás
    QString tagId;     // üres → név szerinti (pl. az LLM új név-ötlete)
    QString name;      // az eredeti név (a kulcs ebből számolódik)
    QString key;
};

struct Row {
    QString     title;
    QDateTime   startedAt;
    QStringList tagIds;
    QStringList participants;
    qint64      durationMs = 0;
};

struct UndoStep {
    QString label;
    bool    hasSet = false;
    QVector<TagRec>   tags;
    QVector<Rejected> rejected;
    QHash<QString, QStringList> meetings;   // meetingId → címkék előtte
    QHash<QString, QStringList> persons;    // személy → kézi címkék előtte
    bool isEmpty() const { return !hasSet && meetings.isEmpty() && persons.isEmpty(); }
};

bool samePerson(const QString& a, const QString& b)
{
    return persontags::personKey(a) == persontags::personKey(b);
}

} // namespace

struct TagService::Impl {
    TagService*   q = nullptr;
    MeetingStore* store = nullptr;
    MeetingProfiles* profiles = nullptr;
    PeopleStore*  people = nullptr;
    std::function<QString()> selfProvider;
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

    // ---- tanult kapcsolat (személy ↔ címke) ----
    mutable std::shared_ptr<const PersonTagStats> stats = std::make_shared<PersonTagStats>();
    mutable bool statsDirty = true;
    bool statsAsync = false;
    bool statsRunning = false;
    bool statsAgain = false;
    int  statsGen = 0;
    std::shared_ptr<std::atomic_bool> alive = std::make_shared<std::atomic_bool>(true);

    // ---- fájl ----
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
            r.person = o.value(QStringLiteral("person")).toString().trimmed();
            r.tagId = o.value(QStringLiteral("tagId")).toString();
            r.name = o.value(QStringLiteral("name")).toString();
            r.key = tagKey(r.name);
            if (r.meetingId.isEmpty() && r.person.isEmpty()) continue;
            if (!r.person.isEmpty() && r.tagId.isEmpty()) continue;
            if (r.tagId.isEmpty() && r.key.isEmpty()) continue;
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
            if (!r.person.isEmpty()) o.insert(QStringLiteral("person"), r.person);
            if (!r.tagId.isEmpty()) o.insert(QStringLiteral("tagId"), r.tagId);
            if (!r.name.isEmpty()) o.insert(QStringLiteral("name"), r.name);
            ra.append(o);
        }
        const QJsonObject root{{QStringLiteral("version"), 1},
                               {QStringLiteral("tags"), ta},
                               {QStringLiteral("rejected"), ra}};
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile f(path);
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
        const bool ok = f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
        stamp = FileStamp::of(path);
        return ok;
    }
    // Módosítás előtt: ha egy másik folyamat közben írta a fájlt, annak állapotára építünk.
    void syncFromDisk() {
        if (FileStamp::of(path) != stamp) load();
    }

    // ---- keresés ----
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

    // ---- meeting-gyorsítótár ----
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
        return Row{ m.title, m.startedAt, m.tagIds, MeetingLibrary::participantsOf(m), m.durationMs };
    }
    void reloadRow(const QString& id) {
        invalidateStats();
        if (!rowsLoaded || !store) return;
        const Meeting m = store->load(id);
        if (m.id.isEmpty()) rows.remove(id);
        else rows.insert(id, rowOf(m));
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

    // ---- visszavonás ----
    void snapshotSet() {
        if (undoing || current.hasSet) return;
        current.hasSet = true;
        current.tags = tags;
        current.rejected = rejected;
    }
    void snapshotMeeting(const QString& meetingId) {
        if (undoing || current.meetings.contains(meetingId)) return;
        ensureRows();
        current.meetings.insert(meetingId, rows.value(meetingId).tagIds);
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
        undo.append(current);
        while (undo.size() > kUndoLimit) undo.removeFirst();
        current = UndoStep{};
        emit q->undoChanged();
    }

    // A meeting címkéinek kiírása (meeting.json). true, ha változott.
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
        store->saveMeeting(m);   // meetingUpdated → a sor újratöltődik
        ensureRows();
        rows[meetingId].tagIds = clean;
        invalidateStats();
        emit q->meetingTagsChanged(meetingId);
        return true;
    }
    // ---- személyek ----
    QString self() const { return selfProvider ? selfProvider().trimmed() : QString(); }
    bool isSelf(const QString& name) const {
        const QString s = self();
        return !s.isEmpty() && samePerson(s, name);
    }
    void snapshotPerson(const QString& name) {
        if (undoing || !people) return;
        for (auto it = current.persons.constBegin(); it != current.persons.constEnd(); ++it)
            if (samePerson(it.key(), name)) return;
        current.persons.insert(name.trimmed(), people->tags(name));
    }
    // A személy kézi címkéinek kiírása (people.json). true, ha változott.
    bool writePersonTags(const QString& name, const QStringList& ids) {
        if (!people) return false;
        people->refresh();
        QStringList clean;
        for (const QString& id : ids)
            if (!id.isEmpty() && !clean.contains(id)) clean << id;
        if (people->tags(name) == clean) return false;
        snapshotPerson(name);
        if (!people->setTags(name, clean)) return false;
        emit q->personTagsChanged(name.trimmed());
        return true;
    }
    // Minden személyen: a `from` címke `to`-ra cserélve (üres `to` = levétel).
    void replacePersonTag(const QString& from, const QString& to) {
        if (!people) return;
        people->refresh();
        const QHash<QString, QStringList> all = people->allTags();
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            QStringList t = it.value();
            const int pos = t.indexOf(from);
            if (pos < 0) continue;
            if (to.isEmpty() || t.contains(to)) t.removeAt(pos);
            else t[pos] = to;
            writePersonTags(it.key(), t);
        }
    }

    // ---- statisztika ----
    QVector<PersonTagRow> statRows() const {
        QVector<PersonTagRow> out;
        out.reserve(rows.size());
        for (auto it = rows.constBegin(); it != rows.constEnd(); ++it)
            out.append(PersonTagRow{ it.key(), it->startedAt, it->tagIds, it->participants });
        return out;
    }
    // A sorok a lemezről (háttérszálon; csak fájl-olvasás).
    static QVector<PersonTagRow> readRows(const QVector<QPair<QString, QString>>& folders) {
        QVector<PersonTagRow> out;
        for (const auto& f : folders) {
            const Meeting m = MeetingStore::readMeetingFolder(f.second);
            if (m.id.isEmpty() && m.title.isEmpty()) continue;
            out.append(PersonTagRow{ f.first, m.startedAt, m.tagIds, MeetingLibrary::participantsOf(m) });
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
        const QString selfName = self();
        // A már betöltött sorok másolata; különben a háttérszál olvassa a meeting.json-okat.
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
            QMetaObject::invokeMethod(q, [this, gen, result, guard] {
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
    PersonTagStat statOf(const PersonTagStats& st, const QString& name, const QString& tagId) const {
        PersonTagStat r;
        r.name = st.displayName(name);
        r.tagId = tagId;
        r.shared = st.shared(name, tagId);
        r.tagTotal = st.tagTotal(tagId);
        r.personTotal = st.personTotal(name);
        r.lastShared = st.lastShared(name, tagId);
        return r;
    }
    bool personRejected(const QString& name, const QString& tagId, const QString& meetingId) const {
        for (const Rejected& r : rejected) {
            if (r.person.isEmpty() || r.tagId != tagId || !samePerson(r.person, name)) continue;
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
            if (r.meetingId != meetingId || !r.person.isEmpty()) return false;
            return (!tagId.isEmpty() && r.tagId == tagId) || (!key.isEmpty() && r.key == key);
        }), rejected.end());
        if (rejected.size() != before) emit q->rejectedChanged();
    }
};

// Egy nyilvános hívás = egy visszavonási lépés (vagy a nyitott csoport része).
class StepScope {
public:
    StepScope(TagService* s, const QString& label) : m_s(s) { m_s->beginGroup(label); }
    ~StepScope() { m_s->endGroup(); }
private:
    TagService* m_s;
};

TagService::TagService(MeetingStore* store, const QString& tagsFile, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    d->store = store;
    d->path = tagsFile;
    d->load();
    if (store) {
        auto refresh = [this](const QString& id) { d->reloadRow(id); };
        connect(store, &MeetingStore::meetingUpdated, this, refresh);
        connect(store, &MeetingStore::meetingAdded, this, refresh);
        connect(store, &MeetingStore::meetingRemoved, this, [this](const QString& id) {
            const bool had = d->rowsLoaded && d->rows.remove(id) > 0;
            d->invalidateStats();
            const auto before = d->rejected.size();
            d->rejected.erase(std::remove_if(d->rejected.begin(), d->rejected.end(),
                                             [&](const Rejected& r) { return r.meetingId == id; }),
                              d->rejected.end());
            if (d->rejected.size() != before) {
                SharedFileLock lock(d->path);
                d->save();
                emit rejectedChanged();
            }
            if (had) emit tagsChanged();   // a darabszámok változtak
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

// ---- készlet ----------------------------------------------------------------------------

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
        return textfold::fold(a.tag.name).localeAwareCompare(textfold::fold(b.tag.name)) < 0;
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

    const QString fq = textfold::fold(norm);
    const QString key = tagKey(norm);
    const int exactIdx = d->indexOfKey(key, true);

    struct Cand { int idx; int tier; int pos; };
    QVector<Cand> cands;
    for (int i = 0; i < d->tags.size(); ++i) {
        if (i == exactIdx) continue;
        const QString fn = textfold::fold(d->tags.at(i).tag.name);
        const int pos = fn.indexOf(fq);
        int tier = -1;
        if (pos == 0) tier = 0;
        else if (pos > 0) tier = fn.at(pos - 1).isLetterOrNumber() ? 2 : 1;   // szó eleje → „előtag”
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
        const int pos = textfold::fold(d->tags.at(exactIdx).tag.name).indexOf(fq);
        addMatch(exactIdx, pos);
    }
    for (const Cand& c : std::as_const(cands)) {
        if (rows.size() >= limit) break;
        addMatch(c.idx, c.pos);
    }
    if (exactIdx >= 0) return rows;   // pontos egyezés: nincs „Új címke” sor

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
    d->syncFromDisk();
    const int i = d->indexOf(id);
    if (i < 0) return false;
    const int other = d->indexOfKey(tagKey(norm), false);
    if (other >= 0 && other != i) return false;
    if (d->tags.at(i).tag.name == norm) return true;
    d->snapshotSet();
    // A régi név egy másik címke álneve volt? Akkor onnan lekerül (ez a címke viseli).
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
    d->syncFromDisk();
    const int fi = d->indexOf(fromId), ki = d->indexOf(keepId);
    if (fi < 0 || ki < 0) return;
    d->snapshotSet();
    d->ensureRows();
    // Minden meeting átcímkézése (a megtartott címke a régi helyére kerül, ha még nincs rajta).
    const QStringList ids = d->rows.keys();
    for (const QString& mid : ids) {
        QStringList t = d->rows.value(mid).tagIds;
        const int pos = t.indexOf(fromId);
        if (pos < 0) continue;
        if (t.contains(keepId)) t.removeAt(pos);
        else t[pos] = keepId;
        d->writeMeetingTags(mid, t);
    }
    // Elutasítások: a megszűnő címkéé a megtartottra száll (ismétlés nélkül).
    QVector<Rejected> rej;
    for (Rejected r : std::as_const(d->rejected)) {
        if (r.tagId == fromId) r.tagId = keepId;
        const bool dup = std::any_of(rej.cbegin(), rej.cend(), [&](const Rejected& x) {
            return x.meetingId == r.meetingId && samePerson(x.person, r.person)
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

// ---- meetingek --------------------------------------------------------------------------

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
    if (!d->writeMeetingTags(meetingId, clean)) return;
    SharedFileLock lock(d->path);
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

// ---- levezetett ---------------------------------------------------------------------------

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
    QHash<QString, int> people;
    for (const QString& mid : mids) {
        const Row r = d->rows.value(mid);
        p.meetings.append({ mid, r.title, r.startedAt, r.durationMs });
        for (const QString& n : r.participants) people[n]++;
    }
    for (auto it = people.constBegin(); it != people.constEnd(); ++it)
        p.topParticipants.append({ it.key(), it.value() });
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

// ---- elutasítás ---------------------------------------------------------------------------

void TagService::reject(const QString& meetingId, const TagSuggestion& suggestion)
{
    if (meetingId.isEmpty()) return;
    if (isRejected(meetingId, suggestion.tagId.isEmpty() ? suggestion.name : suggestion.tagId)) return;
    StepScope step(this, tr("Javaslat elutasítása"));
    SharedFileLock lock(d->path);
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
        if (r.meetingId != meetingId || !r.person.isEmpty()) continue;
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
    d->syncFromDisk();
    d->snapshotSet();
    d->rejected.clear();
    d->save();
    emit rejectedChanged();
}

// ---- visszavonás --------------------------------------------------------------------------

QString TagService::beginGroup(const QString& label)
{
    d->openStep(label);
    return d->current.label;
}

void TagService::endGroup() { d->closeStep(); }

bool TagService::canUndo() const { return !d->undo.isEmpty(); }

QString TagService::undoLabel() const { return d->undo.isEmpty() ? QString() : d->undo.last().label; }

void TagService::undo()
{
    if (d->undo.isEmpty() || d->groupDepth > 0) return;
    const UndoStep step = d->undo.takeLast();
    d->undoing = true;
    if (step.hasSet) {
        SharedFileLock lock(d->path);
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

// ---- személyek: kézi címkék ------------------------------------------------------------------

void TagService::setPeopleStore(PeopleStore* people) { d->people = people; }

void TagService::setSelfNameProvider(std::function<QString()> provider)
{
    d->selfProvider = std::move(provider);
    d->invalidateStats();
}

QString TagService::selfName() const { return d->self(); }

bool TagService::isSelf(const QString& name) const { return d->isSelf(name); }

QStringList TagService::tagsOfPerson(const QString& name) const
{
    QStringList out;
    if (!d->people || name.trimmed().isEmpty() || d->isSelf(name)) return out;
    d->people->refresh();
    for (const QString& id : d->people->tags(name))
        if (d->indexOf(id) >= 0) out << id;
    return out;
}

bool TagService::setPersonTags(const QString& name, const QStringList& ids)
{
    if (!d->people || name.trimmed().isEmpty() || d->isSelf(name)) return false;
    StepScope step(this, tr("Személy címkéinek módosítása"));
    QStringList clean;
    for (const QString& id : ids)
        if (d->indexOf(id) >= 0 && !clean.contains(id)) clean << id;
    // A készletből időközben eltűnt azonosítók a rekordból is lekerülnek.
    const QStringList before = tagsOfPerson(name);
    if (!d->writePersonTags(name, clean)) return true;
    SharedFileLock lock(d->path);
    d->syncFromDisk();
    d->snapshotSet();
    for (const QString& id : clean)
        if (!before.contains(id)) d->touch(id);
    d->save();
    emit tagsChanged();
    return true;
}

Tag TagService::addPersonTag(const QString& name, const QString& nameOrId)
{
    if (!d->people || name.trimmed().isEmpty() || d->isSelf(name)) return {};
    StepScope step(this, tr("Címke a személyen"));
    Tag t = tag(nameOrId);
    if (!t.isValid()) t = create(nameOrId);
    if (!t.isValid()) return {};
    QStringList ids = tagsOfPerson(name);
    if (!ids.contains(t.id)) setPersonTags(name, ids << t.id);
    return t;
}

void TagService::removePersonTag(const QString& name, const QString& id)
{
    StepScope step(this, tr("Címke levétele a személyről"));
    QStringList ids = tagsOfPerson(name);
    if (ids.removeAll(id) == 0) return;
    setPersonTags(name, ids);
}

QStringList TagService::peopleWith(const QString& tagId) const
{
    QStringList out;
    if (!d->people || d->indexOf(tagId) < 0) return out;
    d->people->refresh();
    const QHash<QString, QStringList> all = d->people->allTags();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
        if (it.value().contains(tagId) && !d->isSelf(it.key())) out << it.key();
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return textfold::fold(a).localeAwareCompare(textfold::fold(b)) < 0;
    });
    return out;
}

// ---- személyek: tanult kapcsolat + javaslatok ------------------------------------------------

void TagService::ensurePersonStats()
{
    d->statsAsync = true;
    if (d->statsDirty) d->startStats();
}

bool TagService::personStatsBusy() const { return d->statsRunning; }

void TagService::invalidatePersonStats() { d->invalidateStats(); }

void TagService::refreshPersonStatsNow()
{
    ++d->statsGen;   // a futó háttér-eredmény elavult
    d->computeStatsNow();
    emit personStatsChanged();
}

std::shared_ptr<const PersonTagStats> TagService::personStats() const { return d->currentStats(); }

PersonTagSnapshot TagService::personTagSnapshot() const
{
    PersonTagSnapshot snap;
    snap.stats = d->currentStats();
    snap.selfKey = persontags::personKey(d->self());
    for (const TagRec& r : std::as_const(d->tags)) snap.tagNames.insert(r.tag.id, r.tag.name);
    if (d->people) {
        d->people->refresh();
        const QHash<QString, QStringList> all = d->people->allTags();
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            QStringList ids;
            for (const QString& id : it.value())
                if (snap.tagNames.contains(id)) ids << id;
            if (!ids.isEmpty()) snap.manual.insert(persontags::personKey(it.key()), ids);
        }
    }
    return snap;
}

namespace {

// Kézi előbb, aztán a közös megbeszélések száma, a legutóbbi közös, végül név.
bool statLess(const PersonTagStat& a, const PersonTagStat& b)
{
    if (a.manual != b.manual) return a.manual;
    if (a.shared != b.shared) return a.shared > b.shared;
    if (a.lastShared != b.lastShared) {
        if (!a.lastShared.isValid()) return false;
        if (!b.lastShared.isValid()) return true;
        return a.lastShared > b.lastShared;
    }
    return textfold::fold(a.name).localeAwareCompare(textfold::fold(b.name)) < 0;
}

} // namespace

QVector<PersonTagStat> TagService::peopleStats(const QString& tagId, int limit) const
{
    QVector<PersonTagStat> out;
    if (d->indexOf(tagId) < 0) return out;
    const auto st = d->currentStats();
    QStringList names = peopleWith(tagId);
    for (const QString& n : st->peopleOf(tagId)) {
        if (d->isSelf(n)) continue;
        if (std::any_of(names.cbegin(), names.cend(), [&](const QString& x) { return samePerson(x, n); })) continue;
        if (persontags::learnedLink(st->shared(n, tagId), st->tagTotal(tagId))) names << n;
    }
    for (const QString& n : std::as_const(names)) {
        PersonTagStat s = d->statOf(*st, n, tagId);
        s.manual = d->people && d->people->tags(n).contains(tagId);
        if (s.manual) s.name = n;   // a rekord írásmódja
        s.rejected = d->personRejected(n, tagId, QString());
        out.append(s);
    }
    std::sort(out.begin(), out.end(), statLess);
    if (limit > 0 && out.size() > limit) out.resize(limit);
    return out;
}

QVector<PersonTagStat> TagService::personTagStats(const QString& name) const
{
    QVector<PersonTagStat> out;
    if (name.trimmed().isEmpty() || d->isSelf(name)) return out;
    const auto st = d->currentStats();
    const QStringList manual = tagsOfPerson(name);
    QStringList ids = manual;
    for (const QString& t : st->tagsOf(name))
        if (!ids.contains(t) && d->indexOf(t) >= 0) ids << t;
    for (const QString& t : std::as_const(ids)) {
        PersonTagStat s = d->statOf(*st, name, t);
        s.name = name.trimmed();
        s.manual = manual.contains(t);
        s.rejected = d->personRejected(name, t, QString());
        out.append(s);
    }
    std::sort(out.begin(), out.end(), [this](const PersonTagStat& a, const PersonTagStat& b) {
        if (a.manual != b.manual) return a.manual;
        if (a.shared != b.shared) return a.shared > b.shared;
        return textfold::fold(tag(a.tagId).name).localeAwareCompare(textfold::fold(tag(b.tagId).name)) < 0;
    });
    return out;
}

QVector<PersonTagStat> TagService::suggestPeopleForTag(const QString& tagId, const QStringList& excludeNames,
                                                       int limit) const
{
    QVector<PersonTagStat> out;
    for (const PersonTagStat& s : peopleStats(tagId, 0)) {
        if (s.manual || s.rejected) continue;
        if (std::any_of(excludeNames.cbegin(), excludeNames.cend(),
                        [&](const QString& x) { return samePerson(x, s.name); })) continue;
        if (!persontags::learnedLink(s.shared, s.tagTotal)) continue;
        out.append(s);
        if (limit > 0 && out.size() >= limit) break;
    }
    return out;
}

QVector<PersonTagStat> TagService::suggestTagsForPerson(const QString& name, int limit) const
{
    QVector<PersonTagStat> out;
    for (const PersonTagStat& s : personTagStats(name)) {
        if (s.manual || s.rejected) continue;
        if (!persontags::suggestableForPerson(s.shared, s.tagTotal, s.personTotal)) continue;
        out.append(s);
    }
    std::stable_sort(out.begin(), out.end(), statLess);
    if (limit > 0 && out.size() > limit) out.resize(limit);
    return out;
}

QVector<TagSuggestion> TagService::suggestTagsForPeople(const QStringList& names, const QString& meetingId) const
{
    QVector<TagSuggestion> out;
    QStringList people;
    for (const QString& n : names)
        if (!n.trimmed().isEmpty() && !d->isSelf(n)
            && std::none_of(people.cbegin(), people.cend(), [&](const QString& x) { return samePerson(x, n); }))
            people << n.trimmed();
    if (people.isEmpty()) return out;
    const QStringList onMeeting = meetingId.isEmpty() ? QStringList() : tagsOf(meetingId);
    QHash<QString, QStringList> carriers;   // tagId → a címkét viselő résztvevők
    QStringList order;
    for (const QString& p : std::as_const(people))
        for (const QString& t : tagsOfPerson(p)) {
            if (!carriers.contains(t)) order << t;
            carriers[t] << p;
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

TagEvidence TagService::tagEvidence(const QString& personName, const QStringList& meetingTagIds) const
{
    return personTagSnapshot().evidence(personName, meetingTagIds);
}

void TagService::rejectPersonTag(const QString& name, const QString& tagId, const QString& meetingId)
{
    if (name.trimmed().isEmpty() || d->indexOf(tagId) < 0) return;
    if (d->personRejected(name, tagId, meetingId)) return;
    StepScope step(this, tr("Javaslat elutasítása"));
    SharedFileLock lock(d->path);
    d->syncFromDisk();
    d->snapshotSet();
    Rejected r;
    r.person = name.trimmed();
    r.meetingId = meetingId;
    r.tagId = tagId;
    r.name = tag(tagId).name;
    r.key = tagKey(r.name);
    d->rejected.append(r);
    d->save();
    emit rejectedChanged();
}

bool TagService::isRejectedForPerson(const QString& name, const QString& tagId, const QString& meetingId) const
{
    return d->personRejected(name, tagId, meetingId);
}

void TagService::renamePersonRefs(const QString& oldName, const QString& newName)
{
    const QString o = oldName.trimmed(), n = newName.trimmed();
    if (o.isEmpty() || n.isEmpty() || o == n) return;
    // A verem személy-pillanatképei: tiszta átnevezésnél követik a nevet; összevonásnál (a cél
    // már létezik) a megszűnő név pillanatképe eldobódik — nem írhatná felül az uniót.
    bool merging = false;
    if (d->people && !samePerson(o, n)) {
        d->people->refresh();
        const QStringList listed = d->people->names();
        merging = !d->people->details(n).isEmpty()
            || std::any_of(listed.cbegin(), listed.cend(), [&](const QString& x) { return samePerson(x, n); });
    }
    for (UndoStep& s : d->undo) {
        for (const QString& k : s.persons.keys()) {
            if (!samePerson(k, o)) continue;
            const QStringList tags = s.persons.take(k);
            if (!merging) s.persons.insert(n, tags);
        }
    }
    SharedFileLock lock(d->path);
    d->syncFromDisk();
    QVector<Rejected> rej;
    bool changed = false;
    for (Rejected r : std::as_const(d->rejected)) {
        if (!r.person.isEmpty() && samePerson(r.person, o)) { r.person = n; changed = true; }
        const bool dup = std::any_of(rej.cbegin(), rej.cend(), [&](const Rejected& x) {
            return !x.person.isEmpty() && samePerson(x.person, r.person) && x.meetingId == r.meetingId
                && x.tagId == r.tagId;
        });
        if (!dup) rej.append(r);
    }
    if (changed) {
        d->rejected = rej;
        d->save();
        emit rejectedChanged();
    }
    d->invalidateStats();
    emit personTagsChanged(n);
}

void TagService::forgetPerson(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    for (UndoStep& s : d->undo)
        for (const QString& k : s.persons.keys())
            if (samePerson(k, n)) s.persons.remove(k);
    SharedFileLock lock(d->path);
    d->syncFromDisk();
    const auto before = d->rejected.size();
    d->rejected.erase(std::remove_if(d->rejected.begin(), d->rejected.end(), [&](const Rejected& r) {
        return !r.person.isEmpty() && samePerson(r.person, n);
    }), d->rejected.end());
    if (d->rejected.size() != before) {
        d->save();
        emit rejectedChanged();
    }
    d->invalidateStats();
    emit personTagsChanged(n);
}

} // namespace tanara

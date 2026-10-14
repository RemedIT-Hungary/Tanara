#include "tanara/library/people/PeopleService.h"

#include "tanara/library/people/PersonReferenceEditor.h"
#include "tanara/library/people/PeopleStats.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/library/tracks/TrackCatalog.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace tanara::library {

namespace {

constexpr int kMaxUndoSteps = 20;

using persons::sameName;

// "file#startMs-endMs" -> file + range (the range may be omitted).
void parseSampleRef(const QString& ref, QString* file, qint64* startMs, qint64* endMs)
{
    *file = ref;
    *startMs = *endMs = 0;
    const int hash = ref.lastIndexOf(QLatin1Char('#'));
    if (hash < 0) return;
    *file = ref.left(hash);
    const QString range = ref.mid(hash + 1);
    const int dash = range.indexOf(QLatin1Char('-'));
    if (dash <= 0) return;
    *startMs = range.left(dash).toLongLong();
    *endMs = range.mid(dash + 1).toLongLong();
}

// The trace of a stale mark (for undo): which meeting's which keys were added.
struct StaleMark {
    QString meetingId;
    QString folder;
    QStringList keys;
};

struct UndoStep {
    PeopleUndoKind kind = PeopleUndoKind::None;
    PersonId person;        // sample: the (former) owner; nickname / rename: the person; merge: the survivor
    PersonId other;         // move: the target
    Voiceprint print;       // for sample steps (the selected voiceprint)
    QVector<Voiceprint> prints;   // ...and all its siblings (the same sample with other models), together with it
    QString detail;
    QString alias;
    int aliasIndex = -1;
    QString oldName;        // rename: the name before
    QString newName;        // rename: the name after
    bool createdPerson = false;     // the move created the target person
    Person createdRecord;           // ...its record (undo deletes it if nothing else is bound to it)
    bool aliasWasPresent = false;   // rename: the new name was already a nickname
    bool aliasAdded = false;        // rename: the old name became a nickname now
    PersonMergeUndo merge;          // merge: what changed
    QVector<StaleMark> marks;
};

} // namespace

void ensureSelfPerson(PeopleStore* people, const QString& selfName)
{
    if (!people || people->legacy()) return;
    people->refresh();
    if (!people->selfId().isNull() && people->contains(people->selfId())) return;
    const QString name = selfName.trimmed();
    if (name.isEmpty()) return;
    const PersonId id = people->ensure(name);
    if (!id.isNull()) people->setSelfId(id);
}

struct PeopleService::Private {
    PeopleService* q = nullptr;
    MeetingRepository* store = nullptr;
    PeopleStore* people = nullptr;
    VoiceprintStore* voiceprints = nullptr;
    PeopleStats* stats = nullptr;
    TagService* tags = nullptr;
    PersonReferenceEditor* refs = nullptr;
    VoiceprintMaterialSource* material = nullptr;
    std::function<QString()> selfName;
    std::function<void(const QString&)> setSelfName;
    QVector<UndoStep> undo;

    QString settingsName() const { return selfName ? selfName() : QString(); }
    void writeSettingsName(const QString& name) { if (setSelfName) setSelfName(name); }

    PersonId self() const { return people ? people->selfId() : PersonId(); }
    QString nameOf(const PersonId& person) const { return people ? people->displayName(person) : QString(); }
    bool known(const PersonId& person) const { return people && people->contains(person); }

    PersonRecord record(const Person& p) const
    {
        PersonRecord r;
        r.id = p.id;
        r.name = p.displayName;
        r.aliases = p.aliases;
        r.note = p.note;
        r.isSelf = !p.id.isNull() && p.id == self();
        r.defaultSide = p.defaultSide;
        r.defaultSideManual = p.defaultSideManual;
        r.sampleCount = voiceprints ? voiceprints->sampleCount(p.id) : 0;
        return r;
    }

    void push(const UndoStep& step)
    {
        undo.append(step);
        while (undo.size() > kMaxUndoSteps) undo.removeFirst();
        emit q->undoChanged();
    }
    void clearUndo()
    {
        if (undo.isEmpty()) return;
        undo.clear();
        emit q->undoChanged();
    }

    // Marks the summaries of the person's meetings stale ON DISK (without signals);
    // only: only this meeting (empty = all of them).
    QVector<StaleMark> markStale(const PersonId& person, const QString& only = QString())
    {
        QVector<StaleMark> marks;
        if (!stats || !store) return marks;
        const PersonStats ps = stats->stats(person);
        for (const PersonMeetingStat& pm : ps.meetings) {
            if (!only.isEmpty() && pm.meetingId != only) continue;
            if (!pm.hasSummary || pm.speakerKeys.isEmpty()) continue;
            const Meeting m = store->load(pm.meetingId);
            if (m.id.isEmpty() || !m.hasSummary) continue;
            const QStringList added = refs ? refs->markSummaryStaleKeys(m, pm.speakerKeys) : QStringList();
            if (!added.isEmpty()) marks.append(StaleMark{m.id, m.folder, added});
        }
        return marks;
    }
    // After the marking (or its undo): the open editor rereads the disk, and the library / summary tab
    // refreshes the stale flag.
    void announceStale(const QVector<StaleMark>& marks)
    {
        for (const StaleMark& mark : marks) {
            emit q->speakerMapChanged(mark.meetingId);
            emit q->summaryStaleChanged(mark.meetingId);
        }
    }
    bool unmarkStale(const StaleMark& mark) { return refs && refs->unmarkSummaryStale(mark.folder, mark.keys); }

    QString freeAnonymousName() const
    {
        QStringList have;
        if (people) for (const Person& p : people->people()) have << p.displayName;
        for (int i = 1;; ++i) {
            const QString candidate = PeopleService::tr("Névtelen %1").arg(i);
            bool taken = false;
            for (const QString& n : std::as_const(have))
                if (sameName(n, candidate)) { taken = true; break; }
            if (!taken) return candidate;
        }
    }

    // Another person with this name (the People window keeps names unique).
    bool nameTaken(const QString& name, const PersonId& except = PersonId()) const
    {
        if (!people) return false;
        for (const PersonId& id : people->findByName(name))
            if (id != except) return true;
        return false;
    }

    // A rename through the global route: the own person also updates the settings' name (the former
    // AppController::setUserSpeakerName path: settings, then the record).
    void renameGlobally(const PersonId& person, const QString& name)
    {
        if (person == self()) q->setSelfName(name);
        else q->renameEverywhere(person, name);
    }

    void finish(bool people_, bool prints)
    {
        if (prints) emit q->voiceprintsChanged();
        if (people_) emit q->peopleChanged();
        emit q->changed();
    }
};

PeopleService::PeopleService(const Deps& deps, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->q = this;
    d->store = deps.store;
    d->people = deps.people;
    d->voiceprints = deps.voiceprints;
    d->stats = deps.stats;
    d->tags = deps.tags;
    d->refs = deps.references;
    d->material = deps.material;
    d->selfName = deps.selfName;
    d->setSelfName = deps.setSelfName;
}

PeopleService::~PeopleService() = default;

void PeopleService::setEmbedderFactory(VoiceprintMaterialSource::EmbedderFactory factory)
{
    if (d->material) d->material->setEmbedderFactory(std::move(factory));
}

SelfPerson PeopleService::selfPerson() const
{
    SelfPerson self;
    if (d->people) { d->people->refresh(); self.id = d->people->selfId(); }
    self.name = d->settingsName().trimmed();
    return self;
}

void PeopleService::ensureSelf() { ensureSelfPerson(d->people, d->settingsName()); }

void PeopleService::setSelfName(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    if (d->settingsName().trimmed() != n) d->writeSettingsName(n);
    // The own person's record: ONE field (renameEverywhere refreshes the meetings showing it).
    ensureSelf();
    const PersonId self = d->people ? d->people->selfId() : PersonId();
    if (!self.isNull() && d->people->displayName(self) != n) renameEverywhere(self, n);
    else emit peopleChanged();
}

PersonNames PeopleService::personNames() const
{
    if (!d->people) return {};
    d->people->refresh();
    return d->people->names();
}

QStringList PeopleService::knownNames() const
{
    QStringList out;
    if (!d->people) return out;
    d->people->refresh();
    for (const Person& p : d->people->people()) out << p.displayName;
    return out;
}

void PeopleService::reload()
{
    if (d->people) d->people->refresh();
    if (d->voiceprints) d->voiceprints->refresh();
}

PersonId PeopleService::selfId() const { return d->self(); }

bool PeopleService::isSelf(const PersonId& person) const
{
    return !person.isNull() && person == d->self();
}

QVector<PersonRecord> PeopleService::persons() const
{
    QVector<PersonRecord> out;
    if (!d->people) return out;
    const QVector<Person> all = d->people->people();
    out.reserve(all.size());
    for (const Person& p : all) out.append(d->record(p));
    return out;
}

PersonRecord PeopleService::person(const PersonId& person) const
{
    if (!d->known(person)) return PersonRecord();
    return d->record(d->people->person(person));
}

bool PeopleService::exists(const PersonId& person) const { return d->known(person); }

PersonIds PeopleService::findByName(const QString& name) const
{
    return d->people ? d->people->findByName(name) : PersonIds();
}

QVector<VoiceSample> PeopleService::samples(const PersonId& person) const
{
    QVector<VoiceSample> out;
    if (!d->voiceprints) return out;
    QHash<QString, Meeting> meetings;   // a meeting is loaded once
    // Sibling voiceprints (the same sample with other models) as one sample: the representative is the one
    // with the smallest model id; delete / move also act on the siblings.
    QVector<Voiceprint> reps;
    for (const QVector<Voiceprint>& g : VoiceprintStore::siblingGroups(d->voiceprints->printsFor(person)))
        reps.append(g.first());
    for (const Voiceprint& vp : std::as_const(reps)) {
        VoiceSample s;
        s.id = vp.id;
        s.person = person;
        s.meetingId = vp.sourceMeetingId;
        s.sourceKind = QStringLiteral("unknown");
        QString file;
        parseSampleRef(vp.sampleRef, &file, &s.startMs, &s.endMs);

        Meeting m;
        if (d->store && !vp.sourceMeetingId.isEmpty()) {
            auto it = meetings.constFind(vp.sourceMeetingId);
            if (it == meetings.constEnd())
                it = meetings.insert(vp.sourceMeetingId, d->store->load(vp.sourceMeetingId));
            m = it.value();
        }
        if (!m.id.isEmpty()) {
            s.meetingTitle = m.title;
            s.recordedAt = m.startedAt.date();
            if (!file.isEmpty()) {
                s.audioPath = QDir(m.folder).filePath(file);
                s.audioExists = QFileInfo::exists(s.audioPath);
            }
            // The source track: by its id, failing that by its file name.
            const QVector<TrackView> views = TrackCatalog::tracks(m);
            const TrackView* view = nullptr;
            for (const TrackView& v : views)
                if (!vp.sourceTrack.isEmpty() && v.track.id == vp.sourceTrack) { view = &v; break; }
            if (!view && !file.isEmpty())
                for (const TrackView& v : views)
                    if (v.track.file == file) { view = &v; break; }
            if (view) {
                // The sampleRef is in meeting time; the track file starts at the track's offset, so playback needs file time.
                if (view->track.startOffsetMs > 0 && s.endMs > s.startMs) {
                    s.startMs = qMax<qint64>(0, s.startMs - view->track.startOffsetMs);
                    s.endMs = qMax<qint64>(s.startMs, s.endMs - view->track.startOffsetMs);
                }
                switch (view->role) {
                case TrackRole::OwnMic:
                    s.sourceKind = QStringLiteral("mic");
                    // For the own microphone the device's name is the useful one (several microphones, different voices).
                    s.deviceLabel = view->renamed || view->track.deviceName.isEmpty()
                        ? view->displayName : devicenames::displayName(view->track.deviceName);
                    break;
                case TrackRole::CallAudio:
                    s.sourceKind = QStringLiteral("call");
                    s.deviceLabel = view->displayName;
                    break;
                case TrackRole::SystemAudio:
                    s.sourceKind = QStringLiteral("system");
                    s.deviceLabel = view->displayName;
                    break;
                case TrackRole::Other:
                    s.sourceKind = QStringLiteral("mic");
                    s.deviceLabel = view->displayName;
                    break;
                }
            } else if (vp.sourceTrack == QLatin1String("mixdown")
                       || (!file.isEmpty() && file == m.mixdownFile)) {
                s.sourceKind = QStringLiteral("mix");
            }
        }
        if (s.deviceLabel.isEmpty() && s.sourceKind == QLatin1String("unknown") && !vp.device.isEmpty())
            s.deviceLabel = devicenames::displayName(vp.device);
        if (!s.recordedAt.isValid())
            s.recordedAt = QDateTime::fromString(vp.createdAt, Qt::ISODate).date();
        out.append(s);
    }
    std::stable_sort(out.begin(), out.end(), [](const VoiceSample& a, const VoiceSample& b) {
        return a.recordedAt > b.recordedAt;
    });
    return out;
}

double PeopleService::similarity(const PersonId& a, const PersonId& b) const
{
    if (!d->voiceprints) return -1.0;
    const QVector<Voiceprint> pa = d->voiceprints->printsFor(a), pb = d->voiceprints->printsFor(b);
    if (pa.isEmpty() || pb.isEmpty()) return -1.0;
    // The same rule as in recognition: the best matching pair counts.
    double best = 0.0;
    for (const Voiceprint& x : pa)
        for (const Voiceprint& y : pb)
            best = std::max(best, VoiceprintStore::cosineSimilarity(x.embedding, y.embedding));
    return std::clamp(best, 0.0, 1.0);
}

// ---- operations ---------------------------------------------------------------

PeopleOpResult PeopleService::addPerson(const QString& name)
{
    PeopleOpResult r;
    const QString n = name.trimmed();
    if (n.isEmpty()) {
        r.error = tr("Adj meg egy nevet.");
        return r;
    }
    reload();
    if (d->people) {
        const PersonId existing = d->people->idForName(n);
        if (!existing.isNull()) {
            r.error = tr("Ilyen nevű személy már van.");
            r.person = existing;
            r.name = d->nameOf(existing);
            return r;
        }
    }
    r.person = d->people ? d->people->create(n) : PersonId();
    if (r.person.isNull()) {
        r.error = tr("A személy nem menthető.");
        return r;
    }
    r.ok = true;
    r.name = n;
    d->finish(true, false);
    return r;
}

PeopleOpResult PeopleService::renamePerson(const PersonId& person, const QString& newName)
{
    PeopleOpResult r;
    reload();
    const QString n = newName.trimmed();
    r.person = person;
    if (!d->known(person)) {
        r.error = tr("Ez a személy már nem létezik.");
        return r;
    }
    const QString o = d->nameOf(person);
    r.name = o;
    if (n.isEmpty()) {
        r.error = tr("A név nem lehet üres.");
        return r;
    }
    if (o == n) {
        r.ok = true;
        return r;
    }
    if (!sameName(o, n) && d->nameTaken(n, person)) {
        r.error = tr("Ilyen nevű személy már van. Ha ugyanaz az ember, használd az Összevonást.");
        return r;
    }
    UndoStep step;
    step.kind = PeopleUndoKind::Renamed;
    step.person = person;
    step.oldName = o;
    step.newName = n;
    step.detail = n;
    for (const QString& a : d->people->aliases(person))
        if (sameName(a, n)) step.aliasWasPresent = true;

    // ONE field (the own person: the settings' "Your name" too).
    d->renameGlobally(person, n);
    // The old name stays as a nickname (also findable by the name in old transcripts).
    if (!sameName(o, n)) step.aliasAdded = d->people->addAlias(person, o);

    d->push(step);
    r.ok = true;
    r.name = n;
    emit changed();
    return r;
}

PeopleOpResult PeopleService::addAlias(const PersonId& person, const QString& alias)
{
    PeopleOpResult r;
    r.person = person;
    r.name = d->nameOf(person);
    const QString a = alias.trimmed();
    if (a.isEmpty()) {
        r.error = tr("Adj meg egy becenevet.");
        return r;
    }
    if (sameName(a, r.name)) {
        r.error = tr("Ez a személy neve — becenévnek mást adj meg.");
        return r;
    }
    if (!d->people || !d->people->addAlias(person, a)) {
        r.error = tr("Ez a becenév már szerepel.");
        return r;
    }
    r.ok = true;
    d->finish(true, false);
    return r;
}

bool PeopleService::removeAlias(const PersonId& person, const QString& alias)
{
    if (!d->people) return false;
    const int index = d->people->removeAlias(person, alias);
    if (index < 0) return false;
    UndoStep step;
    step.kind = PeopleUndoKind::AliasRemoved;
    step.person = person;
    step.alias = alias;
    step.detail = alias;
    step.aliasIndex = index;
    d->push(step);
    d->finish(true, false);
    return true;
}

void PeopleService::setNote(const PersonId& person, const QString& note)
{
    if (!d->people || person.isNull()) return;
    if (d->people->setNote(person, note)) emit changed();
}

bool PeopleService::setDefaultSide(const PersonId& person, const QString& side)
{
    if (!d->people || person.isNull()) return false;
    const QString clean = side == QLatin1String("local") || side == QLatin1String("remote") ? side : QString();
    if (!d->people->setDefaultSide(person, clean, /*manual*/ true)) return false;
    emit changed();
    return true;
}

int PeopleService::learnDefaultSides(const QHash<PersonId, QString>& learned)
{
    if (!d->people) return 0;
    int n = 0;
    for (auto it = learned.cbegin(); it != learned.cend(); ++it)
        n += d->people->setDefaultSide(it.key(), it.value()) ? 1 : 0;
    if (n > 0) emit changed();
    return n;
}

PeopleOpResult PeopleService::removeSample(const QString& printId)
{
    PeopleOpResult r;
    if (!d->voiceprints) return r;
    d->voiceprints->refresh();
    UndoStep step;
    step.kind = PeopleUndoKind::SampleRemoved;
    if (!d->voiceprints->findPrint(printId, &step.person, &step.print)) {
        r.error = tr("Ez a minta már nincs meg.");
        return r;
    }
    step.prints = d->voiceprints->siblingsOf(printId);
    bool removed = false;
    for (const Voiceprint& p : std::as_const(step.prints))
        removed = d->voiceprints->removePrint(p.id) || removed;
    if (!removed) {
        r.error = tr("Ez a minta már nincs meg.");
        return r;
    }
    if (d->store) step.detail = d->store->load(step.print.sourceMeetingId).title;
    d->push(step);
    r.ok = true;
    r.person = step.person;
    r.name = d->nameOf(step.person);
    r.samples = 1;
    d->finish(false, true);
    return r;
}

PeopleOpResult PeopleService::moveSample(const QString& printId, const PersonChoice& to)
{
    PeopleOpResult r;
    if (!d->voiceprints || !d->people) return r;
    reload();
    if (to.isAnonymous()) {
        r.error = tr("Adj meg egy nevet.");
        return r;
    }
    UndoStep step;
    step.kind = PeopleUndoKind::SampleMoved;
    if (!d->voiceprints->findPrint(printId, &step.person, &step.print)) {
        r.error = tr("Ez a minta már nincs meg.");
        return r;
    }
    if (!to.id.isNull() && to.id == step.person) {
        r.error = tr("A minta már ennél a személynél van.");
        return r;
    }
    step.createdPerson = to.id.isNull();
    step.other = d->people->resolve(to);
    if (step.other.isNull()) {
        r.error = tr("Adj meg egy nevet.");
        return r;
    }
    if (step.createdPerson) step.createdRecord = d->people->person(step.other);

    // The summary of the source meeting: there the speaker's identity became questionable.
    if (d->stats) d->stats->refreshNow();
    step.marks = d->markStale(step.person, step.print.sourceMeetingId);

    step.prints = d->voiceprints->siblingsOf(printId);   // the siblings go together
    for (const Voiceprint& p : std::as_const(step.prints)) {
        d->voiceprints->removePrint(p.id);
        d->voiceprints->addPrint(step.other, p);   // the id stays
    }
    if (d->store) step.detail = d->store->load(step.print.sourceMeetingId).title;
    d->push(step);

    r.ok = true;
    r.person = step.other;
    r.name = d->nameOf(step.other);
    r.samples = 1;
    r.staleSummaries = step.marks.size();
    d->announceStale(step.marks);
    d->finish(true, true);
    return r;
}

VoiceprintPlan PeopleService::voiceprintPlan(const PersonId& person) const
{
    VoiceprintPlan plan;
    plan.modelAvailable = d->material && d->material->canCreate();
    if (!d->stats || !d->store) return plan;
    const PersonStats ps = d->stats->stats(person);
    for (const PersonMeetingStat& pm : ps.meetings) {
        if (pm.talkMs <= 0) continue;
        ++plan.meetingsWithLines;
        // The existing editor logic tells whether there is a sure (not doubtful), long line.
        const qint64 best = d->material ? d->material->bestUsableMs(pm.meetingId, person) : 0;
        if (best <= 0) continue;
        plan.usableMeetingIds << pm.meetingId;
        plan.usableMs += best;
    }
    return plan;
}

VoiceprintResult PeopleService::createVoiceprintFromMeeting(const PersonId& person, const QString& meetingId)
{
    VoiceprintResult r;
    if (!d->material || !d->material->canCreate()) {
        r.error = tr("A hangmodell nincs telepítve, ezért nem készíthető hanglenyomat.");
        return r;
    }
    if (!d->store || !d->voiceprints) return r;
    // Its own, temporary editor session (in the port): we do not touch the state (undo stack) of an editor
    // possibly open in the main window; the voiceprint goes into the shared store.
    bool noMaterial = false;
    r = d->material->createVoiceprint(meetingId, person, &noMaterial);
    if (noMaterial) {
        r = VoiceprintResult();
        r.error = tr("Ezen a megbeszélésen nincs elég hosszú, biztos sora.");
        return r;
    }
    if (r.ok) d->finish(false, true);
    return r;
}

MergePreview PeopleService::mergePreview(const PersonId& loser, const PersonId& survivor) const
{
    MergePreview p;
    if (d->stats) {
        const PersonStats a = d->stats->stats(loser), b = d->stats->stats(survivor);
        QSet<QString> ids;
        for (const PersonMeetingStat& m : a.meetings) {
            ids.insert(m.meetingId);
            if (m.hasSummary) ++p.staleSummaries;
        }
        for (const PersonMeetingStat& m : b.meetings) ids.insert(m.meetingId);
        p.meetingCount = ids.size();
    }
    if (d->voiceprints)
        p.sampleCount = d->voiceprints->sampleCount(loser) + d->voiceprints->sampleCount(survivor);
    return p;
}

PeopleOpResult PeopleService::merge(const PersonId& loser, const PersonId& survivor)
{
    PeopleOpResult r;
    reload();
    if (!d->known(loser) || !d->known(survivor) || loser == survivor) {
        r.error = tr("Az összevonáshoz két különböző, létező személy kell.");
        return r;
    }
    const QString loserName = d->nameOf(loser);
    if (d->stats) d->stats->refreshNow();
    // In the meetings of the person that disappears the speaker's name changes: their summaries go stale.
    const QVector<StaleMark> marks = d->markStale(loser);

    // Every reference to the loser points to the survivor; the loser's record folds into the survivor's (its
    // name becomes a nickname). ONE undo step with the exact reference list.
    UndoStep step;
    step.kind = PeopleUndoKind::Merged;
    step.person = survivor;
    step.other = loser;
    step.detail = loserName;
    step.marks = marks;
    step.merge = mergeEverywhere(survivor, loser);
    if (!step.merge.isValid()) {
        r.error = tr("Az összevonáshoz két különböző, létező személy kell.");
        return r;
    }
    d->push(step);

    if (d->stats) d->stats->refreshNow();
    r.ok = true;
    r.person = survivor;
    r.name = d->nameOf(survivor);
    r.meetings = d->stats ? d->stats->stats(survivor).meetingCount : 0;
    r.samples = d->voiceprints ? d->voiceprints->sampleCount(survivor) : 0;
    r.staleSummaries = marks.size();
    d->announceStale(marks);
    emit changed();
    return r;
}

DeletePreview PeopleService::deletePreview(const PersonId& person) const
{
    DeletePreview p;
    p.sampleCount = d->voiceprints ? d->voiceprints->sampleCount(person) : 0;
    if (!d->stats) return p;
    const PersonStats ps = d->stats->stats(person);
    p.meetingCount = ps.meetingCount;
    for (const PersonMeetingStat& m : ps.meetings) {
        if (m.hasSummary) ++p.staleSummaries;
        if (!p.exampleLabel.isEmpty()) continue;
        // A raw label the speaker reverts to (from the newest meeting).
        for (const QString& key : m.speakerKeys)
            if (!d->refs || !d->refs->isParticipantKey(key)) {
                p.exampleLabel = key;
                break;
            }
    }
    return p;
}

PeopleOpResult PeopleService::removePerson(const PersonId& person, bool keepSamples)
{
    PeopleOpResult r;
    reload();
    if (!d->known(person)) {
        r.error = tr("Ez a személy már nem létezik.");
        return r;
    }
    if (isSelf(person)) {
        r.error = tr("A saját személyed nem törölhető.");
        return r;
    }
    if (d->stats) d->stats->refreshNow();
    r.meetings = d->stats ? d->stats->stats(person).meetingCount : 0;
    r.samples = d->voiceprints ? d->voiceprints->sampleCount(person) : 0;
    const QVector<StaleMark> marks = d->markStale(person);

    // "Keep voice samples as an anonymous person": the samples go BEFORE the deletion to a new person
    // named "Anonymous N" (so they do not vanish from the store for even a moment).
    if (keepSamples && r.samples > 0 && d->voiceprints && d->people) {
        r.name = d->freeAnonymousName();
        r.person = d->people->create(r.name);
        d->voiceprints->merge(person, r.person);
    }
    removeEverywhere(person);

    d->clearUndo();
    if (d->stats) d->stats->refreshNow();
    r.ok = true;
    r.staleSummaries = marks.size();
    d->announceStale(marks);
    d->finish(!r.person.isNull(), !r.person.isNull());
    return r;
}

// ---- undo ---------------------------------------------------------------

bool PeopleService::canUndo() const { return !d->undo.isEmpty(); }

PeopleUndoInfo PeopleService::undoInfo() const
{
    PeopleUndoInfo info;
    if (d->undo.isEmpty()) return info;
    const UndoStep& s = d->undo.last();
    info.kind = s.kind;
    info.person = s.person;
    info.detail = s.detail;
    return info;
}

PeopleUndoInfo PeopleService::undo()
{
    PeopleUndoInfo info;
    if (d->undo.isEmpty()) return info;
    const UndoStep s = d->undo.takeLast();
    info.kind = s.kind;
    info.person = s.person;
    info.detail = s.detail;
    reload();

    switch (s.kind) {
    case PeopleUndoKind::SampleRemoved:
        if (d->voiceprints)
            for (const Voiceprint& p : s.prints.isEmpty() ? QVector<Voiceprint>{s.print} : s.prints)
                d->voiceprints->addPrint(s.person, p);
        d->finish(false, true);
        break;
    case PeopleUndoKind::SampleMoved: {
        if (d->voiceprints) {
            for (const Voiceprint& p : s.prints.isEmpty() ? QVector<Voiceprint>{s.print} : s.prints) {
                d->voiceprints->removePrint(p.id);
                d->voiceprints->addPrint(s.person, p);
            }
        }
        // If the operation created the target person and nothing is bound to it since, it disappears.
        if (s.createdPerson && d->people && d->voiceprints && d->voiceprints->printCount(s.other) == 0) {
            const Person now = d->people->person(s.other);
            const bool untouched = now.isValid() && now.aliases.isEmpty() && now.note.isEmpty() && now.tags.isEmpty();
            if (untouched && (!d->stats || d->stats->stats(s.other).meetingCount == 0))
                d->people->remove(s.other);
        }
        QVector<StaleMark> undone;
        for (const StaleMark& mark : s.marks)
            if (d->unmarkStale(mark)) undone.append(mark);
        d->announceStale(undone);
        d->finish(true, true);
        break;
    }
    case PeopleUndoKind::AliasRemoved:
        if (d->people) d->people->insertAlias(s.person, s.alias, s.aliasIndex);
        d->finish(true, false);
        break;
    case PeopleUndoKind::Renamed: {
        if (!d->known(s.person)) break;
        // The nickname added at the rename (the old name) goes first: the name cannot be its own nickname.
        if (d->people && s.aliasAdded) d->people->removeAlias(s.person, s.oldName);
        d->renameGlobally(s.person, s.oldName);
        // If the new name was originally a nickname (the rename removed it), it comes back.
        if (d->people && s.aliasWasPresent) d->people->addAlias(s.person, s.newName);
        d->finish(true, false);
        break;
    }
    case PeopleUndoKind::Merged: {
        undoMergeEverywhere(s.merge);
        QVector<StaleMark> undone;
        for (const StaleMark& mark : s.marks)
            if (d->unmarkStale(mark)) undone.append(mark);
        d->announceStale(undone);
        if (d->stats) d->stats->refreshNow();
        info.person = s.other;   // the person who came back
        d->finish(true, true);
        break;
    }
    case PeopleUndoKind::None:
        break;
    }
    emit undoChanged();
    return info;
}

// ---- global person operations (formerly AppController::renamePerson / removePerson / mergePeople /
// undoMergePeople) ----------------------------------------------------------------------------

bool PeopleService::meetingRefersTo(const Meeting& m, const PersonId& person) const
{
    if (person.isNull()) return false;
    for (const PersonId& p : m.speakerMap) if (p == person) return true;
    for (const Participant& p : m.participants) if (p.personId == person) return true;
    if (m.approval)
        for (const ParticipantApprovalEntry& c : m.approval->candidates) if (c.personId == person) return true;
    if (m.folder.isEmpty() || !d->refs) return false;
    return d->refs->overlayRefersTo(m.folder, person);
}

void PeopleService::refreshMeetingNames(const Meeting& m)
{
    if (d->refs) d->refs->rewriteTranscriptNames(m, personNames());
    emit speakerMapChanged(m.id);
}

// A rename writes ONE field (people.json). transcript.md is a derived file with the names in it: the meetings
// showing the person regenerate it, and their speakerMapChanged makes open views reread.
void PeopleService::renameEverywhere(const PersonId& person, const QString& newName)
{
    const QString n = newName.trimmed();
    if (person.isNull() || n.isEmpty() || !d->people) return;
    if (!d->people->rename(person, n)) return;
    if (d->setSelfName && d->people->selfId() == person) {
        if (d->settingsName().trimmed() != n) d->writeSettingsName(n);
    }
    if (d->store)
        for (const Meeting& idx : d->store->loadAll()) {
            const Meeting m = d->store->load(idx.id);
            if (meetingRefersTo(m, person)) refreshMeetingNames(m);
        }
    emit peopleChanged();
}

void PeopleService::removeEverywhere(const PersonId& person)
{
    if (person.isNull()) return;
    if (d->people) d->people->remove(person);
    if (d->tags) d->tags->forgetPerson(person);
    if (d->voiceprints) { d->voiceprints->removePerson(person); emit voiceprintsChanged(); }

    const QVector<Meeting> all = d->store ? d->store->loadAll() : QVector<Meeting>();
    for (const Meeting& idx : all) {
        Meeting m = d->store->load(idx.id);
        bool changed = false;
        const QList<QString> keys = m.speakerMap.keys();
        for (const QString& key : keys)
            if (m.speakerMap.value(key) == person) { m.speakerMap.remove(key); changed = true; }
        for (Participant& p : m.participants)
            if (p.personId == person) { p.personId = PersonId(); changed = true; }
        if (m.approval)
            for (ParticipantApprovalEntry& c : m.approval->candidates)
                if (c.personId == person) { c.personId = PersonId(); changed = true; }
        const bool overlay = d->refs && d->refs->removePersonFromOverlay(m.folder, person);
        if (!changed && !overlay) continue;
        if (changed) d->store->saveMeeting(m);
        refreshMeetingNames(m);
    }
    emit peopleChanged();
}

PersonMergeUndo PeopleService::mergeEverywhere(const PersonId& keep, const PersonId& drop)
{
    PersonMergeUndo u;
    if (!d->people || keep.isNull() || drop.isNull() || keep == drop) return u;
    d->people->refresh();
    if (!d->people->contains(keep) || !d->people->contains(drop)) return u;
    u.keep = keep;
    u.drop = drop;
    u.keepBefore = d->people->person(keep);
    u.dropBefore = d->people->person(drop);
    u.selfWasDrop = d->people->selfId() == drop;
    u.selfNameBefore = d->selfName ? d->settingsName() : QString();

    // 1) The records: drop folds into keep (its name becomes a nickname; "self" moves to keep).
    d->people->merge(keep, drop, /*dropNameAsAlias*/ true);
    if (u.selfWasDrop && d->setSelfName) d->writeSettingsName(u.keepBefore.displayName);
    // 2) Voiceprints and tag rejections.
    if (d->voiceprints) u.movedPrintIds = d->voiceprints->merge(drop, keep);
    if (d->tags) u.rejections = d->tags->movePersonRefs(drop, keep);
    // 3) Every meeting's references.
    if (d->store)
        for (const Meeting& idx : d->store->loadAll()) {
            Meeting m = d->store->load(idx.id);
            PersonMergeMeeting mm;
            mm.meetingId = m.id;
            for (auto it = m.speakerMap.begin(); it != m.speakerMap.end(); ++it)
                if (it.value() == drop) { it.value() = keep; mm.speakerMapKeys << it.key(); }
            for (Participant& p : m.participants)
                if (p.personId == drop) { p.personId = keep; mm.participantIds << p.id; }
            if (m.approval)
                for (ParticipantApprovalEntry& c : m.approval->candidates)
                    if (c.personId == drop) { c.personId = keep; mm.approvalParticipantIds << c.participantId; }
            if (d->refs) mm.overlay = d->refs->replacePersonInOverlay(m.folder, drop, keep);
            if (mm.isEmpty()) continue;
            if (!mm.speakerMapKeys.isEmpty() || !mm.participantIds.isEmpty() || !mm.approvalParticipantIds.isEmpty())
                d->store->saveMeeting(m);
            u.meetings.append(mm);
            refreshMeetingNames(m);
            emit participantsChanged(m.id);
        }
    emit voiceprintsChanged();
    emit peopleChanged();
    return u;
}

bool PeopleService::undoMergeEverywhere(const PersonMergeUndo& u)
{
    if (!u.isValid() || !d->people) return false;
    // 1) The records as they were (the dropped person comes back with its id).
    d->people->restore(u.dropBefore);
    d->people->restore(u.keepBefore);
    if (u.selfWasDrop) {
        d->people->setSelfId(u.drop);
        if (d->setSelfName) d->writeSettingsName(u.selfNameBefore);
    }
    // 2) Voiceprints and tag rejections.
    if (d->voiceprints) {
        d->voiceprints->refresh();
        for (const QString& printId : u.movedPrintIds) {
            PersonId owner;
            Voiceprint vp;
            if (!d->voiceprints->findPrint(printId, &owner, &vp) || owner != u.keep) continue;
            d->voiceprints->removePrint(printId);
            d->voiceprints->addPrint(u.drop, vp);
        }
    }
    if (d->tags) d->tags->undoMovePersonRefs(u.keep, u.drop, u.rejections);
    // 3) The recorded meeting references that still point to keep.
    for (const PersonMergeMeeting& mm : u.meetings) {
        if (!d->store) break;
        Meeting m = d->store->load(mm.meetingId);
        if (m.id.isEmpty()) continue;
        bool changed = false;
        for (const QString& key : mm.speakerMapKeys)
            if (m.speakerMap.value(key) == u.keep) { m.speakerMap.insert(key, u.drop); changed = true; }
        for (Participant& p : m.participants)
            if (mm.participantIds.contains(p.id) && p.personId == u.keep) { p.personId = u.drop; changed = true; }
        if (m.approval)
            for (ParticipantApprovalEntry& c : m.approval->candidates)
                if (mm.approvalParticipantIds.contains(c.participantId) && c.personId == u.keep) {
                    c.personId = u.drop;
                    changed = true;
                }
        if (changed) d->store->saveMeeting(m);
        if (d->refs) d->refs->restorePersonInOverlay(m.folder, mm.overlay, u.keep, u.drop);
        refreshMeetingNames(m);
        emit participantsChanged(m.id);
    }
    emit voiceprintsChanged();
    emit peopleChanged();
    return true;
}

} // namespace tanara::library

#include "tanara/people/PeopleService.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/people/PeopleStats.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/PersonDetailsStore.h"
#include "tanara/store/VoiceprintStore.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace tanara {

namespace {

constexpr int kMaxUndoSteps = 20;

bool sameName(const QString& a, const QString& b)
{
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

// "fájl#startMs-endMs" → fájl + tartomány (a tartomány elhagyható).
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

// Egy elavult-jelölés nyoma (a visszavonáshoz): melyik megbeszélés mely kulcsai kerültek fel.
struct StaleMark {
    QString meetingId;
    QString folder;
    QStringList keys;
};

struct UndoStep {
    PeopleUndoKind kind = PeopleUndoKind::None;
    QString person;         // minta: a (korábbi) gazda; becenév / átnevezés: a személy (régi név)
    QString other;          // áthelyezés: a cél; átnevezés: az új név
    Voiceprint print;       // minta-lépéseknél
    QString detail;
    QString alias;
    int aliasIndex = -1;
    bool createdPerson = false;     // az áthelyezés hozta létre a cél-személyt
    bool aliasWasPresent = false;   // átnevezés: az új név már becenév volt
    QVector<StaleMark> marks;
};

} // namespace

struct PeopleService::Private {
    PeopleService* q = nullptr;
    AppController* controller = nullptr;
    MeetingStore* store = nullptr;
    PeopleStore* people = nullptr;
    VoiceprintStore* voiceprints = nullptr;
    PersonDetailsStore* details = nullptr;
    PeopleStats* stats = nullptr;
    UtteranceEmbedderFactory factory;
    bool factoryInjected = false;
    QVector<UndoStep> undo;

    QString self() const
    {
        return controller && controller->settings()
            ? controller->settings()->settings().userSpeakerName.trimmed() : QString();
    }

    QStringList names() const
    {
        QStringList out;
        auto add = [&](const QString& name) {
            const QString n = name.trimmed();
            if (n.isEmpty()) return;
            for (const QString& have : std::as_const(out))
                if (sameName(have, n)) return;
            out << n;
        };
        add(self());
        if (people) for (const QString& n : people->names()) add(n);
        if (voiceprints) for (const QString& n : voiceprints->people()) add(n);
        return out;
    }

    // A tárolt írásmód (kisbetű-független egyezésnél), különben üres.
    QString canonical(const QString& name) const
    {
        for (const QString& n : names())
            if (sameName(n, name)) return n;
        return QString();
    }

    PersonRecord record(const QString& name) const
    {
        PersonRecord r;
        r.name = name;
        r.isSelf = !self().isEmpty() && sameName(name, self());
        if (details) {
            const PersonDetails d = details->details(name);
            r.aliases = d.aliases;
            r.note = d.note;
        }
        r.sampleCount = voiceprints ? voiceprints->printCount(name) : 0;
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
    // Átnevezés után a verem korábbi lépései az új névre hivatkozzanak.
    void renameInUndo(const QString& from, const QString& to)
    {
        for (UndoStep& s : undo) {
            if (sameName(s.person, from)) s.person = to;
            if (s.kind == PeopleUndoKind::SampleMoved && sameName(s.other, from)) s.other = to;
        }
    }

    // A személy megbeszéléseinek összefoglalóit elavultnak jelöli a LEMEZEN (jelek nélkül);
    // only: csak ezen a megbeszélésen (üres = mindegyiken).
    QVector<StaleMark> markStale(const QString& person, const QString& only = QString())
    {
        QVector<StaleMark> marks;
        if (!stats || !store) return marks;
        const PersonStats ps = stats->stats(person);
        for (const PersonMeetingStat& pm : ps.meetings) {
            if (!only.isEmpty() && pm.meetingId != only) continue;
            if (!pm.hasSummary) continue;
            const Meeting m = store->load(pm.meetingId);
            if (m.id.isEmpty() || !m.hasSummary) continue;
            QStringList keys = pm.speakerKeys;
            if (keys.isEmpty()) keys << person;
            const QStringList added = speakeredit::markSummaryStaleKeys(m, keys);
            if (!added.isEmpty()) marks.append(StaleMark{m.id, m.folder, added});
        }
        return marks;
    }
    // A jelölés (vagy visszavonása) után: a nyitott szerkesztő olvassa újra a lemezt, a
    // könyvtár / összefoglaló-fül frissítse az elavult-jelzőt.
    void announceStale(const QVector<StaleMark>& marks)
    {
        if (!controller) return;
        for (const StaleMark& mark : marks) {
            emit controller->speakerMapChanged(mark.meetingId);
            emit controller->summaryStaleChanged(mark.meetingId);
        }
    }

    UtteranceEmbedderFactory embedderFactory() const
    {
        if (factoryInjected) return factory;
        if (!controller || !controller->voiceIdentificationAvailable()) return {};
        return voiceUtteranceEmbedderFactory(controller->voiceModelPath());
    }

    // A személy beszélő-kulcsai egy megnyitott szerkesztőben.
    static QStringList speakerKeysOf(const SpeakerEditor& ed, const QString& person)
    {
        QStringList keys;
        for (const EditorSpeaker& s : ed.speakers())
            if (!s.anonymous && sameName(s.personName, person)) keys << s.key;
        return keys;
    }

    QString freeAnonymousName() const
    {
        const QStringList have = names();
        for (int i = 1;; ++i) {
            const QString candidate = PeopleService::tr("Névtelen %1").arg(i);
            bool taken = false;
            for (const QString& n : have)
                if (sameName(n, candidate)) { taken = true; break; }
            if (!taken) return candidate;
        }
    }

    void finish(bool people_, bool prints)
    {
        if (controller) {
            if (prints) emit controller->voiceprintsChanged();
            if (people_) emit controller->peopleChanged();
        }
        emit q->changed();
    }
};

PeopleService::PeopleService(AppController* controller, MeetingStore* store, PeopleStore* people,
                             VoiceprintStore* voiceprints, PersonDetailsStore* details,
                             PeopleStats* stats, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->q = this;
    d->controller = controller;
    d->store = store;
    d->people = people;
    d->voiceprints = voiceprints;
    d->details = details;
    d->stats = stats;
}

PeopleService::~PeopleService() = default;

void PeopleService::setEmbedderFactory(UtteranceEmbedderFactory factory)
{
    d->factory = std::move(factory);
    d->factoryInjected = true;
}

void PeopleService::reload()
{
    if (d->people) d->people->refresh();
    if (d->voiceprints) d->voiceprints->refresh();
    if (d->details) d->details->refresh();
}

QString PeopleService::selfName() const { return d->self(); }

bool PeopleService::isSelf(const QString& name) const
{
    const QString self = d->self();
    return !self.isEmpty() && sameName(self, name);
}

QVector<PersonRecord> PeopleService::persons() const
{
    QVector<PersonRecord> out;
    const QStringList names = d->names();
    out.reserve(names.size());
    for (const QString& n : names) out.append(d->record(n));
    return out;
}

PersonRecord PeopleService::person(const QString& name) const
{
    const QString canonical = d->canonical(name);
    return d->record(canonical.isEmpty() ? name.trimmed() : canonical);
}

bool PeopleService::exists(const QString& name) const { return !d->canonical(name).isEmpty(); }

QVector<VoiceSample> PeopleService::samples(const QString& name) const
{
    QVector<VoiceSample> out;
    if (!d->voiceprints) return out;
    QHash<QString, Meeting> meetings;   // egy megbeszélést egyszer töltünk
    for (const Voiceprint& vp : d->voiceprints->printsFor(name)) {
        VoiceSample s;
        s.id = vp.id;
        s.person = name;
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
            // A forrás-sáv: az azonosítója, annak híján a fájlneve alapján.
            const QVector<TrackView> views = TrackCatalog::tracks(m);
            const TrackView* view = nullptr;
            for (const TrackView& v : views)
                if (!vp.sourceTrack.isEmpty() && v.track.id == vp.sourceTrack) { view = &v; break; }
            if (!view && !file.isEmpty())
                for (const TrackView& v : views)
                    if (v.track.file == file) { view = &v; break; }
            if (view) {
                switch (view->role) {
                case TrackRole::OwnMic:
                    s.sourceKind = QStringLiteral("mic");
                    // A saját mikrofonnál az eszköz neve a hasznos (több mikrofon más hang).
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

double PeopleService::similarity(const QString& a, const QString& b) const
{
    if (!d->voiceprints) return -1.0;
    const QVector<Voiceprint> pa = d->voiceprints->printsFor(a), pb = d->voiceprints->printsFor(b);
    if (pa.isEmpty() || pb.isEmpty()) return -1.0;
    // Ugyanaz a szabály, mint a felismerésnél: a legjobban egyező pár számít.
    double best = 0.0;
    for (const Voiceprint& x : pa)
        for (const Voiceprint& y : pb)
            best = std::max(best, VoiceprintStore::cosineSimilarity(x.embedding, y.embedding));
    return std::clamp(best, 0.0, 1.0);
}

// ---- műveletek ---------------------------------------------------------------

PeopleOpResult PeopleService::addPerson(const QString& name)
{
    PeopleOpResult r;
    const QString n = name.trimmed();
    if (n.isEmpty()) {
        r.error = tr("Adj meg egy nevet.");
        return r;
    }
    reload();
    const QString existing = d->canonical(n);
    if (!existing.isEmpty()) {
        r.error = tr("Ilyen nevű személy már van.");
        r.name = existing;
        return r;
    }
    if (d->people) d->people->add(n);
    r.ok = true;
    r.name = n;
    d->finish(true, false);
    return r;
}

PeopleOpResult PeopleService::renamePerson(const QString& oldName, const QString& newName)
{
    PeopleOpResult r;
    reload();
    const QString o = d->canonical(oldName), n = newName.trimmed();
    r.name = o;
    if (o.isEmpty()) {
        r.error = tr("Ez a személy már nem létezik.");
        return r;
    }
    if (n.isEmpty()) {
        r.error = tr("A név nem lehet üres.");
        return r;
    }
    if (o == n) {
        r.ok = true;
        return r;
    }
    if (!sameName(o, n) && !d->canonical(n).isEmpty()) {
        r.error = tr("Ilyen nevű személy már van. Ha ugyanaz az ember, használd az Összevonást.");
        return r;
    }
    UndoStep step;
    step.kind = PeopleUndoKind::Renamed;
    step.person = o;
    step.other = n;
    step.detail = n;
    if (d->details)
        for (const QString& a : d->details->aliases(o))
            if (sameName(a, n)) step.aliasWasPresent = true;

    // A meglévő globális átnevezés: névlista, lenyomatok, minden megbeszélés. A saját névnél
    // ugyanaz történik, mint a Beállítások „Saját neved” mezőjénél.
    if (isSelf(o)) d->controller->setUserSpeakerName(n);
    else d->controller->renamePerson(o, n);
    // A régi név becenévként megmarad (a régi átiratokban szereplő néven is megtalálható).
    if (d->details && !sameName(o, n)) d->details->addAlias(n, o);

    d->renameInUndo(o, n);
    step.person = o;
    d->push(step);
    r.ok = true;
    r.name = n;
    emit changed();
    return r;
}

PeopleOpResult PeopleService::addAlias(const QString& name, const QString& alias)
{
    PeopleOpResult r;
    r.name = name;
    const QString a = alias.trimmed();
    if (a.isEmpty()) {
        r.error = tr("Adj meg egy becenevet.");
        return r;
    }
    if (sameName(a, name)) {
        r.error = tr("Ez a személy neve — becenévnek mást adj meg.");
        return r;
    }
    if (!d->details || !d->details->addAlias(name, a)) {
        r.error = tr("Ez a becenév már szerepel.");
        return r;
    }
    r.ok = true;
    d->finish(true, false);
    return r;
}

bool PeopleService::removeAlias(const QString& name, const QString& alias)
{
    if (!d->details) return false;
    const int index = d->details->removeAlias(name, alias);
    if (index < 0) return false;
    UndoStep step;
    step.kind = PeopleUndoKind::AliasRemoved;
    step.person = name;
    step.alias = alias;
    step.detail = alias;
    step.aliasIndex = index;
    d->push(step);
    d->finish(true, false);
    return true;
}

void PeopleService::setNote(const QString& name, const QString& note)
{
    if (!d->details || name.trimmed().isEmpty()) return;
    if (d->details->details(name).note == note) return;
    d->details->setNote(name, note);
    emit changed();
}

PeopleOpResult PeopleService::removeSample(const QString& printId)
{
    PeopleOpResult r;
    if (!d->voiceprints) return r;
    d->voiceprints->refresh();
    UndoStep step;
    step.kind = PeopleUndoKind::SampleRemoved;
    if (!d->voiceprints->findPrint(printId, &step.person, &step.print)
        || !d->voiceprints->removePrint(printId)) {
        r.error = tr("Ez a minta már nincs meg.");
        return r;
    }
    if (d->store) step.detail = d->store->load(step.print.sourceMeetingId).title;
    d->push(step);
    r.ok = true;
    r.name = step.person;
    r.samples = 1;
    d->finish(false, true);
    return r;
}

PeopleOpResult PeopleService::moveSample(const QString& printId, const QString& toName)
{
    PeopleOpResult r;
    if (!d->voiceprints) return r;
    reload();
    const QString target = toName.trimmed();
    if (target.isEmpty()) {
        r.error = tr("Adj meg egy nevet.");
        return r;
    }
    UndoStep step;
    step.kind = PeopleUndoKind::SampleMoved;
    if (!d->voiceprints->findPrint(printId, &step.person, &step.print)) {
        r.error = tr("Ez a minta már nincs meg.");
        return r;
    }
    const QString existing = d->canonical(target);
    if (sameName(existing, step.person)) {
        r.error = tr("A minta már ennél a személynél van.");
        return r;
    }
    step.other = existing.isEmpty() ? target : existing;
    step.createdPerson = existing.isEmpty();

    // A forrás-megbeszélés összefoglalója: ott a beszélő kiléte kérdésessé vált.
    if (d->stats) d->stats->refreshNow();
    step.marks = d->markStale(step.person, step.print.sourceMeetingId);

    d->voiceprints->removePrint(printId);
    d->voiceprints->addPrint(step.other, step.print);   // az azonosító megmarad
    if (d->people) d->people->add(step.other);
    if (d->store) step.detail = d->store->load(step.print.sourceMeetingId).title;
    d->push(step);

    r.ok = true;
    r.name = step.other;
    r.samples = 1;
    r.staleSummaries = step.marks.size();
    d->announceStale(step.marks);
    d->finish(true, true);
    return r;
}

VoiceprintPlan PeopleService::voiceprintPlan(const QString& name) const
{
    VoiceprintPlan plan;
    const UtteranceEmbedderFactory factory = d->embedderFactory();
    plan.modelAvailable = bool(factory);
    if (!d->stats || !d->store) return plan;
    const PersonStats ps = d->stats->stats(name);
    for (const PersonMeetingStat& pm : ps.meetings) {
        if (pm.talkMs <= 0) continue;
        ++plan.meetingsWithLines;
        // A meglévő szerkesztő-logika mondja meg, van-e elég biztos (nem kétes), hosszú sor.
        SpeakerEditor ed(d->store, d->people, d->voiceprints, pm.meetingId);
        qint64 best = 0;
        for (const QString& key : Private::speakerKeysOf(ed, name)) {
            const VoiceprintMaterial material = ed.voiceprintMaterial(key);
            if (material.sufficient) best = std::max(best, material.usableMs);
        }
        if (best <= 0) continue;
        plan.usableMeetingIds << pm.meetingId;
        plan.usableMs += best;
    }
    return plan;
}

VoiceprintResult PeopleService::createVoiceprintFromMeeting(const QString& name, const QString& meetingId)
{
    VoiceprintResult r;
    const UtteranceEmbedderFactory factory = d->embedderFactory();
    if (!factory) {
        r.error = tr("A hangmodell nincs telepítve, ezért nem készíthető hanglenyomat.");
        return r;
    }
    if (!d->store || !d->voiceprints) return r;
    // Saját, ideiglenes szerkesztő-munkamenet: a főablakban esetleg nyitott szerkesztő
    // állapotához (undo-verem) nem nyúlunk; a lenyomat a közös tárolóba kerül.
    SpeakerEditor ed(d->store, d->people, d->voiceprints, meetingId);
    ed.setEmbedderFactory(factory);
    QString bestKey;
    qint64 bestMs = 0;
    for (const QString& key : Private::speakerKeysOf(ed, name)) {
        const VoiceprintMaterial material = ed.voiceprintMaterial(key);
        if (material.sufficient && material.usableMs > bestMs) {
            bestMs = material.usableMs;
            bestKey = key;
        }
    }
    if (bestKey.isEmpty()) {
        r.error = tr("Ezen a megbeszélésen nincs elég hosszú, biztos sora.");
        return r;
    }
    r = ed.createVoiceprint(bestKey);
    if (r.ok) d->finish(false, true);
    return r;
}

MergePreview PeopleService::mergePreview(const QString& loser, const QString& survivor) const
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
        p.sampleCount = d->voiceprints->printCount(loser) + d->voiceprints->printCount(survivor);
    return p;
}

PeopleOpResult PeopleService::merge(const QString& loserName, const QString& survivorName)
{
    PeopleOpResult r;
    reload();
    const QString loser = d->canonical(loserName), survivor = d->canonical(survivorName);
    if (loser.isEmpty() || survivor.isEmpty() || sameName(loser, survivor)) {
        r.error = tr("Az összevonáshoz két különböző, létező személy kell.");
        return r;
    }
    if (d->stats) d->stats->refreshNow();
    // A megszűnő név megbeszélésein változik a beszélő neve: azok összefoglalója elavul.
    const QVector<StaleMark> marks = d->markStale(loser);

    // A meglévő globális átnevezés létező cél-névre = egyesítés: a névlistában, a
    // lenyomatoknál, minden megbeszélésben; a megszűnő név becenév lesz (people-details.json).
    if (isSelf(loser)) d->controller->setUserSpeakerName(survivor);
    else d->controller->renamePerson(loser, survivor);
    if (d->details) d->details->addAlias(survivor, loser);

    d->clearUndo();   // nem visszavonható, és a korábbi lépések nevei már nem érvényesek
    if (d->stats) d->stats->refreshNow();
    r.ok = true;
    r.name = survivor;
    r.meetings = d->stats ? d->stats->stats(survivor).meetingCount : 0;
    r.samples = d->voiceprints ? d->voiceprints->printCount(survivor) : 0;
    r.staleSummaries = marks.size();
    d->announceStale(marks);
    emit changed();
    return r;
}

DeletePreview PeopleService::deletePreview(const QString& name) const
{
    DeletePreview p;
    p.sampleCount = d->voiceprints ? d->voiceprints->printCount(name) : 0;
    if (!d->stats) return p;
    const PersonStats ps = d->stats->stats(name);
    p.meetingCount = ps.meetingCount;
    for (const PersonMeetingStat& m : ps.meetings) {
        if (m.hasSummary) ++p.staleSummaries;
        if (!p.exampleLabel.isEmpty()) continue;
        // Egy nyers címke, amivé a beszélő visszaváltozik (a legújabb megbeszélésről).
        for (const QString& key : m.speakerKeys)
            if (!speakeredit::isParticipantKey(key) && !sameName(key, name)) {
                p.exampleLabel = key;
                break;
            }
    }
    return p;
}

PeopleOpResult PeopleService::removePerson(const QString& nameIn, bool keepSamples)
{
    PeopleOpResult r;
    reload();
    const QString name = d->canonical(nameIn);
    if (name.isEmpty()) {
        r.error = tr("Ez a személy már nem létezik.");
        return r;
    }
    if (isSelf(name)) {
        r.error = tr("A saját személyed nem törölhető.");
        return r;
    }
    if (d->stats) d->stats->refreshNow();
    r.meetings = d->stats ? d->stats->stats(name).meetingCount : 0;
    r.samples = d->voiceprints ? d->voiceprints->printCount(name) : 0;
    const QVector<StaleMark> marks = d->markStale(name);

    // „Hangminták megtartása névtelen személyként”: a minták a törlés ELŐTT egy új,
    // „Névtelen N” nevű személyhez kerülnek (így egy pillanatra sem tűnnek el a tárolóból).
    if (keepSamples && r.samples > 0 && d->voiceprints) {
        r.name = d->freeAnonymousName();
        d->voiceprints->renamePerson(name, r.name);
        if (d->people) d->people->add(r.name);
    }
    d->controller->removePerson(name);

    d->clearUndo();
    if (d->stats) d->stats->refreshNow();
    r.ok = true;
    r.staleSummaries = marks.size();
    d->announceStale(marks);
    d->finish(!r.name.isEmpty(), !r.name.isEmpty());
    return r;
}

// ---- visszavonás ---------------------------------------------------------------

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
        if (d->voiceprints) d->voiceprints->addPrint(s.person, s.print);
        d->finish(false, true);
        break;
    case PeopleUndoKind::SampleMoved: {
        if (d->voiceprints) {
            d->voiceprints->removePrint(s.print.id);
            d->voiceprints->addPrint(s.person, s.print);
        }
        // Ha a művelet hozta létre a cél-személyt, és azóta semmi nem kötődik hozzá, eltűnik.
        if (s.createdPerson && d->people && d->voiceprints && d->voiceprints->printCount(s.other) == 0
            && (!d->details || d->details->details(s.other).isEmpty())
            && (!d->stats || d->stats->stats(s.other).meetingCount == 0))
            d->people->remove(s.other);
        QVector<StaleMark> undone;
        for (const StaleMark& mark : s.marks)
            if (speakeredit::unmarkSummaryStale(mark.folder, mark.keys)) undone.append(mark);
        d->announceStale(undone);
        d->finish(true, true);
        break;
    }
    case PeopleUndoKind::AliasRemoved:
        if (d->details) d->details->insertAlias(s.person, s.alias, s.aliasIndex);
        d->finish(true, false);
        break;
    case PeopleUndoKind::Renamed: {
        // Vissza a régi névre ugyanazon a globális úton; az átnevezéskor felvett becenév
        // (a régi név) a visszanevezéssel magától megszűnik (a név nem lehet saját beceneve).
        const QString current = d->canonical(s.other);
        if (current.isEmpty()) break;
        if (isSelf(current)) d->controller->setUserSpeakerName(s.person);
        else d->controller->renamePerson(current, s.person);
        // Ha az új név eredetileg becenév volt (az átnevezés levette), visszakerül.
        if (d->details && s.aliasWasPresent) d->details->addAlias(s.person, s.other);
        d->renameInUndo(s.other, s.person);
        d->finish(true, false);
        break;
    }
    case PeopleUndoKind::None:
        break;
    }
    emit undoChanged();
    return info;
}

} // namespace tanara

#include "tanara/people/PeopleStats.h"

#include "tanara/Logging.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/MeetingStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>

namespace tanara {

namespace {

QString fold(const QString& name) { return name.trimmed().toCaseFolded(); }

} // namespace

const MeetingPerson* MeetingPeople::find(const QString& name) const
{
    const QString key = fold(name);
    for (const MeetingPerson& p : people)
        if (fold(p.name) == key) return &p;
    return nullptr;
}

MeetingPeople scanMeetingPeople(const Meeting& m)
{
    MeetingPeople out;
    out.meetingId = m.id;
    out.title = m.title;
    out.folder = m.folder;
    out.startedAt = m.startedAt;
    out.hasSummary = m.hasSummary;

    QHash<QString, int> index;
    auto ensure = [&](const QString& name) -> MeetingPerson* {
        const QString n = name.trimmed();
        if (n.isEmpty()) return nullptr;
        const QString key = n.toCaseFolded();
        auto it = index.constFind(key);
        if (it == index.constEnd()) {
            MeetingPerson p;
            p.name = n;
            out.people.append(p);
            it = index.insert(key, out.people.size() - 1);
        }
        return &out.people[it.value()];
    };
    auto addKey = [](MeetingPerson* p, const QString& key) {
        if (p && !key.isEmpty() && !p->speakerKeys.contains(key)) p->speakerKeys.append(key);
    };

    const QVector<TranscriptLine> lines = speakeredit::loadTranscriptLines(m.folder);
    SpeakerOverlay ov;
    if (QFile::exists(speakeredit::overlayPath(m.folder)))
        ov = lines.isEmpty() ? speakeredit::loadOverlay(m.folder)
                             : speakeredit::loadOverlayFor(m.folder, lines);

    // Jelenlét: ugyanaz a szabály, mint a személyválasztó számlálójánál (listPeople):
    // speakerMap-érték, fix beszélőjű sáv neve, kézzel felvett (nevesített) résztvevő.
    for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
        addKey(ensure(it.value()), it.key());
    QHash<QString, QString> fixedLabels;   // kisbetűsített sáv-címke → címke
    for (const Track& t : m.tracks) {
        if (!t.fixedSpeaker || t.speakerLabel.trimmed().isEmpty()) continue;
        fixedLabels.insert(fold(t.speakerLabel), t.speakerLabel.trimmed());
        addKey(ensure(t.speakerLabel), t.speakerLabel.trimmed());
    }
    for (const OverlayParticipant& p : ov.participants)
        addKey(ensure(p.person), p.key);

    // Beszédidő: a sorok FELOLDOTT beszélője szerint (felülírás → összevonás → speakerMap).
    for (const TranscriptLine& l : lines) {
        const QString key = speakeredit::resolveSpeakerKey(ov, l);
        QString person = speakeredit::speakerPerson(ov, m.speakerMap, key);
        if (person.isEmpty()) {
            // Régi, sávonkénti átirat: a nyers címke maga a (fix) beszélő neve.
            const auto fixed = fixedLabels.constFind(fold(key));
            if (fixed != fixedLabels.constEnd()) person = fixed.value();
            else if (m.speakerMap.value(key) == key) person = key;
        }
        if (person.isEmpty()) continue;
        if (MeetingPerson* p = ensure(person)) p->talkMs += std::max<qint64>(0, l.endMs - l.startMs);
    }
    return out;
}

PeopleStats::PeopleStats(MeetingStore* store, QObject* parent)
    : QObject(parent), m_store(store), m_alive(std::make_shared<std::atomic_bool>(true))
{
}

PeopleStats::~PeopleStats() { m_alive->store(false); }

PersonStats PeopleStats::stats(const QString& name) const
{
    return m_byPerson.value(fold(name));
}

QVector<MeetingPeople> PeopleStats::meetings() const
{
    QVector<MeetingPeople> out;
    out.reserve(m_cache.size());
    for (const MeetingPeople& m : m_cache) out.append(m);
    return out;
}

PeopleStats::Stamp PeopleStats::stampOf(const QString& folder)
{
    Stamp s;
    s.meeting = FileStamp::of(QDir(folder).filePath(QStringLiteral("meeting.json")));
    s.segments = FileStamp::of(speakeredit::segmentsPath(folder));
    s.overlay = FileStamp::of(speakeredit::overlayPath(folder));
    return s;
}

QVector<PeopleStats::Job> PeopleStats::jobs() const
{
    QVector<Job> out;
    if (!m_store) return out;
    const QVector<Meeting> index = m_store->loadAll();
    out.reserve(index.size());
    for (const Meeting& m : index)
        if (!m.id.isEmpty() && !m.folder.isEmpty()) out.append(Job{m.id, m.folder});
    return out;
}

PeopleStats::Result PeopleStats::compute(const QVector<Job>& jobs, const QHash<QString, Stamp>& known)
{
    QElapsedTimer timer;
    timer.start();
    Result r;
    for (const Job& job : jobs) {
        r.ids << job.id;
        const Stamp stamp = stampOf(job.folder);
        const auto it = known.constFind(job.id);
        if (it != known.constEnd() && it.value() == stamp) continue;   // nem változott
        Scanned s;
        s.id = job.id;
        s.stamp = stamp;
        QFile f(QDir(job.folder).filePath(QStringLiteral("meeting.json")));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) {
                Meeting m = meetingFromJson(doc.object());
                m.folder = job.folder;   // az index mappája az érvényes (áthelyezett könyvtár)
                if (m.id.isEmpty()) m.id = job.id;
                s.people = scanMeetingPeople(m);
                s.people.meetingId = job.id;
                s.valid = true;
            }
        }
        r.changed.append(s);
    }
    r.elapsedMs = timer.elapsed();
    return r;
}

void PeopleStats::scheduleRefresh()
{
    if (m_scheduled) return;
    m_scheduled = true;
    QTimer::singleShot(250, this, [this] {
        m_scheduled = false;
        refresh();
    });
}

void PeopleStats::refresh()
{
    if (m_running) {
        m_again = true;
        return;
    }
    if (!QCoreApplication::instance()) {
        refreshNow();
        return;
    }
    m_running = true;
    emit busyChanged();
    const QVector<Job> list = jobs();
    const QHash<QString, Stamp> known = m_stamps;
    const int generation = m_generation;
    const auto alive = m_alive;
    // A szál csak a saját másolataival dolgozik; az eredményt a fő szál veszi át.
    QThreadPool::globalInstance()->start([this, list, known, generation, alive] {
        Result r = compute(list, known);
        r.generation = generation;
        if (QCoreApplication* app = QCoreApplication::instance())
            QMetaObject::invokeMethod(app, [this, r, alive] {
                if (!alive->load()) return;
                m_running = false;
                // Közben szinkron frissítés futott: ez az eredmény régebbi állapotot tükrözhet.
                if (r.generation == m_generation) apply(r);
                else m_again = true;
                emit busyChanged();
                if (m_again) {
                    m_again = false;
                    refresh();
                }
            }, Qt::QueuedConnection);
    });
}

void PeopleStats::refreshNow()
{
    ++m_generation;
    apply(compute(jobs(), m_stamps));
}

void PeopleStats::apply(const Result& result)
{
    bool any = !m_ready;
    // A megszűnt megbeszélések ki a gyorsítótárból.
    const QSet<QString> ids(result.ids.cbegin(), result.ids.cend());
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (ids.contains(it.key())) { ++it; continue; }
        m_stamps.remove(it.key());
        it = m_cache.erase(it);
        any = true;
    }
    for (const Scanned& s : result.changed) {
        m_stamps.insert(s.id, s.stamp);
        if (s.valid) m_cache.insert(s.id, s.people);
        else m_cache.remove(s.id);
        any = true;
    }
    m_lastMs = result.elapsedMs;
    m_lastRescanned = result.changed.size();
    m_ready = true;
    if (!any) return;
    rebuild();
    qCInfo(lcStore).noquote() << "személy-statisztika:" << result.ids.size() << "megbeszélés,"
                              << result.changed.size() << "újraolvasva," << result.elapsedMs << "ms";
    emit changed();
}

void PeopleStats::rebuild()
{
    m_byPerson.clear();
    for (const MeetingPeople& m : std::as_const(m_cache)) {
        for (const MeetingPerson& p : m.people) {
            PersonStats& s = m_byPerson[fold(p.name)];
            ++s.meetingCount;
            s.talkMs += p.talkMs;
            if (!s.lastSeen.isValid() || m.startedAt > s.lastSeen) s.lastSeen = m.startedAt;
            s.meetings.append(PersonMeetingStat{m.meetingId, m.title, m.startedAt, p.talkMs,
                                                m.hasSummary, p.speakerKeys});
        }
    }
    for (PersonStats& s : m_byPerson)
        std::sort(s.meetings.begin(), s.meetings.end(),
                  [](const PersonMeetingStat& a, const PersonMeetingStat& b) {
                      return a.startedAt > b.startedAt;
                  });
}

} // namespace tanara

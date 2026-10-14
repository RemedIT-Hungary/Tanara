#include "tanara/library/people/PeopleStats.h"
#include "tanara/library/MeetingLayout.h"

#include "tanara/foundation/log/Logging.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/store/MeetingRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>

namespace tanara::library {

const MeetingPerson* MeetingPeople::find(const PersonId& person) const
{
    for (const MeetingPerson& p : people)
        if (p.personId == person) return &p;
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

    QHash<PersonId, int> index;
    auto ensure = [&](const PersonId& person) -> MeetingPerson* {
        if (person.isNull()) return nullptr;
        auto it = index.constFind(person);
        if (it == index.constEnd()) {
            MeetingPerson p;
            p.personId = person;
            out.people.append(p);
            it = index.insert(person, out.people.size() - 1);
        }
        return &out.people[it.value()];
    };
    auto addKey = [](MeetingPerson* p, const QString& key) {
        if (p && !key.isEmpty() && !p->speakerKeys.contains(key)) p->speakerKeys.append(key);
    };

    const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(m.folder);
    SpeakerOverlay ov;
    if (QFile::exists(editing::speakeredit::overlayPath(m.folder)))
        ov = lines.isEmpty() ? editing::speakeredit::loadOverlay(m.folder)
                             : editing::speakeredit::loadOverlayFor(m.folder, lines);

    // Presence: the same rule as the person picker's counter (listPeople): a speakerMap person, a manually
    // added (named) participant.
    for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
        addKey(ensure(it.value()), it.key());
    for (const OverlayParticipant& p : ov.participants)
        addKey(ensure(p.personId), p.key);

    // Speaking time: by the lines' RESOLVED speaker (override -> merge -> speakerMap).
    for (const TranscriptLine& l : lines) {
        const QString key = editing::speakeredit::resolveSpeakerKey(ov, l);
        const PersonId person = editing::speakeredit::speakerPerson(ov, m.speakerMap, key);
        if (MeetingPerson* p = ensure(person)) p->talkMs += std::max<qint64>(0, l.endMs - l.startMs);
    }
    return out;
}

PeopleStats::PeopleStats(MeetingRepository* store, QObject* parent)
    : QObject(parent), m_store(store), m_alive(std::make_shared<std::atomic_bool>(true))
{
}

PeopleStats::~PeopleStats() { m_alive->store(false); }

PersonStats PeopleStats::stats(const PersonId& person) const
{
    return m_byPerson.value(person);
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
    s.meeting = FileStamp::of(MeetingLayout::path(folder, MeetingLayout::meetingJson()));
    s.segments = FileStamp::of(editing::speakeredit::segmentsPath(folder));
    s.overlay = FileStamp::of(editing::speakeredit::overlayPath(folder));
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
        if (it != known.constEnd() && it.value() == stamp) continue;   // unchanged
        Scanned s;
        s.id = job.id;
        s.stamp = stamp;
        // The index's folder is the valid one (a moved library); the reader fills it in.
        Meeting m = MeetingRepository::readMeetingFolder(job.folder);
        if (!m.folder.isEmpty()) {
            if (m.id.isEmpty()) m.id = job.id;
            s.people = scanMeetingPeople(m);
            s.people.meetingId = job.id;
            s.valid = true;
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
    // The thread works only with its own copies; the main thread takes over the result.
    QThreadPool::globalInstance()->start([this, list, known, generation, alive] {
        Result r = compute(list, known);
        r.generation = generation;
        if (QCoreApplication* app = QCoreApplication::instance())
            QMetaObject::invokeMethod(app, [this, r, alive] {
                if (!alive->load()) return;
                m_running = false;
                // A synchronous refresh ran meanwhile: this result may reflect an older state.
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
    tanara::PerfScope perfScope("PeopleStats::apply", 20);
    bool any = !m_ready;
    // Vanished meetings out of the cache.
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
            PersonStats& s = m_byPerson[p.personId];
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

} // namespace tanara::library
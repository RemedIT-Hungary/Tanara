#pragma once
//
// PeopleStats: per-person statistics from the meetings: in how many meetings they appear,
// how long they spoke in total, when we last saw them, and how long they spoke per meeting.
//
// The computation reads one meeting.json + transcript.segments.json + transcript.speakers.json
// per meeting (the resolved speaker = per the editor's rules: per-line override -> merge ->
// speakerMap). With dozens of meetings this would be noticeable on the main thread, so it runs on a
// BACKGROUND THREAD and is cached per meeting: the three files' mtime + size is the key, so a
// refresh rereads only the changed meetings. The cache lasts for the process life (not on disk).
#include "tanara/domain/Meeting.h"
#include "tanara/foundation/SharedFile.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>

namespace tanara::library {

class MeetingRepository;

// One person at one meeting.
struct MeetingPerson {
    QString     name;
    qint64      talkMs = 0;
    QStringList speakerKeys;    // which of the meeting's speaker keys (raw label / participant:N) are theirs
};

// A meeting's participants (one cache item).
struct MeetingPeople {
    QString   meetingId;
    QString   title;
    QString   folder;
    QDateTime startedAt;
    bool      hasSummary = false;
    QVector<MeetingPerson> people;

    const MeetingPerson* find(const QString& name) const;
};

struct PersonMeetingStat {
    QString   meetingId;
    QString   title;
    QDateTime startedAt;
    qint64    talkMs = 0;
    bool      hasSummary = false;
    QStringList speakerKeys;
};

struct PersonStats {
    int       meetingCount = 0;
    qint64    talkMs = 0;
    QDateTime lastSeen;                     // invalid if they appear in no meeting
    QVector<PersonMeetingStat> meetings;    // newest first
};

// A meeting's participants and their speaking time from disk. Pure file reading: callable on any thread.
MeetingPeople scanMeetingPeople(const Meeting& meeting);

class PeopleStats : public QObject {
    Q_OBJECT
public:
    explicit PeopleStats(MeetingRepository* store, QObject* parent = nullptr);
    ~PeopleStats() override;

    // Whether a full computation has happened yet (before that every statistic is empty).
    bool ready() const { return m_ready; }
    bool busy() const { return m_running; }
    PersonStats stats(const QString& name) const;
    // The meetings' participants (the cache content), unordered.
    QVector<MeetingPeople> meetings() const;
    // Time of the last refresh in ms and the number of meetings reread (diagnostics).
    qint64 lastRefreshMs() const { return m_lastMs; }
    int lastRescanned() const { return m_lastRescanned; }

public slots:
    // Refresh in the background; at the end changed() (if anything changed, or this is the first).
    void refresh();
    // The same with a short delay, coalesced (one computation for many consecutive signals).
    void scheduleRefresh();
    // Refresh NOW, on the caller's thread (before operations so the consequence numbers are exact;
    // tests). Rereads only the changed meetings.
    void refreshNow();

signals:
    void changed();
    void busyChanged();

private:
    struct Stamp {
        FileStamp meeting, segments, overlay;
        bool operator==(const Stamp& o) const
        { return meeting == o.meeting && segments == o.segments && overlay == o.overlay; }
    };
    struct Job { QString id; QString folder; };
    struct Scanned { QString id; Stamp stamp; MeetingPeople people; bool valid = false; };
    struct Result {
        QStringList ids;
        QVector<Scanned> changed;
        qint64 elapsedMs = 0;
        int generation = 0;
    };
    static Stamp stampOf(const QString& folder);
    static Result compute(const QVector<Job>& jobs, const QHash<QString, Stamp>& known);
    QVector<Job> jobs() const;
    void apply(const Result& result);
    void rebuild();

    MeetingRepository* m_store = nullptr;
    QHash<QString, Stamp> m_stamps;
    QHash<QString, MeetingPeople> m_cache;          // meetingId -> participants
    QHash<QString, PersonStats> m_byPerson;         // lowercased name -> statistics
    std::shared_ptr<std::atomic_bool> m_alive;
    bool m_ready = false;
    bool m_running = false;
    bool m_again = false;
    bool m_scheduled = false;
    int m_generation = 0;
    qint64 m_lastMs = 0;
    int m_lastRescanned = 0;
};

} // namespace tanara::library
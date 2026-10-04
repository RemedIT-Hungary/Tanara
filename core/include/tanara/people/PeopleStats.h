#pragma once
//
// PeopleStats — személyenkénti statisztika a megbeszélésekből: hány megbeszélésen szerepel,
// mennyit beszélt összesen, mikor láttuk utoljára, és megbeszélésenként mennyit beszélt.
//
// A számolás megbeszélésenként egy meeting.json + transcript.segments.json +
// transcript.speakers.json olvasás (a feloldott beszélő = a szerkesztő szabályai szerint:
// soronkénti felülírás → összevonás → speakerMap). Több tucat megbeszélésnél ez a fő szálon
// érezhető lenne, ezért HÁTTÉRSZÁLON fut, és megbeszélésenként gyorsítótárazott: a három
// fájl módosítási ideje + mérete a kulcs, így egy frissítés csak a megváltozott
// megbeszéléseket olvassa újra. A gyorsítótár a folyamat életére szól (nincs lemezen).
//
#include "tanara/Types.h"
#include "tanara/store/SharedFile.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>

namespace tanara {

class MeetingStore;

// Egy személy egy megbeszélésen.
struct MeetingPerson {
    QString     name;
    qint64      talkMs = 0;
    QStringList speakerKeys;    // a megbeszélés mely beszélő-kulcsai (nyers címke / participant:N) az övéi
};

// Egy megbeszélés résztvevői (a gyorsítótár egy eleme).
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
    QDateTime lastSeen;                     // érvénytelen, ha egy megbeszélésen sem szerepel
    QVector<PersonMeetingStat> meetings;    // legújabb elöl
};

// Egy megbeszélés résztvevői és beszédidejük a lemezről. Tiszta fájl-olvasás: bármely szálon
// hívható.
MeetingPeople scanMeetingPeople(const Meeting& meeting);

class PeopleStats : public QObject {
    Q_OBJECT
public:
    explicit PeopleStats(MeetingStore* store, QObject* parent = nullptr);
    ~PeopleStats() override;

    // Volt-e már teljes számolás (előtte minden statisztika üres).
    bool ready() const { return m_ready; }
    bool busy() const { return m_running; }
    PersonStats stats(const QString& name) const;
    // A megbeszélések résztvevői (a gyorsítótár tartalma), sorrend nélkül.
    QVector<MeetingPeople> meetings() const;
    // Az utolsó frissítés ideje ms-ben és az újraolvasott megbeszélések száma (diagnosztika).
    qint64 lastRefreshMs() const { return m_lastMs; }
    int lastRescanned() const { return m_lastRescanned; }

public slots:
    // Frissítés a háttérben; a végén changed() (ha bármi változott, vagy ez az első).
    void refresh();
    // Ugyanez rövid késleltetéssel, összevonva (sok egymás utáni jelre egy számolás).
    void scheduleRefresh();
    // Frissítés MOST, a hívó szálán (műveletek előtt, hogy a következmény-számok pontosak
    // legyenek; tesztek). Csak a megváltozott megbeszéléseket olvassa újra.
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

    MeetingStore* m_store = nullptr;
    QHash<QString, Stamp> m_stamps;
    QHash<QString, MeetingPeople> m_cache;          // meetingId → résztvevők
    QHash<QString, PersonStats> m_byPerson;         // kisbetűsített név → statisztika
    std::shared_ptr<std::atomic_bool> m_alive;
    bool m_ready = false;
    bool m_running = false;
    bool m_again = false;
    bool m_scheduled = false;
    int m_generation = 0;
    qint64 m_lastMs = 0;
    int m_lastRescanned = 0;
};

} // namespace tanara

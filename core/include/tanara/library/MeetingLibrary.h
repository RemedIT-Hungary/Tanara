#pragma once
//
// MeetingLibrary — a főablak bal oldali könyvtárának lekérdező rétege (UI-független).
//
//  - Lista: meetingek legújabb elöl, dátum-szekció kulccsal (ma / tegnap / ezen a héten / korábban).
//  - Keresés: teljes szöveges, a címben ÉS az átirat szövegében, ékezet- és kisbetű-függetlenül
//    („odon” megtalálja „Ödön”-t); találatonként kivonat + a találat helye (kiemeléshez).
//  - Szűrők: nincs átirat / nincs összefoglaló / X személy részt vett; találatszám.
//  - „Ezek várnak rád” (M02): átírásra váró és bukott átírású meetingek.
//
// Teljesítmény: a meeting-metaadat és az átirat-szöveg lustán, meetingenként gyorsítótárazott;
// a store / AppController jelzéseire meetingenként érvénytelenít. Pár száz meetingnél az első
// keresés tölti be az átiratokat (warmUp() ezt előre, szeletekben, az eseményhurokban megteszi),
// utána egy keresés memóriában fut.
//
#include "tanara/Types.h"
#include "tanara/jobs/JobTypes.h"
#include "tanara/library/TextFold.h"

#include <QObject>
#include <QHash>
#include <QVector>
#include <QDate>

namespace tanara {

class MeetingStore;
class MeetingJobTracker;

enum class DateSection { Today, Yesterday, ThisWeek, Earlier };

struct LibraryEntry {
    QString   id;
    QString   title;
    QDateTime startedAt;
    qint64    durationMs = 0;
    bool      hasTranscript = false;
    bool      hasSummary = false;
    DateSection section = DateSection::Earlier;
    QStringList participants;          // nevesített résztvevők (speakerMap + saját mikrofon-sáv)
    MeetingProcessingState state;      // ikon-állapotok (ha a könyvtár kapott trackert)

    // --- keresési találat (üres keresésnél mind érvénytelen/üres) ---
    textfold::Range titleMatch;        // a találat helye a title-ben
    QString   snippet;                 // kivonat az átiratból a találat körül
    textfold::Range snippetMatch;      // a találat helye a snippet-ben
    qint64    snippetMs = -1;          // a találatot tartalmazó megszólalás kezdete (ugráshoz)
    QString   snippetSpeaker;          // a megszólaló (megjelenített név)
};

struct LibraryQuery {
    QString     text;                  // keresőkifejezés (üres → nincs szöveges szűrés)
    bool        noTranscript = false;  // csak az átirat nélküliek
    bool        noSummary = false;     // csak az összefoglaló nélküliek
    QStringList people;                // mindegyik megadott személy részt vett

    bool isEmpty() const { return text.trimmed().isEmpty() && !noTranscript && !noSummary && people.isEmpty(); }
};

struct LibraryResult {
    QVector<LibraryEntry> entries;     // legújabb elöl
    int totalMeetings = 0;             // a könyvtár teljes mérete (szűrés nélkül)
    int count() const { return int(entries.size()); }   // találatszám
};

struct PersonPresence {
    QString name;
    int meetingCount = 0;
};

// Az M02 („Válassz egy megbeszélést / Ezek várnak rád”) kártya elemei.
enum class PendingKind {
    AwaitingTranscription,   // van felvétel, még nincs átirat („Megnyitás”)
    TranscriptionFailed,     // az átírás elbukott („Megnézem”) — error kitöltve
    StaleSummary,            // elavult összefoglaló („Frissítés”) — correctedSpeakers kitöltve
};

struct PendingItem {
    PendingKind kind = PendingKind::AwaitingTranscription;
    QString   meetingId;
    QString   title;
    QDateTime startedAt;
    qint64    durationMs = 0;
    JobError  error;         // TranscriptionFailed esetén
    int       correctedSpeakers = 0;   // StaleSummary: hány beszélőt javítottak az összefoglaló óta
};

class MeetingLibrary : public QObject {
    Q_OBJECT
public:
    // tracker lehet nullptr (ekkor az állapot csak a hasTranscript/hasSummary flagekből áll).
    explicit MeetingLibrary(MeetingStore* store, MeetingJobTracker* tracker = nullptr,
                            QObject* parent = nullptr);
    ~MeetingLibrary() override;

    // A lista / keresés. `now` a szekció-számítás „mai napja” (teszthez átadható).
    LibraryResult query(const LibraryQuery& q = {},
                        const QDateTime& now = QDateTime::currentDateTime()) const;

    // Egy meeting bejegyzése keresés nélkül (inkrementális modell-frissítéshez); üres id, ha nincs.
    LibraryEntry entry(const QString& meetingId,
                       const QDateTime& now = QDateTime::currentDateTime()) const;

    // A teljes (meeting.json-ból töltött) meeting a gyorsítótárból.
    Meeting meeting(const QString& meetingId) const;
    int meetingCount() const;

    // A könyvtárban szereplő nevesített személyek, gyakoriság szerint (szűrő-chipekhez).
    QVector<PersonPresence> people() const;

    // „Ezek várnak rád”: legújabb elöl. limit <= 0 → mind.
    QVector<PendingItem> pendingItems(int limit = 0) const;

    // Dátum-szekció: ma / tegnap / ezen a héten (a hét hétfővel indul) / korábban.
    static DateSection sectionFor(const QDateTime& startedAt, const QDate& today);
    static QString sectionKey(DateSection s);     // "today" | "yesterday" | "thisWeek" | "earlier"
    static QString sectionTitle(DateSection s);   // „Ma” | „Tegnap” | „Ezen a héten” | „Korábban”

    // Betöltötte-e már minden átirat szövegét a kereséshez.
    bool isWarm() const;

public slots:
    // Az átirat-szövegek előtöltése szeletekben az eseményhurokban (nem blokkol). warmedUp jel.
    void warmUp();
    // Egy meeting (vagy üres id → minden) gyorsítótárának eldobása; a következő lekérdezés újratölt.
    void invalidate(const QString& meetingId = QString());

signals:
    void meetingAdded(QString meetingId);
    void meetingChanged(QString meetingId);     // cím, állapot, átirat, résztvevők … változott
    void meetingRemoved(QString meetingId);
    void pendingItemsChanged();
    void reset();                               // teljes újratöltés kell (invalidate() mind)
    void warmedUp();

private:
    struct TextDoc;
    struct Impl;
    void ensureLoaded() const;
    void reloadMeeting(const QString& id);
    const TextDoc* textDoc(const Meeting& m) const;
    LibraryEntry makeEntry(const Meeting& m, const QDate& today) const;
    static QStringList participantsOf(const Meeting& m);

    Impl* d;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::DateSection)
Q_DECLARE_METATYPE(tanara::LibraryEntry)
Q_DECLARE_METATYPE(tanara::LibraryQuery)
Q_DECLARE_METATYPE(tanara::LibraryResult)
Q_DECLARE_METATYPE(tanara::PersonPresence)
Q_DECLARE_METATYPE(tanara::PendingKind)
Q_DECLARE_METATYPE(tanara::PendingItem)

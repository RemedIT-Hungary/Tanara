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
#include "tanara/library/MeetingNotes.h"
#include "tanara/library/TextFold.h"
#include "tanara/tags/TagTypes.h"

#include <QObject>
#include <QHash>
#include <QVector>
#include <QDate>

namespace tanara {

class MeetingStore;
class MeetingJobTracker;
class TagService;

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

    // --- címkék (setTagService után) ---
    QStringList tagIds;                // a meeting sorrendjében, csak a létező címkék
    QStringList tagNames;              // ugyanabban a sorrendben
    bool      tagMatch = false;        // a keresés egy címke nevében talált
    QString   tagMatchName;            // az első találó címke neve
};

struct LibraryQuery {
    QString     text;                  // keresőkifejezés (üres → nincs szöveges szűrés)
    bool        noTranscript = false;  // csak az átirat nélküliek
    bool        noSummary = false;     // csak az összefoglaló nélküliek
    QStringList people;                // mindegyik megadott személy részt vett
    QStringList tags;                  // címke-azonosítók: bármelyik (tagsAll → mindegyik) rajta van
    bool        tagsAll = false;       // true → ÉS, false → VAGY
    bool        untagged = false;      // címke nélküliek (a tags-szel VAGY kapcsolatban)

    bool isEmpty() const {
        return text.trimmed().isEmpty() && !noTranscript && !noSummary && people.isEmpty()
            && tags.isEmpty() && !untagged;
    }
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

    // Sablon-javaslatok a meeting megjegyzéséhez: a hasonló című korábbi megbeszélések nem
    // üres, a jelenlegitől eltérő megjegyzései, legújabb elöl (lásd MeetingNotes.h).
    QVector<meetingnotes::NoteSuggestion> noteSuggestions(const QString& meetingId,
                                                          const QString& currentNote,
                                                          int limit = 3) const;

    // A könyvtárban szereplő nevesített személyek, gyakoriság szerint (szűrő-chipekhez).
    QVector<PersonPresence> people() const;

    // Címkék: a bejegyzések címke-nevei, a címke-szűrő és a keresés címkékben ehhez kell.
    void setTagService(TagService* tags);
    // A szűrő-popover címkéi: minden címke a könyvtárbeli darabszámmal (gyakoriság, majd ABC).
    QVector<TagUsage> tagOptions() const;
    // Hány megbeszélésen nincs címke („Címke nélkül” szám).
    int untaggedCount() const;

    // „Ezek várnak rád”: legújabb elöl. limit <= 0 → mind.
    QVector<PendingItem> pendingItems(int limit = 0) const;

    // Dátum-szekció: ma / tegnap / ezen a héten (a hét hétfővel indul) / korábban.
    static DateSection sectionFor(const QDateTime& startedAt, const QDate& today);
    static QString sectionKey(DateSection s);     // "today" | "yesterday" | "thisWeek" | "earlier"
    static QString sectionTitle(DateSection s);   // „Ma” | „Tegnap” | „Ezen a héten” | „Korábban”

    // Betöltötte-e már minden átirat szövegét a kereséshez.
    bool isWarm() const;

    // A meeting nevesített résztvevői: a saját (mikrofon-) sáv fix beszélője + a speakerMap
    // nevei, egyszer-egyszer. A generikus sáv-címkék („Mikrofon 2”) nem személyek.
    static QStringList participantsOf(const Meeting& m);

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
    // A címkekészlet változott (név, törlés, összevonás): a sorok címke-nevei és a szűrő-
    // popover frissítendő. (Egy meeting címkéinek változása meetingChanged-ként jön.)
    void tagsChanged();
    void warmedUp();

private:
    struct TextDoc;
    struct Impl;
    void ensureLoaded() const;
    void reloadMeeting(const QString& id);
    const TextDoc* textDoc(const Meeting& m) const;
    LibraryEntry makeEntry(const Meeting& m, const QDate& today) const;

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

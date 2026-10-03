#pragma once
//
// Tanara — háttér-feladatok (átírás, összefoglaló, téma-elemzés, lekeverés, azonosítás)
// STRUKTURÁLT állapot-típusai. Sima value-típusok (QObject nincs), queued signalban
// átküldhetők (Q_DECLARE_METATYPE lent). A könyvtár-lista állapot-ikonjai, a feladat-sáv
// (task strip) és a fülek tartalma (M03–M09) ezekből rajzol.
//
// A régi, szabad szöveges AppController::jobProgress / errorOccurred jelek MEGMARADNAK
// (Widgets-UI, CLI) — ez a réteg mellettük él.
//
#include <QString>
#include <QVector>
#include <QDateTime>
#include <QMetaType>

namespace tanara {

struct HttpExchange;   // tanara/cloud/CloudTypes.h
struct CloudError;

// Egy feldolgozási lépés állapota egy meetingen (a könyvtár-ikonokhoz).
//  - az azonosításnál (identifyState) a Failed nem fordul elő.
enum class StepState { None, Running, Failed, Done };

// A megszakítható háttér-feladat fajtája.
enum class JobKind {
    Transcribe,      // átírás (benne: lekeverés az átíráshoz → feltöltés → átírás → azonosítás)
    Summarize,       // gyors összefoglaló, ill. a témánkénti elemzés záró összegzése (reduce)
    ExtractTopics,   // témák javaslása (komplex 1. kör)
    AnalyzeTopics,   // téma-elemzés sor (komplex 2. kör) — meetingenként egy feladat
    Mixdown,         // önálló lekeverés (nem az átírás része)
    Identify,        // résztvevők azonosítása hang alapján (átirat után)
};

enum class StageState { Waiting, Running, Done, Failed, Skipped };

// Egy bukott lépés megmaradó leírása. A meeting mappájában perzisztálódik
// (processing.json), így másik meeting kiválasztása ÉS újraindítás után is látszik.
struct JobError {
    JobKind   kind = JobKind::Transcribe;
    QString   message;        // emberi magyarázat (pl. „A szolgáltató nem fogadta el az API-kulcsot.”)
    QString   detail;         // technikai sor (pl. „HTTP 401 · invalid_api_key · …”)
    QString   fixActionHint;  // gépi tipp a javító gombhoz (pl. "settings:stt"); lehet üres
    QDateTime when;
    bool isValid() const { return !message.isEmpty(); }
};

// Egy futó feladat egy szakasza (M04 szakasz-lista).
struct JobStage {
    QString    id;            // stabil gépi azonosító: mixdown | upload | transcribe | diarize | identify | …
    QString    label;         // megjelenítendő név (lefordítva)
    StageState state = StageState::Waiting;
    int        percent = -1;  // 0..100, CSAK ha valós mérésből jön; -1 = nincs (határozatlan)
    QString    detail;        // rövid kiegészítés (pl. „52 perc, 3 sáv”, „3 / 5 beszélő”)
};

// Egy futó feladat pillanatképe.
struct JobProgress {
    QString   meetingId;
    JobKind   kind = JobKind::Transcribe;
    QString   title;          // pl. „Átírás folyamatban”
    QString   message;        // az utolsó szabad szöveges állapot (= jobProgress üzenet)
    QVector<JobStage> stages; // sorrendben; üres, ha a feladat egylépéses
    int       percent = -1;   // a TELJES feladat százaléka, csak ha valós (pl. lekeverés)
    int       done = -1;      // darab-haladás (pl. 3 / 5 beszélő, 2 / 7 téma); -1 = nincs
    int       total = -1;
    bool      cancellable = true;
    bool      cancelling = false;       // megszakítás kérve, a lezárásra várunk
    QDateTime startedAt;
    // Becsült teljes futásidő mp-ben; -1 = nem becsülhető. Csak korábbi, UGYANAZZAL a
    // szolgáltatóval mért futásokból számolunk (lásd JobStats) — nincs kitalált érték.
    int       estimatedTotalSec = -1;

    bool isValid() const { return !meetingId.isEmpty(); }
    // Hátralévő idő mp-ben (estimatedTotalSec − eltelt), min. 0; -1, ha nincs becslés.
    int etaSeconds(const QDateTime& now = QDateTime::currentDateTime()) const {
        if (estimatedTotalSec < 0 || !startedAt.isValid()) return -1;
        const qint64 left = estimatedTotalSec - startedAt.secsTo(now);
        return left < 0 ? 0 : int(left);
    }
    const JobStage* stage(const QString& id) const {
        for (const JobStage& s : stages) if (s.id == id) return &s;
        return nullptr;
    }
};

// Egy meeting teljes feldolgozási állapota — ebből rajzol a könyvtár-elem (3 ikon),
// a feladat-sáv és a fülek.
struct MeetingProcessingState {
    QString   meetingId;
    StepState transcriptState = StepState::None;
    StepState summaryState    = StepState::None;
    StepState identifyState   = StepState::None;   // None | Running | Done
    bool      mixdownRunning  = false;             // önálló VAGY az átírás részeként futó keverés
    int       mixdownPercent  = -1;                // 0..100, ha fut
    // Az utolsó hiba lépésenként. Failed állapotnál mindig érvényes; ha egy ÚJRA-futtatás
    // bukott el úgy, hogy a korábbi eredmény megvan, az állapot Done marad, de a hiba itt
    // látszik (a UI figyelmeztethet), amíg egy sikeres futás vagy clearError() nem törli.
    JobError  transcriptError;
    JobError  summaryError;
    // Az összefoglaló ELAVULT: elkészülte óta beszélő-hozzárendelés változott (a könyvtár
    // „elavult” ikon-állapota, az Összefoglaló fül pillje, M07 sáv). Csak summaryState==Done
    // mellett lehet igaz. A számláló: hány beszélőt javítottak azóta.
    bool      summaryStale = false;
    int       staleCorrectedSpeakers = 0;
    QVector<JobProgress> jobs;   // a meeting épp futó feladatai (feladat-sáv)

    bool busy() const { return !jobs.isEmpty(); }
};

// Egy téma állapota a témánkénti elemzésben (M08 kártya-pill).
enum class TopicState {
    Waiting,   // nincs elemzése, és nincs is sorban („vár”)
    Queued,    // sorban áll
    Running,   // épp fut
    Done,      // van kész (perzisztált) elemzése
    Failed,    // az utolsó kísérlet elbukott (üzenettel)
};

struct TopicStatus {
    QString    topicId;
    TopicState state = TopicState::Waiting;
    QString    error;         // Failed: emberi üzenet
    QString    errorDetail;   // Failed: technikai sor
};

// A feladat lezárásának módja (jobFinished jel).
enum class JobOutcome { Done, Failed, Cancelled };

} // namespace tanara

Q_DECLARE_METATYPE(tanara::StepState)
Q_DECLARE_METATYPE(tanara::JobKind)
Q_DECLARE_METATYPE(tanara::StageState)
Q_DECLARE_METATYPE(tanara::JobError)
Q_DECLARE_METATYPE(tanara::JobStage)
Q_DECLARE_METATYPE(tanara::JobProgress)
Q_DECLARE_METATYPE(tanara::MeetingProcessingState)
Q_DECLARE_METATYPE(tanara::TopicState)
Q_DECLARE_METATYPE(tanara::TopicStatus)
Q_DECLARE_METATYPE(QVector<tanara::TopicStatus>)
Q_DECLARE_METATYPE(tanara::JobOutcome)

#pragma once
//
// MeetingJobTracker — a meetingenkénti feldolgozási állapot EGYETLEN igazságforrása.
//
//  - Futó feladatok (memóriában): fajta, szakasz-lista, valós százalék / darab-haladás,
//    megszakíthatóság. Az AppController tölti a vezénylő API-n (begin/setStage/finish/…).
//  - Megmaradó hibák (lemezen): a meeting mappájában lévő processing.json őrzi az utolsó
//    bukott lépés emberi + technikai leírását, a témánkénti hibákat és az azonosítás
//    elvégzésének idejét → másik meeting kiválasztása és újraindítás után is lekérdezhető.
//  - Levezetett állapot: state() a Meeting flagjeiből (hasTranscript/hasSummary/speakerMap)
//    + a fentiekből adja a könyvtár-ikonok és a fülek állapotát.
//
// UI-független (QtCore). A jelekre a lista-modellek inkrementálisan frissíthetnek.
//
#include "tanara/Types.h"
#include "tanara/jobs/JobTypes.h"

#include <QObject>
#include <QHash>
#include <functional>

namespace tanara {

class MeetingStore;

class MeetingJobTracker : public QObject {
    Q_OBJECT
public:
    explicit MeetingJobTracker(MeetingStore* store, QObject* parent = nullptr);
    ~MeetingJobTracker() override;

    // ---- lekérdezés (bármikor, olcsó) -------------------------------------------------
    // A meeting teljes állapota. Az id-s változat a store-ból tölti a meetinget; ha a hívó
    // kezében már ott a (teljes, meeting.json-ból töltött) Meeting, a másik gyorsabb.
    MeetingProcessingState state(const QString& meetingId) const;
    MeetingProcessingState state(const Meeting& meeting) const;

    JobProgress job(const QString& meetingId, JobKind kind) const;   // !isValid(), ha nem fut
    bool isRunning(const QString& meetingId, JobKind kind) const;
    bool isBusy(const QString& meetingId) const;                     // fut rajta bármi
    QVector<JobProgress> activeJobs() const;                         // minden meeting, indulási sorrendben

    // Az utolsó megmaradt hiba. Summarize / ExtractTopics / AnalyzeTopics közös „összefoglaló”
    // hibahelyet használ; Identify-nak nincs. Érvénytelen (isValid()==false), ha nincs hiba.
    JobError lastError(const QString& meetingId, JobKind kind) const;
    // Témánkénti megmaradt hibák (topicId → hiba).
    QHash<QString, JobError> topicErrors(const QString& meetingId) const;
    // Lefutott-e (megszakítás nélkül) hang-alapú azonosítás a meetingen; mikor.
    QDateTime identifiedAt(const QString& meetingId) const;

    // Az összefoglaló elavultságának forrása (a beszélő-szerkesztő réteg adja; az
    // AppController köti be). A szonda a javított beszélők számát adja, vagy -1-et, ha az
    // összefoglaló nem elavult. Szonda nélkül a summaryStale mindig hamis.
    using StaleProbe = std::function<int(const Meeting&)>;
    void setSummaryStaleProbe(StaleProbe probe) { m_staleProbe = std::move(probe); }

    // A meeting-mappában lévő állapotfájl neve.
    static QString stateFileName() { return QStringLiteral("processing.json"); }

public slots:
    // A megmaradt hiba elvetése (pl. „Rendben” gomb). stateChanged + errorChanged jel.
    void clearError(const QString& meetingId, tanara::JobKind kind);
    // A trackeren KÍVÜL változott valami, ami a levezetett állapotot érinti (pl. az
    // összefoglaló elavult-jelzője) → stateChanged jel a modelleknek.
    void notifyStateChanged(const QString& meetingId) { emit stateChanged(meetingId); }

public:
    // ---- vezénylő API (az AppController hívja) ----------------------------------------
    // Új feladat indul. Ha ugyanilyen már fut a meetingen, felülírja (újraindítás).
    void begin(const QString& meetingId, JobKind kind, const QString& title,
               const QVector<JobStage>& stages = {}, bool cancellable = true);
    void setStage(const QString& meetingId, JobKind kind, const QString& stageId,
                  StageState state, int percent = -1, const QString& detail = QString());
    // A teljes szakasz-lista cseréje futás közben (pl. „Modell betöltése” szakasz beszúrása,
    // vagy a részek számának változása után).
    void setStages(const QString& meetingId, JobKind kind, const QVector<JobStage>& stages);
    // Csak a szakasz százaléka / részlete (az állapota marad).
    void setStagePercent(const QString& meetingId, JobKind kind, const QString& stageId, int percent);
    void setStageDetail(const QString& meetingId, JobKind kind, const QString& stageId,
                        const QString& detail);
    void setMessage(const QString& meetingId, JobKind kind, const QString& message);
    void setPercent(const QString& meetingId, JobKind kind, int percent);
    void setCounts(const QString& meetingId, JobKind kind, int done, int total);
    void setEstimate(const QString& meetingId, JobKind kind, int estimatedTotalSec);
    void setCancelling(const QString& meetingId, JobKind kind);

    void finish(const QString& meetingId, JobKind kind);                 // siker: a hibahely törlődik
    void fail(const QString& meetingId, JobKind kind, const JobError& error);   // hiba: megmarad
    void cancelled(const QString& meetingId, JobKind kind);              // megszakítva: nincs hiba

    // Feladat nélküli hiba rögzítése (pl. az indítás előfeltétele bukott).
    void recordError(const QString& meetingId, const JobError& error);

    void setTopicError(const QString& meetingId, const QString& topicId, const JobError& error);
    void clearTopicError(const QString& meetingId, const QString& topicId);
    void markIdentified(const QString& meetingId, bool identified = true);

signals:
    // Bármi változott a meeting állapotában (ikonok, hibák, futó feladatok).
    void stateChanged(QString meetingId);
    void jobStarted(QString meetingId, tanara::JobKind kind);
    // Egy futó feladat haladása változott (szakasz, százalék, darab, üzenet).
    void jobProgressChanged(QString meetingId, tanara::JobProgress progress);
    void jobFinished(QString meetingId, tanara::JobKind kind, tanara::JobOutcome outcome);
    // Megmaradó hiba keletkezett / törlődött.
    void errorChanged(QString meetingId, tanara::JobKind kind);

private:
    struct Persisted;
    Persisted& persisted(const QString& meetingId, const QString& folderHint = QString()) const;
    void savePersisted(const QString& meetingId) const;
    JobProgress* find(const QString& meetingId, JobKind kind);
    const JobProgress* find(const QString& meetingId, JobKind kind) const;
    void closeJob(const QString& meetingId, JobKind kind, JobOutcome outcome);
    QString folderFor(const QString& meetingId) const;

    MeetingStore* m_store = nullptr;
    StaleProbe    m_staleProbe;
    QVector<JobProgress> m_jobs;                       // indulási sorrendben
    mutable QHash<QString, Persisted*> m_persisted;    // meetingId → lemez-állapot (lusta)
};

} // namespace tanara

#pragma once
//
// Tanara QML — a „nézetek” szelet (átirat előtti nézet, Összefoglaló, Sávok) nézetmodelljeinek
// közös segédei: a controller feloldása (injektált vagy az App-singletoné), idő-formázás,
// a core gépi tippjeinek (fixActionHint) fordítása gomb-feliratra + Beállítások-oldalra, és
// a szolgáltató megnevezése. Állapot nélküli függvények.
//
#include "tanara/jobs/JobTypes.h"
#include "tanara/provider/ReadinessModel.h"

#include <QString>
#include <QVariantMap>

class QObject;

namespace tanara {
class AppController;
}

namespace tanara_qml::jobsupport {

// Az injektált controller (teszt), különben az App-singletoné. Lehet nullptr.
tanara::AppController* resolveController(QObject* injected);

// Igaz, ha a nézetmodellnek beépített (kitalált) mintaadatot kell mutatnia: App.demo, vagy
// nincs controller.
bool demoMode(tanara::AppController* controller);

// „30:34”, egy óra fölött „1:16:04”. Negatív → „–”.
QString formatDuration(qint64 ms);

// „kb. 4 perc van hátra” / „kevesebb mint 1 perc van hátra”; -1 → üres (nincs becslés).
QString formatEta(int seconds);

// A szakasz-állapot gépi neve a QML-nek: waiting | running | done | failed | skipped.
QString stageStateName(tanara::StageState state);

// Egy javító művelet: a gomb felirata és a shell.openSettings() oldala.
struct FixAction {
    QString label;      // üres → nincs javító gomb
    QString page;       // "", "providers", "cloud", "summary", "watcher"
    bool isValid() const { return !label.isEmpty(); }
};

// Bukott feladat fixActionHint-je ("settings:stt" | "settings:llm" | "cloud" | …) → gomb.
FixAction fixActionForError(const tanara::JobError& error);

// Kapuzás (canRun) → a figyelmeztető sáv tartalma: { title, text, actionLabel, actionPage,
// reason } — a reason a letiltott indító gomb melletti rövid indok. step: Transcribe / Summarize.
QVariantMap blockerInfo(tanara::AppController* controller, tanara::WorkflowStep step,
                        const tanara::ReadinessResult& result);

// „Soniox · saját kulcs” / „Tanara Cloud” — az adott lépés kiválasztott szolgáltatója.
QString providerLabel(tanara::AppController* controller, tanara::WorkflowStep step);

} // namespace tanara_qml::jobsupport

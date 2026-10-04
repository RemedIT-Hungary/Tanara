#pragma once
//
// Tanara QML — az összefoglaló-feladat (JobKind::Summarize) haladásának leképezése a felületre.
// Az Összefoglaló fül futó állapota és a feladat-sáv (ShellMeetingModel) ugyanezt mutatja.
//
// A core szakaszai (AppController::summarizeMeeting):
//   - rövid megbeszélés: egy szakasz, "single" — egy hívás, köztes haladás nincs (határozatlan);
//   - hosszabb: "notes" (done / total rész kész) + "merge" (határozatlan).
// A felület ezekből:
//   „Összefoglalás egy lépésben” · „Jegyzetek készítése: k / n rész” (valós csík k/n-ből) ·
//   „Összefésülés”. Ha egy korábbi (elbukott / megszakított) futás kész részjegyzeteit
//   használja újra, azt külön mondat jelzi. Szakaszok nélküli Summarize-feladat (a témánkénti
//   elemzés záró összegzése) és minden más feladat: stage üres, a feladat címe, határozatlan.
//
#include "tanara/jobs/JobTypes.h"

#include <QCoreApplication>
#include <QString>
#include <QVariantList>

namespace tanara_qml {

struct SummaryProgress {
    Q_DECLARE_TR_FUNCTIONS(SummaryProgress)

public:
    QString stage;          // "" | "single" | "notes" | "merge"
    QString label;          // a futó szakasz egy sorban (üres: a feladat címe a megjelenítendő)
    int percent = -1;       // 0..100 csak valós darab-haladásból (notes); -1 = határozatlan
    int done = -1;          // kész részek (notes / merge); -1 = nincs
    int total = -1;
    int reused = 0;         // ennyi rész jegyzete jött egy korábbi futásból
    QString reusedNote;     // a hozzá tartozó mondat (üres, ha reused == 0)
    // A szakasz-lista (JobStageRow): [{ id, label, state, percent, detail }]
    QVariantList stages;

    bool isValid() const { return !stage.isEmpty(); }

    static SummaryProgress from(const tanara::JobProgress& job);

    // A "notes" szakasz core-részletéből (pl. „3/5. rész · 2 korábbi futásból”) a korábbi
    // futásból átvett részek száma: az utolsó „ · ” utáni első szám; nincs ilyen → 0.
    // (A core a számot csak ebben a szövegben adja át.)
    static int reusedParts(const QString& notesDetail);
};

} // namespace tanara_qml

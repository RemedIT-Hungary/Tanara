#pragma once
//
// Tanara QML: mapping of the summary task's (JobKind::Summarize) progress onto the UI. The
// Summary tab's running state and the task bar (ShellMeetingModel) show the same.
//
// The core's phases (AppController::summarizeMeeting):
//   - short meeting: one phase, "single": one call, no intermediate progress (indeterminate);
//   - longer: "notes" (done / total parts finished) + "merge" (indeterminate);
//   - with LM Studio first, if needed: "model": loading the model with the needed context.
// The UI shows from these:
//   "Summarizing in one step" · "Taking notes: part k / n" (a real bar from k/n) ·
//   "Merging". If it reuses the finished partial notes of an earlier (failed / cancelled)
//   run, a separate sentence says so. A Summarize task without phases (the per-topic
//   analysis' closing summary) and every other task: stage empty, the task's title, indeterminate.
//
#include "tanara/domain/Job.h"

#include <QCoreApplication>
#include <QString>
#include <QVariantList>

namespace tanara_qml {

struct SummaryProgress {
    Q_DECLARE_TR_FUNCTIONS(SummaryProgress)

public:
    QString stage;          // "" | "model" | "single" | "notes" | "merge"
    QString label;          // the running phase on one line (empty: the task's title is to be shown)
    int percent = -1;       // 0..100 only from real per-part progress (notes); -1 = indeterminate
    int done = -1;          // finished parts (notes / merge); -1 = none
    int total = -1;
    int reused = 0;         // this many parts' notes came from an earlier run
    QString reusedNote;     // its sentence (empty if reused == 0)
    // The phase list (JobStageRow): [{ id, label, state, percent, detail }]
    QVariantList stages;

    bool isValid() const { return !stage.isEmpty(); }

    static SummaryProgress from(const tanara::JobProgress& job);

    // From the "notes" phase's core detail (e.g. "part 3/5 · 2 from earlier runs") the number of
    // parts taken over from an earlier run: the first number after the last " · "; none -> 0.
    // (The core passes the number only in this text.)
    static int reusedParts(const QString& notesDetail);

private:
    // The mapping without the model phase (single / notes / merge).
    static SummaryProgress fromWork(const tanara::JobProgress& job);
};

} // namespace tanara_qml

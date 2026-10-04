#include "SummaryProgress.h"

#include "JobSupport.h"

#include <QRegularExpression>
#include <QVariantMap>

namespace tanara_qml {

using namespace tanara;

int SummaryProgress::reusedParts(const QString& notesDetail)
{
    const int sep = notesDetail.lastIndexOf(QStringLiteral(" · "));
    if (sep < 0)
        return 0;
    static const QRegularExpression num(QStringLiteral("(\\d+)"));
    const QRegularExpressionMatch m = num.match(notesDetail, sep);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

SummaryProgress SummaryProgress::from(const JobProgress& job)
{
    SummaryProgress p;
    if (job.kind != JobKind::Summarize)
        return p;
    auto stageMap = [](const QString& id, const QString& label, StageState state, int percent,
                       const QString& detail) {
        return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
                           {QStringLiteral("state"), jobsupport::stageStateName(state)},
                           {QStringLiteral("percent"), percent}, {QStringLiteral("detail"), detail}};
    };

    if (const JobStage* single = job.stage(QStringLiteral("single"))) {
        p.stage = QStringLiteral("single");
        p.label = tr("Összefoglalás egy lépésben");
        // A szakasz a hívás indulásakor vált futóra; addig is ez az egyetlen teendő.
        const StageState st = single->state == StageState::Waiting ? StageState::Running : single->state;
        p.stages << stageMap(p.stage, p.label, st, -1, QString());
        return p;
    }

    const JobStage* notes = job.stage(QStringLiteral("notes"));
    const JobStage* merge = job.stage(QStringLiteral("merge"));
    if (!notes || !merge)
        return p;   // szakaszok nélküli összegzés (témánkénti elemzés vége): a feladat címe

    p.done = job.done;
    p.total = job.total;
    p.reused = reusedParts(notes->detail);
    if (p.reused > 0)
        p.reusedNote = tr("%n rész jegyzete egy korábbi, félbemaradt futásból megvan; ezek nem futnak újra.",
                          nullptr, p.reused);

    const bool merging = merge->state == StageState::Running || notes->state == StageState::Done;
    const QString partsText = p.total > 0 ? tr("%1 / %2 rész").arg(qMax(0, p.done)).arg(p.total) : QString();
    int notesPercent = -1;
    if (p.total > 0 && p.done >= 0)
        notesPercent = qBound(0, int(100.0 * p.done / p.total + 0.5), 100);

    if (merging) {
        p.stage = QStringLiteral("merge");
        p.label = tr("Összefésülés");
    } else {
        p.stage = QStringLiteral("notes");
        p.label = p.total > 0 ? tr("Jegyzetek készítése: %1 / %2 rész").arg(qMax(0, p.done)).arg(p.total)
                              : tr("Jegyzetek készítése");
        p.percent = notesPercent;
    }

    const StageState notesState = merging ? StageState::Done : StageState::Running;
    p.stages << stageMap(QStringLiteral("notes"), tr("Jegyzetek készítése"), notesState,
                         merging ? -1 : notesPercent, partsText);
    p.stages << stageMap(QStringLiteral("merge"), tr("Összefésülés"),
                         merging ? StageState::Running : StageState::Waiting, -1, QString());
    return p;
}

} // namespace tanara_qml

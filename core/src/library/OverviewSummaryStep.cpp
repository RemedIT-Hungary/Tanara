#include "tanara/library/OverviewSummaryStep.h"

#include <QCoreApplication>

namespace tanara::library {

QVariantMap overviewSummaryStep(const SummaryStepFacts& f)
{
    QString phase, sub, state, fixTarget, note, detail;
    int progress = -1;
    bool canStart = false;

    if (f.running) {
        phase = QStringLiteral("running");
        state = QStringLiteral("running");
        sub = f.percent >= 0 ? QStringLiteral("%1%").arg(f.percent) : QCoreApplication::translate("OverviewSummaryStep", "fut");
        progress = f.percent >= 0 ? f.percent : -2;
    } else if (!f.hasTranscript) {
        phase = QStringLiteral("noTranscript");
        state = QStringLiteral("waiting");
        sub = QCoreApplication::translate("OverviewSummaryStep", "az átirat után");
    } else if (f.blocked) {
        phase = QStringLiteral("blocked");
        state = QStringLiteral("attention");
        sub = QCoreApplication::translate("OverviewSummaryStep", "nincs beállítva nyelvi modell");
        fixTarget = QStringLiteral("services");
        note = f.blockerDetail;
    } else if (!f.errorMessage.isEmpty()) {
        phase = QStringLiteral("failed");
        state = QStringLiteral("attention");
        sub = QCoreApplication::translate("OverviewSummaryStep", "nem sikerült");
        note = f.errorMessage;
        detail = f.errorDetail;
        if (f.errorFixInSettings) fixTarget = QStringLiteral("services");
        canStart = true;
    } else if (f.hasSummary) {
        phase = f.stale ? QStringLiteral("stale") : QStringLiteral("done");
        state = f.stale ? QStringLiteral("attention") : QStringLiteral("done");
        sub = f.stale ? QCoreApplication::translate("OverviewSummaryStep", "elavult") : QCoreApplication::translate("OverviewSummaryStep", "kész");
        canStart = true;
    } else {
        phase = QStringLiteral("ready");
        state = QStringLiteral("waiting");
        sub = QCoreApplication::translate("OverviewSummaryStep", "még nem készült");
        canStart = true;
    }
    return {{QStringLiteral("key"), QStringLiteral("summary")},
            {QStringLiteral("title"), QCoreApplication::translate("OverviewSummaryStep", "Összefoglaló")},
            {QStringLiteral("sub"), sub},
            {QStringLiteral("state"), state},
            {QStringLiteral("parallel"), false},
            {QStringLiteral("progress"), progress},
            {QStringLiteral("phase"), phase},
            {QStringLiteral("canStart"), canStart},
            {QStringLiteral("fixTarget"), fixTarget},
            {QStringLiteral("note"), note},
            {QStringLiteral("detail"), detail}};
}

} // namespace tanara::library

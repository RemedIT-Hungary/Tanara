#include "tanara/library/OverviewSpeakers.h"

#include <QCoreApplication>

namespace tanara::library {

QString diarizationProblemText(const QString& raw)
{
    if (raw.contains(QLatin1String("no_segmentation_model"))) return QCoreApplication::translate("OverviewSpeakers", "nincs szegmentáló modell");
    if (raw.contains(QLatin1String("no_embedding_model"))) return QCoreApplication::translate("OverviewSpeakers", "nincs hangmodell");
    if (raw.contains(QLatin1String("decode_failed"))) return QCoreApplication::translate("OverviewSpeakers", "a hangsáv nem olvasható");
    if (raw.contains(QLatin1String("segmentation_failed"))) return QCoreApplication::translate("OverviewSpeakers", "a szegmentálás hibát adott");
    if (raw.contains(QLatin1String("no_input"))) return QCoreApplication::translate("OverviewSpeakers", "nincs használható sáv");
    if (raw.contains(QLatin1String("no speakers")) || raw.contains(QLatin1String("no turns"))) return QCoreApplication::translate("OverviewSpeakers", "nem találtam beszélőt");
    return QCoreApplication::translate("OverviewSpeakers", "részletek a naplóban");
}

QVariantMap overviewSpeakers(const SpeakerFacts& st, bool diarizeRunning)
{
    const bool failed = !st.problem.isEmpty();
    QString line;
    QString fix;
    bool attention = false;
    if (failed) {
        line = QCoreApplication::translate("OverviewSpeakers", "Beszélők: a felismerés nem sikerült") + QStringLiteral(" — ") + diarizationProblemText(st.problem);
        attention = true;
    } else if (st.source == QLatin1String("provider")) {
        line = QCoreApplication::translate("OverviewSpeakers", "Beszélők: a szolgáltatótól");
    } else if (st.source == QLatin1String("local")) {
        line = QCoreApplication::translate("OverviewSpeakers", "Beszélők: helyi elkülönítés");
    } else if (st.blocker == QLatin1String("disabled")) {
        line = QCoreApplication::translate("OverviewSpeakers", "Beszélők: nincs — kapcsold be a Beállításokban");
        fix = QStringLiteral("speakers");
    } else if (st.blocker == QLatin1String("model")) {
        line = QCoreApplication::translate("OverviewSpeakers", "Beszélők: nincs — tölts le modellt a Beállításokban");
        fix = QStringLiteral("speakers");
        attention = true;
    } else {
        line = QCoreApplication::translate("OverviewSpeakers", "Beszélők: nincs szétválasztva");
    }
    return {{QStringLiteral("source"), st.source},
            {QStringLiteral("line"), line},
            {QStringLiteral("fixTarget"), fix},
            {QStringLiteral("canDiarize"), st.canDiarize},
            {QStringLiteral("hasNames"), st.hasNames},
            {QStringLiteral("speakerCount"), st.speakerCount},
            {QStringLiteral("running"), diarizeRunning},
            {QStringLiteral("failed"), failed},
            {QStringLiteral("problem"), failed ? diarizationProblemText(st.problem) : QString()},
            {QStringLiteral("attention"), attention}};
}

} // namespace tanara::library

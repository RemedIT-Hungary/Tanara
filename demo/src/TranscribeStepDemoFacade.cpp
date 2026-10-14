#include "demo/TranscribeStepDemoFacade.h"

#include "demo/DemoText.h"

#include "tanara/providers/core/ReadinessModel.h"

namespace tanara_demo {

using tanara::JobStage;
using tanara::StageState;

namespace {

JobStage stage(const QString& id, const QString& label, StageState state, int percent = -1,
               const QString& detail = QString())
{
    JobStage s;
    s.id = id;
    s.label = label;
    s.state = state;
    s.percent = percent;
    s.detail = detail;
    return s;
}

} // namespace

TranscribeStepDemoFacade::TranscribeStepDemoFacade(const QString& scenario, QObject* parent)
    : ITranscribeStepFacade(parent), m_scenario(scenario)
{
}

tanara::processing::TranscribeStepSnapshot TranscribeStepDemoFacade::snapshot(const QString&) const
{
    const QString st = m_scenario.isEmpty() ? QStringLiteral("steps") : m_scenario;
    tanara::processing::TranscribeStepSnapshot s;
    // One attached tag, the suggestion comes from the title (fictional pool).
    s.tagsDemoState = QStringLiteral("draft");
    s.noteDemo = true;
    if (st == QLatin1String("note") || st == QLatin1String("readyNote")) {
        // Template suggestions from earlier meetings with a similar title + the detected call.
        s.noteDetectedCallApp = QStringLiteral("Microsoft Teams");
        s.noteSuggestions = true;
    } else {
        s.noteText = demoText("Ügyféltámogatás átadása az új csapatnak. Érintett rendszerek: jegykezelő, "
                              "súgóoldalak, számlázás. Résztvevők: Molnár Eszter, Tóth Bence.",
                              "Support handover to the new team. Systems involved: ticket system, help pages, "
                              "billing. Participants: Sophie Bennett, Ryan Mitchell.");
    }
    s.identifyAvailable = true;
    s.identifyEnabled = true;
    s.mixdownState = QStringLiteral("ready");
    s.mixdownPercent = -1;
    s.providerLabel = tr("%1 · saját kulcs").arg(QStringLiteral("Soniox"));
    s.state = QStringLiteral("steps");

    if (st == QLatin1String("running") || st == QLatin1String("uploading")) {
        s.state = QStringLiteral("running");
        const bool up = st == QLatin1String("uploading");
        s.fixedEtaSeconds = up ? -1 : 240;
        // The job's title stays empty (the view's default "Transcription in progress").
        s.job.stages = {
            stage(QStringLiteral("mixdown"), tr("Lekeverés"), StageState::Done),
            stage(QStringLiteral("upload"), tr("Feltöltés"), up ? StageState::Running : StageState::Done,
                  up ? 62 : -1, tr("%1 perc, %2 sáv").arg(52).arg(3)),
            stage(QStringLiteral("transcribe"), tr("Átírás"), up ? StageState::Waiting : StageState::Running),
            stage(QStringLiteral("diarize"), tr("Beszélők szétválasztása"),
                  up ? StageState::Waiting : StageState::Running),
            stage(QStringLiteral("identify"), tr("Résztvevők azonosítása"), StageState::Waiting),
        };
        if (!up)
            s.job.stages.removeFirst();   // the sketch has no mixdown phase (the mix already existed)
    } else if (st == QLatin1String("failed")) {
        s.state = QStringLiteral("failed");
        s.errorMessage = tr("A szolgáltató nem fogadta el az API-kulcsot. Ellenőrizd vagy cseréld "
                            "a kulcsot a Beállításokban.");
        s.errorDetail = QStringLiteral("HTTP 401 · invalid_api_key · ") + tr("szept. 30. 16:42");
        s.fixActionLabel = tr("Kulcs módosítása");
        s.fixActionPage = QStringLiteral("providers");
    } else if (st == QLatin1String("ready") || st == QLatin1String("readyNote")) {
        s.canStart = true;
    } else if (st == QLatin1String("mixing")) {
        s.canStart = true;
        s.mixdownState = QStringLiteral("running");
        s.mixdownPercent = 64;
        s.mixdownCancellable = true;
    } else {
        s.blocker = {
            {QStringLiteral("title"), tr("Nincs beállítva átíró szolgáltató")},
            {QStringLiteral("text"), tr("Hiányzik: API-kulcs (Soniox). Add meg a saját szolgáltatód "
                                        "adatait a Beállításokban.")},
            {QStringLiteral("actionLabel"), tr("Szolgáltató beállítása")},
            {QStringLiteral("actionPage"), QStringLiteral("providers")},
            {QStringLiteral("reason"), tr("A szolgáltató beállítása után indítható.")},
            {QStringLiteral("kind"), int(tanara::providers::BlockerKind::ProviderConfig)},
        };
        s.cloudSoon = true;
    }
    return s;
}

} // namespace tanara_demo

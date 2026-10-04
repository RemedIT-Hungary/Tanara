#include "JobSupport.h"

#include "AppContext.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/jobs/JobErrors.h"
#include "tanara/provider/ProviderRegistry.h"

#include <QCoreApplication>

namespace tanara_qml::jobsupport {

namespace {
QString trj(const char* text) { return QCoreApplication::translate("JobSupport", text); }
} // namespace

tanara::AppController* resolveController(QObject* injected)
{
    if (auto* c = qobject_cast<tanara::AppController*>(injected))
        return c;
    return AppContext::instance()->controller();
}

bool demoMode(tanara::AppController* controller)
{
    return controller == nullptr || AppContext::instance()->demo();
}

QString formatDuration(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("–");
    const qint64 total = (ms + 500) / 1000;
    const qint64 h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

QString formatEta(int seconds)
{
    if (seconds < 0)
        return QString();
    if (seconds < 60)
        return trj("kevesebb mint 1 perc van hátra");
    return trj("kb. %1 perc van hátra").arg((seconds + 30) / 60);
}

QString stageStateName(tanara::StageState state)
{
    switch (state) {
    case tanara::StageState::Running: return QStringLiteral("running");
    case tanara::StageState::Done:    return QStringLiteral("done");
    case tanara::StageState::Failed:  return QStringLiteral("failed");
    case tanara::StageState::Skipped: return QStringLiteral("skipped");
    case tanara::StageState::Waiting: break;
    }
    return QStringLiteral("waiting");
}

FixAction fixActionForError(const tanara::JobError& error)
{
    const QString hint = error.fixActionHint;
    if (hint.isEmpty())
        return {};
    if (hint.startsWith(QLatin1String("cloud")) || hint.startsWith(QLatin1String("login:")))
        return {trj("Tanara Cloud fiók"), QStringLiteral("cloud")};
    // Kontextus-hiba: a Beállítások LLM-kártyája (kontextus-beállítás); LM Studiónál mellette
    // az újratöltés-és-újra gomb is.
    if (hint.contains(QLatin1String("-context"))) {
        FixAction a{QCoreApplication::translate("JobSupport", "Beállítások"), QStringLiteral("providers")};
        if (tanara::isReloadContextHint(hint))
            a.reloadContext = tanara::contextFixTokens(hint);
        return a;
    }
    if (hint.startsWith(QLatin1String("settings"))) {
        // Kulcs-hibánál (401 / 403) a gomb a kulcsot nevezi meg; különben a szolgáltatót.
        const bool keyProblem = error.detail.contains(QLatin1String("HTTP 401"))
                             || error.detail.contains(QLatin1String("HTTP 403"));
        return {keyProblem ? trj("Kulcs módosítása") : trj("Szolgáltató beállítása"),
                QStringLiteral("providers")};
    }
    return {trj("Beállítások"), QString()};
}

QVariantMap blockerInfo(tanara::AppController* controller, tanara::WorkflowStep step,
                        const tanara::ReadinessResult& r)
{
    QVariantMap out;
    if (r.runnable)
        return out;
    const bool transcribe = step == tanara::WorkflowStep::Transcribe;
    const bool cloudLive = controller && controller->cloudLive();
    QString title, text, actionLabel, actionPage, reason;

    const bool cloudProvider = r.providerId == tanara::cloud::ProviderId;
    switch (r.blockerKind) {
    case tanara::BlockerKind::ProviderConfig:
        title = transcribe ? trj("Nincs beállítva átíró szolgáltató")
                           : trj("Nincs beállítva összefoglaló szolgáltató");
        // A pontos hiány (pl. „Hiányzik: API-kulcs (Soniox)”) + merre tovább. A Tanara Cloudot
        // csak akkor említjük, ha ebben a buildben tényleg be lehet jelentkezni.
        text = r.detail;
        if (!text.isEmpty() && !text.endsWith(QLatin1Char('.')))
            text += QLatin1Char('.');
        text += QLatin1Char(' ');
        text += cloudLive ? trj("Add meg a saját kulcsodat, vagy jelentkezz be a Tanara Cloudba.")
                          : trj("Add meg a saját szolgáltatód adatait a Beállításokban.");
        actionLabel = trj("Szolgáltató beállítása");
        actionPage = QStringLiteral("providers");
        reason = trj("A szolgáltató beállítása után indítható.");
        break;
    case tanara::BlockerKind::Auth:
        title = cloudProvider ? trj("Nem vagy bejelentkezve a Tanara Cloudba")
                              : trj("Nincs bejelentkezve a szolgáltatóhoz");
        text = cloudProvider
            ? trj("Jelentkezz be, vagy válts saját kulcsos szolgáltatóra a Beállításokban.")
            : r.detail;
        actionLabel = trj("Bejelentkezés");
        actionPage = cloudProvider ? QStringLiteral("cloud") : QStringLiteral("providers");
        reason = trj("Bejelentkezés után indítható.");
        break;
    case tanara::BlockerKind::Cloud:
        if (r.fixActionHint == QLatin1String("cloud:topup")) {
            title = trj("Elfogyott a Tanara Cloud egyenleged");
            text = trj("Töltsd fel az egyenleged, vagy válts saját kulcsos szolgáltatóra.");
            actionLabel = trj("Feltöltés");
            reason = trj("Feltöltés után indítható.");
        } else {
            title = trj("A Tanara Cloudhoz frissíteni kell az alkalmazást");
            text = r.detail + QLatin1Char(' ') + trj("A saját kulcsos mód frissítés nélkül is működik.");
            actionLabel = trj("Frissítés");
            reason = trj("Frissítés után indítható.");
        }
        actionPage = QStringLiteral("cloud");
        break;
    case tanara::BlockerKind::MeetingState:
        if (transcribe) {
            title = trj("Nincs aktív hangsáv");
            text = trj("Ennek a felvételnek minden sávja eldobott vagy hiányzik, ezért nincs mit átírni.");
            reason = trj("Aktív hangsáv nélkül nem indítható.");
        } else {
            title = trj("Még nincs átirat");
            text = trj("Az összefoglaló az átiratból készül; előbb az átírást kell lefuttatni.");
            reason = trj("Az átirat elkészülte után indítható.");
        }
        break;
    }
    out.insert(QStringLiteral("title"), title);
    out.insert(QStringLiteral("text"), text.trimmed());
    out.insert(QStringLiteral("actionLabel"), actionLabel);
    out.insert(QStringLiteral("actionPage"), actionPage);
    out.insert(QStringLiteral("reason"), reason);
    out.insert(QStringLiteral("kind"), int(r.blockerKind));
    return out;
}

QString providerLabel(tanara::AppController* controller, tanara::WorkflowStep step)
{
    if (!controller)
        return trj("saját kulcs");
    if (controller->usesCloud(step))
        return QStringLiteral("Tanara Cloud");
    const tanara::AppSettings s = controller->settings()->settings();
    const QString name = step == tanara::WorkflowStep::Transcribe
        ? tanara::SttProviderRegistry::instance().descriptor(s.sttProviderId).displayName
        : tanara::LlmProviderRegistry::instance().descriptor(s.llmProviderId).displayName;
    if (name.isEmpty())
        return trj("saját kulcs");
    return trj("%1 · saját kulcs").arg(name);
}

} // namespace tanara_qml::jobsupport

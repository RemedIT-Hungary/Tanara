#include "JobSupport.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/foundation/time/TimeFormat.h"

#include "AppContext.h"

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/library/jobs/JobErrors.h"
#include "tanara/providers/core/ProviderRegistry.h"

#include <QCoreApplication>

namespace tanara_qml::jobsupport {

namespace {
QString trj(const char* text) { return QCoreApplication::translate("JobSupport", text); }

// lupdate does not see through trj(): the strings it translates are listed here, so they reach the .ts
// (without this the English UI shows them in Hungarian).
[[maybe_unused]] const char* const kTranslatable[] = {
    QT_TRANSLATE_NOOP("JobSupport", "kevesebb mint 1 perc van hátra"),
    QT_TRANSLATE_NOOP("JobSupport", "kb. %1 perc van hátra"),
    QT_TRANSLATE_NOOP("JobSupport", "Tanara Cloud fiók"),
    QT_TRANSLATE_NOOP("JobSupport", "Kulcs módosítása"),
    QT_TRANSLATE_NOOP("JobSupport", "Szolgáltató beállítása"),
    QT_TRANSLATE_NOOP("JobSupport", "Nincs beállítva átíró szolgáltató"),
    QT_TRANSLATE_NOOP("JobSupport", "Nincs beállítva összefoglaló szolgáltató"),
    QT_TRANSLATE_NOOP("JobSupport", "Add meg a saját szolgáltatód adatait a Beállításokban."),
    QT_TRANSLATE_NOOP("JobSupport", "A szolgáltató beállítása után indítható."),
    QT_TRANSLATE_NOOP("JobSupport", "A Tanara Cloud még nem érhető el"),
    QT_TRANSLATE_NOOP("JobSupport", "Nincs bejelentkezve a szolgáltatóhoz"),
    QT_TRANSLATE_NOOP("JobSupport", "Válts saját kulcsos szolgáltatóra a Beállításokban."),
    QT_TRANSLATE_NOOP("JobSupport", "Bejelentkezés"),
    QT_TRANSLATE_NOOP("JobSupport", "Bejelentkezés után indítható."),
    QT_TRANSLATE_NOOP("JobSupport", "Nincs aktív hangsáv"),
    QT_TRANSLATE_NOOP("JobSupport", "Ennek a felvételnek minden sávja eldobott vagy hiányzik, ezért nincs mit átírni."),
    QT_TRANSLATE_NOOP("JobSupport", "Aktív hangsáv nélkül nem indítható."),
    QT_TRANSLATE_NOOP("JobSupport", "Még nincs átirat"),
    QT_TRANSLATE_NOOP("JobSupport", "Az összefoglaló az átiratból készül; előbb az átírást kell lefuttatni."),
    QT_TRANSLATE_NOOP("JobSupport", "Az átirat elkészülte után indítható."),
    QT_TRANSLATE_NOOP("JobSupport", "saját kulcs"),
    QT_TRANSLATE_NOOP("JobSupport", "%1 · saját kulcs"),
};
} // namespace

tanara::app::AppController* resolveController(QObject* injected)
{
    if (auto* c = qobject_cast<tanara::app::AppController*>(injected))
        return c;
    return AppContext::instance()->controller();
}

bool demoMode(tanara::app::AppController* controller)
{
    return controller == nullptr || AppContext::instance()->demo();
}

QString formatDuration(qint64 ms)
{
    return tanara::timefmt::duration(ms);
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
    // Context error: Settings' LLM card (context setting); with LM Studio the reload-and-retry
    // button next to it too.
    if (hint.contains(QLatin1String("-context"))) {
        FixAction a{QCoreApplication::translate("JobSupport", "Beállítások"), QStringLiteral("providers")};
        if (tanara::library::isReloadContextHint(hint))
            a.reloadContext = tanara::library::contextFixTokens(hint);
        return a;
    }
    if (hint.startsWith(QLatin1String("settings"))) {
        // For a key error (401 / 403) the button names the key; otherwise the provider.
        const bool keyProblem = error.detail.contains(QLatin1String("HTTP 401"))
                             || error.detail.contains(QLatin1String("HTTP 403"));
        return {keyProblem ? trj("Kulcs módosítása") : trj("Szolgáltató beállítása"),
                QStringLiteral("providers")};
    }
    return {trj("Beállítások"), QString()};
}

QVariantMap blockerInfo(tanara::app::AppController* controller, tanara::providers::WorkflowStep step,
                        const tanara::providers::ReadinessResult& r)
{
    QVariantMap out;
    if (r.runnable)
        return out;
    const bool transcribe = step == tanara::providers::WorkflowStep::Transcribe;
    QString title, text, actionLabel, actionPage, reason;

    const bool cloudProvider = r.providerId == tanara::cloud::ProviderId;
    switch (r.blockerKind) {
    case tanara::providers::BlockerKind::ProviderConfig:
        title = transcribe ? trj("Nincs beállítva átíró szolgáltató")
                           : trj("Nincs beállítva összefoglaló szolgáltató");
        // The exact gap (e.g. "Missing: API key (Soniox)") + where to go next.
        text = r.detail;
        if (!text.isEmpty() && !text.endsWith(QLatin1Char('.')))
            text += QLatin1Char('.');
        text += QLatin1Char(' ');
        text += trj("Add meg a saját szolgáltatód adatait a Beállításokban.");
        actionLabel = trj("Szolgáltató beállítása");
        actionPage = QStringLiteral("providers");
        reason = trj("A szolgáltató beállítása után indítható.");
        break;
    case tanara::providers::BlockerKind::Auth:
        title = cloudProvider ? trj("A Tanara Cloud még nem érhető el")
                              : trj("Nincs bejelentkezve a szolgáltatóhoz");
        text = cloudProvider
            ? trj("Válts saját kulcsos szolgáltatóra a Beállításokban.")
            : r.detail;
        actionLabel = cloudProvider ? trj("Szolgáltató beállítása") : trj("Bejelentkezés");
        actionPage = QStringLiteral("providers");
        reason = cloudProvider ? trj("A szolgáltató beállítása után indítható.")
                               : trj("Bejelentkezés után indítható.");
        break;
    case tanara::providers::BlockerKind::Cloud:
        // The UI does not offer Tanara Cloud (coming soon): the remedy is an own-key provider.
        title = trj("A Tanara Cloud még nem érhető el");
        text = trj("Válts saját kulcsos szolgáltatóra a Beállításokban.");
        actionLabel = trj("Szolgáltató beállítása");
        actionPage = QStringLiteral("providers");
        reason = trj("A szolgáltató beállítása után indítható.");
        break;
    case tanara::providers::BlockerKind::MeetingState:
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

QString providerLabel(tanara::app::AppController* controller, tanara::providers::WorkflowStep step)
{
    if (!controller)
        return trj("saját kulcs");
    if (controller->usesCloud(step))
        return QStringLiteral("Tanara Cloud");
    const tanara::app::AppSettings s = controller->settings()->settings();
    const QString name = step == tanara::providers::WorkflowStep::Transcribe
        ? tanara::providers::ProviderRegistry::defaultInstance().descriptor(tanara::providers::CapabilityKind::Transcription, s.providers.stt.selectedId).displayName
        : tanara::providers::ProviderRegistry::defaultInstance().descriptor(tanara::providers::CapabilityKind::Chat, s.providers.llm.selectedId).displayName;
    if (name.isEmpty())
        return trj("saját kulcs");
    return trj("%1 · saját kulcs").arg(name);
}

} // namespace tanara_qml::jobsupport

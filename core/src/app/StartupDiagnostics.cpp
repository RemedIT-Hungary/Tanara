#include "tanara/app/StartupDiagnostics.h"

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/domain/Types.h"
#include "tanara/recording/audio/DeviceManager.h"
#include "tanara/foundation/Version.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/library/store/MeetingStore.h"

#include <QSysInfo>
#include <QtGlobal>

namespace tanara::app {

namespace {

QString kindStr(TrackKind k)
{
    switch (k) {
        case TrackKind::Mic:      return QStringLiteral("mic");
        case TrackKind::Loopback: return QStringLiteral("loopback");
        default:                  return QStringLiteral("egyéb");
    }
}

} // namespace

void logStartupDiagnostics(const AppController& app)
{
    // Header: always at INFO level.
    qCInfo(lcApp).noquote() << QStringLiteral("Tanara %1 indul (loglevel=%2)")
                                   .arg(libraryVersion(), logLevelName(currentLogLevel()));
    qCInfo(lcApp).noquote() << QStringLiteral("Platform: %1 | Qt %2")
                                   .arg(QSysInfo::prettyProductName(), QString::fromLatin1(qVersion()));
    const QString lf = currentLogFilePath();
    if (!lf.isEmpty())
        qCInfo(lcApp).noquote() << QStringLiteral("Logfájl: %1 (hibák: tanara-error.log)").arg(lf);

    // Detailed dump: at DEBUG level.
    const SettingsManager* sm = app.settings();
    const AppSettings s = sm->settings();
    qCDebug(lcApp).noquote() << QStringLiteral("settings.json: %1").arg(sm->settingsFilePath());
    qCDebug(lcApp).noquote() << QStringLiteral("audioDir:    %1").arg(s.audioDir);
    qCDebug(lcApp).noquote() << QStringLiteral("notesDir:    %1").arg(s.notesDir);
    qCDebug(lcApp).noquote() << QStringLiteral("metadataDir: %1").arg(s.metadataDir);
    qCDebug(lcApp).noquote() << QStringLiteral("saját név:   %1").arg(s.userSpeakerName);
    qCDebug(lcApp).noquote() << QStringLiteral("autoRecordAllDevices: %1, languageHints: %2")
                                    .arg(s.autoRecordAllDevices ? QStringLiteral("igen") : QStringLiteral("nem"),
                                         s.languageHints.join(QLatin1Char(',')));
    qCDebug(lcApp).noquote() << QStringLiteral("hangminőség: %1 (%2 kbps), lekeverés: %3")
                                    .arg(s.audioQuality)
                                    .arg(opusBitrateKbps(s.audioQuality))
                                    .arg(s.mixdownMode);

    // Provider selection: we NEVER log the API key, only its presence.
    const ProviderConfig stt = s.sttSelected();
    qCDebug(lcStt).noquote() << QStringLiteral("STT: provider=%1 baseUrl=%2 model=%3 apiKey=%4")
                                    .arg(s.sttProviderId, stt.baseUrl, stt.model,
                                         app.hasSecret(keys::SonioxApiKey) ? QStringLiteral("megadva")
                                                                           : QStringLiteral("NINCS"));
    const ProviderConfig llm = s.llmSelected();
    qCDebug(lcLlm).noquote() << QStringLiteral("LLM: provider=%1 baseUrl=%2 model=%3 temp=%4 maxTokens=%5 apiKey=%6")
                                    .arg(s.llmProviderId, llm.baseUrl, llm.model)
                                    .arg(llm.temperature).arg(llm.maxTokens)
                                    .arg(app.hasSecret(keys::LlmApiKey) ? QStringLiteral("megadva")
                                                                        : QStringLiteral("nincs (LM Studio: opcionális)"));

    // Audio devices seen (worth calling after refreshDevices()).
    if (auto* dm = app.devices()) {
        const auto devs = dm->captureDevices();
        qCDebug(lcAudio).noquote() << QStringLiteral("Audio capture eszközök (%1):").arg(devs.size());
        for (const auto& d : devs)
            qCDebug(lcAudio).noquote() << QStringLiteral("  • %1 [%2%3]")
                                              .arg(d.name, kindStr(d.kind),
                                                   d.isDefault ? QStringLiteral(", default") : QString());
    }

    if (auto* st = app.store())
        qCDebug(lcStore).noquote() << QStringLiteral("Meetingek a tárban: %1").arg(st->loadAll().size());
}

} // namespace tanara::app
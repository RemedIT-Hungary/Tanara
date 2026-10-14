#include "tanara/app/StartupDiagnostics.h"
#include "tanara/providers/core/ProviderConfig.h"

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/domain/Audio.h"
#include "tanara/recording/audio/DeviceManager.h"
#include "tanara/foundation/Version.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/library/store/MeetingRepository.h"

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
    const paths::AppPaths& ap = sm->paths();
    for (paths::Role r : {paths::Role::Config, paths::Role::Data, paths::Role::State, paths::Role::Logs,
                          paths::Role::Models, paths::Role::Recordings, paths::Role::Notes})
        qCDebug(lcApp).noquote() << QStringLiteral("%1: %2 (%3)").arg(paths::roleName(r), -11)
                                        .arg(ap.dir(r), paths::sourceName(ap.source(r)));
    qCDebug(lcApp).noquote() << QStringLiteral("saját név:   %1").arg(s.recording.userSpeakerName);
    qCDebug(lcApp).noquote() << QStringLiteral("autoRecordAllDevices: %1, languageHints: %2")
                                    .arg(s.recording.autoRecordAllDevices ? QStringLiteral("igen") : QStringLiteral("nem"),
                                         s.processing.languageHints.join(QLatin1Char(',')));
    qCDebug(lcApp).noquote() << QStringLiteral("hangminőség: %1 (%2 kbps), lekeverés: %3")
                                    .arg(s.recording.audioQuality)
                                    .arg(opusBitrateKbps(s.recording.audioQuality))
                                    .arg(s.recording.mixdownMode);

    // Provider selection: we NEVER log the API key, only its presence.
    const providers::ProviderConfig stt = s.providers.stt.selected();
    qCDebug(lcStt).noquote() << QStringLiteral("STT: provider=%1 baseUrl=%2 model=%3 apiKey=%4")
                                    .arg(s.providers.stt.selectedId, stt.baseUrl, stt.model,
                                         app.hasSecret(keys::SonioxApiKey) ? QStringLiteral("megadva")
                                                                           : QStringLiteral("NINCS"));
    const providers::ProviderConfig llm = s.providers.llm.selected();
    qCDebug(lcLlm).noquote() << QStringLiteral("LLM: provider=%1 baseUrl=%2 model=%3 temp=%4 maxTokens=%5 apiKey=%6")
                                    .arg(s.providers.llm.selectedId, llm.baseUrl, llm.model)
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

    // The media tools (resolved once at startup; the version runs `ffmpeg -version` once).
    if (const media::MediaToolPtr mt = app.mediaTool())
        qCInfo(lcAudio).noquote() << QStringLiteral("Médiaeszközök: ffmpeg=%1 ffprobe=%2 (%3)")
                                         .arg(mt->ffmpegPath(), mt->ffprobePath(),
                                              mt->available() ? mt->version() : QStringLiteral("NEM ELÉRHETŐ"));

    if (auto* st = app.store())
        qCDebug(lcStore).noquote() << QStringLiteral("Meetingek a tárban: %1").arg(st->loadAll().size());
}

} // namespace tanara::app
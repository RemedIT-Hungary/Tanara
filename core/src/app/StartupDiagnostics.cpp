#include "tanara/app/StartupDiagnostics.h"
#include "tanara/providers/core/ProviderConfig.h"

#include "tanara/app/AppController.h"
#include "tanara/app/Services.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/domain/Audio.h"
#include "tanara/recording/RecordingService.h"
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
        default:                  return QStringLiteral("other");
    }
}

} // namespace

void logStartupDiagnostics(const AppController& app)
{
    // Header: always at INFO level.
    qCInfo(lcApp).noquote() << QStringLiteral("Tanara %1 starting (loglevel=%2)")
                                   .arg(libraryVersion(), logLevelName(currentLogLevel()));
    qCInfo(lcApp).noquote() << QStringLiteral("Platform: %1 | Qt %2")
                                   .arg(QSysInfo::prettyProductName(), QString::fromLatin1(qVersion()));
    const QString lf = currentLogFilePath();
    if (!lf.isEmpty())
        qCInfo(lcApp).noquote() << QStringLiteral("Log file: %1 (errors: tanara-error.log)").arg(lf);

    // Detailed dump: at DEBUG level.
    const SettingsManager* sm = app.settings();
    const AppSettings s = sm->settings();
    qCDebug(lcApp).noquote() << QStringLiteral("settings.json: %1").arg(sm->settingsFilePath());
    const paths::AppPaths& ap = sm->paths();
    for (paths::Role r : {paths::Role::Config, paths::Role::Data, paths::Role::State, paths::Role::Logs,
                          paths::Role::Models, paths::Role::Recordings, paths::Role::Notes})
        qCDebug(lcApp).noquote() << QStringLiteral("%1: %2 (%3)").arg(paths::roleName(r), -11)
                                        .arg(ap.dir(r), paths::sourceName(ap.source(r)));
    qCDebug(lcApp).noquote() << QStringLiteral("own name:    %1").arg(s.recording.userSpeakerName);
    qCDebug(lcApp).noquote() << QStringLiteral("autoRecordAllDevices: %1, languageHints: %2")
                                    .arg(s.recording.autoRecordAllDevices ? QStringLiteral("yes") : QStringLiteral("no"),
                                         s.processing.languageHints.join(QLatin1Char(',')));
    qCDebug(lcApp).noquote() << QStringLiteral("audio quality: %1 (%2 kbps), mixdown: %3")
                                    .arg(s.recording.audioQuality)
                                    .arg(opusBitrateKbps(s.recording.audioQuality))
                                    .arg(s.recording.mixdownMode);
    // The sleep inhibitor used while recording (platform::IPowerInhibitor).
    if (const Services* svc = app.services())
        qCInfo(lcApp).noquote() << QStringLiteral("Keep-awake while recording: %1, backend: %2")
                                       .arg(s.recording.keepAwake ? QStringLiteral("on") : QStringLiteral("off"),
                                            svc->recording().powerInhibitorBackend());

    // Provider selection: we NEVER log the API key, only its presence.
    const providers::ProviderConfig stt = s.providers.stt.selected();
    qCDebug(lcStt).noquote() << QStringLiteral("STT: provider=%1 baseUrl=%2 model=%3 apiKey=%4")
                                    .arg(s.providers.stt.selectedId, stt.baseUrl, stt.model,
                                         app.hasSecret(keys::SonioxApiKey) ? QStringLiteral("set")
                                                                           : QStringLiteral("MISSING"));
    const providers::ProviderConfig llm = s.providers.llm.selected();
    qCDebug(lcLlm).noquote() << QStringLiteral("LLM: provider=%1 baseUrl=%2 model=%3 temp=%4 maxTokens=%5 apiKey=%6")
                                    .arg(s.providers.llm.selectedId, llm.baseUrl, llm.model)
                                    .arg(llm.temperature).arg(llm.maxTokens)
                                    .arg(app.hasSecret(keys::LlmApiKey) ? QStringLiteral("set")
                                                                        : QStringLiteral("none (LM Studio: optional)"));
    // S3: where each capability runs (the stored route; Ask is answered per run).
    QStringList routes;
    for (providers::Capability cap : providers::allCapabilities())
        routes << QStringLiteral("%1=%2").arg(providers::capabilityKey(cap), providers::routeKey(s.routes.route(cap)));
    qCDebug(lcApp).noquote() << QStringLiteral("Routes: %1").arg(routes.join(QLatin1Char(' ')));

    // Audio devices seen (worth calling after refreshDevices()).
    if (auto* dm = app.devices()) {
        const auto devs = dm->captureDevices();
        qCDebug(lcAudio).noquote() << QStringLiteral("Audio capture devices (%1):").arg(devs.size());
        for (const auto& d : devs)
            qCDebug(lcAudio).noquote() << QStringLiteral("  • %1 [%2%3]")
                                              .arg(d.name, kindStr(d.kind),
                                                   d.isDefault ? QStringLiteral(", default") : QString());
    }

    // The media tools (resolved once at startup; the version runs `ffmpeg -version` once).
    if (const media::MediaToolPtr mt = app.mediaTool())
        qCInfo(lcAudio).noquote() << QStringLiteral("Media tools: ffmpeg=%1 ffprobe=%2 (%3)")
                                         .arg(mt->ffmpegPath(), mt->ffprobePath(),
                                              mt->available() ? mt->version() : QStringLiteral("NOT AVAILABLE"));

    if (auto* st = app.store())
        qCDebug(lcStore).noquote() << QStringLiteral("Meetings in the store: %1").arg(st->loadAll().size());
}

} // namespace tanara::app
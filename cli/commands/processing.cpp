// tanara-cli, processing module: transcribe, summarize, identify, speaker models, diarize-probe, embed-probe, providers.
// All output is English, no tr().
#include "Commands.h"
#include "VoiceCommands.h"
#include "DiarizeCommands.h"

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/cloud/CloudSession.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/processing/voiceid/VoiceEmbedder.h"
#include "tanara/providers/core/ConnectionTester.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "CloudCommands.h"

#include <QCoreApplication>
#include <QTimer>

namespace tanara::cli {

namespace {

int cmd_transcribe(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    QCoreApplication& qapp = ctx.qapp();
    const QString cmd = args.value(1);
    const QString id = args.value(2);
    if (id.isEmpty()) { err() << QStringLiteral("Missing meetingId.") << "\n"; return 1; }
    const bool complex = cmd == "summarize" && args.contains(QStringLiteral("--complex"));
    const providers::WorkflowStep step = cmd == "transcribe" ? providers::WorkflowStep::Transcribe : providers::WorkflowStep::Summarize;
    // Tanara Cloud: estimate + confirmation; without --yes it asks first.
    if (app.usesCloud(step)) {
        const providers::ReadinessResult r = app.canRun(step, id);
        if (!r.runnable) { err() << QStringLiteral("Cannot start: %1").arg(r.detail) << "\n"; return 1; }
        bool enough = false;
        if (!printCloudEstimate(app, id, cmd == "transcribe" ? QStringLiteral("transcribe") : QStringLiteral("summarize"),
                                complex ? QStringLiteral("complex") : QStringLiteral("quick"), &enough))
            return 2;
        if (!enough) return 2;
        if (!args.contains(QStringLiteral("--yes"))) {
            out() << QStringLiteral("Start? [y/N] "); out().flush();
            char buf[16] = {0};
            if (!fgets(buf, sizeof buf, stdin) || (buf[0] != 'y' && buf[0] != 'Y')) {
                out() << QStringLiteral("Cancelled — nothing was charged.") << "\n"; return 1;
            }
        }
    }
    const QString lang = QStringLiteral("en");
    QObject::connect(&app, &app::AppController::cloudCharged, &qapp,
                     [&](QString, QString kind, cloud::Money total, int calls, cloud::Money balance, QString vatMode) {
        const QString sum = formatMoney(total, cloud::MoneyStyle::Charge, lang), bal = formatMoney(balance, cloud::MoneyStyle::Balance, lang);
        if (kind == QLatin1String("transcribe"))
            out() << QStringLiteral("This transcription cost %1. Balance: %2.").arg(sum, bal);
        else if (kind == QLatin1String("complex"))
            out() << plural(calls, "This summary cost %1 (%n part). Balance: %2.", "This summary cost %1 (%n parts). Balance: %2.").arg(sum, bal);
        else if (kind == QLatin1String("topics"))
            out() << QStringLiteral("Collecting the topics cost %1. Balance: %2.").arg(sum, bal);
        else
            out() << QStringLiteral("This summary cost %1. Balance: %2.").arg(sum, bal);
        out() << " (" << cloud::vatLabel(vatMode) << ")\n"; out().flush();
    });
    QObject::connect(&app, &app::AppController::cloudRefunded, &qapp, [&](QString, cloud::Money refund, cloud::Money, QString rid) {
        err() << QStringLiteral("The transcription failed on the provider side. We refunded the %1 charge.")
                   .arg(formatMoney(refund, cloud::MoneyStyle::Charge, lang)) << "\n"
            << QStringLiteral("Error ID: %1").arg(rid) << "\n";
        err().flush(); qapp.exit(3); });
    QObject::connect(&app, &app::AppController::cloudError, &qapp, [&](QString, QString, cloud::CloudError e, cloud::Money charged) {
        err() << describeCloudError(e, charged, lang) << "\n"; err().flush(); qapp.exit(2); });
    QObject::connect(&app, &app::AppController::transcriptReady, &qapp, [&](QString, QString p) {
        out() << QStringLiteral("Transcript done: %1").arg(p) << "\n"; out().flush(); qapp.quit(); });
    QObject::connect(&app, &app::AppController::summaryReady, &qapp, [&](QString, QString p) {
        out() << QStringLiteral("Summary done: %1").arg(p) << "\n"; out().flush(); qapp.quit(); });
    // Complex: pass 1 (topics) -> analysis of every topic + reduce, no editing.
    QObject::connect(&app, &app::AppController::topicsReady, &qapp, [&](QString mid, QVector<SummaryTopic> topics) {
        out() << plural(int(topics.size()), "%n topic — starting the analysis…", "%n topics — starting the analysis…") << "\n"; out().flush();
        app.generateComplexSummary(mid, topics); });
    QObject::connect(&app, &app::AppController::errorOccurred, &qapp, [&](QString e) {
        err() << QStringLiteral("ERROR: %1").arg(e) << "\n"; err().flush(); qapp.exit(1); });
    if (cmd == "transcribe") app.transcribeMeeting(id);
    else if (complex) app.extractMeetingTopics(id);
    else app.summarizeMeeting(id);
    return qapp.exec();
}

int cmd_identify(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    const QString id = args.value(2);
    if (id.isEmpty()) { err() << QStringLiteral("Usage: identify <meetingId>") << "\n"; return 1; }
    app.autoIdentifyMeeting(id);   // synchronous (ffmpeg + onnx)
    const Meeting m = app.store()->load(id);
    out() << QStringLiteral("Auto-identification done. Mapping (speakerMap):") << "\n";
    if (m.speakerMap.isEmpty())
        out() << QStringLiteral("  (empty — no match above threshold, or no model/voiceprint)") << "\n";
    for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
        out() << "  " << it.key() << " → " << app.people()->displayName(it.value())
            << "  [" << it.value().toString() << "]\n";
    out().flush();
    return 0;
}

int cmd_embedProbe(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    // Diagnostics: prints the embedding of one audio segment (for voiceprint debugging).
    //   embed-probe <modelPath> <audioPath> <startMs> <endMs> [scale] [cmn:0/1] [snip:0/1]
    const QString model = args.value(2), path = args.value(3);
    const qint64 s = args.value(4).toLongLong();
    const qint64 e = args.value(5).toLongLong();
    processing::EmbedderConfig cfg;
    if (args.size() > 6) cfg.waveScale = args.value(6).toFloat();
    if (args.size() > 7) cfg.subtractMean = args.value(7).toInt() != 0;
    if (args.size() > 8) cfg.snipEdges = args.value(8).toInt() != 0;
    processing::VoiceEmbedder emb(model, cfg);
    if (!emb.isValid()) { err() << QStringLiteral("ERROR: %1").arg(emb.lastError()) << "\n"; err().flush(); return 1; }
    const QVector<float> v = emb.embedFile(*app.mediaTool(), path, s, e);
    if (v.isEmpty()) { err() << QStringLiteral("ERROR: %1").arg(emb.lastError()) << "\n"; err().flush(); return 1; }
    QStringList parts; for (float x : v) parts << QString::number(x, 'g', 8);
    out() << parts.join(QLatin1Char(' ')) << "\n"; out().flush();
    return 0;
}


// ---- providers ---------------------------------------------------------------------------------

struct ProviderKind { const char* alias; providers::CapabilityKind kind; };
const ProviderKind kProviderKinds[] = {
    {"stt", providers::CapabilityKind::Transcription},
    {"llm", providers::CapabilityKind::Chat},
    {"embedding", providers::CapabilityKind::TextEmbedding},
};

const providers::CapabilityProviders& selectionOf(const app::AppSettings& s, providers::CapabilityKind kind)
{
    return kind == providers::CapabilityKind::Transcription ? s.providers.stt
         : kind == providers::CapabilityKind::TextEmbedding ? s.providers.embedding : s.providers.llm;
}

// The saved config of a provider with its API key from the secret store (the key is never printed).
providers::ProviderConfig runtimeConfigOf(app::AppController& app, const providers::ProviderDescriptor& d,
                                          const providers::CapabilityProviders& sel)
{
    providers::ProviderConfig cfg = sel.configs.value(d.id);
    if (cfg.type.isEmpty()) cfg.type = d.id;
    for (const providers::ConfigField& f : d.fields) {
        if (f.isSecret && f.type == providers::ConfigFieldType::Secret) cfg.apiKey = app.secret(f.secretKey).trimmed();
        else if (cfg.baseUrl.isEmpty() && f.key == QLatin1String("baseUrl")) cfg.baseUrl = f.defaultValue;
        else if (cfg.model.isEmpty() && f.key == QLatin1String("model")) cfg.model = f.defaultValue;
    }
    cfg.baseUrl = cfg.baseUrl.trimmed();
    cfg.model = cfg.model.trimmed();
    return cfg;
}

bool testable(const providers::ProviderDescriptor& d)
{
    return d.authMode != providers::AuthMode::Login && d.probe.isValid();
}

int providersUsage()
{
    err() << "Usage: providers list | providers test <id> [--kind stt|llm|embedding]\n";
    return 2;
}

int cmd_providers(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    const QString sub = args.value(2);
    const providers::ProviderRegistry& reg = app.providerRegistry();
    const app::AppSettings s = app.settings()->settings();
    if (sub == "list") {
        for (const ProviderKind& k : kProviderKinds) {
            const providers::CapabilityProviders& sel = selectionOf(s, k.kind);
            out() << k.alias << ":" << "\n";
            for (const providers::ProviderDescriptor& d : reg.descriptors(k.kind)) {
                const providers::ProviderConfig cfg = runtimeConfigOf(app, d, sel);
                QStringList notes;
                if (d.authMode == providers::AuthMode::ApiKey) notes << (cfg.apiKey.isEmpty() ? "no key" : "key set");
                if (d.authMode == providers::AuthMode::Login) notes << "login";
                if (!testable(d)) notes << "not testable";
                out() << "  " << (d.id == sel.selectedId ? "* " : "  ") << d.id.leftJustified(18) << " " << d.displayName;
                if (!cfg.baseUrl.isEmpty()) out() << "  " << cfg.baseUrl;
                if (!cfg.model.isEmpty()) out() << "  model: " << cfg.model;
                out() << "  [" << notes.join(QStringLiteral(", ")) << "]\n";
            }
        }
        out() << "(* = selected; keys are never printed)\n";
        out().flush();
        return 0;
    }
    if (sub == "test") {
        const QString id = args.value(3);
        if (id.isEmpty()) return providersUsage();
        QString wanted;
        for (int i = 4; i + 1 < args.size(); ++i)
            if (args[i] == "--kind") wanted = args[i + 1];
        const ProviderKind* found = nullptr;
        providers::ProviderDescriptor d;
        for (const ProviderKind& k : kProviderKinds) {
            if (!wanted.isEmpty() && wanted != QLatin1String(k.alias)) continue;
            if (!reg.has(k.kind, id)) continue;
            if (found) { err() << "Ambiguous provider id: " << id << " (use --kind stt|llm|embedding)\n"; return 2; }
            found = &k;
            d = reg.descriptor(k.kind, id);
        }
        if (!found) { err() << "No such provider: " << id << " (see: providers list)\n"; return 2; }
        if (!testable(d)) { err() << d.displayName << " has no connection test.\n"; return 2; }
        const providers::ProviderConfig cfg = runtimeConfigOf(app, d, selectionOf(s, found->kind));
        out() << "Testing " << d.displayName << " (" << cfg.baseUrl << ") ...\n";
        out().flush();
        // One GET of the provider's probe endpoint; nothing is started or paid (see ConnectionTester).
        providers::ConnectionTester tester;
        int rc = 1;
        QObject::connect(&tester, &providers::ConnectionTester::finished, &ctx.qapp(),
                         [&](int, const providers::ConnectionTestResult& r) {
            if (r.ok()) {
                out() << "OK";
                if (r.latencyMs >= 0) out() << "  " << r.latencyMs << " ms";
                if (!r.models.isEmpty()) out() << "  " << plural(r.models.size(), "%n model", "%n models");
                out() << "\n";
                if (!r.warning.isEmpty()) out() << "Warning: " << r.warning << "\n";
                if (!r.serverText.isEmpty()) out() << r.serverText << "\n";
                rc = 0;
            } else {
                err() << "FAILED: " << r.message << (r.code.isEmpty() ? QString() : "  [" + r.code + "]") << "\n";
                err().flush();
            }
            out().flush();
            ctx.qapp().quit();
        });
        tester.test(d, cfg);
        ctx.qapp().exec();
        return rc;
    }
    return providersUsage();
}

} // namespace

void registerProcessingCommands(CommandRegistry& r)
{
    const QString m = QStringLiteral("processing");
    // TODO(P12c): transcribe / summarize go through the processing services once those are on main.
    r.add({"transcribe", m, "Transcribe a meeting (needs a Soniox key or Tanara Cloud)",
           "transcribe <meetingId> [--yes]", true, cmd_transcribe});
    r.add({"summarize", m, "Summarize a meeting (LM Studio or Tanara Cloud)",
           "summarize <meetingId> [--yes] [--complex]", true, cmd_transcribe});
    // TODO(P12d): identify runs on the voice-analysis service.
    r.add({"identify", m, "Identify the speakers of a meeting from the voiceprints", "identify <meetingId>", true, cmd_identify});
    r.add({"voice-models", m, "List / enable / disable / fetch / verify / accept / delete the voice models (speaker + segmentation)",
           "voice-models [enable|disable|fetch <id> [--force] [--accept-mismatch]|verify [<id>]|accept <id>|delete <id>]", false,
           [](CliContext&, const QStringList& a) { return runVoiceModelsCommand(a); }});
    r.add({"voice-eval", m, "Compare the speaker models on a meeting folder (read-only)",
           "voice-eval <meeting-folder> [--models a,b] [--min-ms N] [--json]", false,
           [](CliContext&, const QStringList& a) { return runVoiceEvalCommand(a); }});
    r.add({"diarize-probe", m, "Run the speaker segmentation on one audio file and print the turns (read-only)",
           "diarize-probe <audio> [--model <id>] [--shift <ratio>] [--min-on <s>] [--min-off <s>] [--threads <n>] [--batch <n>] [--json] [--windows]",
           false, [](CliContext&, const QStringList& a) { return runDiarizeProbeCommand(a); }});
    r.add({"embed-probe", m, "Print the embedding of one audio segment (voiceprint debugging)",
           "embed-probe <modelPath> <audioPath> <startMs> <endMs> [scale] [cmn:0/1] [snip:0/1]", true, cmd_embedProbe});
    r.add({"providers", m, "List the providers; test a provider's connection (one GET, nothing paid)",
           "providers list\nproviders test <id> [--kind stt|llm|embedding]", true, cmd_providers});
}

} // namespace tanara::cli

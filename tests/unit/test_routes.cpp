// S3: the per-capability processing route (providers/core/Routes.h): the enum keys, the JSON round trip,
// the provider kinds, the set rules, the effective provider of a route, and the conversion of settings
// written before S3 (SettingsManager, a sandbox config folder only).
#include "tanara/app/SettingsManager.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/core/ReadinessModel.h"
#include "tanara/providers/core/Routes.h"
#include "tanara/providers/embedding/ITextEmbedder.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace tanara;
using providers::Capability;
using providers::Route;

namespace {

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeJson(const QString& path, const QJsonObject& o)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(o).toJson());
}

QJsonObject capProviders(const QString& selected, const QStringList& ids)
{
    QJsonObject cfgs;
    for (const QString& id : ids) cfgs.insert(id, QJsonObject{{QStringLiteral("type"), id}});
    return QJsonObject{{QStringLiteral("selected"), selected}, {QStringLiteral("configs"), cfgs}};
}

providers::RouteContext sttContext(bool cloudLive)
{
    providers::RouteContext ctx;
    ctx.localIds = {QStringLiteral("whisper-compat")};
    ctx.externalIds = {QStringLiteral("soniox")};
    ctx.cloudId = providers::CloudProviderId;
    ctx.cloudLive = cloudLive;
    return ctx;
}

} // namespace

class RoutesTest : public QObject {
    Q_OBJECT
private slots:
    void init() { qunsetenv("TANARA_CLOUD"); }

    void keysRoundTrip()
    {
        for (Capability c : providers::allCapabilities())
            QCOMPARE(providers::capabilityFromKey(providers::capabilityKey(c)), std::optional<Capability>(c));
        for (Route r : {Route::Local, Route::External, Route::Cloud, Route::Ask, Route::Basic})
            QCOMPARE(providers::routeFromKey(providers::routeKey(r)), std::optional<Route>(r));
        QVERIFY(!providers::capabilityFromKey(QStringLiteral("tts")));
        QVERIFY(!providers::routeFromKey(QStringLiteral("somewhere")));
        QCOMPARE(providers::capabilityKey(Capability::Diarization), QStringLiteral("diarization"));
        QCOMPARE(providers::routeKey(Route::Ask), QStringLiteral("ask"));
    }

    void jsonRoundTrip()
    {
        providers::RouteSettings r;
        r.stt = {Route::Ask, QStringLiteral("whisper-compat"), QStringLiteral("soniox")};
        r.diarization.route = Route::Local;
        r.llm = {Route::Cloud, QStringLiteral("openai-compat"), QString()};
        r.embedding = {Route::Basic, providers::embeddingproviders::LocalId, QString()};
        const QJsonObject o = r.toJson();
        // The shape: { "stt": { "route": "ask", "local": "whisper-compat", "external": "soniox" }, ... }
        QCOMPARE(o.value("stt").toObject().value("route").toString(), QStringLiteral("ask"));
        QCOMPARE(o.value("stt").toObject().value("local").toString(), QStringLiteral("whisper-compat"));
        QCOMPARE(o.value("stt").toObject().value("external").toString(), QStringLiteral("soniox"));
        QCOMPARE(o.value("voice").toObject(), QJsonObject({{"route", "local"}}));
        QCOMPARE(o.keys().size(), 5);
        const providers::RouteSettings back = providers::RouteSettings::fromJson(o);
        QVERIFY(back.present);
        for (Capability c : providers::allCapabilities()) QVERIFY(back.at(c) == r.at(c));
        // A missing section: the defaults, flagged for the conversion.
        const providers::RouteSettings none = providers::RouteSettings::fromJson({});
        QVERIFY(!none.present);
        // An unknown route value: the capability's default.
        const providers::RouteSettings odd = providers::RouteSettings::fromJson(
            QJsonObject{{"diarization", QJsonObject{{"route", "valahol"}}}});
        QCOMPARE(odd.diarization.route, Route::External);
    }

    void builtinKindsMatchTheIds()
    {
        providers::ProviderRegistry reg;
        providers::registerBuiltinProviders(reg);
        providers::registerCloudProviders(reg);
        int n = 0;
        for (providers::CapabilityKind k : {providers::CapabilityKind::Transcription, providers::CapabilityKind::Chat,
                                            providers::CapabilityKind::TextEmbedding})
            for (const providers::ProviderDescriptor& d : reg.descriptors(k)) {
                QCOMPARE(d.kind, providers::providerKindOf(d.id));
                ++n;
            }
        QVERIFY(n >= 7);
        QCOMPARE(providers::providerKindOf(QStringLiteral("soniox")), providers::ProviderKind::External);
        QCOMPARE(providers::providerKindOf(QStringLiteral("whisper-compat")), providers::ProviderKind::Local);
        QCOMPARE(providers::providerKindOf(QStringLiteral("openai-compat")), providers::ProviderKind::Local);
        QCOMPARE(providers::providerKindOf(cloud::ProviderId), providers::ProviderKind::Cloud);
        QCOMPARE(providers::providerKindOf(providers::embeddingproviders::CloudId), providers::ProviderKind::Cloud);
    }

    void setRules()
    {
        providers::ProviderSettings p;
        p.stt.selectedId = QStringLiteral("soniox");
        providers::RouteSettings r;
        r.normalize(p);
        QCOMPARE(r.stt.route, Route::External);
        QCOMPARE(r.stt.lastExternalId, QStringLiteral("soniox"));

        // Without the live cloud: no Cloud, no Ask; nothing changes.
        QVERIFY(!r.set(Capability::Stt, Route::Cloud, sttContext(false), p));
        QVERIFY(!r.set(Capability::Stt, Route::Ask, sttContext(false), p));
        QCOMPARE(providers::routeBlocker(Capability::Stt, Route::Ask, sttContext(false)), QStringLiteral("ask_unavailable"));
        QCOMPARE(r.stt.route, Route::External);
        // Ask needs a non-cloud provider too.
        providers::RouteContext cloudOnly = sttContext(true);
        cloudOnly.localIds.clear();
        cloudOnly.externalIds.clear();
        QCOMPARE(providers::routeBlocker(Capability::Stt, Route::Ask, cloudOnly), QStringLiteral("ask_unavailable"));

        // Local selects the first local provider; External restores Soniox.
        QVERIFY(r.set(Capability::Stt, Route::Local, sttContext(false), p));
        QCOMPARE(p.stt.selectedId, QStringLiteral("whisper-compat"));
        QVERIFY(r.set(Capability::Stt, Route::External, sttContext(false), p));
        QCOMPARE(p.stt.selectedId, QStringLiteral("soniox"));
        QCOMPARE(r.stt.lastLocalId, QStringLiteral("whisper-compat"));

        // Cloud and Ask keep the own provider selected; the effective provider follows the resolved route.
        QVERIFY(r.set(Capability::Stt, Route::Cloud, sttContext(true), p));
        QCOMPARE(p.stt.selectedId, QStringLiteral("soniox"));
        QCOMPARE(r.providerFor(Capability::Stt, Route::Cloud, p), providers::CloudProviderId);
        QCOMPARE(r.providerFor(Capability::Stt, Route::Local, p), QStringLiteral("whisper-compat"));
        QCOMPARE(r.providerFor(Capability::Stt, Route::External, p), QStringLiteral("soniox"));
        QCOMPARE(r.effective(p, Route::Cloud, Route::Local).stt.selectedId, providers::CloudProviderId);
        QVERIFY(r.set(Capability::Stt, Route::Ask, sttContext(true), p));
        QCOMPARE(r.fallbackRoute(Capability::Stt, p), Route::External);

        // Voice: local only; embedding: no Ask, Basic clears the provider; diarization: no Cloud yet.
        QCOMPARE(providers::routeBlocker(Capability::Voice, Route::Cloud, sttContext(true)), QStringLiteral("voice_local_only"));
        QVERIFY(!r.set(Capability::Voice, Route::External, sttContext(true), p));
        QCOMPARE(providers::routeBlocker(Capability::Embedding, Route::Ask, sttContext(true)), QStringLiteral("embedding_no_ask"));
        QCOMPARE(providers::routeBlocker(Capability::Stt, Route::Basic, sttContext(true)), QStringLiteral("not_embedding"));
        QCOMPARE(providers::routeBlocker(Capability::Diarization, Route::Cloud, sttContext(true)), QStringLiteral("diarization_later"));
        QVERIFY(r.set(Capability::Diarization, Route::Local, sttContext(false), p));
        QCOMPARE(r.diarization.route, Route::Local);
        providers::RouteContext emb;
        emb.localIds = {providers::embeddingproviders::LocalId};
        emb.cloudId = providers::embeddingproviders::CloudId;
        QVERIFY(r.set(Capability::Embedding, Route::Local, emb, p));
        QCOMPARE(p.embedding.selectedId, providers::embeddingproviders::LocalId);
        QVERIFY(r.set(Capability::Embedding, Route::Basic, emb, p));
        QVERIFY(p.embedding.selectedId.isEmpty());
        QCOMPARE(r.embedding.lastLocalId, providers::embeddingproviders::LocalId);
    }

    void normalizeFollowsTheProviders()
    {
        providers::ProviderSettings p;
        p.stt.selectedId = QStringLiteral("whisper-compat");
        p.llm.selectedId = cloud::ProviderId;   // the pre-S3 cloud choice
        p.llm.configs.insert(QStringLiteral("openai-compat"), {});
        p.llm.configs.insert(cloud::ProviderId, {});
        p.embedding.selectedId = providers::embeddingproviders::CloudId;
        providers::RouteSettings r;
        r.stt.route = Route::External;
        r.llm.route = Route::Ask;
        r.voice.route = Route::Cloud;
        r.diarization.route = Route::Ask;
        r.normalize(p);
        QCOMPARE(r.stt.route, Route::Local);              // the selected provider's kind
        QCOMPARE(r.llm.route, Route::Cloud);              // a selected cloud id = Cloud
        QCOMPARE(p.llm.selectedId, QStringLiteral("openai-compat"));
        QCOMPARE(r.embedding.route, Route::Cloud);
        QCOMPARE(r.voice.route, Route::Local);
        QCOMPARE(r.diarization.route, Route::External);
    }

    // The readiness of a Cloud route checks the Tanara Cloud sign-in, not the own provider's key.
    void readinessFollowsTheEffectiveProvider()
    {
        providers::ProviderRegistry reg;
        providers::registerBuiltinProviders(reg);
        providers::registerCloudProviders(reg);
        providers::ProviderSettings p;
        p.stt.selectedId = QStringLiteral("soniox");
        providers::RouteSettings r;
        r.normalize(p);
        Meeting m;
        Track t;
        t.active = true;
        m.tracks << t;
        auto noSecrets = [](const QString&) { return false; };
        const providers::ReadinessResult own =
            providers::ReadinessModel(r.effective(p, Route::External, Route::Local), noSecrets, &reg)
                .check(providers::WorkflowStep::Transcribe, m);
        QCOMPARE(own.blockerKind, providers::BlockerKind::ProviderConfig);   // the Soniox key
        const providers::ReadinessResult cloudRun =
            providers::ReadinessModel(r.effective(p, Route::Cloud, Route::Local), noSecrets, &reg)
                .check(providers::WorkflowStep::Transcribe, m);
        QCOMPARE(cloudRun.blockerKind, providers::BlockerKind::Auth);
        QCOMPARE(cloudRun.fixActionHint, QStringLiteral("login:tanara-cloud"));
    }

    // settings.json written before S3: no "routes" section -> converted on load, written back on save.
    void convertsSettingsWrittenBeforeRoutes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("settings.json"));
        QJsonObject root{{"version", 2}};
        root["providers"] = QJsonObject{
            {"stt", capProviders(cloud::ProviderId, {QStringLiteral("soniox"), cloud::ProviderId})},
            {"llm", capProviders(QStringLiteral("openai-compat"), {QStringLiteral("openai-compat")})},
            {"embedding", capProviders(QString(), {providers::embeddingproviders::LocalId})}};
        root["processing"] = QJsonObject{{"localDiarization", false}, {"summaryLanguage", "magyar"}};
        writeJson(path, root);
        {
            app::SettingsManager mgr(dir.path());
            const app::AppSettings s = mgr.settings();
            QCOMPARE(s.routes.stt.route, Route::Cloud);
            QCOMPARE(s.providers.stt.selectedId, QStringLiteral("soniox"));
            QCOMPARE(s.routes.llm.route, Route::Local);
            QCOMPARE(s.routes.diarization.route, Route::External);
            QCOMPARE(s.routes.voice.route, Route::Local);
            QCOMPARE(s.routes.embedding.route, Route::Basic);
            mgr.save();
        }
        const QJsonObject saved = readJson(path);
        QCOMPARE(saved.value("routes").toObject().value("stt").toObject().value("route").toString(), QStringLiteral("cloud"));
        QVERIFY(!saved.value("processing").toObject().contains("localDiarization"));
        QCOMPARE(saved.value("processing").toObject().value("summaryLanguage").toString(), QStringLiteral("magyar"));

        // The other branch: the switch on (or missing: its old default), Whisper, the local embedding endpoint.
        root["providers"] = QJsonObject{
            {"stt", capProviders(QStringLiteral("whisper-compat"), {QStringLiteral("whisper-compat")})},
            {"llm", capProviders(cloud::ProviderId, {cloud::ProviderId})},
            {"embedding", capProviders(providers::embeddingproviders::LocalId, {providers::embeddingproviders::LocalId})}};
        root["processing"] = QJsonObject{};
        writeJson(path, root);
        {
            app::SettingsManager mgr(dir.path());
            const app::AppSettings s = mgr.settings();
            QCOMPARE(s.routes.stt.route, Route::Local);
            QCOMPARE(s.routes.diarization.route, Route::Local);
            QCOMPARE(s.routes.embedding.route, Route::Local);
            QCOMPARE(s.routes.llm.route, Route::Cloud);
            QCOMPARE(s.providers.llm.selectedId, QStringLiteral("openai-compat"));   // the built-in own default
            QVERIFY(s.providers.llm.configs.contains(QStringLiteral("openai-compat")));
        }

        // A stored routes section is taken as it is.
        root["routes"] = QJsonObject{{"stt", QJsonObject{{"route", "ask"}}}};
        root["providers"] = QJsonObject{
            {"stt", capProviders(QStringLiteral("soniox"), {QStringLiteral("soniox")})},
            {"llm", capProviders(QStringLiteral("openai-compat"), {QStringLiteral("openai-compat")})}};
        writeJson(path, root);
        app::SettingsManager mgr(dir.path());
        QCOMPARE(mgr.settings().routes.stt.route, Route::Ask);
    }

    void freshInstallDerivesFromWhatIsConfigured()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            app::SettingsManager mgr(dir.path());
            const app::AppSettings s = mgr.settings();
            QCOMPARE(s.routes.stt.route, Route::External);   // the Soniox default
            QCOMPARE(s.routes.llm.route, Route::Local);      // LM Studio
            QCOMPARE(s.routes.diarization.route, Route::External);
            QCOMPARE(s.routes.voice.route, Route::Local);
            QCOMPARE(s.routes.embedding.route, Route::Basic);
        }
        if (!cloud::clientCompiled()) return;
        // With the live cloud an own provider and the cloud are both there: Ask (paid runs ask first).
        QTemporaryDir live;
        qputenv("TANARA_CLOUD", "live");
        app::SettingsManager mgr(live.path());
        qunsetenv("TANARA_CLOUD");
        QCOMPARE(mgr.settings().routes.stt.route, Route::Ask);
        QCOMPARE(mgr.settings().routes.llm.route, Route::Ask);
        QCOMPARE(mgr.settings().routes.embedding.route, Route::Basic);
    }
};

QTEST_GUILESS_MAIN(RoutesTest)
#include "test_routes.moc"

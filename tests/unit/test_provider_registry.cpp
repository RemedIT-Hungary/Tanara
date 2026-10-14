// Tanara: unit tests of THE provider registry (providers/core/ProviderRegistry.h).
//
// One registry holds every capability, keyed by (CapabilityKind, id); a provider is added with a
// typed factory and created by id with a ProviderContext + ProviderConfig. Every test builds its OWN
// registry (it is not a singleton); defaultInstance() is the transitional exception (P15 removes it).
//
// IMPORTANT: the tests/unit/*.cpp files compile into SEPARATE test exes (see tests/CMakeLists.txt
// GLOB), each bringing its OWN main. Core (tanara_core) does NOT link Qt Widgets, hence
// QTEST_GUILESS_MAIN here (no QApplication).
#include <QtTest>
#include <QObject>
#include <QPointer>

#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderContext.h"
#include "tanara/providers/core/ProviderDescriptor.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/stt/ISttProvider.h"
#include "tanara/providers/chat/IChatProvider.h"
#include "tanara/providers/chat/ILocalModelServer.h"
#include "tanara/providers/embedding/ITextEmbedder.h"

using namespace tanara;
using providers::CapabilityKind;

namespace {

// A fake chat provider: records the context and config it was created with.
class FakeChat : public QObject, public providers::IChatProvider {
public:
    FakeChat(const providers::ProviderContext& ctx, const providers::ProviderConfig& cfg, QObject* parent)
        : QObject(parent), ctx(ctx), cfg(cfg) {}
    QString name() const override { return QStringLiteral("fake-chat"); }
    bool supportsStreaming() const override { return false; }
    providers::LlmJob* chat(const providers::LlmRequest&) override { return nullptr; }
    providers::ProviderContext ctx;
    providers::ProviderConfig cfg;
};

class FakeStt : public QObject, public providers::ISttProvider {
public:
    using QObject::QObject;
    QString name() const override { return QStringLiteral("fake-stt"); }
    bool supportsLive() const override { return false; }
    providers::SttJob* transcribe(const providers::SttRequest&) override { return nullptr; }
};

providers::ProviderDescriptor desc(const QString& id)
{
    providers::ProviderDescriptor d;
    d.id = id;
    d.displayName = id;
    return d;
}

bool hasSecret(const providers::ProviderDescriptor& d, const QString& key)
{
    for (const providers::ConfigField& f : d.fields)
        if (f.isSecret && f.secretKey == key) return true;
    return false;
}

} // namespace

class ProviderRegistryTests : public QObject {
    Q_OBJECT
private slots:

    // Several capabilities in one registry; the same id under two capabilities are two entries.
    void multipleCapabilities()
    {
        providers::ProviderRegistry reg;
        reg.add<providers::ISttProvider>(desc(QStringLiteral("x")),
            [](const providers::ProviderContext&, const providers::ProviderConfig&, QObject* p) -> providers::ISttProvider* {
                return new FakeStt(p);
            });
        reg.add<providers::IChatProvider>(desc(QStringLiteral("x")),
            [](const providers::ProviderContext& c, const providers::ProviderConfig& cfg, QObject* p) -> providers::IChatProvider* {
                return new FakeChat(c, cfg, p);
            });
        QVERIFY(reg.has(CapabilityKind::Transcription, QStringLiteral("x")));
        QVERIFY(reg.has(CapabilityKind::Chat, QStringLiteral("x")));
        QVERIFY(!reg.has(CapabilityKind::TextEmbedding, QStringLiteral("x")));
        QCOMPARE(reg.descriptors(CapabilityKind::Transcription).size(), 1);
        QCOMPARE(reg.descriptors(CapabilityKind::Chat).size(), 1);
        // add<Port>() sets the capability from the port.
        QCOMPARE(reg.descriptor(CapabilityKind::Chat, QStringLiteral("x")).capability, CapabilityKind::Chat);
        QCOMPARE(reg.descriptor(CapabilityKind::Transcription, QStringLiteral("x")).capability,
                 CapabilityKind::Transcription);
    }

    // create<Port>() reaches only the factory of its own capability, and hands over context + config.
    void typedCreate()
    {
        providers::ProviderRegistry reg;
        reg.add<providers::IChatProvider>(desc(QStringLiteral("chat")),
            [](const providers::ProviderContext& c, const providers::ProviderConfig& cfg, QObject* p) -> providers::IChatProvider* {
                return new FakeChat(c, cfg, p);
            });
        QObject parent;
        providers::ProviderContext ctx;
        ctx.jobId = QStringLiteral("job-1");
        ctx.auth = providers::AuthSource::fixed(QStringLiteral("tok"));
        providers::ProviderConfig cfg;
        cfg.model = QStringLiteral("m1");

        providers::IChatProvider* chat = reg.create<providers::IChatProvider>(QStringLiteral("chat"), ctx, cfg, &parent);
        QVERIFY(chat);
        auto* fake = dynamic_cast<FakeChat*>(chat);
        QVERIFY(fake);
        QCOMPARE(fake->parent(), &parent);
        QCOMPARE(fake->ctx.jobId, QStringLiteral("job-1"));
        QCOMPARE(fake->ctx.bearer(cfg), QStringLiteral("tok"));
        QCOMPARE(fake->cfg.model, QStringLiteral("m1"));
        // The same id through another port: nothing (type safety by capability).
        QVERIFY(!reg.create<providers::ISttProvider>(QStringLiteral("chat"), ctx, cfg, &parent));
        QVERIFY(!reg.create<providers::ITextEmbedder>(QStringLiteral("chat"), ctx, cfg, &parent));
    }

    void unknownIdReturnsNull()
    {
        providers::ProviderRegistry reg;
        providers::registerBuiltinProviders(reg);
        QObject parent;
        QVERIFY(!reg.create<providers::ISttProvider>(QStringLiteral("nonexistent"), {}, {}, &parent));
        QVERIFY(reg.descriptor(CapabilityKind::Transcription, QStringLiteral("nonexistent")).id.isEmpty());
    }

    // Adding the same key again replaces it (no duplicate); the listing keeps the registration order.
    void addReplacesAndKeepsOrder()
    {
        providers::ProviderRegistry reg;
        auto none = providers::ProviderRegistry::Factory<providers::ISttProvider>();
        reg.add<providers::ISttProvider>(desc(QStringLiteral("b")), none);
        reg.add<providers::ISttProvider>(desc(QStringLiteral("a")), none);
        providers::ProviderDescriptor b2 = desc(QStringLiteral("b"));
        b2.displayName = QStringLiteral("B2");
        reg.add<providers::ISttProvider>(b2, none);
        const auto list = reg.descriptors(CapabilityKind::Transcription);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).id, QStringLiteral("b"));
        QCOMPARE(list.at(0).displayName, QStringLiteral("B2"));
        QCOMPARE(list.at(1).id, QStringLiteral("a"));
        // An empty factory: known, but nothing to create.
        QVERIFY(reg.has(CapabilityKind::Transcription, QStringLiteral("a")));
        QVERIFY(!reg.create<providers::ISttProvider>(QStringLiteral("a"), {}, {}, nullptr));
        reg.remove(CapabilityKind::Transcription, QStringLiteral("a"));
        QCOMPARE(reg.descriptors(CapabilityKind::Transcription).size(), 1);
    }

    // Separate instances do not share anything.
    void ownInstances()
    {
        providers::ProviderRegistry a, b;
        providers::registerBuiltinProviders(a);
        QVERIFY(a.has(CapabilityKind::Transcription, QStringLiteral("soniox")));
        QVERIFY(!b.has(CapabilityKind::Transcription, QStringLiteral("soniox")));
        providers::registerCloudProviders(b);
        QVERIFY(b.has(CapabilityKind::Chat, QStringLiteral("tanara-cloud")));
        QVERIFY(!a.has(CapabilityKind::Chat, QStringLiteral("tanara-cloud")));
        // The default instance has the built-ins on first use.
        QVERIFY(providers::ProviderRegistry::defaultInstance().has(CapabilityKind::Chat, QStringLiteral("openai-compat")));
    }

    // The built-ins: Soniox + Whisper STT, OpenAI-compatible chat + its LM Studio server handler,
    // the two text-embedding descriptors; the factories give working, correctly named instances.
    void builtins()
    {
        providers::ProviderRegistry reg;
        providers::registerBuiltinProviders(reg);
        QObject parent;
        providers::ProviderConfig cfg;

        providers::ISttProvider* soniox = reg.create<providers::ISttProvider>(QStringLiteral("soniox"), {}, cfg, &parent);
        QVERIFY(soniox);
        QCOMPARE(soniox->name(), QStringLiteral("soniox"));
        providers::ISttProvider* whisper = reg.create<providers::ISttProvider>(QStringLiteral("whisper-compat"), {}, cfg, &parent);
        QVERIFY(whisper);
        QCOMPARE(whisper->name(), QStringLiteral("whisper-compat"));
        QVERIFY(reg.create<providers::IChatProvider>(QStringLiteral("openai-compat"), {}, cfg, &parent));
        QVERIFY(reg.create<providers::ITextEmbedder>(providers::embeddingproviders::LocalId, {}, cfg, &parent));
        QVERIFY(reg.create<providers::ITextEmbedder>(providers::embeddingproviders::CloudId, {}, cfg, &parent));
        // The optional local-server handler exists for the own chat endpoint only.
        QPointer<providers::ILocalModelServer> server =
            reg.create<providers::ILocalModelServer>(QStringLiteral("openai-compat"), {}, cfg, &parent);
        QVERIFY(server);
        providers::registerCloudProviders(reg);
        QVERIFY(!reg.create<providers::ILocalModelServer>(QStringLiteral("tanara-cloud"), {}, cfg, &parent));
    }

    // The secret fields (back-compat: the SecretStore keys must not change).
    void builtinSecrets()
    {
        providers::ProviderRegistry reg;
        providers::registerBuiltinProviders(reg);
        QVERIFY(hasSecret(reg.descriptor(CapabilityKind::Transcription, QStringLiteral("soniox")),
                          QStringLiteral("soniox.apiKey")));
        QVERIFY(hasSecret(reg.descriptor(CapabilityKind::Chat, QStringLiteral("openai-compat")),
                          QStringLiteral("llm.apiKey")));
        QVERIFY(hasSecret(reg.descriptor(CapabilityKind::Transcription, QStringLiteral("whisper-compat")),
                          QStringLiteral("stt.whisper.apiKey")));
    }

    // Origin: Cloud = only the Tanara Cloud; own key / address = CustomEndpoint.
    void builtinOriginsAndTraits()
    {
        providers::ProviderRegistry reg;
        providers::registerBuiltinProviders(reg);
        providers::registerCloudProviders(reg);
        const auto soniox = reg.descriptor(CapabilityKind::Transcription, QStringLiteral("soniox"));
        QCOMPARE(soniox.origin, providers::Origin::CustomEndpoint);
        QVERIFY(soniox.traits.dataLeavesDevice);
        QVERIFY(soniox.traits.metered);
        const auto llm = reg.descriptor(CapabilityKind::Chat, QStringLiteral("openai-compat"));
        QCOMPARE(llm.origin, providers::Origin::CustomEndpoint);
        QVERIFY(!llm.traits.dataLeavesDevice);   // LM Studio on localhost by default
        QVERIFY(llm.traits.worksOffline);
        const auto cloudStt = reg.descriptor(CapabilityKind::Transcription, QStringLiteral("tanara-cloud"));
        QCOMPARE(cloudStt.origin, providers::Origin::Cloud);
        QCOMPARE(cloudStt.authMode, providers::AuthMode::Login);
        QCOMPARE(reg.descriptor(CapabilityKind::TextEmbedding, providers::embeddingproviders::CloudId).origin,
                 providers::Origin::Cloud);
        QCOMPARE(providers::capabilityName(CapabilityKind::TextEmbedding), QStringLiteral("text-embedding"));
    }

    // The cancel token: copies share it; a listener added after cancel runs at once.
    void cancelToken()
    {
        providers::ProviderContext ctx;
        int calls = 0;
        ctx.cancel.onCancel([&calls] { ++calls; });
        const providers::ProviderContext copy = ctx;
        QVERIFY(!copy.cancel.isCancelled());
        copy.cancel.cancel();
        QVERIFY(ctx.cancel.isCancelled());
        QCOMPARE(calls, 1);
        ctx.cancel.cancel();
        QCOMPARE(calls, 1);
        ctx.cancel.onCancel([&calls] { ++calls; });
        QCOMPARE(calls, 2);
    }
};

QTEST_GUILESS_MAIN(ProviderRegistryTests)
#include "test_provider_registry.moc"

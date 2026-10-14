// Tanara: unit tests of the provider registry.
//
// The registry resolves and creates an STT/LLM provider by id; there is no hard-coded
// `new SonioxProvider`. Here we check the registration of the built-in providers (Soniox STT +
// OpenAI-compatible LLM) and the create/descriptor/has contract.
//
// IMPORTANT: the tests/unit/*.cpp files compile into SEPARATE test exes (see tests/CMakeLists.txt
// GLOB), each bringing its OWN main. Core (tanara_core) does NOT link Qt Widgets, hence
// QTEST_GUILESS_MAIN here (no QApplication).
#include <QtTest>
#include "tanara/providers/core/ProviderConfig.h"
#include <QObject>

#include "tanara/domain/Types.h"
#include "tanara/providers/core/ProviderDescriptor.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/stt/ISttProvider.h"
#include "tanara/providers/chat/ILlmProvider.h"

using namespace tanara;

class ProviderRegistryTests : public QObject {
    Q_OBJECT
private slots:

    // registerBuiltinProviders() registers the Soniox STT provider, and create() gives a working,
    // correctly named instance, parented to the given QObject.
    void builtins_registerSonioxStt()
    {
        providers::registerBuiltinProviders();

        auto& reg = providers::SttProviderRegistry::instance();
        QVERIFY(reg.has(QStringLiteral("soniox")));

        QObject parent;
        providers::ProviderConfig cfg;
        cfg.type = QStringLiteral("soniox");

        providers::ISttProvider* p = reg.create(QStringLiteral("soniox"), cfg, &parent);
        QVERIFY(p != nullptr);
        QCOMPARE(p->name(), QStringLiteral("soniox"));
    }

    // Soniox's descriptor declares a secret field with the "soniox.apiKey" KeyStore key.
    void builtins_sonioxDescriptorHasApiKeySecret()
    {
        providers::registerBuiltinProviders();

        const providers::ProviderDescriptor desc =
            providers::SttProviderRegistry::instance().descriptor(QStringLiteral("soniox"));

        bool foundSecret = false;
        for (const providers::ConfigField& f : desc.fields) {
            if (f.isSecret && f.secretKey == QStringLiteral("soniox.apiKey")) {
                foundSecret = true;
                break;
            }
        }
        QVERIFY(foundSecret);
    }

    // For an unknown id create() returns nullptr (does not throw or crash).
    void create_unknownIdReturnsNull()
    {
        providers::registerBuiltinProviders();

        QObject parent;
        providers::ProviderConfig cfg;

        providers::ISttProvider* p =
            providers::SttProviderRegistry::instance().create(QStringLiteral("nonexistent"), cfg, &parent);
        QVERIFY(p == nullptr);
    }

    // registerBuiltinProviders() registers the OpenAI-compatible LLM provider, and create() gives
    // a non-null instance, parented to the given QObject.
    void builtins_registerOpenAiCompatLlm()
    {
        providers::registerBuiltinProviders();

        auto& reg = providers::LlmProviderRegistry::instance();
        QVERIFY(reg.has(QStringLiteral("openai-compat")));

        QObject parent;
        providers::ProviderConfig cfg;
        cfg.type = QStringLiteral("openai-compat");

        providers::ILlmProvider* p = reg.create(QStringLiteral("openai-compat"), cfg, &parent);
        QVERIFY(p != nullptr);
    }

    // Back-compat CRITICAL: the OpenAI-compatible descriptor's secret field uses the "llm.apiKey"
    // KeyStore key (it matches keys::LlmApiKey, so an existing secrets.json keeps working
    // unchanged). See the Soniox symmetry above.
    void builtins_openAiCompatDescriptorHasApiKeySecret()
    {
        providers::registerBuiltinProviders();

        const providers::ProviderDescriptor desc =
            providers::LlmProviderRegistry::instance().descriptor(QStringLiteral("openai-compat"));

        bool foundSecret = false;
        for (const providers::ConfigField& f : desc.fields) {
            if (f.isSecret && f.secretKey == QStringLiteral("llm.apiKey")) {
                foundSecret = true;
                break;
            }
        }
        QVERIFY(foundSecret);
    }
};

QTEST_GUILESS_MAIN(ProviderRegistryTests)
#include "test_provider_registry.moc"

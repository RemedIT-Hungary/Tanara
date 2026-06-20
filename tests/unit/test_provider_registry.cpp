//
// Tanara — Provider-registry unit-tesztek.
//
// A registry id alapján old fel és hoz létre STT/LLM providert; nincs hardkódolt
// `new SonioxProvider`. Itt a beépített providerek regisztrációját (Soniox STT +
// OpenAI-kompatibilis LLM) és a create/descriptor/has szerződést ellenőrizzük.
//
// FONTOS: a tests/unit/*.cpp fájlok KÜLÖN teszt-exe-be fordulnak (lásd
// tests/CMakeLists.txt GLOB), mindegyik hozza a SAJÁT main-jét. A core (tanara_core)
// NEM linkel Qt Widgetset, ezért itt QTEST_GUILESS_MAIN (nincs QApplication).
//
#include <QtTest>
#include <QObject>

#include "tanara/Types.h"
#include "tanara/provider/ProviderDescriptor.h"
#include "tanara/provider/ProviderRegistry.h"
#include "tanara/stt/ISttProvider.h"
#include "tanara/llm/ILlmProvider.h"

using namespace tanara;

class ProviderRegistryTests : public QObject {
    Q_OBJECT
private slots:

    // registerBuiltinProviders() bejegyzi a Soniox STT providert, és a create()
    // egy működő, helyesen elnevezett példányt ad, parentelve a megadott QObject-hez.
    void builtins_registerSonioxStt()
    {
        registerBuiltinProviders();

        auto& reg = SttProviderRegistry::instance();
        QVERIFY(reg.has(QStringLiteral("soniox")));

        QObject parent;
        ProviderConfig cfg;
        cfg.type = QStringLiteral("soniox");

        ISttProvider* p = reg.create(QStringLiteral("soniox"), cfg, &parent);
        QVERIFY(p != nullptr);
        QCOMPARE(p->name(), QStringLiteral("soniox"));
    }

    // A Soniox descriptora deklarál egy titkos mezőt a "soniox.apiKey" KeyStore-kulccsal.
    void builtins_sonioxDescriptorHasApiKeySecret()
    {
        registerBuiltinProviders();

        const ProviderDescriptor desc =
            SttProviderRegistry::instance().descriptor(QStringLiteral("soniox"));

        bool foundSecret = false;
        for (const ConfigField& f : desc.fields) {
            if (f.isSecret && f.secretKey == QStringLiteral("soniox.apiKey")) {
                foundSecret = true;
                break;
            }
        }
        QVERIFY(foundSecret);
    }

    // Ismeretlen id-ra a create() nullptr-t ad (nem dob, nem crashel).
    void create_unknownIdReturnsNull()
    {
        registerBuiltinProviders();

        QObject parent;
        ProviderConfig cfg;

        ISttProvider* p =
            SttProviderRegistry::instance().create(QStringLiteral("nonexistent"), cfg, &parent);
        QVERIFY(p == nullptr);
    }

    // registerBuiltinProviders() bejegyzi az OpenAI-kompatibilis LLM providert,
    // és a create() egy nem-null példányt ad, parentelve a megadott QObject-hez.
    void builtins_registerOpenAiCompatLlm()
    {
        registerBuiltinProviders();

        auto& reg = LlmProviderRegistry::instance();
        QVERIFY(reg.has(QStringLiteral("openai-compat")));

        QObject parent;
        ProviderConfig cfg;
        cfg.type = QStringLiteral("openai-compat");

        ILlmProvider* p = reg.create(QStringLiteral("openai-compat"), cfg, &parent);
        QVERIFY(p != nullptr);
    }

    // Back-compat KRITIKUS: az OpenAI-kompatibilis descriptor titkos mezője a
    // "llm.apiKey" KeyStore-kulcsot használja (egyezik a keys::LlmApiKey-vel, így a
    // meglévő secrets.json változatlanul működik). Lásd a Soniox-szimmetriát fent.
    void builtins_openAiCompatDescriptorHasApiKeySecret()
    {
        registerBuiltinProviders();

        const ProviderDescriptor desc =
            LlmProviderRegistry::instance().descriptor(QStringLiteral("openai-compat"));

        bool foundSecret = false;
        for (const ConfigField& f : desc.fields) {
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

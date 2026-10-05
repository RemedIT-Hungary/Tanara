// SettingsEmbeddingModel — a Beállítások „Beágyazás” kártyája és a CÍMKEJAVASLATOK rész (C09):
// a demó-állapotok (T14–T16) kitalált adata, majd izolált TANARA_HOME-on a valódi core-ral: a
// mód a piszkozatba kerül (mentetlen-jelzés, Mégse), a helyi végpont kapcsolat-tesztje ÁL-HTTP-
// szerver ellen, mentés → AppSettings mezők, modellváltás jelzése mentés előtt, a kapcsolók és
// az elutasított javaslatok visszaállítása. Valódi hálózat / LM Studio nincs.
#include "AppContext.h"
#include "SettingsEmbeddingModel.h"
#include "SettingsProviderModel.h"
#include "SettingsViewModel.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/embedding/EmbeddingProviderRegistry.h"
#include "tanara/tags/TagService.h"

#include "../unit/fake_http.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace tanara_qml;
using tanara::AppController;
namespace embid = tanara::embeddingproviders;

class TestSettingsEmbeddingModel : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<AppController> m_app;

    void startApp()
    {
        m_app.reset();
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        qputenv("TANARA_CLOUD_URL", "http://127.0.0.1:9");   // zárt port: a gateway sosem érhető el
        m_app = std::make_unique<AppController>();
    }

private slots:
    void cleanup()
    {
        m_app.reset();
        m_home.reset();
    }
    void cleanupTestCase()
    {
        qunsetenv("TANARA_HOME");
        qunsetenv("TANARA_CLOUD");
        qunsetenv("TANARA_CLOUD_URL");
    }

    // ---- demó (T14–T16) ---------------------------------------------------------------------

    void demoStatesMatchTheDesign()
    {
        SettingsViewModel vm;
        SettingsEmbeddingModel* emb = vm.embedding();
        QVERIFY(emb && emb->card());

        vm.setDemoState(QStringLiteral("B04embeddingRunning"));          // T14
        QCOMPARE(vm.page(), QStringLiteral("services"));
        QCOMPARE(emb->mode(), QStringLiteral("local"));
        QCOMPARE(emb->prepStatus(), QStringLiteral("running"));
        QCOMPARE(emb->countText(), QStringLiteral("23 / 40 megbeszélés"));
        QCOMPARE(emb->etaText(), QStringLiteral("kb. 4 perc van hátra"));
        QVERIFY(qAbs(emb->progress() - 23.0 / 40.0) < 1e-9);
        QCOMPARE(emb->status(), QStringLiteral("ok"));
        QCOMPARE(emb->statusText(), QStringLiteral("Kapcsolódva"));
        QVERIFY(!emb->restartPending());
        QVERIFY(!vm.dirty());
        // A beállított szerepek összecsukva, „Kapcsolódva” állapottal.
        QVERIFY(vm.stt()->configured() && !vm.stt()->expanded());
        QVERIFY(vm.llm()->configured() && !vm.llm()->expanded());
        QCOMPARE(vm.llm()->summaryText(), QStringLiteral("LM Studio · gemma-4-12b-qat"));
        QCOMPARE(vm.stt()->summaryText(), QStringLiteral("Soniox"));
        QCOMPARE(emb->card()->value(QStringLiteral("model")).toString(),
                 QStringLiteral("text-embedding-nomic-embed-text-v1.5"));

        // Megszakítás / Előkészítés a demóban csak az állapotot váltja.
        emb->cancelPreparation();
        QCOMPARE(emb->prepStatus(), QStringLiteral("idle"));
        emb->resumePreparation();
        QCOMPARE(emb->prepStatus(), QStringLiteral("running"));

        vm.setDemoState(QStringLiteral("B04embeddingError"));
        QCOMPARE(emb->prepStatus(), QStringLiteral("error"));
        QCOMPARE(emb->errorText(), QStringLiteral("Megállt 31 / 40-nél: a végpont nem válaszol. Fut a helyi szerver?"));
        QCOMPARE(emb->status(), QStringLiteral("failed"));
        QCOMPARE(emb->statusText(), QStringLiteral("Nem érhető el"));

        vm.setDemoState(QStringLiteral("B04embeddingDone"));
        QCOMPARE(emb->prepStatus(), QStringLiteral("done"));
        QCOMPARE(emb->doneText(), QStringLiteral("Naprakész · 40 megbeszélés · okt. 5. 09:12"));
        emb->reprepare();
        QCOMPARE(emb->prepStatus(), QStringLiteral("running"));
        QCOMPARE(emb->countText(), QStringLiteral("0 / 40 megbeszélés"));

        vm.setDemoState(QStringLiteral("B04embeddingCloud"));             // T16
        QCOMPARE(emb->mode(), QStringLiteral("cloud"));
        QVERIFY(emb->cloudInfo().contains(QStringLiteral("hangfelvétel és a hanglenyomatok a gépen maradnak")));
        QVERIFY(emb->cloudInfo().contains(QStringLiteral("120 kredit")));
        QVERIFY(emb->cloudHint().isEmpty());                              // a demóban be van jelentkezve
        QCOMPARE(emb->status(), QStringLiteral("ok"));

        vm.setDemoState(QStringLiteral("B04modelChange"));                // T15
        QVERIFY(emb->restartPending());
        QVERIFY(vm.dirty());
        QCOMPARE(vm.footerText(), QStringLiteral("Modellváltás · mentéskor újraindul az előkészítés"));
        QCOMPARE(emb->restartWarning(),
                 QStringLiteral("Másik modellre váltasz. Mentés után a könyvtár előkészítése elölről indul "
                                "(40 megbeszélés, kb. 6 perc). Addig a javaslatok az alap szinten működnek."));
        QCOMPARE(emb->prepStatus(), QStringLiteral("error"));

        vm.setDemoState(QStringLiteral("B04embeddingNone"));
        QCOMPARE(emb->mode(), QStringLiteral("none"));
        QCOMPARE(emb->status(), QStringLiteral("neutral"));               // az „alap” nem hiba
        QCOMPARE(emb->statusText(), QStringLiteral("Alap szint"));
        QVERIFY(!emb->prepVisible());
        QVERIFY(emb->description().contains(QStringLiteral("alap szinten")));

        // Elutasított javaslatok: 12 → visszaállítás.
        QCOMPARE(emb->rejectedCount(), 12);
        QSignalSpy rejected(emb, &SettingsEmbeddingModel::rejectedChanged);
        emb->clearRejected();
        QCOMPARE(emb->rejectedCount(), 0);
        QCOMPARE(rejected.count(), 1);
    }

    void deepLinkStillExpandsAndHighlights()
    {
        SettingsViewModel vm;
        vm.setDemoState(QStringLiteral("B04"));                           // az STT kulcsa hiányzik
        QVERIFY(vm.stt()->highlighted());
        QVERIFY(vm.stt()->expanded());
        QVERIFY(!vm.llm()->expanded());                                   // beállítva → összecsukva
        vm.llm()->setExpanded(true);
        QVERIFY(vm.llm()->expanded());
        vm.openPage(QStringLiteral("providers"), QStringLiteral("llm"));
        QVERIFY(vm.llm()->highlighted() && vm.llm()->expanded());
        // A hibás teszt is kinyitja a kártyát.
        vm.setDemoState(QStringLiteral("B05"));
        QVERIFY(vm.llm()->expanded());
    }

    void standaloneElementShowsDemoData()
    {
        SettingsEmbeddingModel emb;                                       // QML-ből szülő nélkül
        QVERIFY(!emb.card());
        emb.setDemoState(QStringLiteral("localRunning"));
        QCOMPARE(emb.mode(), QStringLiteral("local"));
        QCOMPARE(emb.prepStatus(), QStringLiteral("running"));
        emb.setDemoState(QStringLiteral("cloud"));
        QCOMPARE(emb.mode(), QStringLiteral("cloud"));
        QCOMPARE(emb.prepStatus(), QStringLiteral("done"));
        emb.setDemoState(QStringLiteral("modelChange"));
        QVERIFY(emb.restartPending());
    }

    // ---- valódi core ----------------------------------------------------------------------

    void modeIsADraftAndSavesToTheSettings()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        SettingsEmbeddingModel* emb = vm.embedding();
        QCOMPARE(emb->mode(), QStringLiteral("none"));
        QCOMPARE(emb->status(), QStringLiteral("neutral"));
        QVERIFY(!emb->prepVisible());
        QCOMPARE(emb->modeOptions().size(), 2);                           // Cloud nélküli build: nincs Cloud-elem
        emb->setMode(QStringLiteral("cloud"));
        QCOMPARE(emb->mode(), QStringLiteral("none"));
        QVERIFY(!vm.dirty());

        // Helyi végpont: piszkozat, alapértelmezett mezőkkel; vissza „alap”-ra → nincs változás.
        emb->setMode(QStringLiteral("local"));
        QCOMPARE(emb->mode(), QStringLiteral("local"));
        QVERIFY(vm.dirty());
        QCOMPARE(vm.changeCount(), 1);
        QCOMPARE(emb->prepStatus(), QStringLiteral("pending"));
        QVERIFY(!emb->card()->value(QStringLiteral("baseUrl")).toString().isEmpty());
        emb->setMode(QStringLiteral("none"));
        QVERIFY(!vm.dirty());
        emb->setMode(QStringLiteral("local"));

        // Kapcsolat-teszt és lekérés az ÁL-szerver ellen (a piszkozat címével, mentés előtt).
        fakehttp::Server http;
        http.handler = [](const fakehttp::Request&) {
            return fakehttp::Reply{200, "{\"data\":[{\"id\":\"embed-a\"},{\"id\":\"embed-b\"}]}"};
        };
        SettingsProviderModel* card = emb->card();
        QCOMPARE(card->kindName(), QStringLiteral("embedding"));
        card->setValue(QStringLiteral("baseUrl"), http.base());
        card->setValue(QStringLiteral("model"), QStringLiteral("embed-a"));
        QVERIFY(card->testable());
        card->test();
        QVERIFY(QTest::qWaitFor([&] { return card->testState() != QLatin1String("testing"); }, 5000));
        QCOMPARE(card->testState(), QStringLiteral("ok"));
        QCOMPARE(emb->status(), QStringLiteral("ok"));
        QCOMPARE(emb->statusText(), QStringLiteral("Kapcsolódva"));
        QCOMPARE(http.count("GET", QStringLiteral("/v1/models")), 1);
        QVERIFY(card->options(QStringLiteral("model")).contains(QStringLiteral("embed-b")));

        // Kapcsolók a piszkozatban.
        emb->setTagSuggestions(false);
        emb->setLlmTagSuggestions(false);
        QVERIFY(vm.save());
        QVERIFY(!vm.dirty());
        tanara::AppSettings s = m_app->settings()->settings();
        QCOMPARE(s.embeddingProviderId, embid::LocalId);
        QCOMPARE(s.embeddingConfigs.value(embid::LocalId).model, QStringLiteral("embed-a"));
        QCOMPARE(s.embeddingConfigs.value(embid::LocalId).baseUrl, http.base());
        QVERIFY(!s.tagSuggestions);
        QVERIFY(!s.llmTagSuggestions);
        QVERIFY(emb->prepStatus() != QLatin1String("pending"));          // elmentve: a core előkészít

        // Modellváltás: mentés ELŐTT jelezve; a Mégse eldobja.
        card->setValue(QStringLiteral("model"), QStringLiteral("embed-b"));
        QVERIFY(emb->restartPending());
        QVERIFY(emb->restartWarning().startsWith(QStringLiteral("Másik modellre váltasz.")));
        QCOMPARE(vm.footerText(), QStringLiteral("Modellváltás · mentéskor újraindul az előkészítés"));
        vm.discard();
        QVERIFY(!emb->restartPending());
        QCOMPARE(emb->card()->value(QStringLiteral("model")).toString(), QStringLiteral("embed-a"));

        // Más beállítás nem jelez modellváltást; mentve a core az új modellt kapja.
        emb->setTagSuggestions(true);
        QVERIFY(!emb->restartPending());
        card->setValue(QStringLiteral("model"), QStringLiteral("embed-b"));
        QVERIFY(vm.save());
        QVERIFY(!emb->restartPending());
        s = m_app->settings()->settings();
        QCOMPARE(s.embeddingConfigs.value(embid::LocalId).model, QStringLiteral("embed-b"));
        QVERIFY(s.tagSuggestions);

        // Vissza az alap szintre.
        emb->setMode(QStringLiteral("none"));
        QVERIFY(vm.save());
        QVERIFY(m_app->settings()->settings().embeddingProviderId.isEmpty());
        QCOMPARE(emb->status(), QStringLiteral("neutral"));
    }

    void invalidLocalFieldsBlockSave()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        vm.setPage(QStringLiteral("general"));
        SettingsEmbeddingModel* emb = vm.embedding();
        emb->setMode(QStringLiteral("local"));
        emb->card()->setValue(QStringLiteral("baseUrl"), QStringLiteral("nem-cim"));
        QVERIFY(!emb->card()->fieldErrors().isEmpty());
        QVERIFY(!vm.save());
        QCOMPARE(vm.page(), QStringLiteral("services"));
        QVERIFY(m_app->settings()->settings().embeddingProviderId.isEmpty());
    }

    void rejectedSuggestionsCanBeReset()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        SettingsEmbeddingModel* emb = vm.embedding();
        QCOMPARE(emb->rejectedCount(), 0);

        tanara::TagService* svc = m_app->tags();
        const tanara::Tag t = svc->create(QStringLiteral("Nordvik"));
        tanara::TagSuggestion sug;
        sug.tagId = t.id;
        sug.name = t.name;
        QSignalSpy changed(emb, &SettingsEmbeddingModel::rejectedChanged);
        svc->reject(QStringLiteral("m-1"), sug);
        svc->reject(QStringLiteral("m-2"), sug);
        QCOMPARE(emb->rejectedCount(), 2);
        QVERIFY(changed.count() >= 1);

        emb->clearRejected();                                             // azonnal, nem a piszkozatba
        QCOMPARE(emb->rejectedCount(), 0);
        QCOMPARE(svc->rejectedCount(), 0);
        QVERIFY(!vm.dirty());
    }
};

QTEST_MAIN(TestSettingsEmbeddingModel)
#include "test_settings_embedding_model.moc"

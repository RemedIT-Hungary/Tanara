// SettingsEmbeddingModel: the Settings "Embedding" card and the TAG SUGGESTIONS section: the
// demo states' made-up data, then on an isolated TANARA_HOME with the real core: the mode goes
// into the draft (unsaved indicator, Cancel), the local endpoint's connection test against a
// FAKE HTTP server, save to the tanara::app::AppSettings fields, the model-change notice before saving,
// resetting the switches and the rejected suggestions. No real network / LM Studio.
#include "AppContext.h"
#include "SettingsEmbeddingModel.h"
#include "SettingsProviderModel.h"
#include "SettingsViewModel.h"

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/providers/embedding/ITextEmbedder.h"
#include "tanara/library/tags/TagService.h"

#include "../unit/fake_http.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace tanara_qml;
using tanara::app::AppController;
namespace embid = tanara::providers::embeddingproviders;

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
        qputenv("TANARA_CLOUD_URL", "http://127.0.0.1:9");   // closed port: the gateway is never reachable
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

    // ---- demo ----

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
        // The configured roles collapsed, with a "Connected" state.
        QVERIFY(vm.stt()->configured() && !vm.stt()->expanded());
        QVERIFY(vm.llm()->configured() && !vm.llm()->expanded());
        QCOMPARE(vm.llm()->summaryText(), QStringLiteral("LM Studio · gemma-4-12b-qat"));
        QCOMPARE(vm.stt()->summaryText(), QStringLiteral("Soniox"));
        QCOMPARE(emb->card()->value(QStringLiteral("model")).toString(),
                 QStringLiteral("text-embedding-nomic-embed-text-v1.5"));

        // Cancel / Prepare in the demo only switch the state.
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
        QCOMPARE(emb->status(), QStringLiteral("neutral"));               // Cloud is "coming soon"
        QCOMPARE(emb->statusText(), QStringLiteral("Nem elérhető"));

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
        QCOMPARE(emb->status(), QStringLiteral("neutral"));               // the default is not an error
        QCOMPARE(emb->statusText(), QStringLiteral("Alap szint"));
        QVERIFY(!emb->prepVisible());
        QVERIFY(emb->description().contains(QStringLiteral("alap szinten")));

        // Rejected suggestions: 12, then reset.
        QCOMPARE(emb->rejectedCount(), 12);
        QSignalSpy rejected(emb, &SettingsEmbeddingModel::rejectedChanged);
        emb->clearRejected();
        QCOMPARE(emb->rejectedCount(), 0);
        QCOMPARE(rejected.count(), 1);
    }

    void deepLinkStillExpandsAndHighlights()
    {
        SettingsViewModel vm;
        vm.setDemoState(QStringLiteral("B04"));                           // the STT key is missing
        QVERIFY(vm.stt()->highlighted());
        QVERIFY(vm.stt()->expanded());
        QVERIFY(!vm.llm()->expanded());                                   // configured: collapsed
        vm.llm()->setExpanded(true);
        QVERIFY(vm.llm()->expanded());
        vm.openPage(QStringLiteral("providers"), QStringLiteral("llm"));
        QVERIFY(vm.llm()->highlighted() && vm.llm()->expanded());
        // A failed test also opens the card.
        vm.setDemoState(QStringLiteral("B05"));
        QVERIFY(vm.llm()->expanded());
    }

    void standaloneElementShowsDemoData()
    {
        SettingsEmbeddingModel emb;                                       // from QML, without a parent
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

    // ---- real core ----

    void modeIsADraftAndSavesToTheSettings()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        SettingsEmbeddingModel* emb = vm.embedding();
        QCOMPARE(emb->mode(), QStringLiteral("none"));
        QCOMPARE(emb->status(), QStringLiteral("neutral"));
        QVERIFY(!emb->prepVisible());
        {
            // The Cloud item is shown ("coming soon") but never selectable.
            const QVariantList options = emb->modeOptions();
            QCOMPARE(options.size(), 3);
            const QVariantMap cloud = options.last().toMap();
            QCOMPARE(cloud.value(QStringLiteral("value")).toString(), QStringLiteral("cloud"));
            QVERIFY(!cloud.value(QStringLiteral("enabled")).toBool());
        }
        emb->setMode(QStringLiteral("cloud"));
        QCOMPARE(emb->mode(), QStringLiteral("none"));
        QVERIFY(!vm.dirty());

        // Local endpoint: a draft with the default fields; back to "default": no change.
        emb->setMode(QStringLiteral("local"));
        QCOMPARE(emb->mode(), QStringLiteral("local"));
        QVERIFY(vm.dirty());
        QCOMPARE(vm.changeCount(), 1);
        QCOMPARE(emb->prepStatus(), QStringLiteral("pending"));
        QVERIFY(!emb->card()->value(QStringLiteral("baseUrl")).toString().isEmpty());
        emb->setMode(QStringLiteral("none"));
        QVERIFY(!vm.dirty());
        emb->setMode(QStringLiteral("local"));

        // Connection test and fetch against the FAKE server (with the draft's address, before saving).
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

        // Switches in the draft.
        emb->setTagSuggestions(false);
        emb->setLlmTagSuggestions(false);
        QVERIFY(vm.save());
        QVERIFY(!vm.dirty());
        tanara::app::AppSettings s = m_app->settings()->settings();
        QCOMPARE(s.providers.embedding.selectedId, embid::LocalId);
        QCOMPARE(s.providers.embedding.configs.value(embid::LocalId).model, QStringLiteral("embed-a"));
        QCOMPARE(s.providers.embedding.configs.value(embid::LocalId).baseUrl, http.base());
        QVERIFY(!s.processing.tagSuggestions);
        QVERIFY(!s.processing.llmTagSuggestions);
        QVERIFY(emb->prepStatus() != QLatin1String("pending"));          // saved: core prepares

        // Model change: announced BEFORE saving; Cancel discards it.
        card->setValue(QStringLiteral("model"), QStringLiteral("embed-b"));
        QVERIFY(emb->restartPending());
        QVERIFY(emb->restartWarning().startsWith(QStringLiteral("Másik modellre váltasz.")));
        QCOMPARE(vm.footerText(), QStringLiteral("Modellváltás · mentéskor újraindul az előkészítés"));
        vm.discard();
        QVERIFY(!emb->restartPending());
        QCOMPARE(emb->card()->value(QStringLiteral("model")).toString(), QStringLiteral("embed-a"));

        // Another setting does not announce a model change; once saved, core gets the new model.
        emb->setTagSuggestions(true);
        QVERIFY(!emb->restartPending());
        card->setValue(QStringLiteral("model"), QStringLiteral("embed-b"));
        QVERIFY(vm.save());
        QVERIFY(!emb->restartPending());
        s = m_app->settings()->settings();
        QCOMPARE(s.providers.embedding.configs.value(embid::LocalId).model, QStringLiteral("embed-b"));
        QVERIFY(s.processing.tagSuggestions);

        // Back to the default level.
        emb->setMode(QStringLiteral("none"));
        QVERIFY(vm.save());
        QVERIFY(m_app->settings()->settings().providers.embedding.selectedId.isEmpty());
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
        QVERIFY(m_app->settings()->settings().providers.embedding.selectedId.isEmpty());
    }

    void rejectedSuggestionsCanBeReset()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        SettingsEmbeddingModel* emb = vm.embedding();
        QCOMPARE(emb->rejectedCount(), 0);

        tanara::library::TagService* svc = m_app->tags();
        const tanara::Tag t = svc->create(QStringLiteral("Nordvik"));
        tanara::TagSuggestion sug;
        sug.tagId = t.id;
        sug.name = t.name;
        QSignalSpy changed(emb, &SettingsEmbeddingModel::rejectedChanged);
        svc->reject(QStringLiteral("m-1"), sug);
        svc->reject(QStringLiteral("m-2"), sug);
        QCOMPARE(emb->rejectedCount(), 2);
        QVERIFY(changed.count() >= 1);

        emb->clearRejected();                                             // at once, not into the draft
        QCOMPARE(emb->rejectedCount(), 0);
        QCOMPARE(svc->rejectedCount(), 0);
        QVERIFY(!vm.dirty());
    }
};

QTEST_MAIN(TestSettingsEmbeddingModel)
#include "test_settings_embedding_model.moc"

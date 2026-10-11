// VoiceModelListModel (Beállítások › Beszélők, Első lépések) — hangmodellek listája, letöltés,
// bekapcsolás a piszkozatba, lenyomat-pótlás állapota.
//
//  - demó-módban (controller nélkül): a B08-állapotok kitalált adatai;
//  - valódi AppControllerrel IZOLÁLT TANARA_HOME-ban: a letöltés file:// URL-ről megy (HÁLÓZAT
//    NINCS), a modellek „betöltése” hamis (setVoiceEmbedderLoader), az elérhetőség felülírva.
#include "AppContext.h"
#include "QmlApp.h"
#include "SettingsViewModel.h"
#include "VoiceModelListModel.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceModelDownloader.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <memory>

using namespace tanara_qml;
using tanara::AppController;
using tanara::VoiceModelDownloader;
using tanara::VoiceModelRegistry;
using tanara::VoiceModelSpec;

namespace {

const QString kCamp = QStringLiteral("campplus");
const QString kWe = QStringLiteral("wespeaker-resnet34-lm");
const QString kEres = QStringLiteral("eres2netv2");

tanara::PcmEmbedderLoader fakeLoader()
{
    return [](const VoiceModelSpec& spec, const QString&, QString*) -> tanara::PcmEmbedder {
        const int dim = spec.dim;
        return [dim](const QVector<float>&) { return QVector<float>(dim, 0.5f); };
    };
}

QVariant role(const VoiceModelListModel& m, int row, const char* name)
{
    return m.data(m.index(row), m.roleNames().key(name));
}

int rowOf(const VoiceModelListModel& m, const QString& id)
{
    for (int i = 0; i < m.rowCount(); ++i)
        if (role(m, i, "modelId").toString() == id) return i;
    return -1;
}

// A Repeater delegate-jei nem QObject-gyerekek: a vizuális fán keresünk.
QQuickItem* findItem(QQuickItem* root, const QString& name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem* c : root->childItems())
        if (QQuickItem* f = findItem(c, name)) return f;
    return nullptr;
}

QString stateOf(const VoiceModelListModel& m, const QString& id) { return role(m, rowOf(m, id), "state").toString(); }

} // namespace

class TestVoiceModelListModel : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<QTemporaryDir> m_src;
    std::unique_ptr<AppController> m_app;

    void startApp()
    {
        m_app.reset();
        m_home = std::make_unique<QTemporaryDir>();
        m_src = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid() && m_src->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        m_app = std::make_unique<AppController>();
        m_app->setVoiceEmbedderLoader(fakeLoader());
    }
    QString modelPath(const QString& id) const
    {
        return QDir(m_home->path()).filePath(QStringLiteral("models/") + VoiceModelRegistry::spec(id)->fileName);
    }
    void placeModel(const QString& id, qint64 size = -1)
    {
        const QString f = modelPath(id);
        QDir().mkpath(QFileInfo(f).absolutePath());
        QFile file(f);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(size < 0 ? VoiceModelRegistry::spec(id)->sizeBytes : size, 'm'));
    }
    // A beépített leírók, de a letöltés helyi file:// forrásból (kicsi fájl, a méret ehhez igazítva).
    QVector<VoiceModelSpec> localSpecs()
    {
        QVector<VoiceModelSpec> out;
        for (VoiceModelSpec s : VoiceModelRegistry::builtin()) {
            const QString src = m_src->filePath(s.fileName);
            QFile f(src);
            if (f.open(QIODevice::WriteOnly)) f.write(QByteArray(64 * 1024 + s.id.size(), 'x'));
            f.close();
            s.downloadUrl = QUrl::fromLocalFile(src).toString();
            s.sizeBytes = QFileInfo(src).size();
            out << s;
        }
        return out;
    }

private slots:
    void cleanup()
    {
        m_app.reset();
        AppContext::instance()->setController(nullptr);
    }
    void cleanupTestCase()
    {
        qunsetenv("TANARA_HOME");
        qunsetenv("TANARA_CLOUD");
    }

    // ---- demó ----------------------------------------------------------------------------

    void demoStates()
    {
        VoiceModelListModel m;
        m.setDemoState(QStringLiteral("B08"));
        QCOMPARE(m.rowCount(), 3);
        // WeSpeaker, CAM++, ERes2NetV2; ajánlott csak a WeSpeaker.
        QCOMPARE(role(m, 0, "modelId").toString(), kWe);
        QCOMPARE(role(m, 1, "modelId").toString(), kCamp);
        QCOMPARE(role(m, 2, "modelId").toString(), kEres);
        QVERIFY(role(m, 0, "recommended").toBool());
        QVERIFY(!role(m, 1, "recommended").toBool());
        QVERIFY(role(m, 2, "notRecommended").toBool());
        QCOMPARE(role(m, 0, "license").toString(), QStringLiteral("CC-BY-4.0"));
        QVERIFY(!role(m, 0, "licenseNote").toString().isEmpty());          // CC BY: attribúció
        QVERIFY(role(m, 1, "licenseNote").toString().isEmpty());
        QCOMPARE(role(m, 0, "sourceText").toString(), QStringLiteral("huggingface.co/Wespeaker/…"));
        QCOMPARE(role(m, 1, "sizeText").toString(), QStringLiteral("28 MB"));
        QCOMPARE(role(m, 0, "dimText").toString(), QStringLiteral("256 dim."));
        QVERIFY(role(m, 0, "blurb").toString().contains(QStringLiteral("Ajánlott alap")));
        QVERIFY(role(m, 1, "blurb").toString().contains(QStringLiteral("Tartalék")));
        QVERIFY(!m.fallbackActive());
        QCOMPARE(stateOf(m, kEres), QStringLiteral("missing"));
        QVERIFY(m.anyAvailable());
        QCOMPARE(m.activeCount(), 1);
        QCOMPARE(m.backfillState(), QString());

        m.setDemoState(QStringLiteral("B08download"));
        QCOMPARE(stateOf(m, kWe), QStringLiteral("downloading"));
        QVERIFY(role(m, rowOf(m, kWe), "progress").toDouble() > 0.4);
        QVERIFY(role(m, rowOf(m, kWe), "progressText").toString().endsWith(QStringLiteral("/ 27 MB")));

        m.setDemoState(QStringLiteral("B08none"));
        QVERIFY(!m.anyAvailable());
        QCOMPARE(m.activeCount(), 0);
        QCOMPARE(m.defaultState(), QStringLiteral("missing"));
        QCOMPARE(m.defaultSizeText(), QStringLiteral("27 MB"));
        QVERIFY(!m.fallbackActive());   // nincs CAM++ sem: nincs tartalék, a hangelemzés ki van kapcsolva

        m.setDemoState(QStringLiteral("B08error"));
        QCOMPARE(stateOf(m, kWe), QStringLiteral("error"));
        QVERIFY(!role(m, rowOf(m, kWe), "errorText").toString().isEmpty());

        m.setDemoState(QStringLiteral("B08offline"));
        QVERIFY(m.offline());
        QCOMPARE(stateOf(m, kEres), QStringLiteral("offline"));

        m.setDemoState(QStringLiteral("B08backfill"));
        QCOMPARE(m.backfillState(), QStringLiteral("running"));
        QVERIFY(m.backfillText().contains(QStringLiteral("14 / 23")));
        QVERIFY(qAbs(m.backfillProgress() - 14.0 / 23.0) < 1e-6);

        m.setDemoState(QStringLiteral("B08partial"));
        QCOMPARE(m.backfillState(), QStringLiteral("partial"));
        QCOMPARE(m.backfillSkipped(), 3);

        m.setDemoState(QStringLiteral("B08bundled"));
        QCOMPARE(stateOf(m, kCamp), QStringLiteral("bundled"));
        QVERIFY(role(m, 1, "blurb").toString().contains(QStringLiteral("csomagban")));
        QVERIFY(!m.fallbackActive());

        // A WeSpeaker hiányzik, a csomagolt CAM++ tartalékként fut.
        m.setDemoState(QStringLiteral("B08fallback"));
        QCOMPARE(stateOf(m, kWe), QStringLiteral("missing"));
        QCOMPARE(stateOf(m, kCamp), QStringLiteral("bundled"));
        QCOMPARE(m.activeCount(), 0);
        QVERIFY(m.fallbackActive());
        QCOMPARE(m.defaultState(), QStringLiteral("missing"));
    }

    void settingsDemoOpensTheSpeakersPage()
    {
        SettingsViewModel vm;
        vm.setDemoState(QStringLiteral("B08none"));
        QCOMPARE(vm.page(), QStringLiteral("speakers"));
        QVERIFY(!vm.voiceModels()->anyAvailable());
        vm.openPage(QStringLiteral("voiceModels"));
        QCOMPARE(vm.page(), QStringLiteral("speakers"));
    }

    // ---- valódi controller, letöltés file:// forrásról ----------------------------------

    void downloadsTheDefaultModelAndTheCoreUsesIt()
    {
        startApp();
        VoiceModelListModel m;
        m.setSpecsForTest(localSpecs());
        m.setController(m_app.get());
        m.downloader()->setReachableOverride(true);
        m.refresh();
        QVERIFY(!m.anyAvailable());
        QCOMPARE(m.defaultState(), QStringLiteral("missing"));
        QVERIFY(m_app->activeVoiceModelIds().isEmpty());

        // Letöltés előtt nem kapcsolható be.
        m.setEnabled(kCamp, true);
        QVERIFY(!m_app->settings()->enabledVoiceModels().contains(kCamp));

        QSignalSpy done(&m, &VoiceModelListModel::downloadFinished);
        m.download(kWe);
        QCOMPARE(m.defaultState(), QStringLiteral("downloading"));
        QVERIFY(done.wait(5000));
        QVERIFY(done.first().at(1).toBool());
        QCOMPARE(m.defaultState(), QStringLiteral("downloaded"));
        QVERIFY(QFileInfo::exists(modelPath(kWe)));
        // Az alapmodell eleve be volt kapcsolva: a core most már használja.
        QVERIFY(QTest::qWaitFor([&] { return m_app->activeVoiceModelIds() == QStringList{kWe}; }, 3000));
        QVERIFY(m.anyAvailable());
        QCOMPARE(m.activeCount(), 1);
    }

    void downloadEnablesInTheDraftAndSaveWritesIt()
    {
        startApp();
        placeModel(kWe, 10);
        SettingsViewModel vm;
        vm.voiceModels()->setSpecsForTest(localSpecs());
        vm.setController(m_app.get());
        VoiceModelListModel* m = vm.voiceModels();
        m->downloader()->setReachableOverride(true);
        // A helyőrző mérete eltér a leíróétól → „sérült” → hiba.
        m->refresh();
        QCOMPARE(stateOf(*m, kWe), QStringLiteral("error"));
        QVERIFY(role(*m, rowOf(*m, kWe), "errorText").toString().contains(QStringLiteral("mérete")));
        // Újra: a sérült fájlt lecseréli.
        QSignalSpy done(m, &VoiceModelListModel::downloadFinished);
        m->retry(kWe);
        QVERIFY(done.wait(5000));
        QCOMPARE(stateOf(*m, kWe), QStringLiteral("downloaded"));
        QVERIFY(!vm.dirty());

        done.clear();
        m->download(kCamp);
        QVERIFY(done.wait(5000));
        QCOMPARE(stateOf(*m, kCamp), QStringLiteral("downloaded"));
        // A letöltés azonnali, a bekapcsolás piszkozat: Mentésig a core nem használja.
        QVERIFY(role(*m, rowOf(*m, kCamp), "enabled").toBool());
        QVERIFY(vm.dirty());
        QVERIFY(!m_app->settings()->enabledVoiceModels().contains(kCamp));
        QVERIFY(vm.footerText().contains(QStringLiteral("pótlás")));
        QVERIFY(vm.save());
        QCOMPARE(m_app->settings()->enabledVoiceModels(), QStringList({kCamp, kWe}));
        QVERIFY(QTest::qWaitFor([&] { return m_app->activeVoiceModelIds() == QStringList({kCamp, kWe}); }, 3000));

        // Kikapcsolás + Mégse: semmi nem íródik.
        m->setEnabled(kCamp, false);
        QVERIFY(vm.dirty());
        vm.discard();
        QVERIFY(role(*m, rowOf(*m, kCamp), "enabled").toBool());
        QCOMPARE(m_app->settings()->enabledVoiceModels(), QStringList({kCamp, kWe}));
    }

    void cancelOfflineAndBundled()
    {
        startApp();
        VoiceModelListModel m;
        m.setSpecsForTest(localSpecs());
        m.setController(m_app.get());
        m.downloader()->setReachableOverride(true);
        m.refresh();

        m.download(kWe);
        QCOMPARE(stateOf(m, kWe), QStringLiteral("downloading"));
        m.cancel(kWe);
        QVERIFY(QTest::qWaitFor([&] { return stateOf(m, kWe) == QLatin1String("missing"); }, 3000));
        QVERIFY(!QFileInfo::exists(modelPath(kWe)));

        // Offline: a valódi (https) leírónál a letöltés el sem indul.
        m.setSpecsForTest(VoiceModelRegistry::builtin());
        m.downloader()->setReachableOverride(false);
        m.refresh();
        QVERIFY(m.offline());
        QCOMPARE(stateOf(m, kEres), QStringLiteral("offline"));
        m.download(kEres);
        QCOMPARE(stateOf(m, kEres), QStringLiteral("offline"));
        QVERIFY(!m.downloader()->isRunning(kEres));
        QVERIFY(!QFileInfo::exists(modelPath(kEres)));

        // Windows-csomag: az alapmodell az <appDir>/models-ban — „a csomagban”, nem tölthető le.
        QTemporaryDir app;
        const QString bundled = QDir(app.path()).filePath(QStringLiteral("models/") + VoiceModelRegistry::spec(kCamp)->fileName);
        QDir().mkpath(QFileInfo(bundled).absolutePath());
        QFile f(bundled);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        m.setDirsForTest(m_home->path(), app.path());
        QCOMPARE(stateOf(m, kCamp), QStringLiteral("bundled"));
        QVERIFY(role(m, rowOf(m, kCamp), "canEnable").toBool());
        m.download(kCamp);
        QCOMPARE(stateOf(m, kCamp), QStringLiteral("bundled"));
    }

    void backfillStatusFollowsTheCore()
    {
        startApp();
        placeModel(kCamp, 10);
        placeModel(kWe, 10);
        m_app->settings()->setEnabledVoiceModels({kCamp});
        m_app->setVoiceEmbedderLoader(fakeLoader());   // a fájlok most kerültek a helyükre: újraszámol
        QCOMPARE(m_app->activeVoiceModelIds(), QStringList{kCamp});

        // Két minta, amelyekhez nincs hang: egy létező megbeszélésből (fut a háttérben, de
        // hiányzik a felvétel) és egy törölt megbeszélésből (el sem indul).
        tanara::Meeting meeting = m_app->store()->createMeeting(QStringLiteral("Hang nélkül"));
        m_app->store()->saveMeeting(meeting);
        tanara::Voiceprint vp;
        vp.embedding = {1, 0, 0};
        vp.model = kCamp;
        vp.sourceMeetingId = meeting.id;
        vp.sampleRef = QStringLiteral("nincs-ilyen.wav#0-3000");
        vp.createdAt = QStringLiteral("2026-10-01T10:00:00");
        m_app->voiceprints()->addPrint(QStringLiteral("Kovács Lilla"), vp);
        tanara::Voiceprint orphan = vp;
        orphan.sourceMeetingId = QStringLiteral("torolt-megbeszeles");
        orphan.sampleRef = QStringLiteral("mixdown.wav#0-2000");
        m_app->voiceprints()->addPrint(QStringLiteral("Fehér Gábor"), orphan);

        VoiceModelListModel m;
        m.setController(m_app.get());
        QCOMPARE(m.backfillState(), QString());   // a használt modellekből nem hiányzik semmi

        QStringList seen;
        connect(&m, &VoiceModelListModel::backfillChanged, this, [&] { seen << m.backfillState(); });
        m_app->settings()->setEnabledVoiceModels({kCamp, kWe});   // bővült → pótlás
        QVERIFY(m_app->voiceprintBackfillRunning());
        QVERIFY(QTest::qWaitFor([&] { return m.backfillState() == QLatin1String("partial"); }, 10000));
        QVERIFY2(seen.contains(QStringLiteral("running")), qPrintable(seen.join(',')));
        QCOMPARE(m.backfillSkipped(), 2);
        QVERIFY(m.backfillText().contains(QStringLiteral("2 minta")));

        // Egy új példány (pl. az ablak újranyitása) is látja a pótolhatatlan mintákat (az
        // induláskori pótlás közben újra lefuthat — azt megvárjuk).
        QVERIFY(QTest::qWaitFor([&] { return !m_app->voiceprintBackfillRunning(); }, 10000));
        QTest::qWait(50);
        QCOMPARE(m.backfillState(), QStringLiteral("partial"));
        VoiceModelListModel again;
        again.setController(m_app.get());
        QCOMPARE(again.backfillState(), QStringLiteral("partial"));
        QCOMPARE(again.backfillSkipped(), 2);
    }

    void fallbackLineShownOnThePage()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "SettingsWindow");
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{QStringLiteral("demoState"), QStringLiteral("B08fallback")}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        QTest::qWait(200);
        auto* line = findItem(win->contentItem(), QStringLiteral("voiceModelsFallback"));
        QVERIFY(line);
        QVERIFY(line->isVisible());
        QCOMPARE(findItem(win->contentItem(), QStringLiteral("voiceModelsFallbackText"))->property("text").toString(),
                 QStringLiteral("Most a CAM++ tartalék fut, mert a WeSpeaker nincs letöltve."));
        QVERIFY(findItem(win->contentItem(), QStringLiteral("voiceModelsNone"))->isVisible() == false);
        auto* allOff = findItem(win->contentItem(), QStringLiteral("voiceModelsAllOff"));
        QVERIFY(!allOff || !allOff->isVisible());
    }

    // ---- a lap a Beállítások-ablakban ----------------------------------------------------

    void speakersPageRendersTheCards()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "SettingsWindow");
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{QStringLiteral("demoState"), QStringLiteral("B08none")}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        QTest::qWait(200);
        auto* vm0 = qobject_cast<SettingsViewModel*>(win->property("vm").value<QObject*>());
        QVERIFY(vm0);
        QCOMPARE(vm0->page(), QStringLiteral("speakers"));
        QVERIFY(!vm0->voiceModels()->anyAvailable());
        auto* none = findItem(win->contentItem(), QStringLiteral("voiceModelsNone"));
        QVERIFY(none);
        QVERIFY(none->isVisible());
        for (const QString& id : {kCamp, kWe, kEres})
            QVERIFY2(findItem(win->contentItem(), QStringLiteral("voiceModelCard-") + id), qPrintable(id));
        auto* vm = qobject_cast<SettingsViewModel*>(win->property("vm").value<QObject*>());
        QVERIFY(vm);
        // A CTA az alapmodellt tölti (demóban csak az állapot vált).
        QMetaObject::invokeMethod(findItem(win->contentItem(), QStringLiteral("voiceModelsNoneDownload")), "clicked");
        QCOMPARE(vm->voiceModels()->defaultState(), QStringLiteral("downloading"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};

QTEST_MAIN(TestVoiceModelListModel)
#include "test_voice_model_list_model.moc"

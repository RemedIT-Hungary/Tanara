// VoiceModelListModel (Settings > Speakers, Getting started): list of voice models, download,
// switching on in the draft, voiceprint-backfill state.
//
//  - in demo mode (without a controller): the made-up data of the model states;
//  - with a real AppController in an ISOLATED TANARA_HOME: the download goes from a file://
//    URL (NO NETWORK), the models' "loading" is fake (setVoiceEmbedderLoader), availability overridden.
#include "AppContext.h"
#include "UiServices.h"
#include "QmlApp.h"
#include "SettingsViewModel.h"
#include "VoiceModelListModel.h"

#include "tanara/app/AppController.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/processing/voiceid/VoiceModelDownloader.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QCryptographicHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <memory>

using namespace tanara_qml;
using tanara::app::AppController;
using tanara::processing::VoiceModelDownloader;
using tanara::processing::VoiceModelRegistry;
using tanara::processing::VoiceModelSpec;

namespace {

const QString kCamp = QStringLiteral("campplus");
const QString kWe = QStringLiteral("wespeaker-resnet34-lm");
const QString kEres = QStringLiteral("eres2netv2");

tanara::processing::PcmEmbedderLoader fakeLoader()
{
    return [](const VoiceModelSpec& spec, const QString&, QString*) -> tanara::processing::PcmEmbedder {
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

// Repeater delegates are not QObject children: we search the visual tree.
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
    // The real facade set of the test's controller.
    const UiServices& services() { return AppContext::instance()->servicesFor(m_app.get()); }

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
    // The built-in descriptors, but the download from a local file:// source (a small file, the size adjusted to it).
    QVector<VoiceModelSpec> localSpecs()
    {
        QVector<VoiceModelSpec> out;
        for (VoiceModelSpec s : VoiceModelRegistry::builtin()) {
            const QString src = m_src->filePath(s.fileName);
            QFile f(src);
            const QByteArray data(64 * 1024 + s.id.size(), 'x');
            if (f.open(QIODevice::WriteOnly)) f.write(data);
            f.close();
            s.downloadUrl = QUrl::fromLocalFile(src).toString();
            s.sizeBytes = QFileInfo(src).size();
            s.sha256 = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
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

    // ---- demo ----

    void demoStates()
    {
        VoiceModelListModel m;
        m.setDemoState(QStringLiteral("B08"));
        QCOMPARE(m.rowCount(), 3);
        // WeSpeaker, CAM++, ERes2NetV2; only WeSpeaker is recommended.
        QCOMPARE(role(m, 0, "modelId").toString(), kWe);
        QCOMPARE(role(m, 1, "modelId").toString(), kCamp);
        QCOMPARE(role(m, 2, "modelId").toString(), kEres);
        QVERIFY(role(m, 0, "recommended").toBool());
        QVERIFY(!role(m, 1, "recommended").toBool());
        QVERIFY(role(m, 2, "notRecommended").toBool());
        QCOMPARE(role(m, 0, "license").toString(), QStringLiteral("CC-BY-4.0"));
        QVERIFY(!role(m, 0, "licenseNote").toString().isEmpty());          // CC BY: attribution
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
        QVERIFY(!m.fallbackActive());   // no CAM++ either: no fallback, voice analysis is off

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
        QCOMPARE(m.backfillSkips().size(), 3);
        QVERIFY(m.backfillText().contains(QStringLiteral("3 minta")));
        for (const QVariant& e : m.backfillSkips()) {
            const QVariantMap mm = e.toMap();
            QVERIFY(!mm.value(QStringLiteral("person")).toString().isEmpty());
            QVERIFY(!mm.value(QStringLiteral("sample")).toString().isEmpty());
            QVERIFY(mm.value(QStringLiteral("reason")).toString().startsWith(QStringLiteral("hiányzó felvétel")));
        }

        m.setDemoState(QStringLiteral("B08verify"));
        QCOMPARE(stateOf(m, kWe), QStringLiteral("verifying"));
        QVERIFY(role(m, rowOf(m, kWe), "canEnable").toBool());      // usable while verifying
        QVERIFY(!role(m, rowOf(m, kWe), "canDelete").toBool());     // but not deletable
        QCOMPARE(m.activeCount(), 1);
        QCOMPARE(stateOf(m, kEres), QStringLiteral("error"));
        QVERIFY(role(m, rowOf(m, kEres), "corrupt").toBool());
        QVERIFY(m.backfillSkips().isEmpty());

        m.setDemoState(QStringLiteral("B08delete"));
        QVERIFY(role(m, rowOf(m, kCamp), "canDelete").toBool());
        QVERIFY(m.deleteModel(kCamp).isEmpty());
        QCOMPARE(stateOf(m, kCamp), QStringLiteral("missing"));
        QVERIFY(!role(m, rowOf(m, kCamp), "hasFile").toBool());

        m.setDemoState(QStringLiteral("B08bundled"));
        QCOMPARE(stateOf(m, kCamp), QStringLiteral("bundled"));
        QVERIFY(role(m, 1, "blurb").toString().contains(QStringLiteral("csomagban")));
        QVERIFY(!m.fallbackActive());

        // WeSpeaker is missing, the bundled CAM++ runs as a fallback.
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

    // ---- real controller, download from a file:// source ----

    void downloadsTheDefaultModelAndTheCoreUsesIt()
    {
        startApp();
        VoiceModelListModel m;
        m.setSpecsForTest(localSpecs());
        m.setFacade(services().voiceModels(QString(), nullptr));
        m.downloader()->setReachableOverride(true);
        m.refresh();
        QVERIFY(!m.anyAvailable());
        QCOMPARE(m.defaultState(), QStringLiteral("missing"));
        QVERIFY(m_app->activeVoiceModelIds().isEmpty());

        // Cannot be switched on before download.
        m.setEnabled(kCamp, true);
        QVERIFY(!m_app->settings()->enabledVoiceModels().contains(kCamp));

        QSignalSpy done(&m, &VoiceModelListModel::downloadFinished);
        m.download(kWe);
        QCOMPARE(m.defaultState(), QStringLiteral("downloading"));
        QVERIFY(done.wait(5000));
        QVERIFY(done.first().at(1).toBool());
        // After download a check (size), visible on the card; defaultState (Getting started) shows
        // the file's existence, then "downloaded".
        QCOMPARE(stateOf(m, kWe), QStringLiteral("verifying"));
        QCOMPARE(m.defaultState(), QStringLiteral("downloaded"));
        QVERIFY(QTest::qWaitFor([&] { return stateOf(m, kWe) == QLatin1String("downloaded"); }, 3000));
        QVERIFY(QFileInfo::exists(modelPath(kWe)));
        // The default model was switched on already: core uses it now.
        QVERIFY(QTest::qWaitFor([&] { return m_app->activeVoiceModelIds() == QStringList{kWe}; }, 3000));
        QVERIFY(m.anyAvailable());
        QCOMPARE(m.activeCount(), 1);
    }

    void downloadEnablesInTheDraftAndSaveWritesIt()
    {
        startApp();
        placeModel(kWe, 10);
        SettingsViewModel vm(services());
        vm.voiceModels()->setSpecsForTest(localSpecs());
        VoiceModelListModel* m = vm.voiceModels();
        m->downloader()->setReachableOverride(true);
        // The placeholder's size differs from the descriptor's, so "corrupt", so an error.
        m->refresh();
        QVERIFY(QTest::qWaitFor([&] { return stateOf(*m, kWe) == QLatin1String("error"); }, 3000));
        QVERIFY(role(*m, rowOf(*m, kWe), "corrupt").toBool());
        QVERIFY(role(*m, rowOf(*m, kWe), "errorText").toString().contains(QStringLiteral("mérete")));
        // Again: it replaces the corrupt file.
        QSignalSpy done(m, &VoiceModelListModel::downloadFinished);
        m->retry(kWe);
        QVERIFY(done.wait(5000));
        QVERIFY(QTest::qWaitFor([&] { return stateOf(*m, kWe) == QLatin1String("downloaded"); }, 3000));
        QVERIFY(!vm.dirty());

        done.clear();
        m->download(kCamp);
        QVERIFY(done.wait(5000));
        QVERIFY(QTest::qWaitFor([&] { return stateOf(*m, kCamp) == QLatin1String("downloaded"); }, 3000));
        // The download is immediate, switching on is a draft: core does not use it until Save.
        QVERIFY(role(*m, rowOf(*m, kCamp), "enabled").toBool());
        QVERIFY(vm.dirty());
        QVERIFY(!m_app->settings()->enabledVoiceModels().contains(kCamp));
        QVERIFY(vm.footerText().contains(QStringLiteral("pótlás")));
        QVERIFY(vm.save());
        QCOMPARE(m_app->settings()->enabledVoiceModels(), QStringList({kCamp, kWe}));
        QVERIFY(QTest::qWaitFor([&] { return m_app->activeVoiceModelIds() == QStringList({kCamp, kWe}); }, 3000));

        // Switching off + Cancel: nothing is written.
        m->setEnabled(kCamp, false);
        QVERIFY(vm.dirty());
        vm.discard();
        QVERIFY(role(*m, rowOf(*m, kCamp), "enabled").toBool());
        QCOMPARE(m_app->settings()->enabledVoiceModels(), QStringList({kCamp, kWe}));
    }

    // A download whose SHA-256 differs from the pinned one is parked: warning state "mismatch", not usable,
    // "Mégis használom" (acceptMismatch) moves it in, enables it and marks it "unverified" (usable).
    void shaMismatchWarnsAndTheOverrideMarksItUnverified()
    {
        startApp();
        VoiceModelListModel m;
        QVector<VoiceModelSpec> specs = localSpecs();
        for (VoiceModelSpec& s : specs)
            if (s.id == kEres) s.sha256 = QString(64, QLatin1Char('0'));
        m.setSpecsForTest(specs);
        m.setFacade(services().voiceModels(QString(), nullptr));
        m.downloader()->setReachableOverride(true);
        m.refresh();

        QSignalSpy done(&m, &VoiceModelListModel::downloadFinished);
        m.download(kEres);
        QVERIFY(done.wait(5000));
        QVERIFY(!done.first().at(1).toBool());
        QCOMPARE(stateOf(m, kEres), QStringLiteral("mismatch"));
        const int row = rowOf(m, kEres);
        QVERIFY(role(m, row, "errorText").toString().contains(QStringLiteral("rögzített SHA-val")));
        QVERIFY(!role(m, row, "canEnable").toBool());                 // parked: not in use
        QVERIFY(role(m, row, "hasFile").toBool());                    // but it can be deleted
        QVERIFY(!QFileInfo::exists(modelPath(kEres)));
        m.setEnabled(kEres, true);
        QVERIFY(!m.facade()->enabledModels().contains(kEres));

        QCOMPARE(m.acceptMismatch(kEres), QString());
        QVERIFY(QTest::qWaitFor([&] { return stateOf(m, kEres) == QLatin1String("unverified"); }, 3000));
        QVERIFY(QFileInfo::exists(modelPath(kEres)));
        QVERIFY(role(m, row, "canEnable").toBool());
        QVERIFY(role(m, row, "enabled").toBool());                    // enabled in the draft like after a download
        QVERIFY(role(m, row, "errorText").toString().contains(QStringLiteral("Ellenőrizetlen")));

        // The record survives a re-verification (the page opening again).
        m.refresh();
        QVERIFY(QTest::qWaitFor([&] { return stateOf(m, kEres) == QLatin1String("unverified"); }, 3000));

        // Delete removes the file and the override.
        QCOMPARE(m.deleteModel(kEres), QString());
        QCOMPARE(stateOf(m, kEres), QStringLiteral("missing"));
        QVERIFY(!QFileInfo::exists(modelPath(kEres) + QStringLiteral(".accepted-sha256")));

        // The design states.
        VoiceModelListModel d;
        d.setDemoState(QStringLiteral("B08mismatch"));
        QCOMPARE(stateOf(d, kEres), QStringLiteral("mismatch"));
        QCOMPARE(d.acceptMismatch(kEres), QString());
        QCOMPARE(stateOf(d, kEres), QStringLiteral("unverified"));
        d.setDemoState(QStringLiteral("B08unverified"));
        QCOMPARE(stateOf(d, kEres), QStringLiteral("unverified"));
    }

    void cancelOfflineAndBundled()
    {
        startApp();
        VoiceModelListModel m;
        m.setSpecsForTest(localSpecs());
        m.setFacade(services().voiceModels(QString(), nullptr));
        m.downloader()->setReachableOverride(true);
        m.refresh();

        m.download(kWe);
        QCOMPARE(stateOf(m, kWe), QStringLiteral("downloading"));
        m.cancel(kWe);
        QVERIFY(QTest::qWaitFor([&] { return stateOf(m, kWe) == QLatin1String("missing"); }, 3000));
        QVERIFY(!QFileInfo::exists(modelPath(kWe)));

        // Offline: for the real (https) descriptor the download does not even start.
        m.setSpecsForTest(VoiceModelRegistry::builtin());
        m.downloader()->setReachableOverride(false);
        m.refresh();
        QVERIFY(m.offline());
        QCOMPARE(stateOf(m, kEres), QStringLiteral("offline"));
        m.download(kEres);
        QCOMPARE(stateOf(m, kEres), QStringLiteral("offline"));
        QVERIFY(!m.downloader()->isRunning(kEres));
        QVERIFY(!QFileInfo::exists(modelPath(kEres)));

        // Windows package: the default model is in <appDir>/models, "in the package", not downloadable.
        QTemporaryDir app;
        const QString bundled = QDir(app.path()).filePath(QStringLiteral("models/") + VoiceModelRegistry::spec(kCamp)->fileName);
        QDir().mkpath(QFileInfo(bundled).absolutePath());
        QFile f(bundled);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        m.setDirsForTest(m_home->path(), app.path());
        QVERIFY(QTest::qWaitFor([&] { return stateOf(m, kCamp) == QLatin1String("bundled"); }, 3000));
        QVERIFY(role(m, rowOf(m, kCamp), "canEnable").toBool());
        // One in the package cannot be deleted.
        QVERIFY(!role(m, rowOf(m, kCamp), "hasFile").toBool());
        QVERIFY(!role(m, rowOf(m, kCamp), "canDelete").toBool());
        QVERIFY(!m.deleteModel(kCamp).isEmpty());
        QVERIFY(QFileInfo::exists(bundled));
        m.download(kCamp);
        QCOMPARE(stateOf(m, kCamp), QStringLiteral("bundled"));
    }

    void backfillStatusFollowsTheCore()
    {
        startApp();
        placeModel(kCamp, 10);
        placeModel(kWe, 10);
        m_app->settings()->setEnabledVoiceModels({kCamp});
        m_app->setVoiceEmbedderLoader(fakeLoader());   // the files just got into place: recomputes
        QCOMPARE(m_app->activeVoiceModelIds(), QStringList{kCamp});

        // Two samples with no audio: one from an existing meeting (runs in the background, but the
        // recording is missing) and one from a deleted meeting (does not even start).
        tanara::Meeting meeting = m_app->store()->createMeeting(QStringLiteral("Hang nélkül"));
        m_app->store()->saveMeeting(meeting);
        tanara::Voiceprint vp;
        vp.embedding = {1, 0, 0};
        vp.model = kCamp;
        vp.sourceMeetingId = meeting.id;
        vp.sampleRef = QStringLiteral("nincs-ilyen.wav#0-3000");
        vp.createdAt = QStringLiteral("2026-10-01T10:00:00");
        m_app->voiceprints()->addPrint(m_app->people()->ensure(QStringLiteral("Kovács Lilla")), vp);
        tanara::Voiceprint orphan = vp;
        orphan.sourceMeetingId = QStringLiteral("torolt-megbeszeles");
        orphan.sampleRef = QStringLiteral("mixdown.wav#0-2000");
        m_app->voiceprints()->addPrint(m_app->people()->ensure(QStringLiteral("Fehér Gábor")), orphan);

        VoiceModelListModel m;
        m.setFacade(services().voiceModels(QString(), nullptr));
        QCOMPARE(m.backfillState(), QString());   // nothing is missing from the models in use

        QStringList seen;
        connect(&m, &VoiceModelListModel::backfillChanged, this, [&] { seen << m.backfillState(); });
        m_app->settings()->setEnabledVoiceModels({kCamp, kWe});   // expanded: backfill
        QVERIFY(m_app->voiceprintBackfillRunning());
        QVERIFY(QTest::qWaitFor([&] { return m.backfillState() == QLatin1String("partial"); }, 10000));
        QVERIFY2(seen.contains(QStringLiteral("running")), qPrintable(seen.join(',')));
        QCOMPARE(m.backfillSkipped(), 2);
        QVERIFY(m.backfillText().contains(QStringLiteral("2 minta")));
        // For whom, which, why: sorted by person, with the meeting's title.
        const QVariantList skips = m.backfillSkips();
        QCOMPARE(skips.size(), 2);
        QCOMPARE(skips.at(0).toMap().value(QStringLiteral("person")).toString(), QStringLiteral("Fehér Gábor"));
        QVERIFY(skips.at(0).toMap().value(QStringLiteral("sample")).toString().startsWith(QStringLiteral("törölt megbeszélés · 0:00–0:02")));
        QVERIFY(skips.at(0).toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("nincs meg")));
        QCOMPARE(skips.at(1).toMap().value(QStringLiteral("person")).toString(), QStringLiteral("Kovács Lilla"));
        QCOMPARE(skips.at(1).toMap().value(QStringLiteral("sample")).toString(), QStringLiteral("Hang nélkül · 0:00–0:03"));
        QCOMPARE(skips.at(1).toMap().value(QStringLiteral("reason")).toString(), QStringLiteral("hiányzó felvétel"));
        QCOMPARE(skips.at(1).toMap().value(QStringLiteral("models")).toString(), QStringLiteral("WeSpeaker"));

        // The People window's details: the person's own indicator.
        BackfillSkipNote note;
        note.setFacade(services().voiceModels(QString(), nullptr));
        note.setPerson(QStringLiteral("Kovács Lilla"));
        QCOMPARE(note.count(), 1);
        QCOMPARE(note.text(), QStringLiteral("1 minta nem pótolható: hiányzó felvétel"));
        note.setPerson(QStringLiteral("Fehér Gábor"));
        QCOMPARE(note.text(), QStringLiteral("1 minta nem pótolható: hiányzó felvétel"));
        note.setPerson(QStringLiteral("Senki"));
        QCOMPARE(note.count(), 0);

        // A new instance (e.g. reopening the window) also sees the unrecoverable samples (the
        // startup backfill may rerun meanwhile; we wait for that).
        QVERIFY(QTest::qWaitFor([&] { return !m_app->voiceprintBackfillRunning(); }, 10000));
        QTest::qWait(50);
        QCOMPARE(m.backfillState(), QStringLiteral("partial"));
        VoiceModelListModel again;
        again.setFacade(services().voiceModels(QString(), nullptr));
        QCOMPARE(again.backfillState(), QStringLiteral("partial"));
        QCOMPARE(again.backfillSkipped(), 2);
    }

    // The k / N from core's signals (fake signals: the backfill is fast, the real run would not show the middle).
    void backfillProgressFollowsTheSignals()
    {
        startApp();
        VoiceModelListModel m;
        m.setFacade(services().voiceModels(QString(), nullptr));
        QCOMPARE(m.backfillState(), QString());
        emit m_app->voiceprintBackfillStarted(5);
        QCOMPARE(m.backfillState(), QStringLiteral("running"));
        QVERIFY(m.backfillText().contains(QStringLiteral("5 minta")));
        QVERIFY(m.backfillText().contains(QStringLiteral("(0 / 5)")));
        QCOMPARE(m.backfillProgress(), 0.0);
        emit m_app->voiceprintBackfillProgress(3, 5);
        QVERIFY(m.backfillText().contains(QStringLiteral("(3 / 5)")));
        QVERIFY(qAbs(m.backfillProgress() - 0.6) < 1e-9);
        emit m_app->voiceprintBackfillFinished(5, 0);
        QCOMPARE(m.backfillState(), QStringLiteral("done"));
        QVERIFY(m.backfillSkips().isEmpty());
        // New run: "running" again, the old list is not shown.
        emit m_app->voiceprintBackfillStarted(2);
        QCOMPARE(m.backfillState(), QStringLiteral("running"));
        QCOMPARE(m.backfillTotal(), 2);
    }

    // Check when the page opens (refresh) and delete: only the downloaded file.
    void verifyAndDeleteTheDownloadedFile()
    {
        startApp();
        VoiceModelListModel m;
        m.setSpecsForTest(localSpecs());
        m.setFacade(services().voiceModels(QString(), nullptr));
        m.downloader()->setReachableOverride(true);
        m.refresh();
        QSignalSpy done(&m, &VoiceModelListModel::downloadFinished);
        QSignalSpy verified(&m, &VoiceModelListModel::verifyFinished);
        m.download(kCamp);
        // Cannot be deleted during download.
        QVERIFY(!m.deleteModel(kCamp).isEmpty());
        QVERIFY(done.wait(5000));
        QVERIFY(verified.wait(3000));
        QCOMPARE(verified.last().at(0).toString(), kCamp);
        QVERIFY(verified.last().at(1).toBool());
        QCOMPARE(stateOf(m, kCamp), QStringLiteral("downloaded"));
        QVERIFY(role(m, rowOf(m, kCamp), "hasFile").toBool());
        QVERIFY(role(m, rowOf(m, kCamp), "canDelete").toBool());

        // The file gets corrupted meanwhile (e.g. an interrupted copy): on opening the page a check gives an error.
        const QString file = modelPath(kCamp);
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::Append));
            f.write("xx");
        }
        verified.clear();
        m.refresh();
        QCOMPARE(stateOf(m, kCamp), QStringLiteral("verifying"));
        QVERIFY(!role(m, rowOf(m, kCamp), "canDelete").toBool());
        QVERIFY(!m.deleteModel(kCamp).isEmpty());   // not during the check either
        QVERIFY(verified.wait(3000));
        QVERIFY(!verified.last().at(1).toBool());
        QCOMPARE(stateOf(m, kCamp), QStringLiteral("error"));
        QVERIFY(role(m, rowOf(m, kCamp), "corrupt").toBool());
        QVERIFY(role(m, rowOf(m, kCamp), "canDelete").toBool());   // the corrupt file can be deleted

        // Delete: the file disappears, the row is "not downloaded".
        QCOMPARE(m.deleteModel(kCamp), QString());
        QVERIFY(!QFileInfo::exists(file));
        QCOMPARE(stateOf(m, kCamp), QStringLiteral("missing"));
        QVERIFY(!role(m, rowOf(m, kCamp), "hasFile").toBool());
        QVERIFY(!role(m, rowOf(m, kCamp), "corrupt").toBool());
        QVERIFY(!m.deleteModel(kCamp).isEmpty());   // nothing to delete
    }

    // A hand-copied model file: after "Rescan" the list and core both see it.
    void rescanPicksUpAManuallyCopiedFile()
    {
        startApp();
        m_app->settings()->setEnabledVoiceModels({kWe});
        VoiceModelListModel m;
        m.setFacade(services().voiceModels(QString(), nullptr));
        m.downloader()->setReachableOverride(true);
        QCOMPARE(m.defaultState(), QStringLiteral("missing"));
        QVERIFY(m_app->activeVoiceModelIds().isEmpty());
        placeModel(kWe);
        QSignalSpy changed(m_app.get(), &AppController::voiceModelsChanged);
        m.rescan();
        QVERIFY(changed.count() >= 1);
        QCOMPARE(m_app->activeVoiceModelIds(), QStringList{kWe});
        QVERIFY(QTest::qWaitFor([&] { return m.defaultState() == QLatin1String("downloaded"); }, 3000));
        QCOMPARE(m.activeCount(), 1);
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

    // ---- the page in the Settings window ----

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
        // The CTA downloads the default model (in demo mode only the state changes).
        QMetaObject::invokeMethod(findItem(win->contentItem(), QStringLiteral("voiceModelsNoneDownload")), "clicked");
        QCOMPARE(vm->voiceModels()->defaultState(), QStringLiteral("downloading"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    // B08verify / B08delete / B08partial on the page: pill, confirmation, the list of skipped samples.
    void speakersPageDemoStates()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        auto open = [&](const QString& state) {
            QQmlComponent comp(&engine);
            comp.loadFromModule("Tanara", "SettingsWindow");
            std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{QStringLiteral("demoState"), state}}));
            auto* win = qobject_cast<QQuickWindow*>(obj.get());
            if (win) win->show();
            QTest::qWait(200);
            return obj;
        };
        {
            auto obj = open(QStringLiteral("B08verify"));
            auto* win = qobject_cast<QQuickWindow*>(obj.get());
            QVERIFY(win);
            QCOMPARE(findItem(win->contentItem(), QStringLiteral("voiceModelStatus-") + kWe)->property("text").toString(),
                     QStringLiteral("ellenőrzés…"));
            QCOMPARE(findItem(win->contentItem(), QStringLiteral("voiceModelStatus-") + kEres)->property("text").toString(),
                     QStringLiteral("hiba (méret nem egyezik)"));
            QVERIFY(findItem(win->contentItem(), QStringLiteral("voiceModelRetry-") + kEres)->isVisible());
            QVERIFY(findItem(win->contentItem(), QStringLiteral("voiceModelsRescan"))->isVisible());
            QVERIFY(!findItem(win->contentItem(), QStringLiteral("voiceModelsNone"))->isVisible());
        }
        {
            auto obj = open(QStringLiteral("B08delete"));
            auto* win = qobject_cast<QQuickWindow*>(obj.get());
            QVERIFY(win);
            QVERIFY(findItem(win->contentItem(), QStringLiteral("voiceModelMenu-") + kCamp)->isVisible());
            auto* vm = qobject_cast<SettingsViewModel*>(win->property("vm").value<QObject*>());
            QVERIFY(vm);
            auto* confirm = findItem(win->contentItem(), QStringLiteral("voiceModelDeleteConfirm"));
            QVERIFY(confirm);
            QVERIFY(confirm->isVisible());
            QMetaObject::invokeMethod(confirm, "clicked");
            QCOMPARE(stateOf(*vm->voiceModels(), kCamp), QStringLiteral("missing"));
            QVERIFY(QTest::qWaitFor([&] { return !confirm->isVisible(); }, 2000));
        }
        {
            auto obj = open(QStringLiteral("B08partial"));
            auto* win = qobject_cast<QQuickWindow*>(obj.get());
            QVERIFY(win);
            auto* list = findItem(win->contentItem(), QStringLiteral("voiceBackfillSkips"));
            QVERIFY(list);
            QVERIFY(list->isVisible());
            int rows = 0;
            for (QQuickItem* c : list->childItems()) if (c->objectName() == QLatin1String("voiceBackfillSkip")) ++rows;
            QCOMPARE(rows, 3);
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    // The People window's details: "1 sample cannot be backfilled: missing recording" (demo).
    void peopleDetailShowsTheSkippedSample()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "PeopleWindow");
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{QStringLiteral("demoState"), QStringLiteral("samplesModels")}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        QTest::qWait(200);
        auto* note = findItem(win->contentItem(), QStringLiteral("backfillSkipNote"));
        QVERIFY(note);
        QVERIFY(note->isVisible());
        QCOMPARE(findItem(win->contentItem(), QStringLiteral("backfillSkipNoteText"))->property("text").toString(),
                 QStringLiteral("1 minta nem pótolható: hiányzó felvétel"));
        // Not in another demo state.
        std::unique_ptr<QObject> other(comp.createWithInitialProperties({{QStringLiteral("demoState"), QStringLiteral("P01")}}));
        auto* win2 = qobject_cast<QQuickWindow*>(other.get());
        QVERIFY(win2);
        win2->show();
        QTest::qWait(200);
        QVERIFY(!findItem(win2->contentItem(), QStringLiteral("backfillSkipNote"))->isVisible());
    }
};

QTEST_MAIN(TestVoiceModelListModel)
#include "test_voice_model_list_model.moc"

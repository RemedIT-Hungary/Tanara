// SettingsViewModel (+ SettingsProviderModel / SettingsDeviceModel,
// SettingsPromptHighlighter, SettingsWindowHost): the Settings window's view models.
//
//  - in demo mode (without a controller): the design states' data;
//  - with a real AppController in an ISOLATED TANARA_HOME (QTemporaryDir): draft and save
//    (only the difference goes to core), discard, field validation, provider switch keeping
//    the other provider, keys in the KeyStore, device renaming both ways, prompts, deep link,
//    watched apps, cloud modes;
//  - connection test against a local fake HTTP server (NO real provider call).
// The Tanara Cloud gateway address is a closed local port (TANARA_CLOUD_URL), no sign-in.
#include "AppContext.h"
#include "UiServices.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "QmlApp.h"
#include "SettingsDeviceModel.h"
#include "SettingsDialogs.h"
#include "SettingsPromptHighlighter.h"
#include "SettingsEmbeddingModel.h"
#include "SettingsProviderModel.h"
#include "SettingsViewModel.h"
#include "SettingsWindowHost.h"
#include "TrackListModel.h"

#include "tanara/app/AppController.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/recording/audio/DeviceManager.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/cloud/CloudSession.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/providers/embedding/ITextEmbedder.h"
#include "tanara/watcher/detect/Autostart.h"
#include "tanara/watcher/detect/IMeetingDetector.h"
#include "tanara/library/store/detail/MeetingJson.h"
#include "tanara/library/store/ParticipantJson.h"
#include "tanara/library/store/MeetingRepository.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <memory>

using namespace tanara_qml;
using tanara::app::AppController;
using tanara::app::AppSettings;

namespace {

// ---- minimal fake HTTP server (as in tests/unit/test_app_jobs.cpp) ----
struct FakeReply { int status = 200; QByteArray body = "{}"; };

class FakeHttp : public QObject {
public:
    std::function<FakeReply(const QString& path, const QByteArray& auth)> handler;
    QStringList paths;
    QList<QByteArray> auths;

    FakeHttp()
    {
        connect(&m_srv, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = m_srv.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf]() { feed(s, *buf); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        m_srv.listen(QHostAddress::LocalHost);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_srv.serverPort()); }
    void close() { m_srv.close(); }

private:
    void feed(QTcpSocket* s, QByteArray& buf)
    {
        buf += s->readAll();
        const int headEnd = buf.indexOf("\r\n\r\n");
        if (headEnd < 0) return;
        const QByteArray head = buf.left(headEnd);
        const QString path = QString::fromUtf8(head.left(head.indexOf('\r')).split(' ').value(1));
        QByteArray auth;
        for (const QByteArray& line : head.split('\n')) {
            const QByteArray l = line.trimmed();
            if (l.toLower().startsWith("authorization:")) auth = l.mid(14).trimmed();
        }
        buf.clear();
        paths << path;
        auths << auth;
        const FakeReply rep = handler ? handler(path, auth) : FakeReply{};
        const QByteArray out = "HTTP/1.1 " + QByteArray::number(rep.status) + " X\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: " + QByteArray::number(rep.body.size()) + "\r\n"
                               "Connection: close\r\n\r\n" + rep.body;
        s->write(out);
        s->disconnectFromHost();
    }
    QTcpServer m_srv;
};

// ---- fake dialogs (in place of the Widgets world) ----
class FakeDialogs : public SettingsDialogs {
public:
    using SettingsDialogs::SettingsDialogs;
    QStringList calls;
    QString folderAnswer;

    QString pickFolder(const QString& title, const QString& start) override
    {
        calls << QStringLiteral("pick:%1:%2").arg(title, start);
        return folderAnswer;
    }
    void openFolder(const QString& path) override { calls << QStringLiteral("open:") + path; }
    void openUrl(const QString& url) override { calls << QStringLiteral("url:") + url; }
    void openPeople() override { calls << QStringLiteral("people"); }
};

// ---- fake detector: returns the given signal ----
class FakeDetector : public tanara::watcher::IMeetingDetector {
public:
    tanara::watcher::MeetingSignal signal;
    QStringList configured;
    QString id() const override { return QStringLiteral("fake"); }
    tanara::watcher::MeetingSignal poll() override { return signal; }
    bool isAvailable() const override { return true; }
    void configure(const QStringList& apps, const QString&) override { configured = apps; }
};

QVariant fieldProp(const QVariantList& fields, const QString& key, const QString& prop)
{
    for (const QVariant& f : fields)
        if (f.toMap().value(QStringLiteral("key")).toString() == key)
            return f.toMap().value(prop);
    return {};
}

QVariant row(SettingsViewModel& vm, int r, const char* role)
{
    QAbstractItemModel* m = vm.devicesModel();
    return m->data(m->index(r, 0), m->roleNames().key(role));
}

} // namespace

class TestSettingsViewModel : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<AppController> m_app;
    // The real facade set of the test's controller.
    const UiServices& services() { return AppContext::instance()->servicesFor(m_app.get()); }
    QString m_closedPortBase;

    // A fresh, isolated Tanara in a temporary folder. cloud: off | live | teaser.
    void startApp(const char* cloud = "off")
    {
        m_app.reset();
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        qputenv("TANARA_CLOUD", cloud);
        qputenv("TANARA_CLOUD_URL", "http://127.0.0.1:9");   // closed port: the gateway is never reachable
        m_app = std::make_unique<AppController>();
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
    }
    tanara::app::AppSettings live() const { return m_app->settings()->settings(); }
    QJsonObject settingsFile() const
    {
        QFile f(m_app->settings()->settingsFilePath());
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    }
    static bool waitFor(const std::function<bool()>& cond, int ms = 5000)
    {
        return QTest::qWaitFor(cond, ms);
    }

private slots:
    void initTestCase()
    {
        // A certainly closed local port (we open it, remember it, close it).
        FakeHttp probe;
        m_closedPortBase = probe.base();
        probe.close();
    }
    void cleanup()
    {
        m_app.reset();
        AppContext::instance()->setController(nullptr);
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
    }
    void cleanupTestCase()
    {
        qunsetenv("TANARA_HOME");
        qunsetenv("TANARA_CLOUD");
        qunsetenv("TANARA_CLOUD_URL");
    }

    // ---- the core of the difference computation ----

    void mergeAppliesOnlyWhatTheUserChanged()
    {
        // settings.json v2 shape: providers/<capability>/configs/<id>/<field>.
        auto stt = [](const QJsonObject& configs) {
            return QJsonObject{{"stt", QJsonObject{{"selected", "soniox"}, {"configs", configs}}}};
        };
        const QJsonObject base{{"a", 1}, {"b", "x"}, {"names", QJsonObject{{"mic", "A"}, {"spk", "B"}}},
                               {"providers", stt(QJsonObject{{"soniox", QJsonObject{{"model", "m1"}, {"baseUrl", "u"}}}})}};
        QJsonObject draft = base;
        draft["a"] = 2;                                                   // the user changed it
        draft["names"] = QJsonObject{{"mic", "A2"}};                      // rename + deleting one name
        draft["providers"] = stt(QJsonObject{{"soniox", QJsonObject{{"model", "m2"}, {"baseUrl", "u"}}}});
        QJsonObject onto = base;
        onto["b"] = "közben-más-mentette";                                // external change
        onto["c"] = true;                                                 // the window does not know it
        onto["providers"] = stt(QJsonObject{{"soniox", QJsonObject{{"model", "m1"}, {"baseUrl", "külső"}}},
                                            {"whisper", QJsonObject{{"model", "w"}}}});

        const QJsonObject out = SettingsViewModel::mergedSettings(base, draft, onto);
        QCOMPARE(out.value("a").toInt(), 2);
        QCOMPARE(out.value("b").toString(), QStringLiteral("közben-más-mentette"));
        QCOMPARE(out.value("c").toBool(), true);
        QCOMPARE(out.value("names").toObject(), (QJsonObject{{"mic", "A2"}}));
        const QJsonObject cfgs = out.value("providers").toObject().value("stt").toObject().value("configs").toObject();
        QCOMPARE(cfgs.value("soniox").toObject().value("model").toString(), QStringLiteral("m2"));
        QCOMPARE(cfgs.value("soniox").toObject().value("baseUrl").toString(), QStringLiteral("külső"));
        QVERIFY(cfgs.contains("whisper"));

        QCOMPARE(SettingsViewModel::countChanges(base, base), 0);
        QCOMPARE(SettingsViewModel::countChanges(base, draft), 4);   // a, mic, spk (deleted), model
        // The default fields of a provider selected for the first time are not separate changes.
        QJsonObject withNew = base;
        withNew["providers"] = stt(QJsonObject{{"soniox", QJsonObject{{"model", "m1"}, {"baseUrl", "u"}}},
                                               {"whisper", QJsonObject{{"model", "w"}, {"baseUrl", "v"}}}});
        QCOMPARE(SettingsViewModel::countChanges(base, withNew), 0);
    }

    void formatsSizes()
    {
        const QString gb = SettingsViewModel::formatBytes(12'400'000'000LL);
        QVERIFY2(gb == QStringLiteral("12,4 GB") || gb == QStringLiteral("12.4 GB"), qPrintable(gb));
        QCOMPARE(SettingsViewModel::formatBytes(38'200'000), QStringLiteral("38 MB"));
        QCOMPARE(SettingsViewModel::formatBytes(4'000), QStringLiteral("4 kB"));
    }

    void highlighterFindsVariables()
    {
        const QStringList known{QStringLiteral("{{NYELV}}")};
        const auto spans = SettingsPromptHighlighter::spans(
            QStringLiteral("3. Minden {{NYELV}} nyelven, {{SZÓJEGYZÉK}} és {nem} {{ ez sem }}"), known);
        QCOMPARE(spans.size(), 2);
        QCOMPARE(spans.at(0).start, 10);
        QCOMPARE(spans.at(0).length, 9);
        QVERIFY(spans.at(0).known);
        QVERIFY(!spans.at(1).known);                       // accented, but the code does not replace it
        QVERIFY(SettingsPromptHighlighter::spans(QStringLiteral("nincs benne"), known).isEmpty());
    }

    // ---- demo: the design states ----

    void demoStatesCarryFictionalData()
    {
        SettingsViewModel vm;
        vm.setDemoState(QStringLiteral("B01"));
        QVERIFY(vm.demo());
        QCOMPARE(vm.page(), QStringLiteral("general"));
        QCOMPARE(vm.userName(), QStringLiteral("Kovács Lilla"));
        QVERIFY(vm.hasVoiceprint());
        QVERIFY(!vm.dirty());
        QCOMPARE(vm.folders().size(), 4);   // recordings, notes, internal data, models
        QVERIFY(vm.folders().at(0).toMap().value("usage").toString().contains(QStringLiteral("GB")));
        QCOMPARE(vm.footerText(), QStringLiteral("Nincs mentetlen változás"));

        vm.setDemoState(QStringLiteral("B02"));
        QCOMPARE(vm.page(), QStringLiteral("recording"));
        QCOMPARE(vm.deviceCount(), 5);
        QCOMPARE(vm.changeCount(), 1);                     // the device just renamed
        QCOMPARE(row(vm, 3, "name").toString(), QStringLiteral("Kanto hangfal"));
        QCOMPARE(row(vm, 3, "renamed").toBool(), true);
        QCOMPARE(row(vm, 0, "group").toInt(), 0);
        QCOMPARE(row(vm, 2, "groupFirst").toBool(), true);

        vm.setDemoState(QStringLiteral("B03"));
        QVERIFY(vm.liveCallActive());
        QCOMPARE(vm.liveCallApp(), QStringLiteral("Microsoft Teams"));
        const QVariantList apps = vm.watchedApps();
        QCOMPARE(apps.size(), 6);
        QCOMPARE(apps.at(1).toMap().value("label").toString(), QStringLiteral("Microsoft Teams"));
        QCOMPARE(apps.at(1).toMap().value("active").toBool(), true);
        QCOMPARE(apps.at(0).toMap().value("active").toBool(), false);
        QCOMPARE(apps.at(5).toMap().value("label").toString(), QStringLiteral("Google Meet"));

        vm.setDemoState(QStringLiteral("B04"));
        QCOMPARE(vm.page(), QStringLiteral("processing"));
        QCOMPARE(vm.section(), QStringLiteral("stt"));
        QCOMPARE(vm.focusField(), QStringLiteral("stt"));
        QVERIFY(vm.stt()->highlighted());
        QVERIFY(!vm.llm()->highlighted());
        QVERIFY(vm.servicesWarn());                        // the transcriber's key is missing
        QVERIFY(!vm.focusBannerText().isEmpty());
        QCOMPARE(vm.llm()->testState(), QStringLiteral("ok"));

        vm.setDemoState(QStringLiteral("B05"));
        QCOMPARE(vm.llm()->testState(), QStringLiteral("failed"));
        QCOMPARE(vm.llm()->errorCode(), QStringLiteral("ECONNREFUSED"));
        QVERIFY(vm.llm()->advancedOpen());
        QVERIFY(vm.servicesWarn());                        // the latest test failed
        QCOMPARE(vm.stt()->testState(), QStringLiteral("ok"));
        QVERIFY(vm.stt()->statusText().contains(QStringLiteral("210")));

        vm.setDemoState(QStringLiteral("cloudComingSoon"));
        QCOMPARE(vm.page(), QStringLiteral("cloud"));
        QVERIFY(!vm.cloudSelected());
        QVERIFY(!vm.dirty());                              // the "coming soon" notice changes nothing

        vm.setDemoState(QStringLiteral("cloudSelected"));  // the draft puts stt + llm on the Cloud route
        QVERIFY(vm.cloudSelected());
        QCOMPARE(vm.route(QStringLiteral("stt")), QStringLiteral("cloud"));
        QCOMPARE(vm.stt()->providerId(), QStringLiteral("soniox"));   // the own provider stays selected
        QVERIFY(vm.servicesWarn());

        vm.setDemoState(QStringLiteral("B07"));
        QCOMPARE(vm.page(), QStringLiteral("processing"));
        QCOMPARE(vm.section(), QStringLiteral("summary"));
        QVERIFY(vm.promptModified());
        QVERIFY(!vm.dirty());
        QCOMPARE(vm.promptTabs().size(), 5);               // single / notes / merge + topic / analysis
        QCOMPARE(vm.promptGroups().size(), 2);
        QCOMPARE(vm.promptGroup(), QStringLiteral("quick"));
        QCOMPARE(vm.schemaKind(), QStringLiteral("json"));
        QVERIFY(vm.schemaBody().contains(QStringLiteral("actionItems")));
        QCOMPARE(vm.promptVariables().size(), 3);          // only what the code really replaces ({{LANGUAGE}}, {{NYELV}}, {{SPEAKER_LABEL}})

        // In demo mode saving happens only in memory.
        vm.setDemoState(QStringLiteral("dirty"));
        QCOMPARE(vm.changeCount(), 1);
        QSignalSpy saved(&vm, &SettingsViewModel::saved);
        QVERIFY(vm.save());
        QCOMPARE(saved.size(), 1);
        QVERIFY(!vm.dirty());
    }

    // ---- draft, save, discard ----

    void cleanLoadAndDirtyTracking()
    {
        startApp();
        SettingsViewModel vm(services());
        QVERIFY(!vm.demo());
        QVERIFY(!vm.dirty());
        QCOMPARE(vm.changeCount(), 0);
        QCOMPARE(vm.userName(), live().recording.userSpeakerName);

        QSignalSpy dirty(&vm, &SettingsViewModel::dirtyChanged);
        const QString original = vm.audioQuality();
        vm.setAudioQuality(QStringLiteral("low"));
        QCOMPARE(vm.changeCount(), 1);
        QVERIFY(dirty.size() >= 1);
        QVERIFY(vm.audioQualityHint().contains(QStringLiteral("24")));
        vm.setMixdownMode(QStringLiteral("manual"));
        vm.setSilenceAskMinutes(7);
        QCOMPARE(vm.changeCount(), 3);
        QVERIFY(vm.footerText().contains(QStringLiteral("3")));
        // Restored to the original: does not count as a change.
        vm.setAudioQuality(original);
        QCOMPARE(vm.changeCount(), 2);
        // Nothing has changed in core yet.
        QCOMPARE(live().recording.mixdownMode, QStringLiteral("auto"));
        QCOMPARE(live().watcher.silenceAskMinutes, 3);
    }

    void saveWritesOnlyTheDifference()
    {
        startApp();
        // Settings the window does not show must not be lost.
        tanara::app::AppSettings s = live();
        s.watcher.detectorId = QStringLiteral("linux-capture");
        s.cloud.baseUrl = QStringLiteral("http://127.0.0.1:9");
        s.processing.languageHints = {QStringLiteral("de")};
        m_app->settings()->setSettings(s);

        SettingsViewModel vm(services());
        vm.setAudioQuality(QStringLiteral("medium"));
        vm.setDetectorIntervalSec(12);

        // Meanwhile someone else saves too (e.g. the recorder / another window): the untouched field
        // stays, and so does our own unsaved change.
        tanara::app::AppSettings other = live();
        other.watcher.askStopOnCallEnd = false;
        other.watcher.silenceAskMinutes = 9;
        m_app->settings()->setSettings(other);
        QCOMPARE(vm.changeCount(), 2);
        QCOMPARE(vm.audioQuality(), QStringLiteral("medium"));
        QCOMPARE(vm.silenceAskMinutes(), 9);               // the external change appeared
        QCOMPARE(vm.askStopOnCallEnd(), false);

        QSignalSpy saved(&vm, &SettingsViewModel::saved);
        QVERIFY(vm.save());
        QCOMPARE(saved.size(), 1);
        QVERIFY(!vm.dirty());
        const tanara::app::AppSettings after = live();
        QCOMPARE(after.recording.audioQuality, QStringLiteral("medium"));
        QCOMPARE(after.watcher.detectorIntervalSec, 12);
        QCOMPARE(after.watcher.silenceAskMinutes, 9);
        QCOMPARE(after.watcher.askStopOnCallEnd, false);
        QCOMPARE(after.watcher.detectorId, QStringLiteral("linux-capture"));
        QCOMPARE(after.cloud.baseUrl, QStringLiteral("http://127.0.0.1:9"));
        QCOMPARE(after.processing.languageHints, QStringList{QStringLiteral("de")});
        // On disk too.
        QCOMPARE(settingsFile().value("recording").toObject().value("audioQuality").toString(), QStringLiteral("medium"));
        QCOMPARE(settingsFile().value("watcher").toObject().value("detectorId").toString(), QStringLiteral("linux-capture"));
        // Without a change, saving writes nothing.
        QSignalSpy changed(m_app->settings(), &tanara::app::SettingsManager::settingsChanged);
        QVERIFY(vm.save());
        QCOMPARE(changed.size(), 0);
    }

    // D5 / S3: the local speaker separation is the diarization route (draft); the segmentation list sits next to
    // the voice models.
    void localDiarizationRouteFollowsTheDraftAndSaves()
    {
        using tanara::providers::Route;
        startApp();
        QCOMPARE(live().routes.diarization.route, Route::External);   // fresh install: the STT separates them
        SettingsViewModel vm(services());
        QCOMPARE(vm.route(QStringLiteral("diarization")), QStringLiteral("external"));
        QCOMPARE(vm.route(QStringLiteral("diarization")), QStringLiteral("external"));
        QVERIFY(vm.segmentationModels());
        QCOMPARE(vm.segmentationModels()->kind(), QStringLiteral("segmentation"));
        QCOMPARE(vm.segmentationModels()->rowCount(), 2);
        QCOMPARE(vm.voiceModels()->kind(), QStringLiteral("embedding"));
        QVERIFY(!vm.dirty());

        QSignalSpy changed(&vm, &SettingsViewModel::routesChanged);
        QVERIFY(vm.setRoute(QStringLiteral("diarization"), QStringLiteral("local")));
        QCOMPARE(vm.route(QStringLiteral("diarization")), QStringLiteral("local"));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(vm.changeCount(), 1);
        QVERIFY(vm.setRoute(QStringLiteral("diarization"), QStringLiteral("local")));   // same value: nothing
        QCOMPARE(changed.size(), 1);
        QCOMPARE(live().routes.diarization.route, Route::External);   // not yet in the core
        QVERIFY(vm.save());
        QCOMPARE(live().routes.diarization.route, Route::Local);
        QCOMPARE(settingsFile().value("routes").toObject().value("diarization").toObject().value("route").toString(),
                 QStringLiteral("local"));
        QVERIFY(!settingsFile().value("processing").toObject().contains("localDiarization"));   // the old switch is gone
        QVERIFY(!vm.dirty());

        // Discard brings the saved value back.
        QVERIFY(vm.setRoute(QStringLiteral("diarization"), QStringLiteral("external")));
        QVERIFY(vm.dirty());
        vm.discard();
        QCOMPARE(vm.route(QStringLiteral("diarization")), QStringLiteral("local"));
        QVERIFY(!vm.dirty());

        // Demo states: on by default, off in B08diaroff.
        SettingsViewModel d;
        d.setDemoState(QStringLiteral("B08"));
        QCOMPARE(d.route(QStringLiteral("diarization")), QStringLiteral("local"));
        QCOMPARE(d.segmentationModels()->rowCount(), 2);
        d.setDemoState(QStringLiteral("B08diaroff"));
        QCOMPARE(d.route(QStringLiteral("diarization")), QStringLiteral("external"));
        QVERIFY(!d.dirty());
    }

    void discardRestoresEverythingIncludingTheThemePreview()
    {
        startApp();
        SettingsViewModel vm(services());
        QCOMPARE(vm.themeMode(), QStringLiteral("light"));
        vm.setThemeMode(QStringLiteral("dark"));
        QCOMPARE(AppContext::instance()->themeMode(), QStringLiteral("dark"));   // live preview
        QVERIFY(AppContext::instance()->dark());
        vm.setUserName(QStringLiteral("Valaki Más"));
        vm.setAutoRecordAll(!vm.autoRecordAll());
        QCOMPARE(vm.changeCount(), 3);

        vm.discard();
        QVERIFY(!vm.dirty());
        QCOMPARE(vm.themeMode(), QStringLiteral("light"));
        QCOMPARE(AppContext::instance()->themeMode(), QStringLiteral("light"));
        QCOMPARE(vm.userName(), live().recording.userSpeakerName);
        QCOMPARE(vm.autoRecordAll(), live().recording.autoRecordAllDevices);
    }

    void themeIsPersistedThroughTheSignal()
    {
        startApp();
        SettingsViewModel vm(services());
        QSignalSpy theme(&vm, &SettingsViewModel::themeModeSaved);
        vm.setThemeMode(QStringLiteral("dark"));
        QCOMPARE(vm.changeCount(), 1);
        QVERIFY(vm.save());
        QCOMPARE(theme.size(), 1);
        QCOMPARE(theme.first().at(0).toString(), QStringLiteral("dark"));
        QVERIFY(!vm.dirty());
        QCOMPARE(AppContext::instance()->themeMode(), QStringLiteral("dark"));
        vm.discard();                                       // after saving, discard does not restore
        QCOMPARE(AppContext::instance()->themeMode(), QStringLiteral("dark"));
    }

    // Settings > Recording: "keep the machine awake while recording" (default on) is saved.
    void keepAwakeIsSaved()
    {
        startApp();
        SettingsViewModel vm(services());
        QVERIFY(vm.keepAwake());
        QSignalSpy changed(&vm, &SettingsViewModel::recordingChanged);
        vm.setKeepAwake(false);
        QCOMPARE(changed.size(), 1);
        QVERIFY(vm.dirty());
        QVERIFY(vm.save());
        QVERIFY(!live().recording.keepAwake);
        vm.setKeepAwake(true);
        QVERIFY(vm.save());
        QVERIFY(live().recording.keepAwake);
    }

    void validationBlocksSaveButNotNavigation()
    {
        startApp();
        SettingsViewModel vm(services());
        const QString before = live().recording.userSpeakerName;
        vm.setUserName(QStringLiteral("   "));
        QVERIFY(vm.errors().contains(QStringLiteral("userName")));
        // The pages can be visited freely.
        vm.setPage(QStringLiteral("processing"));
        QCOMPARE(vm.page(), QStringLiteral("processing"));
        vm.setAudioQuality(QStringLiteral("low"));

        QSignalSpy saved(&vm, &SettingsViewModel::saved);
        QVERIFY(!vm.save());
        QCOMPARE(saved.size(), 0);
        QCOMPARE(vm.page(), QStringLiteral("people"));     // leads to the invalid field's page (the name is on the People page)
        QCOMPARE(live().recording.userSpeakerName, before);          // nothing was written
        QCOMPARE(live().recording.audioQuality, QStringLiteral("best"));

        vm.setUserName(QStringLiteral("Teszt Elek"));
        QVERIFY(vm.errors().isEmpty());
        QVERIFY(vm.save());
        QCOMPARE(live().recording.userSpeakerName, QStringLiteral("Teszt Elek"));
        QVERIFY(m_app->knownPeople().contains(QStringLiteral("Teszt Elek")));   // the path of setUserSpeakerName
        QCOMPARE(live().recording.audioQuality, QStringLiteral("low"));

        // Folders: empty / relative path is an error.
        vm.setFolder(QStringLiteral("notes"), QStringLiteral("relativ/mappa"));
        QVERIFY(vm.errors().contains(QStringLiteral("folder.notes")));
        QVERIFY(!vm.save());
        vm.setFolder(QStringLiteral("notes"), m_home->filePath(QStringLiteral("jegyzetek")));
        QVERIFY(vm.errors().isEmpty());
        QVERIFY(vm.save());
        QCOMPARE(live().paths.notesDir, m_home->filePath(QStringLiteral("jegyzetek")));
        // With TANARA_HOME the internal data folder is fixed.
        QCOMPARE(vm.folders().at(2).toMap().value("locked").toBool(), true);
        vm.setFolder(QStringLiteral("meta"), QStringLiteral("/máshova"));
        QVERIFY(!vm.dirty());
    }

    void recordingRootsList()
    {
        startApp();
        const QString primary = m_app->settings()->paths().primaryRecordingRoot();
        const QString extra = m_home->filePath(QStringLiteral("archivum"));
        const QString busy = m_home->filePath(QStringLiteral("teli"));
        QVERIFY(QDir(busy).mkpath(QStringLiteral("m1")));
        QFile f(QDir(busy).filePath(QStringLiteral("m1/meeting.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();
        FakeDialogs dialogs;
        dialogs.folderAnswer = extra;
        SettingsViewModel vm(services());
        vm.setDialogsObject(&dialogs);
        QVERIFY(vm.recordingRoots().isEmpty());
        vm.addRecordingRoot();                       // via the folder picker
        vm.addRecordingRootPath(busy);
        vm.addRecordingRootPath(extra);              // twice: ignored
        QCOMPARE(vm.recordingRoots().size(), 2);
        QCOMPARE(vm.recordingRoots().at(0).toMap().value("removable").toBool(), true);
        QCOMPARE(vm.recordingRoots().at(1).toMap().value("meetings").toInt(), 1);
        vm.removeRecordingRoot(busy);                // holds a meeting: stays
        QCOMPARE(vm.recordingRoots().size(), 2);
        vm.makePrimaryRecordingRoot(extra);          // the old primary becomes a further root
        QCOMPARE(vm.folders().at(0).toMap().value("path").toString(), extra);
        QCOMPARE(vm.recordingRoots().size(), 2);
        vm.removeRecordingRoot(primary);             // the old (empty) primary can go now
        QCOMPARE(vm.recordingRoots().size(), 1);
        QVERIFY(vm.save());
        // Inside the sandbox every root counts; the first is the primary.
        QCOMPARE(m_app->settings()->paths().recordingRoots(), (QStringList{extra, busy}));
    }

    void foldersBrowseAndUsage()
    {
        startApp();
        // Two meeting folders under the recordings.
        const QString audio = m_app->settings()->paths().primaryRecordingRoot();
        for (const char* name : {"egyik", "masik"}) {
            QVERIFY(QDir(audio).mkpath(QString::fromLatin1(name)));
            QFile f(QDir(audio).filePath(QString::fromLatin1(name) + QStringLiteral("/meeting.json")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(2500, 'x'));
        }
        FakeDialogs dialogs;
        SettingsViewModel vm(services());
        vm.setDialogsObject(&dialogs);
        QVERIFY(waitFor([&] { return !vm.folders().at(0).toMap().value("usage").toString().isEmpty(); }));
        const QString usage = vm.folders().at(0).toMap().value("usage").toString();
        QVERIFY2(usage.contains(QStringLiteral("5 kB")) && usage.contains(QStringLiteral("2")), qPrintable(usage));

        // Browse: canceling changes nothing; the chosen folder goes into the draft.
        vm.browseFolder(QStringLiteral("audio"));
        QVERIFY(!vm.dirty());
        dialogs.folderAnswer = m_home->filePath(QStringLiteral("uj-felvetelek"));
        vm.browseFolder(QStringLiteral("audio"));
        QCOMPARE(vm.changeCount(), 1);
        QCOMPARE(vm.folders().at(0).toMap().value("path").toString(), dialogs.folderAnswer);
        QVERIFY(dialogs.calls.last().startsWith(QStringLiteral("pick:")));
        vm.openFolder(QStringLiteral("notes"));
        QVERIFY(dialogs.calls.last().startsWith(QStringLiteral("open:")));
        vm.openPeople();
        QCOMPARE(dialogs.calls.last(), QStringLiteral("people"));
        vm.discard();
    }

    // ---- providers ----

    void providersComeFromTheRegistry()
    {
        startApp();
        SettingsViewModel vm(services());
        // The list is the registry's content (the real providers instead of the mock-up's).
        QStringList sttIds, llmIds;
        for (const QVariant& p : vm.stt()->providers()) sttIds << p.toMap().value("value").toString();
        for (const QVariant& p : vm.llm()->providers()) llmIds << p.toMap().value("value").toString();
        QVERIFY(sttIds.contains(QStringLiteral("soniox")));
        QVERIFY(sttIds.contains(QStringLiteral("whisper-compat")));
        QVERIFY(!sttIds.contains(tanara::cloud::ProviderId));
        QCOMPARE(llmIds, QStringList{QStringLiteral("openai-compat")});

        // The fields come from the descriptor: for Soniox address + model + required key, no advanced ones.
        QCOMPARE(vm.stt()->providerId(), QStringLiteral("soniox"));
        const QVariantList fields = vm.stt()->fields();
        QCOMPARE(fields.size(), 3);
        QCOMPARE(fieldProp(fields, "baseUrl", "type").toString(), QStringLiteral("url"));
        QCOMPARE(fieldProp(fields, "apiKey", "type").toString(), QStringLiteral("secret"));
        QCOMPARE(fieldProp(fields, "apiKey", "required").toBool(), true);
        QVERIFY(vm.stt()->advancedFields().isEmpty());
        QVERIFY(vm.stt()->testable());
        // LLM: temperature and max tokens under "Advanced"; the model is a fetchable list.
        QCOMPARE(vm.llm()->advancedFields().size(), 2);
        QCOMPARE(fieldProp(vm.llm()->fields(), "model", "dynamic").toBool(), true);
        QCOMPARE(fieldProp(vm.llm()->advancedFields(), "temperature", "decimals").toInt(), 2);
        // For a local endpoint the key placeholder: not needed.
        QVERIFY(vm.llm()->placeholder(QStringLiteral("apiKey")).contains(QStringLiteral("helyi végpont")));
        vm.llm()->setValue(QStringLiteral("baseUrl"), QStringLiteral("https://api.example.com/v1"));
        QVERIFY(!vm.llm()->placeholder(QStringLiteral("apiKey")).contains(QStringLiteral("helyi végpont")));
    }

    void switchingProviderKeepsTheOtherOnesConfig()
    {
        startApp();
        SettingsViewModel vm(services());
        SettingsProviderModel* stt = vm.stt();
        stt->setValue(QStringLiteral("model"), QStringLiteral("stt-sajat-modell"));
        stt->setValue(QStringLiteral("apiKey"), QStringLiteral("soniox-kulcs"));
        QCOMPARE(vm.changeCount(), 2);

        QSignalSpy fields(stt, &SettingsProviderModel::fieldsChanged);
        stt->setProviderId(QStringLiteral("whisper-compat"));
        QVERIFY(fields.size() >= 1);
        QCOMPARE(stt->providerId(), QStringLiteral("whisper-compat"));
        // The new provider starts with the descriptor's defaults.
        QCOMPARE(stt->value(QStringLiteral("baseUrl")).toString(), QStringLiteral("http://localhost:8000/v1"));
        QCOMPARE(stt->value(QStringLiteral("model")).toString(), QStringLiteral("whisper-1"));
        QCOMPARE(stt->fields().size(), 4);                 // address, model, language, key
        stt->setValue(QStringLiteral("language"), QStringLiteral("en"));
        QCOMPARE(vm.changeCount(), 3);                     // model, key, provider (the new config is not separate)

        // Back: the Soniox setup is still there.
        stt->setProviderId(QStringLiteral("soniox"));
        QCOMPARE(stt->value(QStringLiteral("model")).toString(), QStringLiteral("stt-sajat-modell"));
        QCOMPARE(stt->value(QStringLiteral("apiKey")).toString(), QStringLiteral("soniox-kulcs"));
        stt->setProviderId(QStringLiteral("whisper-compat"));
        QCOMPARE(stt->value(QStringLiteral("language")).toString(), QStringLiteral("en"));

        QVERIFY(vm.save());
        const tanara::app::AppSettings s = live();
        QCOMPARE(s.providers.stt.selectedId, QStringLiteral("whisper-compat"));
        QCOMPARE(s.providers.stt.configs.value(QStringLiteral("soniox")).model, QStringLiteral("stt-sajat-modell"));
        QCOMPARE(s.providers.stt.configs.value(QStringLiteral("whisper-compat")).baseUrl, QStringLiteral("http://localhost:8000/v1"));
        QCOMPARE(s.providers.stt.configs.value(QStringLiteral("whisper-compat")).extra.value(QStringLiteral("language")).toString(),
                 QStringLiteral("en"));
        // The key is in the KeyStore, not in the settings file.
        QCOMPARE(m_app->secret(tanara::app::keys::SonioxApiKey), QStringLiteral("soniox-kulcs"));
        QFile f(m_app->settings()->settingsFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(!f.readAll().contains("soniox-kulcs"));

        // Reopened, the same shows.
        SettingsViewModel again(services());
        QVERIFY(!again.dirty());
        QCOMPARE(again.stt()->providerId(), QStringLiteral("whisper-compat"));
        again.stt()->setProviderId(QStringLiteral("soniox"));
        QCOMPARE(again.stt()->value(QStringLiteral("apiKey")).toString(), QStringLiteral("soniox-kulcs"));
    }

    void fieldValidation()
    {
        startApp();
        SettingsViewModel vm(services());
        vm.llm()->setValue(QStringLiteral("baseUrl"), QStringLiteral("localhost:1234"));
        QVERIFY(!vm.llm()->fieldError(QStringLiteral("baseUrl")).isEmpty());
        vm.setPage(QStringLiteral("general"));
        QSignalSpy jumps(&vm, &SettingsViewModel::sectionRequested);
        QVERIFY(!vm.save());
        QCOMPARE(vm.page(), QStringLiteral("processing"));
        QCOMPARE(vm.section(), QStringLiteral("summary"));  // the LLM card's section
        QCOMPARE(jumps.size(), 1);
        QCOMPARE(jumps.at(0).at(0).toString(), QStringLiteral("summary"));
        vm.llm()->setValue(QStringLiteral("baseUrl"), QString());
        QVERIFY(!vm.llm()->fieldError(QStringLiteral("baseUrl")).isEmpty());   // required
        vm.llm()->setValue(QStringLiteral("baseUrl"), QStringLiteral("http://localhost:1234/v1"));
        QVERIFY(vm.llm()->fieldError(QStringLiteral("baseUrl")).isEmpty());
        vm.llm()->setValue(QStringLiteral("maxTokens"), 3);
        QVERIFY(!vm.llm()->fieldError(QStringLiteral("maxTokens")).isEmpty());
        vm.llm()->setValue(QStringLiteral("maxTokens"), 30000);
        vm.llm()->setValue(QStringLiteral("temperature"), 0.35);
        QVERIFY(vm.save());
        QCOMPARE(live().providers.llm.selected().maxTokens, 30000);
        QCOMPARE(live().providers.llm.selected().temperature, 0.35);

        // A temperature of 0 is a valid value: after saving 0 shows (not the default).
        vm.llm()->setValue(QStringLiteral("temperature"), 0.0);
        QVERIFY(vm.save());
        QCOMPARE(live().providers.llm.selected().temperature, 0.0);
        QCOMPARE(vm.llm()->value(QStringLiteral("temperature")).toDouble(), 0.0);
        QVERIFY(!vm.dirty());

        // While typing the space stays in the field; saving trims it.
        vm.llm()->setValue(QStringLiteral("model"), QStringLiteral("sajat modell "));
        QCOMPARE(vm.llm()->value(QStringLiteral("model")).toString(), QStringLiteral("sajat modell "));
        QVERIFY(vm.save());
        QCOMPARE(live().providers.llm.selected().model, QStringLiteral("sajat modell"));

        // A previously stored out-of-range value does not block saving the other settings.
        tanara::app::AppSettings s = live();
        s.providers.llm.configs[s.providers.llm.selectedId].maxTokens = 100;
        m_app->settings()->setSettings(s);
        QVERIFY(vm.llm()->fieldError(QStringLiteral("maxTokens")).isEmpty());
        vm.setAudioQuality(QStringLiteral("low"));
        QVERIFY(vm.save());
        QCOMPARE(live().providers.llm.selected().maxTokens, 100);
    }

    void warnDotFollowsReadiness()
    {
        startApp();
        SettingsViewModel vm(services());
        // Fresh install: no Soniox key, so a dot next to "Services".
        QVERIFY(vm.servicesWarn());
        QSignalSpy warn(&vm, &SettingsViewModel::servicesWarnChanged);
        vm.stt()->setValue(QStringLiteral("apiKey"), QStringLiteral("k"));
        QVERIFY(!vm.servicesWarn());                       // already reacts to the draft too
        QCOMPARE(warn.size(), 1);
        vm.stt()->setValue(QStringLiteral("apiKey"), QString());
        QVERIFY(vm.servicesWarn());
    }

    void connectionTestStates()
    {
        startApp();
        FakeHttp http;
        http.handler = [](const QString&, const QByteArray& auth) {
            if (auth != "Bearer jo-kulcs") return FakeReply{401, "{}"};
            return FakeReply{200, "{\"data\":[{\"id\":\"modell-b\"},{\"id\":\"modell-a\"}]}"};
        };
        SettingsViewModel vm(services());
        vm.stt()->setValue(QStringLiteral("apiKey"), QStringLiteral("x"));   // STT must not disturb the dot
        SettingsProviderModel* llm = vm.llm();
        llm->setValue(QStringLiteral("baseUrl"), http.base());
        llm->setValue(QStringLiteral("model"), QStringLiteral("modell-a"));
        llm->setValue(QStringLiteral("apiKey"), QStringLiteral("rossz"));
        QCOMPARE(llm->testState(), QString());

        // 1) rejected key: an error with a human sentence and a technical code, the dot.
        QSignalSpy test(llm, &SettingsProviderModel::testChanged);
        llm->test();
        QCOMPARE(llm->testState(), QStringLiteral("testing"));
        QVERIFY(waitFor([&] { return llm->testState() != QLatin1String("testing"); }));
        QCOMPARE(llm->testState(), QStringLiteral("failed"));
        QCOMPARE(llm->errorCode(), QStringLiteral("HTTP 401"));
        QVERIFY(llm->errorText().contains(QStringLiteral("kulcs")));
        QVERIFY(!llm->statusText().isEmpty());
        QVERIFY(vm.servicesWarn());
        QCOMPARE(http.paths.last(), QStringLiteral("/v1/models"));
        QCOMPARE(http.auths.last(), QByteArray("Bearer rossz"));   // the DRAFT's key goes, before saving

        // 2) changing any field makes the result stale.
        llm->setValue(QStringLiteral("apiKey"), QStringLiteral("jo-kulcs"));
        QCOMPARE(llm->testState(), QString());
        QVERIFY(!vm.servicesWarn());

        // 3) success: state + response time; the model list also fills the "Fetch" list.
        llm->test();
        QVERIFY(waitFor([&] { return llm->testState() == QLatin1String("ok"); }));
        QVERIFY(llm->statusText().contains(QStringLiteral("ms")));
        QVERIFY(llm->errorText().isEmpty());
        QVERIFY(llm->warningText().isEmpty());
        QCOMPARE(llm->options(QStringLiteral("model")),
                 (QStringList{QStringLiteral("modell-a"), QStringLiteral("modell-b")}));

        // 4) success, but the configured model is not in the list: a warning.
        llm->setValue(QStringLiteral("model"), QStringLiteral("ismeretlen"));
        llm->test();
        QVERIFY(waitFor([&] { return llm->testState() == QLatin1String("ok"); }));
        QVERIFY(llm->warningText().contains(QStringLiteral("ismeretlen")));

        // 5) "Fetch": a separate action, does not overwrite the test's state.
        http.handler = [](const QString&, const QByteArray&) {
            return FakeReply{200, "{\"data\":[{\"id\":\"friss-modell\"}]}"};
        };
        llm->fetchModels();
        QVERIFY(llm->fetching());
        QVERIFY(waitFor([&] { return !llm->fetching(); }));
        QCOMPARE(llm->options(QStringLiteral("model")), QStringList{QStringLiteral("friss-modell")});
        QVERIFY(llm->fetchError().isEmpty());
        QCOMPARE(llm->testState(), QStringLiteral("ok"));

        // 6) the server stops: "unreachable", ECONNREFUSED; the fetch error has its own text.
        http.close();
        llm->setValue(QStringLiteral("baseUrl"), m_closedPortBase);
        llm->test();
        QVERIFY(waitFor([&] { return llm->testState() == QLatin1String("failed"); }));
        QCOMPARE(llm->errorCode(), QStringLiteral("ECONNREFUSED"));
        QVERIFY(!llm->errorText().isEmpty());
        llm->fetchModels();
        QVERIFY(waitFor([&] { return !llm->fetching(); }));
        QVERIFY(!llm->fetchError().isEmpty());

        // The test saved nothing.
        QVERIFY(vm.dirty());
        QVERIFY(m_app->secret(tanara::app::keys::LlmApiKey).isEmpty());
        vm.discard();
        QCOMPARE(llm->testState(), QString());
    }

    // ---- devices ----

    void deviceRenameRoundTrip()
    {
        startApp();
        m_app->refreshDevices();
        SettingsViewModel vm(services());
        if (vm.deviceCount() == 0)
            QSKIP("Ezen a gépen nincs hangeszköz (a demó-lista átnevezését a demoDeviceRename fedi).");

        const QString raw = row(vm, 0, "rawName").toString();
        const QString factory = row(vm, 0, "defaultName").toString();
        QCOMPARE(row(vm, 0, "name").toString(), factory);
        QCOMPARE(row(vm, 0, "renamed").toBool(), false);

        vm.renameDevice(0, QStringLiteral("  Saját  mikrofonom "));
        QCOMPARE(row(vm, 0, "name").toString(), QStringLiteral("Saját mikrofonom"));
        QCOMPARE(row(vm, 0, "renamed").toBool(), true);
        QCOMPARE(vm.changeCount(), 1);
        // Until saving, core does not know about it.
        QCOMPARE(tanara::library::devicenames::displayName(raw), factory);

        QVERIFY(vm.save());
        QCOMPARE(live().recording.deviceNames.value(raw), QStringLiteral("Saját mikrofonom"));
        QCOMPARE(tanara::library::devicenames::displayName(raw), QStringLiteral("Saját mikrofonom"));
        // The tracks' name follows too (Tracks tab).
        tanara::Track t;
        t.id = QStringLiteral("t");
        t.deviceName = raw;
        t.kind = tanara::TrackKind::Other;
        QCOMPARE(tanara::library::tracknames::friendlyNames({t}).first(), QStringLiteral("Saját mikrofonom"));

        // New window: the saved name shows.
        SettingsViewModel again(services());
        QCOMPARE(row(again, 0, "name").toString(), QStringLiteral("Saját mikrofonom"));

        // Empty name goes back to the factory name; the key leaves the settings.
        again.renameDevice(0, QString());
        QCOMPARE(row(again, 0, "name").toString(), factory);
        QCOMPARE(again.changeCount(), 1);
        QVERIFY(again.save());
        QVERIFY(!live().recording.deviceNames.contains(raw));
        QCOMPARE(tanara::library::devicenames::displayName(raw), factory);
        // The other (still open) view model took it over too.
        QCOMPARE(row(vm, 0, "name").toString(), factory);
    }

    void tracksTabFollowsADeviceRename()
    {
        // The "Tracks" tab's model switches to the saved device name at once (without a restart).
        startApp();
        const QString raw = QStringLiteral("Vonalbemenet - Analóg sztereó");
        tanara::Meeting m = m_app->store()->createMeeting(QStringLiteral("Próba"));
        tanara::Track t;
        t.id = QStringLiteral("line");
        t.deviceName = raw;
        t.kind = tanara::TrackKind::Other;
        t.file = QStringLiteral("track_line.wav");
        t.active = true;
        m.tracks = {t};
        QFile f(QDir(m.folder).filePath(t.file));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("nem valódi hang");
        f.close();
        m_app->store()->saveMeeting(m);

        TrackListModel tracks;
        tracks.setFacade(services().tracks(QString(), nullptr));
        tracks.setMeetingId(m.id);
        QCOMPARE(tracks.rowCount(), 1);
        const int nameRole = tracks.roleNames().key("displayName");
        QCOMPARE(tracks.data(tracks.index(0, 0), nameRole).toString(), QStringLiteral("Vonalbemenet"));

        tanara::app::AppSettings s = live();
        s.recording.deviceNames.insert(raw, QStringLiteral("Gitár"));
        m_app->settings()->setSettings(s);                 // that is what saving Settings does
        QCOMPARE(tracks.data(tracks.index(0, 0), nameRole).toString(), QStringLiteral("Gitár"));

        s.recording.deviceNames.remove(raw);
        m_app->settings()->setSettings(s);
        QCOMPARE(tracks.data(tracks.index(0, 0), nameRole).toString(), QStringLiteral("Vonalbemenet"));
    }

    void demoDeviceRenameAndSelection()
    {
        SettingsViewModel vm;
        vm.setDemoState(QStringLiteral("B01"));
        QCOMPARE(vm.changeCount(), 0);
        const QString factory = row(vm, 3, "defaultName").toString();
        QCOMPARE(factory, QStringLiteral("Kanto YU4"));
        vm.renameDevice(3, QStringLiteral("Nappali hangfal"));
        QCOMPARE(row(vm, 3, "name").toString(), QStringLiteral("Nappali hangfal"));
        QCOMPARE(vm.changeCount(), 1);
        vm.renameDevice(3, factory);                       // renaming to the factory name = no own name
        QCOMPARE(row(vm, 3, "renamed").toBool(), false);
        QCOMPARE(vm.changeCount(), 0);

        QCOMPARE(row(vm, 1, "selected").toBool(), false);
        vm.toggleDevice(1);
        QCOMPARE(row(vm, 1, "selected").toBool(), true);
        QCOMPARE(vm.changeCount(), 1);
        vm.toggleDevice(1);
        QCOMPARE(vm.changeCount(), 0);
    }

    void defaultSourcesAreSavedForTheRecorder()
    {
        startApp();
        m_app->refreshDevices();
        SettingsViewModel vm(services());
        if (vm.deviceCount() == 0)
            QSKIP("Ezen a gépen nincs hangeszköz.");
        const QString raw = row(vm, 0, "rawName").toString();
        const bool was = row(vm, 0, "selected").toBool();
        vm.toggleDevice(0);
        QCOMPARE(vm.changeCount(), 1);
        QVERIFY(vm.save());
        QCOMPARE(m_app->lastUsedDeviceNames().contains(raw), !was);
    }

    // ---- call watcher ----

    void watchedApps()
    {
        startApp();
        SettingsViewModel vm(services());
        const int before = vm.watchedApps().size();
        QVERIFY(before > 0);
        QVERIFY(!vm.addWatchedApp(QStringLiteral("  ")).isEmpty());        // empty
        QVERIFY(!vm.addWatchedApp(QStringLiteral("ZOOM")).isEmpty());      // already listed
        QCOMPARE(vm.addWatchedApp(QStringLiteral(" Signal ")), QString());
        QCOMPARE(vm.watchedApps().size(), before + 1);
        QCOMPARE(vm.watchedApps().last().toMap().value("match").toString(), QStringLiteral("signal"));
        QCOMPARE(vm.changeCount(), 1);
        // The list of currently running processes does not include the ones already watched.
        for (const QVariant& a : vm.runningApps())
            QVERIFY(!a.toMap().value("match").toString().contains(QStringLiteral("signal")));

        // Cannot be saved with an empty list (the watcher would signal nothing).
        while (!vm.watchedApps().isEmpty()) vm.removeWatchedApp(0);
        QVERIFY(vm.errors().contains(QStringLiteral("watchedApps")));
        QVERIFY(!vm.save());
        QCOMPARE(vm.page(), QStringLiteral("recording"));
        QCOMPARE(vm.section(), QStringLiteral("watcher"));
        QCOMPARE(vm.addWatchedApp(QStringLiteral("teams")), QString());
        QVERIFY(vm.save());
        QCOMPARE(live().watcher.knownCallApps, QStringList{QStringLiteral("teams")});
    }

    void liveCallLineFollowsTheDetector()
    {
        startApp();
        SettingsViewModel vm(services());
        auto* detector = new FakeDetector;
        vm.setDetectorForTest(detector);                   // the view model owns it
        QVERIFY(!vm.liveCallActive());

        detector->signal.active = true;
        detector->signal.appId = QStringLiteral("teams");
        detector->signal.appName = QStringLiteral("Microsoft Teams");
        vm.setWatching(true);
        QVERIFY(waitFor([&] { return vm.liveCallActive(); }));
        QVERIFY(vm.detectorAvailable());
        QCOMPARE(vm.liveCallApp(), QStringLiteral("Microsoft Teams"));
        QCOMPARE(detector->configured, live().watcher.knownCallApps);   // asks with the draft's list
        bool highlighted = false;
        for (const QVariant& a : vm.watchedApps())
            if (a.toMap().value("match").toString() == QLatin1String("teams"))
                highlighted = a.toMap().value("active").toBool();
        QVERIFY(highlighted);
        // The page disappears: no more polling.
        vm.setWatching(false);
        QVERIFY(!vm.watching());
    }

    void autostartSwitchNeverTouchesTheRealEntryInASandbox()
    {
        startApp();
        const QString real = tanara::watcher::autostart::watcherEntryPath();
        const bool existed = !real.isEmpty() && QFile::exists(real);
        const QDateTime stamp = existed ? QFileInfo(real).lastModified() : QDateTime();
        SettingsViewModel vm(services());
        vm.setWatcherAutostart(!vm.watcherAutostart());
        QVERIFY(vm.save());
        QCOMPARE(live().watcher.autostart, vm.watcherAutostart());   // the setting is saved...
        if (!real.isEmpty()) {                                      // ...but the real file is untouched
            QCOMPARE(QFile::exists(real), existed);
            if (existed) QCOMPARE(QFileInfo(real).lastModified(), stamp);
        }
    }

    // ---- prompts ----

    void promptsModifiedAndReset()
    {
        startApp();
        SettingsViewModel vm(services());
        QCOMPARE(vm.promptIndex(), 0);
        const QString def = vm.promptText();
        QVERIFY(def.contains(QStringLiteral("{{LANGUAGE}}")));
        QVERIFY(!vm.promptModified());
        QVERIFY(live().processing.summaryPrompt.isEmpty());

        // Text that differs only in whitespace is not a "modification".
        vm.setPromptText(def + QStringLiteral("\n"));
        QVERIFY(!vm.promptModified());
        QVERIFY(!vm.dirty());

        QSignalSpy textChanged(&vm, &SettingsViewModel::promptTextChanged);
        vm.setPromptText(def + QStringLiteral("\n9. Légy tömör."));
        QVERIFY(vm.promptModified());
        QCOMPARE(vm.promptTabs().at(0).toMap().value("modified").toBool(), true);
        QCOMPARE(vm.promptTabs().at(1).toMap().value("modified").toBool(), false);
        QCOMPARE(vm.changeCount(), 1);
        QCOMPARE(textChanged.size(), 0);                   // the editor's own write does not come back

        // Another tab: the text stays, the schema belongs to the tab.
        vm.setPromptIndex(4);                              // per-topic analysis
        QCOMPARE(textChanged.size(), 1);
        QVERIFY(!vm.promptModified());
        QCOMPARE(vm.schemaKind(), QStringLiteral("markdown"));
        vm.setPromptText(vm.promptText() + QStringLiteral("\nMég egy szabály."));
        QCOMPARE(vm.changeCount(), 2);
        vm.setPromptIndex(0);
        QVERIFY(vm.promptText().endsWith(QStringLiteral("9. Légy tömör.")));

        QVERIFY(vm.save());
        QVERIFY(live().processing.summaryPrompt.endsWith(QStringLiteral("9. Légy tömör.")));
        QVERIFY(live().processing.topicExtractionPrompt.isEmpty());   // untouched: empty (the code default applies)
        QVERIFY(live().processing.topicAnalysisPrompt.endsWith(QStringLiteral("Még egy szabály.")));
        QVERIFY(vm.promptModified());                      // still "modified" after saving
        QVERIFY(!vm.dirty());

        // Reset: the built-in text, and empty again in the settings.
        vm.resetPrompt();
        QCOMPARE(vm.promptText(), def);
        QVERIFY(!vm.promptModified());
        QCOMPARE(vm.changeCount(), 1);
        QVERIFY(vm.save());
        QVERIFY(live().processing.summaryPrompt.isEmpty());

        vm.setSummaryLanguage(QStringLiteral("angol"));
        QVERIFY(vm.save());
        QCOMPARE(live().processing.summaryLanguage, QStringLiteral("angol"));
        vm.setSummaryLanguage(QStringLiteral("   "));        // no empty target language
        QCOMPARE(vm.summaryLanguage(), QStringLiteral("angol"));
    }

    // The quick summary's three prompts (single-pass / part note / merge) and the groups: each
    // prompt is saved to its own settings key and loaded back from there.
    void promptGroupsAndNewOverrides()
    {
        startApp();
        SettingsViewModel vm(services());
        const QVariantList tabs = vm.promptTabs();
        QStringList ids, groups;
        for (const QVariant& t : tabs) {
            ids << t.toMap().value("id").toString();
            groups << t.toMap().value("group").toString();
        }
        QCOMPARE(ids, QStringList({"single", "notes", "merge", "topic", "analysis"}));
        QCOMPARE(groups, QStringList({"quick", "quick", "quick", "topics", "topics"}));
        const QVariantList grp = vm.promptGroups();
        QCOMPARE(grp.at(0).toMap().value("tabs").toList(), QVariantList({0, 1, 2}));
        QCOMPARE(grp.at(1).toMap().value("tabs").toList(), QVariantList({3, 4}));
        QVERIFY(!grp.at(0).toMap().value("note").toString().isEmpty());

        // Group switch: the group's first prompt; back: the quick group's first prompt.
        vm.setPromptGroup(QStringLiteral("topics"));
        QCOMPARE(vm.promptIndex(), 3);
        QCOMPARE(vm.promptGroup(), QStringLiteral("topics"));
        vm.setPromptGroup(QStringLiteral("quick"));
        QCOMPARE(vm.promptIndex(), 0);

        // Part note: its own output schema (note headers), the built-in text uses {{LANGUAGE}}.
        vm.setPromptIndex(1);
        QCOMPARE(vm.promptGroup(), QStringLiteral("quick"));
        QVERIFY(vm.promptText().contains(QStringLiteral("{{LANGUAGE}}")));
        QVERIFY(!vm.schemaSummary().isEmpty());
        const QString notesDef = vm.promptText();
        vm.setPromptText(notesDef + QStringLiteral("\nKeep every point short."));
        QVERIFY(vm.promptModified());
        vm.setPromptIndex(2);
        QVERIFY(!vm.promptModified());
        QVERIFY(!vm.schemaBody().isEmpty());
        vm.setPromptText(vm.promptText() + QStringLiteral("\nAt most five decisions."));
        QCOMPARE(vm.changeCount(), 2);
        QVERIFY(vm.save());
        QVERIFY(live().processing.notesPrompt.endsWith(QStringLiteral("Keep every point short.")));
        QVERIFY(live().processing.mergePrompt.endsWith(QStringLiteral("At most five decisions.")));
        QVERIFY(live().processing.summaryPrompt.isEmpty());           // the single-pass one is untouched

        // New view model: the overrides load back, with a "modified" indicator.
        SettingsViewModel again(services());
        QCOMPARE(again.promptTabs().at(1).toMap().value("modified").toBool(), true);
        QCOMPARE(again.promptTabs().at(2).toMap().value("modified").toBool(), true);
        QCOMPARE(again.promptTabs().at(0).toMap().value("modified").toBool(), false);
        again.setPromptIndex(1);
        QVERIFY(again.promptText().endsWith(QStringLiteral("Keep every point short.")));

        // Reset: the setting is empty again (the built-in applies).
        again.resetPrompt();
        QVERIFY(again.save());
        QVERIFY(live().processing.notesPrompt.isEmpty());
        QVERIFY(!live().processing.mergePrompt.isEmpty());

        // Discard: the unsaved edit disappears.
        again.setPromptIndex(2);
        again.setPromptText(QStringLiteral("eldobandó"));
        QVERIFY(again.dirty());
        again.discard();
        QVERIFY(again.promptText().endsWith(QStringLiteral("At most five decisions.")));
    }

    // The model's "thinking" (providers::ProviderConfig::reasoning) in the LLM card's Advanced part.
    void reasoningRoundTrip()
    {
        startApp();
        SettingsViewModel vm(services());
        QVERIFY(vm.llm()->reasoningAvailable());
        QVERIFY(!vm.stt()->reasoningAvailable());           // the transcriber has none
        QCOMPARE(vm.llm()->reasoning(), QStringLiteral("auto"));
        QSignalSpy values(vm.llm(), &SettingsProviderModel::valuesChanged);
        vm.llm()->setReasoning(QStringLiteral("on"));
        QCOMPARE(values.size(), 1);
        QCOMPARE(vm.llm()->reasoning(), QStringLiteral("on"));
        QCOMPARE(vm.changeCount(), 1);
        QVERIFY(vm.save());
        QCOMPARE(live().providers.llm.selected().reasoning, QStringLiteral("on"));
        vm.llm()->setReasoning(QStringLiteral("off"));
        QVERIFY(vm.save());
        QCOMPARE(live().providers.llm.selected().reasoning, QStringLiteral("off"));
        // Unknown value: automatic.
        vm.llm()->setReasoning(QStringLiteral("valami"));
        QCOMPARE(vm.llm()->reasoning(), QStringLiteral("auto"));
        QVERIFY(vm.save());
        QCOMPARE(live().providers.llm.selected().reasoning, QStringLiteral("auto"));

        // New view model with the saved value; discard restores.
        tanara::app::AppSettings s = live();
        s.providers.llm.configs[s.providers.llm.selectedId].reasoning = QStringLiteral("on");
        m_app->settings()->setSettings(s);
        SettingsViewModel again(services());
        QCOMPARE(again.llm()->reasoning(), QStringLiteral("on"));
        again.llm()->setReasoning(QStringLiteral("auto"));
        QVERIFY(again.dirty());
        again.discard();
        QCOMPARE(again.llm()->reasoning(), QStringLiteral("on"));
    }

    // The model's context (providers::ProviderConfig::contextLength): automatic or fixed tokens.
    void contextLengthRoundTrip()
    {
        startApp();
        SettingsViewModel vm(services());
        QVERIFY(vm.llm()->contextAvailable());
        QVERIFY(!vm.stt()->contextAvailable());
        QCOMPARE(vm.llm()->contextLength(), 0);                // automatic
        const QVariantList opts = vm.llm()->contextOptions();
        QCOMPARE(opts.first().toMap().value("value").toString(), QStringLiteral("0"));
        bool has16k = false;
        for (const QVariant& o : opts) has16k = has16k || o.toMap().value("value").toString() == QLatin1String("16384");
        QVERIFY(has16k);
        QSignalSpy values(vm.llm(), &SettingsProviderModel::valuesChanged);
        vm.llm()->setContextLength(16384);
        QCOMPARE(values.size(), 1);
        QCOMPARE(vm.changeCount(), 1);
        QVERIFY(vm.save());
        QCOMPARE(live().providers.llm.selected().contextLength, 16384);
        vm.llm()->setContextLength(-5);                        // invalid: automatic
        QCOMPARE(vm.llm()->contextLength(), 0);
        QVERIFY(vm.save());
        QCOMPARE(live().providers.llm.selected().contextLength, 0);

        // A custom value from the file stays, and is added to the selectable values.
        tanara::app::AppSettings s = live();
        s.providers.llm.configs[s.providers.llm.selectedId].contextLength = 12345;
        m_app->settings()->setSettings(s);
        SettingsViewModel again(services());
        QCOMPARE(again.llm()->contextLength(), 12345);
        bool hasCustom = false;
        for (const QVariant& o : again.llm()->contextOptions())
            hasCustom = hasCustom || o.toMap().value("value").toString() == QLatin1String("12345");
        QVERIFY(hasCustom);
        again.llm()->setContextLength(0);
        QVERIFY(again.dirty());
        again.discard();
        QCOMPARE(again.llm()->contextLength(), 12345);
        // The server data is empty until asked (no native API at the test address).
        QVERIFY(again.llm()->serverInfo().isEmpty());
        QCOMPARE(again.llm()->serverLoadedContext(), -1);
    }

    // ---- pages and jump points ----

    void pagesAndSections()
    {
        startApp();
        SettingsViewModel vm(services());
        QCOMPARE(vm.page(), QStringLiteral("processing"));          // the first opening
        QVERIFY(vm.section().isEmpty());
        QCOMPARE(SettingsViewModel::sectionsOf(QStringLiteral("general")),
                 (QStringList{"appearance", "folders", "account"}));
        QCOMPARE(SettingsViewModel::sectionsOf(QStringLiteral("recording")), (QStringList{"devices", "watcher"}));
        QCOMPARE(SettingsViewModel::sectionsOf(QStringLiteral("processing")),
                 (QStringList{"stt", "diar", "voice", "summary", "tags"}));
        QVERIFY(SettingsViewModel::sectionsOf(QStringLiteral("cloud")).isEmpty());
        QVERIFY(SettingsViewModel::sectionsOf(QStringLiteral("people")).isEmpty());

        // The five page keys; the old ones are not pages any more (openPage maps them).
        for (const char* p : {"general", "recording", "processing", "cloud", "people"}) {
            vm.setPage(QString::fromLatin1(p));
            QCOMPARE(vm.page(), QString::fromLatin1(p));
        }
        for (const char* old : {"watcher", "services", "speakers", "summary"}) {
            vm.setPage(QString::fromLatin1(old));
            QCOMPARE(vm.page(), QStringLiteral("people"));
        }

        // The section belongs to the page; a page change clears it.
        QSignalSpy sectionSpy(&vm, &SettingsViewModel::sectionChanged);
        vm.setPage(QStringLiteral("general"));
        vm.setSection(QStringLiteral("tags"));                     // not General's
        QVERIFY(vm.section().isEmpty());
        vm.setSection(QStringLiteral("folders"));
        QCOMPARE(vm.section(), QStringLiteral("folders"));
        QCOMPARE(sectionSpy.size(), 1);
        vm.setPage(QStringLiteral("processing"));
        QVERIFY(vm.section().isEmpty());
        QCOMPARE(sectionSpy.size(), 2);

        // A sidebar jump: page + section + a request to scroll (and flash).
        QSignalSpy jumps(&vm, &SettingsViewModel::sectionRequested);
        vm.showSection(QStringLiteral("recording"), QStringLiteral("watcher"));
        QCOMPARE(vm.page(), QStringLiteral("recording"));
        QCOMPARE(vm.section(), QStringLiteral("watcher"));
        QCOMPARE(jumps.size(), 1);
        QCOMPARE(jumps.at(0).at(0).toString(), QStringLiteral("watcher"));
        QCOMPARE(jumps.at(0).at(1).toBool(), true);
        QVERIFY(vm.focusField().isEmpty());

        // "" reopens the last page without scrolling.
        vm.openPage(QString());
        QCOMPARE(vm.page(), QStringLiteral("recording"));
        QCOMPARE(vm.section(), QStringLiteral("watcher"));
        QCOMPARE(jumps.size(), 1);
    }

    void legacyTargetsMapToPageAndSection_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("arg");
        QTest::addColumn<QString>("expPage");
        QTest::addColumn<QString>("expSection");
        QTest::addColumn<QString>("expFocus");
        const auto row = [](const char* p, const char* a, const char* ep, const char* es, const char* ef) {
            QTest::addRow("%s+%s", p, a) << QString::fromLatin1(p) << QString::fromLatin1(a) << QString::fromLatin1(ep)
                                         << QString::fromLatin1(es) << QString::fromLatin1(ef);
        };
        // The old six-page names and ShellActions.openSettings' fixTargets.
        row("general", "", "general", "appearance", "");
        row("recording", "", "recording", "devices", "");
        row("watcher", "", "recording", "watcher", "");
        row("providers", "stt", "processing", "stt", "stt");
        row("services", "stt", "processing", "stt", "stt");
        row("providers", "llm", "processing", "summary", "llm");
        row("services", "llm", "processing", "summary", "llm");
        row("providers", "", "processing", "stt", "");
        row("services", "", "processing", "summary", "");
        row("summary", "", "processing", "summary", "");
        row("speakers", "", "processing", "diar", "");
        row("speakers", "voiceModels", "processing", "voice", "");
        row("voiceModels", "", "processing", "voice", "");
        row("diarization", "", "processing", "diar", "");
        row("cloud", "", "cloud", "", "");
        row("people", "", "people", "", "");
        // The new keys: page + its own section; a foreign section is dropped.
        row("processing", "tags", "processing", "tags", "");
        row("general", "folders", "general", "folders", "");
        row("recording", "watcher", "recording", "watcher", "");
        row("processing", "folders", "processing", "", "");
        row("processing", "stt", "processing", "stt", "");       // a section, not the legacy key focus
        // Unknown: stays where it was (General here).
        row("nincs-ilyen", "", "general", "", "");
    }
    void legacyTargetsMapToPageAndSection()
    {
        QFETCH(QString, page);
        QFETCH(QString, arg);
        QFETCH(QString, expPage);
        QFETCH(QString, expSection);
        QFETCH(QString, expFocus);
        SettingsViewModel vm;
        vm.setDemoState(QStringLiteral("B01"));
        vm.setPage(QStringLiteral("general"));
        QSignalSpy jumps(&vm, &SettingsViewModel::sectionRequested);
        vm.openPage(page, arg);
        QCOMPARE(vm.page(), expPage);
        QCOMPARE(vm.section(), expSection);
        QCOMPARE(vm.focusField(), expFocus);
        QCOMPARE(jumps.size(), 1);
        QCOMPARE(jumps.at(0).at(0).toString(), expSection);
    }

    void demoStatesNameTheirSection_data()
    {
        QTest::addColumn<QString>("state");
        QTest::addColumn<QString>("expPage");
        QTest::addColumn<QString>("expSection");
        QTest::addRow("B01") << "B01" << "general" << "appearance";
        QTest::addRow("B03") << "B03" << "recording" << "watcher";
        QTest::addRow("B05") << "B05" << "processing" << "summary";
        QTest::addRow("B04embedding") << "B04embedding" << "processing" << "tags";
        QTest::addRow("cloudSelected") << "cloudSelected" << "processing" << "stt";
        QTest::addRow("B08") << "B08" << "processing" << "voice";
        QTest::addRow("B08segmissing") << "B08segmissing" << "processing" << "diar";
        QTest::addRow("B08backfill") << "B08backfill" << "people" << "";
    }
    void demoStatesNameTheirSection()
    {
        QFETCH(QString, state);
        QFETCH(QString, expPage);
        QFETCH(QString, expSection);
        SettingsViewModel vm;
        vm.setDemoState(state);
        QCOMPARE(vm.page(), expPage);
        QCOMPARE(vm.section(), expSection);
    }

    // ---- deep link ----

    void deepLinkHighlightsAndReturnsAfterSave()
    {
        startApp();
        SettingsViewModel vm(services());
        // The ShellActions page names (the whole table: legacyTargetsMapToPageAndSection).
        vm.openPage(QStringLiteral("watcher"));
        QCOMPARE(vm.page(), QStringLiteral("recording"));
        vm.openPage(QStringLiteral("recording"));
        QCOMPARE(vm.page(), QStringLiteral("recording"));
        vm.openPage(QStringLiteral("summary"));
        QCOMPARE(vm.page(), QStringLiteral("processing"));
        QCOMPARE(vm.section(), QStringLiteral("summary"));
        vm.openPage(QString());                            // empty: stays where it was
        QCOMPARE(vm.page(), QStringLiteral("processing"));
        QCOMPARE(vm.section(), QStringLiteral("summary"));
        vm.openPage(QStringLiteral("cloud"));              // the Cloud page, no draft change
        QCOMPARE(vm.page(), QStringLiteral("cloud"));
        QVERIFY(!vm.dirty());
        QVERIFY(vm.focusField().isEmpty());

        QSignalSpy back(&vm, &SettingsViewModel::returnRequested);
        vm.openPage(QStringLiteral("providers"), QStringLiteral("stt"));
        QCOMPARE(vm.page(), QStringLiteral("processing"));
        QCOMPARE(vm.section(), QStringLiteral("stt"));
        QCOMPARE(vm.focusField(), QStringLiteral("stt"));
        QVERIFY(vm.stt()->highlighted());
        QVERIFY(vm.focusBannerText().contains(QStringLiteral("átírás"), Qt::CaseInsensitive));

        // Saving something else while the key is missing: the highlight stays, no return.
        vm.setAudioQuality(QStringLiteral("high"));
        QVERIFY(vm.save());
        QCOMPARE(back.size(), 0);
        QCOMPARE(vm.focusField(), QStringLiteral("stt"));

        // The gap is gone: after saving, back to the meeting.
        vm.stt()->setValue(QStringLiteral("apiKey"), QStringLiteral("soniox-kulcs"));
        QVERIFY(vm.save());
        QCOMPARE(back.size(), 1);
        QVERIFY(vm.focusField().isEmpty());
        QVERIFY(!vm.stt()->highlighted());
        QVERIFY(m_app->canRun(tanara::providers::WorkflowStep::Transcribe, QString()).blockerKind
                != tanara::providers::BlockerKind::ProviderConfig);
    }

    // ---- Tanara Cloud --------------------------------------------------------------------

    // S2: with the live cloud the cards' routes go to Cloud and back (the serviceMode tab is gone).
    void cloudRoutesFromTheCards()
    {
        startApp("live");
        if (!m_app->cloudLive())
            QSKIP("A build Tanara Cloud kliens nélkül készült.");
        SettingsViewModel vm(services());
        // A fresh install with the live cloud: stt / llm ask per run (an own provider and the cloud are both there).
        QCOMPARE(vm.route(QStringLiteral("stt")), QStringLiteral("ask"));
        QCOMPARE(vm.route(QStringLiteral("llm")), QStringLiteral("ask"));
        QVERIFY(!vm.cloudSelected());
        // On the own-key card Cloud is not among the providers.
        for (const QVariant& p : vm.stt()->providers())
            QVERIFY(p.toMap().value("value").toString() != tanara::cloud::ProviderId);

        vm.stt()->setProviderId(QStringLiteral("whisper-compat"));
        QCOMPARE(vm.route(QStringLiteral("stt")), QStringLiteral("ask"));   // Ask stays, the own provider changed
        const int changes = vm.changeCount();
        QVERIFY(vm.setRoute(QStringLiteral("stt"), QStringLiteral("cloud")));
        QVERIFY(vm.setRoute(QStringLiteral("llm"), QStringLiteral("cloud")));
        QVERIFY(vm.cloudSelected());
        QCOMPARE(vm.changeCount(), changes + 2);
        QCOMPARE(vm.stt()->providerId(), QStringLiteral("whisper-compat"));   // remembered for the own path
        QVERIFY(vm.save());
        QCOMPARE(live().routes.stt.route, tanara::providers::Route::Cloud);
        QCOMPARE(live().providers.stt.selectedId, QStringLiteral("whisper-compat"));
        QVERIFY(m_app->usesCloud(tanara::providers::WorkflowStep::Transcribe));

        QVERIFY(vm.setRoute(QStringLiteral("stt"), QStringLiteral("local")));
        QVERIFY(vm.setRoute(QStringLiteral("llm"), QStringLiteral("local")));
        QVERIFY(!vm.cloudSelected());
        QVERIFY(vm.save());
        QVERIFY(!m_app->usesCloud(tanara::providers::WorkflowStep::Transcribe));

        // The Cloud page changes nothing in the draft.
        vm.openPage(QStringLiteral("cloud"), QString());
        QCOMPARE(vm.page(), QStringLiteral("cloud"));
        QVERIFY(!vm.dirty());

        // Ask is allowed with the live cloud; it is not "Cloud selected".
        QVERIFY(vm.setRoute(QStringLiteral("llm"), QStringLiteral("ask")));
        QVERIFY(!vm.cloudSelected());
    }

    void storedCloudProviderIsFlaggedUnavailable()
    {
        // Transcription saved on Cloud the pre-S3 way (the cloud id selected): the core turns it into the Cloud
        // route with the own provider kept; a deep link to the LLM changes nothing; picking a route clears it.
        startApp("live");
        if (!m_app->cloudLive())
            QSKIP("A build Tanara Cloud kliens nélkül készült.");
        tanara::app::AppSettings s = live();
        s.providers.stt.selectedId = tanara::cloud::ProviderId;
        s.providers.stt.configs[tanara::cloud::ProviderId].type = tanara::cloud::ProviderId;
        m_app->settings()->setSettings(s);
        QCOMPARE(live().routes.stt.route, tanara::providers::Route::Cloud);
        QCOMPARE(live().providers.stt.selectedId, QStringLiteral("soniox"));

        SettingsViewModel vm(services());
        QVERIFY(vm.cloudSelected());
        vm.openPage(QStringLiteral("providers"), QStringLiteral("llm"));
        QVERIFY(!vm.dirty());
        QCOMPARE(vm.stt()->providerId(), QStringLiteral("soniox"));
        QVERIFY(!vm.stt()->loginProvider());
        vm.llm()->setValue(QStringLiteral("model"), QStringLiteral("masik-modell"));
        QVERIFY(vm.save());
        QCOMPARE(live().routes.stt.route, tanara::providers::Route::Cloud);
        QCOMPARE(live().providers.llm.selected().model, QStringLiteral("masik-modell"));

        QVERIFY(vm.setRoute(QStringLiteral("stt"), QStringLiteral("external")));
        QVERIFY(!vm.cloudSelected());
        QVERIFY(vm.save());
        QCOMPARE(live().routes.stt.route, tanara::providers::Route::External);
        QCOMPARE(live().providers.stt.selectedId, QStringLiteral("soniox"));
    }

    // S3: route / setRoute / routeOptions / providerKind and the rejection rules (cloud off).
    void routesAndOptionsFollowTheRules()
    {
        startApp();
        SettingsViewModel vm(services());
        QCOMPARE(vm.route(QStringLiteral("stt")), QStringLiteral("external"));       // Soniox
        QCOMPARE(vm.route(QStringLiteral("llm")), QStringLiteral("local"));          // LM Studio
        QCOMPARE(vm.route(QStringLiteral("diarization")), QStringLiteral("external"));
        QCOMPARE(vm.route(QStringLiteral("voice")), QStringLiteral("local"));
        QCOMPARE(vm.route(QStringLiteral("embedding")), QStringLiteral("basic"));
        QVERIFY(vm.route(QStringLiteral("nincs")).isEmpty());

        auto options = [&vm](const char* cap) {
            QHash<QString, QVariantMap> out;
            for (const QVariant& v : vm.routeOptions(QString::fromLatin1(cap)))
                out.insert(v.toMap().value("value").toString(), v.toMap());
            return out;
        };
        auto voice = options("voice");
        QCOMPARE(voice.size(), 4);
        QVERIFY(voice["local"]["enabled"].toBool());
        for (const char* r : {"external", "cloud", "ask"}) {
            QVERIFY(!voice[r]["enabled"].toBool());
            QCOMPARE(voice[r]["code"].toString(), QStringLiteral("voice_local_only"));
            QVERIFY(!voice[r]["reason"].toString().isEmpty());
        }
        auto emb = options("embedding");
        QCOMPARE(emb.size(), 4);
        QVERIFY(!emb.contains("ask"));
        QVERIFY(emb["basic"]["enabled"].toBool() && emb["local"]["enabled"].toBool());
        QCOMPARE(emb["external"]["code"].toString(), QStringLiteral("no_external"));
        QCOMPARE(emb["cloud"]["code"].toString(), QStringLiteral("cloud_unavailable"));
        auto stt = options("stt");
        QVERIFY(stt["local"]["enabled"].toBool() && stt["external"]["enabled"].toBool());
        QCOMPARE(stt["cloud"]["code"].toString(), QStringLiteral("cloud_unavailable"));
        QCOMPARE(stt["ask"]["code"].toString(), QStringLiteral("ask_unavailable"));
        QCOMPARE(options("llm")["external"]["code"].toString(), QStringLiteral("no_external"));
        QCOMPARE(options("diarization")["cloud"]["code"].toString(), QStringLiteral("diarization_later"));
        QVERIFY(vm.routeOptions(QStringLiteral("nincs")).isEmpty());

        // Rejected: nothing changes.
        QVERIFY(!vm.setRoute(QStringLiteral("embedding"), QStringLiteral("ask")));
        QVERIFY(!vm.setRoute(QStringLiteral("voice"), QStringLiteral("external")));
        QVERIFY(!vm.setRoute(QStringLiteral("stt"), QStringLiteral("ask")));      // no live cloud
        QVERIFY(!vm.setRoute(QStringLiteral("stt"), QStringLiteral("cloud")));
        QVERIFY(!vm.setRoute(QStringLiteral("stt"), QStringLiteral("basic")));
        QVERIFY(!vm.setRoute(QStringLiteral("diarization"), QStringLiteral("cloud")));
        QVERIFY(!vm.setRoute(QStringLiteral("stt"), QStringLiteral("valami")));
        QVERIFY(!vm.dirty());

        // Local / External select the provider of that kind; switching back restores it.
        QSignalSpy routes(&vm, &SettingsViewModel::routesChanged);
        QVERIFY(vm.setRoute(QStringLiteral("stt"), QStringLiteral("local")));
        QCOMPARE(vm.stt()->providerId(), QStringLiteral("whisper-compat"));
        QCOMPARE(vm.route(QStringLiteral("stt")), QStringLiteral("local"));
        QCOMPARE(routes.size(), 1);
        QVERIFY(vm.dirty());
        QVERIFY(vm.setRoute(QStringLiteral("stt"), QStringLiteral("external")));
        QCOMPARE(vm.stt()->providerId(), QStringLiteral("soniox"));
        QVERIFY(!vm.dirty());
        // A provider picked on the card moves the route.
        vm.stt()->setProviderId(QStringLiteral("whisper-compat"));
        QCOMPARE(vm.route(QStringLiteral("stt")), QStringLiteral("local"));
        QCOMPARE(vm.changeCount(), 1);   // the provider change; its route follows it
        vm.discard();

        // Embedding: the route is the mode.
        QVERIFY(vm.setRoute(QStringLiteral("embedding"), QStringLiteral("local")));
        QCOMPARE(vm.embedding()->mode(), QStringLiteral("local"));
        QVERIFY(vm.save());
        QCOMPARE(live().routes.embedding.route, tanara::providers::Route::Local);
        QCOMPARE(live().providers.embedding.selectedId, tanara::providers::embeddingproviders::LocalId);

        QCOMPARE(vm.providerKind(QStringLiteral("soniox")), QStringLiteral("external"));
        QCOMPARE(vm.providerKind(QStringLiteral("whisper-compat")), QStringLiteral("local"));
        QCOMPARE(vm.providerKind(QStringLiteral("openai-compat")), QStringLiteral("local"));
        QCOMPARE(vm.providerKind(tanara::cloud::ProviderId), QStringLiteral("cloud"));
        QCOMPARE(vm.providerKind(tanara::providers::embeddingproviders::LocalId), QStringLiteral("local"));
        for (const QVariant& p : vm.stt()->providers())
            QCOMPARE(p.toMap().value("kind").toString(),
                     vm.providerKind(p.toMap().value("value").toString()));
    }

    // ---- S4: the Tanara Cloud page ----

    void cloudTierMapsToBothStoredTiersAndSaves()
    {
        startApp("live");
        if (!m_app->cloudLive())
            QSKIP("A build Tanara Cloud kliens nélkül készült.");
        // The two stored tiers differ (e.g. set from the CLI), and an Expert model is pinned.
        tanara::app::AppSettings s = live();
        s.cloud.sttTier = QStringLiteral("fast");
        s.cloud.llmTier = QStringLiteral("accurate");
        s.cloud.llmModel = QStringLiteral("tanara/some-expert-model");
        m_app->settings()->setSettings(s);

        SettingsViewModel vm(services());
        QCOMPARE(vm.cloudMode(), QStringLiteral("live"));
        QVERIFY(vm.cloudEditable());
        QCOMPARE(vm.cloudTier(), QStringLiteral("mixed"));
        QCOMPARE(vm.cloudTierOptions().size(), 2);
        QCOMPARE(vm.cloudTierOptions().at(0).toMap().value("value").toString(), QStringLiteral("fast"));
        // No catalog yet (the gateway is a closed port): no price.
        QVERIFY(vm.cloudTierOptions().at(1).toMap().value("price").toString().isEmpty());

        // One control sets both tiers and drops the pinned model (the client has no Pro level).
        vm.setCloudTier(QStringLiteral("accurate"));
        QCOMPARE(vm.cloudTier(), QStringLiteral("accurate"));
        QVERIFY(vm.dirty());
        vm.setCloudTier(QStringLiteral("pro"));                   // unknown: ignored
        QCOMPARE(vm.cloudTier(), QStringLiteral("accurate"));
        QVERIFY(vm.save());
        QCOMPARE(live().cloud.sttTier, QStringLiteral("accurate"));
        QCOMPARE(live().cloud.llmTier, QStringLiteral("accurate"));
        QVERIFY(live().cloud.llmModel.isEmpty());
        QVERIFY(!vm.dirty());

        vm.setCloudTier(QStringLiteral("fast"));
        QVERIFY(vm.save());
        QCOMPARE(live().cloud.sttTier, QStringLiteral("fast"));
        QCOMPARE(live().cloud.llmTier, QStringLiteral("fast"));
        const QJsonObject cloud = settingsFile().value(QStringLiteral("cloud")).toObject();
        QCOMPARE(cloud.value(QStringLiteral("sttTier")).toString(), QStringLiteral("fast"));
        QCOMPARE(cloud.value(QStringLiteral("llmTier")).toString(), QStringLiteral("fast"));
    }

    void cloudAdvancedFlagsGoThroughTheDraft()
    {
        startApp("live");
        if (!m_app->cloudLive())
            QSKIP("A build Tanara Cloud kliens nélkül készült.");
        SettingsViewModel vm(services());
        QCOMPARE(vm.cloudLevel(), QStringLiteral("simple"));
        QVERIFY(vm.cloudEstimateBeforeRun());
        QVERIFY(!vm.cloudFastForLongRecordings());
        QVERIFY(vm.cloudDeleteUploadedAudio());
        QCOMPARE(vm.cloudLongRecordingMinutes(), 90);

        vm.setCloudLevel(QStringLiteral("advanced"));
        vm.setCloudEstimateBeforeRun(false);
        vm.setCloudFastForLongRecordings(true);
        vm.setCloudDeleteUploadedAudio(false);
        QCOMPARE(vm.changeCount(), 4);
        // Not in the core until saved; discard brings the saved values back.
        QVERIFY(live().cloud.estimateBeforeRun);
        vm.discard();
        QCOMPARE(vm.cloudLevel(), QStringLiteral("simple"));
        QVERIFY(vm.cloudEstimateBeforeRun());
        QVERIFY(!vm.dirty());

        vm.setCloudLevel(QStringLiteral("advanced"));
        vm.setCloudEstimateBeforeRun(false);
        vm.setCloudFastForLongRecordings(true);
        vm.setCloudDeleteUploadedAudio(false);
        QVERIFY(vm.save());
        QCOMPARE(live().cloud.level, QStringLiteral("advanced"));
        QVERIFY(!live().cloud.estimateBeforeRun);
        QVERIFY(live().cloud.fastForLongRecordings);
        QVERIFY(!live().cloud.deleteUploadedAudio);
        const QJsonObject cloud = settingsFile().value(QStringLiteral("cloud")).toObject();
        QCOMPARE(cloud.value(QStringLiteral("level")).toString(), QStringLiteral("advanced"));
        QCOMPARE(cloud.value(QStringLiteral("estimateBeforeRun")).toBool(true), false);
        QCOMPARE(cloud.value(QStringLiteral("fastForLongRecordings")).toBool(false), true);
        QCOMPARE(cloud.value(QStringLiteral("deleteUploadedAudio")).toBool(true), false);

        // A fresh window reads them back.
        SettingsViewModel again(services());
        QCOMPARE(again.cloudLevel(), QStringLiteral("advanced"));
        QVERIFY(!again.cloudEstimateBeforeRun());
        QVERIFY(again.cloudFastForLongRecordings());
        QVERIFY(!again.cloudDeleteUploadedAudio());
    }

    void cloudSettersAreGatedOutsideLiveMode_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addRow("teaser") << QStringLiteral("teaser");
        QTest::addRow("off") << QStringLiteral("off");
    }
    void cloudSettersAreGatedOutsideLiveMode()
    {
        QFETCH(QString, mode);
        startApp(mode.toLatin1().constData());
        SettingsViewModel vm(services());
        const QString expected = m_app->cloudTeaser() ? QStringLiteral("teaser") : QStringLiteral("off");
        QCOMPARE(vm.cloudMode(), expected);
        QVERIFY(!vm.cloudEditable());
        QVERIFY(!vm.cloudSignedIn());
        vm.setCloudTier(QStringLiteral("fast"));
        vm.setCloudLevel(QStringLiteral("advanced"));
        vm.setCloudEstimateBeforeRun(false);
        vm.setCloudFastForLongRecordings(true);
        vm.setCloudDeleteUploadedAudio(false);
        QCOMPARE(vm.cloudTier(), QStringLiteral("accurate"));
        QCOMPARE(vm.cloudLevel(), QStringLiteral("simple"));
        QVERIFY(vm.cloudEstimateBeforeRun());
        QVERIFY(!vm.cloudFastForLongRecordings());
        QVERIFY(vm.cloudDeleteUploadedAudio());
        QVERIFY(!vm.dirty());
        // The account calls do nothing either (no sign-in flow outside live mode).
        vm.cloudSignIn();
        QVERIFY(!vm.cloudSigningIn());
        // The deep link still lands on the page.
        vm.openPage(QStringLiteral("cloud"));
        QCOMPARE(vm.page(), QStringLiteral("cloud"));
    }

    void cloudAccountFollowsTheSession()
    {
        startApp("live");
        if (!m_app->cloudLive())
            QSKIP("A build Tanara Cloud kliens nélkül készült.");
        FakeDialogs dialogs;
        SettingsViewModel vm(services());
        vm.setDialogsObject(&dialogs);
        QVERIFY(!vm.cloudSignedIn());
        QVERIFY(!vm.cloudAccount().value("valid").toBool());

        // A key from the dashboard (the sandbox's secret file; no network): signed in, the account
        // refresh goes to the closed port and its failure becomes one sentence.
        QSignalSpy changed(&vm, &SettingsViewModel::cloudChanged);
        m_app->cloud()->setManualApiKey(QStringLiteral("tnr-test-key"), QStringLiteral("teszt@pelda.hu"));
        QVERIFY(changed.size() >= 1);
        QVERIFY(vm.cloudSignedIn());
        QCOMPARE(vm.cloudAccount().value("email").toString(), QStringLiteral("teszt@pelda.hu"));
        QTRY_VERIFY_WITH_TIMEOUT(!vm.cloudAccount().value("error").toString().isEmpty(), 5000);

        // Top-up: the link request fails the same way; nothing is opened.
        vm.cloudTopUp();
        QTest::qWait(200);
        QVERIFY(dialogs.calls.isEmpty());

        // Sign-out: the local key goes even though the gateway is unreachable.
        vm.cloudSignOut();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.cloudSignedIn(), 5000);
        QVERIFY(!m_app->cloud()->isLoggedIn());
    }

    void cloudDemoStates()
    {
        FakeDialogs dialogs;
        SettingsViewModel vm;
        vm.setDialogsObject(&dialogs);
        vm.setDemoState(QStringLiteral("cloudComingSoon"));
        QCOMPARE(vm.page(), QStringLiteral("cloud"));
        QCOMPARE(vm.cloudMode(), QStringLiteral("teaser"));
        QVERIFY(!vm.cloudEditable());

        vm.setDemoState(QStringLiteral("cloudSignedOut"));
        QCOMPARE(vm.page(), QStringLiteral("cloud"));
        QCOMPARE(vm.cloudMode(), QStringLiteral("live"));
        QVERIFY(!vm.cloudSignedIn());
        QVERIFY(!vm.cloudTierOptions().at(0).toMap().value("price").toString().isEmpty());
        vm.cloudSignIn();
        QVERIFY(vm.cloudSigningIn());
        QCOMPARE(vm.cloudAccount().value("signInCode").toString(), QStringLiteral("WDJB-MJHT"));
        vm.cloudOpenSignInPage();
        QCOMPARE(dialogs.calls.size(), 1);
        vm.cloudCancelSignIn();
        QVERIFY(!vm.cloudSigningIn());

        vm.setDemoState(QStringLiteral("cloudLowBalance"));
        QVERIFY(vm.cloudSignedIn());
        const QVariantMap a = vm.cloudAccount();
        QVERIFY(a.value("valid").toBool());
        QVERIFY(a.value("lowBalance").toBool());
        QVERIFY(a.value("balance").toString().contains(QStringLiteral("1")));
        QVERIFY(!a.value("vatLabel").toString().isEmpty());
        QVERIFY(!a.value("hoursText").toString().isEmpty());
        QVERIFY(!vm.cloudTopUpUrl().isEmpty());
        vm.cloudTopUp();
        QCOMPARE(dialogs.calls.size(), 2);
        // The fictional facade saves in memory: the tier can be changed in live mode.
        vm.setCloudTier(QStringLiteral("fast"));
        QCOMPARE(vm.cloudTier(), QStringLiteral("fast"));
        QVERIFY(vm.dirty());

        vm.setDemoState(QStringLiteral("cloudAdvanced"));
        QCOMPARE(vm.cloudLevel(), QStringLiteral("advanced"));
        QCOMPARE(vm.cloudTier(), QStringLiteral("fast"));
        QVERIFY(vm.cloudFastForLongRecordings());
        QVERIFY(!vm.cloudAccount().value("lowBalance").toBool());
    }

    void cloudPageShowsTheRightBlocks_data()
    {
        QTest::addColumn<QString>("state");
        QTest::addColumn<bool>("comingSoon");
        QTest::addColumn<QString>("accountRow");
        QTest::addColumn<bool>("lowBalance");
        QTest::addColumn<bool>("switches");
        QTest::addRow("teaser") << QStringLiteral("cloudComingSoon") << true << QString() << false << false;
        QTest::addRow("signedOut") << QStringLiteral("cloudSignedOut") << false << QStringLiteral("cloudSignedOut") << false << false;
        QTest::addRow("lowBalance") << QStringLiteral("cloudLowBalance") << false << QStringLiteral("cloudSignedIn") << true << false;
        QTest::addRow("advanced") << QStringLiteral("cloudAdvanced") << false << QStringLiteral("cloudSignedIn") << false << true;
    }
    void cloudPageShowsTheRightBlocks()
    {
        QFETCH(QString, state);
        QFETCH(bool, comingSoon);
        QFETCH(QString, accountRow);
        QFETCH(bool, lowBalance);
        QFETCH(bool, switches);
        QQmlEngine engine;
        setupEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "SettingsWindow");
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{QStringLiteral("demoState"), state}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        QTest::qWait(150);
        QQuickItem* page = findItem(win->contentItem(), QStringLiteral("settingsCloudPage"));
        QVERIFY(page);
        QVERIFY(page->isVisible());
        QCOMPARE(findItem(page, QStringLiteral("cloudComingSoon"))->isVisible(), comingSoon);
        QCOMPARE(findItem(page, QStringLiteral("cloudAccountBlock"))->isVisible(), !comingSoon);
        if (!accountRow.isEmpty()) QVERIFY(findItem(page, accountRow)->isVisible());
        QCOMPARE(findItem(page, QStringLiteral("cloudLowBalance"))->isVisible(), lowBalance);
        QCOMPARE(findItem(page, QStringLiteral("cloudAdvancedSwitches"))->isVisible(), switches);
        QVERIFY(findItem(page, QStringLiteral("cloudTier-fast")));
        // "Feldolgozás" jumps to the Processing page.
        QQuickItem* link = findItem(page, QStringLiteral("cloudToProcessing"));
        QVERIFY(link);
        QMetaObject::invokeMethod(link, "clicked");
        auto* vm = qobject_cast<SettingsViewModel*>(win->property("vm").value<QObject*>());
        QCOMPARE(vm->page(), QStringLiteral("processing"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    // ---- S2: the Processing page's cards ----

    static QVariantMap capOf(const SettingsViewModel& vm, const QString& key)
    {
        for (const QVariant& v : vm.capabilities())
            if (v.toMap().value("key").toString() == key) return v.toMap();
        return {};
    }

    void capabilitiesDescribeTheCards()
    {
        SettingsViewModel vm;
        vm.setDemoState(QStringLiteral("processing"));
        QCOMPARE(vm.page(), QStringLiteral("processing"));
        QVERIFY(vm.section().isEmpty());
        QVERIFY(vm.expandedCards().isEmpty());              // every card collapsed
        const QVariantList caps = vm.capabilities();
        QCOMPARE(caps.size(), 5);
        QStringList keys, caps2;
        for (const QVariant& v : caps) {
            keys << v.toMap().value("key").toString();
            caps2 << v.toMap().value("cap").toString();
            QCOMPARE(v.toMap().value("status").toString(), QStringLiteral("ok"));
            QVERIFY(!v.toMap().value("warn").toBool());
            QCOMPARE(v.toMap().value("options").toList().size(), 4);
        }
        QCOMPARE(keys, (QStringList{"stt", "diar", "voice", "summary", "tags"}));
        QCOMPARE(caps2, (QStringList{"stt", "diarization", "voice", "llm", "embedding"}));
        QCOMPARE(capOf(vm, "stt").value("where").toString(), QStringLiteral("external"));
        QCOMPARE(capOf(vm, "diar").value("where").toString(), QStringLiteral("local-tanara"));
        QCOMPARE(capOf(vm, "voice").value("where").toString(), QStringLiteral("local-tanara"));
        QCOMPARE(capOf(vm, "summary").value("where").toString(), QStringLiteral("local-server"));
        QCOMPARE(capOf(vm, "tags").value("where").toString(), QStringLiteral("basic"));
        QCOMPARE(capOf(vm, "tags").value("route").toString(), QStringLiteral("basic"));
        // Voice: only local; the other segments say why.
        for (const QVariant& o : capOf(vm, "voice").value("options").toList()) {
            const QVariantMap m = o.toMap();
            if (m.value("value") == QStringLiteral("local")) { QVERIFY(m.value("enabled").toBool()); continue; }
            QVERIFY(!m.value("enabled").toBool());
            QVERIFY(m.value("reason").toString().contains(QStringLiteral("hanglenyomat"), Qt::CaseInsensitive));
        }

        // A route change follows at once (the list is the cards' only source).
        QSignalSpy changed(&vm, &SettingsViewModel::processingChanged);
        QVERIFY(vm.setRoute(QStringLiteral("stt"), QStringLiteral("local")));
        QVERIFY(changed.size() >= 1);
        QCOMPARE(capOf(vm, "stt").value("route").toString(), QStringLiteral("local"));
        QCOMPARE(capOf(vm, "stt").value("where").toString(), QStringLiteral("local-server"));
        // Diarization External after a Whisper transcriber: nobody separates the speakers.
        QVERIFY(vm.setRoute(QStringLiteral("diarization"), QStringLiteral("external")));
        QCOMPARE(capOf(vm, "diar").value("status").toString(), QStringLiteral("none"));
        QVERIFY(vm.setRoute(QStringLiteral("stt"), QStringLiteral("external")));
        QCOMPARE(capOf(vm, "diar").value("status").toString(), QStringLiteral("ok"));   // Soniox separates

        // Missing models: the pill and the attention dot.
        vm.setDemoState(QStringLiteral("B08none"));
        QCOMPARE(capOf(vm, "voice").value("status").toString(), QStringLiteral("missing"));
        QVERIFY(capOf(vm, "voice").value("warn").toBool());
        QVERIFY(!capOf(vm, "voice").value("warnText").toString().isEmpty());
        QCOMPARE(vm.expandedCards(), QStringList{QStringLiteral("voice")});   // the state's card is open
        vm.setDemoState(QStringLiteral("B08segmissing"));
        QCOMPARE(capOf(vm, "diar").value("status").toString(), QStringLiteral("missing"));
        QVERIFY(capOf(vm, "diar").value("warn").toBool());
        QVERIFY(!capOf(vm, "voice").value("warn").toBool());
        vm.setDemoState(QStringLiteral("B08segmismatch"));     // the float model parked: still missing
        QCOMPARE(capOf(vm, "diar").value("status").toString(), QStringLiteral("missing"));
        QVERIFY(vm.segmentationModels()->acceptMismatch(QStringLiteral("pyannote-segmentation-3.0")).isEmpty());
        QCOMPARE(capOf(vm, "diar").value("status").toString(), QStringLiteral("unverified"));   // "Mégis használom"
        QVERIFY(capOf(vm, "diar").value("warn").toBool());

        // Providers: a missing key, a failed test.
        vm.setDemoState(QStringLiteral("B04"));
        QCOMPARE(capOf(vm, "stt").value("status").toString(), QStringLiteral("none"));
        QVERIFY(capOf(vm, "stt").value("warn").toBool());
        QVERIFY(!capOf(vm, "summary").value("warn").toBool());
        vm.setDemoState(QStringLiteral("B05"));
        QCOMPARE(capOf(vm, "summary").value("status").toString(), QStringLiteral("ok"));
        QVERIFY(capOf(vm, "summary").value("warn").toBool());
        QVERIFY(!capOf(vm, "stt").value("warn").toBool());

        // Cloud that cannot run yet: "Hamarosan" (stt warns, the background embedding does not).
        vm.setDemoState(QStringLiteral("cloudSelected"));
        QCOMPARE(capOf(vm, "stt").value("status").toString(), QStringLiteral("soon"));
        QVERIFY(capOf(vm, "stt").value("warn").toBool());
        vm.setDemoState(QStringLiteral("B04embeddingCloud"));
        QCOMPARE(capOf(vm, "tags").value("status").toString(), QStringLiteral("soon"));
        QVERIFY(!capOf(vm, "tags").value("warn").toBool());

        // Ask with the live cloud: the own path is External (Soniox).
        vm.setDemoState(QStringLiteral("processingSttAsk"));
        QCOMPARE(capOf(vm, "stt").value("route").toString(), QStringLiteral("ask"));
        QCOMPARE(capOf(vm, "stt").value("where").toString(), QStringLiteral("ask"));
        QCOMPARE(capOf(vm, "stt").value("ownRoute").toString(), QStringLiteral("external"));
        QCOMPARE(capOf(vm, "stt").value("status").toString(), QStringLiteral("ok"));
        for (const QVariant& o : capOf(vm, "stt").value("options").toList())
            QVERIFY2(o.toMap().value("enabled").toBool(), qPrintable(o.toMap().value("value").toString()));
    }

    void serviceShimsAreGone()
    {
        const QMetaObject* mo = &SettingsViewModel::staticMetaObject;
        for (const char* prop : {"serviceMode", "localDiarization"})
            QCOMPARE(mo->indexOfProperty(prop), -1);
        QCOMPARE(mo->indexOfSignal("serviceModeChanged()"), -1);
        QCOMPARE(mo->indexOfSignal("diarizationChanged()"), -1);
        for (const char* prop : {"capabilities", "expandedCards", "summaryAdvancedOpen", "summaryPromptsLocked",
                                 "summaryPromptNotice"})
            QVERIFY2(mo->indexOfProperty(prop) >= 0, prop);
    }

    // Ádám's security decision: on the Cloud summary route the prompts are read-only; switching there with an
    // own prompt shows the notice; nothing is deleted.
    void cloudPromptLockAndNotice()
    {
        SettingsViewModel vm;
        vm.setDemoState(QStringLiteral("processingSttAsk"));   // the cloud is live, no own prompt
        QVERIFY(!vm.summaryPromptsLocked());
        QVERIFY(!vm.anyPromptModified());
        QVERIFY(vm.setRoute(QStringLiteral("llm"), QStringLiteral("cloud")));
        QVERIFY(vm.summaryPromptsLocked());
        QVERIFY(!vm.summaryPromptNotice());                    // no own prompt: nothing to say

        vm.setDemoState(QStringLiteral("processingSummaryCloud"));
        QCOMPARE(vm.route(QStringLiteral("llm")), QStringLiteral("cloud"));
        QVERIFY(vm.dirty());                                   // the switch is not saved yet
        QVERIFY(vm.summaryPromptsLocked());
        QVERIFY(vm.summaryPromptNotice());
        QVERIFY(vm.summaryAdvancedOpen());
        QVERIFY(vm.expandedCards().contains(QStringLiteral("summary")));
        QVERIFY(vm.promptModified());
        const QString own = vm.promptText();

        // Back to the own path: unlocked, no notice; again Cloud: both again.
        QSignalSpy changed(&vm, &SettingsViewModel::processingChanged);
        QVERIFY(vm.setRoute(QStringLiteral("llm"), QStringLiteral("local")));
        QVERIFY(!vm.summaryPromptsLocked());
        QVERIFY(!vm.summaryPromptNotice());
        QVERIFY(changed.size() >= 1);
        QVERIFY(vm.setRoute(QStringLiteral("llm"), QStringLiteral("cloud")));
        QVERIFY(vm.summaryPromptsLocked());
        QVERIFY(vm.summaryPromptNotice());

        // Saved: still locked, the notice was about the switch; the own prompt is kept.
        QVERIFY(vm.save());
        QVERIFY(vm.summaryPromptsLocked());
        QVERIFY(!vm.summaryPromptNotice());
        QCOMPARE(vm.promptText(), own);
        QVERIFY(vm.promptModified());
    }

    void openPageExpandsTheCardAndTheSummaryAdvancedBlock()
    {
        SettingsViewModel vm;
        vm.setDemoState(QStringLiteral("B01"));
        QVERIFY(!vm.summaryAdvancedOpen());
        QVERIFY(vm.expandedCards().isEmpty());
        // The old Summary page = the prompts: processing#summary with "Haladó" open.
        vm.openPage(QStringLiteral("summary"));
        QCOMPARE(vm.page(), QStringLiteral("processing"));
        QCOMPARE(vm.section(), QStringLiteral("summary"));
        QVERIFY(vm.summaryAdvancedOpen());
        QVERIFY(vm.llm()->advancedOpen());                     // the LLM's advanced fields live in that block
        QVERIFY(vm.expandedCards().contains(QStringLiteral("summary")));
        vm.setSummaryAdvancedOpen(false);
        // The LLM's settings (key, unreachable server): the card, "Haladó" stays as it was.
        vm.openPage(QStringLiteral("providers"), QStringLiteral("llm"));
        QCOMPARE(vm.section(), QStringLiteral("summary"));
        QVERIFY(!vm.summaryAdvancedOpen());
        // A jump opens its card; closing and unknown keys.
        vm.openPage(QStringLiteral("processing"), QStringLiteral("tags"));
        QVERIFY(vm.expandedCards().contains(QStringLiteral("tags")));
        QSignalSpy expanded(&vm, &SettingsViewModel::expandedCardsChanged);
        vm.setCardExpanded(QStringLiteral("tags"), false);
        QVERIFY(!vm.expandedCards().contains(QStringLiteral("tags")));
        vm.setCardExpanded(QStringLiteral("nincs"), true);
        vm.setCardExpanded(QStringLiteral("tags"), false);
        QCOMPARE(expanded.size(), 1);
        // The diarization fix target (OverviewSpeakers) lands on the separation card.
        vm.openPage(QStringLiteral("diarization"));
        QCOMPARE(vm.section(), QStringLiteral("diar"));
        QVERIFY(vm.expandedCards().contains(QStringLiteral("diar")));
    }

    void processingPageRendersTheCardStates()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        const auto open = [&engine](const QString& state) {
            QQmlComponent comp(&engine);
            comp.loadFromModule("Tanara", "SettingsWindow");
            std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{QStringLiteral("demoState"), state}}));
            if (auto* win = qobject_cast<QQuickWindow*>(obj.get())) win->show();
            QTest::qWait(150);
            return obj;
        };
        const auto item = [](QObject* obj, const QString& name) {
            return findItem(qobject_cast<QQuickWindow*>(obj)->contentItem(), name);
        };
        const auto shown = [&item](QObject* obj, const QString& name) {
            QQuickItem* i = item(obj, name);
            return i && i->isVisible();
        };
        const QStringList keys{"stt", "diar", "voice", "summary", "tags"};
        {
            // All collapsed: every header, no body; the pills say "Beállítva".
            auto obj = open(QStringLiteral("processing"));
            for (const QString& k : keys) {
                QVERIFY2(shown(obj.get(), "card-" + k), qPrintable(k));
                QVERIFY2(!shown(obj.get(), "cardBody-" + k), qPrintable(k));
                QCOMPARE(item(obj.get(), "cardStatus-" + k)->property("text").toString(), QStringLiteral("Beállítva"));
            }
            // The header opens the card; a route change re-binds the card at once.
            QMetaObject::invokeMethod(item(obj.get(), "cardHeader-stt"), "clicked");
            QTRY_VERIFY(shown(obj.get(), "cardBody-stt"));
            QCOMPARE(item(obj.get(), "card-stt")->property("route").toString(), QStringLiteral("external"));
            auto* vm = qobject_cast<SettingsViewModel*>(obj->property("vm").value<QObject*>());
            QVERIFY(vm->setRoute(QStringLiteral("stt"), QStringLiteral("local")));
            QTRY_COMPARE(item(obj.get(), "card-stt")->property("route").toString(), QStringLiteral("local"));
            QCOMPARE(item(obj.get(), "card-stt")->property("where").toString(), QStringLiteral("local-server"));
            QCOMPARE(item(obj.get(), "cardRoute-stt")->property("value").toString(), QStringLiteral("local"));
        }
        {
            // Transcription asks: the reason and the two paths.
            auto obj = open(QStringLiteral("processingSttAsk"));
            QVERIFY(shown(obj.get(), "cardBody-stt"));
            QVERIFY(shown(obj.get(), "askNote-stt"));
            QVERIFY(shown(obj.get(), "askOwn-stt"));
            QVERIFY(shown(obj.get(), "askCloud-stt"));
        }
        {
            // A missing voice model: the pill, the empty state, the dot.
            auto obj = open(QStringLiteral("B08none"));
            QVERIFY(shown(obj.get(), "cardBody-voice"));
            QCOMPARE(item(obj.get(), "cardStatus-voice")->property("text").toString(), QStringLiteral("Modell hiányzik"));
            QVERIFY(shown(obj.get(), "voiceModelsNone"));
            QTRY_VERIFY(item(obj.get(), "nav-processing-voice")->property("warned").toBool());
        }
        {
            // Summary on Cloud with an own prompt: read-only editor, the lock line, the notice.
            auto obj = open(QStringLiteral("processingSummaryCloud"));
            QVERIFY(shown(obj.get(), "cardBody-summary"));
            QVERIFY(shown(obj.get(), "summaryAdvanced"));
            QVERIFY(shown(obj.get(), "promptLock"));
            QVERIFY(shown(obj.get(), "promptNotice"));
            QVERIFY(item(obj.get(), "promptEditor")->property("readOnly").toBool());
            auto* vm = qobject_cast<SettingsViewModel*>(obj->property("vm").value<QObject*>());
            QVERIFY(vm->setRoute(QStringLiteral("llm"), QStringLiteral("local")));
            QTRY_VERIFY(!item(obj.get(), "promptEditor")->property("readOnly").toBool());
            QVERIFY(!shown(obj.get(), "promptLock"));
        }
        {
            // Tag suggestions on the basic level: no endpoint; "Haladó" holds the rejected suggestions.
            auto obj = open(QStringLiteral("B04embeddingNone"));
            QVERIFY(shown(obj.get(), "cardBody-tags"));
            QCOMPARE(item(obj.get(), "cardRoute-tags")->property("value").toString(), QStringLiteral("basic"));
            QVERIFY(!shown(obj.get(), "embeddingCard"));
            QVERIFY(shown(obj.get(), "tagSuggestionSettings"));
            QVERIFY(!shown(obj.get(), "rejectedReset"));
            QMetaObject::invokeMethod(item(obj.get(), "tagsAdvancedToggle"), "clicked");
            QTRY_VERIFY(shown(obj.get(), "rejectedReset"));
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    // ---- the window and its host ----

    void windowLoadsEveryDemoStateWithoutWarnings_data()
    {
        QTest::addColumn<QString>("state");
        for (const char* st : {"B01", "B02", "B03", "B04", "B05", "B07", "B07notes", "advanced", "dirty", "unsaved",
                               "schema", "cloudComingSoon", "cloudSignedOut", "cloudLowBalance", "cloudAdvanced",
                               "cloudSelected", "addApp", "reset",
                               "B04embedding", "B04embeddingRunning", "B04embeddingError", "B04embeddingDone",
                               "B04embeddingCloud", "B04embeddingNone", "B04modelChange", "rejectedReset",
                               "B08", "B08download", "B08none", "B08error", "B08offline", "B08backfill",
                               "B08partial", "B08bundled", "B08diaroff", "B08segmissing", "B08segmismatch",
                               "processing", "processingSttAsk", "processingSummaryCloud"})
            QTest::addRow("%s", st) << QString::fromLatin1(st);
    }
    void windowLoadsEveryDemoStateWithoutWarnings()
    {
        QFETCH(QString, state);
        for (const char* theme : {"light", "dark"}) {
            AppContext::instance()->setThemeMode(QString::fromLatin1(theme));
            QQmlEngine engine;
            setupEngine(engine);
            QStringList warnings;
            connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError>& list) {
                for (const QQmlError& e : list) warnings << e.toString();
            });
            QQmlComponent comp(&engine);
            comp.loadFromModule("Tanara", "SettingsWindow");
            QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
            std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{QStringLiteral("demoState"), state}}));
            auto* win = qobject_cast<QQuickWindow*>(obj.get());
            QVERIFY(win);
            win->show();
            QTest::qWait(200);
            auto* vm = qobject_cast<SettingsViewModel*>(win->property("vm").value<QObject*>());
            QVERIFY(vm);
            QVERIFY(vm->demo());
            QVERIFY(!win->grabWindow().isNull());
            QVERIFY2(warnings.isEmpty(), qPrintable(state + QStringLiteral(": ") + warnings.join(QLatin1Char('\n'))));
        }
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
    }

    // A visual item by objectName (Repeater delegates are not QObject children of the window).
    static QQuickItem* findItem(QQuickItem* root, const QString& name)
    {
        if (!root) return nullptr;
        if (root->objectName() == name) return root;
        for (QQuickItem* child : root->childItems())
            if (QQuickItem* hit = findItem(child, name)) return hit;
        return nullptr;
    }

    void windowShowsEveryPageAndJumpPointWithoutWarnings()
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
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{QStringLiteral("demoState"), QStringLiteral("B01")}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        QTest::qWait(100);
        auto* vm = qobject_cast<SettingsViewModel*>(win->property("vm").value<QObject*>());
        QVERIFY(vm);
        QQuickItem* content = findItem(win->contentItem(), QStringLiteral("settingsContent"));
        QVERIFY(content);

        for (const char* p : {"general", "recording", "processing", "cloud", "people"}) {
            const QString page = QString::fromLatin1(p);
            // The group row switches the page.
            QQuickItem* group = findItem(win->contentItem(), QStringLiteral("nav-") + page);
            QVERIFY2(group, p);
            QMetaObject::invokeMethod(group, "clicked");
            QCOMPARE(vm->page(), page);
            QTest::qWait(50);
            QVERIFY(!win->grabWindow().isNull());
            // Each jump point: the section is set, its block is scrolled into view.
            for (const QString& key : SettingsViewModel::sectionsOf(page)) {
                QQuickItem* item = findItem(win->contentItem(), QStringLiteral("nav-%1-%2").arg(page, key));
                QVERIFY2(item, qPrintable(key));
                QMetaObject::invokeMethod(item, "clicked");
                QCOMPARE(vm->section(), key);
                QTest::qWait(350);                                // the scroll animation
                QCOMPARE(vm->section(), key);                     // the scroll did not move it away
                QQuickItem* block = findItem(win->contentItem(), QStringLiteral("section-") + key);
                QVERIFY2(block, qPrintable(key));
                QVERIFY(block->isVisible());
                const qreal y = block->mapToItem(content, QPointF(0, 0)).y();
                QVERIFY2(y >= -1 && y < content->height(), qPrintable(key));
            }
        }

        // The sidebar follows the scroll: at the bottom of Processing, the last jump point.
        vm->openPage(QStringLiteral("processing"), QStringLiteral("stt"));
        QTest::qWait(400);                                        // the scroll animation
        QCOMPARE(vm->section(), QStringLiteral("stt"));
        const qreal maxY = content->property("contentHeight").toReal() - content->property("height").toReal();
        QVERIFY(maxY > 0);
        content->setProperty("contentY", maxY);
        QTRY_COMPARE(vm->section(), QStringLiteral("tags"));
        content->setProperty("contentY", 0);
        QTRY_COMPARE(vm->section(), QStringLiteral("stt"));

        // A deep link from outside scrolls too.
        vm->openPage(QStringLiteral("speakers"), QStringLiteral("voiceModels"));
        QTRY_VERIFY(content->property("contentY").toReal() > 0);
        QCOMPARE(vm->section(), QStringLiteral("voice"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void sidebarWarnDotsFollowTheCheapSignals()
    {
        QQmlEngine engine;
        setupEngine(engine);
        const auto open = [&engine](const QString& state) {
            QQmlComponent comp(&engine);
            comp.loadFromModule("Tanara", "SettingsWindow");
            std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{QStringLiteral("demoState"), state}}));
            if (auto* win = qobject_cast<QQuickWindow*>(obj.get())) win->show();
            return obj;
        };
        const auto warned = [](QObject* obj, const char* name) {
            QQuickItem* item = findItem(qobject_cast<QQuickWindow*>(obj)->contentItem(), QString::fromLatin1(name));
            return item && item->property("warned").toBool();
        };
        {
            // The LLM's test failed: the dot on Summary, not on Transcription.
            auto obj = open(QStringLiteral("B05"));
            QTRY_VERIFY(warned(obj.get(), "nav-processing-summary"));
            QVERIFY(!warned(obj.get(), "nav-processing-stt"));
        }
        {
            // No segmentation model: Speaker separation; on the group row only away from the group.
            auto obj = open(QStringLiteral("B08segmissing"));
            QTRY_VERIFY(warned(obj.get(), "nav-processing-diar"));
            QVERIFY(!warned(obj.get(), "nav-processing-voice"));
            auto* win = qobject_cast<QQuickWindow*>(obj.get());
            QQuickItem* group = findItem(win->contentItem(), QStringLiteral("nav-processing"));
            QVERIFY(group);
            QVERIFY(!group->property("warn").toBool());
            win->property("vm").value<QObject*>()->setProperty("page", QStringLiteral("general"));
            QTRY_VERIFY(group->property("warn").toBool());
        }
        {
            // No voice model at all: Speaker recognition.
            auto obj = open(QStringLiteral("B08none"));
            QTRY_VERIFY(warned(obj.get(), "nav-processing-voice"));
        }
    }

    void hostOpensOnTheRequestedPageAndAsksBeforeLosingChanges()
    {
        startApp();
        FakeDialogs dialogs;
        SettingsWindowHost host(m_app.get(), &dialogs);
        QSignalSpy saved(&host, &SettingsWindowHost::saved);
        QSignalSpy closed(&host, &SettingsWindowHost::closed);
        QVERIFY(host.open(QStringLiteral("watcher")));
        QVERIFY(host.isVisible());
        SettingsViewModel* vm = host.viewModel();
        QVERIFY(vm);
        QVERIFY(!vm->demo());
        QCOMPARE(vm->facade(), services().settings(QString(), nullptr));
        QCOMPARE(vm->dialogs(), &dialogs);
        QCOMPARE(vm->page(), QStringLiteral("recording"));
        QCOMPARE(vm->section(), QStringLiteral("watcher"));
        QTest::qWait(100);
        QVERIFY(vm->watching());                           // its page is visible: live detection
        QVERIFY(vm->monitoring());                         // the devices share the page

        // A new request with the window open: only the page switches, the draft stays.
        vm->setSilenceAskMinutes(11);
        QVERIFY(host.open(QStringLiteral("providers"), QStringLiteral("llm")));
        QCOMPARE(vm->page(), QStringLiteral("processing"));
        QCOMPARE(vm->section(), QStringLiteral("summary"));
        QCOMPARE(vm->focusField(), QStringLiteral("llm"));
        QVERIFY(vm->dirty());
        QTest::qWait(50);
        QVERIFY(!vm->watching());
        QVERIFY(!vm->monitoring());

        // Closing with an unsaved change: the window stays open (the question lives in QML).
        host.window()->close();
        QTest::qWait(100);
        QVERIFY(host.isVisible());
        QCOMPARE(closed.size(), 0);

        // Save through the view model: the host signals; after that closing is plain.
        QVERIFY(vm->save());
        QCOMPARE(saved.size(), 1);
        QCOMPARE(live().watcher.silenceAskMinutes, 11);
        host.window()->close();
        QVERIFY(waitFor([&] { return !host.isVisible(); }));
        QCOMPARE(closed.size(), 1);

        // Reopened: a fresh state from disk, on the requested page.
        tanara::app::AppSettings s = live();
        s.watcher.silenceAskMinutes = 4;
        m_app->settings()->setSettings(s);
        QVERIFY(host.open(QStringLiteral("summary")));
        QCOMPARE(vm->page(), QStringLiteral("processing"));
        QCOMPARE(vm->section(), QStringLiteral("summary"));
        QCOMPARE(vm->silenceAskMinutes(), 4);
        host.closeNow();
        QVERIFY(!host.isVisible());
    }

    void hostPersistsTheThemeWhenThereIsNoMainWindow()
    {
        startApp();
        FakeDialogs dialogs;
        SettingsWindowHost host(m_app.get(), &dialogs);
        QVERIFY(host.open(QStringLiteral("general")));
        host.viewModel()->setThemeMode(QStringLiteral("dark"));
        QVERIFY(host.viewModel()->save());
        QFile f(QDir(m_home->path()).filePath(QStringLiteral("ui-state.json")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(f.readAll()).object().value("themeMode").toString(),
                 QStringLiteral("dark"));
        host.closeNow();
    }
};

QTEST_MAIN(TestSettingsViewModel)
#include "test_settings_view_model.moc"

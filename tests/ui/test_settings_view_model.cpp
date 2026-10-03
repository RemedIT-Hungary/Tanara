// SettingsViewModel (+ SettingsProviderModel / SettingsDeviceModel / SettingsCloudModel,
// SettingsPromptHighlighter, SettingsWindowHost) — a Beállítások-ablak nézetmodelljei.
//
//  - demó-módban (controller nélkül): a design-állapotok adatai;
//  - valódi AppControllerrel, IZOLÁLT TANARA_HOME-ban (QTemporaryDir): piszkozat és mentés
//    (csak a különbség megy a core-ba), eldobás, mező-ellenőrzés, szolgáltató-váltás a másik
//    szolgáltató megtartásával, kulcsok a KeyStore-ban, eszköz-átnevezés oda-vissza, promptok,
//    mély hivatkozás (B04), figyelt alkalmazások, cloud-módok;
//  - kapcsolat-teszt egy helyi ál-HTTP-szerverrel (valódi szolgáltató-hívás NINCS).
// A Tanara Cloud gateway címe egy zárt helyi port (TANARA_CLOUD_URL), bejelentkezés nincs.
#include "AppContext.h"
#include "QmlApp.h"
#include "SettingsCloudModel.h"
#include "SettingsDeviceModel.h"
#include "SettingsDialogs.h"
#include "SettingsPromptHighlighter.h"
#include "SettingsProviderModel.h"
#include "SettingsViewModel.h"
#include "SettingsWindowHost.h"
#include "TrackListModel.h"

#include "tanara/AppController.h"
#include "tanara/Paths.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/detect/Autostart.h"
#include "tanara/detect/IMeetingDetector.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/MeetingStore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <memory>

using namespace tanara_qml;
using tanara::AppController;
using tanara::AppSettings;

namespace {

// ---- minimális ál-HTTP-szerver (mint a tests/unit/test_app_jobs.cpp-ben) -----------------
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

// ---- ál-párbeszédablakok (a Widgets-világ helyén) ----------------------------------------
class FakeDialogs : public SettingsDialogs {
public:
    using SettingsDialogs::SettingsDialogs;
    QStringList calls;
    QString folderAnswer;
    bool loginAnswer = false;

    QString pickFolder(const QString& title, const QString& start) override
    {
        calls << QStringLiteral("pick:%1:%2").arg(title, start);
        return folderAnswer;
    }
    void openFolder(const QString& path) override { calls << QStringLiteral("open:") + path; }
    void openUrl(const QString& url) override { calls << QStringLiteral("url:") + url; }
    void openPeople() override { calls << QStringLiteral("people"); }
    bool cloudLogin() override { calls << QStringLiteral("login"); return loginAnswer; }
    void cloudTopup() override { calls << QStringLiteral("topup"); }
    bool cloudPickModel(const QString& kind) override { calls << QStringLiteral("expert:") + kind; return false; }
    bool cloudTerms() override { calls << QStringLiteral("terms"); return false; }
};

// ---- ál-detektor: a megadott jelet adja vissza ------------------------------------------
class FakeDetector : public tanara::IMeetingDetector {
public:
    tanara::MeetingSignal signal;
    QStringList configured;
    QString id() const override { return QStringLiteral("fake"); }
    tanara::MeetingSignal poll() override { return signal; }
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
    QString m_closedPortBase;

    // Friss, izolált Tanara egy ideiglenes mappában. cloud: off | live | teaser.
    void startApp(const char* cloud = "off")
    {
        m_app.reset();
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        qputenv("TANARA_CLOUD", cloud);
        qputenv("TANARA_CLOUD_URL", "http://127.0.0.1:9");   // zárt port: a gateway sosem érhető el
        m_app = std::make_unique<AppController>();
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
    }
    AppSettings live() const { return m_app->settings()->settings(); }
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
        // Egy biztosan zárt helyi port (megnyitjuk, megjegyezzük, bezárjuk).
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

    // ---- a különbség-számítás magja ------------------------------------------------------

    void mergeAppliesOnlyWhatTheUserChanged()
    {
        const QJsonObject base{{"a", 1}, {"b", "x"}, {"names", QJsonObject{{"mic", "A"}, {"spk", "B"}}},
                               {"sttProviders", QJsonObject{{"soniox", QJsonObject{{"model", "m1"}, {"baseUrl", "u"}}}}}};
        QJsonObject draft = base;
        draft["a"] = 2;                                                   // a felhasználó módosította
        draft["names"] = QJsonObject{{"mic", "A2"}};                      // átnevezés + egy név törlése
        draft["sttProviders"] = QJsonObject{{"soniox", QJsonObject{{"model", "m2"}, {"baseUrl", "u"}}}};
        QJsonObject onto = base;
        onto["b"] = "közben-más-mentette";                                // külső változás
        onto["c"] = true;                                                 // az ablak nem ismeri
        onto["sttProviders"] = QJsonObject{{"soniox", QJsonObject{{"model", "m1"}, {"baseUrl", "külső"}}},
                                           {"whisper", QJsonObject{{"model", "w"}}}};

        const QJsonObject out = SettingsViewModel::mergedSettings(base, draft, onto);
        QCOMPARE(out.value("a").toInt(), 2);
        QCOMPARE(out.value("b").toString(), QStringLiteral("közben-más-mentette"));
        QCOMPARE(out.value("c").toBool(), true);
        QCOMPARE(out.value("names").toObject(), (QJsonObject{{"mic", "A2"}}));
        const QJsonObject stt = out.value("sttProviders").toObject();
        QCOMPARE(stt.value("soniox").toObject().value("model").toString(), QStringLiteral("m2"));
        QCOMPARE(stt.value("soniox").toObject().value("baseUrl").toString(), QStringLiteral("külső"));
        QVERIFY(stt.contains("whisper"));

        QCOMPARE(SettingsViewModel::countChanges(base, base), 0);
        QCOMPARE(SettingsViewModel::countChanges(base, draft), 4);   // a, mic, spk (törölve), model
        // Egy most először kiválasztott szolgáltató alapértelmezett mezői nem külön változások.
        QJsonObject withNew = base;
        withNew["sttProviders"] = QJsonObject{{"soniox", QJsonObject{{"model", "m1"}, {"baseUrl", "u"}}},
                                              {"whisper", QJsonObject{{"model", "w"}, {"baseUrl", "v"}}}};
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
        QVERIFY(!spans.at(1).known);                       // ékezetes, de a kód nem cseréli
        QVERIFY(SettingsPromptHighlighter::spans(QStringLiteral("nincs benne"), known).isEmpty());
    }

    // ---- demó: a design állapotai -------------------------------------------------------

    void demoStatesCarryFictionalData()
    {
        SettingsViewModel vm;
        vm.setDemoState(QStringLiteral("B01"));
        QVERIFY(vm.demo());
        QCOMPARE(vm.page(), QStringLiteral("general"));
        QCOMPARE(vm.userName(), QStringLiteral("Kovács Lilla"));
        QVERIFY(vm.hasVoiceprint());
        QVERIFY(!vm.dirty());
        QCOMPARE(vm.folders().size(), 3);
        QVERIFY(vm.folders().at(0).toMap().value("usage").toString().contains(QStringLiteral("GB")));
        QCOMPARE(vm.footerText(), QStringLiteral("Nincs mentetlen változás"));

        vm.setDemoState(QStringLiteral("B02"));
        QCOMPARE(vm.page(), QStringLiteral("recording"));
        QCOMPARE(vm.deviceCount(), 5);
        QCOMPARE(vm.changeCount(), 1);                     // az épp átnevezett eszköz
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
        QCOMPARE(vm.page(), QStringLiteral("services"));
        QCOMPARE(vm.focusField(), QStringLiteral("stt"));
        QVERIFY(vm.stt()->highlighted());
        QVERIFY(!vm.llm()->highlighted());
        QVERIFY(vm.servicesWarn());                        // hiányzik az átíró kulcsa
        QVERIFY(!vm.focusBannerText().isEmpty());
        QCOMPARE(vm.llm()->testState(), QStringLiteral("ok"));

        vm.setDemoState(QStringLiteral("B05"));
        QCOMPARE(vm.llm()->testState(), QStringLiteral("failed"));
        QCOMPARE(vm.llm()->errorCode(), QStringLiteral("ECONNREFUSED"));
        QVERIFY(vm.llm()->advancedOpen());
        QVERIFY(vm.servicesWarn());                        // a legutóbbi teszt elbukott
        QCOMPARE(vm.stt()->testState(), QStringLiteral("ok"));
        QVERIFY(vm.stt()->statusText().contains(QStringLiteral("210")));

        vm.setDemoState(QStringLiteral("B06"));
        QCOMPARE(vm.cloudAvailability(), QStringLiteral("live"));
        QCOMPARE(vm.serviceMode(), QStringLiteral("cloud"));
        QVERIFY(vm.dirty());
        QVERIFY(vm.footerText().contains(QStringLiteral("Tanara Cloud")));
        QVERIFY(vm.cloud()->loggedIn());
        QVERIFY(vm.cloud()->email().endsWith(QStringLiteral("@example.com")));   // kitalált fiók

        vm.setDemoState(QStringLiteral("teaser"));
        QCOMPARE(vm.cloudAvailability(), QStringLiteral("teaser"));
        QVERIFY(!vm.dirty());                              // a teaser-kártya nem módosít semmit

        vm.setDemoState(QStringLiteral("B07"));
        QCOMPARE(vm.page(), QStringLiteral("summary"));
        QVERIFY(vm.promptModified());
        QVERIFY(!vm.dirty());
        QCOMPARE(vm.promptTabs().size(), 3);
        QCOMPARE(vm.schemaKind(), QStringLiteral("json"));
        QVERIFY(vm.schemaBody().contains(QStringLiteral("actionItems")));
        QCOMPARE(vm.promptVariables().size(), 1);          // csak amit a kód tényleg cserél

        // Demóban a mentés csak a memóriában történik.
        vm.setDemoState(QStringLiteral("dirty"));
        QCOMPARE(vm.changeCount(), 1);
        QSignalSpy saved(&vm, &SettingsViewModel::saved);
        QVERIFY(vm.save());
        QCOMPARE(saved.size(), 1);
        QVERIFY(!vm.dirty());
    }

    // ---- piszkozat, mentés, eldobás -----------------------------------------------------

    void cleanLoadAndDirtyTracking()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        QVERIFY(!vm.demo());
        QVERIFY(!vm.dirty());
        QCOMPARE(vm.changeCount(), 0);
        QCOMPARE(vm.userName(), live().userSpeakerName);
        QCOMPARE(vm.cloudAvailability(), QStringLiteral("none"));

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
        // Visszaállítva az eredetire: nem számít változásnak.
        vm.setAudioQuality(original);
        QCOMPARE(vm.changeCount(), 2);
        // A core-ban még semmi nem változott.
        QCOMPARE(live().mixdownMode, QStringLiteral("auto"));
        QCOMPARE(live().silenceAskMinutes, 3);
    }

    void saveWritesOnlyTheDifference()
    {
        startApp();
        // Beállítások, amelyeket az ablak nem mutat — nem veszhetnek el.
        AppSettings s = live();
        s.detectorId = QStringLiteral("linux-capture");
        s.cloudBaseUrl = QStringLiteral("http://127.0.0.1:9");
        s.languageHints = {QStringLiteral("de")};
        m_app->settings()->setSettings(s);

        SettingsViewModel vm;
        vm.setController(m_app.get());
        vm.setAudioQuality(QStringLiteral("medium"));
        vm.setDetectorIntervalSec(12);

        // Közben más is ment (pl. a felvevő / egy másik ablak): a nem érintett mező megmarad,
        // a saját mentetlen változás is.
        AppSettings other = live();
        other.askStopOnCallEnd = false;
        other.silenceAskMinutes = 9;
        m_app->settings()->setSettings(other);
        QCOMPARE(vm.changeCount(), 2);
        QCOMPARE(vm.audioQuality(), QStringLiteral("medium"));
        QCOMPARE(vm.silenceAskMinutes(), 9);               // a külső változás megjelent
        QCOMPARE(vm.askStopOnCallEnd(), false);

        QSignalSpy saved(&vm, &SettingsViewModel::saved);
        QVERIFY(vm.save());
        QCOMPARE(saved.size(), 1);
        QVERIFY(!vm.dirty());
        const AppSettings after = live();
        QCOMPARE(after.audioQuality, QStringLiteral("medium"));
        QCOMPARE(after.detectorIntervalSec, 12);
        QCOMPARE(after.silenceAskMinutes, 9);
        QCOMPARE(after.askStopOnCallEnd, false);
        QCOMPARE(after.detectorId, QStringLiteral("linux-capture"));
        QCOMPARE(after.cloudBaseUrl, QStringLiteral("http://127.0.0.1:9"));
        QCOMPARE(after.languageHints, QStringList{QStringLiteral("de")});
        // A lemezen is.
        QCOMPARE(settingsFile().value("audioQuality").toString(), QStringLiteral("medium"));
        QCOMPARE(settingsFile().value("detectorId").toString(), QStringLiteral("linux-capture"));
        // Változás nélkül a mentés nem ír semmit.
        QSignalSpy changed(m_app->settings(), &tanara::SettingsManager::settingsChanged);
        QVERIFY(vm.save());
        QCOMPARE(changed.size(), 0);
    }

    void discardRestoresEverythingIncludingTheThemePreview()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        QCOMPARE(vm.themeMode(), QStringLiteral("light"));
        vm.setThemeMode(QStringLiteral("dark"));
        QCOMPARE(AppContext::instance()->themeMode(), QStringLiteral("dark"));   // élő előnézet
        QVERIFY(AppContext::instance()->dark());
        vm.setUserName(QStringLiteral("Valaki Más"));
        vm.setAutoRecordAll(!vm.autoRecordAll());
        QCOMPARE(vm.changeCount(), 3);

        vm.discard();
        QVERIFY(!vm.dirty());
        QCOMPARE(vm.themeMode(), QStringLiteral("light"));
        QCOMPARE(AppContext::instance()->themeMode(), QStringLiteral("light"));
        QCOMPARE(vm.userName(), live().userSpeakerName);
        QCOMPARE(vm.autoRecordAll(), live().autoRecordAllDevices);
    }

    void themeIsPersistedThroughTheSignal()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        QSignalSpy theme(&vm, &SettingsViewModel::themeModeSaved);
        vm.setThemeMode(QStringLiteral("dark"));
        QCOMPARE(vm.changeCount(), 1);
        QVERIFY(vm.save());
        QCOMPARE(theme.size(), 1);
        QCOMPARE(theme.first().at(0).toString(), QStringLiteral("dark"));
        QVERIFY(!vm.dirty());
        QCOMPARE(AppContext::instance()->themeMode(), QStringLiteral("dark"));
        vm.discard();                                       // mentés után az eldobás nem állítja vissza
        QCOMPARE(AppContext::instance()->themeMode(), QStringLiteral("dark"));
    }

    void validationBlocksSaveButNotNavigation()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        const QString before = live().userSpeakerName;
        vm.setUserName(QStringLiteral("   "));
        QVERIFY(vm.errors().contains(QStringLiteral("userName")));
        // A lapok között szabadon lehet járni.
        vm.setPage(QStringLiteral("summary"));
        QCOMPARE(vm.page(), QStringLiteral("summary"));
        vm.setAudioQuality(QStringLiteral("low"));

        QSignalSpy saved(&vm, &SettingsViewModel::saved);
        QVERIFY(!vm.save());
        QCOMPARE(saved.size(), 0);
        QCOMPARE(vm.page(), QStringLiteral("general"));    // a hibás mező lapjára visz
        QCOMPARE(live().userSpeakerName, before);          // semmi nem íródott
        QCOMPARE(live().audioQuality, QStringLiteral("best"));

        vm.setUserName(QStringLiteral("Teszt Elek"));
        QVERIFY(vm.errors().isEmpty());
        QVERIFY(vm.save());
        QCOMPARE(live().userSpeakerName, QStringLiteral("Teszt Elek"));
        QVERIFY(m_app->knownPeople().contains(QStringLiteral("Teszt Elek")));   // a setUserSpeakerName útja
        QCOMPARE(live().audioQuality, QStringLiteral("low"));

        // Mappák: üres / relatív út hiba.
        vm.setFolder(QStringLiteral("notes"), QStringLiteral("relativ/mappa"));
        QVERIFY(vm.errors().contains(QStringLiteral("folder.notes")));
        QVERIFY(!vm.save());
        vm.setFolder(QStringLiteral("notes"), m_home->filePath(QStringLiteral("jegyzetek")));
        QVERIFY(vm.errors().isEmpty());
        QVERIFY(vm.save());
        QCOMPARE(live().notesDir, m_home->filePath(QStringLiteral("jegyzetek")));
        // TANARA_HOME mellett a belső adatok mappája rögzített.
        QCOMPARE(vm.folders().at(2).toMap().value("locked").toBool(), true);
        vm.setFolder(QStringLiteral("meta"), QStringLiteral("/máshova"));
        QVERIFY(!vm.dirty());
    }

    void foldersBrowseAndUsage()
    {
        startApp();
        // Két megbeszélés-mappa a felvételek alatt.
        const QString audio = tanara::paths::expandHome(live().audioDir);
        for (const char* name : {"egyik", "masik"}) {
            QVERIFY(QDir(audio).mkpath(QString::fromLatin1(name)));
            QFile f(QDir(audio).filePath(QString::fromLatin1(name) + QStringLiteral("/meeting.json")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(2500, 'x'));
        }
        FakeDialogs dialogs;
        SettingsViewModel vm;
        vm.setDialogsObject(&dialogs);
        vm.setController(m_app.get());
        QVERIFY(waitFor([&] { return !vm.folders().at(0).toMap().value("usage").toString().isEmpty(); }));
        const QString usage = vm.folders().at(0).toMap().value("usage").toString();
        QVERIFY2(usage.contains(QStringLiteral("5 kB")) && usage.contains(QStringLiteral("2")), qPrintable(usage));

        // Tallózás: a visszalépés nem változtat; a választott mappa a piszkozatba kerül.
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

    // ---- szolgáltatók --------------------------------------------------------------------

    void providersComeFromTheRegistry()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        // A lista a registry tartalma (a makett szolgáltatói helyett a ténylegesek).
        QStringList sttIds, llmIds;
        for (const QVariant& p : vm.stt()->providers()) sttIds << p.toMap().value("value").toString();
        for (const QVariant& p : vm.llm()->providers()) llmIds << p.toMap().value("value").toString();
        QVERIFY(sttIds.contains(QStringLiteral("soniox")));
        QVERIFY(sttIds.contains(QStringLiteral("whisper-compat")));
        QVERIFY(!sttIds.contains(tanara::cloud::ProviderId));
        QCOMPARE(llmIds, QStringList{QStringLiteral("openai-compat")});

        // A mezők a leíróból: a Sonioxnál cím + modell + kötelező kulcs, haladó nélkül.
        QCOMPARE(vm.stt()->providerId(), QStringLiteral("soniox"));
        const QVariantList fields = vm.stt()->fields();
        QCOMPARE(fields.size(), 3);
        QCOMPARE(fieldProp(fields, "baseUrl", "type").toString(), QStringLiteral("url"));
        QCOMPARE(fieldProp(fields, "apiKey", "type").toString(), QStringLiteral("secret"));
        QCOMPARE(fieldProp(fields, "apiKey", "required").toBool(), true);
        QVERIFY(vm.stt()->advancedFields().isEmpty());
        QVERIFY(vm.stt()->testable());
        // LLM: a hőmérséklet és a max. tokenek a „Haladó” alatt; a modell lekérhető lista.
        QCOMPARE(vm.llm()->advancedFields().size(), 2);
        QCOMPARE(fieldProp(vm.llm()->fields(), "model", "dynamic").toBool(), true);
        QCOMPARE(fieldProp(vm.llm()->advancedFields(), "temperature", "decimals").toInt(), 2);
        // Helyi végpontnál a kulcs helykitöltője: nem kell.
        QVERIFY(vm.llm()->placeholder(QStringLiteral("apiKey")).contains(QStringLiteral("helyi végpont")));
        vm.llm()->setValue(QStringLiteral("baseUrl"), QStringLiteral("https://api.example.com/v1"));
        QVERIFY(!vm.llm()->placeholder(QStringLiteral("apiKey")).contains(QStringLiteral("helyi végpont")));
    }

    void switchingProviderKeepsTheOtherOnesConfig()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        SettingsProviderModel* stt = vm.stt();
        stt->setValue(QStringLiteral("model"), QStringLiteral("stt-sajat-modell"));
        stt->setValue(QStringLiteral("apiKey"), QStringLiteral("soniox-kulcs"));
        QCOMPARE(vm.changeCount(), 2);

        QSignalSpy fields(stt, &SettingsProviderModel::fieldsChanged);
        stt->setProviderId(QStringLiteral("whisper-compat"));
        QVERIFY(fields.size() >= 1);
        QCOMPARE(stt->providerId(), QStringLiteral("whisper-compat"));
        // Az új szolgáltató a leíró alapértelmezéseivel indul.
        QCOMPARE(stt->value(QStringLiteral("baseUrl")).toString(), QStringLiteral("http://localhost:8000/v1"));
        QCOMPARE(stt->value(QStringLiteral("model")).toString(), QStringLiteral("whisper-1"));
        QCOMPARE(stt->fields().size(), 4);                 // cím, modell, nyelv, kulcs
        stt->setValue(QStringLiteral("language"), QStringLiteral("en"));
        QCOMPARE(vm.changeCount(), 3);                     // modell, kulcs, szolgáltató (az új config nem külön)

        // Vissza: a Soniox beállítása megvan.
        stt->setProviderId(QStringLiteral("soniox"));
        QCOMPARE(stt->value(QStringLiteral("model")).toString(), QStringLiteral("stt-sajat-modell"));
        QCOMPARE(stt->value(QStringLiteral("apiKey")).toString(), QStringLiteral("soniox-kulcs"));
        stt->setProviderId(QStringLiteral("whisper-compat"));
        QCOMPARE(stt->value(QStringLiteral("language")).toString(), QStringLiteral("en"));

        QVERIFY(vm.save());
        const AppSettings s = live();
        QCOMPARE(s.sttProviderId, QStringLiteral("whisper-compat"));
        QCOMPARE(s.sttConfigs.value(QStringLiteral("soniox")).model, QStringLiteral("stt-sajat-modell"));
        QCOMPARE(s.sttConfigs.value(QStringLiteral("whisper-compat")).baseUrl, QStringLiteral("http://localhost:8000/v1"));
        QCOMPARE(s.sttConfigs.value(QStringLiteral("whisper-compat")).extra.value(QStringLiteral("language")).toString(),
                 QStringLiteral("en"));
        // A kulcs a KeyStore-ban van, a beállítás-fájlban nem.
        QCOMPARE(m_app->secret(tanara::keys::SonioxApiKey), QStringLiteral("soniox-kulcs"));
        QFile f(m_app->settings()->settingsFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(!f.readAll().contains("soniox-kulcs"));

        // Újranyitva ugyanaz látszik.
        SettingsViewModel again;
        again.setController(m_app.get());
        QVERIFY(!again.dirty());
        QCOMPARE(again.stt()->providerId(), QStringLiteral("whisper-compat"));
        again.stt()->setProviderId(QStringLiteral("soniox"));
        QCOMPARE(again.stt()->value(QStringLiteral("apiKey")).toString(), QStringLiteral("soniox-kulcs"));
    }

    void fieldValidation()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        vm.llm()->setValue(QStringLiteral("baseUrl"), QStringLiteral("localhost:1234"));
        QVERIFY(!vm.llm()->fieldError(QStringLiteral("baseUrl")).isEmpty());
        vm.setPage(QStringLiteral("general"));
        QVERIFY(!vm.save());
        QCOMPARE(vm.page(), QStringLiteral("services"));
        vm.llm()->setValue(QStringLiteral("baseUrl"), QString());
        QVERIFY(!vm.llm()->fieldError(QStringLiteral("baseUrl")).isEmpty());   // kötelező
        vm.llm()->setValue(QStringLiteral("baseUrl"), QStringLiteral("http://localhost:1234/v1"));
        QVERIFY(vm.llm()->fieldError(QStringLiteral("baseUrl")).isEmpty());
        vm.llm()->setValue(QStringLiteral("maxTokens"), 3);
        QVERIFY(!vm.llm()->fieldError(QStringLiteral("maxTokens")).isEmpty());
        vm.llm()->setValue(QStringLiteral("maxTokens"), 30000);
        vm.llm()->setValue(QStringLiteral("temperature"), 0.35);
        QVERIFY(vm.save());
        QCOMPARE(live().llmSelected().maxTokens, 30000);
        QCOMPARE(live().llmSelected().temperature, 0.35);

        // A 0 hőmérséklet érvényes érték: mentés után is 0 látszik (nem az alapértelmezés).
        vm.llm()->setValue(QStringLiteral("temperature"), 0.0);
        QVERIFY(vm.save());
        QCOMPARE(live().llmSelected().temperature, 0.0);
        QCOMPARE(vm.llm()->value(QStringLiteral("temperature")).toDouble(), 0.0);
        QVERIFY(!vm.dirty());

        // Gépelés közben a szóköz megmarad a mezőben; a mentés vágja le.
        vm.llm()->setValue(QStringLiteral("model"), QStringLiteral("sajat modell "));
        QCOMPARE(vm.llm()->value(QStringLiteral("model")).toString(), QStringLiteral("sajat modell "));
        QVERIFY(vm.save());
        QCOMPARE(live().llmSelected().model, QStringLiteral("sajat modell"));

        // Egy korábbról tárolt, tartományon kívüli érték nem akadályozza a többi beállítás mentését.
        AppSettings s = live();
        s.llmConfigs[s.llmProviderId].maxTokens = 100;
        m_app->settings()->setSettings(s);
        QVERIFY(vm.llm()->fieldError(QStringLiteral("maxTokens")).isEmpty());
        vm.setAudioQuality(QStringLiteral("low"));
        QVERIFY(vm.save());
        QCOMPARE(live().llmSelected().maxTokens, 100);
    }

    void warnDotFollowsReadiness()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        // Friss telepítés: nincs Soniox-kulcs → a „Szolgáltatások” mellett pötty.
        QVERIFY(vm.servicesWarn());
        QSignalSpy warn(&vm, &SettingsViewModel::servicesWarnChanged);
        vm.stt()->setValue(QStringLiteral("apiKey"), QStringLiteral("k"));
        QVERIFY(!vm.servicesWarn());                       // már a piszkozatra is reagál
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
        SettingsViewModel vm;
        vm.setController(m_app.get());
        vm.stt()->setValue(QStringLiteral("apiKey"), QStringLiteral("x"));   // az STT ne zavarja a pöttyöt
        SettingsProviderModel* llm = vm.llm();
        llm->setValue(QStringLiteral("baseUrl"), http.base());
        llm->setValue(QStringLiteral("model"), QStringLiteral("modell-a"));
        llm->setValue(QStringLiteral("apiKey"), QStringLiteral("rossz"));
        QCOMPARE(llm->testState(), QString());

        // 1) elutasított kulcs → hiba emberi mondattal és technikai kóddal, pötty.
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
        QCOMPARE(http.auths.last(), QByteArray("Bearer rossz"));   // a PISZKOZAT kulcsa megy, mentés előtt

        // 2) bármelyik mező módosítása elavulttá teszi az eredményt.
        llm->setValue(QStringLiteral("apiKey"), QStringLiteral("jo-kulcs"));
        QCOMPARE(llm->testState(), QString());
        QVERIFY(!vm.servicesWarn());

        // 3) siker: állapot + válaszidő; a modell-lista a „Lekérés” listáját is feltölti.
        llm->test();
        QVERIFY(waitFor([&] { return llm->testState() == QLatin1String("ok"); }));
        QVERIFY(llm->statusText().contains(QStringLiteral("ms")));
        QVERIFY(llm->errorText().isEmpty());
        QVERIFY(llm->warningText().isEmpty());
        QCOMPARE(llm->options(QStringLiteral("model")),
                 (QStringList{QStringLiteral("modell-a"), QStringLiteral("modell-b")}));

        // 4) siker, de a beállított modell nincs a listán → figyelmeztetés.
        llm->setValue(QStringLiteral("model"), QStringLiteral("ismeretlen"));
        llm->test();
        QVERIFY(waitFor([&] { return llm->testState() == QLatin1String("ok"); }));
        QVERIFY(llm->warningText().contains(QStringLiteral("ismeretlen")));

        // 5) „Lekérés”: külön művelet, a teszt állapotát nem írja át.
        http.handler = [](const QString&, const QByteArray&) {
            return FakeReply{200, "{\"data\":[{\"id\":\"friss-modell\"}]}"};
        };
        llm->fetchModels();
        QVERIFY(llm->fetching());
        QVERIFY(waitFor([&] { return !llm->fetching(); }));
        QCOMPARE(llm->options(QStringLiteral("model")), QStringList{QStringLiteral("friss-modell")});
        QVERIFY(llm->fetchError().isEmpty());
        QCOMPARE(llm->testState(), QStringLiteral("ok"));

        // 6) a szerver leáll → „nem érhető el”, ECONNREFUSED; a lekérés hibája külön szöveg.
        http.close();
        llm->setValue(QStringLiteral("baseUrl"), m_closedPortBase);
        llm->test();
        QVERIFY(waitFor([&] { return llm->testState() == QLatin1String("failed"); }));
        QCOMPARE(llm->errorCode(), QStringLiteral("ECONNREFUSED"));
        QVERIFY(!llm->errorText().isEmpty());
        llm->fetchModels();
        QVERIFY(waitFor([&] { return !llm->fetching(); }));
        QVERIFY(!llm->fetchError().isEmpty());

        // A teszt semmit nem mentett.
        QVERIFY(vm.dirty());
        QVERIFY(m_app->secret(tanara::keys::LlmApiKey).isEmpty());
        vm.discard();
        QCOMPARE(llm->testState(), QString());
    }

    // ---- eszközök ------------------------------------------------------------------------

    void deviceRenameRoundTrip()
    {
        startApp();
        m_app->refreshDevices();
        SettingsViewModel vm;
        vm.setController(m_app.get());
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
        // Mentésig a core nem tud róla.
        QCOMPARE(tanara::devicenames::displayName(raw), factory);

        QVERIFY(vm.save());
        QCOMPARE(live().deviceNames.value(raw), QStringLiteral("Saját mikrofonom"));
        QCOMPARE(tanara::devicenames::displayName(raw), QStringLiteral("Saját mikrofonom"));
        // A sávok neve is követi (Sávok fül).
        tanara::Track t;
        t.id = QStringLiteral("t");
        t.deviceName = raw;
        t.kind = tanara::TrackKind::Other;
        QCOMPARE(tanara::tracknames::friendlyNames({t}).first(), QStringLiteral("Saját mikrofonom"));

        // Új ablak: a mentett név látszik.
        SettingsViewModel again;
        again.setController(m_app.get());
        QCOMPARE(row(again, 0, "name").toString(), QStringLiteral("Saját mikrofonom"));

        // Üres név → vissza a gyári névre; a kulcs kikerül a beállításból.
        again.renameDevice(0, QString());
        QCOMPARE(row(again, 0, "name").toString(), factory);
        QCOMPARE(again.changeCount(), 1);
        QVERIFY(again.save());
        QVERIFY(!live().deviceNames.contains(raw));
        QCOMPARE(tanara::devicenames::displayName(raw), factory);
        // A másik (nyitva maradt) nézetmodell is átvette.
        QCOMPARE(row(vm, 0, "name").toString(), factory);
    }

    void tracksTabFollowsADeviceRename()
    {
        // A „Sávok” fül modellje a mentett eszköznévre azonnal átvált (újraindítás nélkül).
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
        tracks.setController(m_app.get());
        tracks.setMeetingId(m.id);
        QCOMPARE(tracks.rowCount(), 1);
        const int nameRole = tracks.roleNames().key("displayName");
        QCOMPARE(tracks.data(tracks.index(0, 0), nameRole).toString(), QStringLiteral("Vonalbemenet"));

        AppSettings s = live();
        s.deviceNames.insert(raw, QStringLiteral("Gitár"));
        m_app->settings()->setSettings(s);                 // ezt teszi a Beállítások mentése
        QCOMPARE(tracks.data(tracks.index(0, 0), nameRole).toString(), QStringLiteral("Gitár"));

        s.deviceNames.remove(raw);
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
        vm.renameDevice(3, factory);                       // a gyári névre átnevezés = nincs saját név
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
        SettingsViewModel vm;
        vm.setController(m_app.get());
        if (vm.deviceCount() == 0)
            QSKIP("Ezen a gépen nincs hangeszköz.");
        const QString raw = row(vm, 0, "rawName").toString();
        const bool was = row(vm, 0, "selected").toBool();
        vm.toggleDevice(0);
        QCOMPARE(vm.changeCount(), 1);
        QVERIFY(vm.save());
        QCOMPARE(m_app->lastUsedDeviceNames().contains(raw), !was);
    }

    // ---- hívásfigyelő --------------------------------------------------------------------

    void watchedApps()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        const int before = vm.watchedApps().size();
        QVERIFY(before > 0);
        QVERIFY(!vm.addWatchedApp(QStringLiteral("  ")).isEmpty());        // üres
        QVERIFY(!vm.addWatchedApp(QStringLiteral("ZOOM")).isEmpty());      // már szerepel
        QCOMPARE(vm.addWatchedApp(QStringLiteral(" Signal ")), QString());
        QCOMPARE(vm.watchedApps().size(), before + 1);
        QCOMPARE(vm.watchedApps().last().toMap().value("match").toString(), QStringLiteral("signal"));
        QCOMPARE(vm.changeCount(), 1);
        // A most futó folyamatok listája nem tartalmazza a már figyelteket.
        for (const QVariant& a : vm.runningApps())
            QVERIFY(!a.toMap().value("match").toString().contains(QStringLiteral("signal")));

        // Üres listával nem menthető (a figyelő semmire nem jelezne).
        while (!vm.watchedApps().isEmpty()) vm.removeWatchedApp(0);
        QVERIFY(vm.errors().contains(QStringLiteral("watchedApps")));
        QVERIFY(!vm.save());
        QCOMPARE(vm.page(), QStringLiteral("watcher"));
        QCOMPARE(vm.addWatchedApp(QStringLiteral("teams")), QString());
        QVERIFY(vm.save());
        QCOMPARE(live().knownCallApps, QStringList{QStringLiteral("teams")});
    }

    void liveCallLineFollowsTheDetector()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        auto* detector = new FakeDetector;
        vm.setDetectorForTest(detector);                   // a nézetmodell birtokolja
        QVERIFY(!vm.liveCallActive());

        detector->signal.active = true;
        detector->signal.appId = QStringLiteral("teams");
        detector->signal.appName = QStringLiteral("Microsoft Teams");
        vm.setWatching(true);
        QVERIFY(waitFor([&] { return vm.liveCallActive(); }));
        QVERIFY(vm.detectorAvailable());
        QCOMPARE(vm.liveCallApp(), QStringLiteral("Microsoft Teams"));
        QCOMPARE(detector->configured, live().knownCallApps);   // a piszkozat listájával kérdez
        bool highlighted = false;
        for (const QVariant& a : vm.watchedApps())
            if (a.toMap().value("match").toString() == QLatin1String("teams"))
                highlighted = a.toMap().value("active").toBool();
        QVERIFY(highlighted);
        // A lap eltűnik → nincs több lekérdezés.
        vm.setWatching(false);
        QVERIFY(!vm.watching());
    }

    void autostartSwitchNeverTouchesTheRealEntryInASandbox()
    {
        startApp();
        const QString real = tanara::autostart::watcherEntryPath();
        const bool existed = !real.isEmpty() && QFile::exists(real);
        const QDateTime stamp = existed ? QFileInfo(real).lastModified() : QDateTime();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        vm.setWatcherAutostart(!vm.watcherAutostart());
        QVERIFY(vm.save());
        QCOMPARE(live().watcherAutostart, vm.watcherAutostart());   // a beállítás mentődik…
        if (!real.isEmpty()) {                                      // …de a valódi fájl érintetlen
            QCOMPARE(QFile::exists(real), existed);
            if (existed) QCOMPARE(QFileInfo(real).lastModified(), stamp);
        }
    }

    // ---- promptok ------------------------------------------------------------------------

    void promptsModifiedAndReset()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        QCOMPARE(vm.promptIndex(), 0);
        const QString def = vm.promptText();
        QVERIFY(def.contains(QStringLiteral("{{NYELV}}")));
        QVERIFY(!vm.promptModified());
        QVERIFY(live().summaryPrompt.isEmpty());

        // Csak szóközben eltérő szöveg nem „módosítás”.
        vm.setPromptText(def + QStringLiteral("\n"));
        QVERIFY(!vm.promptModified());
        QVERIFY(!vm.dirty());

        QSignalSpy textChanged(&vm, &SettingsViewModel::promptTextChanged);
        vm.setPromptText(def + QStringLiteral("\n9. Légy tömör."));
        QVERIFY(vm.promptModified());
        QCOMPARE(vm.promptTabs().at(0).toMap().value("modified").toBool(), true);
        QCOMPARE(vm.promptTabs().at(1).toMap().value("modified").toBool(), false);
        QCOMPARE(vm.changeCount(), 1);
        QCOMPARE(textChanged.size(), 0);                   // a szerkesztő saját írása nem jön vissza

        // Másik fül: a szöveg megmarad, a séma a fülhöz tartozik.
        vm.setPromptIndex(2);
        QCOMPARE(textChanged.size(), 1);
        QVERIFY(!vm.promptModified());
        QCOMPARE(vm.schemaKind(), QStringLiteral("markdown"));
        vm.setPromptText(vm.promptText() + QStringLiteral("\nMég egy szabály."));
        QCOMPARE(vm.changeCount(), 2);
        vm.setPromptIndex(0);
        QVERIFY(vm.promptText().endsWith(QStringLiteral("9. Légy tömör.")));

        QVERIFY(vm.save());
        QVERIFY(live().summaryPrompt.endsWith(QStringLiteral("9. Légy tömör.")));
        QVERIFY(live().topicExtractionPrompt.isEmpty());   // érintetlen → üres (a kód-default él)
        QVERIFY(live().topicAnalysisPrompt.endsWith(QStringLiteral("Még egy szabály.")));
        QVERIFY(vm.promptModified());                      // mentés után is „módosítva”
        QVERIFY(!vm.dirty());

        // Visszaállítás: a beépített szöveg, és a beállításban újra üres.
        vm.resetPrompt();
        QCOMPARE(vm.promptText(), def);
        QVERIFY(!vm.promptModified());
        QCOMPARE(vm.changeCount(), 1);
        QVERIFY(vm.save());
        QVERIFY(live().summaryPrompt.isEmpty());

        vm.setSummaryLanguage(QStringLiteral("angol"));
        QVERIFY(vm.save());
        QCOMPARE(live().summaryLanguage, QStringLiteral("angol"));
        vm.setSummaryLanguage(QStringLiteral("   "));        // üres célnyelv nincs
        QCOMPARE(vm.summaryLanguage(), QStringLiteral("angol"));
    }

    // ---- mély hivatkozás (B04) -----------------------------------------------------------

    void deepLinkHighlightsAndReturnsAfterSave()
    {
        startApp();
        SettingsViewModel vm;
        vm.setController(m_app.get());
        // A ShellActions lapnevei.
        vm.openPage(QStringLiteral("watcher"));
        QCOMPARE(vm.page(), QStringLiteral("watcher"));
        vm.openPage(QStringLiteral("recording"));
        QCOMPARE(vm.page(), QStringLiteral("recording"));
        vm.openPage(QStringLiteral("summary"));
        QCOMPARE(vm.page(), QStringLiteral("summary"));
        vm.openPage(QString());                            // üres: marad, ahol volt
        QCOMPARE(vm.page(), QStringLiteral("summary"));
        vm.openPage(QStringLiteral("cloud"));              // cloud nélkül is a Szolgáltatások lap
        QCOMPARE(vm.page(), QStringLiteral("services"));
        QCOMPARE(vm.serviceMode(), QStringLiteral("own"));
        QVERIFY(vm.focusField().isEmpty());

        QSignalSpy back(&vm, &SettingsViewModel::returnRequested);
        vm.openPage(QStringLiteral("providers"), QStringLiteral("stt"));
        QCOMPARE(vm.page(), QStringLiteral("services"));
        QCOMPARE(vm.focusField(), QStringLiteral("stt"));
        QVERIFY(vm.stt()->highlighted());
        QVERIFY(vm.focusBannerText().contains(QStringLiteral("átírás"), Qt::CaseInsensitive));

        // Más mentése, amíg a kulcs hiányzik: marad a kiemelés, nincs visszatérés.
        vm.setAudioQuality(QStringLiteral("high"));
        QVERIFY(vm.save());
        QCOMPARE(back.size(), 0);
        QCOMPARE(vm.focusField(), QStringLiteral("stt"));

        // A hiány megszűnik → mentés után vissza a megbeszéléshez.
        vm.stt()->setValue(QStringLiteral("apiKey"), QStringLiteral("soniox-kulcs"));
        QVERIFY(vm.save());
        QCOMPARE(back.size(), 1);
        QVERIFY(vm.focusField().isEmpty());
        QVERIFY(!vm.stt()->highlighted());
        QVERIFY(m_app->canRun(tanara::WorkflowStep::Transcribe, QString()).blockerKind
                != tanara::BlockerKind::ProviderConfig);
    }

    // ---- Tanara Cloud --------------------------------------------------------------------

    void cloudLiveModeIsADraftChoice()
    {
        startApp("live");
        if (!m_app->cloudLive())
            QSKIP("A build Tanara Cloud kliens nélkül készült.");
        FakeDialogs dialogs;
        SettingsViewModel vm;
        vm.setDialogsObject(&dialogs);
        vm.setController(m_app.get());
        QCOMPARE(vm.cloudAvailability(), QStringLiteral("live"));
        QCOMPARE(vm.serviceMode(), QStringLiteral("own"));
        // A saját kulcsos kártyán a Cloud nem szerepel a szolgáltatók között.
        for (const QVariant& p : vm.stt()->providers())
            QVERIFY(p.toMap().value("value").toString() != tanara::cloud::ProviderId);

        vm.stt()->setProviderId(QStringLiteral("whisper-compat"));
        vm.setServiceMode(QStringLiteral("cloud"));
        QCOMPARE(vm.serviceMode(), QStringLiteral("cloud"));
        QVERIFY(vm.dirty());
        QVERIFY(vm.servicesWarn());                        // nincs bejelentkezve
        QVERIFY(!vm.cloud()->loggedIn());
        // Semmi kitalált szám: bejelentkezés nélkül nincs egyenleg, se óra.
        QVERIFY(vm.cloud()->balanceText().isEmpty());
        QVERIFY(vm.cloud()->hoursText().isEmpty());
        QCOMPARE(live().sttProviderId, QStringLiteral("soniox"));   // mentésig semmi nem vált

        // Vissza: a korábbi saját választás (a még nem mentett is) megmarad.
        vm.setServiceMode(QStringLiteral("own"));
        QCOMPARE(vm.stt()->providerId(), QStringLiteral("whisper-compat"));
        QCOMPARE(vm.llm()->providerId(), QStringLiteral("openai-compat"));

        vm.setServiceMode(QStringLiteral("cloud"));
        vm.cloud()->setSttTier(QStringLiteral("fast"));
        vm.cloud()->setEstimateBeforeRun(false);
        vm.cloud()->setMeetingLanguage(QStringLiteral("en"));
        QVERIFY(vm.save());
        const AppSettings s = live();
        QCOMPARE(s.sttProviderId, tanara::cloud::ProviderId);
        QCOMPARE(s.llmProviderId, tanara::cloud::ProviderId);
        QCOMPARE(s.cloudSttTier, QStringLiteral("fast"));
        QCOMPARE(s.cloudEstimateBeforeRun, false);
        QCOMPARE(s.languageHints, QStringList{QStringLiteral("en")});
        QVERIFY(s.sttConfigs.contains(QStringLiteral("soniox")));          // a saját kulcsos beállítás megmarad
        QVERIFY(m_app->usesCloud(tanara::WorkflowStep::Transcribe));

        // Újranyitva Cloud módban indul; a visszaváltás a saját szolgáltatókra visz.
        SettingsViewModel again;
        again.setController(m_app.get());
        QCOMPARE(again.serviceMode(), QStringLiteral("cloud"));
        again.setServiceMode(QStringLiteral("own"));
        QVERIFY(again.stt()->providerId() != tanara::cloud::ProviderId);
        QVERIFY(again.llm()->providerId() != tanara::cloud::ProviderId);

        // A modális folyamatok a Widgets-ablakokat kérik (itt: az ál-párbeszédablakokat).
        vm.cloud()->login();
        vm.cloud()->topup();
        vm.cloud()->pickExpert(QStringLiteral("stt"));
        QCOMPARE(dialogs.calls, (QStringList{QStringLiteral("login"), QStringLiteral("topup"),
                                             QStringLiteral("expert:stt")}));
    }

    void mixedCloudSetupSurvivesADeepLink()
    {
        // Átírás a Cloudban, összefoglaló saját kulccsal: a hiányzó LLM-hez vezető mély
        // hivatkozás nem válthatja le csendben az átírást a Cloudról.
        startApp("live");
        if (!m_app->cloudLive())
            QSKIP("A build Tanara Cloud kliens nélkül készült.");
        AppSettings s = live();
        s.sttProviderId = tanara::cloud::ProviderId;
        s.sttConfigs[tanara::cloud::ProviderId].type = tanara::cloud::ProviderId;
        m_app->settings()->setSettings(s);

        SettingsViewModel vm;
        vm.setController(m_app.get());
        QCOMPARE(vm.serviceMode(), QStringLiteral("own"));
        vm.openPage(QStringLiteral("providers"), QStringLiteral("llm"));
        QVERIFY(!vm.dirty());
        QCOMPARE(vm.stt()->providerId(), tanara::cloud::ProviderId);
        QVERIFY(vm.stt()->loginProvider());
        vm.llm()->setValue(QStringLiteral("model"), QStringLiteral("masik-modell"));
        QVERIFY(vm.save());
        QCOMPARE(live().sttProviderId, tanara::cloud::ProviderId);
        QCOMPARE(live().llmSelected().model, QStringLiteral("masik-modell"));
    }

    void cloudTeaserOnlyShowsTheOffer()
    {
        startApp("teaser");
        if (!m_app->cloudTeaser())
            QSKIP("A build a „Hamarosan” panel nélkül készült.");
        SettingsViewModel vm;
        vm.setController(m_app.get());
        QCOMPARE(vm.cloudAvailability(), QStringLiteral("teaser"));
        vm.setServiceMode(QStringLiteral("cloud"));
        QCOMPARE(vm.serviceMode(), QStringLiteral("cloud"));   // a várólista-ajánlat látszik…
        QVERIFY(!vm.dirty());                                   // …de nincs mit menteni
        QCOMPARE(live().sttProviderId, QStringLiteral("soniox"));

        // A feliratkozás gombja csak érvényes címmel és hozzájárulással él (küldés nincs a tesztben).
        SettingsCloudModel* cloud = vm.cloud();
        QVERIFY(!cloud->waitCanSubmit());
        cloud->setWaitEmail(QStringLiteral("valaki@example.com"));
        QVERIFY(!cloud->waitCanSubmit());
        cloud->setWaitConsent(true);
        QVERIFY(cloud->waitCanSubmit());
        cloud->setWaitEmail(QStringLiteral("nem-email"));
        QVERIFY(!cloud->waitCanSubmit());
        cloud->toggleWaitLanguage(QStringLiteral("hu"));
        QCOMPARE(cloud->waitLanguages(), QStringList{QStringLiteral("hu")});
        QVERIFY(cloud->joinedEmail().isEmpty());
    }

    // ---- az ablak és a gazdája -----------------------------------------------------------

    void windowLoadsEveryDemoStateWithoutWarnings_data()
    {
        QTest::addColumn<QString>("state");
        for (const char* st : {"B01", "B02", "B03", "B04", "B05", "B06", "B07", "dirty", "unsaved",
                               "schema", "teaser", "cloudOut", "addApp", "logout", "reset"})
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
        QCOMPARE(vm->controller(), m_app.get());
        QCOMPARE(vm->dialogs(), &dialogs);
        QCOMPARE(vm->page(), QStringLiteral("watcher"));
        QTest::qWait(100);
        QVERIFY(vm->watching());                           // a lapja látszik → élő észlelés
        QVERIFY(!vm->monitoring());

        // Újabb kérés nyitott ablaknál: csak a lap vált, a piszkozat marad.
        vm->setSilenceAskMinutes(11);
        QVERIFY(host.open(QStringLiteral("providers"), QStringLiteral("llm")));
        QCOMPARE(vm->page(), QStringLiteral("services"));
        QCOMPARE(vm->focusField(), QStringLiteral("llm"));
        QVERIFY(vm->dirty());
        QTest::qWait(50);
        QVERIFY(!vm->watching());

        // Bezárás mentetlen változással: az ablak nyitva marad (a kérdés a QML-ben él).
        host.window()->close();
        QTest::qWait(100);
        QVERIFY(host.isVisible());
        QCOMPARE(closed.size(), 0);

        // Mentés a nézetmodellen át → a gazda jelzi; utána a bezárás sima.
        QVERIFY(vm->save());
        QCOMPARE(saved.size(), 1);
        QCOMPARE(live().silenceAskMinutes, 11);
        host.window()->close();
        QVERIFY(waitFor([&] { return !host.isVisible(); }));
        QCOMPARE(closed.size(), 1);

        // Újranyitva friss állapot a lemezről, a kért lapon.
        AppSettings s = live();
        s.silenceAskMinutes = 4;
        m_app->settings()->setSettings(s);
        QVERIFY(host.open(QStringLiteral("summary")));
        QCOMPARE(vm->page(), QStringLiteral("summary"));
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

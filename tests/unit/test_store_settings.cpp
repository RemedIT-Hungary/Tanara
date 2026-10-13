// Tanara: Settings + Meeting store unit tests.
//
// IMPORTANT: the tests/unit/*.cpp files are GLOBbed into a SINGLE exe, and test_smoke.cpp
// already owns QTEST_MAIN. So no further QTEST_MAIN/QTEST_GUILESS_MAIN can be used here: it
// would cause a main symbol clash.
//
// The solution for the MVP: we drive the test classes with a registrar that runs at
// static-init time (QTest::qExec), so there is no main and no clash. If we switch to a
// multi-test harness on integration, this file is easy to switch over.
#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>

#include "tanara/Types.h"
#include "tanara/SettingsManager.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/KeyStore.h"

using namespace tanara;

// ===========================================================================
class SettingsTests : public QObject {
    Q_OBJECT
private slots:

    // Tanara Cloud settings round trip + old settings.json gives defaults.
    void appSettings_cloudFieldsRoundTrip()
    {
        AppSettings s;
        s.cloudEnabled  = true;
        s.cloudBaseUrl  = QStringLiteral("http://127.0.0.1:8300");
        s.cloudSttTier  = QStringLiteral("fast");
        s.cloudLlmTier  = QStringLiteral("accurate");
        s.cloudSttModel = QStringLiteral("soniox/stt-async-v5");
        s.waitlistEmail = QStringLiteral("anna@example.com");
        const AppSettings r = appSettingsFromJson(toJson(s));
        QCOMPARE(r.cloudEnabled, true);
        QCOMPARE(r.cloudBaseUrl, s.cloudBaseUrl);
        QCOMPARE(r.cloudSttTier, QStringLiteral("fast"));
        QCOMPARE(r.cloudSttModel, s.cloudSttModel);
        QVERIFY(r.cloudLlmModel.isEmpty());
        QCOMPARE(r.waitlistEmail, s.waitlistEmail);

        const AppSettings old = appSettingsFromJson(QJsonObject{ { QStringLiteral("audioDir"), QStringLiteral("/x") } });
        QCOMPARE(old.cloudEnabled, false);                       // before launch: teaser
        QCOMPARE(old.cloudSttTier, QStringLiteral("accurate"));
        QVERIFY(old.waitlistEmail.isEmpty());
    }

    // The "Getting started" flag goes both ways; in an old settings.json it is missing, so false.
    void appSettings_onboardingDoneRoundTrip()
    {
        AppSettings s;
        QCOMPARE(s.onboardingDone, false);
        s.onboardingDone = true;
        QCOMPARE(appSettingsFromJson(toJson(s)).onboardingDone, true);
        const AppSettings old = appSettingsFromJson(QJsonObject{ { QStringLiteral("audioDir"), QStringLiteral("/x") } });
        QCOMPARE(old.onboardingDone, false);
    }

    // AppSettings JSON round trip (NEW multi-provider shape).
    // A track's start offset (Track::startOffsetMs) goes both ways in meeting.json; at 0 it is omitted.
    void track_startOffsetRoundTrip()
    {
        Meeting m;
        m.id = QStringLiteral("m1");
        Track t;
        t.id = QStringLiteral("loop");
        t.file = QStringLiteral("track_loop.ogg");
        t.startOffsetMs = 57200;
        Track zero;
        zero.id = QStringLiteral("mic");
        m.tracks << t << zero;

        const QJsonObject o = toJson(m);
        const QJsonArray tracks = o.value(QStringLiteral("tracks")).toArray();
        QCOMPARE(tracks.at(0).toObject().value(QStringLiteral("startOffsetMs")).toDouble(), 57200.0);
        QVERIFY(!tracks.at(1).toObject().contains(QStringLiteral("startOffsetMs")));
        const Meeting back = meetingFromJson(o);
        QCOMPARE(back.tracks.at(0).startOffsetMs, qint64(57200));
        QCOMPARE(back.tracks.at(1).startOffsetMs, qint64(0));
    }

    void appSettings_jsonRoundTrip()
    {
        AppSettings s;
        s.audioDir        = QStringLiteral("/tmp/rec");
        s.notesDir        = QStringLiteral("/tmp/notes");
        s.metadataDir     = QStringLiteral("/tmp/meta");
        s.userSpeakerName = QStringLiteral("Ádám");
        s.languageHints   = QStringList{QStringLiteral("hu"), QStringLiteral("en")};

        s.sttProviderId = QStringLiteral("soniox");
        ProviderConfig stt;
        stt.type    = QStringLiteral("soniox");
        stt.baseUrl = QStringLiteral("https://api.soniox.com/v1");
        stt.model   = QStringLiteral("stt-async-v5");
        stt.apiKey  = QStringLiteral("SECRET-should-not-persist");
        s.sttConfigs.insert(QStringLiteral("soniox"), stt);

        s.llmProviderId = QStringLiteral("openai-compat");
        ProviderConfig llm;
        llm.type    = QStringLiteral("openai-compat");
        llm.baseUrl = QStringLiteral("http://localhost:1234/v1");
        llm.model   = QStringLiteral("gemma-4-12b");
        llm.apiKey  = QStringLiteral("LLM-SECRET-should-not-persist");
        s.llmConfigs.insert(QStringLiteral("openai-compat"), llm);

        const QJsonObject o = toJson(s);
        const AppSettings r = appSettingsFromJson(o);

        QCOMPARE(r.audioDir, s.audioDir);
        QCOMPARE(r.notesDir, s.notesDir);
        QCOMPARE(r.metadataDir, s.metadataDir);
        QCOMPARE(r.userSpeakerName, s.userSpeakerName);
        QCOMPARE(r.languageHints, s.languageHints);

        // The selected provider ids come through.
        QCOMPARE(r.sttProviderId, s.sttProviderId);
        QCOMPARE(r.llmProviderId, s.llmProviderId);

        // The selected configs' fields come through (from the map / by accessor).
        QCOMPARE(r.sttSelected().type,    s.sttSelected().type);
        QCOMPARE(r.sttSelected().baseUrl, s.sttSelected().baseUrl);
        QCOMPARE(r.sttSelected().model,   s.sttSelected().model);
        QCOMPARE(r.llmSelected().model,   s.llmSelected().model);
        QVERIFY(r.sttConfigs.contains(QStringLiteral("soniox")));
        QVERIFY(r.llmConfigs.contains(QStringLiteral("openai-compat")));

        // apiKey is NEVER persisted into the JSON, not into the providers block either.
        const QJsonObject sttProv =
            o.value(QStringLiteral("sttProviders")).toObject()
             .value(QStringLiteral("soniox")).toObject();
        QVERIFY(!sttProv.contains(QStringLiteral("apiKey")));
        const QJsonObject llmProv =
            o.value(QStringLiteral("llmProviders")).toObject()
             .value(QStringLiteral("openai-compat")).toObject();
        QVERIFY(!llmProv.contains(QStringLiteral("apiKey")));

        QVERIFY(r.sttSelected().apiKey.isEmpty());
        QVERIFY(r.llmSelected().apiKey.isEmpty());
    }

    // JSON round trip of the meeting watcher fields (with non-default values).
    void appSettings_detectorRoundTrip()
    {
        AppSettings s;
        s.detectorEnabled     = false;
        s.detectorIntervalSec = 15;
        s.watcherAutostart    = true;
        s.detectorId          = QStringLiteral("linux-capture");
        s.knownCallApps       = QStringList{QStringLiteral("zoom"), QStringLiteral("jitsi")};

        const AppSettings r = appSettingsFromJson(toJson(s));

        QCOMPARE(r.detectorEnabled, false);
        QCOMPARE(r.detectorIntervalSec, 15);
        QCOMPARE(r.watcherAutostart, true);
        QCOMPARE(r.detectorId, QStringLiteral("linux-capture"));
        QCOMPARE(r.knownCallApps, (QStringList{QStringLiteral("zoom"), QStringLiteral("jitsi")}));
    }

    // Back-compat: an (old) JSON without the watcher fields loads with the defaults.
    void appSettings_detectorDefaultsWhenAbsent()
    {
        const AppSettings d;                          // built-in defaults
        const AppSettings r = appSettingsFromJson(QJsonObject{});   // empty JSON
        QCOMPARE(r.detectorEnabled, d.detectorEnabled);
        QCOMPARE(r.detectorIntervalSec, d.detectorIntervalSec);
        QCOMPARE(r.watcherAutostart, d.watcherAutostart);
        QVERIFY(!r.knownCallApps.isEmpty());          // the default list stays
    }

    // The old {stt:{...},llm:{...}} shape migrates into the NEW maps.
    void appSettings_migratesOldShape()
    {
        // A hand-built OLD-shape JSON (no sttProviders/llmProviders).
        QJsonObject oldStt;
        oldStt[QStringLiteral("type")]    = QStringLiteral("soniox");
        oldStt[QStringLiteral("baseUrl")] = QStringLiteral("https://api.soniox.com/v1");
        oldStt[QStringLiteral("model")]   = QStringLiteral("stt-async-v5");

        QJsonObject oldLlm;
        oldLlm[QStringLiteral("type")]    = QStringLiteral("openai-compat");
        oldLlm[QStringLiteral("baseUrl")] = QStringLiteral("http://localhost:1234/v1");
        oldLlm[QStringLiteral("model")]   = QStringLiteral("gemma-4-12b");

        QJsonObject o;
        o[QStringLiteral("audioDir")]        = QStringLiteral("/tmp/rec");
        o[QStringLiteral("userSpeakerName")] = QStringLiteral("Ádám");
        o[QStringLiteral("stt")]             = oldStt;
        o[QStringLiteral("llm")]             = oldLlm;

        const AppSettings r = appSettingsFromJson(o);

        // The type became the id (an empty type falls back to "soniox"/"openai-compat").
        QCOMPARE(r.sttProviderId, QStringLiteral("soniox"));
        QCOMPARE(r.llmProviderId, QStringLiteral("openai-compat"));

        // The configs went into the map under the id.
        QVERIFY(r.sttConfigs.contains(QStringLiteral("soniox")));
        QVERIFY(r.llmConfigs.contains(QStringLiteral("openai-compat")));

        // The fields are there (baseUrl/model), through the selected config.
        QCOMPARE(r.sttSelected().type,    QStringLiteral("soniox"));
        QCOMPARE(r.sttSelected().baseUrl, QStringLiteral("https://api.soniox.com/v1"));
        QCOMPARE(r.sttSelected().model,   QStringLiteral("stt-async-v5"));
        QCOMPARE(r.llmSelected().type,    QStringLiteral("openai-compat"));
        QCOMPARE(r.llmSelected().baseUrl, QStringLiteral("http://localhost:1234/v1"));
        QCOMPARE(r.llmSelected().model,   QStringLiteral("gemma-4-12b"));
    }

    // Old shape with an EMPTY type: fallback id ("soniox"/"openai-compat").
    void appSettings_migratesOldShapeEmptyType()
    {
        QJsonObject oldStt;
        oldStt[QStringLiteral("baseUrl")] = QStringLiteral("https://api.soniox.com/v1");
        oldStt[QStringLiteral("model")]   = QStringLiteral("stt-async-v5");

        QJsonObject oldLlm;
        oldLlm[QStringLiteral("baseUrl")] = QStringLiteral("http://localhost:1234/v1");
        oldLlm[QStringLiteral("model")]   = QStringLiteral("gemma-4-12b");

        QJsonObject o;
        o[QStringLiteral("stt")] = oldStt;
        o[QStringLiteral("llm")] = oldLlm;

        const AppSettings r = appSettingsFromJson(o);

        QCOMPARE(r.sttProviderId, QStringLiteral("soniox"));
        QCOMPARE(r.llmProviderId, QStringLiteral("openai-compat"));
        QVERIFY(r.sttConfigs.contains(QStringLiteral("soniox")));
        QVERIFY(r.llmConfigs.contains(QStringLiteral("openai-compat")));
        QCOMPARE(r.sttSelected().model, QStringLiteral("stt-async-v5"));
        QCOMPARE(r.llmSelected().model, QStringLiteral("gemma-4-12b"));
    }

    // Default languageHints == ["hu"] + the selected provider config's defaults.
    void defaults_languageHintsHu()
    {
        const AppSettings d = SettingsManager::defaults(QStringLiteral("/tmp/x"));
        QCOMPARE(d.languageHints, (QStringList{QStringLiteral("hu")}));
        QVERIFY(!d.userSpeakerName.trimmed().isEmpty());   // the OS account's name (no hard-coded name)
        QCOMPARE(d.sttProviderId, QStringLiteral("soniox"));
        QCOMPARE(d.llmProviderId, QStringLiteral("openai-compat"));
        QCOMPARE(d.sttSelected().type, QStringLiteral("soniox"));
        QCOMPARE(d.llmSelected().baseUrl, QStringLiteral("http://localhost:1234/v1"));
    }

    // SettingsManager: loads, writes defaults, reloads and matches.
    void settingsManager_loadSaveRoundTrip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString meta = tmp.filePath(QStringLiteral("meta"));

        SettingsManager mgr(meta);
        // The constructor writes the default if there is no file.
        QVERIFY(QFile::exists(mgr.settingsFilePath()));
        QCOMPARE(mgr.settings().languageHints, (QStringList{QStringLiteral("hu")}));

        AppSettings s = mgr.settings();
        s.userSpeakerName = QStringLiteral("Béla");
        mgr.setSettings(s);

        SettingsManager mgr2(meta);
        QCOMPARE(mgr2.settings().userSpeakerName, QStringLiteral("Béla"));
    }

    // PRODUCTION PATH: a settings.json in the old {stt:{...},llm:{...}} shape loaded from disk
    // (SettingsManager::load) migrates into the NEW maps, and the apiKey is NOT loaded from JSON.
    void settingsManager_migratesOldShapeFromDisk()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString meta = tmp.filePath(QStringLiteral("meta"));
        QVERIFY(QDir().mkpath(meta));

        // A hand-written OLD-shape settings.json (with the apiKey too, to see that loading NEVER
        // brings it back from the JSON).
        QJsonObject oldStt;
        oldStt[QStringLiteral("type")]    = QStringLiteral("soniox");
        oldStt[QStringLiteral("baseUrl")] = QStringLiteral("https://api.soniox.com/v1");
        oldStt[QStringLiteral("model")]   = QStringLiteral("stt-async-v5");
        oldStt[QStringLiteral("apiKey")]  = QStringLiteral("SECRET-from-json");

        QJsonObject oldLlm;
        oldLlm[QStringLiteral("type")]      = QStringLiteral("openai-compat");
        oldLlm[QStringLiteral("baseUrl")]   = QStringLiteral("http://localhost:1234/v1");
        oldLlm[QStringLiteral("model")]     = QStringLiteral("gemma-4-12b");
        oldLlm[QStringLiteral("maxTokens")] = 4096;
        oldLlm[QStringLiteral("apiKey")]    = QStringLiteral("LLM-SECRET-from-json");

        QJsonObject root;
        root[QStringLiteral("userSpeakerName")] = QStringLiteral("Ádám");
        root[QStringLiteral("stt")] = oldStt;
        root[QStringLiteral("llm")] = oldLlm;

        const QString path = QDir(meta).filePath(QStringLiteral("settings.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson());
        f.close();

        // Production load.
        SettingsManager mgr(meta);
        const AppSettings r = mgr.settings();

        // type becomes id; the config went into the map with the preserved baseUrl/model.
        QCOMPARE(r.sttProviderId, QStringLiteral("soniox"));
        QCOMPARE(r.llmProviderId, QStringLiteral("openai-compat"));
        QVERIFY(r.sttConfigs.contains(QStringLiteral("soniox")));
        QVERIFY(r.llmConfigs.contains(QStringLiteral("openai-compat")));
        QCOMPARE(r.sttSelected().baseUrl, QStringLiteral("https://api.soniox.com/v1"));
        QCOMPARE(r.sttSelected().model,   QStringLiteral("stt-async-v5"));
        QCOMPARE(r.llmSelected().baseUrl, QStringLiteral("http://localhost:1234/v1"));
        QCOMPARE(r.llmSelected().model,   QStringLiteral("gemma-4-12b"));
        QCOMPARE(r.llmSelected().maxTokens, 4096);

        // apiKey is NEVER loaded from JSON.
        QVERIFY(r.sttSelected().apiKey.isEmpty());
        QVERIFY(r.llmSelected().apiKey.isEmpty());
    }

    // SAFETY NET (production path): a new-shape JSON where sttProviderId points to an id that
    // does NOT exist in the map: load must fall back to a sensible (non-empty) selection, and not
    // give an empty sttSelected().
    void settingsManager_safetyNetStaleSelectedId()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString meta = tmp.filePath(QStringLiteral("meta"));
        QVERIFY(QDir().mkpath(meta));

        // New shape: sttProviders contains only "soniox", but the selected id is a non-existent "ghost-stt".
        QJsonObject sonioxCfg;
        sonioxCfg[QStringLiteral("type")]    = QStringLiteral("soniox");
        sonioxCfg[QStringLiteral("baseUrl")] = QStringLiteral("https://api.soniox.com/v1");
        sonioxCfg[QStringLiteral("model")]   = QStringLiteral("stt-async-v5");
        QJsonObject sttProviders;
        sttProviders[QStringLiteral("soniox")] = sonioxCfg;

        QJsonObject llmCfg;
        llmCfg[QStringLiteral("type")]    = QStringLiteral("openai-compat");
        llmCfg[QStringLiteral("baseUrl")] = QStringLiteral("http://localhost:1234/v1");
        llmCfg[QStringLiteral("model")]   = QStringLiteral("gemma-4-12b");
        QJsonObject llmProviders;
        llmProviders[QStringLiteral("openai-compat")] = llmCfg;

        QJsonObject root;
        root[QStringLiteral("sttProviderId")] = QStringLiteral("ghost-stt"); // stale!
        root[QStringLiteral("sttProviders")]  = sttProviders;
        root[QStringLiteral("llmProviderId")] = QStringLiteral("openai-compat");
        root[QStringLiteral("llmProviders")]  = llmProviders;

        const QString path = QDir(meta).filePath(QStringLiteral("settings.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson());
        f.close();

        SettingsManager mgr(meta);
        const AppSettings r = mgr.settings();

        // It fell back from the stale id to a sensible, NON-empty selection.
        QVERIFY(!r.sttProviderId.isEmpty());
        QVERIFY(r.sttConfigs.contains(r.sttProviderId));
        QVERIFY(!r.sttSelected().type.isEmpty());
        QVERIFY(!r.sttSelected().baseUrl.isEmpty());
        // The single existing config ("soniox") is the fallback used.
        QCOMPARE(r.sttProviderId, QStringLiteral("soniox"));
    }

    // KeyStore: set/get/remove file-based.
    void keyStore_setGetRemove()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("secrets.json"));

        KeyStore ks(path);
        QVERIFY(!ks.contains(QStringLiteral("soniox.apiKey")));
        ks.set(QStringLiteral("soniox.apiKey"), QStringLiteral("abc123"));
        QCOMPARE(ks.get(QStringLiteral("soniox.apiKey")), QStringLiteral("abc123"));

        // Another instance on the same file sees the value.
        KeyStore ks2(path);
        QCOMPARE(ks2.get(QStringLiteral("soniox.apiKey")), QStringLiteral("abc123"));

        ks2.remove(QStringLiteral("soniox.apiKey"));
        KeyStore ks3(path);
        QVERIFY(!ks3.contains(QStringLiteral("soniox.apiKey")));
    }
};

// ===========================================================================
class StoreTests : public QObject {
    Q_OBJECT
private slots:

    // createMeeting: folder + meeting.json round trip; loadAll/load match.
    void meetingStore_createAndRoundTrip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString audio = tmp.filePath(QStringLiteral("recordings"));
        const QString meta  = tmp.filePath(QStringLiteral("meta"));

        MeetingStore store(audio, meta);
        const Meeting m = store.createMeeting(QStringLiteral("Heti sync"));

        QVERIFY(!m.id.isEmpty());
        QVERIFY(!m.folder.isEmpty());
        QVERIFY(QDir(m.folder).exists());
        QVERIFY(QFile::exists(QDir(m.folder).filePath(QStringLiteral("meeting.json"))));

        // load() returns the same from disk.
        const Meeting loaded = store.load(m.id);
        QCOMPARE(loaded.id, m.id);
        QCOMPARE(loaded.title, QStringLiteral("Heti sync"));

        // loadAll() contains it.
        const QVector<Meeting> all = store.loadAll();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().id, m.id);

        // saveMeeting update: durationMs + flags.
        Meeting upd = loaded;
        upd.durationMs = 123456;
        upd.hasSummary = true;
        store.saveMeeting(upd);

        const Meeting reread = store.load(m.id);
        QCOMPARE(reread.durationMs, static_cast<qint64>(123456));
        QVERIFY(reread.hasSummary);
    }

    // rebuildIndexFromDisk: an emptied index is rebuilt from disk.
    void meetingStore_rebuildIndex()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString audio = tmp.filePath(QStringLiteral("recordings"));
        const QString meta  = tmp.filePath(QStringLiteral("meta"));

        MeetingStore store(audio, meta);
        store.createMeeting(QStringLiteral("A"));
        store.createMeeting(QStringLiteral("B"));
        QCOMPARE(store.loadAll().size(), 2);

        store.rebuildIndexFromDisk();
        QCOMPARE(store.loadAll().size(), 2);
    }
};

// RUN / INTEGRATION.
//
// The tests/unit/*.cpp files are GLOBbed into a SINGLE exe, and test_smoke.cpp already owns
// QTEST_MAIN. Two mains / two QTEST_MAINs would clash (a link error), which would break the
// parallel build for EVERYONE, so there is NEITHER main NOR QTEST_MAIN here. The two test
// classes above (SettingsTests, StoreTests) thus build cleanly into the shared exe, without running.
//
// To make it runnable from the command line on its own (WITHOUT violating the shared exe),
// compile this file separately with the TANARA_STORE_SETTINGS_STANDALONE macro:
//
//   moc test_store_settings.cpp -o test_store_settings.moc -I core/include
//   g++ -std=c++20 -DTANARA_STORE_SETTINGS_STANDALONE \
//       test_store_settings.cpp <tanara_core+Qt links> -o store_tests && ./store_tests
//
// Then it gets its own main; in the shared GLOB build the macro is NOT defined, so there is
// NO main and no clash. On integration the two classes can be moved into a multi-test harness unchanged.
#ifdef TANARA_STORE_SETTINGS_STANDALONE
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int rc = 0;
    {
        SettingsTests t;
        rc |= QTest::qExec(&t, argc, argv);
    }
    {
        StoreTests t;
        rc |= QTest::qExec(&t, argc, argv);
    }
    return rc;
}
#endif

#include "test_store_settings.moc"

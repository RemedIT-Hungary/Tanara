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
#include "tanara/providers/core/ProviderConfig.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>

#include "tanara/domain/Types.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/store/detail/MeetingJson.h"
#include "tanara/library/store/ParticipantJson.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/foundation/secrets/SecretStore.h"
#include "tanara/app/LegacySettings.h"

using namespace tanara;

// ===========================================================================
class SettingsTests : public QObject {
    Q_OBJECT
private slots:

    // Tanara Cloud settings round trip + old settings.json gives defaults.
    void appSettings_cloudFieldsRoundTrip()
    {
        tanara::app::AppSettings s;
        s.cloud.enabled  = true;
        s.cloud.baseUrl  = QStringLiteral("http://127.0.0.1:8300");
        s.cloud.sttTier  = QStringLiteral("fast");
        s.cloud.llmTier  = QStringLiteral("accurate");
        s.cloud.sttModel = QStringLiteral("soniox/stt-async-v5");
        s.cloud.waitlistEmail = QStringLiteral("anna@example.com");
        const tanara::app::AppSettings r = tanara::app::AppSettings::fromJson(s.toJson());
        QCOMPARE(r.cloud.enabled, true);
        QCOMPARE(r.cloud.baseUrl, s.cloud.baseUrl);
        QCOMPARE(r.cloud.sttTier, QStringLiteral("fast"));
        QCOMPARE(r.cloud.sttModel, s.cloud.sttModel);
        QVERIFY(r.cloud.llmModel.isEmpty());
        QCOMPARE(r.cloud.waitlistEmail, s.cloud.waitlistEmail);

        const tanara::app::AppSettings old = tanara::app::AppSettings::fromJson(QJsonObject{ { QStringLiteral("paths"), QJsonObject{} } });
        QCOMPARE(old.cloud.enabled, false);                       // before launch: teaser
        QCOMPARE(old.cloud.sttTier, QStringLiteral("accurate"));
        QVERIFY(old.cloud.waitlistEmail.isEmpty());
    }

    // The "Getting started" flag goes both ways; in an old settings.json it is missing, so false.
    void appSettings_onboardingDoneRoundTrip()
    {
        tanara::app::AppSettings s;
        QCOMPARE(s.ui.onboardingDone, false);
        s.ui.onboardingDone = true;
        QCOMPARE(tanara::app::AppSettings::fromJson(s.toJson()).ui.onboardingDone, true);
        const tanara::app::AppSettings old = tanara::app::AppSettings::fromJson(QJsonObject{ { QStringLiteral("paths"), QJsonObject{} } });
        QCOMPARE(old.ui.onboardingDone, false);
    }

    // tanara::app::AppSettings JSON round trip (NEW multi-provider shape).
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

        const QJsonObject o = library::toJson(m);
        const QJsonArray tracks = o.value(QStringLiteral("tracks")).toArray();
        QCOMPARE(tracks.at(0).toObject().value(QStringLiteral("startOffsetMs")).toDouble(), 57200.0);
        QVERIFY(!tracks.at(1).toObject().contains(QStringLiteral("startOffsetMs")));
        const Meeting back = library::meetingFromJson(o);
        QCOMPARE(back.tracks.at(0).startOffsetMs, qint64(57200));
        QCOMPARE(back.tracks.at(1).startOffsetMs, qint64(0));
    }

    void appSettings_jsonRoundTrip()
    {
        tanara::app::AppSettings s;
        s.paths.setPrimaryRecordingRoot(QStringLiteral("/tmp/rec"));
        s.paths.notesDir  = QStringLiteral("/tmp/notes");
        s.paths.dataDir   = QStringLiteral("/tmp/meta");
        s.recording.userSpeakerName = QStringLiteral("Ádám");
        s.processing.languageHints   = QStringList{QStringLiteral("hu"), QStringLiteral("en")};

        s.providers.stt.selectedId = QStringLiteral("soniox");
        providers::ProviderConfig stt;
        stt.type    = QStringLiteral("soniox");
        stt.baseUrl = QStringLiteral("https://api.soniox.com/v1");
        stt.model   = QStringLiteral("stt-async-v5");
        stt.apiKey  = QStringLiteral("SECRET-should-not-persist");
        s.providers.stt.configs.insert(QStringLiteral("soniox"), stt);

        s.providers.llm.selectedId = QStringLiteral("openai-compat");
        providers::ProviderConfig llm;
        llm.type    = QStringLiteral("openai-compat");
        llm.baseUrl = QStringLiteral("http://localhost:1234/v1");
        llm.model   = QStringLiteral("gemma-4-12b");
        llm.apiKey  = QStringLiteral("LLM-SECRET-should-not-persist");
        s.providers.llm.configs.insert(QStringLiteral("openai-compat"), llm);

        const QJsonObject o = s.toJson();
        const tanara::app::AppSettings r = tanara::app::AppSettings::fromJson(o);

        QVERIFY(r.paths == s.paths);
        QCOMPARE(r.recording.userSpeakerName, s.recording.userSpeakerName);
        QCOMPARE(r.processing.languageHints, s.processing.languageHints);

        // The selected provider ids come through.
        QCOMPARE(r.providers.stt.selectedId, s.providers.stt.selectedId);
        QCOMPARE(r.providers.llm.selectedId, s.providers.llm.selectedId);

        // The selected configs' fields come through (from the map / by accessor).
        QCOMPARE(r.providers.stt.selected().type,    s.providers.stt.selected().type);
        QCOMPARE(r.providers.stt.selected().baseUrl, s.providers.stt.selected().baseUrl);
        QCOMPARE(r.providers.stt.selected().model,   s.providers.stt.selected().model);
        QCOMPARE(r.providers.llm.selected().model,   s.providers.llm.selected().model);
        QVERIFY(r.providers.stt.configs.contains(QStringLiteral("soniox")));
        QVERIFY(r.providers.llm.configs.contains(QStringLiteral("openai-compat")));

        // apiKey is NEVER persisted into the JSON, not into the providers block either.
        const QJsonObject provs = o.value(QStringLiteral("providers")).toObject();
        const QJsonObject sttProv = provs.value(QStringLiteral("stt")).toObject().value(QStringLiteral("configs"))
                                        .toObject().value(QStringLiteral("soniox")).toObject();
        QCOMPARE(sttProv.value(QStringLiteral("model")).toString(), QStringLiteral("stt-async-v5"));
        QVERIFY(!sttProv.contains(QStringLiteral("apiKey")));
        const QJsonObject llmProv = provs.value(QStringLiteral("llm")).toObject().value(QStringLiteral("configs"))
                                        .toObject().value(QStringLiteral("openai-compat")).toObject();
        QVERIFY(!llmProv.contains(QStringLiteral("apiKey")));

        QVERIFY(r.providers.stt.selected().apiKey.isEmpty());
        QVERIFY(r.providers.llm.selected().apiKey.isEmpty());
    }

    // JSON round trip of the meeting watcher fields (with non-default values).
    void appSettings_detectorRoundTrip()
    {
        tanara::app::AppSettings s;
        s.watcher.detectorEnabled     = false;
        s.watcher.detectorIntervalSec = 15;
        s.watcher.autostart    = true;
        s.watcher.detectorId          = QStringLiteral("linux-capture");
        s.watcher.knownCallApps       = QStringList{QStringLiteral("zoom"), QStringLiteral("jitsi")};

        const tanara::app::AppSettings r = tanara::app::AppSettings::fromJson(s.toJson());

        QCOMPARE(r.watcher.detectorEnabled, false);
        QCOMPARE(r.watcher.detectorIntervalSec, 15);
        QCOMPARE(r.watcher.autostart, true);
        QCOMPARE(r.watcher.detectorId, QStringLiteral("linux-capture"));
        QCOMPARE(r.watcher.knownCallApps, (QStringList{QStringLiteral("zoom"), QStringLiteral("jitsi")}));
    }

    // Back-compat: an (old) JSON without the watcher fields loads with the defaults.
    void appSettings_detectorDefaultsWhenAbsent()
    {
        const tanara::app::AppSettings d;                          // built-in defaults
        const tanara::app::AppSettings r = tanara::app::AppSettings::fromJson(QJsonObject{});   // empty JSON
        QCOMPARE(r.watcher.detectorEnabled, d.watcher.detectorEnabled);
        QCOMPARE(r.watcher.detectorIntervalSec, d.watcher.detectorIntervalSec);
        QCOMPARE(r.watcher.autostart, d.watcher.autostart);
        QVERIFY(!r.watcher.knownCallApps.isEmpty());          // the default list stays
    }

    // The old {stt:{...},llm:{...}} shape migrates into the NEW maps (tanara-cli settings import-legacy).
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

        const tanara::app::AppSettings r = app::legacy::importFlat(o, tanara::app::AppSettings{});

        // The type became the id (an empty type falls back to "soniox"/"openai-compat").
        QCOMPARE(r.providers.stt.selectedId, QStringLiteral("soniox"));
        QCOMPARE(r.providers.llm.selectedId, QStringLiteral("openai-compat"));

        // The configs went into the map under the id.
        QVERIFY(r.providers.stt.configs.contains(QStringLiteral("soniox")));
        QVERIFY(r.providers.llm.configs.contains(QStringLiteral("openai-compat")));

        // The fields are there (baseUrl/model), through the selected config.
        QCOMPARE(r.providers.stt.selected().type,    QStringLiteral("soniox"));
        QCOMPARE(r.providers.stt.selected().baseUrl, QStringLiteral("https://api.soniox.com/v1"));
        QCOMPARE(r.providers.stt.selected().model,   QStringLiteral("stt-async-v5"));
        QCOMPARE(r.providers.llm.selected().type,    QStringLiteral("openai-compat"));
        QCOMPARE(r.providers.llm.selected().baseUrl, QStringLiteral("http://localhost:1234/v1"));
        QCOMPARE(r.providers.llm.selected().model,   QStringLiteral("gemma-4-12b"));
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

        const tanara::app::AppSettings r = app::legacy::importFlat(o, tanara::app::AppSettings{});

        QCOMPARE(r.providers.stt.selectedId, QStringLiteral("soniox"));
        QCOMPARE(r.providers.llm.selectedId, QStringLiteral("openai-compat"));
        QVERIFY(r.providers.stt.configs.contains(QStringLiteral("soniox")));
        QVERIFY(r.providers.llm.configs.contains(QStringLiteral("openai-compat")));
        QCOMPARE(r.providers.stt.selected().model, QStringLiteral("stt-async-v5"));
        QCOMPARE(r.providers.llm.selected().model, QStringLiteral("gemma-4-12b"));
    }

    // Default languageHints == ["hu"] + the selected provider config's defaults.
    void defaults_languageHintsHu()
    {
        const tanara::app::AppSettings d = app::SettingsManager::defaults();
        QCOMPARE(d.processing.languageHints, (QStringList{QStringLiteral("hu")}));
        QVERIFY(!d.recording.userSpeakerName.trimmed().isEmpty());   // the OS account's name (no hard-coded name)
        QCOMPARE(d.providers.stt.selectedId, QStringLiteral("soniox"));
        QCOMPARE(d.providers.llm.selectedId, QStringLiteral("openai-compat"));
        QCOMPARE(d.providers.stt.selected().type, QStringLiteral("soniox"));
        QCOMPARE(d.providers.llm.selected().baseUrl, QStringLiteral("http://localhost:1234/v1"));
    }

    // SettingsManager: loads, writes defaults, reloads and matches.
    void settingsManager_loadSaveRoundTrip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString meta = tmp.filePath(QStringLiteral("meta"));

        app::SettingsManager mgr(meta);
        // The constructor writes the default if there is no file.
        QVERIFY(QFile::exists(mgr.settingsFilePath()));
        QCOMPARE(mgr.settings().processing.languageHints, (QStringList{QStringLiteral("hu")}));

        tanara::app::AppSettings s = mgr.settings();
        s.recording.userSpeakerName = QStringLiteral("Béla");
        mgr.setSettings(s);

        app::SettingsManager mgr2(meta);
        QCOMPARE(mgr2.settings().recording.userSpeakerName, QStringLiteral("Béla"));
    }

    // An old-shape ({stt:{...},llm:{...}}) settings.json on disk: the app does NOT migrate it (D8:
    // defaults); the one-time legacy import takes it over, and the apiKey is NOT loaded from JSON.
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

        // The app: defaults (old format), the file untouched.
        app::SettingsManager mgr(meta);
        QVERIFY(mgr.legacyFormat());
        QCOMPARE(mgr.settings().recording.userSpeakerName, app::SettingsManager::defaults().recording.userSpeakerName);
        // The legacy import.
        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const tanara::app::AppSettings r = app::legacy::importFlat(QJsonDocument::fromJson(in.readAll()).object(),
                                                                  app::SettingsManager::defaults());

        // type becomes id; the config went into the map with the preserved baseUrl/model.
        QCOMPARE(r.providers.stt.selectedId, QStringLiteral("soniox"));
        QCOMPARE(r.providers.llm.selectedId, QStringLiteral("openai-compat"));
        QVERIFY(r.providers.stt.configs.contains(QStringLiteral("soniox")));
        QVERIFY(r.providers.llm.configs.contains(QStringLiteral("openai-compat")));
        QCOMPARE(r.providers.stt.selected().baseUrl, QStringLiteral("https://api.soniox.com/v1"));
        QCOMPARE(r.providers.stt.selected().model,   QStringLiteral("stt-async-v5"));
        QCOMPARE(r.providers.llm.selected().baseUrl, QStringLiteral("http://localhost:1234/v1"));
        QCOMPARE(r.providers.llm.selected().model,   QStringLiteral("gemma-4-12b"));
        QCOMPARE(r.providers.llm.selected().maxTokens, 4096);

        // apiKey is NEVER loaded from JSON.
        QVERIFY(r.providers.stt.selected().apiKey.isEmpty());
        QVERIFY(r.providers.llm.selected().apiKey.isEmpty());
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

        QJsonObject providersSec;
        providersSec[QStringLiteral("stt")] = QJsonObject{{QStringLiteral("selected"), QStringLiteral("ghost-stt")}, // stale!
                                                         {QStringLiteral("configs"), sttProviders}};
        providersSec[QStringLiteral("llm")] = QJsonObject{{QStringLiteral("selected"), QStringLiteral("openai-compat")},
                                                         {QStringLiteral("configs"), llmProviders}};
        QJsonObject root;
        root[QStringLiteral("version")] = 2;
        root[QStringLiteral("providers")] = providersSec;

        const QString path = QDir(meta).filePath(QStringLiteral("settings.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson());
        f.close();

        app::SettingsManager mgr(meta);
        const tanara::app::AppSettings r = mgr.settings();

        // It fell back from the stale id to a sensible, NON-empty selection.
        QVERIFY(!r.providers.stt.selectedId.isEmpty());
        QVERIFY(r.providers.stt.configs.contains(r.providers.stt.selectedId));
        QVERIFY(!r.providers.stt.selected().type.isEmpty());
        QVERIFY(!r.providers.stt.selected().baseUrl.isEmpty());
        // The single existing config ("soniox") is the fallback used.
        QCOMPARE(r.providers.stt.selectedId, QStringLiteral("soniox"));
    }

    // FileSecretStore: set/get/remove file-based.
    void keyStore_setGetRemove()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("secrets.json"));

        FileSecretStore ks(path);
        QVERIFY(!ks.contains(QStringLiteral("soniox.apiKey")));
        ks.set(QStringLiteral("soniox.apiKey"), QStringLiteral("abc123"));
        QCOMPARE(ks.get(QStringLiteral("soniox.apiKey")), QStringLiteral("abc123"));

        // Another instance on the same file sees the value.
        FileSecretStore ks2(path);
        QCOMPARE(ks2.get(QStringLiteral("soniox.apiKey")), QStringLiteral("abc123"));

        ks2.remove(QStringLiteral("soniox.apiKey"));
        FileSecretStore ks3(path);
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

        library::MeetingRepository store(audio, meta);
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

        library::MeetingRepository store(audio, meta);
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

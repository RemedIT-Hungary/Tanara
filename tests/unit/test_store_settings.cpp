//
// Tanara — Settings + Meeting store unit-tesztek.
//
// FONTOS: a tests/unit/*.cpp fájlok EGYETLEN exe-be GLOB-olódnak, és a
// test_smoke.cpp már birtokolja a QTEST_MAIN-t. Ezért itt NEM lehet újabb
// QTEST_MAIN/QTEST_GUILESS_MAIN — az main-szimbólum-ütközést okozna.
//
// Megoldás MVP-re: a teszt-osztályokat egy static-init időben futó
// regisztrátorral hajtjuk meg (QTest::qExec), így nincs main, nincs ütközés.
// Ha integrációkor multi-test harnessre váltunk, ez a fájl könnyen átkapcsolható.
//
#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
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

    // AppSettings JSON round-trip (ÚJ multi-provider shape).
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

        // A kiválasztott provider id-k átjönnek.
        QCOMPARE(r.sttProviderId, s.sttProviderId);
        QCOMPARE(r.llmProviderId, s.llmProviderId);

        // A kiválasztott config-ok mezői átjönnek (a map-ből / accessorral).
        QCOMPARE(r.sttSelected().type,    s.sttSelected().type);
        QCOMPARE(r.sttSelected().baseUrl, s.sttSelected().baseUrl);
        QCOMPARE(r.sttSelected().model,   s.sttSelected().model);
        QCOMPARE(r.llmSelected().model,   s.llmSelected().model);
        QVERIFY(r.sttConfigs.contains(QStringLiteral("soniox")));
        QVERIFY(r.llmConfigs.contains(QStringLiteral("openai-compat")));

        // apiKey SOHA nem perzisztálódik a JSON-be — sem a providers blokkba.
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

    // Régi {stt:{...},llm:{...}} shape migrálódik az ÚJ map-ekbe.
    void appSettings_migratesOldShape()
    {
        // Kézzel épített RÉGI shape-ű JSON (nincs sttProviders/llmProviders).
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

        // A típus lett az id (üres type → "soniox"/"openai-compat" fallback).
        QCOMPARE(r.sttProviderId, QStringLiteral("soniox"));
        QCOMPARE(r.llmProviderId, QStringLiteral("openai-compat"));

        // A config-ok a map-be kerültek az id alatt.
        QVERIFY(r.sttConfigs.contains(QStringLiteral("soniox")));
        QVERIFY(r.llmConfigs.contains(QStringLiteral("openai-compat")));

        // A mezők megvannak (baseUrl/model) — a kiválasztott config-on át.
        QCOMPARE(r.sttSelected().type,    QStringLiteral("soniox"));
        QCOMPARE(r.sttSelected().baseUrl, QStringLiteral("https://api.soniox.com/v1"));
        QCOMPARE(r.sttSelected().model,   QStringLiteral("stt-async-v5"));
        QCOMPARE(r.llmSelected().type,    QStringLiteral("openai-compat"));
        QCOMPARE(r.llmSelected().baseUrl, QStringLiteral("http://localhost:1234/v1"));
        QCOMPARE(r.llmSelected().model,   QStringLiteral("gemma-4-12b"));
    }

    // Régi shape ÜRES type-pal → fallback id ("soniox"/"openai-compat").
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

    // Default languageHints == ["hu"] + a kiválasztott provider-config defaultjai.
    void defaults_languageHintsHu()
    {
        const AppSettings d = SettingsManager::defaults(QStringLiteral("/tmp/x"));
        QCOMPARE(d.languageHints, (QStringList{QStringLiteral("hu")}));
        QCOMPARE(d.userSpeakerName, QStringLiteral("Ádám"));
        QCOMPARE(d.sttProviderId, QStringLiteral("soniox"));
        QCOMPARE(d.llmProviderId, QStringLiteral("openai-compat"));
        QCOMPARE(d.sttSelected().type, QStringLiteral("soniox"));
        QCOMPARE(d.llmSelected().baseUrl, QStringLiteral("http://localhost:1234/v1"));
    }

    // SettingsManager: betölt → defaultokat ír → újratölt és egyezik.
    void settingsManager_loadSaveRoundTrip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString meta = tmp.filePath(QStringLiteral("meta"));

        SettingsManager mgr(meta);
        // A konstruktor defaultot ír, ha nincs fájl.
        QVERIFY(QFile::exists(mgr.settingsFilePath()));
        QCOMPARE(mgr.settings().languageHints, (QStringList{QStringLiteral("hu")}));

        AppSettings s = mgr.settings();
        s.userSpeakerName = QStringLiteral("Béla");
        mgr.setSettings(s);

        SettingsManager mgr2(meta);
        QCOMPARE(mgr2.settings().userSpeakerName, QStringLiteral("Béla"));
    }

    // PRODUKCIÓS ÚT: régi {stt:{...},llm:{...}} shape settings.json lemezről
    // betöltve (SettingsManager::load) az ÚJ map-ekbe migrálódik, az apiKey
    // pedig NEM töltődik be JSON-ből.
    void settingsManager_migratesOldShapeFromDisk()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString meta = tmp.filePath(QStringLiteral("meta"));
        QVERIFY(QDir().mkpath(meta));

        // Kézzel írt RÉGI shape-ű settings.json (apiKey-vel együtt, hogy lássuk:
        // a betöltés SOHA nem hozza vissza a JSON-ből).
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

        // Produkciós betöltés.
        SettingsManager mgr(meta);
        const AppSettings r = mgr.settings();

        // type → id; a config a map-be került a megőrzött baseUrl/model-lel.
        QCOMPARE(r.sttProviderId, QStringLiteral("soniox"));
        QCOMPARE(r.llmProviderId, QStringLiteral("openai-compat"));
        QVERIFY(r.sttConfigs.contains(QStringLiteral("soniox")));
        QVERIFY(r.llmConfigs.contains(QStringLiteral("openai-compat")));
        QCOMPARE(r.sttSelected().baseUrl, QStringLiteral("https://api.soniox.com/v1"));
        QCOMPARE(r.sttSelected().model,   QStringLiteral("stt-async-v5"));
        QCOMPARE(r.llmSelected().baseUrl, QStringLiteral("http://localhost:1234/v1"));
        QCOMPARE(r.llmSelected().model,   QStringLiteral("gemma-4-12b"));
        QCOMPARE(r.llmSelected().maxTokens, 4096);

        // apiKey SOHA nem töltődik be JSON-ből.
        QVERIFY(r.sttSelected().apiKey.isEmpty());
        QVERIFY(r.llmSelected().apiKey.isEmpty());
    }

    // SAFETY NET (produkciós út): új-shape JSON, ahol a sttProviderId egy a
    // map-ben NEM létező id-re mutat → a load essen vissza értelmes
    // (nem-üres) kiválasztásra, ne adjon üres sttSelected()-et.
    void settingsManager_safetyNetStaleSelectedId()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString meta = tmp.filePath(QStringLiteral("meta"));
        QVERIFY(QDir().mkpath(meta));

        // Új shape: a sttProviders csak "soniox"-ot tartalmaz, de a kiválasztott
        // id egy nem létező "ghost-stt".
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

        // A stale id-ről értelmes, NEM üres kiválasztásra esett vissza.
        QVERIFY(!r.sttProviderId.isEmpty());
        QVERIFY(r.sttConfigs.contains(r.sttProviderId));
        QVERIFY(!r.sttSelected().type.isEmpty());
        QVERIFY(!r.sttSelected().baseUrl.isEmpty());
        // A meglévő egyetlen config ("soniox") a használt fallback.
        QCOMPARE(r.sttProviderId, QStringLiteral("soniox"));
    }

    // KeyStore: set/get/remove fájl-alapon.
    void keyStore_setGetRemove()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("secrets.json"));

        KeyStore ks(path);
        QVERIFY(!ks.contains(QStringLiteral("soniox.apiKey")));
        ks.set(QStringLiteral("soniox.apiKey"), QStringLiteral("abc123"));
        QCOMPARE(ks.get(QStringLiteral("soniox.apiKey")), QStringLiteral("abc123"));

        // Másik példány ugyanazon fájlon lássa az értéket.
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

    // createMeeting → mappa + meeting.json round-trip; loadAll/load egyezik.
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

        // load() a lemezről ugyanazt adja vissza.
        const Meeting loaded = store.load(m.id);
        QCOMPARE(loaded.id, m.id);
        QCOMPARE(loaded.title, QStringLiteral("Heti sync"));

        // loadAll() tartalmazza.
        const QVector<Meeting> all = store.loadAll();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().id, m.id);

        // saveMeeting frissítés: durationMs + flag-ek.
        Meeting upd = loaded;
        upd.durationMs = 123456;
        upd.hasSummary = true;
        store.saveMeeting(upd);

        const Meeting reread = store.load(m.id);
        QCOMPARE(reread.durationMs, static_cast<qint64>(123456));
        QVERIFY(reread.hasSummary);
    }

    // rebuildIndexFromDisk: ürített index újraépül a lemezről.
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

// ===========================================================================
// FUTTATÁS / INTEGRÁCIÓ.
//
// A tests/unit/*.cpp fájlok EGYETLEN exe-be GLOB-olódnak, és a test_smoke.cpp
// már birtokolja a QTEST_MAIN-t. Két main / két QTEST_MAIN ütközne (link-hiba),
// ami a párhuzamos build-et MINDENKINEK eltörné — ezért itt SEM main, SEM
// QTEST_MAIN nincs. A fenti két teszt-osztály (SettingsTests, StoreTests)
// így tisztán befordul a közös exe-be, futtatás nélkül.
//
// Hogy parancssorból is futtatható legyen önállóan (a közös exe SÉRTÉSE nélkül),
// ezt a fájlt fordítsd külön a TANARA_STORE_SETTINGS_STANDALONE makróval:
//
//   moc test_store_settings.cpp -o test_store_settings.moc -I core/include
//   g++ -std=c++20 -DTANARA_STORE_SETTINGS_STANDALONE \
//       test_store_settings.cpp <tanara_core+Qt linkek> -o store_tests && ./store_tests
//
// Ekkor saját main jön létre; a közös GLOB-os build során a makró NINCS
// definiálva, tehát NINCS main → nincs ütközés. Integrációkor a két osztály
// változtatás nélkül átemelhető egy multi-test harnessbe.
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

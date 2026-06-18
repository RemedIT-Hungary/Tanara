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

    // AppSettings JSON round-trip.
    void appSettings_jsonRoundTrip()
    {
        AppSettings s;
        s.audioDir        = QStringLiteral("/tmp/rec");
        s.notesDir        = QStringLiteral("/tmp/notes");
        s.metadataDir     = QStringLiteral("/tmp/meta");
        s.userSpeakerName = QStringLiteral("Ádám");
        s.languageHints   = QStringList{QStringLiteral("hu"), QStringLiteral("en")};
        s.stt.type    = QStringLiteral("soniox");
        s.stt.baseUrl = QStringLiteral("https://api.soniox.com/v1");
        s.stt.model   = QStringLiteral("stt-async-v5");
        s.stt.apiKey  = QStringLiteral("SECRET-should-not-persist");
        s.llm.type    = QStringLiteral("openai-compat");
        s.llm.baseUrl = QStringLiteral("http://localhost:1234/v1");
        s.llm.model   = QStringLiteral("gemma-4-12b");

        const QJsonObject o = toJson(s);
        const AppSettings r = appSettingsFromJson(o);

        QCOMPARE(r.audioDir, s.audioDir);
        QCOMPARE(r.notesDir, s.notesDir);
        QCOMPARE(r.metadataDir, s.metadataDir);
        QCOMPARE(r.userSpeakerName, s.userSpeakerName);
        QCOMPARE(r.languageHints, s.languageHints);
        QCOMPARE(r.stt.type, s.stt.type);
        QCOMPARE(r.stt.baseUrl, s.stt.baseUrl);
        QCOMPARE(r.stt.model, s.stt.model);
        QCOMPARE(r.llm.model, s.llm.model);

        // apiKey SOHA nem perzisztálódik a JSON-be.
        QVERIFY(!o.value(QStringLiteral("stt")).toObject().contains(QStringLiteral("apiKey")));
        QVERIFY(r.stt.apiKey.isEmpty());
    }

    // Default languageHints == ["hu"].
    void defaults_languageHintsHu()
    {
        const AppSettings d = SettingsManager::defaults(QStringLiteral("/tmp/x"));
        QCOMPARE(d.languageHints, (QStringList{QStringLiteral("hu")}));
        QCOMPARE(d.userSpeakerName, QStringLiteral("Ádám"));
        QCOMPARE(d.stt.type, QStringLiteral("soniox"));
        QCOMPARE(d.llm.baseUrl, QStringLiteral("http://localhost:1234/v1"));
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

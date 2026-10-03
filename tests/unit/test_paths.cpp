//
// TANARA_HOME — a metaadat-mappa felülírása (tanara/Paths.h).
//
// A lényeg: beállított TANARA_HOME mellett SEMMI nem mutathat a valódi ~/.tanara-ba — sem az
// alapértelmezett fájl-utak (kulcsok, személyek, lenyomatok, promptok), sem a beállításokból
// (settings.json metadataDir) visszaszivárgó érték. A teszt a HOME-ot is egy temp mappára
// állítja, így a „valódi” ~/.tanara itt is csak egy eldobható hely — és ellenőrizzük, hogy
// az érintetlen marad.
//
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/Paths.h"
#include "tanara/SettingsManager.h"
#include "tanara/PromptLibrary.h"
#include "tanara/AppController.h"
#include "tanara/store/KeyStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/store/MeetingStore.h"

using namespace tanara;

class PathsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void defaultIsHomeDotTanara();
    void overrideWinsEverywhere();
    void overrideExpandsTildeAndRelative();
    void storesDefaultIntoOverride();
    void settingsFileCannotRedirectOut();
    void appControllerStaysInside();

private:
    QTemporaryDir m_fakeHome;    // a „valódi” HOME szerepében
    QTemporaryDir m_sandbox;     // TANARA_HOME
    QString realMeta() const { return QDir(m_fakeHome.path()).filePath(QStringLiteral(".tanara")); }
    // Minden fájl a mappa alatt (rekurzívan) — „érintetlen maradt-e” ellenőrzéshez.
    static QStringList listAll(const QString& dir) {
        QStringList out;
        QDirIterator it(dir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) out << it.next();
        return out;
    }
};

void PathsTest::initTestCase()
{
    QVERIFY(m_fakeHome.isValid());
    QVERIFY(m_sandbox.isValid());
    qputenv("HOME", m_fakeHome.path().toUtf8());
    QCOMPARE(QDir::homePath(), m_fakeHome.path());
}

void PathsTest::init()
{
    qunsetenv("TANARA_HOME");
}

void PathsTest::defaultIsHomeDotTanara()
{
    QVERIFY(paths::homeOverride().isEmpty());
    QCOMPARE(paths::defaultMetadataDir(), realMeta());
    QCOMPARE(paths::resolveMetadataDir(QString()), realMeta());
    QCOMPARE(paths::resolveMetadataDir(QStringLiteral("~/x")), QDir(m_fakeHome.path()).filePath("x"));
    QCOMPARE(paths::resolveMetadataDir(QStringLiteral("/abs/meta")), QStringLiteral("/abs/meta"));
    QCOMPARE(paths::metadataFile(QStringLiteral("people.json")), realMeta() + QStringLiteral("/people.json"));
    QCOMPARE(paths::defaultAudioDir(), QDir(m_fakeHome.path()).filePath("Tanara/recordings"));
}

void PathsTest::overrideWinsEverywhere()
{
    const QString sb = m_sandbox.path();
    qputenv("TANARA_HOME", sb.toUtf8());
    QCOMPARE(paths::homeOverride(), sb);
    QCOMPARE(paths::defaultMetadataDir(), sb);
    // A beállításból jövő érték sem térítheti el — akkor sem, ha a valódi mappára mutat.
    QCOMPARE(paths::resolveMetadataDir(realMeta()), sb);
    QCOMPARE(paths::resolveMetadataDir(QStringLiteral("~/.tanara")), sb);
    QCOMPARE(paths::metadataFile(QStringLiteral("logs")), sb + QStringLiteral("/logs"));
    // A felvételek / jegyzetek ALAPÉRTELMEZÉSE is a sandboxba kerül (nem ~/Tanara).
    QCOMPARE(paths::defaultAudioDir(), sb + QStringLiteral("/recordings"));
    QCOMPARE(paths::defaultNotesDir(), sb + QStringLiteral("/notes"));
    // A prompt-override fájlok is.
    QVERIFY(promptFilePath(QStringLiteral("simple")).startsWith(sb + QLatin1Char('/')));
    QVERIFY(promptFilePath(QStringLiteral("simple"), realMeta()).startsWith(sb + QLatin1Char('/')));
}

void PathsTest::overrideExpandsTildeAndRelative()
{
    qputenv("TANARA_HOME", "~/sandbox-home");
    QCOMPARE(paths::homeOverride(), QDir(m_fakeHome.path()).filePath("sandbox-home"));
    qputenv("TANARA_HOME", "rel/dir");
    QCOMPARE(paths::homeOverride(), QDir::cleanPath(QDir::current().absoluteFilePath("rel/dir")));
    qputenv("TANARA_HOME", "   ");
    QVERIFY(paths::homeOverride().isEmpty());
}

void PathsTest::storesDefaultIntoOverride()
{
    const QString sb = m_sandbox.path();
    qputenv("TANARA_HOME", sb.toUtf8());

    KeyStore keys;
    QCOMPARE(keys.filePath(), sb + QStringLiteral("/secrets.json"));
    keys.set(QStringLiteral("k"), QStringLiteral("v"));
    QVERIFY(QFile::exists(sb + QStringLiteral("/secrets.json")));

    PeopleStore people;
    people.add(QStringLiteral("Teszt Elek"));
    QVERIFY(QFile::exists(sb + QStringLiteral("/people.json")));

    VoiceprintStore prints;
    Voiceprint vp;
    vp.embedding = {1.0f, 0.0f};
    prints.addPrint(QStringLiteral("Teszt Elek"), vp);
    QVERIFY(QFile::exists(sb + QStringLiteral("/voiceprints.json")));

    SettingsManager sm;   // üres → alapértelmezett metaadat-mappa
    QCOMPARE(sm.settingsFilePath(), sb + QStringLiteral("/settings.json"));
    QCOMPARE(sm.settings().metadataDir, sb);
    QCOMPARE(sm.settings().audioDir, sb + QStringLiteral("/recordings"));

    MeetingStore store(&sm);
    QCOMPARE(store.metadataDir(), sb);
    QVERIFY(QFile::exists(sb + QStringLiteral("/index.db")));

    // A „valódi” ~/.tanara és ~/Tanara nem jött létre.
    QVERIFY2(!QDir(realMeta()).exists(), "a ~/.tanara létrejött TANARA_HOME mellett");
    QVERIFY(!QDir(QDir(m_fakeHome.path()).filePath("Tanara")).exists());
}

void PathsTest::settingsFileCannotRedirectOut()
{
    // Átmásolt (valódi) settings.json: a metadataDir a valódi ~/.tanara-ra mutat.
    QTemporaryDir sb;
    QVERIFY(sb.isValid());
    qputenv("TANARA_HOME", sb.path().toUtf8());
    QJsonObject o;
    o["metadataDir"] = realMeta();
    o["audioDir"] = sb.filePath("rec");
    o["notesDir"] = sb.filePath("notes");
    QFile f(sb.filePath("settings.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(o).toJson());
    f.close();

    SettingsManager sm;
    QCOMPARE(sm.settings().metadataDir, sb.path());       // a felülírás győz
    QCOMPARE(sm.settings().audioDir, sb.filePath("rec"));  // az explicit audioDir marad
    QVERIFY(!QDir(realMeta()).exists());
}

void PathsTest::appControllerStaysInside()
{
    QTemporaryDir sb;
    QVERIFY(sb.isValid());
    qputenv("TANARA_HOME", sb.path().toUtf8());
    qputenv("TANARA_CLOUD", "off");
    const QStringList before = listAll(m_fakeHome.path());
    {
        AppController app;
        app.setLastUsedDeviceNames({QStringLiteral("Mic")});          // state.json
        app.setSecret(QStringLiteral("soniox.apiKey"), QStringLiteral("x"));   // secrets.json
        const Meeting m = app.store()->createMeeting(QStringLiteral("Próba"));
        QVERIFY(m.folder.startsWith(sb.path() + QLatin1Char('/')));
        QVERIFY(!app.knownPeople().contains(QStringLiteral("nincs")));
    }
    for (const char* name : {"settings.json", "state.json", "secrets.json", "index.db"})
        QVERIFY2(QFile::exists(sb.filePath(QString::fromLatin1(name))), name);
    // A hamis HOME alatt semmi új nem keletkezett (se .tanara, se Tanara).
    QStringList created;
    for (const QString& p : listAll(m_fakeHome.path()))
        // (A hangrendszer kliens-könyvtára a ~/.config/pulse alá tehet sütit — az nem Tanara-adat.)
        if (!before.contains(p) && !p.contains(QStringLiteral("/.config"))) created << p;
    QVERIFY2(created.isEmpty(), qPrintable(created.join(QStringLiteral(", "))));
    QVERIFY(!QDir(realMeta()).exists());
}

QTEST_GUILESS_MAIN(PathsTest)
#include "test_paths.moc"

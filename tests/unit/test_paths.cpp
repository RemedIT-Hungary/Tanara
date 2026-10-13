// TANARA_HOME: overriding the metadata folder (tanara/Paths.h).
//
// The point: with TANARA_HOME set NOTHING may point into the real ~/.tanara: neither the
// default file paths (keys, people, voiceprints, prompts) nor a value leaking back from the
// settings (settings.json metadataDir). The test sets HOME to a temp folder too, so the
// "real" ~/.tanara is only a disposable place here, and we check that it stays untouched.
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
    void voiceModelResolutionOrder();

private:
    QTemporaryDir m_fakeHome;    // playing the role of the "real" HOME
    QTemporaryDir m_sandbox;     // TANARA_HOME
    bool m_homeFaked = false;    // whether QDir::homePath() points to m_fakeHome
    QString realMeta() const { return QDir(m_fakeHome.path()).filePath(QStringLiteral(".tanara")); }
    // Every file under the folder (recursively), to check that "nothing changed".
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
    m_homeFaked = QDir::homePath() == m_fakeHome.path();
#if !defined(Q_OS_WIN)
    QVERIFY(m_homeFaked);
#endif
}

void PathsTest::init()
{
    // On Windows QDir::homePath() gives the user profile, and HOME does not redirect it. Cases
    // built on HOME would write into the REAL profile there, so we skip them (not the
    // TANARA_HOME-independent ones).
    if (!m_homeFaked && qstrcmp(QTest::currentTestFunction(), "voiceModelResolutionOrder") != 0)
        QSKIP("A HOME ezen a platformon nem téríti el a QDir::homePath()-t.");
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
    // A value from the settings cannot redirect it either, even if it points at the real folder.
    QCOMPARE(paths::resolveMetadataDir(realMeta()), sb);
    QCOMPARE(paths::resolveMetadataDir(QStringLiteral("~/.tanara")), sb);
    QCOMPARE(paths::metadataFile(QStringLiteral("logs")), sb + QStringLiteral("/logs"));
    // The DEFAULTS of recordings / notes go into the sandbox too (not ~/Tanara).
    QCOMPARE(paths::defaultAudioDir(), sb + QStringLiteral("/recordings"));
    QCOMPARE(paths::defaultNotesDir(), sb + QStringLiteral("/notes"));
    // The prompt override files too.
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

    SettingsManager sm;   // empty: the default metadata folder
    QCOMPARE(sm.settingsFilePath(), sb + QStringLiteral("/settings.json"));
    QCOMPARE(sm.settings().metadataDir, sb);
    QCOMPARE(sm.settings().audioDir, sb + QStringLiteral("/recordings"));

    MeetingStore store(&sm);
    QCOMPARE(store.metadataDir(), sb);
    QVERIFY(QFile::exists(sb + QStringLiteral("/index.db")));

    // The "real" ~/.tanara and ~/Tanara were not created.
    QVERIFY2(!QDir(realMeta()).exists(), "a ~/.tanara létrejött TANARA_HOME mellett");
    QVERIFY(!QDir(QDir(m_fakeHome.path()).filePath("Tanara")).exists());
}

void PathsTest::settingsFileCannotRedirectOut()
{
    // A copied (real) settings.json: metadataDir points to the real ~/.tanara.
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
    QCOMPARE(sm.settings().metadataDir, sb.path());       // the override wins
    QCOMPARE(sm.settings().audioDir, sb.filePath("rec"));  // the explicit audioDir stays
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
    // Nothing new was created under the fake HOME (neither .tanara nor Tanara).
    QStringList created;
    for (const QString& p : listAll(m_fakeHome.path()))
        // (The audio system's client library may put a cookie under ~/.config/pulse; that is not Tanara data.)
        if (!before.contains(p) && !p.contains(QStringLiteral("/.config"))) created << p;
    QVERIFY2(created.isEmpty(), qPrintable(created.join(QStringLiteral(", "))));
    QVERIFY(!QDir(realMeta()).exists());
}

// The CAM++ model: first the user's metadata folder, then the models/ bundled next to the
// app, and if neither exists, the "expected place" in the metadata folder.
void PathsTest::voiceModelResolutionOrder()
{
    QTemporaryDir meta, app;
    QVERIFY(meta.isValid() && app.isValid());
    const QString rel = QStringLiteral("models/") + paths::voiceModelFileName();
    const QString userModel = QDir(meta.path()).filePath(rel);
    const QString appModel = QDir(app.path()).filePath(rel);
    auto touch = [](const QString& f) {
        QVERIFY(QDir().mkpath(QFileInfo(f).absolutePath()));
        QFile file(f);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    };

    // 1) neither exists: the expected place (metadata folder), independent of appDir
    QCOMPARE(paths::resolveVoiceModelPath(meta.path(), app.path()), userModel);
    QCOMPARE(paths::resolveVoiceModelPath(meta.path(), QString()), userModel);

    // 2) only next to the app: the bundled model
    touch(appModel);
    QCOMPARE(paths::resolveVoiceModelPath(meta.path(), app.path()), appModel);
    QCOMPARE(paths::resolveVoiceModelPath(meta.path(), QString()), userModel);

    // 3) the user's model takes precedence
    touch(userModel);
    QCOMPARE(paths::resolveVoiceModelPath(meta.path(), app.path()), userModel);
}

QTEST_GUILESS_MAIN(PathsTest)
#include "test_paths.moc"

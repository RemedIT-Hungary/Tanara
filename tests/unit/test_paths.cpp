// AppPaths: the single source of truth of every Tanara folder (foundation/paths/AppPaths.h).
//
//  - the role table per platform (Linux via XDG variables pointing into a temp folder; Windows and
//    macOS through the pure resolver with a hand-built PlatformBase, no #ifdef);
//  - the "paths" settings section overrides data / models / recordings / notes ("~" expanded);
//  - TANARA_HOME overrides EVERYTHING, the settings too: nothing may point into the real home.
// The test sets HOME and the XDG variables to temp folders, so the "real" folders are only
// disposable places here, and we check that they stay untouched.
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/foundation/secrets/SecretStore.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/app/AppController.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/library/store/MeetingRepository.h"

using namespace tanara;

class PathsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void linuxRoleTable();
    void windowsRoleTable();
    void macRoleTable();
    void settingsSectionOverrides();
    void tanaraHomeWinsEverywhere();
    void tanaraHomeExpandsTildeAndRelative();
    void storesDefaultIntoOverride();
    void settingsFileCannotRedirectOut();
    void appControllerStaysInside();
    void voiceModelResolutionOrder();

private:
    QTemporaryDir m_fakeHome;    // playing the role of the "real" HOME
    QTemporaryDir m_sandbox;     // TANARA_HOME
    bool m_homeFaked = false;    // whether QDir::homePath() points to m_fakeHome
    QString fake(const QString& rel) const { return QDir(m_fakeHome.path()).filePath(rel); }
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
    // The XDG base folders inside the fake HOME (whatever the developer's environment says).
    qputenv("XDG_CONFIG_HOME", fake(QStringLiteral("xdg-config")).toUtf8());
    qputenv("XDG_DATA_HOME", fake(QStringLiteral("xdg-data")).toUtf8());
    qputenv("XDG_STATE_HOME", fake(QStringLiteral("xdg-state")).toUtf8());
    QVERIFY(QDir().mkpath(fake(QStringLiteral("xdg-config"))));
    {
        // The Documents folder (localized name, as on a Hungarian desktop).
        QFile dirs(fake(QStringLiteral("xdg-config/user-dirs.dirs")));
        QVERIFY(dirs.open(QIODevice::WriteOnly));
        dirs.write("XDG_DOCUMENTS_DIR=\"$HOME/Dokumentumok\"\n");
    }
    m_homeFaked = QDir::homePath() == m_fakeHome.path();
#if !defined(Q_OS_WIN)
    QVERIFY(m_homeFaked);
#endif
}

void PathsTest::init()
{
    // On Windows QDir::homePath() gives the user profile, and HOME does not redirect it. Cases built
    // on the system folders would point at the REAL profile there, so they are skipped (the pure
    // resolver cases run everywhere).
    const QByteArray fn = QTest::currentTestFunction();
    const bool pure = fn == "windowsRoleTable" || fn == "macRoleTable" || fn == "settingsSectionOverrides"
                      || fn == "voiceModelResolutionOrder";
    if (!m_homeFaked && !pure)
        QSKIP("A HOME ezen a platformon nem téríti el a QDir::homePath()-t.");
    qunsetenv("TANARA_HOME");
}

void PathsTest::linuxRoleTable()
{
#if !defined(Q_OS_LINUX)
    QSKIP("XDG layout: Linux only");
#else
    const paths::PlatformBase b = paths::systemBase();
    QCOMPARE(b.platform, paths::Platform::Linux);
    QCOMPARE(b.documents, fake(QStringLiteral("Dokumentumok")));
    const paths::AppPaths p = paths::AppPaths::system();
    QVERIFY(!p.sandboxed());
    QCOMPARE(p.configDir(), fake(QStringLiteral("xdg-config/tanara")));
    QCOMPARE(p.dataDir(), fake(QStringLiteral("xdg-data/tanara")));
    QCOMPARE(p.stateDir(), fake(QStringLiteral("xdg-state/tanara")));
    QCOMPARE(p.logDir(), fake(QStringLiteral("xdg-state/tanara/logs")));
    QCOMPARE(p.modelsDir(), fake(QStringLiteral("xdg-data/tanara/models")));
    QCOMPARE(p.primaryRecordingRoot(), fake(QStringLiteral("Dokumentumok/Tanara/recordings")));
    QCOMPARE(p.notesDir(), fake(QStringLiteral("Dokumentumok/Tanara/notes")));
    QCOMPARE(p.settingsFile(), fake(QStringLiteral("xdg-config/tanara/settings.json")));
    QCOMPARE(p.dataFile(paths::files::People), fake(QStringLiteral("xdg-data/tanara/people.json")));
    QCOMPARE(p.stateFile(paths::files::RecordingLock), fake(QStringLiteral("xdg-state/tanara/recording.lock")));
    for (paths::Role r : {paths::Role::Config, paths::Role::Data, paths::Role::Recordings})
        QCOMPARE(p.source(r), paths::Source::Default);
    // Without a published SettingsManager the process-wide paths are the same.
    QCOMPARE(paths::current().logDir(), p.logDir());
#endif
}

void PathsTest::windowsRoleTable()
{
    paths::PlatformBase b;
    b.platform = paths::Platform::Windows;
    b.home = QStringLiteral("C:/Users/anna");
    b.documents = QStringLiteral("C:/Users/anna/Documents");
    b.configBase = b.dataBase = QStringLiteral("C:/Users/anna/AppData/Roaming");
    b.stateBase = b.localBase = QStringLiteral("C:/Users/anna/AppData/Local");
    const paths::AppPaths p = paths::AppPaths::resolve(b, paths::PathSettings(), QString());
    // %APPDATA%\Tanara (not ...\RemedIT\Tanara): independent of the organization name.
    QCOMPARE(p.configDir(), QStringLiteral("C:/Users/anna/AppData/Roaming/Tanara"));
    QCOMPARE(p.dataDir(), QStringLiteral("C:/Users/anna/AppData/Roaming/Tanara/data"));
    QCOMPARE(p.stateDir(), QStringLiteral("C:/Users/anna/AppData/Local/Tanara"));
    QCOMPARE(p.logDir(), QStringLiteral("C:/Users/anna/AppData/Local/Tanara/logs"));
    QCOMPARE(p.modelsDir(), QStringLiteral("C:/Users/anna/AppData/Local/Tanara/models"));
    QCOMPARE(p.primaryRecordingRoot(), QStringLiteral("C:/Users/anna/Documents/Tanara/recordings"));
    QCOMPARE(p.notesDir(), QStringLiteral("C:/Users/anna/Documents/Tanara/notes"));
    // TANARA_HOME on Windows too.
    const paths::AppPaths h = paths::AppPaths::resolve(b, paths::PathSettings(), QStringLiteral("D:/sandbox"));
    QCOMPARE(h.configDir(), QStringLiteral("D:/sandbox"));
    QCOMPARE(h.modelsDir(), QStringLiteral("D:/sandbox/models"));
}

void PathsTest::macRoleTable()
{
    paths::PlatformBase b;
    b.platform = paths::Platform::MacOS;
    b.home = QStringLiteral("/Users/anna");
    b.documents = QStringLiteral("/Users/anna/Documents");
    b.configBase = b.dataBase = b.stateBase = QStringLiteral("/Users/anna/Library/Application Support");
    b.logBase = QStringLiteral("/Users/anna/Library/Logs");
    const paths::AppPaths p = paths::AppPaths::resolve(b, paths::PathSettings(), QString());
    QCOMPARE(p.configDir(), QStringLiteral("/Users/anna/Library/Application Support/Tanara"));
    QCOMPARE(p.dataDir(), QStringLiteral("/Users/anna/Library/Application Support/Tanara/data"));
    QCOMPARE(p.logDir(), QStringLiteral("/Users/anna/Library/Logs/Tanara"));
    QCOMPARE(p.modelsDir(), QStringLiteral("/Users/anna/Library/Application Support/Tanara/models"));
    QCOMPARE(p.primaryRecordingRoot(), QStringLiteral("/Users/anna/Documents/Tanara/recordings"));
}

void PathsTest::settingsSectionOverrides()
{
    paths::PlatformBase b;
    b.platform = paths::Platform::Linux;
    b.home = QStringLiteral("/home/u");
    b.documents = QStringLiteral("/home/u/Documents");
    b.configBase = QStringLiteral("/home/u/.config");
    b.dataBase = QStringLiteral("/home/u/.local/share");
    b.stateBase = QStringLiteral("/home/u/.local/state");

    // The legacy layout (~/.tanara + ~/Tanara) is just a "paths" section.
    paths::PathSettings legacy;
    legacy.dataDir = QStringLiteral("~/.tanara");
    legacy.modelsDir = QStringLiteral("~/.tanara/models");
    legacy.notesDir = QStringLiteral("~/Tanara/notes");
    legacy.recordingRoots = {{QStringLiteral("~/Tanara/recordings"), true}, {QStringLiteral("/mnt/regi"), false}};
    const paths::AppPaths p = paths::AppPaths::resolve(b, legacy, QString());
    QCOMPARE(p.dataDir(), QStringLiteral("/home/u/.tanara"));
    QCOMPARE(p.modelsDir(), QStringLiteral("/home/u/.tanara/models"));
    QCOMPARE(p.notesDir(), QStringLiteral("/home/u/Tanara/notes"));
    QCOMPARE(p.recordingRoots(), QStringList({"/home/u/Tanara/recordings", "/mnt/regi"}));
    QCOMPARE(p.primaryRecordingRoot(), QStringLiteral("/home/u/Tanara/recordings"));
    QCOMPARE(p.source(paths::Role::Data), paths::Source::Settings);
    QCOMPARE(p.source(paths::Role::Recordings), paths::Source::Settings);
    // config / state / logs are not settings: the platform convention stays.
    QCOMPARE(p.configDir(), QStringLiteral("/home/u/.config/tanara"));
    QCOMPARE(p.stateDir(), QStringLiteral("/home/u/.local/state/tanara"));
    QCOMPARE(p.source(paths::Role::Config), paths::Source::Default);

    // JSON round trip of the section.
    QVERIFY(paths::PathSettings::fromJson(legacy.toJson()) == legacy);
    // The primary root is replaceable, the others stay.
    paths::PathSettings moved = legacy;
    moved.setPrimaryRecordingRoot(QStringLiteral("/data/felv"));
    QCOMPARE(moved.primaryRecordingRoot(), QStringLiteral("/data/felv"));
    QCOMPARE(moved.recordingRoots.size(), 2);
    moved.setPrimaryRecordingRoot(QString());
    QCOMPARE(moved.primaryRecordingRoot(), QStringLiteral("/mnt/regi"));   // no primary: the first one

    // "~" expansion helper.
    QCOMPARE(paths::normalize(QStringLiteral("~"), QString()), QDir::homePath());
    QVERIFY(paths::normalize(QString()).isEmpty());
}

void PathsTest::tanaraHomeWinsEverywhere()
{
    const QString sb = m_sandbox.path();
    qputenv("TANARA_HOME", sb.toUtf8());
    QCOMPARE(paths::homeOverride(), sb);
    // A "paths" section cannot redirect it either, even if it points at the real folders.
    paths::PathSettings out;
    out.dataDir = fake(QStringLiteral(".tanara"));
    out.modelsDir = QStringLiteral("~/.tanara/models");
    out.notesDir = QStringLiteral("~/Tanara/notes");
    out.setPrimaryRecordingRoot(QStringLiteral("~/Tanara/recordings"));
    const paths::AppPaths p = paths::AppPaths::system(out);
    QVERIFY(p.sandboxed());
    QCOMPARE(p.configDir(), sb);
    QCOMPARE(p.dataDir(), sb);
    QCOMPARE(p.stateDir(), sb);
    QCOMPARE(p.logDir(), sb + QStringLiteral("/logs"));
    QCOMPARE(p.modelsDir(), sb + QStringLiteral("/models"));
    QCOMPARE(p.recordingRoots(), QStringList{sb + QStringLiteral("/recordings")});
    QCOMPARE(p.notesDir(), sb + QStringLiteral("/notes"));
    for (paths::Role r : {paths::Role::Config, paths::Role::Data, paths::Role::Models, paths::Role::Recordings})
        QCOMPARE(p.source(r), paths::Source::TanaraHome);
    QCOMPARE(paths::current().configDir(), sb);
    // The prompt override files too.
    QVERIFY(processing::promptFilePath(QStringLiteral("simple")).startsWith(sb + QLatin1Char('/')));
}

void PathsTest::tanaraHomeExpandsTildeAndRelative()
{
    qputenv("TANARA_HOME", "~/sandbox-home");
    QCOMPARE(paths::homeOverride(), fake(QStringLiteral("sandbox-home")));
    qputenv("TANARA_HOME", "rel/dir");
    QCOMPARE(paths::homeOverride(), QDir::cleanPath(QDir::current().absoluteFilePath("rel/dir")));
    qputenv("TANARA_HOME", "   ");
    QVERIFY(paths::homeOverride().isEmpty());
}

void PathsTest::storesDefaultIntoOverride()
{
    const QString sb = m_sandbox.path();
    qputenv("TANARA_HOME", sb.toUtf8());

    // The secret store: always the file in the sandbox (never the keychain).
    const secrets::OpenResult keys = secrets::open(SecretSettings{QStringLiteral("keychain")},
                                                  paths::current().dataFile(paths::files::Secrets), {});
    QCOMPARE(keys.store->backendName(), QStringLiteral("file"));
    keys.store->set(QStringLiteral("k"), QStringLiteral("v"));
    QVERIFY(QFile::exists(sb + QStringLiteral("/secrets.json")));

    library::PeopleStore people;
    people.add(QStringLiteral("Teszt Elek"));
    QVERIFY(QFile::exists(sb + QStringLiteral("/people.json")));

    library::VoiceprintStore prints;
    Voiceprint vp;
    vp.embedding = {1.0f, 0.0f};
    prints.addPrint(QStringLiteral("Teszt Elek"), vp);
    QVERIFY(QFile::exists(sb + QStringLiteral("/voiceprints.json")));

    app::SettingsManager sm;   // empty: the config folder of AppPaths
    QCOMPARE(sm.settingsFilePath(), sb + QStringLiteral("/settings.json"));
    QCOMPARE(sm.paths().dataDir(), sb);
    QCOMPARE(sm.paths().primaryRecordingRoot(), sb + QStringLiteral("/recordings"));

    library::MeetingRepository store(sm.paths().primaryRecordingRoot(), sm.paths().dataDir());
    QCOMPARE(store.metadataDir(), sb);
    QVERIFY(QFile::exists(sb + QStringLiteral("/index.db")));

    // Nothing was created in the fake home (no XDG folder, no Documents/Tanara, no ~/.tanara).
    for (const char* rel : {".tanara", "Tanara", "xdg-data/tanara", "xdg-state/tanara", "xdg-config/tanara",
                            "Dokumentumok/Tanara"})
        QVERIFY2(!QDir(fake(QString::fromLatin1(rel))).exists(), rel);
}

void PathsTest::settingsFileCannotRedirectOut()
{
    // A copied (real) v2 settings.json: its "paths" point to the real folders.
    QTemporaryDir sb;
    QVERIFY(sb.isValid());
    qputenv("TANARA_HOME", sb.path().toUtf8());
    paths::PathSettings real;
    real.dataDir = fake(QStringLiteral(".tanara"));
    real.notesDir = fake(QStringLiteral("Tanara/notes"));
    real.setPrimaryRecordingRoot(fake(QStringLiteral("Tanara/recordings")));
    QFile f(sb.filePath("settings.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(QJsonObject{{"version", 2}, {"paths", real.toJson()}}).toJson());
    f.close();

    app::SettingsManager sm;
    QVERIFY(sm.settings().paths == real);                 // kept in the settings ...
    QCOMPARE(sm.paths().dataDir(), sb.path());            // ... but the sandbox wins
    QCOMPARE(sm.paths().primaryRecordingRoot(), sb.filePath("recordings"));
    QCOMPARE(sm.paths().notesDir(), sb.filePath("notes"));
    QVERIFY(!QDir(fake(QStringLiteral(".tanara"))).exists());
    QVERIFY(!QDir(fake(QStringLiteral("Tanara"))).exists());
}

void PathsTest::appControllerStaysInside()
{
    QTemporaryDir sb;
    QVERIFY(sb.isValid());
    qputenv("TANARA_HOME", sb.path().toUtf8());
    qputenv("TANARA_CLOUD", "off");
    const QStringList before = listAll(m_fakeHome.path());
    {
        app::AppController app;
        QCOMPARE(app.secretBackend(), QStringLiteral("file"));
        app.setLastUsedDeviceNames({QStringLiteral("Mic")});          // state.json
        app.setSecret(QStringLiteral("soniox.apiKey"), QStringLiteral("x"));   // secrets.json
        const Meeting m = app.store()->createMeeting(QStringLiteral("Próba"));
        QVERIFY(m.folder.startsWith(sb.path() + QLatin1Char('/')));
        QVERIFY(!app.knownPeople().contains(QStringLiteral("nincs")));
    }
    for (const char* name : {"settings.json", "state.json", "secrets.json", "index.db"})
        QVERIFY2(QFile::exists(sb.filePath(QString::fromLatin1(name))), name);
    // Nothing new was created under the fake HOME.
    QStringList created;
    for (const QString& p : listAll(m_fakeHome.path()))
        // (The audio system's client library may put a cookie under ~/.config/pulse; that is not Tanara data.)
        if (!before.contains(p) && !p.contains(QStringLiteral("/pulse"))) created << p;
    QVERIFY2(created.isEmpty(), qPrintable(created.join(QStringLiteral(", "))));
}

// A speaker model: first the user's models folder, then the models/ bundled next to the app, and
// if neither exists, the "expected place" in the models folder.
void PathsTest::voiceModelResolutionOrder()
{
    QTemporaryDir models, app;
    QVERIFY(models.isValid() && app.isValid());
    const QString file = processing::VoiceModelRegistry::defaultModelFileName();
    const QString userModel = QDir(models.path()).filePath(file);
    const QString appModel = QDir(app.path()).filePath(QStringLiteral("models/") + file);
    auto touch = [](const QString& f) {
        QVERIFY(QDir().mkpath(QFileInfo(f).absolutePath()));
        QFile out(f);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("x");
    };

    // 1) neither exists: the expected place (models folder), independent of appDir
    QCOMPARE(processing::VoiceModelRegistry::resolveDefaultModelPath(models.path(), app.path()), userModel);
    QCOMPARE(processing::VoiceModelRegistry::resolveDefaultModelPath(models.path(), QString()), userModel);

    // 2) only next to the app: the bundled model
    touch(appModel);
    QCOMPARE(processing::VoiceModelRegistry::resolveDefaultModelPath(models.path(), app.path()), appModel);
    QCOMPARE(processing::VoiceModelRegistry::resolveDefaultModelPath(models.path(), QString()), userModel);

    // 3) the user's model takes precedence
    touch(userModel);
    QCOMPARE(processing::VoiceModelRegistry::resolveDefaultModelPath(models.path(), app.path()), userModel);
}

QTEST_GUILESS_MAIN(PathsTest)
#include "test_paths.moc"

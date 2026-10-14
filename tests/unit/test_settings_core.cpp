// The Settings window's core additions:
//  - settings.json v2 (sections), old / corrupt files, the legacy import;
//  - the user name of audio devices (RecordingSettings::deviceNames, tanara::devicenames): storage,
//    readability of old files, resolution in ONE place (recorder name, track name);
//  - cloudEstimateBeforeRun;
//  - autostart guard (with TANARA_HOME we do not touch the real entry);
//  - the prompts' variables and output form.
// Every disk operation happens in a temporary folder.
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/watcher/detect/Autostart.h"
#include "tanara/app/LegacySettings.h"
#include <QJsonArray>

#if defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

using namespace tanara;

namespace {

const QString kMic = QStringLiteral("alsa_input.usb-Trust_USB_Microphone-00.mono-fallback");
const QString kHeadset = QStringLiteral("Monitor of Sennheiser headset - Communication");
const QString kSpeakers = QStringLiteral("Monitor of Kanto YU4 - Optikai Digitális sztereó (IEC958)");

Track mk(const QString& id, const QString& device, TrackKind kind, float peak = 0.5f)
{
    Track t;
    t.id = id;
    t.deviceName = device;
    t.kind = kind;
    t.active = true;
    t.peakLevel = peak;
    return t;
}

} // namespace

class SettingsCoreTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_home;

private slots:
    void initTestCase()
    {
        // Isolated TANARA_HOME: SettingsManager's default folders (recordings, notes) go here too;
        // nothing touches the user's ~/.tanara and ~/Tanara folders.
        QVERIFY(m_home.isValid());
        qputenv("TANARA_HOME", m_home.path().toUtf8());
    }
    void init() { library::devicenames::setOverrides({}); }
    void cleanupTestCase() { library::devicenames::setOverrides({}); }

    // ---- device names ----

    void displayNameFallsBackToShortName()
    {
        QCOMPARE(library::devicenames::displayName(kSpeakers), QStringLiteral("Kanto YU4"));
        QCOMPARE(library::devicenames::displayName(kSpeakers), library::tracknames::shortDeviceName(kSpeakers));
        QVERIFY(!library::devicenames::hasOverride(kSpeakers));
        // Instead of an unshortenable (empty) name, the raw name itself.
        QCOMPARE(library::devicenames::displayName(QStringLiteral("x")), QStringLiteral("x"));
    }

    void overrideWinsEverywhere()
    {
        library::devicenames::setOverrides({{kSpeakers, QStringLiteral("  Nappali   hangfal ")}, {kMic, QString()}});
        QCOMPARE(library::devicenames::displayName(kSpeakers), QStringLiteral("Nappali hangfal"));   // simplified
        QVERIFY(library::devicenames::hasOverride(kSpeakers));
        QVERIFY(!library::devicenames::hasOverride(kMic));          // an empty name is not an override
        QCOMPARE(library::devicenames::overrides().size(), 1);
        // The draft table (Settings, before saving) is independent of the global one.
        QCOMPARE(library::devicenames::displayName(kSpeakers, {}), QStringLiteral("Kanto YU4"));
        QCOMPARE(library::devicenames::displayName(kMic, {{kMic, QStringLiteral("Asztali mikrofon")}}),
                 QStringLiteral("Asztali mikrofon"));
    }

    void trackNamesFollowTheDeviceName()
    {
        const QVector<Track> tracks{mk(QStringLiteral("mic"), kMic, TrackKind::Mic),
                                    mk(QStringLiteral("call"), kHeadset, TrackKind::Loopback),
                                    mk(QStringLiteral("sys"), kSpeakers, TrackKind::Loopback, 0.2f)};
        const QStringList before = library::tracknames::friendlyNames(tracks);
        QCOMPARE(before.size(), 3);
        QVERIFY(!before.contains(QStringLiteral("Nappali hangfal")));

        // The renamed device's track gets the user's name; the others keep their role name.
        library::devicenames::setOverrides({{kSpeakers, QStringLiteral("Nappali hangfal")}});
        const QStringList after = library::tracknames::friendlyNames(tracks);
        QCOMPARE(after.at(2), QStringLiteral("Nappali hangfal"));
        QCOMPARE(after.at(0), before.at(0));
        QCOMPARE(after.at(1), before.at(1));

        // The track's own (meeting-level) name is still stronger.
        Meeting m;
        m.folder = QDir::tempPath();
        m.tracks = tracks;
        m.tracks[2].customName = QStringLiteral("Zene");
        const QVector<library::TrackView> views = library::TrackCatalog::tracks(m);
        QCOMPARE(views.at(2).displayName, QStringLiteral("Zene"));
        QCOMPARE(views.at(2).friendlyName, QStringLiteral("Nappali hangfal"));
        QCOMPARE(views.at(2).rawDeviceName, kSpeakers);
    }

    void twoRenamedDevicesKeepTheirOwnNames()
    {
        const QString mic2 = QStringLiteral("alsa_input.pci-0000_00_1f.3.analog-stereo");
        library::devicenames::setOverrides({{kMic, QStringLiteral("Asztali")}, {mic2, QStringLiteral("Asztali")}});
        const QStringList names = library::tracknames::friendlyNames(
            {mk(QStringLiteral("a"), kMic, TrackKind::Mic), mk(QStringLiteral("b"), mic2, TrackKind::Mic)});
        // The same user name: we do not rewrite it as "Desktop (...)", only an ordinal tells them apart.
        QCOMPARE(names.at(0), QStringLiteral("Asztali"));
        QCOMPARE(names.at(1), QStringLiteral("Asztali 2"));
    }

    // ---- storage ----

    void jsonRoundTrip()
    {
        tanara::app::AppSettings s;
        s.recording.deviceNames = {{kMic, QStringLiteral("Asztali mikrofon")}, {kSpeakers, QStringLiteral("Nappali hangfal")}};
        s.cloud.estimateBeforeRun = false;
        s.paths.dataDir = QStringLiteral("~/adat");
        s.paths.setPrimaryRecordingRoot(QStringLiteral("/srv/felvetelek"));
        s.secrets.backend = QStringLiteral("file");
        const QJsonObject o = s.toJson();
        // Sections: every module's values under its own key.
        QVERIFY(o.value(QStringLiteral("recording")).toObject().value(QStringLiteral("deviceNames")).isObject());
        QCOMPARE(o.value(QStringLiteral("cloud")).toObject().value(QStringLiteral("estimateBeforeRun")).toBool(true), false);
        const tanara::app::AppSettings back = tanara::app::AppSettings::fromJson(o);
        QCOMPARE(back.recording.deviceNames, s.recording.deviceNames);
        QCOMPARE(back.cloud.estimateBeforeRun, false);
        QVERIFY(back.paths == s.paths);
        QCOMPARE(back.secrets.backend, QStringLiteral("file"));
        QVERIFY(!QJsonDocument(o).toJson().contains("apiKey"));
    }

    void v2FileRoundTrip()
    {
        // Write -> read through the SettingsManager: the file is v2 with sections.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            app::SettingsManager mgr(dir.path());
            tanara::app::AppSettings s = mgr.settings();
            s.recording.audioQuality = QStringLiteral("medium");
            s.watcher.silenceAskMinutes = 7;
            s.ui.language = QStringLiteral("en");
            s.processing.languageHints = {};
            s.providers.llm.configs[s.providers.llm.selectedId].model = QStringLiteral("m");
            mgr.setSettings(s);
        }
        QFile f(QDir(dir.path()).filePath(QStringLiteral("settings.json")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        QCOMPARE(o.value(QStringLiteral("version")).toInt(), 2);
        for (const QString& sec : tanara::app::AppSettings::sectionNames())
            QVERIFY2(o.value(sec).isObject(), qPrintable(sec));
        QVERIFY(!o.contains(QStringLiteral("audioQuality")));   // nothing flat any more
        app::SettingsManager again(dir.path());
        QVERIFY(!again.legacyFormat());
        QCOMPARE(again.settings().recording.audioQuality, QStringLiteral("medium"));
        QCOMPARE(again.settings().watcher.silenceAskMinutes, 7);
        QCOMPARE(again.settings().ui.language, QStringLiteral("en"));
        QVERIFY(again.settings().processing.languageHints.isEmpty());   // empty list = automatic, kept
        QCOMPARE(again.settings().providers.llm.selected().model, QStringLiteral("m"));
    }

    void oldFlatFileMeansDefaults()
    {
        // No backward compatibility (D8): an old flat settings.json -> defaults + warning; the file
        // is set aside (not overwritten) on the next save, so import-legacy can still read it.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("settings.json"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(QJsonObject{{"audioQuality", "low"}, {"userSpeakerName", "Lilla"},
                                              {"uiLanguage", "en"}}).toJson());
        }
        app::SettingsManager mgr(dir.path());
        QVERIFY(mgr.legacyFormat());
        QVERIFY(!mgr.loadFailed());
        QCOMPARE(mgr.settings().recording.audioQuality, QStringLiteral("best"));
        QCOMPARE(mgr.settings().ui.language, QStringLiteral("auto"));
        mgr.save();
        const QStringList aside = QDir(dir.path()).entryList({QStringLiteral("settings.json.v1-*")}, QDir::Files);
        QCOMPARE(aside.size(), 1);
        QFile old(QDir(dir.path()).filePath(aside.first()));
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(old.readAll()).object().value("audioQuality").toString(), QStringLiteral("low"));
    }

    void corruptFileIsSetAside()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("settings.json"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ nem json");
        }
        app::SettingsManager mgr(dir.path());
        QVERIFY(mgr.loadFailed());
        QVERIFY(QFile::exists(path));   // untouched until a save
        mgr.save();
        QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("settings.json.corrupt-*")}, QDir::Files).size(), 1);
        QVERIFY(!app::SettingsManager(dir.path()).loadFailed());
    }

    void legacyImportMapsFlatKeys()
    {
        // tanara-cli settings import-legacy: the old flat values land in their sections.
        const QJsonObject flat{{"audioQuality", "low"}, {"uiLanguage", "en"}, {"watcherAutostart", true},
                               {"cloudEstimateBeforeRun", false}, {"voiceModels", QJsonArray{"b", "a"}},
                               {"sttProviderId", "soniox"},
                               {"sttProviders", QJsonObject{{"soniox", QJsonObject{{"type", "soniox"}, {"model", "x"}}}}},
                               {"audioDir", "~/Felv"}, {"metadataDir", "/m/.tanara"}};
        QVERIFY(app::legacy::isFlat(flat));
        const tanara::app::AppSettings s = app::legacy::importFlat(flat, app::SettingsManager::defaults());
        QCOMPARE(s.recording.audioQuality, QStringLiteral("low"));
        QCOMPARE(s.ui.language, QStringLiteral("en"));
        QVERIFY(s.watcher.autostart);
        QVERIFY(!s.cloud.estimateBeforeRun);
        QCOMPARE(s.processing.voiceModels, QStringList({"a", "b"}));
        QCOMPARE(s.providers.stt.selected().model, QStringLiteral("x"));
        QVERIFY(s.paths == paths::PathSettings());   // paths: adopt-legacy's job
        const paths::PathSettings p = app::legacy::legacyPaths(flat, QStringLiteral("/home/u"));
        QCOMPARE(p.dataDir, QStringLiteral("/m/.tanara"));
        QCOMPARE(p.modelsDir, QStringLiteral("/m/.tanara/models"));
        QCOMPARE(p.primaryRecordingRoot(), QStringLiteral("~/Felv"));
        QCOMPARE(p.notesDir, QStringLiteral("/home/u/Tanara/notes"));
    }

    void settingsManagerFeedsTheResolver()
    {
        const QDir dir(m_home.path());
        {
            app::SettingsManager mgr(dir.path());
            QVERIFY(library::devicenames::overrides().isEmpty());
            tanara::app::AppSettings s = mgr.settings();
            s.recording.deviceNames.insert(kSpeakers, QStringLiteral("Nappali hangfal"));
            mgr.setSettings(s);
            QCOMPARE(library::devicenames::displayName(kSpeakers), QStringLiteral("Nappali hangfal"));
        }
        // A new process (here: a new instance) loads from disk too.
        library::devicenames::setOverrides({});
        app::SettingsManager again(dir.path());
        QCOMPARE(again.settings().recording.deviceNames.value(kSpeakers), QStringLiteral("Nappali hangfal"));
        QCOMPARE(library::devicenames::displayName(kSpeakers), QStringLiteral("Nappali hangfal"));
        // In the file the key sits next to the other settings.
        QFile f(again.settingsFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(o.value(QStringLiteral("recording")).toObject().value(QStringLiteral("deviceNames"))
                     .toObject().value(kSpeakers).toString(),
                 QStringLiteral("Nappali hangfal"));
        QVERIFY(o.contains(QStringLiteral("paths")));
    }

    // ---- autostart -----------------------------------------------------------------------

    void autostartEntryIsWrittenOnlyWhereAsked()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString entry = dir.filePath(QStringLiteral("autostart/tanara-watcher.desktop"));
        QVERIFY(watcher::autostart::writeEntry(entry, QStringLiteral("/opt/tanara/tanara-watcher")));
        QFile f(entry);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.contains(QStringLiteral("[Desktop Entry]")));
        QVERIFY(text.contains(QStringLiteral("Exec=/opt/tanara/tanara-watcher\n")));
        // An empty executable path: does not write.
        QVERIFY(!watcher::autostart::writeEntry(dir.filePath(QStringLiteral("x.desktop")), QString()));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("x.desktop"))));
    }

    void autostartIsNeverTouchedInASandbox()
    {
        // With TANARA_HOME (sandbox, test; set throughout here) the real ~/.config/autostart entry is
        // untouchable: no creation, deletion or rewrite.
        const QString real = watcher::autostart::watcherEntryPath();
        const bool existed = !real.isEmpty() && QFile::exists(real);
        const QDateTime stamp = existed ? QFileInfo(real).lastModified() : QDateTime();

#if defined(Q_OS_WIN)
        // On Windows the entry is the "Tanara Watcher" value under HKCU\...\Run.
        const QString realRun = watcher::autostart::readRunValue(QLatin1String(watcher::autostart::kRunSubKey),
                                                         QLatin1String(watcher::autostart::kRunValueName));
#endif

        QVERIFY(!qEnvironmentVariableIsEmpty("TANARA_HOME"));
        QVERIFY(!watcher::autostart::managed());
        QVERIFY(!watcher::autostart::applyWatcher(true, QStringLiteral("/nem/letezik/tanara-watcher")));
        QVERIFY(!watcher::autostart::applyWatcher(false, QString()));
#if defined(Q_OS_WIN)
        QCOMPARE(watcher::autostart::readRunValue(QLatin1String(watcher::autostart::kRunSubKey),
                                         QLatin1String(watcher::autostart::kRunValueName)),
                 realRun);
#endif

        if (!real.isEmpty()) {
            QCOMPARE(QFile::exists(real), existed);
            if (existed) QCOMPARE(QFileInfo(real).lastModified(), stamp);
        }
    }

    // Windows Run key: write, read back, delete on an OWN test key that is then deleted (the test
    // does not touch the real Run key; nothing remains afterwards).
    void autostartRunValueRoundTrip()
    {
#if defined(Q_OS_WIN)
        const QString key = QStringLiteral("Software\\TanaraUnitTest-%1")
                                .arg(QCoreApplication::applicationPid());
        const QString name = QLatin1String(watcher::autostart::kRunValueName);
        auto cleanup = qScopeGuard([&] {
            RegDeleteKeyW(HKEY_CURRENT_USER, reinterpret_cast<const wchar_t*>(key.utf16()));
        });
        QVERIFY(watcher::autostart::readRunValue(key, name).isEmpty());
        QVERIFY(watcher::autostart::writeRunValue(key, name, QStringLiteral("C:/Program Files/Tanara/tanara-watcher.exe")));
        // A quoted path with native separators (because of the path with spaces).
        QCOMPARE(watcher::autostart::readRunValue(key, name),
                 QStringLiteral("\"C:\\Program Files\\Tanara\\tanara-watcher.exe\""));
        QVERIFY(!watcher::autostart::writeRunValue(key, name, QString()));   // empty path: does not write
        QVERIFY(watcher::autostart::removeRunValue(key, name));
        QVERIFY(watcher::autostart::readRunValue(key, name).isEmpty());
        QVERIFY(watcher::autostart::removeRunValue(key, name));               // already gone: true anyway
        // The entry path is a human-readable registry path.
        QVERIFY(watcher::autostart::watcherEntryPath().endsWith(QStringLiteral("\\Run\\Tanara Watcher")));
#else
        QVERIFY(watcher::autostart::readRunValue(QStringLiteral("x"), QStringLiteral("y")).isEmpty());
        QVERIFY(!watcher::autostart::writeRunValue(QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")));
#endif
    }

    void findWatcherExecutableLooksNextToTheApp()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(watcher::autostart::findWatcherExecutable(dir.path()).isEmpty());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("gui")));
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("watcher")));
#if defined(Q_OS_WIN)
        const QString exe = QStringLiteral("tanara-watcher.exe");
#else
        const QString exe = QStringLiteral("tanara-watcher");
#endif
        QFile f(dir.filePath(QStringLiteral("watcher/") + exe));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        // In the build tree: ../watcher/tanara-watcher from next to <build>/gui/tanara.
        QCOMPARE(watcher::autostart::findWatcherExecutable(dir.filePath(QStringLiteral("gui"))),
                 QFileInfo(f).absoluteFilePath());
    }

    // ---- prompts ----

    void promptVariablesAreTheOnesTheCodeSubstitutes()
    {
        const QVector<processing::PromptVariable> vars = processing::promptVariables();
        QVERIFY(!vars.isEmpty());
        for (const processing::PromptVariable& v : vars) {
            QVERIFY(!v.description.isEmpty());
            // What the legend promises, applySummaryLanguage really replaces ...
            const QString out = processing::applySummaryLanguage(QStringLiteral("A: ") + v.token, QStringLiteral("angol"));
            QVERIFY2(!out.contains(v.token), qPrintable(v.token));
        }
        // ... and the built-in prompts use only such variables.
        static const QRegularExpression re(QStringLiteral("\\{\\{[^}]+\\}\\}"));
        QStringList known;
        for (const processing::PromptVariable& v : vars) known << v.token;
        for (const QString& id : {QStringLiteral("single"), QStringLiteral("notes"), QStringLiteral("merge"),
                                  QStringLiteral("topic"), QStringLiteral("analysis"), QStringLiteral("reduce")}) {
            QVERIFY2(!processing::promptBuiltin(id).isEmpty(), qPrintable(id));
            auto it = re.globalMatch(processing::promptBuiltin(id));
            while (it.hasNext()) {
                const QString token = it.next().captured();
                QVERIFY2(known.contains(token), qPrintable(id + QStringLiteral(": ") + token));
            }
        }
    }

    void outputFormats()
    {
        // The short form's keys: the single-pass ("single", formerly "simple") and the merge ("merge")
        // prompt ask for the same ones, and the schema description shows them too.
        for (const QString& id : {QStringLiteral("single"), QStringLiteral("simple"), QStringLiteral("merge")}) {
            const processing::PromptOutputFormat f = processing::promptOutputFormat(id);
            QCOMPARE(f.kind, QStringLiteral("json"));
            for (const QString& key : {QStringLiteral("execSummary"), QStringLiteral("decisions"),
                                       QStringLiteral("openQuestions"), QStringLiteral("actionItems")}) {
                QVERIFY2(f.summary.contains(key), qPrintable(id + key));
                QVERIFY2(f.body.contains(key), qPrintable(id + key));
                QVERIFY2(processing::promptBuiltin(id).contains(key), qPrintable(id + key));
            }
        }
        // The parser expects the note step's four headers in English.
        const processing::PromptOutputFormat notes = processing::promptOutputFormat(QStringLiteral("notes"));
        QCOMPARE(notes.kind, QStringLiteral("text"));
        for (const QString& h : {QStringLiteral("TOPICS"), QStringLiteral("DECISIONS"),
                                 QStringLiteral("OPEN"), QStringLiteral("ACTIONS")}) {
            QVERIFY2(notes.body.contains(h), qPrintable(h));
            QVERIFY2(processing::promptBuiltin(QStringLiteral("notes")).contains(h + QLatin1Char('\n')), qPrintable(h));
        }
        QVERIFY(processing::promptBuiltin(QStringLiteral("single")).contains(QStringLiteral("\nSUMMARY\n")));
        // The parser matches the analysis's Hungarian section titles; these stay in the English prompt too.
        for (const QString& h : {QStringLiteral("## Döntések"), QStringLiteral("## Nyitott kérdések"),
                                 QStringLiteral("## Teendők")})
            QVERIFY2(processing::promptBuiltin(QStringLiteral("analysis")).contains(h), qPrintable(h));
        const processing::PromptOutputFormat analysis = processing::promptOutputFormat(QStringLiteral("analysis"));
        QCOMPARE(analysis.kind, QStringLiteral("markdown"));
        QVERIFY(analysis.body.contains(QStringLiteral("## Döntések")));
        QVERIFY(analysis.body.contains(QStringLiteral("## Teendők")));
        QCOMPARE(processing::promptOutputFormat(QStringLiteral("topic")).kind, QStringLiteral("markdown"));
        QVERIFY(processing::promptOutputFormat(QStringLiteral("nincs-ilyen")).kind.isEmpty());
    }
};

QTEST_GUILESS_MAIN(SettingsCoreTest)
#include "test_settings_core.moc"

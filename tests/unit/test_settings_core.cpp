// The Settings window's core additions:
//  - the user name of audio devices (AppSettings::deviceNames, tanara::devicenames): storage,
//    readability of old files, resolution in ONE place (recorder name, track name);
//  - cloudEstimateBeforeRun;
//  - autostart guard (with TANARA_HOME we do not touch the real entry);
//  - the prompts' variables and output form.
// Every disk operation happens in a temporary folder.
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "tanara/PromptLibrary.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/detect/Autostart.h"
#include "tanara/store/JsonSerialization.h"

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
    void init() { devicenames::setOverrides({}); }
    void cleanupTestCase() { devicenames::setOverrides({}); }

    // ---- device names ----

    void displayNameFallsBackToShortName()
    {
        QCOMPARE(devicenames::displayName(kSpeakers), QStringLiteral("Kanto YU4"));
        QCOMPARE(devicenames::displayName(kSpeakers), tracknames::shortDeviceName(kSpeakers));
        QVERIFY(!devicenames::hasOverride(kSpeakers));
        // Instead of an unshortenable (empty) name, the raw name itself.
        QCOMPARE(devicenames::displayName(QStringLiteral("x")), QStringLiteral("x"));
    }

    void overrideWinsEverywhere()
    {
        devicenames::setOverrides({{kSpeakers, QStringLiteral("  Nappali   hangfal ")}, {kMic, QString()}});
        QCOMPARE(devicenames::displayName(kSpeakers), QStringLiteral("Nappali hangfal"));   // simplified
        QVERIFY(devicenames::hasOverride(kSpeakers));
        QVERIFY(!devicenames::hasOverride(kMic));          // an empty name is not an override
        QCOMPARE(devicenames::overrides().size(), 1);
        // The draft table (Settings, before saving) is independent of the global one.
        QCOMPARE(devicenames::displayName(kSpeakers, {}), QStringLiteral("Kanto YU4"));
        QCOMPARE(devicenames::displayName(kMic, {{kMic, QStringLiteral("Asztali mikrofon")}}),
                 QStringLiteral("Asztali mikrofon"));
    }

    void trackNamesFollowTheDeviceName()
    {
        const QVector<Track> tracks{mk(QStringLiteral("mic"), kMic, TrackKind::Mic),
                                    mk(QStringLiteral("call"), kHeadset, TrackKind::Loopback),
                                    mk(QStringLiteral("sys"), kSpeakers, TrackKind::Loopback, 0.2f)};
        const QStringList before = tracknames::friendlyNames(tracks);
        QCOMPARE(before.size(), 3);
        QVERIFY(!before.contains(QStringLiteral("Nappali hangfal")));

        // The renamed device's track gets the user's name; the others keep their role name.
        devicenames::setOverrides({{kSpeakers, QStringLiteral("Nappali hangfal")}});
        const QStringList after = tracknames::friendlyNames(tracks);
        QCOMPARE(after.at(2), QStringLiteral("Nappali hangfal"));
        QCOMPARE(after.at(0), before.at(0));
        QCOMPARE(after.at(1), before.at(1));

        // The track's own (meeting-level) name is still stronger.
        Meeting m;
        m.folder = QDir::tempPath();
        m.tracks = tracks;
        m.tracks[2].customName = QStringLiteral("Zene");
        const QVector<TrackView> views = TrackCatalog::tracks(m);
        QCOMPARE(views.at(2).displayName, QStringLiteral("Zene"));
        QCOMPARE(views.at(2).friendlyName, QStringLiteral("Nappali hangfal"));
        QCOMPARE(views.at(2).rawDeviceName, kSpeakers);
    }

    void twoRenamedDevicesKeepTheirOwnNames()
    {
        const QString mic2 = QStringLiteral("alsa_input.pci-0000_00_1f.3.analog-stereo");
        devicenames::setOverrides({{kMic, QStringLiteral("Asztali")}, {mic2, QStringLiteral("Asztali")}});
        const QStringList names = tracknames::friendlyNames(
            {mk(QStringLiteral("a"), kMic, TrackKind::Mic), mk(QStringLiteral("b"), mic2, TrackKind::Mic)});
        // The same user name: we do not rewrite it as "Desktop (...)", only an ordinal tells them apart.
        QCOMPARE(names.at(0), QStringLiteral("Asztali"));
        QCOMPARE(names.at(1), QStringLiteral("Asztali 2"));
    }

    // ---- storage ----

    void jsonRoundTrip()
    {
        AppSettings s;
        s.deviceNames = {{kMic, QStringLiteral("Asztali mikrofon")}, {kSpeakers, QStringLiteral("Nappali hangfal")}};
        s.cloudEstimateBeforeRun = false;
        const QJsonObject o = toJson(s);
        QVERIFY(o.value(QStringLiteral("deviceNames")).isObject());
        const AppSettings back = appSettingsFromJson(o);
        QCOMPARE(back.deviceNames, s.deviceNames);
        QCOMPARE(back.cloudEstimateBeforeRun, false);
    }

    void oldFilesStayReadable()
    {
        // An old build's settings.json: without the new keys, so defaults.
        QJsonObject old = toJson(AppSettings{});
        old.remove(QStringLiteral("deviceNames"));
        old.remove(QStringLiteral("cloudEstimateBeforeRun"));
        old[QStringLiteral("userSpeakerName")] = QStringLiteral("Lilla");
        const AppSettings s = appSettingsFromJson(old);
        QVERIFY(s.deviceNames.isEmpty());
        QCOMPARE(s.cloudEstimateBeforeRun, true);
        QCOMPARE(s.userSpeakerName, QStringLiteral("Lilla"));

        // An old build reads the new file too: the existing keys' meaning did not change, and it just
        // does not know the new ones (here: we remove them, and everything else stays the same).
        AppSettings now;
        now.deviceNames = {{kMic, QStringLiteral("Asztali")}};
        now.audioQuality = QStringLiteral("medium");
        QJsonObject fresh = toJson(now);
        fresh.remove(QStringLiteral("deviceNames"));
        fresh.remove(QStringLiteral("cloudEstimateBeforeRun"));
        QCOMPARE(appSettingsFromJson(fresh).audioQuality, QStringLiteral("medium"));
    }

    void emptyNamesAreNotStored()
    {
        QJsonObject o = toJson(AppSettings{});
        o[QStringLiteral("deviceNames")] = QJsonObject{{kMic, QStringLiteral("   ")}, {kSpeakers, QStringLiteral("Hangfal")}};
        const AppSettings s = appSettingsFromJson(o);
        QCOMPARE(s.deviceNames.size(), 1);
        QCOMPARE(s.deviceNames.value(kSpeakers), QStringLiteral("Hangfal"));
    }

    void settingsManagerFeedsTheResolver()
    {
        const QDir dir(m_home.path());
        {
            SettingsManager mgr(dir.path());
            QVERIFY(devicenames::overrides().isEmpty());
            AppSettings s = mgr.settings();
            s.deviceNames.insert(kSpeakers, QStringLiteral("Nappali hangfal"));
            mgr.setSettings(s);
            QCOMPARE(devicenames::displayName(kSpeakers), QStringLiteral("Nappali hangfal"));
        }
        // A new process (here: a new instance) loads from disk too.
        devicenames::setOverrides({});
        SettingsManager again(dir.path());
        QCOMPARE(again.settings().deviceNames.value(kSpeakers), QStringLiteral("Nappali hangfal"));
        QCOMPARE(devicenames::displayName(kSpeakers), QStringLiteral("Nappali hangfal"));
        // In the file the key sits next to the other settings.
        QFile f(again.settingsFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(o.value(QStringLiteral("deviceNames")).toObject().value(kSpeakers).toString(),
                 QStringLiteral("Nappali hangfal"));
        QVERIFY(o.contains(QStringLiteral("audioDir")));
    }

    // ---- autostart -----------------------------------------------------------------------

    void autostartEntryIsWrittenOnlyWhereAsked()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString entry = dir.filePath(QStringLiteral("autostart/tanara-watcher.desktop"));
        QVERIFY(autostart::writeEntry(entry, QStringLiteral("/opt/tanara/tanara-watcher")));
        QFile f(entry);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.contains(QStringLiteral("[Desktop Entry]")));
        QVERIFY(text.contains(QStringLiteral("Exec=/opt/tanara/tanara-watcher\n")));
        // An empty executable path: does not write.
        QVERIFY(!autostart::writeEntry(dir.filePath(QStringLiteral("x.desktop")), QString()));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("x.desktop"))));
    }

    void autostartIsNeverTouchedInASandbox()
    {
        // With TANARA_HOME (sandbox, test; set throughout here) the real ~/.config/autostart entry is
        // untouchable: no creation, deletion or rewrite.
        const QString real = autostart::watcherEntryPath();
        const bool existed = !real.isEmpty() && QFile::exists(real);
        const QDateTime stamp = existed ? QFileInfo(real).lastModified() : QDateTime();

#if defined(Q_OS_WIN)
        // On Windows the entry is the "Tanara Watcher" value under HKCU\...\Run.
        const QString realRun = autostart::readRunValue(QLatin1String(autostart::kRunSubKey),
                                                         QLatin1String(autostart::kRunValueName));
#endif

        QVERIFY(!qEnvironmentVariableIsEmpty("TANARA_HOME"));
        QVERIFY(!autostart::managed());
        QVERIFY(!autostart::applyWatcher(true, QStringLiteral("/nem/letezik/tanara-watcher")));
        QVERIFY(!autostart::applyWatcher(false, QString()));
#if defined(Q_OS_WIN)
        QCOMPARE(autostart::readRunValue(QLatin1String(autostart::kRunSubKey),
                                         QLatin1String(autostart::kRunValueName)),
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
        const QString name = QLatin1String(autostart::kRunValueName);
        auto cleanup = qScopeGuard([&] {
            RegDeleteKeyW(HKEY_CURRENT_USER, reinterpret_cast<const wchar_t*>(key.utf16()));
        });
        QVERIFY(autostart::readRunValue(key, name).isEmpty());
        QVERIFY(autostart::writeRunValue(key, name, QStringLiteral("C:/Program Files/Tanara/tanara-watcher.exe")));
        // A quoted path with native separators (because of the path with spaces).
        QCOMPARE(autostart::readRunValue(key, name),
                 QStringLiteral("\"C:\\Program Files\\Tanara\\tanara-watcher.exe\""));
        QVERIFY(!autostart::writeRunValue(key, name, QString()));   // empty path: does not write
        QVERIFY(autostart::removeRunValue(key, name));
        QVERIFY(autostart::readRunValue(key, name).isEmpty());
        QVERIFY(autostart::removeRunValue(key, name));               // already gone: true anyway
        // The entry path is a human-readable registry path.
        QVERIFY(autostart::watcherEntryPath().endsWith(QStringLiteral("\\Run\\Tanara Watcher")));
#else
        QVERIFY(autostart::readRunValue(QStringLiteral("x"), QStringLiteral("y")).isEmpty());
        QVERIFY(!autostart::writeRunValue(QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")));
#endif
    }

    void findWatcherExecutableLooksNextToTheApp()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(autostart::findWatcherExecutable(dir.path()).isEmpty());
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
        QCOMPARE(autostart::findWatcherExecutable(dir.filePath(QStringLiteral("gui"))),
                 QFileInfo(f).absoluteFilePath());
    }

    // ---- prompts ----

    void promptVariablesAreTheOnesTheCodeSubstitutes()
    {
        const QVector<PromptVariable> vars = promptVariables();
        QVERIFY(!vars.isEmpty());
        for (const PromptVariable& v : vars) {
            QVERIFY(!v.description.isEmpty());
            // What the legend promises, applySummaryLanguage really replaces ...
            const QString out = applySummaryLanguage(QStringLiteral("A: ") + v.token, QStringLiteral("angol"));
            QVERIFY2(!out.contains(v.token), qPrintable(v.token));
        }
        // ... and the built-in prompts use only such variables.
        static const QRegularExpression re(QStringLiteral("\\{\\{[^}]+\\}\\}"));
        QStringList known;
        for (const PromptVariable& v : vars) known << v.token;
        for (const QString& id : {QStringLiteral("single"), QStringLiteral("notes"), QStringLiteral("merge"),
                                  QStringLiteral("topic"), QStringLiteral("analysis"), QStringLiteral("reduce")}) {
            QVERIFY2(!promptBuiltin(id).isEmpty(), qPrintable(id));
            auto it = re.globalMatch(promptBuiltin(id));
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
            const PromptOutputFormat f = promptOutputFormat(id);
            QCOMPARE(f.kind, QStringLiteral("json"));
            for (const QString& key : {QStringLiteral("execSummary"), QStringLiteral("decisions"),
                                       QStringLiteral("openQuestions"), QStringLiteral("actionItems")}) {
                QVERIFY2(f.summary.contains(key), qPrintable(id + key));
                QVERIFY2(f.body.contains(key), qPrintable(id + key));
                QVERIFY2(promptBuiltin(id).contains(key), qPrintable(id + key));
            }
        }
        // The parser expects the note step's four headers in English.
        const PromptOutputFormat notes = promptOutputFormat(QStringLiteral("notes"));
        QCOMPARE(notes.kind, QStringLiteral("text"));
        for (const QString& h : {QStringLiteral("TOPICS"), QStringLiteral("DECISIONS"),
                                 QStringLiteral("OPEN"), QStringLiteral("ACTIONS")}) {
            QVERIFY2(notes.body.contains(h), qPrintable(h));
            QVERIFY2(promptBuiltin(QStringLiteral("notes")).contains(h + QLatin1Char('\n')), qPrintable(h));
        }
        QVERIFY(promptBuiltin(QStringLiteral("single")).contains(QStringLiteral("\nSUMMARY\n")));
        // The parser matches the analysis's Hungarian section titles; these stay in the English prompt too.
        for (const QString& h : {QStringLiteral("## Döntések"), QStringLiteral("## Nyitott kérdések"),
                                 QStringLiteral("## Teendők")})
            QVERIFY2(promptBuiltin(QStringLiteral("analysis")).contains(h), qPrintable(h));
        const PromptOutputFormat analysis = promptOutputFormat(QStringLiteral("analysis"));
        QCOMPARE(analysis.kind, QStringLiteral("markdown"));
        QVERIFY(analysis.body.contains(QStringLiteral("## Döntések")));
        QVERIFY(analysis.body.contains(QStringLiteral("## Teendők")));
        QCOMPARE(promptOutputFormat(QStringLiteral("topic")).kind, QStringLiteral("markdown"));
        QVERIFY(promptOutputFormat(QStringLiteral("nincs-ilyen")).kind.isEmpty());
    }
};

QTEST_GUILESS_MAIN(SettingsCoreTest)
#include "test_settings_core.moc"

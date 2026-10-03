//
// A Beállítások-ablak core-kiegészítései:
//  - hangeszközök felhasználói neve (AppSettings::deviceNames → tanara::devicenames): tárolás,
//    régi fájlok olvashatósága, feloldás EGY helyen (felvevő-név, sáv-név);
//  - cloudEstimateBeforeRun;
//  - autostart-védelem (TANARA_HOME mellett a valódi bejegyzéshez nem nyúlunk);
//  - a promptok változói és kimeneti formája.
// Minden lemez-művelet ideiglenes mappában történik.
//
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "tanara/PromptLibrary.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/detect/Autostart.h"
#include "tanara/store/JsonSerialization.h"

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
        // Izolált TANARA_HOME: a SettingsManager alapértelmezett mappái (felvételek, jegyzetek)
        // is ide kerülnek — a felhasználó ~/.tanara és ~/Tanara mappájához semmi nem nyúl.
        QVERIFY(m_home.isValid());
        qputenv("TANARA_HOME", m_home.path().toUtf8());
    }
    void init() { devicenames::setOverrides({}); }
    void cleanupTestCase() { devicenames::setOverrides({}); }

    // ---- eszköznevek ---------------------------------------------------------------------

    void displayNameFallsBackToShortName()
    {
        QCOMPARE(devicenames::displayName(kSpeakers), QStringLiteral("Kanto YU4"));
        QCOMPARE(devicenames::displayName(kSpeakers), tracknames::shortDeviceName(kSpeakers));
        QVERIFY(!devicenames::hasOverride(kSpeakers));
        // Rövidíthetetlen (üres) név helyett maga a nyers név.
        QCOMPARE(devicenames::displayName(QStringLiteral("x")), QStringLiteral("x"));
    }

    void overrideWinsEverywhere()
    {
        devicenames::setOverrides({{kSpeakers, QStringLiteral("  Nappali   hangfal ")}, {kMic, QString()}});
        QCOMPARE(devicenames::displayName(kSpeakers), QStringLiteral("Nappali hangfal"));   // egyszerűsítve
        QVERIFY(devicenames::hasOverride(kSpeakers));
        QVERIFY(!devicenames::hasOverride(kMic));          // az üres név nem felülírás
        QCOMPARE(devicenames::overrides().size(), 1);
        // A piszkozat-tábla (Beállítások, mentés előtt) a globálistól független.
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

        // Az átnevezett eszköz sávja a felhasználó nevét kapja; a többi a szerep-nevét tartja.
        devicenames::setOverrides({{kSpeakers, QStringLiteral("Nappali hangfal")}});
        const QStringList after = tracknames::friendlyNames(tracks);
        QCOMPARE(after.at(2), QStringLiteral("Nappali hangfal"));
        QCOMPARE(after.at(0), before.at(0));
        QCOMPARE(after.at(1), before.at(1));

        // A sáv saját (meeting-szintű) neve továbbra is erősebb.
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
        // Azonos felhasználói név: nem írjuk át „Asztali (…)”-ra, csak sorszám különbözteti meg.
        QCOMPARE(names.at(0), QStringLiteral("Asztali"));
        QCOMPARE(names.at(1), QStringLiteral("Asztali 2"));
    }

    // ---- tárolás -------------------------------------------------------------------------

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
        // Egy régi build settings.json-ja: az új kulcsok nélkül → alapértelmezések.
        QJsonObject old = toJson(AppSettings{});
        old.remove(QStringLiteral("deviceNames"));
        old.remove(QStringLiteral("cloudEstimateBeforeRun"));
        old[QStringLiteral("userSpeakerName")] = QStringLiteral("Lilla");
        const AppSettings s = appSettingsFromJson(old);
        QVERIFY(s.deviceNames.isEmpty());
        QCOMPARE(s.cloudEstimateBeforeRun, true);
        QCOMPARE(s.userSpeakerName, QStringLiteral("Lilla"));

        // Az új fájlt egy régi build is olvassa: a meglévő kulcsok jelentése nem változott,
        // az újakat egyszerűen nem ismeri (itt: kivesszük őket, és minden más ugyanaz marad).
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
        // Új folyamat (itt: új példány) a lemezről is betölti.
        devicenames::setOverrides({});
        SettingsManager again(dir.path());
        QCOMPARE(again.settings().deviceNames.value(kSpeakers), QStringLiteral("Nappali hangfal"));
        QCOMPARE(devicenames::displayName(kSpeakers), QStringLiteral("Nappali hangfal"));
        // A fájlban a kulcs a többi beállítás mellett áll.
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
        // Üres futtatható út → nem ír.
        QVERIFY(!autostart::writeEntry(dir.filePath(QStringLiteral("x.desktop")), QString()));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("x.desktop"))));
    }

    void autostartIsNeverTouchedInASandbox()
    {
        // TANARA_HOME mellett (homokozó, teszt — itt végig be van állítva) a valódi
        // ~/.config/autostart bejegyzés érinthetetlen: se létrehozás, se törlés, se átírás.
        const QString real = autostart::watcherEntryPath();
        const bool existed = !real.isEmpty() && QFile::exists(real);
        const QDateTime stamp = existed ? QFileInfo(real).lastModified() : QDateTime();

        QVERIFY(!qEnvironmentVariableIsEmpty("TANARA_HOME"));
        QVERIFY(!autostart::managed());
        QVERIFY(!autostart::applyWatcher(true, QStringLiteral("/nem/letezik/tanara-watcher")));
        QVERIFY(!autostart::applyWatcher(false, QString()));

        if (!real.isEmpty()) {
            QCOMPARE(QFile::exists(real), existed);
            if (existed) QCOMPARE(QFileInfo(real).lastModified(), stamp);
        }
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
        // A build-fában: <build>/gui/tanara mellől a ../watcher/tanara-watcher.
        QCOMPARE(autostart::findWatcherExecutable(dir.filePath(QStringLiteral("gui"))),
                 QFileInfo(f).absoluteFilePath());
    }

    // ---- promptok ------------------------------------------------------------------------

    void promptVariablesAreTheOnesTheCodeSubstitutes()
    {
        const QVector<PromptVariable> vars = promptVariables();
        QVERIFY(!vars.isEmpty());
        for (const PromptVariable& v : vars) {
            QVERIFY(!v.description.isEmpty());
            // Amit a jelmagyarázat ígér, azt az applySummaryLanguage tényleg kicseréli…
            const QString out = applySummaryLanguage(QStringLiteral("A: ") + v.token, QStringLiteral("angol"));
            QVERIFY2(!out.contains(v.token), qPrintable(v.token));
        }
        // …és a beépített promptok csak ilyen változót használnak.
        static const QRegularExpression re(QStringLiteral("\\{\\{[^}]+\\}\\}"));
        QStringList known;
        for (const PromptVariable& v : vars) known << v.token;
        for (const QString& id : {QStringLiteral("simple"), QStringLiteral("topic"),
                                  QStringLiteral("analysis"), QStringLiteral("reduce")}) {
            auto it = re.globalMatch(promptBuiltin(id));
            while (it.hasNext()) {
                const QString token = it.next().captured();
                QVERIFY2(known.contains(token), qPrintable(id + QStringLiteral(": ") + token));
            }
        }
    }

    void outputFormats()
    {
        const PromptOutputFormat simple = promptOutputFormat(QStringLiteral("simple"));
        QCOMPARE(simple.kind, QStringLiteral("json"));
        for (const QString& key : {QStringLiteral("execSummary"), QStringLiteral("decisions"),
                                   QStringLiteral("actionItems"), QStringLiteral("participants")}) {
            QVERIFY2(simple.summary.contains(key), qPrintable(key));
            QVERIFY2(simple.body.contains(key), qPrintable(key));
            // A beépített prompt ugyanezeket a kulcsokat kéri.
            QVERIFY2(promptBuiltin(QStringLiteral("simple")).contains(key), qPrintable(key));
        }
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

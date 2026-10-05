//
// Tanara — Windows-os meeting-detektor (WindowsCaptureDetector) unit-tesztek.
//
// A tiszta szabályok (detail/WinCaptureRules.h: képnév, ön-kizárás, app-illesztés, a
// ConsentStore kulcsnév + FILETIME logika) minden platformon futnak — nincs bennük COM.
// A detektor-példány (COM / WASAPI) és a registry-szerződés csak Windowson; máshol QSKIP.
//
#include <QtTest>
#include <QObject>

#include <memory>

#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/IMeetingDetector.h"
#include "tanara/detect/detail/WinCaptureRules.h"

using namespace tanara;
namespace win = tanara::detail::win;

namespace {
const QStringList kDefaults = {
    QStringLiteral("zoom"), QStringLiteral("teams"), QStringLiteral("webex"),
    QStringLiteral("slack"), QStringLiteral("discord"), QStringLiteral("meet"),
    QStringLiteral("skype"), QStringLiteral("chromium"), QStringLiteral("firefox")};
}

class WindowsDetectorTests : public QObject {
    Q_OBJECT
private slots:

    void imageBaseNameNormalizes()
    {
        QCOMPARE(win::imageBaseName(QStringLiteral("C:\\Program Files\\Zoom\\bin\\Zoom.exe")),
                 QStringLiteral("zoom"));
        QCOMPARE(win::imageBaseName(QStringLiteral("C:/x/ms-teams.EXE")), QStringLiteral("ms-teams"));
        QCOMPARE(win::imageBaseName(QStringLiteral("Discord.exe")), QStringLiteral("discord"));
        QCOMPARE(win::imageBaseName(QStringLiteral("ffmpeg")), QStringLiteral("ffmpeg"));
        QCOMPARE(win::imageBaseName(QString()), QString());
    }

    // Ön-kizárás: a tanara, tanara-cli, tanara-watcher kiesik; idegen, hasonló nevű app nem.
    void selfExclusion()
    {
        const QString self = QStringLiteral("tanara");
        QVERIFY(win::isSelfImage(QStringLiteral("tanara"), self));
        QVERIFY(win::isSelfImage(QStringLiteral("tanara-cli"), self));
        QVERIFY(win::isSelfImage(QStringLiteral("tanara-watcher"), self));
        QVERIFY(win::isSelfImage(QStringLiteral("tanara-cli"), QStringLiteral("tanara.exe")));
        QVERIFY(!win::isSelfImage(QStringLiteral("tanarak"), self));
        QVERIFY(!win::isSelfImage(QStringLiteral("zoom"), self));
        QVERIFY(!win::isSelfImage(QStringLiteral("tanara"), QString()));
    }

    void callAppMapping_data()
    {
        QTest::addColumn<QString>("image");
        QTest::addColumn<bool>("matched");
        QTest::addColumn<QString>("appId");
        QTest::addColumn<QString>("appName");
        QTest::newRow("zoom")      << "zoom"            << true  << "zoom"    << "Zoom";
        QTest::newRow("new teams") << "ms-teams"        << true  << "teams"   << "Microsoft Teams";
        QTest::newRow("old teams") << "teams"           << true  << "teams"   << "Microsoft Teams";
        QTest::newRow("webex")     << "ciscocollabhost" << true  << "webex"   << "Webex";
        QTest::newRow("webex mta") << "webexmta"        << true  << "webex"   << "Webex";
        QTest::newRow("discord")   << "discord"         << true  << "discord" << "Discord";
        QTest::newRow("slack")     << "slack"           << true  << "slack"   << "Slack";
        QTest::newRow("skype")     << "skype"           << true  << "skype"   << "Skype";
        // Böngészők: a "meet" lista-elemen keresztül (Google Meet böngészőben).
        QTest::newRow("chrome")    << "chrome"          << true  << "chrome"  << "Chrome";
        QTest::newRow("edge")      << "msedge"          << true  << "msedge"  << "Microsoft Edge";
        QTest::newRow("firefox")   << "firefox"         << true  << "firefox" << "Firefox";
        QTest::newRow("brave")     << "brave"           << true  << "brave"   << "Brave";
        // Nem hívás-app (a default listával).
        QTest::newRow("ffmpeg")    << "ffmpeg"          << false << ""        << "";
        QTest::newRow("audacity")  << "audacity"        << false << ""        << "";
        QTest::newRow("telegram")  << "telegram"        << false << ""        << "";
    }
    void callAppMapping()
    {
        QFETCH(QString, image);
        QFETCH(bool, matched);
        QFETCH(QString, appId);
        QFETCH(QString, appName);
        const win::CallAppMatch m = win::matchCallApp(image, kDefaults);
        QCOMPARE(m.matched, matched);
        QCOMPARE(m.appId, appId);
        QCOMPARE(m.appName, appName);
    }

    // A felhasználó (vagy a teszt) felvehet új appot; az ismeretlen app a képnevét kapja.
    void userAddedApps()
    {
        QStringList apps = kDefaults;
        apps << QStringLiteral("ffmpeg") << QStringLiteral("Telegram");
        QCOMPARE(win::matchCallApp(QStringLiteral("ffmpeg"), apps).appId, QStringLiteral("ffmpeg"));
        QCOMPARE(win::matchCallApp(QStringLiteral("telegram"), apps).appName, QStringLiteral("Telegram"));
        // "meet" nélkül a böngésző nem számít hívásnak.
        QVERIFY(!win::matchCallApp(QStringLiteral("chrome"), {QStringLiteral("zoom")}).matched);
        QVERIFY(!win::matchCallApp(QString(), apps).matched);
    }

    void consentKeyNames()
    {
        QCOMPARE(win::consentKeyToName(QStringLiteral("MSTeams_8wekyb3d8bbwe"), false),
                 QStringLiteral("msteams"));
        QCOMPARE(win::consentKeyToName(QStringLiteral("5319275A.WhatsAppDesktop_cv1g1gvanyjgm"), false),
                 QStringLiteral("5319275a.whatsappdesktop"));
        QCOMPARE(win::consentKeyToName(QStringLiteral("C:#Program Files#Zoom#bin#Zoom.exe"), true),
                 QStringLiteral("zoom"));
        QCOMPARE(win::consentKeyToName(QStringLiteral("C:#Users#a#AppData#Local#Tanara#tanara-cli.exe"), true),
                 QStringLiteral("tanara-cli"));
        // A csomagolt Teams a "teams" elemre illeszkedik.
        const win::CallAppMatch m = win::matchCallApp(QStringLiteral("msteams"), kDefaults);
        QVERIFY(m.matched);
        QCOMPARE(m.appId, QStringLiteral("teams"));
        QVERIFY(win::matchCallApp(QStringLiteral("microsoft.skypeapp"), kDefaults).matched);
    }

    void consentFiletimeLogic()
    {
        const quint64 t0 = 134030000000000000ULL;   // ~2025, 100 ns-os FILETIME
        QVERIFY(!win::consentInUse(0, 0));                 // sosem használta
        QVERIFY(win::consentInUse(t0, 0));                  // most kezdte, még nincs vége
        QVERIFY(win::consentInUse(t0 + 10, t0));            // újra kezdte az előző vég után
        QVERIFY(!win::consentInUse(t0, t0 + 10));           // befejezte
        QVERIFY(!win::consentInUse(t0, t0));                // azonos → nem aktív
    }

    void detectorRegisteredAndPolls()
    {
#if defined(Q_OS_WIN)
        registerBuiltinDetectors();
        auto& reg = MeetingDetectorRegistry::instance();
        QVERIFY(reg.has(QStringLiteral("windows-wasapi")));
        const DetectorDescriptor desc = reg.descriptor(QStringLiteral("windows-wasapi"));
        QCOMPARE(desc.platform, QStringLiteral("windows"));
        QVERIFY(desc.derivesAppName);

        std::unique_ptr<IMeetingDetector> d(reg.create(QStringLiteral("windows-wasapi")));
        QVERIFY(d != nullptr);
        QCOMPARE(d->id(), QStringLiteral("windows-wasapi"));
        QVERIFY(d->isAvailable());
        // Csak a saját magunkat ismerő lista: a saját processz SOHA nem lehet meeting.
        d->configure({QStringLiteral("test_windows_detector")}, QStringLiteral("tanara"));
        QElapsedTimer t;
        t.start();
        const MeetingSignal sig = d->poll();
        qInfo() << "poll:" << t.elapsed() << "ms, active =" << sig.active << sig.sourceRef;
        QVERIFY(!sig.active || !sig.sourceRef.contains(QStringLiteral("test_windows_detector")));
        d->poll();   // a gyorsítótárazott enumerátorral is működik
#else
        QSKIP("Csak Windowson (WASAPI).");
#endif
    }
};

QTEST_GUILESS_MAIN(WindowsDetectorTests)
#include "test_windows_detector.moc"

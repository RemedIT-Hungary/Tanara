// Tanara: unit tests of the Windows meeting detector (WindowsCaptureDetector).
//
// The pure rules (detail/WinCaptureRules.h: image name, self-exclusion, app matching, the
// ConsentStore key name + FILETIME logic) run on every platform: there is no COM in them.
// The detector instance (COM / WASAPI) and the registry contract are Windows-only; elsewhere QSKIP.
#include <QtTest>
#include <QObject>

#include <memory>

#include "tanara/watcher/detect/DetectorRegistry.h"
#include "tanara/watcher/detect/IMeetingDetector.h"
#include "tanara/watcher/detect/detail/WinCaptureRules.h"

using namespace tanara;
namespace win = tanara::watcher::detail::win;

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

    // Self-exclusion: tanara, tanara-cli, tanara-watcher drop out; a foreign app with a similar name does not.
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
        // Browsers: through the "meet" list item (Google Meet in a browser).
        QTest::newRow("chrome")    << "chrome"          << true  << "chrome"  << "Chrome";
        QTest::newRow("edge")      << "msedge"          << true  << "msedge"  << "Microsoft Edge";
        QTest::newRow("firefox")   << "firefox"         << true  << "firefox" << "Firefox";
        QTest::newRow("brave")     << "brave"           << true  << "brave"   << "Brave";
        // Not a call app (with the default list).
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

    // The user (or the test) can add a new app; an unknown app gets its image name.
    void userAddedApps()
    {
        QStringList apps = kDefaults;
        apps << QStringLiteral("ffmpeg") << QStringLiteral("Telegram");
        QCOMPARE(win::matchCallApp(QStringLiteral("ffmpeg"), apps).appId, QStringLiteral("ffmpeg"));
        QCOMPARE(win::matchCallApp(QStringLiteral("telegram"), apps).appName, QStringLiteral("Telegram"));
        // Without "meet" a browser does not count as a call.
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
        // The bundled Teams matches the "teams" item.
        const win::CallAppMatch m = win::matchCallApp(QStringLiteral("msteams"), kDefaults);
        QVERIFY(m.matched);
        QCOMPARE(m.appId, QStringLiteral("teams"));
        QVERIFY(win::matchCallApp(QStringLiteral("microsoft.skypeapp"), kDefaults).matched);
    }

    void consentFiletimeLogic()
    {
        const quint64 t0 = 134030000000000000ULL;   // about 2025, 100 ns FILETIME
        QVERIFY(!win::consentInUse(0, 0));                 // never used
        QVERIFY(win::consentInUse(t0, 0));                  // just started, not ended yet
        QVERIFY(win::consentInUse(t0 + 10, t0));            // started again after the previous end
        QVERIFY(!win::consentInUse(t0, t0 + 10));           // ended
        QVERIFY(!win::consentInUse(t0, t0));                // equal: not active
    }

    void detectorRegisteredAndPolls()
    {
#if defined(Q_OS_WIN)
        registerBuiltinDetectors();
        auto& reg = watcher::MeetingDetectorRegistry::instance();
        QVERIFY(reg.has(QStringLiteral("windows-wasapi")));
        const watcher::DetectorDescriptor desc = reg.descriptor(QStringLiteral("windows-wasapi"));
        QCOMPARE(desc.platform, QStringLiteral("windows"));
        QVERIFY(desc.derivesAppName);

        std::unique_ptr<watcher::IMeetingDetector> d(reg.create(QStringLiteral("windows-wasapi")));
        QVERIFY(d != nullptr);
        QCOMPARE(d->id(), QStringLiteral("windows-wasapi"));
        QVERIFY(d->isAvailable());
        // A list that knows only ourselves: our own process can NEVER be a meeting.
        d->configure({QStringLiteral("test_windows_detector")}, QStringLiteral("tanara"));
        QElapsedTimer t;
        t.start();
        const watcher::MeetingSignal sig = d->poll();
        qInfo() << "poll:" << t.elapsed() << "ms, active =" << sig.active << sig.sourceRef;
        QVERIFY(!sig.active || !sig.sourceRef.contains(QStringLiteral("test_windows_detector")));
        d->poll();   // works with the cached enumerator too
#else
        QSKIP("Csak Windowson (WASAPI).");
#endif
    }
};

QTEST_GUILESS_MAIN(WindowsDetectorTests)
#include "test_windows_detector.moc"

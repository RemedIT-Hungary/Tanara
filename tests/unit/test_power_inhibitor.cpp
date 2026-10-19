// Tanara: platform::IPowerInhibitor (the sleep inhibitor held while recording).
//  - the platform implementation does not crash, release() is idempotent, held() follows
//    acquire / release where the backend is available (QSKIP where it is not, e.g. a CI container
//    without logind or a portal). The hold lasts milliseconds and is always released.
//  - the recording.keepAwake setting: default on, JSON round trip.
#include <QtTest>

#include "tanara/foundation/platform/PowerInhibitor.h"
#include "tanara/recording/RecordingSettings.h"

using namespace tanara;

class PowerInhibitorTest : public QObject {
    Q_OBJECT
private slots:
    void platformAcquireRelease()
    {
        auto inhibitor = platform::makePowerInhibitor();
        QVERIFY(inhibitor);
        QVERIFY(!inhibitor->backendName().isEmpty());
        QVERIFY(!inhibitor->held());
        inhibitor->release();   // nothing held: a no-op
        QVERIFY(!inhibitor->held());
        if (inhibitor->backendName() == QLatin1String("none"))
            QSKIP("no sleep-inhibitor backend on this platform / build");
        const bool ok = inhibitor->acquire(QStringLiteral("Tanara test (released at once)"));
        if (!ok) {
            QVERIFY(!inhibitor->held());
            QSKIP(qPrintable(QStringLiteral("backend %1 not reachable here").arg(inhibitor->backendName())));
        }
        QVERIFY(inhibitor->held());
        QVERIFY(inhibitor->acquire(QStringLiteral("again")));   // idempotent: keeps the hold
        QVERIFY(inhibitor->held());
        inhibitor->release();
        QVERIFY(!inhibitor->held());
        inhibitor->release();
        QVERIFY(!inhibitor->held());
    }

    void destructorReleases()
    {
        auto inhibitor = platform::makePowerInhibitor();
        if (!inhibitor->acquire(QStringLiteral("Tanara test (released by the destructor)")))
            QSKIP("no reachable sleep-inhibitor backend");
        inhibitor.reset();   // must not leave the machine unable to sleep
    }

    void keepAwakeSetting()
    {
        QVERIFY(recording::RecordingSettings{}.keepAwake);
        QVERIFY(recording::RecordingSettings::fromJson({}).keepAwake);
        recording::RecordingSettings s;
        s.keepAwake = false;
        QVERIFY(!recording::RecordingSettings::fromJson(s.toJson()).keepAwake);
        s.keepAwake = true;
        QVERIFY(recording::RecordingSettings::fromJson(s.toJson()).keepAwake);
    }
};

QTEST_GUILESS_MAIN(PowerInhibitorTest)
#include "test_power_inhibitor.moc"

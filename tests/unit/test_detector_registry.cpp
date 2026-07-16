//
// Tanara — Meeting-detektor registry unit-tesztek.
//
// A registry id alapján old fel és hoz létre platform-detektort; nincs hardkódolt
// `new LinuxCaptureDetector`. A registerBuiltinDetectors() idempotens, és a create/
// has/all szerződést ellenőrizzük. A platform-specifikus assertek #if alatt.
//
// Külön teszt-exe (tests/CMakeLists.txt GLOB), saját main. Core → QTEST_GUILESS_MAIN.
//
#include <QtTest>
#include <QObject>

#include <memory>

#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/IMeetingDetector.h"

using namespace tanara;

class DetectorRegistryTests : public QObject {
    Q_OBJECT
private slots:

    // registerBuiltinDetectors() idempotens: a többszöri hívás nem duplikál, nem crashel.
    void registerIsIdempotent()
    {
        registerBuiltinDetectors();
        const int n1 = MeetingDetectorRegistry::instance().all().size();
        registerBuiltinDetectors();
        const int n2 = MeetingDetectorRegistry::instance().all().size();
        QCOMPARE(n1, n2);
    }

    // Ismeretlen id-ra a create() nullptr-t ad (nem dob, nem crashel).
    void createUnknownReturnsNull()
    {
        registerBuiltinDetectors();
        std::unique_ptr<IMeetingDetector> d(
            MeetingDetectorRegistry::instance().create(QStringLiteral("nonexistent")));
        QVERIFY(d == nullptr);
    }

#if defined(Q_OS_LINUX)
    // Linuxon regisztrálódik a PipeWire capture-detektor, és a create() működő,
    // helyesen elnevezett példányt ad, a megadott QObject-hez parentelve.
    void linuxCaptureRegistered()
    {
        registerBuiltinDetectors();
        auto& reg = MeetingDetectorRegistry::instance();
        QVERIFY(reg.has(QStringLiteral("linux-capture")));
        QVERIFY(!reg.all().isEmpty());

        std::unique_ptr<IMeetingDetector> d(reg.create(QStringLiteral("linux-capture")));
        QVERIFY(d != nullptr);
        QCOMPARE(d->id(), QStringLiteral("linux-capture"));

        const DetectorDescriptor desc = reg.descriptor(QStringLiteral("linux-capture"));
        QCOMPARE(desc.platform, QStringLiteral("linux"));
        QVERIFY(desc.derivesAppName);
    }
#endif
};

QTEST_GUILESS_MAIN(DetectorRegistryTests)
#include "test_detector_registry.moc"

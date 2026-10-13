// Tanara: unit tests of the meeting detector registry.
//
// The registry resolves and creates a platform detector by id; there is no hard-coded
// `new LinuxCaptureDetector`. registerBuiltinDetectors() is idempotent, and we check the
// create/has/all contract. Platform-specific asserts are under #if.
//
// A separate test exe (tests/CMakeLists.txt GLOB), its own main. Core: QTEST_GUILESS_MAIN.
#include <QtTest>
#include <QObject>

#include <memory>

#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/IMeetingDetector.h"

using namespace tanara;

class DetectorRegistryTests : public QObject {
    Q_OBJECT
private slots:

    // registerBuiltinDetectors() is idempotent: repeated calls do not duplicate or crash.
    void registerIsIdempotent()
    {
        registerBuiltinDetectors();
        const int n1 = MeetingDetectorRegistry::instance().all().size();
        registerBuiltinDetectors();
        const int n2 = MeetingDetectorRegistry::instance().all().size();
        QCOMPARE(n1, n2);
    }

    // For an unknown id create() returns nullptr (does not throw or crash).
    void createUnknownReturnsNull()
    {
        registerBuiltinDetectors();
        std::unique_ptr<IMeetingDetector> d(
            MeetingDetectorRegistry::instance().create(QStringLiteral("nonexistent")));
        QVERIFY(d == nullptr);
    }

#if defined(Q_OS_LINUX)
    // On Linux the PipeWire capture detector is registered, and create() gives a working,
    // correctly named instance, parented to the given QObject.
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

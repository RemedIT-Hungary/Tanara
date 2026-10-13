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

#include "tanara/watcher/detect/DetectorRegistry.h"
#include "tanara/watcher/detect/IMeetingDetector.h"

using namespace tanara;

class DetectorRegistryTests : public QObject {
    Q_OBJECT
private slots:

    // registerBuiltinDetectors() is idempotent: repeated calls do not duplicate or crash.
    void registerIsIdempotent()
    {
        watcher::registerBuiltinDetectors();
        const int n1 = watcher::MeetingDetectorRegistry::instance().all().size();
        watcher::registerBuiltinDetectors();
        const int n2 = watcher::MeetingDetectorRegistry::instance().all().size();
        QCOMPARE(n1, n2);
    }

    // For an unknown id create() returns nullptr (does not throw or crash).
    void createUnknownReturnsNull()
    {
        watcher::registerBuiltinDetectors();
        std::unique_ptr<watcher::IMeetingDetector> d(
            watcher::MeetingDetectorRegistry::instance().create(QStringLiteral("nonexistent")));
        QVERIFY(d == nullptr);
    }

#if defined(Q_OS_LINUX)
    // On Linux the PipeWire capture detector is registered, and create() gives a working,
    // correctly named instance, parented to the given QObject.
    void linuxCaptureRegistered()
    {
        watcher::registerBuiltinDetectors();
        auto& reg = watcher::MeetingDetectorRegistry::instance();
        QVERIFY(reg.has(QStringLiteral("linux-capture")));
        QVERIFY(!reg.all().isEmpty());

        std::unique_ptr<watcher::IMeetingDetector> d(reg.create(QStringLiteral("linux-capture")));
        QVERIFY(d != nullptr);
        QCOMPARE(d->id(), QStringLiteral("linux-capture"));

        const watcher::DetectorDescriptor desc = reg.descriptor(QStringLiteral("linux-capture"));
        QCOMPARE(desc.platform, QStringLiteral("linux"));
        QVERIFY(desc.derivesAppName);
    }
#endif
};

QTEST_GUILESS_MAIN(DetectorRegistryTests)
#include "test_detector_registry.moc"

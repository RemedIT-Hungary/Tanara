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
        watcher::MeetingDetectorRegistry reg;
        watcher::registerBuiltinDetectors(reg);
        const int n1 = reg.all().size();
        watcher::registerBuiltinDetectors(reg);
        const int n2 = reg.all().size();
        QCOMPARE(n1, n2);
        QCOMPARE(watcher::MeetingDetectorRegistry::withBuiltins().all().size(), n1);
    }

    // Instances, not a singleton: each registry has only what was registered into it.
    void registriesAreIndependent()
    {
        watcher::MeetingDetectorRegistry empty;
        QVERIFY(empty.all().isEmpty());
        QVERIFY(empty.createBest() == nullptr);

        watcher::MeetingDetectorRegistry own;
        watcher::DetectorDescriptor desc;
        desc.id = QStringLiteral("test-only");
        own.registerDetector(desc, [] { return static_cast<watcher::IMeetingDetector*>(nullptr); });
        QVERIFY(own.has(QStringLiteral("test-only")));
        QVERIFY(!empty.has(QStringLiteral("test-only")));
        QVERIFY(!watcher::MeetingDetectorRegistry::withBuiltins().has(QStringLiteral("test-only")));
    }

    // For an unknown id create() returns nullptr (does not throw or crash).
    void createUnknownReturnsNull()
    {
        const watcher::MeetingDetectorRegistry reg = watcher::MeetingDetectorRegistry::withBuiltins();
        std::unique_ptr<watcher::IMeetingDetector> d(reg.create(QStringLiteral("nonexistent")));
        QVERIFY(d == nullptr);
    }

#if defined(Q_OS_LINUX)
    // On Linux the PipeWire capture detector is registered, and create() gives a working,
    // correctly named instance, parented to the given QObject.
    void linuxCaptureRegistered()
    {
        const watcher::MeetingDetectorRegistry reg = watcher::MeetingDetectorRegistry::withBuiltins();
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

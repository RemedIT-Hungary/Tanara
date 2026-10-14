// Tanara: watcher::CallMonitor with a fake detector (no pw-dump / WASAPI). The call-end signal
// comes only after a call seen ACTIVE turns inactive in 2 consecutive polls; it re-arms when a
// call becomes active again; the poll gate and stop() silence it; no detector = not started.
#include <QtTest>
#include <QSignalSpy>

#include <memory>

#include "tanara/watcher/CallMonitor.h"
#include "tanara/watcher/detect/IMeetingDetector.h"

using namespace tanara;

namespace {

// Scripted detector: poll() returns `active` (shared with the test), counts the polls and remembers
// the configuration.
struct Script {
    bool active = false;
    QString appName = QStringLiteral("Zoom");
    int polls = 0;
    QStringList knownApps;
    QString selfBinary;
    QString createdWithId;
};

class FakeDetector final : public watcher::IMeetingDetector {
public:
    explicit FakeDetector(std::shared_ptr<Script> s) : m_s(std::move(s)) {}
    QString id() const override { return QStringLiteral("fake"); }
    watcher::MeetingSignal poll() override
    {
        ++m_s->polls;
        watcher::MeetingSignal sig;
        sig.active = m_s->active;
        if (sig.active) sig.appName = m_s->appName;
        return sig;
    }
    bool isAvailable() const override { return true; }
    void configure(const QStringList& apps, const QString& self) override
    {
        m_s->knownApps = apps;
        m_s->selfBinary = self;
    }

private:
    std::shared_ptr<Script> m_s;
};

watcher::WatcherSettings settings()
{
    watcher::WatcherSettings w;
    w.detectorIntervalSec = 3600;   // the test drives poll() by hand
    w.detectorId = QStringLiteral("fake");
    return w;
}

} // namespace

class CallMonitorTest : public QObject {
    Q_OBJECT
private slots:
    void endsAfterTwoInactivePolls();
    void noSignalWithoutActiveCall();
    void rearmsAfterNewCall();
    void gateAndStopSilence();
    void noDetectorDoesNotStart();
};

void CallMonitorTest::endsAfterTwoInactivePolls()
{
    auto script = std::make_shared<Script>();
    watcher::CallMonitor mon;
    mon.setDetectorFactory([script](const QString& id) {
        script->createdWithId = id;
        return std::make_unique<FakeDetector>(script);
    });
    QSignalSpy ended(&mon, &watcher::CallMonitor::callEnded);

    script->active = true;
    QVERIFY(mon.start(settings()));
    QVERIFY(mon.active());
    QCOMPARE(script->polls, 1);                     // start() polls once at once
    QCOMPARE(script->createdWithId, QStringLiteral("fake"));
    QCOMPARE(script->selfBinary, QStringLiteral("tanara"));   // our own capture never counts
    QCOMPARE(script->knownApps, settings().knownCallApps);

    script->active = false;
    mon.poll();
    QCOMPARE(ended.size(), 0);                      // one inactive poll is only a blip
    mon.poll();
    QCOMPARE(ended.size(), 1);
    QCOMPARE(ended.at(0).at(0).toString(), QStringLiteral("Zoom"));
    mon.poll();
    mon.poll();
    QCOMPARE(ended.size(), 1);                      // asked once per call
}

void CallMonitorTest::noSignalWithoutActiveCall()
{
    auto script = std::make_shared<Script>();
    watcher::CallMonitor mon;
    mon.setDetectorFactory([script](const QString&) { return std::make_unique<FakeDetector>(script); });
    QSignalSpy ended(&mon, &watcher::CallMonitor::callEnded);
    QVERIFY(mon.start(settings()));
    for (int i = 0; i < 5; ++i) mon.poll();
    QCOMPARE(ended.size(), 0);   // a manual recording without a call: nothing to ask

    // A blip (active, inactive, active) does not end the call either.
    script->active = true;
    mon.poll();
    script->active = false;
    mon.poll();
    script->active = true;
    mon.poll();
    script->active = false;
    mon.poll();
    QCOMPARE(ended.size(), 0);
}

void CallMonitorTest::rearmsAfterNewCall()
{
    auto script = std::make_shared<Script>();
    watcher::CallMonitor mon;
    mon.setDetectorFactory([script](const QString&) { return std::make_unique<FakeDetector>(script); });
    QSignalSpy ended(&mon, &watcher::CallMonitor::callEnded);
    QVERIFY(mon.start(settings()));
    script->active = true;
    mon.poll();
    script->active = false;
    mon.poll();
    mon.poll();
    QCOMPARE(ended.size(), 1);
    script->active = true;
    script->appName = QStringLiteral("Teams");
    mon.poll();
    script->active = false;
    mon.poll();
    mon.poll();
    QCOMPARE(ended.size(), 2);
    QCOMPARE(ended.at(1).at(0).toString(), QStringLiteral("Teams"));
}

void CallMonitorTest::gateAndStopSilence()
{
    auto script = std::make_shared<Script>();
    watcher::CallMonitor mon;
    mon.setDetectorFactory([script](const QString&) { return std::make_unique<FakeDetector>(script); });
    bool open = true;
    mon.setPollGate([&open] { return open; });
    QSignalSpy ended(&mon, &watcher::CallMonitor::callEnded);
    script->active = true;
    QVERIFY(mon.start(settings()));
    script->active = false;
    open = false;                  // e.g. the recording is stopping
    const int before = script->polls;
    mon.poll();
    mon.poll();
    QCOMPARE(script->polls, before);   // the detector is not even asked
    QCOMPARE(ended.size(), 0);
    open = true;
    mon.stop();
    QVERIFY(!mon.active());
    mon.poll();
    mon.poll();
    QCOMPARE(ended.size(), 0);
}

void CallMonitorTest::noDetectorDoesNotStart()
{
    watcher::CallMonitor mon;
    mon.setDetectorFactory([](const QString&) { return std::unique_ptr<watcher::IMeetingDetector>(); });
    QVERIFY(!mon.start(settings()));
    QVERIFY(!mon.active());
    mon.poll();   // harmless
}

QTEST_GUILESS_MAIN(CallMonitorTest)
#include "test_call_monitor.moc"

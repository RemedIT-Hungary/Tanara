// Tanara: recording::RecordingService on its own (no AppController): fake capture engine, fake
// media tool (encoders write small files), simulated device enumeration, a fake call detector and
// an injected clock for the silence watch. No real audio device, no ffmpeg.
//  - start / stop: state signals, the last used devices persist, the meeting is saved with the
//    recording's tags, readyForProcessing comes after finished + Idle;
//  - refusals: no capturable device, already recording;
//  - device loss (two absences close the track) and return (two sightings record it again);
//  - switching a device off / on during the recording;
//  - the silence question with the injected clock (re-arms when sound returns), also while the call
//    monitor runs;
//  - the call-end question relayed while recording, silent otherwise.
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <memory>

#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/recording/RecordingService.h"
#include "tanara/recording/audio/DeviceManager.h"
#include "tanara/watcher/CallMonitor.h"
#include "tanara/watcher/detect/IMeetingDetector.h"

#include "fake_audio_engine.h"
#include "fake_media_tool.h"

using namespace tanara;

namespace {

AudioDeviceInfo dev(const QString& name, TrackKind kind)
{
    AudioDeviceInfo d;
    d.id = name;
    d.name = name;
    d.kind = kind;
    d.channels = 1;
    return d;
}

struct CallScript { std::atomic<bool> active{false}; std::atomic<int> polls{0}; };

class ScriptedDetector final : public watcher::IMeetingDetector {
public:
    explicit ScriptedDetector(std::shared_ptr<CallScript> s) : m_s(std::move(s)) {}
    QString id() const override { return QStringLiteral("scripted"); }
    watcher::MeetingSignal poll() override
    {
        ++m_s->polls;
        watcher::MeetingSignal sig;
        sig.active = m_s->active;
        if (sig.active) sig.appName = QStringLiteral("Zoom");
        return sig;
    }
    bool isAvailable() const override { return true; }

private:
    std::shared_ptr<CallScript> m_s;
};

// One recording service with its fakes, in a temp folder.
struct Rig {
    QTemporaryDir dir;
    std::shared_ptr<testing::FakeMediaTool> media = std::make_shared<testing::FakeMediaTool>();
    std::unique_ptr<library::MeetingRepository> store;
    std::unique_ptr<library::TagService> tags;
    std::unique_ptr<watcher::CallMonitor> calls;
    std::shared_ptr<CallScript> call = std::make_shared<CallScript>();
    std::unique_ptr<recording::RecordingService> svc;
    recording::RecordingService::Config config;
    std::atomic<qint64> nowMs{1000000};
    FakeEngine* engine = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
    QTimer feeder;

    explicit Rig(int silenceMinutes = 0, bool askOnCallEnd = false)
    {
        store = std::make_unique<library::MeetingRepository>(dir.filePath(QStringLiteral("rec")),
                                                             dir.filePath(QStringLiteral("meta")));
        tags = std::make_unique<library::TagService>(store.get(), dir.filePath(QStringLiteral("tags.json")));
        calls = std::make_unique<watcher::CallMonitor>();
        calls->setDetectorFactory([s = call](const QString&) { return std::make_unique<ScriptedDetector>(s); });
        config.recording.userSpeakerName = QStringLiteral("Teszt Elek");
        config.watcher.silenceAskMinutes = silenceMinutes;
        config.watcher.askStopOnCallEnd = askOnCallEnd;
        config.watcher.detectorIntervalSec = 3600;   // the test polls by hand

        recording::RecordingService::Deps deps;
        deps.store = store.get();
        deps.tags = tags.get();
        deps.media = media;
        deps.callMonitor = calls.get();
        deps.audioDir = dir.filePath(QStringLiteral("rec"));
        deps.statePath = dir.filePath(QStringLiteral("state.json"));
        deps.config = [this] { return config; };
        svc = std::make_unique<recording::RecordingService>(deps);
        svc->setClock([this] { return nowMs.load(); });
        svc->setSilenceCheckIntervalMs(20);
        svc->setEngineFactory([this] {
            auto e = std::make_unique<FakeEngine>();
            engine = e.get();
            alive = engine->alive;
            return e;
        });
        feeder.setInterval(5);
        QObject::connect(&feeder, &QTimer::timeout, [this] { if (engine && alive && *alive) engine->feed(); });
    }
    ~Rig()
    {
        feeder.stop();
        svc.reset();
    }
    void setDevices(const QVector<AudioDeviceInfo>& list)
    {
        svc->devices()->setDeviceListOverride(list);
        svc->devices()->refresh();
    }
};

QStringList persistedDevices(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList out;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).object().value("lastDevices").toArray())
        out << v.toString();
    return out;
}

const AudioDeviceInfo kMic = dev(QStringLiteral("Trust USB mikrofon"), TrackKind::Mic);
const AudioDeviceInfo kHeadset = dev(QStringLiteral("Sennheiser - Füles"), TrackKind::Mic);
const AudioDeviceInfo kMonitor = dev(QStringLiteral("Monitor of Kanto"), TrackKind::Loopback);

} // namespace

class RecordingServiceTest : public QObject {
    Q_OBJECT
private slots:
    void startStopSavesMeeting();
    void refusals();
    void deviceLossAndReturn();
    void switchDeviceOffAndOn();
    void silenceQuestionWithInjectedClock();
    void callEndRelayedOnlyWhileRecording();
};

void RecordingServiceTest::startStopSavesMeeting()
{
    Rig rig;
    QVERIFY(rig.dir.isValid());
    rig.setDevices({kMic, kMonitor});
    QSignalSpy states(rig.svc.get(), &recording::RecordingService::stateChanged);
    QSignalSpy finished(rig.svc.get(), &recording::RecordingService::finished);
    QSignalSpy ready(rig.svc.get(), &recording::RecordingService::readyForProcessing);
    QSignalSpy errors(rig.svc.get(), &recording::RecordingService::error);
    QStringList order;
    connect(rig.svc.get(), &recording::RecordingService::finished, this, [&order] { order << "finished"; });
    connect(rig.svc.get(), &recording::RecordingService::stateChanged, this,
            [&order](RecordingState s) { if (s == RecordingState::Idle) order << "idle"; });
    connect(rig.svc.get(), &recording::RecordingService::readyForProcessing, this, [&order] { order << "ready"; });

    const Tag tag = rig.tags->create(QStringLiteral("Heti egyeztetés"));
    rig.svc->start(QStringLiteral("Indul"), {});   // empty = the auto-record set
    QCOMPARE(rig.svc->state(), RecordingState::Recording);
    QVERIFY(rig.engine);
    QCOMPARE(rig.svc->liveDeviceNames(), (QStringList{kMic.name, kMonitor.name}));
    QVERIFY(rig.svc->disconnectedDeviceNames().isEmpty());
    QVERIFY(!rig.svc->currentFolder().isEmpty());
    QCOMPARE(rig.svc->lastUsedDeviceNames(), (QStringList{kMic.name, kMonitor.name}));
    QCOMPARE(persistedDevices(rig.dir.filePath("state.json")), (QStringList{kMic.name, kMonitor.name}));
    rig.svc->setTags({tag.id, tag.id, QString(), QStringLiteral("no-such-tag")});
    QCOMPARE(rig.svc->tags(), (QStringList{tag.id, QStringLiteral("no-such-tag")}));

    rig.feeder.start();
    QTest::qWait(300);
    rig.svc->stop();
    QVERIFY(finished.wait(10000));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.at(0).at(0).toString()));
    QCOMPARE(rig.svc->state(), RecordingState::Idle);
    QVERIFY(rig.svc->liveDeviceNames().isEmpty());
    QVERIFY(rig.svc->tags().isEmpty());   // not inherited by the next recording
    QCOMPARE(states.first().at(0).value<RecordingState>(), RecordingState::Recording);
    QCOMPARE(states.last().at(0).value<RecordingState>(), RecordingState::Idle);
    QCOMPARE(ready.size(), 1);
    QCOMPARE(order.mid(order.indexOf("finished")), (QStringList{"finished", "idle", "ready"}));

    const Meeting m = finished.at(0).at(0).value<Meeting>();
    QCOMPARE(m.title, QStringLiteral("Indul"));
    QCOMPARE(m.tracks.size(), 2);
    QCOMPARE(m.tagIds, QStringList{tag.id});   // only the known tag
    QCOMPARE(rig.svc->currentFolder(), m.folder);
    const Meeting stored = rig.store->load(m.id);
    QCOMPARE(stored.id, m.id);
    QCOMPARE(stored.tagIds, QStringList{tag.id});

    // A fresh service reads the persisted device names back.
    recording::RecordingService::Deps deps;
    deps.statePath = rig.dir.filePath("state.json");
    recording::RecordingService again(deps);
    QCOMPARE(again.lastUsedDeviceNames(), (QStringList{kMic.name, kMonitor.name}));
}

void RecordingServiceTest::refusals()
{
    Rig rig;
    QSignalSpy errors(rig.svc.get(), &recording::RecordingService::error);
    rig.setDevices({});
    rig.svc->start(QStringLiteral("Semmi"), {});
    QCOMPARE(errors.size(), 1);
    QCOMPARE(rig.svc->state(), RecordingState::Idle);
    QVERIFY(!rig.svc->addDevice(kMic));          // nothing is recording
    QVERIFY(!rig.svc->stopDevice(kMic.name));

    rig.setDevices({kMic});
    rig.svc->start(QStringLiteral("Egy"), {kMic});
    QCOMPARE(rig.svc->state(), RecordingState::Recording);
    rig.svc->start(QStringLiteral("Kettő"), {kMic});   // already recording
    QCOMPARE(errors.size(), 2);
    QVERIFY(!rig.svc->stopDevice(kMic.name));     // the last open track stays
    QSignalSpy finished(rig.svc.get(), &recording::RecordingService::finished);
    rig.feeder.start();
    rig.svc->stop();
    QVERIFY(finished.wait(10000));
}

void RecordingServiceTest::deviceLossAndReturn()
{
    Rig rig;
    rig.setDevices({kMic, kHeadset, kMonitor});
    QSignalSpy closed(rig.svc.get(), &recording::RecordingService::trackClosed);
    QSignalSpy added(rig.svc.get(), &recording::RecordingService::trackAdded);
    QSignalSpy returned(rig.svc.get(), &recording::RecordingService::deviceReturned);
    QSignalSpy finished(rig.svc.get(), &recording::RecordingService::finished);

    rig.svc->start(QStringLiteral("Kihúzás"), {kMic, kHeadset, kMonitor});
    QCOMPARE(rig.svc->state(), RecordingState::Recording);
    rig.feeder.start();
    QTest::qWait(200);

    // Unplugged: one absence is only a blip; the timed re-enumeration closes the track.
    rig.engine->vanish(kHeadset.name);
    rig.setDevices({kMic, kMonitor});
    QCOMPARE(closed.size(), 0);
    QVERIFY(closed.wait(4000));
    QCOMPARE(closed.at(0).at(0).toString(), kHeadset.name);
    QCOMPARE(rig.svc->state(), RecordingState::Recording);
    QCOMPARE(rig.svc->liveDeviceNames(), (QStringList{kMic.name, kMonitor.name}));
    QCOMPARE(rig.svc->disconnectedDeviceNames(), QStringList{kHeadset.name});

    // An empty enumeration (audio system unreachable) closes nothing.
    rig.setDevices({});
    rig.setDevices({});
    QCOMPARE(closed.size(), 1);

    // Plugged back: recorded again after two sightings, as a new span.
    rig.engine->revive(kHeadset.name);
    rig.setDevices({kMic, kHeadset, kMonitor});
    QCOMPARE(added.size(), 0);
    QVERIFY(added.wait(4000));
    QCOMPARE(added.at(0).at(0).toString(), kHeadset.name);
    QCOMPARE(returned.size(), 1);
    QVERIFY(rig.svc->disconnectedDeviceNames().isEmpty());
    QVERIFY(rig.svc->liveDeviceNames().contains(kHeadset.name));
    QTest::qWait(200);

    rig.svc->stop();
    QVERIFY(finished.wait(10000));
    const Meeting m = finished.at(0).at(0).value<Meeting>();
    QCOMPARE(m.tracks.size(), 4);
    QCOMPARE(m.tracks.at(3).deviceName, kHeadset.name);
    QVERIFY(m.tracks.at(3).startOffsetMs > 0);
}

void RecordingServiceTest::switchDeviceOffAndOn()
{
    Rig rig;
    rig.setDevices({kMic, kHeadset});
    QSignalSpy closed(rig.svc.get(), &recording::RecordingService::trackClosed);
    QSignalSpy added(rig.svc.get(), &recording::RecordingService::trackAdded);
    QSignalSpy finished(rig.svc.get(), &recording::RecordingService::finished);
    rig.svc->setLastUsedDeviceNames({kMic.name});
    rig.svc->start(QStringLiteral("Ki-be"), {kMic});
    rig.feeder.start();
    QTest::qWait(100);

    QVERIFY(rig.svc->addDevice(kHeadset));
    QCOMPARE(added.size(), 1);
    QCOMPARE(rig.svc->lastUsedDeviceNames(), (QStringList{kMic.name, kHeadset.name}));   // remembered
    QVERIFY(rig.svc->stopDevice(kHeadset.name));
    QCOMPARE(closed.size(), 1);
    // Switched off by the user: not "disconnected", and not recorded again by itself.
    QVERIFY(rig.svc->disconnectedDeviceNames().isEmpty());
    QCOMPARE(rig.svc->liveDeviceNames(), QStringList{kMic.name});
    rig.svc->devices()->refresh();
    rig.svc->devices()->refresh();
    QCOMPARE(added.size(), 1);
    QVERIFY(rig.svc->addDevice(kHeadset));   // re-enabled: a new span
    QCOMPARE(rig.svc->liveDeviceNames(), (QStringList{kMic.name, kHeadset.name}));

    rig.svc->stop();
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.at(0).at(0).value<Meeting>().tracks.size(), 3);
}

void RecordingServiceTest::silenceQuestionWithInjectedClock()
{
    // The call monitor runs too: the silence watch must not depend on it.
    Rig rig(/*silenceMinutes*/ 2, /*askOnCallEnd*/ true);
    rig.setDevices({kMic});
    QSignalSpy silence(rig.svc.get(), &recording::RecordingService::silenceDetected);
    QSignalSpy finished(rig.svc.get(), &recording::RecordingService::finished);
    rig.svc->start(QStringLiteral("Csend"), {kMic});
    QCOMPARE(rig.svc->state(), RecordingState::Recording);
    rig.engine->vanish(kMic.name);   // RMS 0 from now on
    rig.feeder.start();

    rig.nowMs += 119000;               // just under 2 minutes of silence
    QTest::qWait(150);
    QCOMPARE(silence.size(), 0);
    rig.nowMs += 2000;
    QVERIFY(silence.wait(2000));
    QCOMPARE(silence.at(0).at(0).toInt(), 2);
    rig.nowMs += 300000;               // asked once per silent stretch
    QTest::qWait(150);
    QCOMPARE(silence.size(), 1);

    // Sound returns: the question re-arms, and a new silent stretch asks again.
    rig.engine->revive(kMic.name);
    QTest::qWait(150);                 // a loud meter refreshes the timestamp
    rig.engine->vanish(kMic.name);
    QTest::qWait(100);
    QCOMPARE(silence.size(), 1);
    rig.nowMs += 121000;
    QVERIFY(silence.wait(2000));
    QCOMPARE(silence.size(), 2);

    rig.svc->stop();
    QVERIFY(finished.wait(10000));
    rig.nowMs += 600000;               // stopped: no more questions
    QTest::qWait(100);
    QCOMPARE(silence.size(), 2);
}

void RecordingServiceTest::callEndRelayedOnlyWhileRecording()
{
    Rig rig(/*silenceMinutes*/ 0, /*askOnCallEnd*/ true);
    rig.setDevices({kMic});
    QSignalSpy ended(rig.svc.get(), &recording::RecordingService::callEnded);
    QSignalSpy finished(rig.svc.get(), &recording::RecordingService::finished);
    rig.call->active = true;
    rig.svc->start(QStringLiteral("Hívás"), {kMic});
    QVERIFY(rig.calls->active());           // subscribed while recording
    QCOMPARE(rig.call->polls.load(), 1);    // polled at once
    rig.call->active = false;
    rig.calls->poll();
    rig.calls->poll();
    QCOMPARE(ended.size(), 1);
    QCOMPARE(ended.at(0).at(0).toString(), QStringLiteral("Zoom"));

    rig.feeder.start();
    rig.svc->stop();
    QVERIFY(finished.wait(10000));
    QVERIFY(!rig.calls->active());          // unsubscribed at the end

    // Off in the settings: no watch at all.
    rig.config.watcher.askStopOnCallEnd = false;
    QSignalSpy finished2(rig.svc.get(), &recording::RecordingService::finished);
    rig.svc->start(QStringLiteral("Hívás nélkül"), {kMic});
    QVERIFY(!rig.calls->active());
    rig.svc->stop();
    QVERIFY(finished2.wait(10000));
}

QTEST_GUILESS_MAIN(RecordingServiceTest)
#include "test_recording_service.moc"

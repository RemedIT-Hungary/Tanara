#include "tanara/recording/RecordingService.h"

#include "tanara/domain/Meeting.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/platform/PowerInhibitor.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/recording/audio/DeviceManager.h"
#include "tanara/recording/audio/DeviceMonitor.h"
#include "tanara/recording/audio/RecordingSession.h"
#include "tanara/watcher/CallMonitor.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

namespace tanara::recording {

namespace {

QStringList loadLastDevices(const QString& path) {
    if (path.isEmpty()) return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    QStringList out;
    for (const auto& v : root["lastDevices"].toArray()) out << v.toString();
    return out;
}

void saveLastDevices(const QString& path, const QStringList& names) {
    if (path.isEmpty()) return;
    QJsonArray arr;
    for (const auto& n : names) arr.append(n);
    QJsonObject root;
    root["lastDevices"] = arr;
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(root).toJson(QJsonDocument::Compact)); f.commit(); }
}

// Silence watch: an RMS at speech level counts as "loud". In line with RecordingSession's kSilencePeak.
constexpr float kLoudRms = 0.008f;
constexpr int kDefaultSilenceCheckMs = 10000;

} // namespace

struct RecordingService::Impl {
    Deps deps;
    DeviceManager* devices = nullptr;
    DeviceMonitor* monitor = nullptr;
    RecordingSession* session = nullptr;
    RecordingSession::EngineFactory engineFactory;   // test seam; empty = the real one
    RecordingState state = RecordingState::Idle;
    QString currentFolder;
    QStringList lastDevices;
    QStringList tagIds;                         // tags given during recording

    // Level monitoring + track handling during recording (for the floating recorder).
    bool        monitorWanted = false;          // the UI requested level monitoring
    bool        monitorLegacy = false;          // the start/stopLevelMonitoring (recorder) request
    QHash<QObject*, QMetaObject::Connection> monitorHolders;   // consumers of retainLevelMonitoring (Settings)
    QStringList monitorNames;                   // the monitor's current device set
    QStringList recNames;                       // the recording's tracks (index -> device name)
    QStringList recClosed;                      // devices detached mid-run
    QStringList recOff;                         // switched off by the user mid-run
    QHash<QString, int> recMissing;             // in how many consecutive enumerations it was missing
    QHash<QString, int> recReturning;           // detached device: in how many consecutive enumerations it is present again

    // Silence watch during recording (silenceAskMinutes).
    QTimer* silenceTimer = nullptr;
    qint64  lastLoudMs = 0;        // timestamp of the last "loud" RMS
    bool    silenceAsked = false;  // we already asked for this silent stretch
    std::function<qint64()> clock;
    int     silenceCheckMs = kDefaultSilenceCheckMs;

    // Keep-awake: whether the setting was off at the last start (for keepAwakeState()).
    bool keepAwakeOff = false;

    Config config() const { return deps.config ? deps.config() : Config{}; }
    qint64 now() const { return clock ? clock() : QDateTime::currentMSecsSinceEpoch(); }
    void stopSilenceWatch()
    {
        if (silenceTimer) { silenceTimer->stop(); silenceTimer->deleteLater(); silenceTimer = nullptr; }
    }
    void clearTracks()
    {
        recNames.clear();
        recClosed.clear();
        recOff.clear();
    }
};

RecordingService::RecordingService(Deps deps, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->deps = std::move(deps);
    d->devices = new DeviceManager(this);
    connect(d->devices, &DeviceManager::devicesChanged, this, &RecordingService::handleDeviceSetChange);
    d->lastDevices = loadLastDevices(d->deps.statePath);
    d->monitor = new DeviceMonitor(this);
    connect(d->monitor, &DeviceMonitor::levelPeak, this, &RecordingService::levelPeak);
    if (watcher::CallMonitor* calls = d->deps.callMonitor) {
        calls->setPollGate([this] { return d->state == RecordingState::Recording; });
        connect(calls, &watcher::CallMonitor::callEnded, this, &RecordingService::callEnded);
    }
}

RecordingService::~RecordingService()
{
    // Never leave the machine unable to sleep, whatever state the recording is in (no signal from
    // a dying object).
    if (d->deps.powerInhibitor) d->deps.powerInhibitor->release();
    // The session (a child) finalizes an unfinished recording when it is destroyed; the call monitor
    // is not ours, its watch ends here.
    if (d->deps.callMonitor) {
        d->deps.callMonitor->disconnect(this);
        d->deps.callMonitor->setPollGate({});
        d->deps.callMonitor->stop();
    }
}

DeviceManager* RecordingService::devices() const { return d->devices; }
RecordingState RecordingService::state() const { return d->state; }
QString RecordingService::currentFolder() const { return d->currentFolder; }

void RecordingService::setEngineFactory(std::function<std::unique_ptr<AudioEngine>()> factory)
{
    d->engineFactory = std::move(factory);
}

void RecordingService::setClock(std::function<qint64()> nowMs) { d->clock = std::move(nowMs); }

void RecordingService::setSilenceCheckIntervalMs(int ms) { d->silenceCheckMs = ms > 0 ? ms : kDefaultSilenceCheckMs; }

QStringList RecordingService::lastUsedDeviceNames() const { return d->lastDevices; }

void RecordingService::setLastUsedDeviceNames(const QStringList& names)
{
    d->lastDevices = names;
    saveLastDevices(d->deps.statePath, names);
}

void RecordingService::setTags(const QStringList& tagIds)
{
    d->tagIds.clear();
    for (const QString& id : tagIds)
        if (!id.isEmpty() && !d->tagIds.contains(id)) d->tagIds << id;
}

QStringList RecordingService::tags() const { return d->tagIds; }

// ---- level monitoring ------------------------------------------------------------------

void RecordingService::startLevelMonitoring()
{
    d->monitorLegacy = true;
    beginLevelMonitoring();
}

void RecordingService::stopLevelMonitoring()
{
    d->monitorLegacy = false;
    if (!d->monitorHolders.isEmpty()) return;   // another consumer (retain) still requests it
    endLevelMonitoring();
}

void RecordingService::retainLevelMonitoring(QObject* owner)
{
    if (!owner || d->monitorHolders.contains(owner)) return;
    // When the consumer goes away, its request goes too: the microphone must not stay open afterwards.
    d->monitorHolders.insert(owner, connect(owner, &QObject::destroyed, this,
                                            [this, owner] { releaseLevelMonitoring(owner); }));
    if (!(d->monitorWanted && d->monitor && d->monitor->active()))
        beginLevelMonitoring();
}

void RecordingService::releaseLevelMonitoring(QObject* owner)
{
    if (!d->monitorHolders.contains(owner)) return;
    disconnect(d->monitorHolders.take(owner));
    if (d->monitorHolders.isEmpty() && !d->monitorLegacy)
        endLevelMonitoring();
}

void RecordingService::beginLevelMonitoring()
{
    d->monitorWanted = true;
    if (d->state == RecordingState::Stopping || d->state == RecordingState::Encoding) return;
    d->devices->refresh();
    restartLevelMonitor(/*force*/ true);
}

void RecordingService::endLevelMonitoring()
{
    d->monitorWanted = false;
    d->monitorNames.clear();
    if (d->monitor) d->monitor->stop();
}

// (Re)starts the level monitor with the right device set; without force only if the set changed.
// During recording the recorded devices are left out.
void RecordingService::restartLevelMonitor(bool force)
{
    if (!d->monitor || !d->monitorWanted) return;
    const bool recording = d->state == RecordingState::Recording;
    if (d->state != RecordingState::Idle && !recording) return;
    QVector<AudioDeviceInfo> list;
    QStringList names;
    for (const AudioDeviceInfo& dev : d->devices->captureDevices()) {
        // During recording the devices on a track are owned and measured by the RecordingSession.
        if (recording && d->recNames.contains(dev.name) && !d->recClosed.contains(dev.name)
            && !d->recOff.contains(dev.name))
            continue;
        list.push_back(dev);
        names << dev.name;
    }
    if (!force && d->monitor->active() && names == d->monitorNames) return;
    QElapsedTimer perfTimer;
    perfTimer.start();
    const QStringList before = d->monitorNames;
    d->monitorNames = names;
    d->monitor->start(list);   // for an empty list it stops; the open runs on the monitor's background thread
    qCDebug(lcPerf).noquote() << QStringLiteral("szintmérő újraindítás kérve (%1 → %2 eszköz, force=%3): %4 ms a fő szálon")
                                 .arg(before.size()).arg(names.size()).arg(force).arg(perfTimer.elapsed());
}

// After a device re-enumeration: during recording, closes the track of a recorded device that
// disappeared and records a returned one again.
void RecordingService::handleDeviceSetChange()
{
    const QVector<AudioDeviceInfo> present = d->devices->captureDevices();
    if (d->state == RecordingState::Recording && d->session && !present.isEmpty()) {
        // Hot-plug during recording: the track of a recorded device missing from the enumeration TWICE in a row
        // is safely closed (a single transient absence must not close a track; and an empty list, an unreachable
        // audio system, closes nothing).
        bool recheck = false;
        const QStringList rec = d->recNames;
        for (const QString& name : rec) {
            if (d->recClosed.contains(name) || d->recOff.contains(name)) continue;
            bool found = false;
            for (const AudioDeviceInfo& dev : present)
                if (dev.name == name) { found = true; break; }
            if (found) { d->recMissing.remove(name); continue; }
            if (++d->recMissing[name] >= 2) {
                qCWarning(lcApp).noquote() << "Rögzített eszköz leválasztva, a sávja lezárul:" << name;
                d->recMissing.remove(name);
                d->session->closeTrack(name);
            } else {
                recheck = true;
            }
        }
        // A re-plugged device: one that the detach closed (not switched off by the user) and that is present again
        // in the enumeration TWICE in a row (looks stable), is recorded again by itself: a new span starts, the
        // recording so far stays untouched. The user rightly expects not to have to switch it back on by hand
        // after a short USB dropout.
        const QStringList closedNow = d->recClosed;
        for (const QString& name : closedNow) {
            const AudioDeviceInfo* dev = nullptr;
            for (const AudioDeviceInfo& cand : present)
                if (cand.name == name) { dev = &cand; break; }
            if (!dev) { d->recReturning.remove(name); continue; }
            if (++d->recReturning[name] < 2) { recheck = true; continue; }
            d->recReturning.remove(name);
            qCInfo(lcApp).noquote() << "Leválasztott eszköz visszatért, a rögzítése folytatódik (új szakasz):" << name;
            emit deviceReturned(name);
            if (!d->session->addDevice(*dev))
                qCWarning(lcApp).noquote() << "A visszatért eszköz sávja nem indult el:" << name;
        }
        if (recheck)
            QTimer::singleShot(1200, this, [this] {
                if (d->state == RecordingState::Recording) d->devices->refresh();
            });
    }
    if (d->monitorWanted && d->monitor && d->monitor->active())
        restartLevelMonitor(/*force*/ false);
}

// ---- tracks during recording ---------------------------------------------------------------

QStringList RecordingService::liveDeviceNames() const
{
    QStringList out;
    for (const QString& n : d->recNames)
        if (!d->recClosed.contains(n) && !d->recOff.contains(n) && !out.contains(n)) out << n;
    return out;
}

QStringList RecordingService::disconnectedDeviceNames() const { return d->recClosed; }

bool RecordingService::addDevice(const AudioDeviceInfo& device)
{
    if (d->state != RecordingState::Recording || !d->session) return false;
    return d->session->addDevice(device);
}

bool RecordingService::stopDevice(const QString& deviceName)
{
    if (d->state != RecordingState::Recording || !d->session) return false;
    const QStringList open = liveDeviceNames();
    if (!open.contains(deviceName) || open.size() < 2) return false;   // the last open track stays
    if (!d->recOff.contains(deviceName)) d->recOff << deviceName;
    d->session->closeTrack(deviceName);   // → trackClosed
    return true;
}

// ---- keep-awake ----------------------------------------------------------------------------

QString RecordingService::keepAwakeState() const
{
    if (d->deps.powerInhibitor && d->deps.powerInhibitor->held()) return QStringLiteral("held");
    const bool off = d->state == RecordingState::Idle ? !d->config().recording.keepAwake : d->keepAwakeOff;
    return off ? QStringLiteral("off") : QStringLiteral("unavailable");
}

QString RecordingService::powerInhibitorBackend() const
{
    return d->deps.powerInhibitor ? d->deps.powerInhibitor->backendName() : QStringLiteral("none");
}

void RecordingService::acquireKeepAwake()
{
    d->keepAwakeOff = !d->config().recording.keepAwake;
    platform::IPowerInhibitor* inhibitor = d->deps.powerInhibitor.get();
    if (d->keepAwakeOff) {
        qCInfo(lcApp) << "Keep-awake while recording: off (setting)";
    } else if (!inhibitor) {
        qCInfo(lcApp) << "Keep-awake while recording: unavailable (no inhibitor)";
    } else if (inhibitor->acquire(QStringLiteral("Recording a meeting"))) {
        qCInfo(lcApp).noquote() << "Keep-awake while recording: held via" << inhibitor->backendName();
    } else {
        qCWarning(lcApp).noquote() << "Keep-awake while recording: unavailable (backend"
                                   << inhibitor->backendName() << "did not hold)";
    }
    emit keepAwakeStateChanged();
}

void RecordingService::releaseKeepAwake()
{
    platform::IPowerInhibitor* inhibitor = d->deps.powerInhibitor.get();
    if (!inhibitor || !inhibitor->held()) return;
    inhibitor->release();
    qCInfo(lcApp) << "Keep-awake while recording: released";
    emit keepAwakeStateChanged();
}

// ---- call-end and silence watch -------------------------------------------------------------

void RecordingService::startWatches()
{
    // Call end: the same detector core the tanara-watcher uses; only while recording.
    if (watcher::CallMonitor* calls = d->deps.callMonitor) {
        calls->stop();
        const watcher::WatcherSettings w = d->config().watcher;
        if (w.askStopOnCallEnd) calls->start(w);
    }
}

void RecordingService::stopWatches()
{
    d->stopSilenceWatch();
    if (d->deps.callMonitor) d->deps.callMonitor->stop();
}

// ---- start / stop -----------------------------------------------------------------------------

void RecordingService::start(const QString& title, const QVector<AudioDeviceInfo>& devices)
{
    if (d->state == RecordingState::Recording || d->state == RecordingState::Stopping) {
        emit error(tr("Már folyik felvétel."));
        return;
    }
    // The monitor releases the devices before recording (the request, monitorWanted, stays: during recording it
    // restarts on the devices not on a track). The shutdown runs asynchronously on the monitor's background
    // thread: for a short time a device may be open in parallel with the recording (in shared mode that is no obstacle).
    d->monitorNames.clear();
    if (d->monitor) d->monitor->stop();
    d->clearTracks();
    d->recMissing.clear();
    d->recReturning.clear();
    QVector<AudioDeviceInfo> use = devices;
    if (use.isEmpty()) {
        d->devices->refresh();
        use = d->devices->autoRecordDevices();   // line-in/AUX left out of the auto set
    }
    if (use.isEmpty()) {
        emit error(tr("Nincs felvehető hangeszköz."));
        return;
    }

    // We remember the devices used (to pre-check at startup).
    QStringList usedNames;
    for (const auto& dvc : use) usedNames << dvc.name;
    setLastUsedDeviceNames(usedNames);

    d->tagIds.clear();   // the previous recording's tags are not inherited
    const Config cfg = d->config();
    auto* sess = new RecordingSession(d->deps.media, d->deps.audioDir, title, cfg.recording.userSpeakerName,
                                      opusBitrateKbps(cfg.recording.audioQuality), this);
    if (d->engineFactory) sess->setEngineFactory(d->engineFactory);
    d->session = sess;

    connect(sess, &RecordingSession::stateChanged, this, [this, sess](RecordingState st) {
        d->state = st;
        // The meeting folder already exists in start(), so it is available at the START of the recording (not
        // only at finished). Needed for the --record lock and currentFolder().
        if (st == RecordingState::Recording) {
            d->currentFolder = sess->folder();
            d->recNames.clear();
            for (const AudioDeviceInfo& dev : sess->trackDevices()) d->recNames << dev.name;
            startWatches();
            restartLevelMonitor(/*force*/ true);   // only if the UI requested it (monitorWanted)
        } else if (st == RecordingState::Stopping) {
            // During the shutdown the monitor also stops; at the recording's end (if needed) it restarts.
            d->monitorNames.clear();
            if (d->monitor) d->monitor->stop();
        } else if (st == RecordingState::Idle) {
            stopWatches();
            releaseKeepAwake();
        }
        emit stateChanged(st);
    });
    connect(sess, &RecordingSession::trackLevel, this, [this](int idx, float rms, float peak) {
        if (idx >= 0 && idx < d->recNames.size())
            emit levelPeak(d->recNames.at(idx), rms, peak);
    });
    connect(sess, &RecordingSession::trackAdded, this, [this](int, const QString& name) {
        d->recNames << name;
        d->recClosed.removeAll(name);   // a re-plugged, re-recorded device
        d->recOff.removeAll(name);      // a re-enabled device: a new span
        if (!d->lastDevices.contains(name)) {
            QStringList names = d->lastDevices;
            names << name;
            setLastUsedDeviceNames(names);
        }
        restartLevelMonitor(/*force*/ false);
        emit trackAdded(name);
    });
    connect(sess, &RecordingSession::trackClosed, this, [this](int, const QString& name) {
        // Switched off by the user, so not "detached"; the monitor measures its level again.
        if (!d->recOff.contains(name) && !d->recClosed.contains(name)) d->recClosed << name;
        restartLevelMonitor(/*force*/ false);
        emit trackClosed(name);
    });
    // Silence watch: if any track's RMS at speech level is "loud", the timestamp refreshes and the silence
    // question re-arms.
    connect(sess, &RecordingSession::levelMeterUpdated, this, [this](int, float rms) {
        if (rms >= kLoudRms) {
            d->lastLoudMs = d->now();
            d->silenceAsked = false;
        }
    });
    {
        const int minutes = cfg.watcher.silenceAskMinutes;
        d->stopSilenceWatch();
        if (minutes > 0) {
            d->lastLoudMs = d->now();
            d->silenceAsked = false;
            d->silenceTimer = new QTimer(this);
            connect(d->silenceTimer, &QTimer::timeout, this, [this, minutes]() {
                if (d->state != RecordingState::Recording || d->silenceAsked) return;
                const qint64 quiet = d->now() - d->lastLoudMs;
                if (quiet >= qint64(minutes) * 60000) {
                    d->silenceAsked = true;
                    qCInfo(lcApp) << "Csend felvétel közben:" << minutes << "perc → rákérdezés";
                    emit silenceDetected(minutes);
                }
            });
            d->silenceTimer->start(d->silenceCheckMs);
        }
    }
    connect(sess, &RecordingSession::elapsedChanged, this, &RecordingService::elapsedChanged);
    connect(sess, &RecordingSession::failed, this, [this](QString e) {
        stopWatches();
        releaseKeepAwake();
        emit error(e);
        if (d->session) { d->session->deleteLater(); d->session = nullptr; }
        d->state = RecordingState::Idle;
        d->clearTracks();
        emit stateChanged(d->state);
        restartLevelMonitor(/*force*/ true);
    });
    connect(sess, &RecordingSession::finished, this, [this](Meeting m) {
        stopWatches();
        releaseKeepAwake();
        d->currentFolder = m.folder;
        // The tags given during recording (the meeting enters the store only now).
        for (const QString& id : std::as_const(d->tagIds))
            if (d->deps.tags && d->deps.tags->tag(id).isValid() && !m.tagIds.contains(id)) m.tagIds << id;
        d->tagIds.clear();
        if (d->deps.store) d->deps.store->saveMeeting(m);
        if (d->session) { d->session->deleteLater(); d->session = nullptr; }
        d->state = RecordingState::Idle;
        d->clearTracks();
        emit finished(m);
        emit stateChanged(d->state);
        restartLevelMonitor(/*force*/ true);
        emit readyForProcessing(m);
    });

    // Held before the capture opens, so the recording is protected from its first second; a start
    // that fails releases it again (the failed handler above).
    acquireKeepAwake();
    sess->start(use);
}

void RecordingService::stop()
{
    if (d->session) d->session->stop();
}

} // namespace tanara::recording

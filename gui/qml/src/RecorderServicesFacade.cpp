#include "RecorderServicesFacade.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/foundation/ProcessLock.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/recording/RecordingService.h"
#include "tanara/recording/audio/DeviceManager.h"
#include "tanara/watcher/PlaybackRouting.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>

using namespace tanara;

namespace tanara_qml {

RecorderServicesFacade::RecorderServicesFacade(const Deps& deps, QObject* parent)
    : IRecorderFacade(parent), m_deps(deps), m_tagController(deps.tagController)
{
    recording::RecordingService* rec = m_deps.recording;
    connect(rec->devices(), &recording::DeviceManager::devicesChanged, this, &IRecorderFacade::devicesChanged);
    connect(rec, &recording::RecordingService::levelPeak, this, &IRecorderFacade::deviceLevel);
    connect(rec, &recording::RecordingService::stateChanged, this, &IRecorderFacade::stateChanged);
    connect(rec, &recording::RecordingService::elapsedChanged, this, &IRecorderFacade::elapsedChanged);
    connect(rec, &recording::RecordingService::trackAdded, this, &IRecorderFacade::trackAdded);
    connect(rec, &recording::RecordingService::trackClosed, this, &IRecorderFacade::trackClosed);
    connect(rec, &recording::RecordingService::deviceReturned, this, &IRecorderFacade::deviceReturned);
    connect(rec, &recording::RecordingService::error, this, &IRecorderFacade::error);
    connect(rec, &recording::RecordingService::callEnded, this, &IRecorderFacade::callEnded);
    connect(rec, &recording::RecordingService::silenceDetected, this, &IRecorderFacade::silenceDetected);
    // Before "Saved" we check that the track files are really there.
    connect(rec, &recording::RecordingService::finished, this, [this](const Meeting& m) {
        recording::RecordingOutcome out;
        out.meetingId = m.id;
        out.title = m.title;
        out.durationMs = m.durationMs;
        for (const Track& t : m.tracks) {
            const QFileInfo fi(QDir(m.folder).absoluteFilePath(t.file));
            if (fi.exists() && fi.size() > 0) ++out.tracksOk; else ++out.tracksMissing;
        }
        emit recordingFinished(out);
    });
    // A device renamed in Settings.
    if (m_deps.settings)
        connect(m_deps.settings, &app::SettingsManager::settingsChanged, this, &IRecorderFacade::deviceNamesChanged);
    if (m_deps.tags)
        connect(m_deps.tags, &library::TagService::tagsChanged, this, &IRecorderFacade::tagsChanged);

    m_fallbackScan.setInterval(3000);
    connect(&m_fallbackScan, &QTimer::timeout, this, &RecorderServicesFacade::refreshDevices);
}

RecorderServicesFacade::~RecorderServicesFacade() = default;

// ---- devices ----

QVector<AudioDeviceInfo> RecorderServicesFacade::captureDevices() const
{
    // The recorder's order: microphones, audio outputs, other inputs; in each group the system's
    // default device first.
    QVector<AudioDeviceInfo> devices = m_deps.recording->devices()->captureDevices();
    auto group = [](const AudioDeviceInfo& d) {
        return d.kind == TrackKind::Mic ? 0 : d.kind == TrackKind::Loopback ? 1 : 2;
    };
    std::stable_sort(devices.begin(), devices.end(), [&](const AudioDeviceInfo& x, const AudioDeviceInfo& y) {
        if (group(x) != group(y)) return group(x) < group(y);
        return x.isDefault && !y.isDefault;
    });
    return devices;
}

QString RecorderServicesFacade::friendlyName(const QString& deviceName) const
{
    return library::devicenames::displayName(deviceName);
}

QString RecorderServicesFacade::appForOutput(const QString& deviceName) const
{
    return m_routes ? m_routes->snapshot().appForOutput(deviceName) : QString();
}

bool RecorderServicesFacade::autoRecordAllDevices() const
{
    return m_deps.settings ? m_deps.settings->settings().recording.autoRecordAllDevices
                           : app::AppSettings{}.recording.autoRecordAllDevices;
}

QStringList RecorderServicesFacade::knownDevices() const
{
    QSettings ini(windowStatePath(), QSettings::IniFormat);
    return ini.value(QStringLiteral("devices/known")).toStringList();
}

void RecorderServicesFacade::setKnownDevices(const QStringList& names)
{
    QSettings ini(windowStatePath(), QSettings::IniFormat);
    ini.setValue(QStringLiteral("devices/known"), names);
}

QStringList RecorderServicesFacade::lastUsedDeviceNames() const
{
    return m_deps.recording->lastUsedDeviceNames();
}

void RecorderServicesFacade::setLastUsedDeviceNames(const QStringList& names)
{
    m_deps.recording->setLastUsedDeviceNames(names);
}

void RecorderServicesFacade::refreshDevices()
{
    m_deps.recording->devices()->refresh();
}

void RecorderServicesFacade::startWatching()
{
    if (m_watchers++ > 0) return;
    // Playback routes + hot-plug: where available (Linux/PipeWire), the graph change signals it;
    // elsewhere we re-enumerate devices on a timer.
    if (watcher::PlaybackRouteMonitor::available()) {
        m_routes = new watcher::PlaybackRouteMonitor(this);
        connect(m_routes, &watcher::PlaybackRouteMonitor::routesChanged, this, &IRecorderFacade::routesChanged);
        connect(m_routes, &watcher::PlaybackRouteMonitor::deviceSetChanged, this, &RecorderServicesFacade::refreshDevices);
        m_routes->start(2000);
    } else {
        m_fallbackScan.start();
    }
}

void RecorderServicesFacade::stopWatching()
{
    if (m_watchers == 0 || --m_watchers > 0) return;
    m_fallbackScan.stop();
    if (m_routes) {
        m_routes->stop();
        m_routes->deleteLater();
        m_routes = nullptr;
    }
}

void RecorderServicesFacade::startLevelMonitoring() { m_deps.recording->startLevelMonitoring(); }
void RecorderServicesFacade::stopLevelMonitoring() { m_deps.recording->stopLevelMonitoring(); }

// ---- recording ----

RecordingState RecorderServicesFacade::state() const { return m_deps.recording->state(); }
QString RecorderServicesFacade::currentFolder() const { return m_deps.recording->currentFolder(); }
QStringList RecorderServicesFacade::recordingDeviceNames() const { return m_deps.recording->liveDeviceNames(); }
QStringList RecorderServicesFacade::disconnectedDeviceNames() const { return m_deps.recording->disconnectedDeviceNames(); }

void RecorderServicesFacade::start(const QString& title, const QVector<AudioDeviceInfo>& devices)
{
    m_deps.recording->start(title, devices);
}

void RecorderServicesFacade::stop() { m_deps.recording->stop(); }
bool RecorderServicesFacade::addDevice(const AudioDeviceInfo& device) { return m_deps.recording->addDevice(device); }
bool RecorderServicesFacade::stopDevice(const QString& deviceName) { return m_deps.recording->stopDevice(deviceName); }
void RecorderServicesFacade::setRecordingTags(const QStringList& tagIds) { m_deps.recording->setTags(tagIds); }

// ---- the finished meeting ----

void RecorderServicesFacade::renameMeeting(const QString& meetingId, const QString& title)
{
    const QString t = title.trimmed();
    if (t.isEmpty() || !m_deps.store) return;
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) { emit error(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    m.title = t;
    m_deps.store->saveMeeting(m);   // meeting.json + the index refresh -> meetingUpdated signal
}

void RecorderServicesFacade::setMeetingContextNote(const QString& meetingId, const QString& note)
{
    if (!m_deps.store) return;
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QString n = note.trimmed();
    if (m.contextNote == n) return;   // no change
    m.contextNote = n;
    m_deps.store->saveMeeting(m);
}

void RecorderServicesFacade::setMeetingDetectedCall(const QString& meetingId, const QString& appName)
{
    if (!m_deps.store) return;
    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QString a = appName.trimmed();
    if (m.detectedCallApp == a) return;
    m.detectedCallApp = a;
    m_deps.store->saveMeeting(m);
}

// ---- tags ----

recording::RecorderTag RecorderServicesFacade::createTag(const QString& name)
{
    if (!m_deps.tags) return {};
    const Tag t = m_deps.tags->create(name);   // for an existing key, the existing tag
    return t.isValid() ? recording::RecorderTag{t.id, t.name} : recording::RecorderTag{};
}

std::optional<recording::RecorderTag> RecorderServicesFacade::tag(const QString& id) const
{
    if (!m_deps.tags) return std::nullopt;
    const Tag t = m_deps.tags->tag(id);
    if (!t.isValid()) return std::nullopt;
    return recording::RecorderTag{t.id, t.name};
}

// ---- hosting ----

QString RecorderServicesFacade::windowStatePath() const
{
    return (m_deps.settings ? m_deps.settings->paths() : paths::current()).stateFile(paths::files::RecorderIni);
}

QString RecorderServicesFacade::lockFilePath() const
{
    return recordingLockPath();
}

} // namespace tanara_qml

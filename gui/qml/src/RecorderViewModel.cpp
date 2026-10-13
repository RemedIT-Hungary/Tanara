#include "RecorderViewModel.h"

#include "DemoText.h"

#include "AppContext.h"
#include "TagDemoBackend.h"

#include "tanara/AppController.h"
#include "tanara/foundation/paths/Paths.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/audio/PlaybackRouting.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/library/MeetingNotes.h"
#include "tanara/tags/TagService.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>

#include <algorithm>
#include <cmath>

using namespace tanara;

namespace tanara_qml {

namespace {

constexpr float  kSignalRms    = 0.004f;   // above this there is "signal" (about -48 dBFS)
constexpr qint64 kSignalHoldMs = 1500;     // the last sound still counts as signal for this long
constexpr qint64 kPeakHoldMs   = 1500;     // peak-hold time
constexpr int    kSegments     = 14;

QString recorderIniPath(AppController* c)
{
    const QString dir = paths::resolveMetadataDir(
        c && c->settings() ? c->settings()->settings().metadataDir : QString());
    return QDir(dir).filePath(QStringLiteral("recorder.ini"));
}

} // namespace

// ---- RecorderDeviceModel ---------------------------------------------------------------

RecorderDeviceModel::RecorderDeviceModel(RecorderViewModel* vm)
    : QAbstractListModel(vm), m_vm(vm) {}

int RecorderDeviceModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_vm->m_rows.size();
}

QHash<int, QByteArray> RecorderDeviceModel::roleNames() const
{
    return {
        {KeyRole, "key"}, {NameRole, "name"}, {RawNameRole, "rawName"}, {GroupRole, "group"},
        {GroupFirstRole, "groupFirst"}, {IconRole, "iconName"}, {SelectedRole, "selected"},
        {LockedRole, "locked"}, {DefaultRole, "isDefault"}, {AppRole, "appName"},
        {LevelRole, "level"}, {PeakRole, "peak"}, {StatusRole, "status"},
        {StatusTextRole, "statusText"}, {ToggleableRole, "toggleable"},
    };
}

QVariant RecorderDeviceModel::data(const QModelIndex& index, int role) const
{
    const int i = index.row();
    if (i < 0 || i >= m_vm->m_rows.size()) return {};
    const RecorderViewModel::Row& r = m_vm->m_rows.at(i);
    switch (role) {
    case KeyRole:
    case RawNameRole:    return r.info.name;
    case NameRole:       return r.friendly;
    case GroupRole:      return r.group;
    case GroupFirstRole: return i == 0 || m_vm->m_rows.at(i - 1).group != r.group;
    case IconRole:       return m_vm->iconFor(r);
    case SelectedRole:   return r.selected || r.recorded;
    case LockedRole:     return r.disconnected;
    case DefaultRole:    return r.info.isDefault;
    case AppRole:        return r.app;
    case LevelRole:      return r.level;
    case PeakRole:       return r.peakSeg;
    case StatusRole:     return r.status;
    case StatusTextRole: return r.statusText;
    case ToggleableRole: {
        // During recording the recorded track can be switched off too (its file is closed; switching
        // it on again starts a new segment). A disconnected device's row is locked until it returns.
        const QString st = m_vm->m_state;
        if (r.disconnected) return false;
        return st == QLatin1String("idle") || st == QLatin1String("recording");
    }
    }
    return {};
}

// ---- RecorderViewModel -----------------------------------------------------------------

RecorderViewModel::RecorderViewModel(QObject* parent) : QObject(parent)
{
    m_clock.start();
    m_tick.setInterval(33);   // ~30 Hz: peak hold + statuses
    connect(&m_tick, &QTimer::timeout, this, &RecorderViewModel::tick);
    m_fallbackScan.setInterval(3000);
    connect(&m_fallbackScan, &QTimer::timeout, this, [this] {
        if (m_controller) m_controller->refreshDevices();
    });

    m_title = automaticTitle(QString(), QDateTime::currentDateTime());
    // App.controller binds in the next event-loop turn, if by then the caller has not given its
    // own (setController) and has not asked for a demo state.
    QMetaObject::invokeMethod(this, [this] {
        if (m_controllerSet) return;
        if (AppController* c = AppContext::instance()->controller())
            setController(c);
        else if (m_demoState.isEmpty() && AppContext::instance()->demo())
            setDemoState(QStringLiteral("R01"));
    }, Qt::QueuedConnection);
}

RecorderViewModel::~RecorderViewModel()
{
    // Level monitoring is stopped only when no recording runs (we do not touch the recording).
    if (m_controller && m_controller->recordingState() == RecordingState::Idle)
        m_controller->stopLevelMonitoring();
}

QObject* RecorderViewModel::controllerObject() const { return m_controller; }
AppController* RecorderViewModel::controller() const { return m_controller; }

void RecorderViewModel::setControllerObject(QObject* controller)
{
    setController(qobject_cast<AppController*>(controller));
}

void RecorderViewModel::setController(AppController* controller)
{
    m_controllerSet = true;
    if (m_controller == controller) return;
    if (m_controller) m_controller->disconnect(this);
    m_controller = controller;
    attachTagsOnly(controller);
    if (m_controller) {
        m_demoState.clear();
        attach();
    }
    emit controllerChanged();
}

void RecorderViewModel::attachTagsOnly(AppController* controller)
{
    m_controllerSet = true;
    disconnect(m_tagConn);
    m_tagCtl = controller;
    if (controller && controller->tags())
        m_tagConn = connect(controller->tags(), &TagService::tagsChanged, this, &RecorderViewModel::refreshTagNames);
}

QString RecorderViewModel::automaticTitle(const QString& appName, const QDateTime& when)
{
    const QString stamp = QLocale().toString(when, QStringLiteral("MMM d. HH:mm"));
    const QString what = appName.trimmed().isEmpty()
        ? tr("Megbeszélés") : tr("%1-hívás").arg(appName.trimmed());
    return what + QStringLiteral(" · ") + stamp;
}

int RecorderViewModel::levelSegments(float rms)
{
    if (rms <= 0.001f) return 0;                       // nothing below -60 dBFS
    const double db = 20.0 * std::log10(static_cast<double>(rms));
    return std::clamp(static_cast<int>(std::lround((db + 60.0) / 60.0 * kSegments)), 0, kSegments);
}

QString RecorderViewModel::elapsedText() const
{
    const qint64 s = m_elapsedMs / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

int RecorderViewModel::selectedCount() const
{
    int n = 0;
    for (const Row& r : m_rows) if (r.selected || r.recorded) ++n;
    return n;
}

int RecorderViewModel::trackCount() const
{
    int n = 0;
    for (const Row& r : m_rows) if (r.recorded || r.disconnected) ++n;
    return n;
}

bool RecorderViewModel::canStart() const
{
    return m_state == QLatin1String("idle") && selectedCount() > 0;
}

void RecorderViewModel::setTitle(const QString& title)
{
    const QString t = title.trimmed();
    if (t.isEmpty() || (t == m_title && !m_titleAuto)) return;
    if (t == m_title && m_titleAuto) return;           // nothing was typed -> stays automatic
    m_title = t;
    m_titleAuto = false;
    emit titleChanged();
    // A finished meeting is renamed right away; a running recording's at the end (onFinished).
    if (m_controller && m_state == QLatin1String("done") && !m_doneMeetingId.isEmpty())
        m_controller->renameMeeting(m_doneMeetingId, m_title);
}

void RecorderViewModel::setState(const QString& state)
{
    if (m_state == state) return;
    m_state = state;
    emit stateChanged();
    emit countsChanged();
    if (!m_rows.isEmpty())
        emit m_model.dataChanged(m_model.index(0), m_model.index(m_rows.size() - 1));
}

QString RecorderViewModel::iconFor(const Row& r) const
{
    if (r.group == 0) return QStringLiteral("mic");
    if (r.group == 2) return QStringLiteral("cable");
    const QString n = r.info.name.toLower();
    static const QStringList heads{QStringLiteral("headset"), QStringLiteral("headphone"),
                                   QStringLiteral("fejhallgat"), QStringLiteral("fülhallgat"),
                                   QStringLiteral("earbud"), QStringLiteral("airpods")};
    for (const QString& k : heads) if (n.contains(k)) return QStringLiteral("headphones");
    return tracknames::looksLikeCallDevice(r.info.name) ? QStringLiteral("headphones")
                                                        : QStringLiteral("speaker");
}

int RecorderViewModel::rowOf(const QString& deviceName) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).info.name == deviceName) return i;
    return -1;
}

QVector<AudioDeviceInfo> RecorderViewModel::selectedDevices() const
{
    QVector<AudioDeviceInfo> out;
    for (const Row& r : m_rows) if (r.selected) out.push_back(r.info);
    return out;
}

// ---- wiring the core -------------------------------------------------------------------

void RecorderViewModel::attach()
{
    AppController* c = m_controller;
    {
        QSettings ini(recorderIniPath(c), QSettings::IniFormat);
        m_knownDevices = ini.value(QStringLiteral("devices/known")).toStringList();
    }
    connect(c, &AppController::devicesChanged, this, &RecorderViewModel::rebuildDevices);
    // A device renamed in Settings is refreshed here immediately too (devicenames).
    if (c->settings())
        connect(c->settings(), &SettingsManager::settingsChanged, this, [this] {
            for (int i = 0; i < m_rows.size(); ++i) {
                const QString name = devicenames::displayName(m_rows.at(i).info.name);
                if (name == m_rows.at(i).friendly) continue;
                m_rows[i].friendly = name;
                const QModelIndex idx = m_model.index(i);
                emit m_model.dataChanged(idx, idx, {RecorderDeviceModel::NameRole});
            }
        });
    connect(c, &AppController::deviceLevelPeak, this, &RecorderViewModel::onLevel);
    connect(c, &AppController::recordingStateChanged, this, &RecorderViewModel::onRecordingState);
    connect(c, &AppController::elapsedChanged, this, [this](qint64 ms) {
        // Signaling once per second is enough (the display is HH:MM:SS).
        if (ms / 1000 == m_elapsedMs / 1000 && m_elapsedMs != 0) { m_elapsedMs = ms; return; }
        m_elapsedMs = ms;
        emit elapsedChanged();
    });
    connect(c, &AppController::recordingFinished, this, &RecorderViewModel::onFinished);
    connect(c, &AppController::recordingTrackAdded, this, [this](const QString& name) {
        const int i = rowOf(name);
        if (i < 0) return;
        m_rows[i].recorded = true;
        m_rows[i].disconnected = false;
        m_rows[i].selected = true;
        m_rows[i].silentSince = m_clock.elapsed();
        refreshRow(i, false);
        emit countsChanged();
    });
    connect(c, &AppController::recordingTrackClosed, this, [this](const QString& name) {
        const int i = rowOf(name);
        // Disconnected device, or the user switched it off (the latter can be switched on again).
        const bool unplugged = !m_controller
            || m_controller->disconnectedRecordingDeviceNames().contains(name);
        if (i >= 0) {
            m_rows[i].recorded = false;
            m_rows[i].disconnected = unplugged;
            if (!unplugged) {
                m_rows[i].selected = false;
                m_rows[i].silentSince = m_clock.elapsed();
            }
            m_rows[i].rms = m_rows[i].peak = 0.f;
            refreshRow(i, false);
        } else {
            rebuildDevices();   // the row is no longer in the enumeration -> comes back as a "disconnected" row
        }
        emit countsChanged();
        if (unplugged && m_state == QLatin1String("recording")) {
            // Notice: the recording did NOT stop, only this device's track closed.
            const int row = rowOf(name);
            const QString shown = (row >= 0 && !m_rows[row].friendly.isEmpty()) ? m_rows[row].friendly : name;
            m_errorText = tr("A(z) %1 eszköz eltűnt, a sávja lezárult, a többi sáv megy tovább.").arg(shown);
            emit errorChanged();
        }
    });
    connect(c, &AppController::recordingDeviceReturned, this, [this](const QString& name) {
        // A device unplugged and plugged back in is recorded again by the system on its own (new segment).
        const int row = rowOf(name);
        const QString shown = (row >= 0 && !m_rows[row].friendly.isEmpty()) ? m_rows[row].friendly : name;
        m_errorText = tr("A(z) %1 eszköz visszatért, a rögzítése folytatódik (új szakasz).").arg(shown);
        emit errorChanged();
    });
    connect(c, &AppController::callEnded, this, [this](const QString& app) {
        raiseAsk(app.isEmpty()
            ? tr("Úgy tűnik, a hívás véget ért. Magamtól nem állítom le.")
            : tr("Úgy tűnik, véget ért: %1. Magamtól nem állítom le.").arg(app));
    });
    connect(c, &AppController::silenceDetected, this, [this](int minutes) {
        raiseAsk(tr("Minden sávon %n perce csend van. Magamtól nem állítom le.", nullptr, minutes));
    });
    connect(c, &AppController::errorOccurred, this, [this](const QString& e) {
        // We show only errors that concern the recorder: those arriving during start / stop.
        if (m_state == QLatin1String("done")) return;
        m_errorText = e;
        emit errorChanged();
    });

    // Playback routes + hot-plug: where available (Linux/PipeWire), the graph change signals it;
    // elsewhere we re-enumerate devices on a timer.
    if (PlaybackRouteMonitor::available()) {
        m_routes = new PlaybackRouteMonitor(this);
        connect(m_routes, &PlaybackRouteMonitor::routesChanged, this, &RecorderViewModel::updateRoutes);
        connect(m_routes, &PlaybackRouteMonitor::deviceSetChanged, this, [this] {
            if (m_controller) m_controller->refreshDevices();
        });
        m_routes->start(2000);
    } else {
        m_fallbackScan.start();
    }

    c->startLevelMonitoring();   // refresh → devicesChanged → rebuildDevices
    if (m_rows.isEmpty()) rebuildDevices();
    onRecordingState(c->recordingState());
    m_tick.start();
}

void RecorderViewModel::rebuildDevices()
{
    if (!m_controller || !m_controller->devices()) return;
    const QVector<AudioDeviceInfo> present = m_controller->devices()->captureDevices();
    const QStringList recNames = m_controller->recordingDeviceNames();
    const QStringList closed = m_controller->disconnectedRecordingDeviceNames();

    // Same set in the same state -> nothing to do (the DeviceManager signals on every
    // enumeration; rows and levels should not jump needlessly).
    QStringList presentNames;
    for (const AudioDeviceInfo& d : present) presentNames << d.name;
    // Rows of devices missing from the enumeration but belonging to the recording stay: as
    // recorded until the core closes the track; afterwards "disconnected".
    QStringList extras;
    for (const QString& n : recNames + closed)
        if (!presentNames.contains(n) && !extras.contains(n)) extras << n;

    // Same set in the same state -> nothing to do (the DeviceManager signals on every
    // enumeration; rows and levels should not jump needlessly).
    bool same = m_selectionLoaded && m_rows.size() == presentNames.size() + extras.size();
    for (int k = 0; same && k < present.size(); ++k) {
        const int i = rowOf(present.at(k).name);
        same = i >= 0 && !m_rows.at(i).disconnected
               && m_rows.at(i).info.isDefault == present.at(k).isDefault;
    }
    for (int k = 0; same && k < extras.size(); ++k) {
        const int i = rowOf(extras.at(k));
        same = i >= 0 && m_rows.at(i).disconnected == closed.contains(extras.at(k));
    }
    if (same) {
        updateRoutes();
        return;
    }

    // Source of the selection: the live rows; on the first enumeration the saved selection. A
    // device not seen until then arrives switched on in "record every device" mode (except
    // line-in types), switched off in manual mode.
    const AppSettings s = m_controller->settings() ? m_controller->settings()->settings()
                                                   : AppSettings{};
    const QStringList last = m_controller->lastUsedDeviceNames();
    auto wantSelected = [&](const AudioDeviceInfo& d) {
        const int old = rowOf(d.name);
        if (old >= 0 && m_selectionLoaded) return m_rows.at(old).selected;
        if (last.contains(d.name)) return true;
        const bool isNew = !m_knownDevices.contains(d.name);
        return (isNew || last.isEmpty()) && s.autoRecordAllDevices && d.kind != TrackKind::Other;
    };

    QVector<Row> rows;
    const qint64 now = m_clock.elapsed();
    for (const AudioDeviceInfo& d : present) {
        Row r;
        const int old = rowOf(d.name);
        if (old >= 0) r = m_rows.at(old);
        else r.silentSince = now;
        r.selected = wantSelected(d);
        r.info = d;
        r.friendly = devicenames::displayName(d.name);   // the user's name, otherwise the abbreviated one
        r.group = d.kind == TrackKind::Mic ? 0 : d.kind == TrackKind::Loopback ? 1 : 2;
        r.recorded = recNames.contains(d.name);
        r.disconnected = false;
        rows.push_back(r);
    }
    for (const QString& name : extras) {
        Row r;
        const int old = rowOf(name);
        if (old >= 0) r = m_rows.at(old);
        else {
            r.info.name = name;
            r.friendly = devicenames::displayName(name);
            r.info.kind = name.contains(QStringLiteral("monitor"), Qt::CaseInsensitive)
                              ? TrackKind::Loopback : TrackKind::Mic;
            r.group = r.info.kind == TrackKind::Mic ? 0 : 1;
        }
        r.disconnected = closed.contains(name);
        r.recorded = !r.disconnected;
        r.selected = true;
        r.rms = r.peak = 0.f;
        r.level = 0; r.peakSeg = -1;
        rows.push_back(r);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) {
        if (x.group != y.group) return x.group < y.group;
        return x.info.isDefault && !y.info.isDefault;
    });

    m_model.beginResetModel();
    m_rows = rows;
    m_model.endResetModel();
    m_selectionLoaded = true;

    bool knownChanged = false;
    for (const QString& n : presentNames)
        if (!m_knownDevices.contains(n)) { m_knownDevices << n; knownChanged = true; }
    if (knownChanged) {
        QSettings ini(recorderIniPath(m_controller), QSettings::IniFormat);
        ini.setValue(QStringLiteral("devices/known"), m_knownDevices);
    }

    updateRoutes();
    for (int i = 0; i < m_rows.size(); ++i) computeStatus(m_rows[i], now);
    emit countsChanged();
    if (m_state == QLatin1String("idle") || m_state == QLatin1String("noDevice"))
        setState(m_rows.isEmpty() ? QStringLiteral("noDevice") : QStringLiteral("idle"));
}

void RecorderViewModel::updateRoutes()
{
    if (!m_routes) return;
    const AudioGraphSnapshot snap = m_routes->snapshot();
    for (int i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        const QString app = r.group == 1 ? snap.appForOutput(r.info.name) : QString();
        if (app == r.app) continue;
        r.app = app;
        emit m_model.dataChanged(m_model.index(i), m_model.index(i), {RecorderDeviceModel::AppRole});
    }
}

void RecorderViewModel::onLevel(const QString& device, float rms, float peak)
{
    const int i = rowOf(device);
    if (i < 0) return;
    m_rows[i].rms = rms;
    m_rows[i].peak = qMax(peak, rms);
}

void RecorderViewModel::computeStatus(Row& r, qint64 now) const
{
    if (r.demoFixed) return;
    const bool recordingNow = m_state == QLatin1String("recording");
    const bool signal = r.lastSignalAt > 0 && now - r.lastSignalAt <= kSignalHoldMs;
    QString st, text;
    if (r.disconnected) {
        st = QStringLiteral("disconnected");
        text = tr("leválasztva");
    } else if (recordingNow ? r.recorded : r.selected) {
        if (!signal) {
            const qint64 silentMin = recordingNow ? (now - r.silentSince) / 60000 : 0;
            if (recordingNow && silentMin >= silenceWarnMinutes()) {
                st = QStringLiteral("silentWarn");
                text = tr("%n perce nincs jel", nullptr, static_cast<int>(silentMin));
            } else if (now - qMax(r.lastSignalAt, r.silentSince) > kSignalHoldMs) {
                st = QStringLiteral("noSignal");
                text = tr("nincs jel");
            }
        }
    } else if (signal && (m_state == QLatin1String("idle") || recordingNow)) {
        st = QStringLiteral("signalUnrecorded");
        text = tr("jel van · nincs rögzítve");
    }
    r.status = st;
    r.statusText = text;
}

void RecorderViewModel::tick()
{
    const qint64 now = m_clock.elapsed();
    for (int i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        if (r.demoFixed) continue;
        const int oldLevel = r.level, oldPeak = r.peakSeg;
        const QString oldStatus = r.status, oldText = r.statusText;

        r.level = levelSegments(r.rms);
        if (r.rms >= kSignalRms) {
            r.lastSignalAt = now;
            r.silentSince = now;
        }
        // Peak hold: the highest reached segment stays marked for ~1.5 s, then drops.
        const int peakLevel = qMax(levelSegments(r.peak), r.level);
        const int seg = peakLevel - 1;
        if (seg >= r.peakSeg) { r.peakSeg = seg; r.peakAt = now; }
        else if (now - r.peakAt > kPeakHoldMs) { r.peakSeg = seg; r.peakAt = now; }
        if (r.peakSeg < r.level) r.peakSeg = -1;        // nothing to mark below the lit part

        computeStatus(r, now);
        const bool levelChanged = r.level != oldLevel || r.peakSeg != oldPeak;
        const bool statusChanged = r.status != oldStatus || r.statusText != oldText;
        if (statusChanged) refreshRow(i, false);
        else if (levelChanged) refreshRow(i, true);
    }
}

void RecorderViewModel::refreshRow(int row, bool levelOnly)
{
    const QModelIndex ix = m_model.index(row);
    if (levelOnly)
        emit m_model.dataChanged(ix, ix, {RecorderDeviceModel::LevelRole, RecorderDeviceModel::PeakRole});
    else
        emit m_model.dataChanged(ix, ix);
}

void RecorderViewModel::persistSelection()
{
    if (!m_controller) return;
    QStringList names;
    for (const Row& r : m_rows) if (r.selected && !r.disconnected) names << r.info.name;
    m_controller->setLastUsedDeviceNames(names);
}

// ---- actions -------------------------------------------------------------------------

void RecorderViewModel::applyRequest(const QString& title, const QString& appName,
                                     const QString& context, const QList<int>& deviceIndexes)
{
    if (m_state == QLatin1String("recording") || m_state == QLatin1String("stopping"))
        return;   // external requests do not touch a running recording's title / sources
    // The old watcher also sent the detected call as a note ("Automatically detected call:
    // ..."): that is not a note but the detected app, so it goes into a separate field.
    QString detectedApp = appName.trimmed();
    QString autoApp;
    if (tanara::meetingnotes::parseAutoCallNote(context, &autoApp)) {
        if (detectedApp.isEmpty()) detectedApp = autoApp;
    } else if (!context.trimmed().isEmpty()) {
        m_context = context.trimmed();
    }
    if (!detectedApp.isEmpty()) m_appName = detectedApp;
    if (!title.trimmed().isEmpty()) {
        m_title = title.trimmed();
        m_titleAuto = false;
        emit titleChanged();
    } else if (!detectedApp.isEmpty() && m_titleAuto) {
        m_title = automaticTitle(m_appName, QDateTime::currentDateTime());
        emit titleChanged();
    }
    if (!deviceIndexes.isEmpty() && m_controller && m_controller->devices()) {
        const QVector<AudioDeviceInfo> all = m_controller->devices()->captureDevices();
        QStringList want;
        for (int idx : deviceIndexes)
            if (idx >= 0 && idx < all.size()) want << all.at(idx).name;
        if (!want.isEmpty()) {
            for (Row& r : m_rows) r.selected = want.contains(r.info.name);
            if (!m_rows.isEmpty())
                emit m_model.dataChanged(m_model.index(0), m_model.index(m_rows.size() - 1));
            emit countsChanged();
        }
    }
}

void RecorderViewModel::start()
{
    if (m_state != QLatin1String("idle")) return;
    clearError();
    if (!m_controller) {                       // demo: only the state changes
        if (!m_demoState.isEmpty()) setDemoState(QStringLiteral("R03"));
        return;
    }
    const QVector<AudioDeviceInfo> sel = selectedDevices();
    if (sel.isEmpty()) {
        m_errorText = tr("Nincs bekapcsolt forrás. Kapcsolj be legalább egy eszközt.");
        emit errorChanged();
        return;
    }
    if (m_titleAuto) {                         // the automatic name carries the START time
        m_title = automaticTitle(m_appName, QDateTime::currentDateTime());
        emit titleChanged();
    }
    m_controller->startRecording(m_title, sel);
    syncRecordingTags();                       // tags chosen before starting (start clears them)
}

void RecorderViewModel::stop()
{
    if (m_state != QLatin1String("recording")) return;
    if (!m_controller) {
        if (!m_demoState.isEmpty()) setDemoState(QStringLiteral("R09"));
        return;
    }
    m_controller->stopRecording();
}

void RecorderViewModel::toggleDevice(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    Row& r = m_rows[row];
    if (r.disconnected) return;                          // disconnected device: nothing to switch
    if (m_state == QLatin1String("idle")) {
        r.selected = !r.selected;
        r.silentSince = m_clock.elapsed();
        if (!r.demoFixed) computeStatus(r, m_clock.elapsed());
        else { r.status.clear(); r.statusText.clear(); }
        refreshRow(row, false);
        emit countsChanged();
        persistSelection();
    } else if (m_state == QLatin1String("recording")) {
        // During recording: ON -> its track starts from that moment (a new segment for a device
        // switched off earlier); OFF -> the track's file closes here, the recording goes on.
        if (!m_controller) {
            r.recorded = r.selected = !r.recorded;
            refreshRow(row, false);
            emit countsChanged();
            return;
        }
        if (r.recorded) {
            if (!m_controller->stopRecordingDevice(r.info.name)) {
                m_errorText = tr("Az utolsó rögzített sáv nem kapcsolható ki — a felvételt a Leállítás gombbal fejezheted be.");
                emit errorChanged();
            }
            return;
        }
        if (!m_controller->addRecordingDevice(r.info)) {
            m_errorText = tr("Nem sikerült sávot indítani ezen az eszközön: %1").arg(r.friendly);
            emit errorChanged();
        }
    }
}

void RecorderViewModel::rescan()
{
    clearError();
    if (m_controller) m_controller->refreshDevices();
}

void RecorderViewModel::newRecording()
{
    if (m_state != QLatin1String("done")) return;
    m_doneSummary.clear(); m_doneMeetingId.clear(); m_doneProblem.clear();
    emit doneChanged();
    m_elapsedMs = 0;
    emit elapsedChanged();
    m_appName.clear();
    m_context.clear();
    if (!m_tags.isEmpty()) { m_tags.clear(); emit tagsChanged(); }   // the core starts the next one empty too
    m_titleAuto = true;
    m_title = automaticTitle(QString(), QDateTime::currentDateTime());
    emit titleChanged();
    if (!m_controller && !m_demoState.isEmpty()) { setDemoState(QStringLiteral("R01")); return; }
    setState(m_rows.isEmpty() ? QStringLiteral("noDevice") : QStringLiteral("idle"));
}

void RecorderViewModel::openInAnalyzer() { emit openAnalyzerRequested(m_doneMeetingId); }
void RecorderViewModel::openSettings() { emit settingsRequested(); }

void RecorderViewModel::continueRecording()
{
    if (!m_askVisible) return;
    m_askVisible = false;
    emit askChanged();
}

void RecorderViewModel::clearError()
{
    if (m_errorText.isEmpty()) return;
    m_errorText.clear();
    emit errorChanged();
}

void RecorderViewModel::raiseAsk(const QString& text)
{
    if (m_state != QLatin1String("recording")) return;
    m_askText = text;
    m_askVisible = true;
    emit askChanged();
    emit askRaised(tr("Vége a megbeszélésnek?"), text);
}

// ---- tags ----

QVariantList RecorderViewModel::tags() const
{
    QVariantList out;
    for (const TagRef& t : m_tags)
        out << QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("name"), t.name}};
    return out;
}

QStringList RecorderViewModel::tagIds() const
{
    QStringList out;
    for (const TagRef& t : m_tags) out << t.id;
    return out;
}

bool RecorderViewModel::tagsEditable() const
{
    // Also during stopping: the core reads the recording's tags when saving the meeting.
    return m_state == QLatin1String("idle") || m_state == QLatin1String("recording")
        || m_state == QLatin1String("stopping");
}

bool RecorderViewModel::addTag(const QString& name)
{
    const QString n = name.simplified();
    if (n.isEmpty() || !tagsEditable()) return false;
    TagRef ref;
    if (m_tagCtl) {
        TagService* svc = m_tagCtl->tags();
        if (!svc) return false;
        const Tag t = svc->create(n);          // for an existing key, the existing tag
        if (!t.isValid()) return false;
        ref = {t.id, t.name};
    } else {
        // Demo: the ids of the fictional pool (the input list sees the same).
        TagDemoBackend demo(TagDemoBackend::Content::Sample, nullptr);
        for (const TagItem& t : demo.tags())
            if (t.name.compare(n, Qt::CaseInsensitive) == 0) ref = {t.id, t.name};
        if (ref.id.isEmpty()) ref = {QStringLiteral("new-") + n, n};
    }
    for (const TagRef& t : std::as_const(m_tags))
        if (t.id == ref.id) return false;
    m_tags.append(ref);
    emit tagsChanged();
    syncRecordingTags();
    return true;
}

void RecorderViewModel::removeTag(const QString& id)
{
    for (int i = 0; i < m_tags.size(); ++i) {
        if (m_tags.at(i).id != id) continue;
        if (!tagsEditable()) return;
        m_tags.removeAt(i);
        emit tagsChanged();
        syncRecordingTags();
        return;
    }
}

bool RecorderViewModel::openTagInput()
{
    if (!tagsEditable()) return false;
    emit tagInputRequested();
    return true;
}

void RecorderViewModel::syncRecordingTags()
{
    // Handed over before the start too (startRecording clears them, then handed over again);
    // the finished recording's tags are no longer touched from here.
    if (!m_tagCtl || m_state == QLatin1String("done")) return;
    m_tagCtl->setRecordingTags(tagIds());
}

void RecorderViewModel::refreshTagNames()
{
    if (!m_tagCtl || !m_tagCtl->tags()) return;
    bool changed = false;
    for (int i = 0; i < m_tags.size(); ++i) {
        const Tag t = m_tagCtl->tags()->tag(m_tags.at(i).id);
        if (!t.isValid()) { m_tags.removeAt(i--); changed = true; continue; }   // deleted meanwhile
        if (t.name != m_tags.at(i).name) { m_tags[i].name = t.name; changed = true; }
    }
    if (!changed) return;
    emit tagsChanged();
    syncRecordingTags();
}

// ---- recording state ------------------------------------------------------------------

void RecorderViewModel::onRecordingState(RecordingState st)
{
    if (!m_controller) return;
    if (st == RecordingState::Recording) {
        const QStringList rec = m_controller->recordingDeviceNames();
        const qint64 now = m_clock.elapsed();
        for (Row& r : m_rows) {
            r.recorded = rec.contains(r.info.name);
            r.silentSince = now;
        }
        clearError();
        syncRecordingTags();
        setState(QStringLiteral("recording"));
        emit recordingStarted(m_controller->currentMeetingFolder());
    } else if (st == RecordingState::Stopping || st == RecordingState::Encoding) {
        if (m_askVisible) { m_askVisible = false; emit askChanged(); }
        setState(QStringLiteral("stopping"));
    } else if (m_state == QLatin1String("recording") || m_state == QLatin1String("stopping")) {
        // Idle without the "done" signal: the recording was not created / was interrupted (the error
        // came via errorOccurred). Only recordingFinished leads to the done state.
        for (Row& r : m_rows) { r.recorded = false; r.rms = r.peak = 0.f; }
        setState(m_rows.isEmpty() ? QStringLiteral("noDevice") : QStringLiteral("idle"));
        rebuildDevices();
    }
}

void RecorderViewModel::onFinished(const Meeting& m)
{
    // We get here when the core has closed the tracks' encoders AND saved the meeting. Before
    // "Saved" we check ourselves that the track files are really there.
    int ok = 0;
    QStringList missing;
    for (const Track& t : m.tracks) {
        const QFileInfo fi(QDir(m.folder).absoluteFilePath(t.file));
        if (fi.exists() && fi.size() > 0) ++ok; else missing << t.file;
    }
    const qint64 s = m.durationMs / 1000;
    const QString dur = s >= 3600
        ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
              .arg(s % 60, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    m_doneSummary = dur + QStringLiteral(" · ") + tr("%n sáv", nullptr, ok);
    m_doneMeetingId = m.id;
    m_doneProblem = ok == 0 ? tr("Egyetlen sáv hangfájlja sem jött létre.")
                  : !missing.isEmpty() ? tr("%n sáv hangfájlja hiányzik.", nullptr, static_cast<int>(missing.size()))
                  : QString();
    emit doneChanged();

    if (m_controller) {
        if (m.title != m_title && !m_title.isEmpty())
            m_controller->renameMeeting(m.id, m_title);     // rename during recording
        if (!m_context.isEmpty())
            m_controller->setMeetingContextNote(m.id, m_context);
        if (!m_appName.isEmpty())
            m_controller->setMeetingDetectedCall(m.id, m_appName);   // "Detected call: ..."
    }
    for (Row& r : m_rows) { r.recorded = false; r.rms = r.peak = 0.f; }
    const bool hadDisconnected = std::any_of(m_rows.cbegin(), m_rows.cend(),
                                             [](const Row& r) { return r.disconnected; });
    setState(QStringLiteral("done"));
    if (hadDisconnected) { m_selectionLoaded = true; rebuildDevices(); }
    emit recordingFinished(m.id);
}

// ---- demo (fictional devices, the design states) ----------------------------------------

void RecorderViewModel::setDemoState(const QString& state)
{
    if (m_controller) return;
    m_controllerSet = true;              // demo: we do not wait for App.controller either
    m_demoState = state;
    loadDemo();
    emit demoStateChanged();
}

void RecorderViewModel::loadDemo()
{
    const QString st = m_demoState;
    const bool rec = st == QLatin1String("R03") || st == QLatin1String("R04")
                  || st == QLatin1String("R05") || st == QLatin1String("R06")
                  || st == QLatin1String("R07");
    const bool quiet = st == QLatin1String("R06");
    const QDateTime when(QDate(2026, 10, 3), QTime(14, 2));

    // The fictional device names in the English demo language (English counterparts of the Hungarian system descriptions).
    static const QHash<QString, QString> deviceEn{
        {QStringLiteral("Trust USB mikrofon"), QStringLiteral("Trust USB microphone")},
        {QStringLiteral("Sennheiser fejhallgató (analóg)"), QStringLiteral("Sennheiser headphones (analog)")},
        {QStringLiteral("Monitor of Sennheiser headset - Kommunikáció"),
         QStringLiteral("Monitor of Sennheiser headset - Communication")},
        {QStringLiteral("Kanto YU4 hangfal"), QStringLiteral("Kanto YU4 speakers")},
        {QStringLiteral("Monitor of Kanto YU4 - Optikai digitális sztereó"),
         QStringLiteral("Monitor of Kanto YU4 - Digital Stereo (IEC958)")},
        {QStringLiteral("Monitor of Sennheiser fejhallgató (analóg sztereó)"),
         QStringLiteral("Monitor of Sennheiser headphones (analog stereo)")},
        {QStringLiteral("Alaplapi vonalbemenet"), QStringLiteral("Motherboard line-in")},
    };
    auto dev = [](const char* hu) {
        const QString s = QString::fromUtf8(hu);
        return demotext::english() ? deviceEn.value(s, s) : s;
    };
    auto mk = [&](const char* friendly, const char* raw, int group, bool sel, bool def,
                  const char* app, int level, int peak, const char* status, const QString& text) {
        Row r;
        r.info.name = dev(raw);
        r.info.kind = group == 0 ? TrackKind::Mic : group == 1 ? TrackKind::Loopback : TrackKind::Other;
        r.info.isDefault = def;
        r.friendly = dev(friendly);
        r.group = group;
        r.selected = sel;
        r.recorded = rec && sel;
        r.app = QString::fromUtf8(app);
        r.level = quiet ? 0 : level;
        r.peakSeg = quiet ? -1 : peak;
        r.status = QString::fromLatin1(status);
        r.statusText = text;
        r.demoFixed = true;
        return r;
    };
    QVector<Row> rows;
    if (st != QLatin1String("R10")) {
        const QString kantoText = rec ? tr("%n perce nincs jel", nullptr, 3) : tr("nincs jel");
        rows << mk("Trust USB mikrofon", "alsa_input.usb-Trust_USB_microphone-00.mono-fallback",
                   0, true, true, "", 9, 10, "", QString())
             << mk("Sennheiser fejhallgató (analóg)", "alsa_input.pci-0000_00_1f.3.analog-stereo",
                   0, false, false, "", 0, -1, "", QString())
             << mk("Sennheiser headset", "Monitor of Sennheiser headset - Kommunikáció",
                   1, true, false, "Microsoft Teams", rec ? 5 : 7, rec ? 8 : 9, "", QString())
             << mk("Kanto YU4 hangfal", "Monitor of Kanto YU4 - Optikai digitális sztereó",
                   1, true, true, "", 0, -1, quiet ? "" : (rec ? "silentWarn" : "noSignal"),
                   quiet ? QString() : kantoText)
             << mk("Sennheiser fejhallgató (analóg)", "Monitor of Sennheiser fejhallgató (analóg sztereó)",
                   1, false, false, "Firefox", 6, 7, "signalUnrecorded", tr("jel van · nincs rögzítve"))
             << mk("Alaplapi vonalbemenet", "alsa_input.pci-0000_00_1f.3.analog-stereo.line-in",
                   2, false, false, "", 0, -1, "", QString());
    }
    m_model.beginResetModel();
    m_rows = rows;
    m_model.endResetModel();

    const bool namedTitle = st != QLatin1String("R01") && st != QLatin1String("R10");
    m_title = namedTitle ? demoText("Ügyféltámogatás átadás-átvétel", "Customer support handover")
                         : automaticTitle(st == QLatin1String("R10") ? QString() : QStringLiteral("Teams"), when);
    m_titleAuto = !namedTitle;
    emit titleChanged();

    // Fictional tags: one before starting, two during recording.
    m_tags.clear();
    if (st != QLatin1String("R09") && st != QLatin1String("R10")) {
        m_tags.append({QStringLiteral("t-nordvik"), QStringLiteral("Nordvik")});
        if (rec) m_tags.append({QStringLiteral("t-q4"), demoText("Q4 tervezés", "Q4 planning")});
    }
    emit tagsChanged();

    m_elapsedMs = quiet ? (41 * 60 + 3) * 1000 : rec ? (12 * 60 + 47) * 1000 : 0;
    emit elapsedChanged();

    m_askVisible = quiet;
    m_askText = quiet ? tr("A hívás sávján %n perce csend van. Magamtól nem állítom le.", nullptr, 2)
                      : QString();
    emit askChanged();

    const bool done = st == QLatin1String("R09");
    m_doneSummary = done ? QStringLiteral("30:34 · ") + tr("%n sáv", nullptr, 3) : QString();
    m_doneMeetingId = done ? QStringLiteral("demo") : QString();
    m_doneProblem.clear();
    emit doneChanged();

    emit countsChanged();
    m_state.clear();
    setState(st == QLatin1String("R10") ? QStringLiteral("noDevice")
             : done ? QStringLiteral("done")
             : rec ? QStringLiteral("recording") : QStringLiteral("idle"));
}

} // namespace tanara_qml

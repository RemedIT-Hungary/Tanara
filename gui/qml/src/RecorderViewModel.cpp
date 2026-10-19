#include "RecorderViewModel.h"

#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/library/MeetingNotes.h"

#include <QDateTime>
#include <QLocale>

#include <algorithm>
#include <cmath>

using namespace tanara;
using tanara::recording::IRecorderFacade;

namespace tanara_qml {

namespace {

constexpr float  kSignalRms    = 0.004f;   // above this there is "signal" (about -48 dBFS)
constexpr qint64 kSignalHoldMs = 1500;     // the last sound still counts as signal for this long
constexpr qint64 kPeakHoldMs   = 1500;     // peak-hold time
constexpr int    kSegments     = 14;

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
        {StatusTextRole, "statusText"}, {ToggleableRole, "toggleable"}, {StalledRole, "stalled"},
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
    case StalledRole:    return m_vm->m_stalled.contains(r.info.name);
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

RecorderViewModel::RecorderViewModel(IRecorderFacade* facade, QObject* parent)
    : RecorderViewModel(facade, Wiring::Full, parent)
{
}

RecorderViewModel::RecorderViewModel(IRecorderFacade* facade, Wiring wiring, QObject* parent)
    : QObject(parent), m_facade(facade)
{
    m_clock.start();
    m_tick.setInterval(33);   // ~30 Hz: peak hold + statuses
    connect(&m_tick, &QTimer::timeout, this, &RecorderViewModel::tick);

    m_title = automaticTitle(QString(), QDateTime::currentDateTime());
    if (!m_facade)
        return;
    attachTags();
    if (wiring == Wiring::Full)
        attach();
}

RecorderViewModel::~RecorderViewModel()
{
    if (!m_attached || !m_facade)
        return;
    m_facade->stopWatching();
    // Level monitoring is stopped only when no recording runs (we do not touch the recording).
    if (m_facade->state() == RecordingState::Idle)
        m_facade->stopLevelMonitoring();
}


QObject* RecorderViewModel::tagBackendObject() const { return m_facade ? m_facade->tagBackend() : nullptr; }

void RecorderViewModel::attachTags()
{
    connect(m_facade, &IRecorderFacade::tagsChanged, this, &RecorderViewModel::refreshTagNames);
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
    if (m_facade && m_state == QLatin1String("done") && !m_doneMeetingId.isEmpty())
        m_facade->renameMeeting(m_doneMeetingId, m_title);
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
    return library::tracknames::looksLikeCallDevice(r.info.name) ? QStringLiteral("headphones")
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
    IRecorderFacade* c = m_facade;
    m_attached = true;
    m_knownDevices = c->knownDevices();
    connect(c, &IRecorderFacade::devicesChanged, this, &RecorderViewModel::rebuildDevices);
    // A device renamed in Settings is refreshed here immediately too (devicenames).
    connect(c, &IRecorderFacade::deviceNamesChanged, this, [this] {
        for (int i = 0; i < m_rows.size(); ++i) {
            const QString name = m_facade->friendlyName(m_rows.at(i).info.name);
            if (name == m_rows.at(i).friendly) continue;
            m_rows[i].friendly = name;
            const QModelIndex idx = m_model.index(i);
            emit m_model.dataChanged(idx, idx, {RecorderDeviceModel::NameRole});
        }
    });
    connect(c, &IRecorderFacade::deviceLevel, this, &RecorderViewModel::onLevel);
    connect(c, &IRecorderFacade::stateChanged, this, &RecorderViewModel::onRecordingState);
    connect(c, &IRecorderFacade::stateChanged, this, &RecorderViewModel::keepAwakeChanged);
    connect(c, &IRecorderFacade::keepAwakeStateChanged, this, &RecorderViewModel::keepAwakeChanged);
    connect(c, &IRecorderFacade::elapsedChanged, this, [this](qint64 ms) {
        if (!m_stalled.isEmpty()) updateStallWarning(ms);   // the warning's seconds run with the recording clock
        // Signaling once per second is enough (the display is HH:MM:SS).
        if (ms / 1000 == m_elapsedMs / 1000 && m_elapsedMs != 0) { m_elapsedMs = ms; return; }
        m_elapsedMs = ms;
        emit elapsedChanged();
    });
    connect(c, &IRecorderFacade::recordingFinished, this, &RecorderViewModel::onFinished);
    connect(c, &IRecorderFacade::trackAdded, this, [this](const QString& name) {
        const int i = rowOf(name);
        if (i < 0) return;
        m_rows[i].recorded = true;
        m_rows[i].disconnected = false;
        m_rows[i].selected = true;
        m_rows[i].silentSince = m_clock.elapsed();
        refreshRow(i, false);
        emit countsChanged();
    });
    connect(c, &IRecorderFacade::trackClosed, this, [this](const QString& name) {
        const int i = rowOf(name);
        // Disconnected device, or the user switched it off (the latter can be switched on again).
        const bool unplugged = !m_facade || m_facade->disconnectedDeviceNames().contains(name);
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
        // A track being re-opened has the "interrupted, resuming" line instead of this notice.
        if (unplugged && m_state == QLatin1String("recording") && !m_reopening.contains(name)) {
            // Notice: the recording did NOT stop, only this device's track closed.
            const int row = rowOf(name);
            const QString shown = (row >= 0 && !m_rows[row].friendly.isEmpty()) ? m_rows[row].friendly : name;
            m_errorText = tr("A(z) %1 eszköz eltűnt, a sávja lezárult, a többi sáv megy tovább.").arg(shown);
            emit errorChanged();
        }
    });
    connect(c, &IRecorderFacade::deviceReturned, this, [this](const QString& name) {
        // A device unplugged and plugged back in is recorded again by the system on its own (new segment).
        if (m_reopening.contains(name)) {   // the resume line tells it
            onReopenEnded(name, true);
            return;
        }
        const int row = rowOf(name);
        const QString shown = (row >= 0 && !m_rows[row].friendly.isEmpty()) ? m_rows[row].friendly : name;
        m_errorText = tr("A(z) %1 eszköz visszatért, a rögzítése folytatódik (új szakasz).").arg(shown);
        emit errorChanged();
    });
    connect(c, &IRecorderFacade::callEnded, this, [this](const QString& app) {
        raiseAsk(app.isEmpty()
            ? tr("Úgy tűnik, a hívás véget ért. Magamtól nem állítom le.")
            : tr("Úgy tűnik, véget ért: %1. Magamtól nem állítom le.").arg(app));
    });
    connect(c, &IRecorderFacade::silenceDetected, this, [this](int minutes) {
        raiseAsk(tr("Minden sávon %n perce csend van. Magamtól nem állítom le.", nullptr, minutes));
    });
    connect(c, &IRecorderFacade::error, this, [this](const QString& e) {
        // We show only errors that concern the recorder: those arriving during start / stop.
        if (m_state == QLatin1String("done")) return;
        m_errorText = e;
        emit errorChanged();
    });
    connect(c, &IRecorderFacade::trackStalled, this, &RecorderViewModel::onTrackStalled);
    connect(c, &IRecorderFacade::trackReopening, this, &RecorderViewModel::onTrackReopening);
    connect(c, &IRecorderFacade::trackReopenFailed, this,
            [this](const QString& name) { onReopenEnded(name, false); });
    connect(c, &IRecorderFacade::trackResumed, this, &RecorderViewModel::onTrackResumed);
    connect(c, &IRecorderFacade::routesChanged, this, &RecorderViewModel::updateRoutes);
    connect(c, &IRecorderFacade::sessionChanged, this, &RecorderViewModel::reloadFromFacade);

    // Playback routes + hot-plug (where the platform has them), then the level monitoring
    // (refresh -> devicesChanged -> rebuildDevices).
    c->startWatching();
    c->startLevelMonitoring();
    if (m_rows.isEmpty()) rebuildDevices();
    onRecordingState(c->state());
    applySession(c->session());
    // A recorder window created while a track was already stalled.
    if (c->state() == RecordingState::Recording) {
        const QMap<QString, qint64> stalledNow = c->stalledTracks();
        for (auto it = stalledNow.cbegin(); it != stalledNow.cend(); ++it) onTrackStalled(it.key(), it.value());
        for (const QString& name : c->reopeningDeviceNames())
            onTrackReopening(name, QStringLiteral("device-lost"));
    }
    m_tick.start();
}

// The fixture loaded another session: devices, selection and the whole session again.
void RecorderViewModel::reloadFromFacade()
{
    m_knownDevices = m_facade->knownDevices();
    m_selectionLoaded = false;   // the source decides the selection again
    m_reloading = true;
    rebuildDevices();
    m_reloading = false;
    const tanara::recording::RecorderSession session = m_facade->session();
    applySession(session);
    m_state.clear();
    setState(session.done ? QStringLiteral("done")
             : m_facade->state() == RecordingState::Recording ? QStringLiteral("recording")
             : m_rows.isEmpty() ? QStringLiteral("noDevice") : QStringLiteral("idle"));
}

void RecorderViewModel::applySession(const tanara::recording::RecorderSession& s)
{
    if (!s.title.isEmpty()) {
        m_title = s.title;
        m_titleAuto = false;
        emit titleChanged();
    } else if (s.automaticWhen.isValid()) {
        m_title = automaticTitle(s.automaticApp, s.automaticWhen);
        m_titleAuto = true;
        emit titleChanged();
    }
    m_tags.clear();
    for (const tanara::recording::RecorderTag& t : s.tags) m_tags.append({t.id, t.name});
    emit tagsChanged();

    m_elapsedMs = s.elapsedMs;
    emit elapsedChanged();

    m_askVisible = !s.askText.isEmpty();
    m_askText = s.askText;
    emit askChanged();

    m_doneSummary = s.done ? s.doneSummary : QString();
    m_doneMeetingId = s.done ? s.doneMeetingId : QString();
    m_doneProblem.clear();
    emit doneChanged();
    if (s.done) setState(QStringLiteral("done"));
}

void RecorderViewModel::rebuildDevices()
{
    if (!m_facade) return;
    const QVector<AudioDeviceInfo> present = m_facade->captureDevices();
    const QStringList recNames = m_facade->recordingDeviceNames();
    const QStringList closed = m_facade->disconnectedDeviceNames();

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
    const bool autoAll = m_facade->autoRecordAllDevices();
    const QStringList last = m_facade->lastUsedDeviceNames();
    auto wantSelected = [&](const AudioDeviceInfo& d) {
        const int old = rowOf(d.name);
        if (old >= 0 && m_selectionLoaded) return m_rows.at(old).selected;
        if (last.contains(d.name)) return true;
        const bool isNew = !m_knownDevices.contains(d.name);
        return (isNew || last.isEmpty()) && autoAll && d.kind != TrackKind::Other;
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
        r.friendly = m_facade->friendlyName(d.name);   // the user's name, otherwise the abbreviated one
        r.group = d.kind == TrackKind::Mic ? 0 : d.kind == TrackKind::Loopback ? 1 : 2;
        r.recorded = recNames.contains(d.name);
        r.disconnected = false;
        applyPin(r);
        rows.push_back(r);
    }
    for (const QString& name : extras) {
        Row r;
        const int old = rowOf(name);
        if (old >= 0) r = m_rows.at(old);
        else {
            r.info.name = name;
            r.friendly = m_facade->friendlyName(name);
            r.info.kind = name.contains(QStringLiteral("monitor"), Qt::CaseInsensitive)
                              ? TrackKind::Loopback : TrackKind::Mic;
            r.group = r.info.kind == TrackKind::Mic ? 0 : 1;
        }
        r.disconnected = closed.contains(name);
        r.recorded = !r.disconnected;
        r.selected = true;
        r.rms = r.peak = 0.f;
        r.level = 0; r.peakSeg = -1;
        r.pinned = false;
        rows.push_back(r);
    }
    // The facade lists the devices in display order (default first per group); the rows of
    // devices that are gone only join the end of their group.
    std::stable_sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) { return x.group < y.group; });

    m_model.beginResetModel();
    m_rows = rows;
    m_model.endResetModel();
    m_selectionLoaded = true;

    bool knownChanged = false;
    for (const QString& n : presentNames)
        if (!m_knownDevices.contains(n)) { m_knownDevices << n; knownChanged = true; }
    if (knownChanged)
        m_facade->setKnownDevices(m_knownDevices);

    updateRoutes();
    for (int i = 0; i < m_rows.size(); ++i) computeStatus(m_rows[i], now);
    emit countsChanged();
    if (!m_reloading && (m_state == QLatin1String("idle") || m_state == QLatin1String("noDevice")))
        setState(m_rows.isEmpty() ? QStringLiteral("noDevice") : QStringLiteral("idle"));
}

void RecorderViewModel::applyPin(Row& r) const
{
    const std::optional<tanara::recording::DevicePin> pin = m_facade->pinnedDisplay(r.info.name);
    r.pinned = pin.has_value();
    if (!pin) return;
    r.level = pin->level;
    r.peakSeg = pin->peakSegment;
    r.status = pin->status;
    r.statusText = pin->statusText;
}

void RecorderViewModel::updateRoutes()
{
    if (!m_facade) return;
    for (int i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        const QString app = r.group == 1 ? m_facade->appForOutput(r.info.name) : QString();
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
    if (r.pinned) return;
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
    if (!m_reopening.isEmpty() || m_resumedAt >= 0) updateResumeText();
    for (int i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        if (r.pinned) continue;
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
    if (!m_facade) return;
    QStringList names;
    for (const Row& r : m_rows) if (r.selected && !r.disconnected) names << r.info.name;
    m_facade->setLastUsedDeviceNames(names);
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
    if (tanara::library::meetingnotes::parseAutoCallNote(context, &autoApp)) {
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
    if (!deviceIndexes.isEmpty() && m_facade) {
        const QVector<AudioDeviceInfo> all = m_facade->captureDevices();
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
    if (!m_facade) return;
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
    m_facade->start(m_title, sel);
    syncRecordingTags();                       // tags chosen before starting (start clears them)
}

void RecorderViewModel::stop()
{
    if (m_state != QLatin1String("recording")) return;
    if (m_facade) m_facade->stop();
}

void RecorderViewModel::toggleDevice(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    Row& r = m_rows[row];
    if (r.disconnected) return;                          // disconnected device: nothing to switch
    if (m_state == QLatin1String("idle")) {
        r.selected = !r.selected;
        r.silentSince = m_clock.elapsed();
        if (!r.pinned) computeStatus(r, m_clock.elapsed());
        else { r.status.clear(); r.statusText.clear(); }
        refreshRow(row, false);
        emit countsChanged();
        persistSelection();
    } else if (m_state == QLatin1String("recording")) {
        // During recording: ON -> its track starts from that moment (a new segment for a device
        // switched off earlier); OFF -> the track's file closes here, the recording goes on.
        if (!m_facade) return;
        if (r.recorded) {
            if (!m_facade->stopDevice(r.info.name)) {
                m_errorText = tr("Az utolsó rögzített sáv nem kapcsolható ki — a felvételt a Leállítás gombbal fejezheted be.");
                emit errorChanged();
            }
            return;
        }
        if (!m_facade->addDevice(r.info)) {
            m_errorText = tr("Nem sikerült sávot indítani ezen az eszközön: %1").arg(r.friendly);
            emit errorChanged();
        }
    }
}

void RecorderViewModel::rescan()
{
    clearError();
    if (m_facade) m_facade->refreshDevices();
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
    if (m_facade) m_facade->beginNewSession();   // a fixture returns to its start state (reloads us)
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
    if (n.isEmpty() || !tagsEditable() || !m_facade) return false;
    const tanara::recording::RecorderTag t = m_facade->createTag(n);   // for an existing key, the existing tag
    if (!t.isValid()) return false;
    const TagRef ref{t.id, t.name};
    for (const TagRef& existing : std::as_const(m_tags))
        if (existing.id == ref.id) return false;
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
    if (!m_facade || m_state == QLatin1String("done")) return;
    m_facade->setRecordingTags(tagIds());
}

void RecorderViewModel::refreshTagNames()
{
    if (!m_facade) return;
    bool changed = false;
    for (int i = 0; i < m_tags.size(); ++i) {
        const std::optional<tanara::recording::RecorderTag> t = m_facade->tag(m_tags.at(i).id);
        if (!t) { m_tags.removeAt(i--); changed = true; continue; }   // deleted meanwhile
        if (t->name != m_tags.at(i).name) { m_tags[i].name = t->name; changed = true; }
    }
    if (!changed) return;
    emit tagsChanged();
    syncRecordingTags();
}

// ---- recording state ------------------------------------------------------------------

void RecorderViewModel::onRecordingState(RecordingState st)
{
    if (!m_facade) return;
    if (st == RecordingState::Recording) {
        const QStringList rec = m_facade->recordingDeviceNames();
        const qint64 now = m_clock.elapsed();
        for (Row& r : m_rows) {
            r.recorded = rec.contains(r.info.name);
            r.silentSince = now;
        }
        clearError();
        syncRecordingTags();
        setState(QStringLiteral("recording"));
        emit recordingStarted(m_facade->currentFolder());
    } else if (st == RecordingState::Stopping || st == RecordingState::Encoding) {
        if (m_askVisible) { m_askVisible = false; emit askChanged(); }
        clearStalls();
        clearResume();
        setState(QStringLiteral("stopping"));
    } else if (m_state == QLatin1String("recording") || m_state == QLatin1String("stopping")) {
        // Idle without the "done" signal: the recording was not created / was interrupted (the error
        // came via errorOccurred). Only recordingFinished leads to the done state.
        for (Row& r : m_rows) { r.recorded = false; r.rms = r.peak = 0.f; }
        clearStalls();
        clearResume();
        setState(m_rows.isEmpty() ? QStringLiteral("noDevice") : QStringLiteral("idle"));
        rebuildDevices();
    }
}

void RecorderViewModel::onFinished(const tanara::recording::RecordingOutcome& outcome)
{
    // We get here when the core has closed the tracks' encoders AND saved the meeting; the track
    // files were checked before "Saved" (outcome.tracksOk / tracksMissing).
    const int ok = outcome.tracksOk;
    const qint64 s = outcome.durationMs / 1000;
    const QString dur = s >= 3600
        ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
              .arg(s % 60, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    m_doneSummary = dur + QStringLiteral(" · ") + tr("%n sáv", nullptr, ok);
    m_doneMeetingId = outcome.meetingId;
    m_doneProblem = ok == 0 ? tr("Egyetlen sáv hangfájlja sem jött létre.")
                  : outcome.tracksMissing > 0 ? tr("%n sáv hangfájlja hiányzik.", nullptr, outcome.tracksMissing)
                  : QString();
    emit doneChanged();

    if (m_facade) {
        if (outcome.title != m_title && !m_title.isEmpty())
            m_facade->renameMeeting(outcome.meetingId, m_title);     // rename during recording
        if (!m_context.isEmpty())
            m_facade->setMeetingContextNote(outcome.meetingId, m_context);
        if (!m_appName.isEmpty())
            m_facade->setMeetingDetectedCall(outcome.meetingId, m_appName);   // "Detected call: ..."
    }
    for (Row& r : m_rows) { r.recorded = false; r.rms = r.peak = 0.f; }
    const bool hadDisconnected = std::any_of(m_rows.cbegin(), m_rows.cend(),
                                             [](const Row& r) { return r.disconnected; });
    setState(QStringLiteral("done"));
    if (hadDisconnected) { m_selectionLoaded = true; rebuildDevices(); }
    emit recordingFinished(outcome.meetingId);
}

// ---- stalled tracks (no data) -----------------------------------------------------------------

void RecorderViewModel::onTrackStalled(const QString& device, qint64 sinceMs)
{
    if (m_stalled.contains(device)) return;
    m_stalled.insert(device, sinceMs);
    const int row = rowOf(device);
    if (row >= 0)
        emit m_model.dataChanged(m_model.index(row), m_model.index(row), {RecorderDeviceModel::StalledRole});
    updateStallWarning(m_elapsedMs);
    const bool loopback = row >= 0 && m_rows.at(row).info.kind == TrackKind::Loopback;
    emit stallRaised(loopback ? tr("A hívás hangja nem jön") : tr("A mikrofon nem ad hangot"), m_stallWarning);
}

void RecorderViewModel::onTrackResumed(const QString& device)
{
    if (m_stalled.remove(device) == 0) return;
    const int row = rowOf(device);
    if (row >= 0)
        emit m_model.dataChanged(m_model.index(row), m_model.index(row), {RecorderDeviceModel::StalledRole});
    updateStallWarning(m_elapsedMs);
}

// One line for the longest stalled track (the others have their warn dot in the device list).
void RecorderViewModel::updateStallWarning(qint64 elapsedMs)
{
    QString text;
    if (!m_stalled.isEmpty()) {
        auto oldest = m_stalled.cbegin();
        for (auto it = m_stalled.cbegin(); it != m_stalled.cend(); ++it)
            if (it.value() < oldest.value()) oldest = it;
        const int row = rowOf(oldest.key());
        const QString shown = (row >= 0 && !m_rows.at(row).friendly.isEmpty()) ? m_rows.at(row).friendly
                                                                                : oldest.key();
        const qint64 secs = qMax<qint64>(0, elapsedMs - oldest.value()) / 1000;
        const bool loopback = row >= 0 && m_rows.at(row).info.kind == TrackKind::Loopback;
        text = loopback ? tr("A hívás hangja (%1) %2 másodperce nem jön").arg(shown).arg(secs)
                        : tr("A mikrofon (%1) %2 másodperce nem ad hangot").arg(shown).arg(secs);
    }
    if (text == m_stallWarning) return;
    m_stallWarning = text;
    emit stallChanged();
}

void RecorderViewModel::clearStalls()
{
    if (m_stalled.isEmpty()) return;
    const QStringList names = m_stalled.keys();
    m_stalled.clear();
    for (const QString& n : names) {
        const int row = rowOf(n);
        if (row >= 0)
            emit m_model.dataChanged(m_model.index(row), m_model.index(row), {RecorderDeviceModel::StalledRole});
    }
    updateStallWarning(m_elapsedMs);
}

// ---- resume after a sleep / a lost device -----------------------------------------------------

QString RecorderViewModel::shownName(const QString& device) const
{
    const int row = rowOf(device);
    return (row >= 0 && !m_rows.at(row).friendly.isEmpty()) ? m_rows.at(row).friendly : device;
}

void RecorderViewModel::onTrackReopening(const QString& device, const QString& reason)
{
    const bool first = m_reopening.isEmpty();
    if (first) {
        m_reopenSince = m_clock.elapsed();
        m_resumedAt = -1;
    }
    m_reopening.insert(device, reason);
    updateResumeText();
    if (first) emit interruptionRaised(tr("A felvétel megszakadt"), m_resumeText);
}

void RecorderViewModel::onReopenEnded(const QString& device, bool resumed)
{
    if (m_reopening.remove(device) == 0) return;
    if (!m_reopening.isEmpty()) {
        updateResumeText();
        return;
    }
    if (resumed) {
        m_resumedAt = m_clock.elapsed();
        updateResumeText();
        emit interruptionRaised(tr("A felvétel folytatódik"), m_resumeText);
    } else {
        m_resumedAt = -1;
        updateResumeText();   // the give-up message comes on the error path
        emit interruptionRaised(tr("A felvétel megszakadt"),
                                tr("A(z) %1 eszköz nem tért vissza, a sávja lezárva marad.").arg(shownName(device)));
    }
}

void RecorderViewModel::updateResumeText()
{
    const qint64 now = m_clock.elapsed();
    QString text;
    if (!m_reopening.isEmpty()) {
        const bool slept = std::any_of(m_reopening.cbegin(), m_reopening.cend(),
                                       [](const QString& r) { return r == QLatin1String("sleep"); });
        const qint64 secs = qMax<qint64>(0, now - m_reopenSince) / 1000;
        text = (slept ? tr("A felvétel megszakadt (a gép aludt), folytatás… %1 mp")
                      : tr("A felvétel megszakadt (az eszköz eltűnt), folytatás… %1 mp")).arg(secs);
    } else if (m_resumedAt >= 0) {
        if (now - m_resumedAt < 10000) text = tr("Folytatva új szakaszban");
        else m_resumedAt = -1;
    }
    if (text == m_resumeText) return;
    m_resumeText = text;
    emit resumeChanged();
}

void RecorderViewModel::clearResume()
{
    if (m_reopening.isEmpty() && m_resumedAt < 0 && m_resumeText.isEmpty()) return;
    m_reopening.clear();
    m_resumedAt = -1;
    m_resumeText.clear();
    emit resumeChanged();
}

} // namespace tanara_qml

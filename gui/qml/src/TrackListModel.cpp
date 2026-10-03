#include "TrackListModel.h"

#include "WaveformItem.h"

#include "AppContext.h"
#include "JobSupport.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/store/MeetingStore.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace tanara_qml {

using namespace tanara;

namespace {

const QString kMixdownId = QStringLiteral("mixdown");

// Kitalált, de „beszédszerű” hullámforma a demóhoz (determinisztikus).
QList<qreal> demoPeaks(int seed, qreal level)
{
    QList<qreal> out;
    quint32 x = 2463534242u + quint32(seed) * 7919u;
    qreal env = 0.4;
    for (int i = 0; i < 160; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        const qreal r = (x % 1000) / 1000.0;
        if (i % 7 == 0) env = 0.03 + 0.97 * std::pow(((x >> 8) % 1000) / 1000.0, 2.0);
        out.append(level * env * (0.35 + 0.65 * r));
    }
    return out;
}

} // namespace

TrackListModel::TrackListModel(QObject* parent) : QAbstractListModel(parent)
{
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, [this]() { connectController(); reload(); });
    connect(ctx, &AppContext::demoChanged, this, &TrackListModel::reload);
    connectController();
    reload();
}

AppController* TrackListModel::app() const { return jobsupport::resolveController(m_injected); }
QObject* TrackListModel::controllerObject() const { return app(); }
bool TrackListModel::demo() const { return jobsupport::demoMode(app()); }

void TrackListModel::setController(QObject* controller)
{
    if (m_injected == controller)
        return;
    m_injected = controller;
    connectController();
    emit controllerChanged();
    reload();
}

void TrackListModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    // A régi meeting függő hullámforma-kéréseit eldobjuk (ne számoljon a háttérben feleslegesen).
    if (AppController* c = app(); c && !m_meetingId.isEmpty() && !jobsupport::demoMode(c))
        c->waveforms()->cancel(m_meetingId);
    m_meetingId = id;
    m_waveformsRequested = false;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    m_mixdownPeaks.clear();
    m_mixdownPeaksLoading = false;
    m_mixdownDurationMs = -1;
    emit meetingIdChanged();
    reload();
}

void TrackListModel::setDemoState(const QString& state)
{
    if (state == m_demoState)
        return;
    m_demoState = state;
    emit demoStateChanged();
    reload();
}

void TrackListModel::connectController()
{
    AppController* c = app();
    if (c == m_connected)
        return;
    for (const QMetaObject::Connection& conn : std::as_const(m_connections))
        disconnect(conn);
    m_connections.clear();
    m_connected = c;
    if (!c)
        return;

    auto mine = [this](const QString& id) { return !m_meetingId.isEmpty() && id == m_meetingId; };
    m_connections << connect(c, &AppController::tracksChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reload(); });
    // Az eszközök átnevezése (Beállítások) a sávok barátságos nevét is megváltoztatja.
    if (c->settings()) {
        m_deviceNames = c->settings()->settings().deviceNames;
        m_connections << connect(c->settings(), &tanara::SettingsManager::settingsChanged, this, [this] {
            AppController* a = app();
            if (!a || !a->settings()) return;
            const QMap<QString, QString> names = a->settings()->settings().deviceNames;
            if (names == m_deviceNames) return;
            m_deviceNames = names;
            reload();
        });
    }
    m_connections << connect(c, &AppController::mixdownUpdated, this,
                             [this, mine](const QString& id, bool ok) {
        if (!mine(id)) return;
        if (ok) {                       // új keverék → a régi csúcsok érvénytelenek
            m_mixdownPeaks.clear();
            m_mixdownDurationMs = -1;
        }
        reload();
        if (ok && m_waveformsRequested)
            requestWaveforms();
    });
    m_connections << connect(c, &AppController::mixdownProgress, this,
                             [this, mine](const QString& id, int pct) {
        if (!mine(id) || pct == m_mixdownPercent) return;
        m_mixdownPercent = pct;
        emit mixdownPercentChanged();
    });
    m_connections << connect(c->jobs(), &MeetingJobTracker::stateChanged, this,
                             [this, mine](const QString& id) { if (mine(id)) reloadMixdown(); });
    m_connections << connect(c->waveforms(), &WaveformService::peaksReady, this,
                             [this, mine](const QString& id, const QString& trackId, const TrackPeaks& p) {
        if (mine(id)) applyPeaks(trackId, p, false);
    });
    m_connections << connect(c->waveforms(), &WaveformService::peaksFailed, this,
                             [this, mine](const QString& id, const QString& trackId, const QString&) {
        if (mine(id)) applyPeaks(trackId, TrackPeaks{}, true);
    });
}

QList<qreal> TrackListModel::toList(const QVector<float>& v)
{
    QList<qreal> out;
    out.reserve(v.size());
    for (float f : v) out.append(qreal(f));
    return out;
}

QString TrackListModel::iconFor(TrackRole role)
{
    switch (role) {
    case TrackRole::OwnMic:      return QStringLiteral("mic");
    case TrackRole::CallAudio:   return QStringLiteral("monitor-speaker");
    case TrackRole::SystemAudio: return QStringLiteral("speaker");
    case TrackRole::Other:       break;
    }
    return QStringLiteral("audio-lines");
}

int TrackListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row& r = m_rows.at(index.row());
    switch (role) {
    case TrackIdRole:      return r.view.track.id;
    case Qt::DisplayRole:
    case DisplayNameRole:  return r.view.displayName;
    case FriendlyNameRole: return r.view.friendlyName;
    case RenamedRole:      return r.view.renamed;
    case RawNameRole: {
        const QString file = QFileInfo(r.view.track.file).fileName();
        if (r.view.rawDeviceName.isEmpty()) return file;
        return file.isEmpty() ? r.view.rawDeviceName
                              : r.view.rawDeviceName + QStringLiteral(" · ") + file;
    }
    case IconNameRole:     return iconFor(r.view.role);
    case ActiveRole:       return r.view.track.active;
    case MissingRole:      return r.view.fileMissing;
    case PathRole:         return r.view.absolutePath;
    case DurationTextRole:
        if (r.view.fileMissing) return QStringLiteral("–");
        return jobsupport::formatDuration(r.durationMs >= 0 ? r.durationMs : m_meetingDurationMs);
    case PeaksRole:        return QVariant::fromValue(r.peaks);
    case PeaksStateRole:   return r.peaksState;
    case ColorIndexRole:   return r.colorIndex;
    default: break;
    }
    return {};
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        {TrackIdRole, "trackId"}, {DisplayNameRole, "displayName"}, {FriendlyNameRole, "friendlyName"},
        {RenamedRole, "renamed"}, {RawNameRole, "rawName"}, {IconNameRole, "iconName"},
        {ActiveRole, "active"}, {MissingRole, "missing"}, {PathRole, "path"},
        {DurationTextRole, "durationText"}, {PeaksRole, "peaks"}, {PeaksStateRole, "peaksState"},
        {ColorIndexRole, "colorIndex"},
    };
}

int TrackListModel::activeCount() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(),
                             [](const Row& r) { return r.view.track.active && !r.view.fileMissing; }));
}

int TrackListModel::droppedCount() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(),
                             [](const Row& r) { return !r.view.track.active; }));
}

// A rajzolt szintek: az effektív (RMS) szint, ha van — hosszú felvételnél a vödrönkénti csúcs
// mindenhol közel maximális, az RMS viszont megmutatja, hol van beszéd és hol csend.
QList<qreal> TrackListModel::levelsOf(const TrackPeaks& peaks)
{
    return toList(peaks.rms.size() == peaks.peaks.size() && !peaks.rms.isEmpty() ? peaks.rms : peaks.peaks);
}

qreal TrackListModel::peakReference() const
{
    // Közös skála a sávokra: a szintek felső percentilise (nem a maximum — egyetlen hangos
    // pillanat ne nyomja össze az egész képet). A leghangosabb részek így telt magasságúak.
    QList<qreal> all;
    for (const Row& r : m_rows) all += r.peaks;
    return std::max(WaveformItem::referenceLevel(all), 0.004);   // csendes felvételnél se nagyítsuk fel a zajt
}

qreal TrackListModel::rowReference(int row) const
{
    const qreal common = peakReference();
    if (row < 0 || row >= m_rows.size())
        return common;
    return std::max(WaveformItem::referenceLevel(m_rows[row].peaks), common * 0.3);
}

QString TrackListModel::mixdownDurationText() const
{
    return jobsupport::formatDuration(m_mixdownDurationMs >= 0 ? m_mixdownDurationMs : m_meetingDurationMs);
}

void TrackListModel::reload()
{
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        loadDemo();
        return;
    }
    QVector<Row> fresh;
    m_meetingDurationMs = 0;
    m_mixdownPath.clear();
    if (!m_meetingId.isEmpty()) {
        const Meeting m = c->store()->load(m_meetingId);
        m_meetingDurationMs = m.durationMs;
        if (!m.mixdownFile.isEmpty())
            m_mixdownPath = QDir(m.folder).filePath(m.mixdownFile);
        const QVector<TrackView> views = TrackCatalog::tracks(m);
        int color = 0;
        for (const TrackView& v : views) {
            Row row;
            row.view = v;
            row.durationMs = v.durationMs;
            row.colorIndex = color++;
            // A korábban betöltött csúcsok megmaradnak (ugyanaz a sáv, ugyanaz a fájl).
            const auto old = std::find_if(m_rows.cbegin(), m_rows.cend(), [&v](const Row& r) {
                return r.view.track.id == v.track.id && r.view.absolutePath == v.absolutePath;
            });
            if (old != m_rows.cend() && !v.fileMissing) {
                row.peaks = old->peaks;
                row.peaksState = old->peaksState;
                if (row.durationMs < 0) row.durationMs = old->durationMs;
            } else if (!v.fileMissing) {
                // Érvényes gyorsítótár → azonnal megvan (kicsi fájl, szinkron olvasás).
                const TrackPeaks cached = WaveformService::loadCached(v.absolutePath);
                if (cached.isValid()) {
                    row.peaks = levelsOf(cached);
                    row.peaksState = QStringLiteral("ready");
                    if (row.durationMs < 0) row.durationMs = cached.durationMs;
                } else if (m_waveformsRequested) {
                    row.peaksState = QStringLiteral("loading");
                }
            }
            fresh.append(row);
        }
        if (m_mixdownPeaks.isEmpty() && !m_mixdownPath.isEmpty() && QFileInfo::exists(m_mixdownPath)) {
            const TrackPeaks cached = WaveformService::loadCached(m_mixdownPath);
            if (cached.isValid()) {
                m_mixdownPeaks = levelsOf(cached);
                m_mixdownDurationMs = cached.durationMs;
                m_mixdownPeaksLoading = false;
            }
        }
    }

    bool sameIds = fresh.size() == m_rows.size();
    for (int i = 0; sameIds && i < fresh.size(); ++i)
        sameIds = fresh[i].view.track.id == m_rows[i].view.track.id;
    if (sameIds) {
        m_rows = fresh;
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(int(m_rows.size()) - 1));
    } else {
        beginResetModel();
        m_rows = fresh;
        endResetModel();
    }
    reloadMixdown();
    emit peaksChanged();
    emit changed();
    // Új / megtalált fájl: ha a fül már kérte a csúcsokat, a hiányzókat most is kérjük.
    if (m_waveformsRequested)
        requestWaveforms();
}

void TrackListModel::reloadMixdown()
{
    AppController* c = app();
    if (jobsupport::demoMode(c) || m_meetingId.isEmpty())
        return;
    const Meeting m = c->store()->load(m_meetingId);
    const MeetingProcessingState ps = c->jobs()->state(m);
    const int oldPct = m_mixdownPercent;
    m_mixdownCancellable = false;
    m_mixdownCancelling = false;
    if (ps.mixdownRunning) {
        m_mixdownState = QStringLiteral("running");
        m_mixdownPercent = ps.mixdownPercent;
        const JobProgress job = c->jobs()->job(m_meetingId, JobKind::Mixdown);
        m_mixdownCancellable = job.isValid() && job.cancellable;
        m_mixdownCancelling = job.isValid() && job.cancelling;
    } else {
        m_mixdownPercent = -1;
        const QString path = m.mixdownFile.isEmpty() ? QString() : QDir(m.folder).filePath(m.mixdownFile);
        if (path.isEmpty() || !QFileInfo::exists(path))
            m_mixdownState = QStringLiteral("none");
        else
            m_mixdownState = m.mixdownDirty ? QStringLiteral("stale") : QStringLiteral("ready");
    }
    if (oldPct != m_mixdownPercent)
        emit mixdownPercentChanged();
    emit changed();
}

void TrackListModel::applyPeaks(const QString& trackId, const TrackPeaks& peaks, bool failed)
{
    if (trackId == kMixdownId) {
        m_mixdownPeaksLoading = false;
        if (!failed) {
            m_mixdownPeaks = levelsOf(peaks);
            m_mixdownDurationMs = peaks.durationMs;
        }
        emit peaksChanged();
        return;
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        if (r.view.track.id != trackId) continue;
        if (failed) {
            r.peaksState = QStringLiteral("failed");
        } else {
            r.peaks = levelsOf(peaks);
            r.peaksState = QStringLiteral("ready");
            r.durationMs = peaks.durationMs;
        }
        emit dataChanged(index(i), index(i), {PeaksRole, PeaksStateRole, DurationTextRole});
        emit peaksChanged();   // a közös skála is változhatott
        return;
    }
}

void TrackListModel::requestWaveforms()
{
    m_waveformsRequested = true;
    AppController* c = app();
    if (jobsupport::demoMode(c) || m_meetingId.isEmpty())
        return;
    bool any = false;
    for (int i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        if (r.view.fileMissing || r.peaksState == QLatin1String("ready")
            || r.peaksState == QLatin1String("failed"))
            continue;
        any = true;
        if (r.peaksState != QLatin1String("loading")) {
            r.peaksState = QStringLiteral("loading");
            emit dataChanged(index(i), index(i), {PeaksStateRole});
        }
    }
    const bool mixMissing = m_mixdownPeaks.isEmpty() && !m_mixdownPath.isEmpty()
                         && QFileInfo::exists(m_mixdownPath) && m_mixdownState != QLatin1String("running");
    if (mixMissing && !m_mixdownPeaksLoading) {
        m_mixdownPeaksLoading = true;
        emit peaksChanged();
    }
    // A szolgáltatás a már futó / váró kérést nem duplázza; a gyorsítótárazottak azonnal jönnek.
    if (any || mixMissing)
        c->requestWaveforms(m_meetingId);
}

bool TrackListModel::rename(int row, const QString& name)
{
    if (row < 0 || row >= m_rows.size())
        return false;
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        Row& r = m_rows[row];
        const QString n = name.trimmed();
        r.view.renamed = !n.isEmpty() && n != r.view.friendlyName;
        r.view.displayName = r.view.renamed ? n : r.view.friendlyName;
        emit dataChanged(index(row), index(row), {DisplayNameRole, RenamedRole, Qt::DisplayRole});
        return true;
    }
    return c->tracks()->renameTrack(m_meetingId, m_rows[row].view.track.id, name.trimmed());
}

void TrackListModel::restore(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        m_rows[row].view.track.active = true;
        emit dataChanged(index(row), index(row), {ActiveRole});
        emit changed();
        return;
    }
    c->restoreTrack(m_meetingId, m_rows[row].view.track.id);
}

QString TrackListModel::trackIdAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows[row].view.track.id : QString();
}

QString TrackListModel::relocate(int row, const QString& filePath)
{
    if (row < 0 || row >= m_rows.size() || filePath.isEmpty())
        return QString();
    return relocateTrack(m_meetingId, m_rows[row].view.track.id, filePath);
}

QString TrackListModel::relocateTrack(const QString& meetingId, const QString& trackId,
                                      const QString& filePath)
{
    if (filePath.isEmpty())
        return QString();
    AppController* c = app();
    if (jobsupport::demoMode(c))
        return QString();
    if (meetingId.isEmpty() || trackId.isEmpty() || meetingId != m_meetingId)
        return tr("Közben másik megbeszélésre váltottál, ezért a fájl nem lett hozzárendelve.");
    QString error;
    if (!c->tracks()->relocateTrack(meetingId, trackId, filePath, &error))
        return error.isEmpty() ? tr("A fájlt nem sikerült a megbeszéléshez rendelni.") : error;
    return QString();
}

int TrackListModel::deleteDropped()
{
    AppController* c = app();
    if (jobsupport::demoMode(c)) {
        int n = 0;
        for (int i = int(m_rows.size()) - 1; i >= 0; --i) {
            if (m_rows[i].view.track.active) continue;
            beginRemoveRows(QModelIndex(), i, i);
            m_rows.removeAt(i);
            endRemoveRows();
            ++n;
        }
        emit changed();
        return n;
    }
    return m_meetingId.isEmpty() ? 0 : deleteDroppedIn(m_meetingId);
}

int TrackListModel::deleteDroppedIn(const QString& meetingId)
{
    AppController* c = app();
    if (jobsupport::demoMode(c))
        return deleteDropped();
    // A megerősítés EHHEZ a megbeszéléshez szólt: ha közben másik lett a kijelölt, nem törlünk.
    if (meetingId.isEmpty() || meetingId != m_meetingId)
        return -1;
    return c->tracks()->deleteDroppedTracks(meetingId);
}

void TrackListModel::refreshMixdown()
{
    AppController* c = app();
    if (jobsupport::demoMode(c) || m_meetingId.isEmpty())
        return;
    c->regenerateMixdown(m_meetingId);
}

// Kitalált mintaadat (M09).
void TrackListModel::loadDemo()
{
    const QString st = m_demoState.isEmpty() ? QStringLiteral("default") : m_demoState;
    const bool loading = st == QLatin1String("loading");
    m_meetingDurationMs = (30 * 60 + 34) * 1000;

    auto make = [&](const char* id, TrackRole role, const QString& name, const QString& raw,
                    const QString& file, bool active, bool missing, int color, qreal level) {
        Row r;
        r.view.track.id = QString::fromLatin1(id);
        r.view.track.file = file;
        r.view.track.active = active;
        r.view.role = role;
        r.view.displayName = r.view.friendlyName = name;
        r.view.rawDeviceName = raw;
        r.view.fileMissing = missing;
        r.view.absolutePath = QStringLiteral("/demo/") + file;
        r.colorIndex = color;
        if (missing) {
            r.peaksState = QStringLiteral("none");
        } else if (loading) {
            r.peaksState = QStringLiteral("loading");
        } else {
            r.peaks = demoPeaks(color + 1, level);
            r.peaksState = QStringLiteral("ready");
        }
        return r;
    };
    QVector<Row> rows;
    rows << make("mic", TrackRole::OwnMic, tr("Saját mikrofon"),
                 QStringLiteral("Jabra Evolve2 65 · alsa_input.usb-GN_Netcom_Jabra_Evolve2_65-00.mono-fallback"),
                 QStringLiteral("track-01.ogg"), true, false, 0, 0.8)
         << make("call", TrackRole::CallAudio, tr("Hívás hangja"),
                 QStringLiteral("Microsoft Teams · Monitor of Built-in Audio Analog Stereo"),
                 QStringLiteral("track-02.ogg"), true, false, 1, 0.9);
    if (st != QLatin1String("idle")) {
        rows << make("system", TrackRole::SystemAudio, tr("Rendszerhang"),
                     QStringLiteral("Monitor of HDMI / DisplayPort 1 Output"),
                     QStringLiteral("track-03.ogg"), false, false, 2, 0.004)
             << make("mic2", TrackRole::OwnMic, tr("Második mikrofon"),
                     QStringLiteral("USB PnP Audio Device"),
                     QStringLiteral("track-04.flac"), true, true, 3, 0.0);
    }

    beginResetModel();
    m_rows = rows;
    endResetModel();

    const int oldPct = m_mixdownPercent;
    m_mixdownCancellable = false;
    m_mixdownCancelling = false;
    m_mixdownPeaksLoading = false;
    m_mixdownPeaks.clear();
    m_mixdownDurationMs = m_meetingDurationMs;
    if (st == QLatin1String("idle")) {
        m_mixdownState = QStringLiteral("ready");
        m_mixdownPercent = -1;
        m_mixdownPeaks = demoPeaks(9, 0.85);
    } else if (loading) {
        m_mixdownState = QStringLiteral("stale");
        m_mixdownPercent = -1;
        m_mixdownPeaksLoading = true;
    } else {
        m_mixdownState = QStringLiteral("running");
        m_mixdownPercent = 64;
        m_mixdownCancellable = true;
    }
    if (oldPct != m_mixdownPercent)
        emit mixdownPercentChanged();
    emit peaksChanged();
    emit changed();
}

} // namespace tanara_qml

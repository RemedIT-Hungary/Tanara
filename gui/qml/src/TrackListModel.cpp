#include "TrackListModel.h"

#include "WaveformItem.h"

#include "AppContext.h"
#include "JobSupport.h"
#include "UiServices.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <cmath>

namespace tanara_qml {

using namespace tanara;

namespace {

const QString kMixdownId = QStringLiteral("mixdown");

} // namespace

TrackListModel::TrackListModel(QObject* parent) : QAbstractListModel(parent)
{
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, &TrackListModel::resolveServices);
    connect(ctx, &AppContext::demoChanged, this, &TrackListModel::resolveServices);
    resolveServices();
}

bool TrackListModel::demo() const { return !m_facade || !m_facade->live(); }

void TrackListModel::setFacade(library::ITracksFacade* facade)
{
    m_explicitFacade = true;
    useFacade(facade);
}

// The process's services changed (a controller arrived, App.demo flipped), or the design state.
void TrackListModel::resolveServices()
{
    if (m_explicitFacade) return;
    useFacade(AppContext::instance()->services().tracks(m_demoState, this));
}

bool TrackListModel::mine(const QString& meetingId) const
{
    // The fictional facade announces its changes for the asked (possibly empty) meeting.
    return meetingId == m_meetingId && (!m_meetingId.isEmpty() || demo());
}

void TrackListModel::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    // The old meeting's pending waveform requests are dropped (no needless background computing).
    if (m_facade && !m_meetingId.isEmpty())
        m_facade->cancelWaveforms(m_meetingId);
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
    // A fictional facade serves one design state: a new one for the new state.
    if (demo()) resolveServices();
    else reload();
}

void TrackListModel::useFacade(library::ITracksFacade* facade)
{
    for (const QMetaObject::Connection& conn : std::as_const(m_connections))
        disconnect(conn);
    m_connections.clear();
    if (m_facade && m_facade != facade && m_facade->parent() == this) m_facade->deleteLater();   // a fictional facade of ours
    m_facade = facade;
    if (facade) {
        m_connections << connect(facade, &library::ITracksFacade::tracksChanged, this,
                                 [this](const QString& id) { if (mine(id)) reload(); });
        // Renaming devices (Settings) also changes the tracks' friendly names.
        m_connections << connect(facade, &library::ITracksFacade::trackNamesChanged, this, &TrackListModel::reload);
        m_connections << connect(facade, &library::ITracksFacade::mixdownUpdated, this,
                                 [this](const QString& id, bool ok) {
            if (!mine(id)) return;
            if (ok) {                       // new mix -> the old peaks are invalid
                m_mixdownPeaks.clear();
                m_mixdownDurationMs = -1;
            }
            reload();
            if (ok && m_waveformsRequested)
                requestWaveforms();
        });
        m_connections << connect(facade, &library::ITracksFacade::mixdownProgress, this,
                                 [this](const QString& id, int pct) {
            if (!mine(id) || pct == m_mixdownPercent) return;
            m_mixdownPercent = pct;
            emit mixdownPercentChanged();
        });
        m_connections << connect(facade, &library::ITracksFacade::jobsChanged, this,
                                 [this](const QString& id) { if (mine(id)) reloadMixdown(); });
        m_connections << connect(facade, &library::ITracksFacade::peaksReady, this,
                                 [this](const QString& id, const QString& trackId, const library::TrackPeaks& p) {
            if (mine(id)) applyPeaks(trackId, p, false);
        });
        m_connections << connect(facade, &library::ITracksFacade::peaksFailed, this,
                                 [this](const QString& id, const QString& trackId) {
            if (mine(id)) applyPeaks(trackId, library::TrackPeaks{}, true);
        });
    }
    emit changed();
    reload();
}

QList<qreal> TrackListModel::toList(const QVector<float>& v)
{
    QList<qreal> out;
    out.reserve(v.size());
    for (float f : v) out.append(qreal(f));
    return out;
}

QString TrackListModel::iconFor(library::TrackRole role)
{
    switch (role) {
    case library::TrackRole::OwnMic:      return QStringLiteral("mic");
    case library::TrackRole::CallAudio:   return QStringLiteral("monitor-speaker");
    case library::TrackRole::SystemAudio: return QStringLiteral("speaker");
    case library::TrackRole::Other:       break;
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
        // Second line: [segment N · starts m:ss · ]raw device name · file name.
        const QString file = QFileInfo(r.view.track.file).fileName();
        QStringList parts;
        if (r.view.segment > 0)
            parts << tr("%1. szakasz").arg(r.view.segment);
        // A later-starting track: it started this long after the recording began (Track::startOffsetMs).
        if (r.view.track.startOffsetMs > 0)
            parts << tr("kezdete: %1").arg(jobsupport::formatDuration(r.view.track.startOffsetMs));
        if (!r.view.rawDeviceName.isEmpty()) parts << r.view.rawDeviceName;
        if (!file.isEmpty()) parts << file;
        return parts.join(QStringLiteral(" · "));
    }
    case IconNameRole:     return iconFor(r.view.role);
    case ActiveRole:       return r.view.track.active;
    case MissingRole:      return r.view.fileMissing;
    case PathRole:         return r.view.absolutePath;
    case DurationTextRole:
        // The file's (recorded segment's) length; until known, from its start to the meeting's end.
        if (r.view.fileMissing) return QStringLiteral("–");
        return jobsupport::formatDuration(r.durationMs >= 0 ? r.durationMs
                                          : qMax<qint64>(0, m_meetingDurationMs - r.view.track.startOffsetMs));
    case WaveStartRole:
        return waveExtent(r.view.track.startOffsetMs, r.durationMs, m_meetingDurationMs).first;
    case WaveSpanRole:
        return waveExtent(r.view.track.startOffsetMs, r.durationMs, m_meetingDurationMs).second;
    case SegmentRole:      return r.view.segment;
    case StartOffsetRole:  return r.view.track.startOffsetMs;
    case PeaksRole:        return QVariant::fromValue(r.peaks);
    case PeaksStateRole:   return r.peaksState;
    case ColorIndexRole:   return r.colorIndex;
    case SpeechRatioRole:  return r.view.track.speechRatio;
    case ExcludedReasonRole: return r.view.track.excludedReason;
    case IncludedRole:     return r.view.track.included() && !r.view.fileMissing;
    case GroupIdRole:      return r.groupId.isEmpty() ? r.view.track.id : r.groupId;
    case KindRole:
        return r.view.track.kind == tanara::TrackKind::Mic ? QStringLiteral("mic")
             : r.view.track.kind == tanara::TrackKind::Loopback ? QStringLiteral("loopback")
                                                                 : QStringLiteral("other");
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
        {ColorIndexRole, "colorIndex"}, {WaveStartRole, "waveStart"}, {WaveSpanRole, "waveSpan"},
        {SegmentRole, "segment"}, {StartOffsetRole, "startOffsetMs"},
        {SpeechRatioRole, "speechRatio"}, {ExcludedReasonRole, "excludedReason"},
        {IncludedRole, "included"}, {KindRole, "kind"}, {GroupIdRole, "groupId"},
    };
}

QPair<qreal, qreal> TrackListModel::waveExtent(qint64 startOffsetMs, qint64 fileDurationMs,
                                               qint64 meetingDurationMs)
{
    if (meetingDurationMs <= 0) return {0.0, 1.0};
    const qreal total = qreal(meetingDurationMs);
    const qreal start = std::clamp(qreal(qMax<qint64>(0, startOffsetMs)) / total, 0.0, 1.0);
    const qreal span = fileDurationMs >= 0 ? qreal(fileDurationMs) / total : 1.0 - start;
    return {start, std::clamp(span, 0.0, 1.0 - start)};
}

int TrackListModel::activeCount() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(),
                             [](const Row& r) { return r.view.track.active && !r.view.fileMissing; }));
}

int TrackListModel::includedCount() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(), [](const Row& r) {
        return r.view.track.included() && !r.view.fileMissing;
    }));
}

int TrackListModel::excludedCount() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(), [](const Row& r) {
        return !r.view.track.excludedReason.isEmpty();
    }));
}

int TrackListModel::droppedCount() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(),
                             [](const Row& r) { return !r.view.track.active; }));
}

// The drawn levels: the effective (RMS) level if there is one: for a long recording the
// per-bucket peak is near maximum everywhere, while RMS shows where there is speech and where silence.
QList<qreal> TrackListModel::levelsOf(const library::TrackPeaks& peaks)
{
    return toList(peaks.rms.size() == peaks.peaks.size() && !peaks.rms.isEmpty() ? peaks.rms : peaks.peaks);
}

qreal TrackListModel::peakReference() const
{
    // Common scale for the tracks: the levels' upper percentile (not the maximum, so a single
    // loud moment does not squash the whole picture). The loudest parts thus reach full height.
    QList<qreal> all;
    for (const Row& r : m_rows) all += r.peaks;
    return std::max(WaveformItem::referenceLevel(all), 0.004);   // do not amplify the noise on a quiet recording either
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
    if (!m_facade)
        return;
    if (const std::optional<library::TrackListFixture> f = m_facade->fixture(m_meetingId)) {
        applyFixture(*f);
        return;
    }
    QVector<Row> fresh;
    m_meetingDurationMs = 0;
    m_mixdownPath.clear();
    if (!m_meetingId.isEmpty()) {
        const Meeting m = m_facade->meeting(m_meetingId);
        m_meetingDurationMs = m.durationMs;
        if (!m.mixdownFile.isEmpty())
            m_mixdownPath = QDir(m.folder).filePath(m.mixdownFile);
        const QVector<library::TrackView> views = library::TrackCatalog::tracks(m);
        QHash<QString, QString> groupOf;   // segment id -> the logical track's leading id
        const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
        for (int i = 0; i < m.tracks.size(); ++i)
            groupOf.insert(m.tracks.at(i).id, m.tracks.at(seg.value(i).leader >= 0 ? seg.value(i).leader : i).id);
        int color = 0;
        for (const library::TrackView& v : views) {
            Row row;
            row.view = v;
            row.groupId = groupOf.value(v.track.id, v.track.id);
            row.durationMs = v.durationMs;
            row.colorIndex = color++;
            // Previously loaded peaks stay (same track, same file).
            const auto old = std::find_if(m_rows.cbegin(), m_rows.cend(), [&v](const Row& r) {
                return r.view.track.id == v.track.id && r.view.absolutePath == v.absolutePath;
            });
            if (old != m_rows.cend() && !v.fileMissing) {
                row.peaks = old->peaks;
                row.peaksState = old->peaksState;
                if (row.durationMs < 0) row.durationMs = old->durationMs;
            } else if (!v.fileMissing) {
                // Valid cache -> available immediately (small file, synchronous read).
                const library::TrackPeaks cached = library::WaveformService::loadCached(v.absolutePath);
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
            const library::TrackPeaks cached = library::WaveformService::loadCached(m_mixdownPath);
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
    // New / located file: if the tab already requested the peaks, we request the missing ones now too.
    if (m_waveformsRequested)
        requestWaveforms();
}

void TrackListModel::reloadMixdown()
{
    if (!m_facade || m_meetingId.isEmpty() || m_facade->fixture(m_meetingId))
        return;
    const Meeting m = m_facade->meeting(m_meetingId);
    const library::MixdownJob job = m_facade->mixdownJob(m_meetingId);
    const int oldPct = m_mixdownPercent;
    m_mixdownCancellable = false;
    m_mixdownCancelling = false;
    if (job.running) {
        m_mixdownState = QStringLiteral("running");
        m_mixdownPercent = job.percent;
        m_mixdownCancellable = job.cancellable;
        m_mixdownCancelling = job.cancelling;
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

void TrackListModel::applyPeaks(const QString& trackId, const library::TrackPeaks& peaks, bool failed)
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
        emit dataChanged(index(i), index(i), {PeaksRole, PeaksStateRole, DurationTextRole, WaveSpanRole});
        emit peaksChanged();   // the common scale may have changed too
        return;
    }
}

void TrackListModel::requestWaveforms()
{
    m_waveformsRequested = true;
    if (!m_facade || m_meetingId.isEmpty())
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
    // The service does not duplicate an already running / waiting request; cached ones come immediately.
    if (any || mixMissing)
        m_facade->requestWaveforms(m_meetingId);
}

bool TrackListModel::rename(int row, const QString& name)
{
    if (row < 0 || row >= m_rows.size())
        return false;
    return m_facade && m_facade->renameTrack(m_meetingId, m_rows[row].view.track.id, name.trimmed());
}

void TrackListModel::restore(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    if (m_facade) m_facade->restoreTrack(m_meetingId, m_rows[row].view.track.id);
}

void TrackListModel::setIncluded(int row, bool included)
{
    if (row < 0 || row >= m_rows.size())
        return;
    if (m_facade) m_facade->setTrackIncluded(m_meetingId, m_rows[row].view.track.id, included);
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
    if (demo())
        return QString();
    if (meetingId.isEmpty() || trackId.isEmpty() || meetingId != m_meetingId)
        return tr("Közben másik megbeszélésre váltottál, ezért a fájl nem lett hozzárendelve.");
    QString error;
    if (!m_facade->relocateTrack(meetingId, trackId, filePath, &error))
        return error.isEmpty() ? tr("A fájlt nem sikerült a megbeszéléshez rendelni.") : error;
    return QString();
}

int TrackListModel::deleteDropped()
{
    if (demo())
        return m_facade ? m_facade->deleteDroppedTracks(m_meetingId) : 0;
    return m_meetingId.isEmpty() ? 0 : deleteDroppedIn(m_meetingId);
}

int TrackListModel::deleteDroppedIn(const QString& meetingId)
{
    if (demo())
        return deleteDropped();
    // The confirmation was for THIS meeting: if another became selected meanwhile, we do not delete.
    if (meetingId.isEmpty() || meetingId != m_meetingId)
        return -1;
    return m_facade->deleteDroppedTracks(meetingId);
}

bool TrackListModel::deleteTrackIn(const QString& meetingId, const QString& trackId)
{
    // The fictional facade's list has no meeting of its own: any track of it can go.
    const bool known = std::any_of(m_rows.cbegin(), m_rows.cend(), [&trackId](const Row& r) { return r.view.track.id == trackId; });
    if (!m_facade || trackId.isEmpty() || (demo() ? !known : (meetingId.isEmpty() || meetingId != m_meetingId)))
        return false;
    m_facade->deleteTrack(demo() ? m_meetingId : meetingId, trackId);
    return true;
}

void TrackListModel::refreshMixdown()
{
    if (!m_facade || m_meetingId.isEmpty())
        return;
    m_facade->regenerateMixdown(m_meetingId);
}

// The fictional facade's ready-made list (a design state).
void TrackListModel::applyFixture(const library::TrackListFixture& f)
{
    m_meetingDurationMs = f.meetingDurationMs;
    m_mixdownPath.clear();
    QVector<Row> fresh;
    for (const library::TrackListFixture::Row& fr : f.rows) {
        Row r;
        r.view = fr.view;
        r.peaks = fr.levels;
        r.peaksState = fr.peaksState;
        r.colorIndex = fr.colorIndex;
        fresh.append(r);
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

    const int oldPct = m_mixdownPercent;
    m_mixdownState = f.mixdownState;
    m_mixdownPercent = f.mixdownPercent;
    m_mixdownCancellable = f.mixdownCancellable;
    m_mixdownCancelling = false;
    m_mixdownPeaks = f.mixdownLevels;
    m_mixdownPeaksLoading = f.mixdownPeaksLoading;
    m_mixdownDurationMs = m_meetingDurationMs;
    if (oldPct != m_mixdownPercent)
        emit mixdownPercentChanged();
    emit peaksChanged();
    emit changed();
}

} // namespace tanara_qml

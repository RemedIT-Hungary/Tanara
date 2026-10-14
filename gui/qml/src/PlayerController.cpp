#include "PlayerController.h"
#include "tanara/library/MeetingLayout.h"

#include "AppContext.h"
#include "LibraryDemoData.h"

#include "tanara/app/AppController.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/library/store/MeetingRepository.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

namespace tanara_qml {

namespace {

PlayerController::BackendFactory& backendFactory()
{
    static PlayerController::BackendFactory f;
    return f;
}

// Silent, clock-stepped engine: demo, screenshot and test (no audio output).
class SilentBackend : public PlayerBackend {
public:
    explicit SilentBackend(QObject* parent) : PlayerBackend(parent)
    {
        m_end.setSingleShot(true);
        connect(&m_end, &QTimer::timeout, this, [this] {
            m_base = m_duration;
            m_playing = false;
            emit playingChanged(false);
            emit finished();
        });
    }
    void setSource(const QString&, qint64 knownDurationMs) override
    {
        pause();
        m_base = 0;
        m_duration = qMax<qint64>(0, knownDurationMs);
        emit durationChanged(m_duration);
    }
    void play() override
    {
        if (m_playing || m_duration <= 0) return;
        if (m_base >= m_duration) m_base = 0;
        m_playing = true;
        m_clock.start();
        armEnd();
        emit playingChanged(true);
    }
    void pause() override
    {
        if (!m_playing) return;
        m_base = position();
        m_playing = false;
        m_end.stop();
        emit playingChanged(false);
    }
    void setPosition(qint64 ms) override
    {
        m_base = qBound(qint64(0), ms, m_duration);
        if (m_playing) { m_clock.start(); armEnd(); }
    }
    qint64 position() const override
    {
        if (!m_playing) return m_base;
        return qMin(m_duration, m_base + qint64(m_clock.elapsed() * m_rate));
    }
    qint64 duration() const override { return m_duration; }
    bool isPlaying() const override { return m_playing; }
    void setRate(qreal rate) override
    {
        if (m_playing) { m_base = position(); m_clock.start(); }
        m_rate = rate > 0 ? rate : 1.0;
        if (m_playing) armEnd();
    }
    void setVolume(qreal) override {}

private:
    void armEnd() { m_end.start(int(qMax<qint64>(0, (m_duration - m_base) / m_rate))); }

    QElapsedTimer m_clock;
    QTimer m_end;
    qint64 m_base = 0;
    qint64 m_duration = 0;
    qreal m_rate = 1.0;
    bool m_playing = false;
};

// The meeting's playable audio: the mixdown; if none (old recording, missing mix), the
// largest ACTIVE track that runs from the recording's start (startOffsetMs == 0): segment
// timestamps are in meeting time, so only such a track is time-correct. If there is none,
// the largest active track (plays shifted, but at least audible).
QString meetingAudioPath(const tanara::Meeting& m)
{
    const QString mixdown = QDir(m.folder).filePath(tanara::library::MeetingLayout::mixdownOf(m));
    if (QFileInfo::exists(mixdown))
        return mixdown;
    QString best;
    qint64 bestSize = -1;
    bool bestAligned = false;
    for (const tanara::Track& t : m.tracks) {
        if (!t.active || t.file.isEmpty()) continue;
        const QFileInfo fi(QDir(m.folder).filePath(t.file));
        if (!fi.exists()) continue;
        const bool aligned = t.startOffsetMs <= 0;
        if ((aligned && !bestAligned) || (aligned == bestAligned && fi.size() > bestSize)) {
            bestSize = fi.size();
            bestAligned = aligned;
            best = fi.absoluteFilePath();
        }
    }
    return best;
}

} // namespace

PlayerController::TrackSeek PlayerController::resolveTrackSeek(const QVector<tanara::Track>& tracks,
                                                               const QString& trackId, qint64 meetingMs)
{
    TrackSeek out;
    const QVector<tanara::library::tracknames::SegmentInfo> seg = tanara::library::tracknames::segments(tracks);
    int lead = -1;
    for (int i = 0; i < tracks.size() && lead < 0; ++i)
        if (tracks.at(i).id == trackId) lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
    if (lead < 0) return out;
    // The logical track's segments in order of their start.
    QVector<int> members;
    for (int i = 0; i < tracks.size(); ++i)
        if ((seg.value(i).leader >= 0 ? seg.value(i).leader : i) == lead) members << i;
    std::stable_sort(members.begin(), members.end(), [&tracks](int a, int b) {
        return tracks.at(a).startOffsetMs < tracks.at(b).startOffsetMs;
    });
    int pick = members.first();
    for (int i : members)
        if (qMax<qint64>(0, tracks.at(i).startOffsetMs) <= meetingMs) pick = i;
    out.index = pick;
    out.leaderId = tracks.at(lead).id;
    out.offsetMs = qMax<qint64>(0, tracks.at(pick).startOffsetMs);
    out.fileMs = qMax<qint64>(0, meetingMs - out.offsetMs);
    return out;
}

void PlayerController::setBackendFactory(BackendFactory factory)
{
    backendFactory() = std::move(factory);
}

PlayerBackend* PlayerController::createBackend(QObject* parent)
{
    PlayerBackend* backend = backendFactory() ? backendFactory()(parent) : nullptr;
    return backend ? backend : new SilentBackend(parent);
}

PlayerController::PlayerController(QObject* parent) : QObject(parent)
{
    m_tick.setInterval(40);
    connect(&m_tick, &QTimer::timeout, this, &PlayerController::onTick);

    AppContext* ctx = AppContext::instance();
    m_controller = ctx->controller();
    connect(ctx, &AppContext::controllerChanged, this, [this, ctx] {
        if (m_controllerInjected) return;
        m_controller = ctx->controller();
        attach();
    });
    attach();
}

PlayerController::~PlayerController()
{
    if (m_backend)
        m_backend->pause();
}

void PlayerController::setController(tanara::app::AppController* controller)
{
    m_controllerInjected = true;
    m_controller = controller;
    attach();
}

void PlayerController::attach()
{
    if (m_controller != m_attached) {
        if (m_attached)
            m_attached->disconnect(this);
        m_attached = m_controller;
        if (m_controller) {
            // The mixdown became ready / changed, or the tracks changed -> re-resolve the source
            // (during playback a track change does not interrupt the audio).
            connect(m_controller, &tanara::app::AppController::mixdownUpdated, this,
                    [this](const QString& id, bool) { if (id == m_meetingId) reload(); });
            connect(m_controller, &tanara::app::AppController::tracksChanged, this,
                    [this](const QString& id) { if (id == m_meetingId && !m_playing) reload(); });
        }
    }
    resolveSource();
}

void PlayerController::setMeetingId(const QString& id)
{
    if (id == m_meetingId)
        return;
    stopPreview();
    if (m_backend) {
        m_backend->pause();
        m_backend->setSource(QString(), 0);
    }
    m_mixLoaded = false;
    m_rangeEndMs = -1;
    m_ended = false;
    setPlaying(false);
    m_meetingId = id;
    m_positionMs = 0;
    emit meetingIdChanged();
    emit positionMsChanged();
    resolveSource();
}

void PlayerController::resolveSource()
{
    QString path;
    qint64 duration = 0;
    bool available = false;
    if (!m_meetingId.isEmpty()) {
        if (m_controller && m_controller->store()) {
            const tanara::Meeting m = m_controller->store()->load(m_meetingId);
            if (!m.id.isEmpty()) {
                path = meetingAudioPath(m);
                duration = m.durationMs;
                available = !path.isEmpty();
            }
        } else if (AppContext::instance()->demo()) {
            if (const demo::DemoMeeting* d = demo::find(m_meetingId)) {
                duration = d->entry.durationMs;
                available = true;
            }
        }
    }
    const bool pathChanged = path != m_mixPath;
    m_mixPath = path;
    if (pathChanged)
        m_mixLoaded = false;
    // The length measured by the engine is more accurate; until it is loaded, the one in meeting.json is valid.
    if (!m_mixLoaded && duration != m_durationMs) {
        m_durationMs = duration;
        emit durationMsChanged();
    }
    if (available != m_available || pathChanged) {
        m_available = available;
        emit availableChanged();
    }
}

void PlayerController::reload()
{
    const bool wasPlaying = m_playing;
    if (wasPlaying && m_backend)
        m_backend->pause();
    m_mixLoaded = false;
    resolveSource();
    if (wasPlaying && m_available)
        startMix();
}

void PlayerController::ensureBackend()
{
    if (m_backend)
        return;
    if (backendFactory())
        m_backend = backendFactory()(this);
    if (!m_backend)
        m_backend = new SilentBackend(this);
    m_backend->setRate(m_rate);
    m_backend->setVolume(m_volume);
    connect(m_backend, &PlayerBackend::playingChanged, this, &PlayerController::onBackendPlaying);
    connect(m_backend, &PlayerBackend::durationChanged, this, &PlayerController::onBackendDuration);
    connect(m_backend, &PlayerBackend::finished, this, &PlayerController::onBackendFinished);
    connect(m_backend, &PlayerBackend::errorOccurred, this, &PlayerController::errorOccurred);
}

void PlayerController::loadMix()
{
    ensureBackend();
    if (m_mixLoaded)
        return;
    m_mixLoaded = true;
    m_backend->setSource(m_mixPath, m_durationMs);
    m_backend->setPosition(m_positionMs);
}

void PlayerController::startMix()
{
    if (!m_available)
        return;
    stopPreview();
    loadMix();
    if (m_durationMs > 0 && m_positionMs >= m_durationMs - 50) {
        setPosition(0);          // restarted from the end: from the start
        m_backend->setPosition(0);
    }
    m_ended = false;
    m_backend->play();
}

void PlayerController::play()
{
    m_rangeEndMs = -1;           // plain playback is continuous (leaves the single-utterance mode)
    startMix();
}

void PlayerController::pause()
{
    if (!m_previewPath.isEmpty()) {
        stopPreview();
        return;
    }
    if (m_backend && m_mixLoaded)
        m_backend->pause();
}

void PlayerController::toggle()
{
    if (m_playing)
        pause();
    else
        play();
}

void PlayerController::seek(int ms)
{
    const qint64 clamped = qBound(qint64(0), qint64(ms), qMax(qint64(0), m_durationMs));
    m_rangeEndMs = -1;           // a manual seek cancels the single-utterance mode
    m_ended = false;
    setPosition(clamped);
    if (m_backend && m_mixLoaded && m_previewPath.isEmpty())
        m_backend->setPosition(clamped);
}

void PlayerController::playRange(int startMs, int endMs)
{
    if (!m_available)
        return;
    stopPreview();
    seek(startMs);
    m_rangeEndMs = endMs > startMs ? endMs : -1;
    startMix();
}

void PlayerController::playRange(int startMs, int endMs, int minStartMs, int maxEndMs)
{
    int from = startMs - kPreRollMs;
    if (minStartMs >= 0) from = qMax(from, minStartMs);
    from = qMax(0, qMin(from, startMs));
    int to = endMs + kPostRollMs;
    if (maxEndMs >= 0) to = qMin(to, maxEndMs);
    to = qMax(to, endMs);
    playRange(from, to);
}

void PlayerController::playFile(const QString& absolutePath)
{
    if (absolutePath.isEmpty())
        return;
    // With a real engine only an existing file can play (the silent engine "plays" anything in the demo).
    if (backendFactory() && !QFileInfo::exists(absolutePath)) {
        emit errorOccurred(tr("A hangfájl nem található: %1").arg(absolutePath));
        return;
    }
    m_previewTrackId.clear();
    m_previewSegmentId.clear();
    m_previewOffsetMs = 0;
    m_previewPath.clear();       // always reloaded (from the file's start)
    loadPreview(absolutePath, 0);
}

void PlayerController::loadPreview(const QString& absolutePath, qint64 fileMs)
{
    ensureBackend();
    if (m_playing)
        m_backend->pause();
    m_mixLoaded = false;         // the engine's source is now the preview
    setPlaying(false);
    const bool samePath = absolutePath == m_previewPath;
    m_previewPath = absolutePath;
    m_previewPositionMs = fileMs;
    if (!samePath) {
        m_previewDurationMs = 0;
        // Silent engine (demo / test): the length is the meeting's.
        m_backend->setSource(absolutePath, backendFactory() ? 0 : m_durationMs);
    }
    m_backend->setPosition(fileMs);
    emit previewPathChanged();
    emit previewPositionMsChanged();
    emit previewDurationMsChanged();
    m_backend->play();
}

void PlayerController::playTrack(const QString& trackId, int meetingMs)
{
    if (trackId.isEmpty())
        return;
    QString path;
    TrackSeek seek;
    if (m_controller && m_controller->store() && !m_meetingId.isEmpty()) {
        const tanara::Meeting m = m_controller->store()->load(m_meetingId);
        qint64 at = meetingMs;
        if (at < 0) {
            // Resume: same track -> where it left off; otherwise the track's start.
            seek = resolveTrackSeek(m.tracks, trackId, 0);
            at = seek.leaderId == m_previewTrackId && !m_previewTrackId.isEmpty()
                     ? previewMeetingMs() : seek.offsetMs;
        }
        seek = resolveTrackSeek(m.tracks, trackId, at);
        if (seek.index < 0)
            return;
        const tanara::Track& t = m.tracks.at(seek.index);
        path = QDir(m.folder).filePath(t.file);
        if (backendFactory() && !QFileInfo::exists(path)) {
            emit errorOccurred(tr("A hangfájl nem található: %1").arg(path));
            return;
        }
        m_previewSegmentId = t.id;
    } else if (AppContext::instance()->demo() || !m_controller) {
        // Demo: no file, the silent engine "plays" over the meeting's length.
        seek.leaderId = trackId;
        seek.fileMs = meetingMs >= 0 ? meetingMs
                    : (trackId == m_previewTrackId ? m_previewPositionMs : 0);
        path = QStringLiteral("demo:") + trackId;
        m_previewSegmentId = trackId;
    } else {
        return;
    }
    m_previewTrackId = seek.leaderId;
    m_previewOffsetMs = seek.offsetMs;
    loadPreview(path, seek.fileMs);
}

void PlayerController::toggleTrack(const QString& trackId)
{
    if (!m_previewTrackId.isEmpty() && m_previewTrackId == trackId && !m_previewPath.isEmpty() && m_backend) {
        if (m_previewPlaying) m_backend->pause();
        else m_backend->play();
        return;
    }
    playTrack(trackId, -1);
}

void PlayerController::stopPreview()
{
    if (m_previewPath.isEmpty())
        return;
    if (m_backend)
        m_backend->pause();
    m_previewPath.clear();
    m_previewTrackId.clear();
    m_previewSegmentId.clear();
    m_previewOffsetMs = 0;
    emit previewPathChanged();
    if (m_previewPlaying) {
        m_previewPlaying = false;
        emit previewPlayingChanged();
    }
    m_tick.stop();
    // The mixdown reloads on the next playback / seek (loadMix), with the preserved position.
}

void PlayerController::setRate(qreal rate)
{
    rate = qBound(0.5, rate, 3.0);
    if (qFuzzyCompare(rate, m_rate))
        return;
    m_rate = rate;
    if (m_backend)
        m_backend->setRate(rate);
    emit rateChanged();
}

void PlayerController::setVolume(qreal volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (qFuzzyCompare(volume + 1.0, m_volume + 1.0))
        return;
    m_volume = volume;
    if (m_backend)
        m_backend->setVolume(volume);
    emit volumeChanged();
}

void PlayerController::setPosition(qint64 ms)
{
    if (ms == m_positionMs)
        return;
    m_positionMs = ms;
    emit positionMsChanged();
}

void PlayerController::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;
    m_playing = playing;
    emit playingChanged();
}

void PlayerController::onTick()
{
    if (!m_backend)
        return;
    const qint64 pos = m_backend->position();
    if (!m_previewPath.isEmpty()) {
        if (pos != m_previewPositionMs) {
            m_previewPositionMs = pos;
            emit previewPositionMsChanged();
        }
        return;
    }
    if (!m_mixLoaded || m_ended)
        return;
    setPosition(pos);
    if (m_rangeEndMs >= 0 && pos >= m_rangeEndMs) {
        m_rangeEndMs = -1;
        m_backend->pause();
    }
}

void PlayerController::onBackendPlaying(bool playing)
{
    if (!m_previewPath.isEmpty()) {
        if (playing != m_previewPlaying) {
            m_previewPlaying = playing;
            emit previewPlayingChanged();
        }
    } else if (m_mixLoaded) {
        setPlaying(playing);
        if (!playing)
            onTick();            // the exact position of the stop
    }
    if (playing)
        m_tick.start();
    else
        m_tick.stop();
}

void PlayerController::onBackendDuration(qint64 ms)
{
    if (ms <= 0)
        return;
    if (!m_previewPath.isEmpty()) {
        if (ms != m_previewDurationMs) {
            m_previewDurationMs = ms;
            emit previewDurationMsChanged();
        }
    } else if (m_mixLoaded && ms != m_durationMs) {
        m_durationMs = ms;
        emit durationMsChanged();
    }
}

void PlayerController::onBackendFinished()
{
    if (!m_previewPath.isEmpty()) {
        // Track preview: the logical track's next segment (if any) continues.
        if (!m_previewTrackId.isEmpty() && m_controller && m_controller->store() && !m_meetingId.isEmpty()) {
            const tanara::Meeting m = m_controller->store()->load(m_meetingId);
            const qint64 here = m_previewOffsetMs;
            qint64 next = -1;
            const TrackSeek cur = resolveTrackSeek(m.tracks, m_previewTrackId, here);
            for (const tanara::Track& t : m.tracks) {
                const TrackSeek s = resolveTrackSeek(m.tracks, t.id, qMax<qint64>(0, t.startOffsetMs));
                if (s.leaderId == cur.leaderId && s.offsetMs > here && (next < 0 || s.offsetMs < next))
                    next = s.offsetMs;
            }
            if (next > here) {
                playTrack(m_previewTrackId, int(next));
                return;
            }
        }
        stopPreview();
        return;
    }
    m_rangeEndMs = -1;
    m_ended = true;
    if (m_durationMs > 0)
        setPosition(m_durationMs);
    setPlaying(false);
}

} // namespace tanara_qml

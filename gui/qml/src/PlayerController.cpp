#include "PlayerController.h"

#include "AppContext.h"
#include "LibraryDemoData.h"

#include "tanara/AppController.h"
#include "tanara/store/MeetingStore.h"

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

// Néma, órával léptetett motor: demó, képernyőkép és teszt (nincs hangkimenet).
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

// A megbeszélés hallgatható hangja: a lekeverés; ha nincs (régi felvétel, elmaradt keverés),
// a legnagyobb AKTÍV, a felvétel elejétől tartó (startOffsetMs == 0) sáv — a szegmens-
// időbélyegek megbeszélés-időben vannak, így csak egy ilyen sávon időhelyesek. Ha nincs ilyen,
// a legnagyobb aktív sáv (eltolva szól, de legalább hallható).
QString meetingAudioPath(const tanara::Meeting& m)
{
    const QString mixdown = QDir(m.folder).filePath(
        m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3") : m.mixdownFile);
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

void PlayerController::setController(tanara::AppController* controller)
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
            // Elkészült / megváltozott a lekeverés, vagy változtak a sávok → a forrás
            // újra-feloldása (lejátszás közben a sáv-változás nem szakítja meg a hangot).
            connect(m_controller, &tanara::AppController::mixdownUpdated, this,
                    [this](const QString& id, bool) { if (id == m_meetingId) reload(); });
            connect(m_controller, &tanara::AppController::tracksChanged, this,
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
    // A motor által mért hossz pontosabb; amíg nincs betöltve, a meeting.json-beli érvényes.
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
        setPosition(0);          // a végéről újraindítva elölről
        m_backend->setPosition(0);
    }
    m_ended = false;
    m_backend->play();
}

void PlayerController::play()
{
    m_rangeEndMs = -1;           // a sima lejátszás folyamatos (kilép az egy-megszólalás módból)
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
    m_rangeEndMs = -1;           // kézi ugrás megszakítja az egy-megszólalás módot
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

void PlayerController::playFile(const QString& absolutePath)
{
    if (absolutePath.isEmpty())
        return;
    // Valódi motorral csak létező fájl játszható (a néma motor a demóban bármit „lejátszik”).
    if (backendFactory() && !QFileInfo::exists(absolutePath)) {
        emit errorOccurred(tr("A hangfájl nem található: %1").arg(absolutePath));
        return;
    }
    ensureBackend();
    if (m_playing)
        m_backend->pause();
    m_mixLoaded = false;         // a motor forrása mostantól az előnézet
    setPlaying(false);
    m_previewPath = absolutePath;
    m_previewPositionMs = 0;
    m_previewDurationMs = 0;
    emit previewPathChanged();
    emit previewPositionMsChanged();
    emit previewDurationMsChanged();
    m_backend->setSource(absolutePath, backendFactory() ? 0 : m_durationMs);
    m_backend->play();
}

void PlayerController::stopPreview()
{
    if (m_previewPath.isEmpty())
        return;
    if (m_backend)
        m_backend->pause();
    m_previewPath.clear();
    emit previewPathChanged();
    if (m_previewPlaying) {
        m_previewPlaying = false;
        emit previewPlayingChanged();
    }
    m_tick.stop();
    // A lekeverés a következő lejátszáskor / ugráskor töltődik vissza (loadMix), a megőrzött
    // pozícióval.
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
            onTick();            // a megállás pontos pozíciója
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

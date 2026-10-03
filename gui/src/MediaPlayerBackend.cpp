#include "MediaPlayerBackend.h"

#include "AudioOutputFactory.h"

#include "tanara/Logging.h"

#include <QUrl>

namespace tanara_gui {

MediaPlayerBackend::MediaPlayerBackend(QObject* parent) : tanara_qml::PlayerBackend(parent)
{
    // A PlayerController lustán hoz létre minket (az első lejátszáskor), így az app indulása
    // nem triggereli a Qt Multimedia eszköz- és hwaccel-próbáit.
    m_player = new QMediaPlayer(this);
    m_output = makeFollowDefaultAudioOutput(this);
    m_player->setAudioOutput(m_output);

    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState st) {
                emit playingChanged(st == QMediaPlayer::PlayingState);
            });
    connect(m_player, &QMediaPlayer::durationChanged, this,
            [this](qint64 d) { emit durationChanged(d); });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &MediaPlayerBackend::onMediaStatus);
    // Diagnosztika: ha a hang-kimenet / dekódolás hibázik, látszódjon a logban és a UI-ban.
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error e, const QString& s) {
                qCWarning(tanara::lcAudio) << "QMediaPlayer error:" << e << s;
                emit errorOccurred(s);
            });
    qCDebug(tanara::lcAudio).noquote()
        << "audio-out eszköz:" << m_output->device().description()
        << "| volume:" << m_output->volume();
}

MediaPlayerBackend::~MediaPlayerBackend()
{
    m_player->stop();
}

void MediaPlayerBackend::setSource(const QString& path, qint64)
{
    m_player->stop();
    m_loaded = false;
    m_pendingPosition = -1;
    m_player->setSource(path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path));   // betölt, NEM játszik
}

void MediaPlayerBackend::play()
{
    m_player->play();
}

void MediaPlayerBackend::pause()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
}

void MediaPlayerBackend::setPosition(qint64 ms)
{
    if (!m_loaded)
        m_pendingPosition = ms;      // a betöltés végén alkalmazzuk (onMediaStatus)
    m_player->setPosition(ms);
}

qint64 MediaPlayerBackend::position() const
{
    if (!m_loaded && m_pendingPosition >= 0)
        return m_pendingPosition;
    return m_player->position();
}

qint64 MediaPlayerBackend::duration() const
{
    return m_player->duration();
}

bool MediaPlayerBackend::isPlaying() const
{
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

void MediaPlayerBackend::setRate(qreal rate)
{
    m_player->setPlaybackRate(rate);
}

void MediaPlayerBackend::setVolume(qreal volume)
{
    m_output->setVolume(float(volume));   // QAudioOutput: lineáris 0…1
}

void MediaPlayerBackend::onMediaStatus(QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
        if (!m_loaded) {
            m_loaded = true;
            if (m_pendingPosition > 0 && qAbs(m_player->position() - m_pendingPosition) > 200)
                m_player->setPosition(m_pendingPosition);
            m_pendingPosition = -1;
        }
    } else if (status == QMediaPlayer::EndOfMedia) {
        emit finished();
    }
}

} // namespace tanara_gui

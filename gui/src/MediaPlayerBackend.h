#pragma once
// MediaPlayerBackend: the real audio engine of the QML player (tanara_qml::PlayerController):
// QMediaPlayer + a QAudioOutput that FOLLOWS the system default output (when switching
// headphones / speakers, playback moves to the new device; see AudioOutputFactory.h).
// main.cpp registers it: PlayerController::setBackendFactory(...).
#include "PlayerBackend.h"

#include <QMediaPlayer>

class QAudioOutput;

namespace tanara_gui {

class MediaPlayerBackend : public tanara_qml::PlayerBackend {
    Q_OBJECT
public:
    explicit MediaPlayerBackend(QObject* parent = nullptr);
    ~MediaPlayerBackend() override;

    void setSource(const QString& path, qint64 knownDurationMs) override;
    void play() override;
    void pause() override;
    void setPosition(qint64 ms) override;
    qint64 position() const override;
    qint64 duration() const override;
    bool isPlaying() const override;
    void setRate(qreal rate) override;
    void setVolume(qreal volume) override;

private:
    void onMediaStatus(QMediaPlayer::MediaStatus status);

    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_output = nullptr;
    // The position requested until the source has loaded (QMediaPlayer may drop a seek made before loading).
    qint64 m_pendingPosition = -1;
    bool m_loaded = false;
};

} // namespace tanara_gui

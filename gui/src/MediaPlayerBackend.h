#pragma once
//
// MediaPlayerBackend — a QML-lejátszó (tanara_qml::PlayerController) valódi hang-motorja:
// QMediaPlayer + a rendszer alapértelmezett kimenetét KÖVETŐ QAudioOutput (fülhallgató /
// hangszóró váltásnál a lejátszás átmegy az új eszközre — lásd AudioOutputFactory.h).
// A main.cpp regisztrálja: PlayerController::setBackendFactory(…).
//
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
    // A forrás betöltődéséig kért pozíció (a QMediaPlayer a betöltés előtti seeket eldobhatja).
    qint64 m_pendingPosition = -1;
    bool m_loaded = false;
};

} // namespace tanara_gui

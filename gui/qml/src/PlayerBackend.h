#pragma once
//
// PlayerBackend: the abstraction of PlayerController's audio engine.
//
// The QML module (tanara_qml) does not link Qt Multimedia; the real (QMediaPlayer-based)
// engine lives in gui/src (MediaPlayerBackend), and main.cpp registers its factory:
//   PlayerController::setBackendFactory(...)
// Without a factory (demo, screenshot, tests) PlayerController uses a built-in, clock-stepped
// silent engine, so the player's logic can be played through without audio.
//
#include <QObject>
#include <QString>

namespace tanara_qml {

class PlayerBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Loads the source (absolute file path; empty -> clears). Does not start playback.
    // knownDurationMs: the length known to the caller (the silent engine uses it).
    virtual void setSource(const QString& path, qint64 knownDurationMs) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void setPosition(qint64 ms) = 0;
    virtual qint64 position() const = 0;
    virtual qint64 duration() const = 0;      // 0 until known
    virtual bool isPlaying() const = 0;
    virtual void setRate(qreal rate) = 0;
    virtual void setVolume(qreal volume) = 0; // 0..1, linear

signals:
    void playingChanged(bool playing);
    void durationChanged(qint64 ms);
    void finished();                          // reached the end of the source
    void errorOccurred(const QString& message);
};

} // namespace tanara_qml

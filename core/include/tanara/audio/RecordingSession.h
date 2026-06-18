#pragma once
//
// Tanara — egy felvételi munkamenet vezérlése:
//   capture (AudioEngine) → eszközönként ffmpeg (QProcess) raw PCM stdin → Opus,
//   majd stopkor egyetlen mixdown (.mp3) és egy kész tanara::Meeting.
//
#include "tanara/Types.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

namespace tanara {

class AudioEngine;

class RecordingSession : public QObject {
    Q_OBJECT
public:
    // audioDir: a felvételek gyökere (ez alá jön a meeting-mappa).
    // title: a meeting címe (a mappanév slugjához és a Meeting.title-höz).
    // userSpeakerName: az első mic-sáv fix beszélő-neve (pl. "Ádám").
    explicit RecordingSession(QString audioDir,
                              QString title,
                              QString userSpeakerName = QStringLiteral("Beszélő 1"),
                              QObject* parent = nullptr);
    ~RecordingSession() override;

    RecordingState state() const;
    QString folder() const;          // a létrejött meeting-mappa abszolút útja

public slots:
    // Létrehozza a meeting-mappát, elindítja a capture-t és eszközönként az
    // ffmpeg encodert + a drain workert. Nem dob; hibára failed()-et emittál.
    void start(const QVector<AudioDeviceInfo>& devices);

    // Leállít, flush + closeWriteChannel minden ffmpeg-en, megvárja a végét,
    // lefuttatja a mixdownt, majd finished(Meeting)-et (vagy failed()-et) emittál.
    void stop();

signals:
    void stateChanged(tanara::RecordingState state);
    void levelMeterUpdated(int trackIndex, float rms);   // ~30 Hz, queued
    void elapsedChanged(qint64 ms);
    void finished(tanara::Meeting meeting);
    void failed(QString error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tanara

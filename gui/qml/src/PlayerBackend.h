#pragma once
//
// PlayerBackend — a PlayerController hang-motorjának absztrakciója.
//
// A QML-modul (tanara_qml) nem linkeli a Qt Multimediát; a valódi (QMediaPlayer-es) motor a
// gui/src-ben él (MediaPlayerBackend), és a main.cpp regisztrálja a gyárát:
//   PlayerController::setBackendFactory(…)
// Gyár nélkül (demó, képernyőkép, tesztek) a PlayerController egy beépített, órával léptetett
// néma motort használ — így a lejátszó logikája hang nélkül is végigjátszható.
//
#include <QObject>
#include <QString>

namespace tanara_qml {

class PlayerBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // A forrás betöltése (abszolút fájl-út; üres → kiürít). Nem indít lejátszást.
    // knownDurationMs: a hívó által ismert hossz (a néma motor ezt használja).
    virtual void setSource(const QString& path, qint64 knownDurationMs) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void setPosition(qint64 ms) = 0;
    virtual qint64 position() const = 0;
    virtual qint64 duration() const = 0;      // 0, amíg nem ismert
    virtual bool isPlaying() const = 0;
    virtual void setRate(qreal rate) = 0;
    virtual void setVolume(qreal volume) = 0; // 0…1, lineáris

signals:
    void playingChanged(bool playing);
    void durationChanged(qint64 ms);
    void finished();                          // a forrás végére ért
    void errorOccurred(const QString& message);
};

} // namespace tanara_qml

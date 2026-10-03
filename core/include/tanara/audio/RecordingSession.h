#pragma once
//
// Tanara — egy felvételi munkamenet vezérlése:
//   capture (AudioEngine) → eszközönként ffmpeg (QProcess) raw PCM stdin → Opus,
//   majd stopkor a per-sáv encoderek lezárása és egy kész tanara::Meeting.
//   A lekevert .mp3 (mixdown) NEM itt készül — azt az AppController gyártja aszinkron,
//   hogy a leállítás ne fagyassza a UI-t (lásd AppController::regenerateMixdown()).
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
    // opusKbps: per-sáv Opus bitráta (a hangminőség-beállításból; lásd opusBitrateKbps()).
    explicit RecordingSession(QString audioDir,
                              QString title,
                              QString userSpeakerName = QStringLiteral("Beszélő 1"),
                              int opusKbps = 64,
                              QObject* parent = nullptr);
    ~RecordingSession() override;

    RecordingState state() const;
    QString folder() const;          // a létrejött meeting-mappa abszolút útja

    // A sávok eszközei a sáv-index sorrendjében (a felvétel közben hozzáadottak a végén).
    QVector<AudioDeviceInfo> trackDevices() const;
    // Él-e még a sáv (hamis, ha az eszközt leválasztották és a sáv le lett zárva).
    bool trackOpen(int trackIndex) const;

public slots:
    // Létrehozza a meeting-mappát, elindítja a capture-t és eszközönként az
    // ffmpeg encodert + a drain workert. Nem dob; hibára failed()-et emittál.
    void start(const QVector<AudioDeviceInfo>& devices);

    // Leállít: flush + closeWriteChannel minden ffmpeg-en, megvárja a per-sáv encoderek
    // végét (gyors tail-flush), majd finished(Meeting)-et (vagy failed()-et) emittál.
    // NEM blokkol: a lezárás a worker szálán fut, a finished() akkor jön, amikor MINDEN
    // sávfájl lezárult a lemezen (addig az állapot Stopping → Encoding).
    // A mixdownt MÁR NEM gyártja le — `mixdownFile` üres marad; a lekeverést az
    // AppController készíti később, aszinkron (auto vagy kézi módban).
    void stop();

    // Felvétel KÖZBEN egy további eszköz sávjának indítása. A sáv fájlja a felvétel elejétől
    // csenddel van kitöltve, így időben együtt áll a többi sávval (a hang „attól a
    // pillanattól” szól benne). false, ha nem fut felvétel, az eszköz már sávon van, vagy
    // nem nyitható meg. Siker: trackAdded().
    bool addDevice(const tanara::AudioDeviceInfo& device);

    // Egy sáv biztonságos lezárása felvétel közben (az eszközt leválasztották): a capture
    // leáll, a maradék hang kiíródik, az encoder lezárja a fájlt. A felvétel a többi sávval
    // megy tovább; a lezárt sáv a meeting része marad (rövidebb fájllal). trackClosed().
    void closeTrack(const QString& deviceName);

signals:
    void stateChanged(tanara::RecordingState state);
    void levelMeterUpdated(int trackIndex, float rms);   // ~30 Hz, queued
    void trackLevel(int trackIndex, float rms, float peak);   // ugyanaz, csúccsal
    void trackAdded(int trackIndex, QString deviceName);
    void trackClosed(int trackIndex, QString deviceName);
    void elapsedChanged(qint64 ms);
    void finished(tanara::Meeting meeting);
    void failed(QString error);

private:
    void onFinalized(const QStringList& failedFiles);   // a worker lezárta a sávfájlokat
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tanara

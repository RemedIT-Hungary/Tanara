#pragma once
//
// AppController — az EGYETLEN objektum, amihez az UI köt (Widgets most, QML később).
// Összedrótozza a modulokat: beállítások, eszközök, felvétel, tár, STT, LLM, összefoglaló.
// SZABÁLY: itt sincs Widgets-függőség — sima QObject API (signal/slot + value DTO-k).
//
#include "tanara/Types.h"
#include <QObject>
#include <QVector>
#include <memory>

namespace tanara {

class SettingsManager;
class DeviceManager;
class MeetingStore;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(tanara::RecordingState recordingState READ recordingState NOTIFY recordingStateChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    // Alkomponensek (a UI ezekre köthet modellt/nézetet — pl. MeetingListModel::setStore(store())).
    SettingsManager* settings() const;
    DeviceManager*   devices() const;
    MeetingStore*    store() const;

    RecordingState recordingState() const;
    QString currentMeetingFolder() const;   // épp felvett/utoljára felvett mappa

    // Az utoljára (legutóbbi felvételnél) használt eszközök NEVEI — induláskor
    // ezeket érdemes előpipálni a UI-ban. Perzisztens (~/.tanara/state.json).
    QStringList lastUsedDeviceNames() const;

    // Ismert személynevek (globális, meetingek közt újrahasznált) — autocomplete-hez.
    QStringList knownPeople() const;

    // Személy átnevezése/törlése GLOBÁLISAN: a névlistában + MINDEN meeting
    // speakerMap-jében átvezetve (a transcript.md-k újragenerálva). peopleChanged jel.
    void renamePerson(const QString& oldName, const QString& newName);
    void removePerson(const QString& name);

public slots:
    // Eszközök újrafelsorolása (→ devicesChanged()).
    void refreshDevices();

    // Felvétel ELŐTTI élő szintfigyelés (VU-sávokhoz a UI-ban). Megnyitja az
    // összes capture-eszközt, és deviceLevel(name, rms)-t emittál ~20 Hz-cel.
    // Felvétel indításakor automatikusan leáll.
    void startLevelMonitoring();
    void stopLevelMonitoring();

    // Az utoljára használt eszközök kézi felülírása/perzisztálása.
    void setLastUsedDeviceNames(const QStringList& names);

    // Felvétel indítása a megadott eszközökkel (üres → az összes capture eszköz).
    void startRecording(const QString& title, const QVector<tanara::AudioDeviceInfo>& devices = {});
    void stopRecording();

    // Egy meeting átírása (Soniox, per-sáv → merge → transcript.md). Kulcs a KeyStore-ból.
    void transcribeMeeting(const QString& meetingId);
    // Egy meeting összefoglalása (LM Studio/Gemma → summary.md + másolat a notesDir-be).
    void summarizeMeeting(const QString& meetingId);

    // Egy beszélő átnevezése egy meetingben (nyers címke → valódi név). Perzisztál
    // (Meeting.speakerMap + people.json), újragenerálja a transcript.md-t a nevekkel,
    // és speakerMapChanged-et emittál. Üres/azonos név → a leképezés törlése.
    void renameSpeaker(const QString& meetingId, const QString& rawLabel, const QString& displayName);

    // Titok (pl. Soniox API-kulcs) beállítása a KeyStore-ban. name pl. "soniox.apiKey".
    void setSecret(const QString& name, const QString& value);
    bool hasSecret(const QString& name) const;

signals:
    void devicesChanged();
    void deviceLevel(QString deviceName, float rms);   // élő szint (monitoring)
    void recordingStateChanged(tanara::RecordingState state);
    void levelMeterUpdated(int trackIndex, float rms);
    void elapsedChanged(qint64 ms);
    void recordingFinished(tanara::Meeting meeting);
    void transcriptReady(QString meetingId, QString markdownPath);
    void summaryReady(QString meetingId, QString markdownPath);
    void speakerMapChanged(QString meetingId);              // beszélő-átnevezés után
    void peopleChanged();                                   // személy-lista változott
    void jobProgress(QString meetingId, QString message);   // átírás/összefoglaló állapot
    void errorOccurred(QString message);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// A KeyStore kulcsnevei (egy helyen).
namespace keys {
inline const QString SonioxApiKey = QStringLiteral("soniox.apiKey");
inline const QString LlmApiKey    = QStringLiteral("llm.apiKey");
}

} // namespace tanara

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
class VoiceprintStore;

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
    VoiceprintStore* voiceprints() const;   // hang-lenyomat DB (voice-ID)

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

    // Mely meetingeken szerepel az adott személy (speakerMap-érték vagy mic-sáv neve).
    // "Cím (yyyy-MM-dd)" formátumú sorok, legújabb elöl — az azonosítást segíti.
    QStringList meetingsForPerson(const QString& name) const;

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

    // Meeting címének átnevezése (meeting.json + index frissül).
    void renameMeeting(const QString& meetingId, const QString& newTitle);

    // Meeting VÉGLEGES törlése (mappa + index). A store meetingRemoved jelét adja.
    void deleteMeeting(const QString& meetingId);

    // Egy (felvétel után eldobott) sáv visszaállítása aktívvá — a fájl megvolt a lemezen.
    void restoreTrack(const QString& meetingId, const QString& trackId);
    // Egy sáv VÉGLEGES törlése: a hangfájl fizikailag törlődik + kikerül a meetingből.
    void deleteTrack(const QString& meetingId, const QString& trackId);

    // A saját (mic-sáv) beszélőnév beállítása — a beállításba ÉS a személy-DB-be is.
    void setUserSpeakerName(const QString& name);

    // Az LLM-endpont (settings.llm.baseUrl) elérhető modelljeinek lekérése (GET /models).
    // Eredmény: llmModelsFetched(QStringList) vagy llmModelsFailed(QString).
    void fetchLlmModels();

    // Egy meeting átírása (Soniox, per-sáv → merge → transcript.md). Kulcs a KeyStore-ból.
    void transcribeMeeting(const QString& meetingId);
    // Egy meeting összefoglalása (LM Studio/Gemma → summary.md + másolat a notesDir-be).
    void summarizeMeeting(const QString& meetingId);

    // Egy beszélő átnevezése egy meetingben (nyers címke → valódi név). Perzisztál
    // (Meeting.speakerMap + people.json), újragenerálja a transcript.md-t a nevekkel,
    // és speakerMapChanged-et emittál. Üres/azonos név → a leképezés törlése.
    // Ha enroll=true (alapért.) és van hang-modell, a beszélő hangjából lenyomatot is
    // rögzít a name alá (a kézi címkézés „tanítja" a voice-ID-t).
    void renameSpeaker(const QString& meetingId, const QString& rawLabel,
                       const QString& displayName, bool enroll = true);

    // Egy beszélő hang-lenyomatának explicit rögzítése a voiceprint DB-be (a meeting
    // adott nyers címkéjének reprezentatív hangjából). voiceprintsChanged jel.
    void enrollSpeaker(const QString& meetingId, const QString& rawLabel, const QString& name);

    // Auto-azonosítás: a meeting még névtelen (nem leképezett) nyers beszélőit a
    // voiceprint DB ellen párosítja; küszöb felett előtölti a speakerMap-et.
    // speakerMapChanged-et emittál. Bármikor újrafuttatható (a friss DB-vel).
    void autoIdentifyMeeting(const QString& meetingId);

    // Fingerprint-teszt EGY beszélőre: a hangjából a legjobb egyezés a voiceprint-DB-ből
    // (név + cosine pontszám). Nem módosít semmit; { "", -1 } ha nincs modell/egyezés.
    tanara::VoiceMatch testSpeakerMatch(const QString& meetingId, const QString& rawLabel);

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
    void voiceprintsChanged();                              // voice-ID lenyomat-DB változott
    void tracksChanged(QString meetingId);                  // sáv aktív/eldobott/törölve
    void llmModelsFetched(QStringList models);             // fetchLlmModels eredménye
    void llmModelsFailed(QString error);
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

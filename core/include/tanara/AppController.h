#pragma once
//
// AppController — az EGYETLEN objektum, amihez az UI köt (a QML-nézetmodellek, a CLI).
// Összedrótozza a modulokat: beállítások, eszközök, felvétel, tár, STT, LLM, összefoglaló.
// SZABÁLY: itt sincs Widgets-függőség — sima QObject API (signal/slot + value DTO-k).
//
#include "tanara/Types.h"
#include "tanara/provider/ReadinessModel.h"
#include "tanara/edit/SpeakerEditTypes.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/jobs/JobTypes.h"
#include "tanara/summary/SummaryStore.h"
#include "tanara/import/AudioImporter.h"
#include "tanara/tags/TagTypes.h"
#include <QObject>
#include <QVector>
#include <memory>
#include <functional>

namespace tanara {

class SettingsManager;
class DeviceManager;
class MeetingStore;
class VoiceprintStore;
class SpeakerEditor;
class CloudAccount;
class MeetingJobTracker;
class MeetingLibrary;
class TrackCatalog;
class WaveformService;
class PeopleService;
class PeopleStats;
class TagService;
class MeetingProfiles;
class EmbeddingIndex;
class EmbeddingPreparer;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(tanara::RecordingState recordingState READ recordingState NOTIFY recordingStateChanged)
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    // Alkomponensek (a UI ezekre köthet modellt/nézetet).
    SettingsManager* settings() const;
    DeviceManager*   devices() const;
    MeetingStore*    store() const;
    VoiceprintStore* voiceprints() const;   // hang-lenyomat DB (voice-ID)

    // ---- az újratervezett főablak háttere (strukturált réteg) -------------------------
    // Meetingenkénti feldolgozási állapot: futó feladatok (szakaszok, valós haladás),
    // megmaradó hibák, levezetett ikon-állapotok. Lásd jobs/MeetingJobTracker.h.
    MeetingJobTracker* jobs() const;
    // A könyvtár-oldalsáv lekérdezései: dátum-szekciók, keresés, szűrők, „Ezek várnak rád”.
    MeetingLibrary*    library() const;
    // „Sávok” fül: barátságos nevek, átnevezés, hiányzó fájl megkeresése, eldobottak törlése.
    TrackCatalog*      tracks() const;
    // Hullámforma-csúcsok (aszinkron, gyorsítótárazva a meeting mappájában).
    WaveformService*   waveforms() const;
    // Hangfájl-importálás motorja (fájl-adatok lekérése: probeAsync; indítás: importAudio).
    AudioImporter*     importer() const;
    // Kényelmi: jobs()->state(meetingId).
    MeetingProcessingState processingState(const QString& meetingId) const;

    // A meeting összefoglalója STRUKTURÁLTAN + a keletkezés metaadatai (mikor, melyik
    // szolgáltató/modell, gyors vagy témánkénti). Régi (csak summary.md) összefoglalónál a
    // struktúra a markdownból jön (fromMarkdown=true). exists=false, ha nincs összefoglaló.
    SummaryDocument summaryDocument(const QString& meetingId) const;

    // A meeting (szerkesztett) téma-listája a lemezről (summary.topics.json) — LLM-hívás nélkül.
    QVector<tanara::SummaryTopic> meetingTopics(const QString& meetingId) const;
    // Témánkénti állapot BÁRMIKOR lekérdezve (nem csak az átmeneti jelekből): a téma-lista
    // sorrendjében vár / sorban áll / fut / kész / hibás (üzenettel; a hiba újraindítás után
    // is megmarad). A lemezen lévő elemzésekből, a futó sorból és a megmaradt hibákból áll össze.
    QVector<tanara::TopicStatus> topicStatuses(const QString& meetingId) const;

    RecordingState recordingState() const;
    QString currentMeetingFolder() const;   // épp felvett/utoljára felvett mappa

    // ---- Tanara Cloud ----------------------------------------------------------------
    // A fiók (mindig létezik: a teaser-mód várólistája is ezen megy). Lásd CloudAccount.h.
    CloudAccount* cloud() const;
    // A bejelentkezős cloud-mód él: befordult (TANARA_BUILD_CLOUD) ÉS bekapcsolt
    // (settings.cloudEnabled, vagy TANARA_CLOUD=live). TANARA_CLOUD=off mindent kikapcsol.
    bool cloudLive() const;
    // A „Hamarosan” (várólista) panel látszik: befordult (TANARA_CLOUD_TEASER) és a cloud még nem él.
    bool cloudTeaser() const;
    // Az adott lépés kiválasztott providere a Tanara Cloud.
    bool usesCloud(WorkflowStep step) const;
    // A ténylegesen küldött modell-azonosító (Expert-modell, különben a tier virtuális neve).
    QString cloudModelFor(WorkflowStep step) const;
    // Becslés-kérés összeállítása egy meetingre (task: transcribe | summarize; summaryMode:
    // quick | complex). Az LLM-részt folyamat-függetlenül, hívás-csoportokban írja le (llm_calls).
    EstimateRequest makeEstimateRequest(const QString& meetingId, const QString& task,
                                        const QString& summaryMode) const;

    // Egy munkafolyamat-lépés kapuzása EGYETLEN igazságforrásból (ReadinessModel):
    // futtatható-e az adott meetingen, és ha nem, PONTOSAN mi hiányzik. Ugyanezt
    // használja a GUI gomb-engedélyezés és az itteni guardok is. Headless (nincs UI).
    ReadinessResult canRun(WorkflowStep step, const QString& meetingId) const;

    // Az utoljára (legutóbbi felvételnél) használt eszközök NEVEI — induláskor
    // ezeket érdemes előpipálni a UI-ban. Perzisztens (~/.tanara/state.json).
    QStringList lastUsedDeviceNames() const;

    // ---- felvevő (lebegő felvevő-ablak) -------------------------------------------------
    // A futó felvétel ÉLŐ sávjainak eszköznevei (sáv-sorrendben), ill. a felvétel közben
    // leválasztott (biztonságosan lezárt) sávok eszköznevei. Felvételen kívül üresek.
    QStringList recordingDeviceNames() const;
    QStringList disconnectedRecordingDeviceNames() const;

    // ---- Személyek ablak háttere ---------------------------------------------------------
    // Személyek, becenevek / megjegyzés, hangminták, összevonás, törlés, visszavonás — lásd
    // people/PeopleService.h. A személyenkénti statisztika (megbeszélés-szám, beszédidő,
    // utoljára látva) háttérszálon számolódik: people/PeopleStats.h.
    PeopleService* peopleService() const;
    PeopleStats*   peopleStats() const;
    // A hang-modell (CAM++ ONNX) várt helye; a megléte: voiceIdentificationAvailable().
    QString voiceModelPath() const;

    // ---- címkék és címkejavaslatok -------------------------------------------------------
    // A készlet és a meetingenkénti címkék (tags/TagService.h); a klasszikus hasonlóság
    // (tags/MeetingProfiles.h); a beágyazások és a könyvtár előkészítése (embedding/).
    TagService*        tags() const;
    MeetingProfiles*   profiles() const;
    EmbeddingIndex*    embeddings() const;
    EmbeddingPreparer* embeddingPreparer() const;
    // A meeting utolsó javaslat-listája (bármelyik forrásból; a már felrakott és az elutasított
    // kiszűrve) — a később csatlakozó nézetmodellnek.
    QVector<tanara::TagSuggestion> pendingTagSuggestions(const QString& meetingId) const;
    // Javaslat még nem létező megbeszéléshez (import-ablak, felvétel előtt): cím alapján. Szinkron.
    QVector<tanara::TagSuggestion> draftTagSuggestions(const QString& title) const;
    // Felvétel KÖZBEN megadott címkék (a meeting még nincs a tárban): a felvétel végén a
    // meetingre kerülnek. Felvételen kívül hatástalan; a következő felvétel üresen indul.
    void setRecordingTags(const QStringList& tagIds);
    QStringList recordingTags() const;

    // Ismert személynevek (globális, meetingek közt újrahasznált) — autocomplete-hez.
    QStringList knownPeople() const;

    // Személy átnevezése/törlése GLOBÁLISAN: a névlistában + MINDEN meeting
    // speakerMap-jében átvezetve (a transcript.md-k újragenerálva). peopleChanged jel.
    // A személy rekordja (becenevek, megjegyzés) egyben megy: átnevezésnél az új névre
    // kerül (ha a cél-név már létezik, a két személy adatai egyesülnek), törlésnél
    // törlődik. (A régi nevet becenévként a Személyek ablak művelete, a PeopleService őrzi meg.)
    void renamePerson(const QString& oldName, const QString& newName);
    void removePerson(const QString& name);

    // A meeting eddig elkészült (perzisztált) téma-elemzései (summary.analyses.json).
    // A UI ebből tölti a téma-kártyák „✓ Kész" állapotát a szerkesztő megnyitásakor.
    QVector<tanara::TopicAnalysis> topicAnalyses(const QString& meetingId) const;

    // ---- átirat-szerkesztő (beszélő-javítás) ------------------------------
    // A meeting beszélő-szerkesztő munkamenete (lásd edit/SpeakerEditor.h). Meetingenként
    // EGY példány él (az AppController a szülője); az undo-verem a bezárásig megmarad.
    // Ismeretlen meeting → nullptr. A voice-ID modell megléte esetén az embedder be van kötve.
    SpeakerEditor* speakerEditor(const QString& meetingId);
    // A munkamenet lezárása (az undo-verem eldobva; a javítások a lemezen maradnak).
    void closeSpeakerEditor(const QString& meetingId);

    // Ismert személyek a választó panelhez: név, van-e hanglenyomat, hány meetingen szerepel.
    // (Szűrés: edit/PeopleDirectory.h — filterPeople / matchesSearch.)
    QVector<tanara::PersonInfo> peopleDirectory() const;

    // Elavult-e az összefoglaló (a készítése óta változott a beszélő-hozzárendelés), és
    // hány beszélőt javítottak azóta. Megnyitott szerkesztő nélkül is hívható.
    tanara::SummaryStaleInfo summaryStale(const QString& meetingId) const;
    tanara::SummaryStaleInfo summaryStale(const tanara::Meeting& meeting) const;
    // Mi vész el újra-átíráskor (kézi javítások száma a megerősítő párbeszédhez).
    tanara::RetranscribeImpact retranscribeImpact(const QString& meetingId) const;

public slots:
    // Eszközök újrafelsorolása (→ devicesChanged()).
    void refreshDevices();

    // Élő szintfigyelés (a felvevő szintmérőihez). Megnyitja az összes capture-eszközt, és
    // deviceLevelPeak(name, rms, peak)-et emittál. A felvétel ALATT is megy — a sávra NEM
    // kerülő eszközökön (a rögzítettek szintjét a felvétel adja), így a felvevő minden eszköz
    // mérőjét mozgatni tudja.
    void startLevelMonitoring();
    void stopLevelMonitoring();

    // Felvétel KÖZBEN egy további eszköz sávjának indítása (a sáv attól a pillanattól szól;
    // a fájl elejét csend tölti ki, így együtt áll a többivel). false, ha nem megy felvétel
    // vagy az eszköz nem nyitható. Siker: recordingTrackAdded + a kijelölés mentése.
    bool addRecordingDevice(const tanara::AudioDeviceInfo& device);

    // A felvétel utáni AUTOMATIKUS lekeverés engedélyezése ebben a folyamatban (alapból be).
    // Az önálló felvevő-folyamat (tanara --record) kikapcsolja: az a felvétel után kilép, a
    // félbehagyott ffmpeg csak csonka mixdown.part.mp3-at hagyna. A lekeverést ilyenkor az
    // elemző készíti el, amikor kell (az átírás a hiányzó/elavult keveréket előbb legyártja).
    void setAutoMixdownAfterRecording(bool on);

    // Az utoljára használt eszközök kézi felülírása/perzisztálása.
    void setLastUsedDeviceNames(const QStringList& names);

    // Felvétel indítása a megadott eszközökkel (üres → az összes capture eszköz).
    void startRecording(const QString& title, const QVector<tanara::AudioDeviceInfo>& devices = {});
    void stopRecording();

    // Meeting címének átnevezése (meeting.json + index frissül).
    void renameMeeting(const QString& meetingId, const QString& newTitle);

    // A meeting pár szavas kontextus-leírásának mentése (téma/résztvevők) — beépül a
    // STT context-envelope-jába és az LLM-összefoglaló kontextusába. Perzisztens.
    void setMeetingContextNote(const QString& meetingId, const QString& note);
    // A figyelő által észlelt hívás-alkalmazás (pl. „Microsoft Teams”) — csak tájékoztató mező,
    // a megjegyzést nem érinti, és az átírónak sem megy. Üres → törli.
    void setMeetingDetectedCall(const QString& meetingId, const QString& appName);

    // Meeting VÉGLEGES törlése (mappa + index). A store meetingRemoved jelét adja.
    void deleteMeeting(const QString& meetingId);

    // Egy (felvétel után eldobott) sáv visszaállítása aktívvá — a fájl megvolt a lemezen.
    void restoreTrack(const QString& meetingId, const QString& trackId);
    // Egy sáv VÉGLEGES törlése: a hangfájl fizikailag törlődik + kikerül a meetingből.
    void deleteTrack(const QString& meetingId, const QString& trackId);

    // A mixdown.mp3 újrakeverése a meeting AKTÍV sávjaiból (a sávok módosítása után
    // a mixdown „dirty" lesz; ez frissíti). Aszinkron (ffmpeg QProcess, nem blokkol):
    // a végén mixdownUpdated(meetingId, ok) + tracksChanged jelet ad. jobProgress-t is
    // emittál az állapotról. Ha nincs aktív sáv vagy fut már egy keverés → no-op.
    void regenerateMixdown(const QString& meetingId);

    // Hangfájlok importálása ÚJ meetingbe (lásd import/AudioImporter.h): forrásonként egy sáv,
    // kérésre csatornánként bontva; aszinkron (ffmpeg), a UI nem áll meg. Visszaadja a leendő
    // meeting azonosítóját (üres + errorOccurred, ha már fut egy importálás). A haladás a
    // jobs()-ban JobKind::Import feladatként látszik EZEN az azonosítón (valós százalék,
    // megszakítható: cancelJob(id, JobKind::Import)). A meeting csak a sikeres végén jön
    // létre (importFinished); hiba / megszakítás után semmi nem marad a lemezen. A lekeverés
    // ugyanúgy indul, mint felvétel után: mixdownMode "auto" → azonnal a háttérben, "manual"
    // → kézre vár (és setAutoMixdownAfterRecording(false) mellett itt sem indul). Átírás
    // NEM indul magától.
    QString importAudio(const tanara::ImportRequest& request);

    // ---- megbeszélés-archívum (*.tanara.zip, lásd store/MeetingArchive.h) ----
    // Exportálás háttérszálon (a GUI nem áll meg egy 1 GB-os felvételnél sem). Azonnal
    // visszatér a művelet azonosítójával ("export:<meetingId>"); a haladás a jobs()-ban
    // JobKind::Export feladatként látszik a meetingen (valós százalék, megszakítható:
    // cancelJob(meetingId, JobKind::Export)), és archiveProgress jelként is jön. A vége
    // archiveFinished. Üres azonosító + errorOccurred: nincs ilyen meeting / felvétel alatt
    // áll / már fut egy archívum-művelet.
    QString exportMeetingArchive(const QString& meetingId, const QString& zipPath);
    // Importálás háttérszálon: kicsomagolás a felvételek mappája alatti .import-* mappába,
    // majd (fő szálon) MeetingStore::adoptMeetingFolder + a címkék feloldása a helyi
    // címkekészletre (azonos azonosító → az; különben név szerint, szükség esetén létrehozva).
    // Visszaad: művelet-azonosító ("import:<uuid>"); üres + errorOccurred, ha már fut egy.
    // A vége archiveFinished (siker esetén meetingId = az új megbeszélés).
    QString importMeetingArchive(const QString& zipPath);
    // Egy sima megbeszélés-mappa behúzása (másik gépről kimásolt mappa, zip nélkül): a
    // felvételek mappáján kívüli forrást háttérszálon bemásolja (.import-*), majd ugyanaz
    // az útvonal, mint az archívumnál (adopt + címkék). A felvételek mappája alatti forrás
    // helyben marad. Visszaad: "import:<uuid>"; a vége archiveFinished.
    QString importMeetingFolder(const QString& folder);
    // Ugyanezek szinkron (CLI, tesztek). Hiba: false / üres Meeting + *error.
    bool exportMeetingArchiveNow(const QString& meetingId, const QString& zipPath, QString* error,
                                 std::function<void(int)> progress = {});
    tanara::Meeting importMeetingArchiveNow(const QString& zipPath, QString* error,
                                            std::function<void(int)> progress = {});
    // Fut-e épp archívum-művelet (export vagy import).
    bool archiveBusy() const;

    // A saját (mic-sáv) beszélőnév beállítása — a beállításba ÉS a személy-DB-be is.
    void setUserSpeakerName(const QString& name);

    // Egy meeting átírása. A leirat a MIXDOWNból készül (egyetlen hangfolyam → nincs
    // sávonkénti átfedés-összefésülés/duplikáció, ~N× helyett 1× Soniox-költség). Ha a
    // mixdown hiányzik/elavult, előbb legyártja, és a kész jelére indítja az átírást.
    // Kulcs a KeyStore-ból.
    void transcribeMeeting(const QString& meetingId);

    // Újra-átírás meglévő átirat mellett: törli a beszélő-hozzárendeléseket (más provider
    // más beszélő-felosztást adhat — a nevek tévesen ragadnának át), majd transcribeMeeting.
    // keepBackup: a mostani átirat-fájlok másolata megmarad a meeting-mappában
    // (transcript-backup-<időbélyeg>/). A kézi sor-javítások (overlay) az új átirat
    // elkészültekor törlődnek — a megszólalások határai megváltoznak.
    void retranscribeMeeting(const QString& meetingId, bool keepBackup = false);
    // „Rendben így": az összefoglaló elavult-jelzőjének elengedése újragenerálás nélkül.
    void dismissSummaryStale(const QString& meetingId);
    // Egy meeting összefoglalása (LM Studio/Gemma → summary.md + másolat a notesDir-be).
    void summarizeMeeting(const QString& meetingId);

    // Komplex (több körös) összefoglaló — 1. kör: a teljes átiratból TÉMÁKAT nyer ki, és
    // topicsReady(meetingId, topics)-szal adja vissza (a UI szerkesztésre megjeleníti).
    // Ha már létezik summary.topics.json, azt adja vissza (nincs újrakinyerés).
    void extractMeetingTopics(const QString& meetingId);
    // 2. kör + reduce (batch): a (szerkesztett) témákból CSAK a még elemzetleneket sorolja
    // be a téma-elemzés job-sorba, majd ha minden téma elemzése megvan, reduce → summary.md
    // (summaryReady). Minden kész elemzés AZONNAL a summary.analyses.json-ba perzisztálódik,
    // így megszakítás/hiba után az újraindítás onnan folytatja, ahol tartott.
    void generateComplexSummary(const QString& meetingId,
                                const QVector<tanara::SummaryTopic>& topics);
    // EGY téma elemzésének (újra)indítása — a job-sorba kerül (a lokális modell parallel=1,
    // ezért egyszerre egy fut). A topic a summary.topics.json-ba is átvezetődik (szerkesztés).
    // Eredmény: topicAnalysisReady/Failed; a kész elemzés felülírja a korábbit a JSON-ban.
    void analyzeTopic(const QString& meetingId, const tanara::SummaryTopic& topic);
    // Reduce a LEMEZEN lévő elemzésekből (summary.analyses.json, a topics.json sorrendjében)
    // → summary.md + summaryReady. Külön hívható, pl. egy hibás téma újrafuttatása után.
    void finalizeComplexSummary(const QString& meetingId);

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
    // onProgress(done,total): opcionális; false visszatérés → megszakítás (a UI-nak,
    // a fő szálon hívva — a callback rajzolhat progresst / nézhet „mégse"-t).
    void autoIdentifyMeeting(const QString& meetingId,
                             const std::function<bool(int,int)>& onProgress = {});

    // Fingerprint-teszt EGY beszélőre: a hangjából a legjobb egyezés a voiceprint-DB-ből
    // (név + cosine pontszám). Nem módosít semmit; { "", -1 } ha nincs modell/egyezés.
    tanara::VoiceMatch testSpeakerMatch(const QString& meetingId, const QString& rawLabel);

    // ÁTÍRÁS ELŐTTI résztvevő-azonosítás: sávonként ablakozva mintázza a hangot,
    // beszélőkre klaszterezi, és a voiceprint-DB ellen párosítja → kik voltak a
    // meetingen (név vagy "ismeretlen"). Lokális, Soniox nélkül; szinkron (pár mp).
    // onProgress(done,total): opcionális; false → megszakítás (lásd autoIdentifyMeeting).
    QVector<tanara::ParticipantGuess> identifyParticipants(
        const QString& meetingId, const std::function<bool(int,int)>& onProgress = {});

    // Lenyomat felvétele egy tetszőleges hang-szegmensből (átírás előtti névadáshoz):
    // a meeting adott sávjának [startMs,endMs] részéből embeddinget számol és a név
    // alá menti. voiceprintsChanged jel.
    void enrollVoiceprintFromSample(const QString& name, const QString& meetingId,
                                    const QString& trackId, qint64 startMs, qint64 endMs);

    // ---- strukturált réteg: megszakítás, témák, azonosítás, hullámforma ----------------
    // Egy futó feladat megszakítása. A meeting konzisztens állapotban marad, felvétel nem
    // törlődik:
    //  - Transcribe: a lekeverés-fázisban az ffmpeg leáll (a régi keverék érintetlen); a
    //    szolgáltatónál a job megszakad és a feltöltött hang/átírás törlődik (Soniox cleanup);
    //    a korábbi átirat (ha volt) megmarad. Az azonosítás-szakaszban: csak az azonosítás
    //    marad ki, az átirat kész.
    //  - Summarize / ExtractTopics: a futó LLM-kérés megszakad, a korábbi összefoglaló marad.
    //  - AnalyzeTopics: a meeting sorban álló témái kikerülnek, a futó megszakad; a már kész
    //    (perzisztált) elemzések megmaradnak, a folytatás onnan megy tovább.
    //  - Mixdown: az ffmpeg leáll, a félkész fájl törlődik, a korábbi keverék érintetlen.
    //  - Identify: a már megtalált egyezések mentődnek, a többi beszélő névtelen marad.
    // true, ha volt mit megszakítani. Megszakításnál NINCS errorOccurred és nem marad hiba.
    bool cancelJob(const QString& meetingId, tanara::JobKind kind);
    // A meeting MINDEN futó feladatának megszakítása (pl. törlés előtt).
    void cancelAllJobs(const QString& meetingId);
    // „Betöltés nagyobb kontextussal” (LM Studio): a KÖVETKEZŐ LLM-feladat előtt a modell
    // legalább `tokens` tokenes kontextussal (és parallel = 1-gyel) töltődik újra, ha a betöltött
    // példány ennél kisebb. Egyszer érvényes; a feladatot a hívó indítja újra. A Tanara Cloudnál
    // nincs hatása.
    void requestLlmContext(int tokens);
    // Egyetlen téma kivétele a sorból / futó elemzésének megszakítása.
    bool cancelTopicAnalysis(const QString& meetingId, const QString& topicId);

    // A téma-lista mentése a megadott SORRENDBEN (hozzáadás / törlés / szerkesztés /
    // átrendezés egyaránt ezzel perzisztál). Az üres id-jű (új) témák azonosítót kapnak, az
    // üres című témák kimaradnak; a törölt témák megmaradt hibái törlődnek. A kész
    // elemzések (summary.analyses.json) érintetlenek — a törölt témáké egyszerűen nem kerül
    // az összegzésbe. Visszaadja a mentett listát. topicsChanged jel.
    QVector<tanara::SummaryTopic> setMeetingTopics(const QString& meetingId,
                                                   const QVector<tanara::SummaryTopic>& topics);

    // Résztvevők azonosítása hang alapján ASZINKRON (az autoIdentifyMeeting nem-blokkoló
    // párja): a hang dekódolása + embedding háttérszálon fut, a párosítás és a mentés a fő
    // szálon. Haladás: jobs() (Identify feladat, „3 / 5 beszélő”); megszakítható. A végén
    // speakerMapChanged (ha lett új név). false, ha nincs hang-modell / átirat / már fut.
    bool identifyMeetingAsync(const QString& meetingId);

    // Az átírás utáni automatikus hang-azonosítás be/ki EGY meetingre (M03 „Résztvevők
    // azonosítása hang alapján” kapcsoló). Alapértelmezés: be. Kikapcsolva az átírás-feladat
    // „identify” szakasza Skipped, az átirat névtelen beszélőkkel készül el. A választás a
    // folyamat életére szól (nem perzisztált); az utólagos identifyMeetingAsync-et nem érinti.
    void setIdentifyAfterTranscription(const QString& meetingId, bool enabled);
    bool identifyAfterTranscription(const QString& meetingId) const;
    // Van-e használható hang-modell (nélküle nincs azonosítás-szakasz; a kapcsoló letiltható).
    bool voiceIdentificationAvailable() const;

    // Hullámforma-csúcsok kérése a meeting összes meglévő sávjára (+ a keverékre, trackId
    // "mixdown"). Eredmény: waveforms()->peaksReady / peaksFailed.
    void requestWaveforms(const QString& meetingId);

    // ---- címkejavaslatok (aszinkron; jelek: tagSuggestionsComputing → tagSuggestionsReady) ----
    // Hasonló megbeszélésekből (+ beágyazással, ha van). A meeting megjelenítésekor / új
    // átiratnál hívandó; meetingenként gyorsítótárazott, amíg a címkék, az átirat vagy az
    // index nem változik. Kikapcsolt javaslatoknál üres listát ad.
    void requestTagSuggestions(const QString& meetingId);
    // Egy címke felrakása után az együtt járók (source = Cooccur). Ha nincs ilyen, nem jön jel
    // (a korábbi javaslatok maradnak, a felrakott címke nélkül).
    void requestCooccurSuggestions(const QString& meetingId, const QString& tagId);
    // A nyelvi modell javaslata az összefoglalóból (source = Llm; új név-ötletek isNew-val).
    // Az összefoglaló elkészülte után magától fut, ha a llmTagSuggestions beállítás be van kapcsolva.
    void requestLlmTagSuggestions(const QString& meetingId);
    // A beágyazó provider beállítása (a settings.json-ba is). Modellváltás → az index
    // eldobása és az előkészítés újraindítása. Üres id → nincs beágyazás (alap szint).
    void setEmbeddingProvider(const QString& providerId, const tanara::ProviderConfig& config);

    // Titok (pl. Soniox API-kulcs) beállítása a KeyStore-ban. name pl. "soniox.apiKey".
    void setSecret(const QString& name, const QString& value);
    bool hasSecret(const QString& name) const;
    // A tárolt titok értéke ("" ha nincs) — a Beállítások kulcs-mezőjéhez (szem-gomb) és a
    // „Kapcsolat tesztelése” próbához. Máshol ne jelenítsd meg.
    QString secret(const QString& name) const;

    // Számlált szintfigyelés több fogyasztónak (a Beállítások „Rögzítés” lapja a felvevő
    // mellett): amíg legalább egy fogyasztó kéri — vagy a start/stopLevelMonitoring párosa
    // (a felvevő) él —, a figyelés megy; az utolsó elengedésekor leáll. A megszűnő `owner`
    // kérése magától elengedődik.
    void retainLevelMonitoring(QObject* owner);
    void releaseLevelMonitoring(QObject* owner);

signals:
    void devicesChanged();
    // Élő szint ESZKÖZNÉV szerint, csúccsal (~30 Hz) — felvétel előtt minden figyelt eszközre,
    // felvétel alatt a rögzített sávokra és (a szintfigyelésből) a többire is.
    void deviceLevelPeak(QString deviceName, float rms, float peak);
    void recordingTrackAdded(QString deviceName);    // felvétel közben új sáv indult
    void recordingTrackClosed(QString deviceName);   // a rögzített eszközt leválasztották
    void recordingStateChanged(tanara::RecordingState state);
    void elapsedChanged(qint64 ms);
    void recordingFinished(tanara::Meeting meeting);
    // Az importálás elkészült: a meeting a tárban van (a könyvtár felvette).
    void importFinished(tanara::Meeting meeting);
    // Az importálás nem sikerült (nem maradt utána semmi). detail: technikai sor, lehet üres.
    void importFailed(QString importId, QString message, QString detail);
    void importCancelled(QString importId);
    // Archívum-művelet haladása (opId: az exportMeetingArchive / importMeetingArchive
    // visszatérési értéke) és vége. ok == false: message a hiba (megszakításnál „Megszakítva.”).
    // Exportnál path a kész fájl, importnál meetingId az új megbeszélés.
    void archiveProgress(QString opId, int percent);
    void archiveFinished(QString opId, bool ok, QString meetingId, QString path, QString message);
    // Felvétel közben a hívás véget ért (a detektor 2 egymást követő pollban inaktívat
    // látott egy korábban aktív hívás után). A UI ebből kérdez rá a leállításra.
    void callEnded(QString appName);
    // Felvétel közben minden sáv legalább `minutes` perce csendes (silenceAskMinutes).
    // A UI ebből kérdez rá a leállításra; hang visszatértekor újra-élesedik.
    void silenceDetected(int minutes);
    void transcriptReady(QString meetingId, QString markdownPath);
    void summaryReady(QString meetingId, QString markdownPath);
    void topicsReady(QString meetingId, QVector<tanara::SummaryTopic> topics);  // komplex 1. kör
    // Komplex 2. kör — PER-TÉMA életciklus (a UI a topicId-vel címzi a kártyát):
    void topicAnalysisQueued(QString meetingId, QString topicId);    // sorba került
    void topicAnalysisStarted(QString meetingId, QString topicId);   // elemzés fut
    void topicAnalysisReady(QString meetingId, tanara::TopicAnalysis analysis);  // kész + perzisztálva
    void topicAnalysisFailed(QString meetingId, QString topicId, QString error); // csak ez a téma bukott
    void topicAnalysisQueueFinished(QString meetingId, int okCount, int failCount); // a sor kiürült
    // A téma-lista változott (javaslat érkezett / setMeetingTopics / analyzeTopic szerkesztés).
    void topicsChanged(QString meetingId);
    // Egy téma állapota változott (lásd topicStatuses) — a kártya inkrementális frissítéséhez.
    void topicStatusChanged(QString meetingId, QString topicId);
    void speakerMapChanged(QString meetingId);              // beszélő-átnevezés után
    void summaryStaleChanged(QString meetingId);            // az összefoglaló elavult-jelzője változott
    void peopleChanged();                                   // személy-lista változott
    void voiceprintsChanged();                              // voice-ID lenyomat-DB változott
    void tracksChanged(QString meetingId);                  // sáv aktív/eldobott/törölve
    void mixdownUpdated(QString meetingId, bool ok);        // regenerateMixdown eredménye
    void mixdownProgress(QString meetingId, int pct);       // lekeverés haladása 0..100
    // ---- Tanara Cloud feldolgozás visszajelzései (K-07, K-09…K-12) ----
    // Egy futás (átírás / összefoglaló / komplex köteg) sikeres vége: a hívásonkénti
    // terhelések összege (a gateway adta — a kliens nem számol), hívásszám, új egyenleg.
    // kind: transcribe | summary | topics | complex.
    void cloudCharged(QString meetingId, QString kind, tanara::Money total, int calls,
                      tanara::Money balance, QString vatMode);
    // Az átírás upstream-hibával zárult, a gateway automatikusan visszaírta a díjat.
    void cloudRefunded(QString meetingId, tanara::Money refund, tanara::Money balance,
                       QString requestId);
    // Strukturált gateway-hiba egy futásban. chargedSoFar: a futás már terhelt hívásainak
    // összege (részleges hibánál > 0; ilyenkor NEM igaz, hogy „semmit nem terheltünk”).
    void cloudError(QString meetingId, QString kind, tanara::CloudError error,
                    tanara::Money chargedSoFar);

    // Címkejavaslatok: a számolás elindult / kész (a lista lehet üres). A Ready mindig a
    // Computing után jön, de ugyanabban az eseményhurok-körben is jöhet.
    void tagSuggestionsComputing(QString meetingId);
    void tagSuggestionsReady(QString meetingId, QVector<tanara::TagSuggestion> suggestions);

    void jobProgress(QString meetingId, QString message);   // átírás/összefoglaló állapot
    void errorOccurred(QString message);

private:
    // Hívás-vég figyelés felvétel közben (askStopOnCallEnd) — a figyelő detektor-magjával.
    void startCallEndMonitor();
    void stopCallEndMonitor();
    void pollCallEnd();
    // A szintfigyelő (újra)indítása a megfelelő eszköz-halmazzal; force nélkül csak akkor,
    // ha a halmaz változott. Felvétel alatt a rögzített eszközök kimaradnak.
    void restartLevelMonitor(bool force);
    void beginLevelMonitoring();
    void endLevelMonitoring();
    // Eszköz-újrafelsorolás után: felvétel alatt az eltűnt rögzített eszköz sávjának lezárása.
    void handleDeviceSetChange();
    // A kész (friss) mixdownt egyetlen Soniox-kéréssel írja át; a transcribeMeeting ehhez
    // láncolja a lekeverés elkészültét. A beszélő-szeparációt a Soniox diarizációja adja
    // („Beszélő N" címkék) — a nevet utólag a voice-ID / kézi átnevezés oldja fel.
    void transcribeFromMixdown(const QString& meetingId);

    // Téma-elemzés job-sor: a témák sorba kerülnek (topicAnalysisQueued), egyszerre EGY fut
    // (lokális modell), a kész eredmény azonnal perzisztálódik. Egy téma hibája nem állítja
    // le a sort. reduceWhenDone=true → a sor kiürülésekor (ha nincs bukás) finalize.
    void enqueueTopicAnalyses(const QString& meetingId,
                              const QVector<tanara::SummaryTopic>& topics, bool reduceWhenDone);
    void startNextTopicJob();

    // Tanara Cloud feldolgozás-futás (X-Tanara-Job-Id, terhelés-összesítő, utolsó hiba).
    struct CloudRun;
    using CloudRunPtr = std::shared_ptr<CloudRun>;
    CloudRunPtr newCloudRun(const QString& meetingId, const QString& kind) const;
    // A futás gateway-configja (baseUrl, kulcs, modell, X-Tanara-* fejlécek, válasz-hook).
    ProviderConfig cloudConfig(WorkflowStep step, const CloudRunPtr& run,
                               const QString& summaryMode) const;
    // BYO: a kiválasztott LLM config + kulcs (run marad null). Cloud: gateway-config; ha a
    // run még null, új futás indul (kind / summaryMode szerint).
    ProviderConfig llmConfigFor(CloudRunPtr& run, const QString& meetingId, const QString& kind,
                                const QString& summaryMode) const;
    void finishCloudRun(const CloudRunPtr& run);                       // cloudCharged
    // Az átirat utáni automatikus azonosítás az átírás-feladat utolsó szakaszaként (aszinkron).
    bool startIdentify(const QString& meetingId, bool asTranscribeStage);
    // Címkejavaslatok: a kiszámolt lista kiadása (szűrve), ill. a beágyazás beállítása.
    void publishTagSuggestions(const QString& meetingId, QVector<tanara::TagSuggestion> list);
    void computeTagSuggestions(const QString& meetingId);
    void applyEmbeddingSettings(bool restart);
    // Hiba: strukturált gateway-hiba → cloudError, különben errorOccurred(fallback).
    void failCloudRun(const CloudRunPtr& run, const QString& fallbackMessage);

    struct Impl;
    std::unique_ptr<Impl> d;
};

// A KeyStore kulcsnevei (egy helyen).
namespace keys {
inline const QString SonioxApiKey = QStringLiteral("soniox.apiKey");
inline const QString LlmApiKey    = QStringLiteral("llm.apiKey");
}

} // namespace tanara

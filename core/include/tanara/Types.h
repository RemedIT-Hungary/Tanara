#pragma once
//
// Tanara — közös adattípusok (a backend ⟷ UI szerződés magja).
// Minden modul ezekre épít. QObject NINCS itt: ezek sima value-típusok,
// hogy queued signalokban átküldhetők legyenek (Q_DECLARE_METATYPE lent).
//
#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>
#include <QVariantMap>
#include <QMap>
#include <QMetaType>
#include <QByteArray>
#include <QList>
#include <QPair>

#include <functional>

namespace tanara {

struct HttpExchange;   // tanara/cloud/CloudTypes.h

QString libraryVersion();   // definíció: core/src/Version.cpp

// ---- enumok ----------------------------------------------------------------
enum class TrackKind { Mic, Loopback, Other };
enum class RecordingState { Idle, Recording, Stopping, Encoding };
enum class JobState { Idle, Uploading, Queued, Processing, Completed, Failed };

// ---- audio eszköz / sáv ----------------------------------------------------
struct AudioDeviceInfo {
    QString id;
    QString name;
    TrackKind kind = TrackKind::Mic;
    bool isDefault = false;
    int sampleRate = 48000;
    int channels = 1;
};

struct Track {
    QString id;             // pl. "mic", "loopback", deviceslug
    QString deviceName;
    QString file;           // relatív a meeting-mappához (pl. "track_mic.ogg")
    QString speakerLabel;   // ehhez a sávhoz tartozó (fix) beszélő
    TrackKind kind = TrackKind::Mic;
    bool fixedSpeaker = false;
    int sampleRate = 48000;
    int channels = 1;
    bool active = true;     // false = felvétel után csendesnek ítélve, eldobva (fájl MARAD)
    float peakLevel = 0.0f; // a felvétel alatti csúcs-RMS (a megtartás-döntéshez)
    QString customName;     // a felhasználó által adott sávnév; üres → barátságos név (TrackCatalog)
    // A sáv hangja ennyivel KÉSŐBB kezdődik a felvétel 0-pontjánál (ms). 0 = együtt indult.
    // A fájl a ténylegesen felvett szakasz (csend-kitöltés NINCS): a felvevő a felvétel közben
    // bekapcsolt eszköz sávjának, ill. egy ki-be kapcsolt eszköz minden szakaszának (új fájl:
    // track_<slug>-2.ogg …) a megnyitás idejét írja ide. A lekeverés ennyi csenddel tolja el
    // (adelay); a sávfájlból olvasók a megbeszélés-időt ennyivel tolják (tracktiming). Régi /
    // máshonnan hozott mappánál a CLI `align` parancsa állítja (--auto: közös vég feltételezve).
    qint64 startOffsetMs = 0;
};

// ---- transcript ------------------------------------------------------------
struct TranscriptToken {
    QString text;
    QString speaker;        // Soniox "speaker" (per-sáv általában fix label)
    qint64 startMs = 0;
    qint64 endMs = 0;
    double confidence = 0.0;
    QString trackId;        // melyik sávból jött (merge során töltjük)
};

struct TrackTranscript {
    QString trackId;
    QString speakerLabel;
    QString language;
    QVector<TranscriptToken> tokens;
};

// Egy összefüggő beszéd-blokk (egy beszélő, szünetig). A lejátszó-szinkronhoz
// és a kattintható transcripthez ez a gépiesen kezelhető egység.
struct Utterance {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString speaker;
    QString text;
};

struct MergedTranscript {
    QString language;
    QVector<TranscriptToken> tokens;     // startMs szerint rendezve, sávok összefésülve
    QVector<Utterance> segments() const; // beszélőnként, szünet-alapú blokkok időrendben
    QString renderMarkdown() const;      // impl: TranscriptMerger modul
};

// ---- összefoglaló ----------------------------------------------------------
struct ActionItem {
    QString text;
    QString owner;
    QString due;
};

// A memó egy szakasza: egy tárgy, amiről a megbeszélésen szó volt, időrendben. A pontok a
// konkrétumok (nevek, számok, dátumok; ki mondta, ha számít). Az idő -1, ha nem ismert.
struct MemoSection {
    QString     title;
    qint64      startMs = -1;
    qint64      endMs = -1;
    QStringList points;
};

// Egy összefoglaló-futás eredménye: a RÖVID forma (vezetői összefoglaló + listák) és a
// HOSSZÚ forma (memó) ugyanabból a részenkénti jegyzetből készül, így egymással konzisztensek.
struct Summary {
    QString execSummary;
    QStringList decisions;
    QStringList openQuestions;           // eldöntetlen, nyitva maradt ügyek (legfeljebb ~5)
    QVector<ActionItem> actionItems;
    QStringList participants;
    QVector<MemoSection> memo;           // a megbeszélés részletes jegyzete, időrendben
    QString renderMarkdown() const;      // impl: SummaryService modul
};

// ---- komplex (több körös, téma-bontásos) összefoglaló ----------------------
// 1. kör: a teljes átiratból kinyert TÉMÁK, a felhasználó által szerkeszthető
// köztes állapot (cím + 1 soros gist). A 2. kör ezekre a (jóváhagyott) témákra megy.
struct SummaryTopic {
    QString id;        // stabil azonosító (QUuid) — túléli a szerkesztést/köröket
    QString title;
    QString summary;   // 1-2 mondatos gist (az 1. kör adja; szerkeszthető)
};

// 2. kör eredménye egy témára: részletes összegző + döntések + teendők.
struct TopicAnalysis {
    QString topicId;
    QString title;
    QString detail;
    QStringList decisions;
    QStringList openQuestions;           // a témában nyitva maradt kérdések (opcionális)
    QVector<ActionItem> actionItems;
    // A törzs markdownja CÍM NÉLKÜL (detail + döntések + teendők) — a téma-kártya
    // eredmény-nézete és a komplex summary.md témaszekciói is ebből épülnek.
    QString renderMarkdown() const;      // impl: SummaryService modul
};

// ---- meeting --------------------------------------------------------------
struct Meeting {
    QString id;
    QString title;
    QString folder;          // abszolút mappa-út
    QDateTime startedAt;
    qint64 durationMs = 0;
    QVector<Track> tracks;
    QString mixdownFile;     // pl. "mixdown.mp3"
    bool mixdownDirty = false;   // true → a sávok változtak a mixdown óta (újrakeverés kell)
    bool hasTranscript = false;
    bool hasSummary = false;
    QMap<QString, QString> speakerMap;   // nyers beszélő-címke ("Távoli 1") → valódi név ("Béla")
    // A felhasználó pár szavas leírása a meetingről (téma/kontextus). Beépül a STT
    // context-envelope-jába (pontosabb átirat a kétes részeknél) ÉS az LLM-összefoglaló
    // kontextusába is. Az átirat előtti nézetben és az Összefoglaló fülön szerkeszthető; ide
    // valók a nevek, szakszavak és az ismert félrehallások („A „…” helyesen: …”).
    QString contextNote;
    // A figyelő által észlelt hívás-alkalmazás (pl. „Microsoft Teams”); üres, ha kézi felvétel.
    // Csak tájékoztató: sem az átírónak, sem az összefoglalónak nem megy.
    QString detectedCallApp;
    // A megbeszélés címkéinek azonosítói, a felrakás sorrendjében (a címkekészlet:
    // <metadataDir>/tags.json, lásd tags/TagService.h). A meeting.json-ban "tags".
    QStringList tagIds;
};

// ---- beszélő-azonosítás (voice fingerprint) -------------------------------
// Egy hang-lenyomat: egy beszélő egy reprezentatív szegmenséből számolt,
// L2-normalizált embedding-vektor + a forrás metaadatai. Egy névhez több is
// tartozhat (más mikrofon más akusztikai aláírást ad).
struct Voiceprint {
    QString id;                 // egyedi azonosító (QUuid)
    QVector<float> embedding;   // L2-normalizált beszélő-embedding
    int dim = 0;                // embedding.size() (redundáns, de a JSON-ban hasznos)
    QString sourceMeetingId;    // melyik meetingből származik
    QString sourceTrack;        // pl. "loopback" / "mic" / track-id
    QString device;             // a felvevő eszköz neve (több-mikrofonos kontextus)
    QString sampleRef;          // "<folder>/track_x.ogg#startMs-endMs" — az idők megbeszélés-időben
    QString createdAt;          // ISO-8601
};

// Egy párosítás eredménye: melyik személy, milyen (cosine) pontszámmal.
struct VoiceMatch {
    QString name;
    double score = -1.0;        // [-1..1] cosine; <0 = nincs találat / üres DB
};

// Egy ÁTÍRÁS ELŐTT (lokálisan) felismert beszélő-jelölt egy sávon: a hang-klaszter
// + a DB-beli legjobb találat (vagy üres név = ismeretlen). A reprezentatív
// szegmens visszahallgatható / elnevezhető (= betanítás) az átírás előtt.
struct ParticipantGuess {
    QString trackId;
    QString deviceName;
    QString name;          // a DB-találat neve, vagy "" ha ismeretlen (küszöb alatt)
    double  score = -1.0;  // a találat cosine pontszáma
    int     windows = 0;   // hány hang-ablakból állt a klaszter (megbízhatóság)
    QString sampleRef;     // "track_x.ogg#startMs-endMs" — reprezentatív minta (megbeszélés-időben)
};

// ---- provider konfiguráció / beállítások ----------------------------------
struct ProviderConfig {
    QString type;            // "soniox" | "openai-compat" | ...
    QString baseUrl;
    QString apiKey;          // futásidőben; tároláskor keychainbe megy
    QString model;
    double temperature = 0.2; // LLM mintavételezési hőmérséklet (összefoglaló); STT nem használja
    int maxTokens = 8000;     // LLM válasz max tokenszáma (reasoning-modellnek bőven); STT nem használja
    // A modell „gondolkodása” (reasoning) — csak LLM. Mérés szerint az összefoglalóhoz KI kell
    // kapcsolni (különben a kimeneti keret elfogy gondolkodásra, és 2–5× lassabb):
    //  "auto" — kikapcsolva; a módszert a modellnév dönti el (Gemma: reasoning_effort "none";
    //           Qwen 3+: üres <think> blokkal előtöltött asszisztens-üzenet; ismeretlen:
    //           reasoning_effort "none", amit a nem ismerő szerverek figyelmen kívül hagynak),
    //  "off"  — ugyanaz, mint az auto (kifejezett kikapcsolás),
    //  "on"   — nem küldünk kapcsolót: a modell alapviselkedése (gondolkodó modell gondolkodik).
    QString reasoning{QStringLiteral("auto")};
    // A modell kontextusa (tokenben) — csak LLM. 0 = automatikus: a Tanara a feladat indulásakor
    // a legkisebb szabványos lépcsőt kéri (8k / 16k / 20k / 32k …), ami a feladatot lefedi
    // (legfeljebb a modell maximumáig). Rögzített érték: mindig ekkorával töltjük be. Csak
    // LM Studiónál hat (natív API-val betöltés), más szervernél a betöltést ott kell beállítani.
    int contextLength = 0;
    QVariantMap extra;

    // --- futásidejű (NEM perzisztált) gateway-hookok — csak a Tanara Cloud útvonal tölti ---
    // Minden kéréshez hozzáadott fejlécek (X-Tanara-Client, Accept-Language, X-Tanara-Job-Id …).
    QList<QPair<QByteArray, QByteArray>> extraHeaders;
    // Minden HTTP-válasz (siker, hiba, hálózati hiba) visszajelzése a hívónak — ebből olvassa a
    // cloud-réteg a terhelést, a visszaírást, a request id-t és a strukturált hibát.
    std::function<void(const HttpExchange&)> onExchange;
};

struct AppSettings {
    QString audioDir;            // hova menti a felvételeket
    QString notesDir;            // hova az összefoglaló .md másolatát (vault Meetings/)
    QString metadataDir;         // pl. ~/.tanara
    QString userSpeakerName;     // a mic-sáv fix neve (pl. "Ádám")
    bool autoRecordAllDevices = true;  // true → minden eszközt rögzít (csendeseket utólag eldobja)
    QStringList languageHints{QStringLiteral("hu")};

    // UI-nyelv: "auto" (rendszer-locale dönt) | "hu" | "en". A forrásnyelv a magyar;
    // váltás után újraindítás kell (nincs futásidejű retranslate). Lásd Localization.h.
    QString uiLanguage{QStringLiteral("auto")};

    // Felvételi hangminőség (per-sáv Opus bitráta). A nyers per-sáv .ogg-kat az STT is
    // használja, ezért a legalsó fokozat is „STT-biztos" (24 kbps). Gyengébb fokozat =
    // kisebb fájlok. Értékek: "best"|"high"|"medium"|"low".
    QString audioQuality{QStringLiteral("best")};

    // Lekeverés (mixdown) időzítése a felvétel leállítása után. "auto" → háttérben,
    // nem-blokkolóan azonnal; "manual" → csak kézi indításra (a review-panel gombja).
    // A mixdown CSAK hallgatásra kell (az STT a per-sáv .ogg-kból megy), ezért a stop()
    // sosem várja meg — a fő szál nem fagy.
    QString mixdownMode{QStringLiteral("auto")};

    // Az összefoglaló LLM rendszer-promptjai. ÜRES → a fájl-override / beépített default
    // (PromptLibrary) érvényes (így a kód-default jövőbeli javításai automatikusan
    // érvényesülnek, amíg a felhasználó nem ír sajátot).
    //  summaryPrompt — az EGY részből álló (rövid) megbeszélés egylépéses promptja ("single":
    //                  memó-jegyzet + vezetői összefoglaló és listák egy hívásban),
    //  notesPrompt   — hosszabb megbeszélésnél a részenkénti jegyzetelés ("notes"),
    //  mergePrompt   — a részjegyzetek összegzése ("merge").
    QString summaryPrompt;
    QString notesPrompt;
    QString mergePrompt;

    // A komplex (több körös) összefoglaló két szerkeszthető prompt-ja. ÜRES → a kód-default
    // (ComplexSummaryService::defaultTopicPrompt() / defaultAnalysisPrompt()).
    QString topicExtractionPrompt;   // 1. kör: téma-kinyerés
    QString topicAnalysisPrompt;     // 2. kör: témánkénti elemzés

    // Az összefoglaló CÉLNYELVE (szabad szöveg, pl. "magyar", "angol", "német") — a
    // promptok {{NYELV}} placeholderébe kerül (PromptLibrary::applySummaryLanguage).
    // Független a UI-nyelvtől. A strukturális szakaszcímek (## Döntések/## Teendők)
    // nyelvtől függetlenül magyarok maradnak (a parser ezekre illeszt).
    QString summaryLanguage{QStringLiteral("magyar")};

    // Multi-provider: a kiválasztott provider id-ja típusonként + providerenkénti
    // config (így a váltás nem törli a másik provider beállításait). A régi egyetlen
    // `stt`/`llm` shape JSON-ből migrálódik (lásd JsonSerialization).
    // Tanara Cloud (a gateway-szerződés: docs/cloud-gateway-api.yaml).
    //  cloudEnabled — a bejelentkezős cloud-mód él („indulás után”); false → a Beállítások
    //                 Tanara Cloud szekciója a „Hamarosan” (várólista) panelt mutatja.
    //                 A TANARA_CLOUD_URL környezeti változó fejlesztéshez felülírja (bekapcsol).
    //  cloudBaseUrl — gateway alap-URL (/v1 nélkül); üres → az éles alapértelmezés.
    //  cloudSttTier / cloudLlmTier — Gyors / Pontos szint feladatonként (fast | accurate).
    //  cloudSttModel / cloudLlmModel — Expert mód: konkrét katalógus-id; üres → a tier virtuális modellje.
    //  waitlistEmail — a „Hamarosan” panelről feliratkozott cím (nem üres → nem kérdez újra).
    bool    cloudEnabled = false;
    QString cloudBaseUrl;
    QString cloudSttTier{QStringLiteral("accurate")};
    QString cloudLlmTier{QStringLiteral("accurate")};
    QString cloudSttModel;
    QString cloudLlmModel;
    QString waitlistEmail;
    //  cloudEstimateBeforeRun — Tanara Cloud feldolgozás előtt mindig költségbecslés +
    //                 megerősítés (alapból be). Kikapcsolva a futás becslés nélkül indul; a
    //                 hibaágak (elfogyott egyenleg, ÁSZF) ugyanúgy megállítják.
    bool    cloudEstimateBeforeRun = true;

    // Hangeszközök felhasználói neve: nyers OS-eszköznév → barátságos név. Mindenhol ez
    // látszik, ahol az eszköz vagy a sávja névvel szerepel (felvevő, Sávok fül, értesítés).
    // A feloldás EGY helyen történik: tanara::devicenames (audio/TrackCatalog.h); a
    // SettingsManager tölti be minden betöltéskor / mentéskor.
    QMap<QString, QString> deviceNames;

    // Az „Első lépések” ablak (OnboardingWindow.qml) már lezajlott: bezárták, kihagyták vagy
    // végigmentek rajta. Csak az automatikus megnyitást kapcsolja ki (első indításkor egyszer);
    // a Fájl › „Első lépések…” menüből és a Beállításokból bármikor újranyitható.
    bool onboardingDone = false;

    QString sttProviderId{QStringLiteral("soniox")};
    QString llmProviderId{QStringLiteral("openai-compat")};
    QMap<QString, ProviderConfig> sttConfigs;   // id -> config
    QMap<QString, ProviderConfig> llmConfigs;   // id -> config

    // Back-compat accessorok: a kiválasztott provider futásidejű configja.
    // A hívóhelyek nagy része ezekre cserélhető (s.stt → s.sttSelected()).
    ProviderConfig sttSelected() const { return sttConfigs.value(sttProviderId); }
    ProviderConfig llmSelected() const { return llmConfigs.value(llmProviderId); }

    // Beágyazó modell (embedding) a címkejavaslatokhoz. Üres id → nincs („alap” szint: a
    // javaslatok a közös résztvevőkből, kifejezésekből és a címből készülnek). Lásd
    // embedding/EmbeddingProviderRegistry.h: "openai-compat-embedding" | "tanara-hosted-embedding".
    QString embeddingProviderId;
    QMap<QString, ProviderConfig> embeddingConfigs;   // id -> config
    ProviderConfig embeddingSelected() const { return embeddingConfigs.value(embeddingProviderId); }
    // Címkejavaslatok be/ki, ill. a nyelvi modell javasoljon-e az összefoglaló után.
    bool tagSuggestions = true;
    bool llmTagSuggestions = true;

    // Bekapcsolt beszélő-embedding modellek (VoiceModelRegistry id-k), ábécérendben. Lenyomatnál
    // és elemzésnél mindegyik számít (fúzió); a hiányzó modellfájl futáskor kimarad, a beállítás
    // megmarad. A settings.json "voiceModels" kulcsa; hiányzik → ["campplus"].
    QStringList voiceModels{QStringLiteral("campplus")};

    // Meeting-figyelő (háttér-detektor + tray). A figyelő olvassa; az elemző/felvevő
    // nem függ tőle. detectorId üres → a registry az első elérhető detektort választja.
    bool detectorEnabled = true;              // aktív-hívás észlelés be/ki
    int  detectorIntervalSec = 8;             // poll-intervallum (mp); épkézláb tartomány 5–30
    bool watcherAutostart = false;            // a figyelő induljon-e bejelentkezéskor
    // Felvétel közben a rögzítő is figyeli a hívást: ha az véget ér (az app leáll / elengedi
    // a mikrofont), rákérdez a leállításra. Nem állít le magától.
    bool askStopOnCallEnd = true;
    // Csend-figyelés felvétel közben: ha MINDEN sáv ennyi percig csendes, rákérdez a
    // leállításra (Teams/böngésző esetén a mikrofon-elengedés nem mindig látszik a
    // detektornak — ez a detektortól FÜGGETLEN háló). 0 = kikapcsolva.
    int  silenceAskMinutes = 3;
    QString detectorId;                       // üres → MeetingDetectorRegistry::createBest()
    // Ismert hívás-appok (bináris/app-név részletek) — ezekre jelez a detektor.
    QStringList knownCallApps{
        QStringLiteral("zoom"), QStringLiteral("teams"), QStringLiteral("webex"),
        QStringLiteral("slack"), QStringLiteral("discord"), QStringLiteral("meet"),
        QStringLiteral("skype"), QStringLiteral("chromium"), QStringLiteral("firefox")};
};

// A felvételi hangminőség-fokozat → per-sáv Opus bitráta (kbps). Ismeretlen → 64 ("best").
// A legalsó fokozat (24 kbps) az „STT-talp": az alá nem megyünk, hogy a per-sáv .ogg-ból
// dolgozó átírás pontossága ne romoljon.
inline int opusBitrateKbps(const QString& quality) {
    if (quality == QStringLiteral("low"))    return 24;
    if (quality == QStringLiteral("medium")) return 32;
    if (quality == QStringLiteral("high"))   return 48;
    return 64;   // "best" / ismeretlen
}

} // namespace tanara

Q_DECLARE_METATYPE(tanara::AudioDeviceInfo)
Q_DECLARE_METATYPE(tanara::Track)
Q_DECLARE_METATYPE(tanara::TranscriptToken)
Q_DECLARE_METATYPE(tanara::TrackTranscript)
Q_DECLARE_METATYPE(tanara::Utterance)
Q_DECLARE_METATYPE(tanara::MergedTranscript)
Q_DECLARE_METATYPE(tanara::ActionItem)
Q_DECLARE_METATYPE(tanara::MemoSection)
Q_DECLARE_METATYPE(tanara::Summary)
Q_DECLARE_METATYPE(tanara::SummaryTopic)
Q_DECLARE_METATYPE(QVector<tanara::SummaryTopic>)
Q_DECLARE_METATYPE(tanara::TopicAnalysis)
Q_DECLARE_METATYPE(tanara::Meeting)
Q_DECLARE_METATYPE(tanara::Voiceprint)
Q_DECLARE_METATYPE(tanara::VoiceMatch)
Q_DECLARE_METATYPE(tanara::ProviderConfig)
Q_DECLARE_METATYPE(tanara::AppSettings)

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

namespace tanara {

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

struct Summary {
    QString execSummary;
    QStringList decisions;
    QVector<ActionItem> actionItems;
    QStringList participants;
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
    // kontextusába is. Átíráskor a UI bekéri (előtöltve ezzel az értékkel).
    QString contextNote;
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
    QString sampleRef;          // "<folder>/track_x.ogg#startMs-endMs"
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
    QString sampleRef;     // "track_x.ogg#startMs-endMs" — reprezentatív minta
};

// ---- provider konfiguráció / beállítások ----------------------------------
struct ProviderConfig {
    QString type;            // "soniox" | "openai-compat" | ...
    QString baseUrl;
    QString apiKey;          // futásidőben; tároláskor keychainbe megy
    QString model;
    double temperature = 0.2; // LLM mintavételezési hőmérséklet (összefoglaló); STT nem használja
    int maxTokens = 8000;     // LLM válasz max tokenszáma (reasoning-modellnek bőven); STT nem használja
    QVariantMap extra;
};

struct AppSettings {
    QString audioDir;            // hova menti a felvételeket
    QString notesDir;            // hova az összefoglaló .md másolatát (vault Meetings/)
    QString metadataDir;         // pl. ~/.tanara
    QString userSpeakerName;     // a mic-sáv fix neve (pl. "Ádám")
    bool autoRecordAllDevices = true;  // true → minden eszközt rögzít (csendeseket utólag eldobja)
    QStringList languageHints{QStringLiteral("hu")};

    // Felvételi hangminőség (per-sáv Opus bitráta). A nyers per-sáv .ogg-kat az STT is
    // használja, ezért a legalsó fokozat is „STT-biztos" (24 kbps). Gyengébb fokozat =
    // kisebb fájlok. Értékek: "best"|"high"|"medium"|"low".
    QString audioQuality{QStringLiteral("best")};

    // Lekeverés (mixdown) időzítése a felvétel leállítása után. "auto" → háttérben,
    // nem-blokkolóan azonnal; "manual" → csak kézi indításra (a review-panel gombja).
    // A mixdown CSAK hallgatásra kell (az STT a per-sáv .ogg-kból megy), ezért a stop()
    // sosem várja meg — a fő szál nem fagy.
    QString mixdownMode{QStringLiteral("auto")};

    // Az összefoglaló LLM rendszer-promptja (a séma + szabályok). ÜRES → a beépített
    // SummaryService::defaultSystemPrompt() érvényes (így a kód-default jövőbeli javításai
    // automatikusan érvényesülnek, amíg a felhasználó nem ír sajátot).
    QString summaryPrompt;

    // A komplex (több körös) összefoglaló két szerkeszthető prompt-ja. ÜRES → a kód-default
    // (ComplexSummaryService::defaultTopicPrompt() / defaultAnalysisPrompt()).
    QString topicExtractionPrompt;   // 1. kör: téma-kinyerés
    QString topicAnalysisPrompt;     // 2. kör: témánkénti elemzés

    // Multi-provider: a kiválasztott provider id-ja típusonként + providerenkénti
    // config (így a váltás nem törli a másik provider beállításait). A régi egyetlen
    // `stt`/`llm` shape JSON-ből migrálódik (lásd JsonSerialization).
    QString sttProviderId{QStringLiteral("soniox")};
    QString llmProviderId{QStringLiteral("openai-compat")};
    QMap<QString, ProviderConfig> sttConfigs;   // id -> config
    QMap<QString, ProviderConfig> llmConfigs;   // id -> config

    // Back-compat accessorok: a kiválasztott provider futásidejű configja.
    // A hívóhelyek nagy része ezekre cserélhető (s.stt → s.sttSelected()).
    ProviderConfig sttSelected() const { return sttConfigs.value(sttProviderId); }
    ProviderConfig llmSelected() const { return llmConfigs.value(llmProviderId); }
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
Q_DECLARE_METATYPE(tanara::Summary)
Q_DECLARE_METATYPE(tanara::SummaryTopic)
Q_DECLARE_METATYPE(QVector<tanara::SummaryTopic>)
Q_DECLARE_METATYPE(tanara::TopicAnalysis)
Q_DECLARE_METATYPE(tanara::Meeting)
Q_DECLARE_METATYPE(tanara::Voiceprint)
Q_DECLARE_METATYPE(tanara::VoiceMatch)
Q_DECLARE_METATYPE(tanara::ProviderConfig)
Q_DECLARE_METATYPE(tanara::AppSettings)

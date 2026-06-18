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

struct MergedTranscript {
    QString language;
    QVector<TranscriptToken> tokens;     // startMs szerint rendezve, sávok összefésülve
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

// ---- meeting --------------------------------------------------------------
struct Meeting {
    QString id;
    QString title;
    QString folder;          // abszolút mappa-út
    QDateTime startedAt;
    qint64 durationMs = 0;
    QVector<Track> tracks;
    QString mixdownFile;     // pl. "mixdown.mp3"
    bool hasTranscript = false;
    bool hasSummary = false;
};

// ---- provider konfiguráció / beállítások ----------------------------------
struct ProviderConfig {
    QString type;            // "soniox" | "openai-compat" | ...
    QString baseUrl;
    QString apiKey;          // futásidőben; tároláskor keychainbe megy
    QString model;
    QVariantMap extra;
};

struct AppSettings {
    QString audioDir;            // hova menti a felvételeket
    QString notesDir;            // hova az összefoglaló .md másolatát (vault Meetings/)
    QString metadataDir;         // pl. ~/.tanara
    QString userSpeakerName;     // a mic-sáv fix neve (pl. "Ádám")
    QStringList languageHints{QStringLiteral("hu")};
    ProviderConfig stt;          // Soniox
    ProviderConfig llm;          // OpenAI-kompatibilis (LM Studio)
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::AudioDeviceInfo)
Q_DECLARE_METATYPE(tanara::Track)
Q_DECLARE_METATYPE(tanara::TranscriptToken)
Q_DECLARE_METATYPE(tanara::TrackTranscript)
Q_DECLARE_METATYPE(tanara::MergedTranscript)
Q_DECLARE_METATYPE(tanara::ActionItem)
Q_DECLARE_METATYPE(tanara::Summary)
Q_DECLARE_METATYPE(tanara::Meeting)
Q_DECLARE_METATYPE(tanara::ProviderConfig)
Q_DECLARE_METATYPE(tanara::AppSettings)

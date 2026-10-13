#pragma once
//
// Shared data types (the core of the backend <-> UI contract).
// Every module builds on these. No QObject here: these are plain value types so they
// can be sent in queued signals (Q_DECLARE_METATYPE below).
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
#include <optional>

#include "tanara/edit/Evidence.h"

namespace tanara {

struct HttpExchange;   // tanara/cloud/CloudTypes.h

QString libraryVersion();   // defined in core/src/Version.cpp

// ---- enumok ----------------------------------------------------------------
enum class TrackKind { Mic, Loopback, Other };
enum class RecordingState { Idle, Recording, Stopping, Encoding };
enum class JobState { Idle, Uploading, Queued, Processing, Completed, Failed };

// ---- audio device / track ----------------------------------------------------
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
    QString file;           // relative to the meeting folder (e.g. "track_mic.ogg")
    QString speakerLabel;   // the (fixed) speaker belonging to this track
    TrackKind kind = TrackKind::Mic;
    bool fixedSpeaker = false;
    int sampleRate = 48000;
    int channels = 1;
    bool active = true;     // false = judged silent after recording and dropped (the file STAYS)
    float peakLevel = 0.0f; // peak RMS during recording (for the keep/drop decision)
    QString customName;     // track name given by the user; empty = friendly name (TrackCatalog)
    // The track's audio starts this much LATER than the recording's zero point (ms). 0 = started together.
    // The file is the span actually recorded (NO silence padding): the recorder writes here the
    // open time of a device track enabled mid-recording, or of each span of a device toggled
    // on and off (new file: track_<slug>-2.ogg ...). The mixdown shifts by that much silence
    // (adelay); readers of the track file shift meeting time by it (tracktiming). For an old /
    // imported folder the CLI `align` command sets it (--auto: assumes a common end).
    qint64 startOffsetMs = 0;
    // Share of speech-level frames in the track's covered time (0..1); -1 = not measured yet.
    // Filled by the speech check after recording / import (edit/TrackSpeech.h).
    double speechRatio = -1.0;
    // Why it was left out of the mixdown: "" = included, "noSpeech" = no speech on it
    // (automatic), "manual" = the user removed it. A left-out track also has active == false.
    QString excludedReason;
    // Whether it goes into the mixdown (and the analyses).
    bool included() const { return active && excludedReason.isEmpty(); }
};

// ---- transcript ------------------------------------------------------------
struct TranscriptToken {
    QString text;
    QString speaker;        // Soniox "speaker" (usually a fixed label per track)
    qint64 startMs = 0;
    qint64 endMs = 0;
    double confidence = 0.0;
    QString trackId;        // which track it came from (filled during merge)
};

struct TrackTranscript {
    QString trackId;
    QString speakerLabel;
    QString language;
    QVector<TranscriptToken> tokens;
    // Token times mark the moment the token was EMITTED (Soniox), not a real word span:
    // utterances need a later pre/post-roll pass (MergedTranscript::segments). Only SonioxProvider sets this.
    bool emissionTiming = false;
};

// A contiguous speech block (one speaker, up to a pause). The machine-friendly unit
// for player sync and the clickable transcript.
struct Utterance {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString speaker;
    QString text;
};

struct MergedTranscript {
    QString language;
    QVector<TranscriptToken> tokens;     // sorted by startMs, tracks merged
    // Emission-time timing (see TrackTranscript): after grouping, segments() pads the finished
    // lines (sonioxtiming::padUtterances). tokens.json: "timing": "emission".
    bool emissionTiming = false;
    QVector<Utterance> segments() const; // per speaker, pause-based blocks in time order
    QString renderMarkdown() const;      // impl: the TranscriptMerger module
};

// ---- summary ----------------------------------------------------------------
struct ActionItem {
    QString text;
    QString owner;
    QString due;
};

// One section of the memo: a subject discussed in the meeting, in time order. The points are
// the specifics (names, numbers, dates; who said it, if it matters). Time is -1 if unknown.
struct MemoSection {
    QString     title;
    qint64      startMs = -1;
    qint64      endMs = -1;
    QStringList points;
    QStringList speakers;   // speakers of the utterances within the section's time span (in order of first utterance)
    // The section's intro: 1-2 sentences on the gist of the subject (no markers; empty = none).
    QString     intro;
    // Subsection of each point, parallel to points (a shorter list = the rest belong to the body):
    // "" = section body, "arguments" = "Arguments made", "open" = "Open question",
    // other text: the model's own subheading. Subsections follow each other in point order.
    QStringList pointGroups;
    QString groupOf(int i) const { return i >= 0 && i < pointGroups.size() ? pointGroups.at(i) : QString(); }
};

// Source of a summary statement in the transcript: time range + ids of the utterances
// (the "u<startMs>" ids of transcript.segments.json).
struct SourceSpan {
    qint64      startMs = -1;
    qint64      endMs = -1;
    QStringList utteranceIds;
};

// Statement: one sentence of the executive summary; Decision / Todo / OpenQuestion: one item of
// decisions / actionItems / openQuestions; MemoIntro: one sentence of a memo section intro;
// MemoPoint: one memo point (in any subsection).
enum class StatementKind { Statement, Decision, Todo, OpenQuestion, MemoIntro, MemoPoint };

// One statement of the summary with source references. Order and id follow the Summary fields:
// "s<k>" is sentence k of execSummary, "d<k>" decisions[k-1], "q<k>" openQuestions[k-1],
// "t<k>" actionItems[k-1], "i<n>.<k>" sentence k of memo[n-1].intro, "m<n>.<k>"
// memo[n-1].points[k-1]. Empty sourceSpans = "no source".
struct SummaryStatement {
    QString       id;               // "s1", "d1", "q1", "t1", "i3.1", "m3.2" ... (stable within the summary)
    QString       text;             // without markers
    StatementKind kind = StatementKind::Statement;
    QVector<SourceSpan> sourceSpans;
    QString       owner;            // for an action item: the owner
    bool          flagged = false;  // "Not said like this?" - flagged by the user
    // Runtime only (not stored): the speaker of the source lines changed since the summary
    // ("X -> Y?"); ownerStaleBecause: the action item's owner is affected too ("X -> Y?").
    QStringList   staleBecause;
    QString       ownerStaleBecause;
};

// The speaker of a source utterance at summary time (for targeted staleness).
struct SourceSpeaker {
    QString key;     // the resolved speaker key (raw label or "participant:N")
    QString name;    // the displayed name, as the LLM saw it
};

// Result of a summary run: the SHORT form (executive summary + lists) and the LONG form
// (memo) are built from the same per-part notes, so they stay consistent.
struct Summary {
    QString execSummary;
    QStringList decisions;
    QStringList openQuestions;           // undecided, still-open matters (at most ~5)
    QVector<ActionItem> actionItems;
    QStringList participants;
    QVector<MemoSection> memo;           // detailed notes of the meeting, in time order
    // Source references (for the quick summary; empty for an old / per-topic summary).
    QVector<SummaryStatement> statements;
    QMap<QString, SourceSpeaker> sourceSpeakers;   // utteranceId -> speaker at the time
    QString renderMarkdown() const;      // impl: the SummaryService module
};

// ---- complex (multi-pass, topic-based) summary ------------------------------
// Pass 1: the TOPICS extracted from the full transcript, an intermediate state the
// user can edit (title + 1-line gist). Pass 2 runs on these (approved) topics.
struct SummaryTopic {
    QString id;        // stable id (QUuid); survives editing / passes
    QString title;
    QString summary;   // 1-2 sentence gist (from pass 1; editable)
};

// Result of pass 2 for one topic: detailed summary + decisions + action items.
struct TopicAnalysis {
    QString topicId;
    QString title;
    QString detail;
    QStringList decisions;
    QStringList openQuestions;           // questions left open in the topic (optional)
    QVector<ActionItem> actionItems;
    // Markdown of the body WITHOUT a title (detail + decisions + action items); the topic card's
    // result view and the topic sections of the complex summary.md are built from it.
    QString renderMarkdown() const;      // impl: the SummaryService module
};

// ---- participants ("Who was there?") ------------------------------------------
// Where a participant came from: added by hand, voice analysis, tag suggestion, calendar (later).
enum class ParticipantSource { Manual, Voice, Tag, Calendar };
// Groups of the approval dialog: Certain (voice + another source), Doubtful (voice only, or
// conflicting evidence), Invited but not heard (calendar, no voice).
enum class ParticipantGroup { Sure, Doubt, InvitedNotHeard };

struct Participant {
    QString id;                     // stable id within the meeting
    QString personName;             // empty = anonymous (unknown voice)
    QStringList rawSpeakerIds;      // bound raw STT speakers ("Speaker 1")
    ParticipantSource source = ParticipantSource::Voice;
    bool approved = false;          // approved by the user ("Who was there?")
    QStringList sides;              // "mic" | "loopback": which side it was heard on
    // Manual speaker -> track binding (track ids, "Which track do they speak on?"); empty = the
    // inferred side (sides) applies. After transcription SpeakerOverlay::speakerTracks is its
    // counterpart (the core keeps them in sync).
    QStringList tracks;
    QVector<Evidence> evidence;
    double talkShare = 0.0;         // 0..1, share of speaking time
};

// A participant's / speaker's track binding for the UI: the manual tracks (manual), otherwise
// the tracks of the inferred side (leading ids of the mic / call tracks included in the mixdown).
struct ParticipantTrackBinding {
    QStringList trackIds;
    bool manual = false;
};

// State of a candidate at the moment of the decision.
struct ParticipantApprovalEntry {
    QString participantId;
    QString personName;
    ParticipantGroup group = ParticipantGroup::Doubt;
    bool checked = false;
    QStringList rawSpeakerIds;
};

struct ParticipantApproval {
    QString at;                     // ISO-8601
    QStringList modelIds;           // voice models of the analysis the decision was based on
    QVector<ParticipantApprovalEntry> candidates;
    bool skipped = false;           // "Skip": no binding, the markers stay
    bool solo = false;              // "Only I spoke": every raw speaker maps to the user's own name
};

// ---- meeting --------------------------------------------------------------
struct Meeting {
    QString id;
    QString title;
    QString folder;          // absolute folder path
    QDateTime startedAt;
    qint64 durationMs = 0;
    QVector<Track> tracks;
    QString mixdownFile;     // pl. "mixdown.mp3"
    bool mixdownDirty = false;   // true = the tracks changed since the mixdown (remix needed)
    bool hasTranscript = false;
    bool hasSummary = false;
    QMap<QString, QString> speakerMap;   // raw speaker label ("Remote 1") -> real name ("Bela")
    // A few words from the user describing the meeting (topic/context). It goes into the STT
    // context envelope (more accurate transcript for doubtful parts) AND into the LLM summary
    // context. Editable in the pre-transcript view and on the Summary tab; names, jargon and
    // known mishearings belong here ("X" is correctly: ...).
    QString contextNote;
    // The call app detected by the watcher (e.g. "Microsoft Teams"); empty for a manual recording.
    // Informational only: not sent to the transcriber or the summarizer.
    QString detectedCallApp;
    // Ids of the meeting's tags, in the order they were applied (the tag set lives in
    // <metadataDir>/tags.json, see tags/TagService.h). "tags" in meeting.json.
    QStringList tagIds;
    // Participants (manual + voice-analysis candidates) and the approval. "participants" /
    // "participantApproval" in meeting.json.
    QVector<Participant> participants;
    std::optional<ParticipantApproval> approval;
};

// ---- speaker identification (voiceprint) ------------------------------------
// A voiceprint: an L2-normalized embedding vector computed from one representative
// segment of a speaker + the source metadata. A name may have several (a different
// microphone gives a different acoustic signature).
struct Voiceprint {
    QString id;                 // unique id (QUuid)
    QVector<float> embedding;   // L2-normalized speaker embedding
    int dim = 0;                // embedding.size() (redundant, but useful in the JSON)
    QString sourceMeetingId;    // which meeting it came from
    QString sourceTrack;        // pl. "loopback" / "mic" / track-id
    QString device;             // name of the recording device (multi-microphone context)
    QString sampleRef;          // "<folder>/track_x.ogg#startMs-endMs"; times in meeting time
    QString createdAt;          // ISO-8601
    // The embedding model (VoiceModelRegistry id). JSON "model"; missing = "campplus". A sample
    // (sampleRef) gets a separate voiceprint per enabled model (same sampleRef/createdAt).
    QString model{QStringLiteral("campplus")};
    // Every contributing audio span as "<file>#startMs-endMs" (meeting time), when the voiceprint
    // was built from several spans (JSON "sourceRefs"). sampleRef is the span to play (the longest);
    // lazy backfill (with another model) recomputes from these, or from sampleRef if absent.
    QStringList sourceRefs;
};

// Result of a match: which person, with what (cosine) score.
struct VoiceMatch {
    QString name;
    double score = -1.0;        // [-1..1] cosine; <0 = no match / empty DB
};

// A speaker candidate recognized (locally) BEFORE transcription on a track: the voice cluster
// + the best DB match (or an empty name = unknown). The representative segment can be
// listened to / named (= training) before transcription.
struct ParticipantGuess {
    QString trackId;
    QString deviceName;
    QString name;          // name of the DB match, or "" if unknown (below threshold)
    double  score = -1.0;  // cosine score of the match
    int     windows = 0;   // how many voice windows the cluster consisted of (reliability)
    QString sampleRef;     // "track_x.ogg#startMs-endMs": representative sample (in meeting time)
};

// ---- provider configuration / settings ----------------------------------------
struct ProviderConfig {
    QString type;            // "soniox" | "openai-compat" | ...
    QString baseUrl;
    QString apiKey;          // runtime only; goes to the keychain when stored
    QString model;
    double temperature = 0.2; // LLM sampling temperature (summary); not used by STT
    int maxTokens = 8000;     // max tokens of the LLM reply (plenty for a reasoning model); not used by STT
    // The model's "thinking" (reasoning), LLM only. Measured: it must be OFF for the summary
    // (otherwise the output budget is spent on thinking, and it is 2-5x slower):
    //  "auto" - off; the method depends on the model name (Gemma: reasoning_effort "none";
    //           Qwen 3+: assistant message prefilled with an empty <think> block; unknown:
    //           reasoning_effort "none", which servers that do not know it ignore),
    //  "off"  - same as auto (explicit off),
    //  "on"   - no switch is sent: the model's default behavior (a thinking model thinks).
    QString reasoning{QStringLiteral("auto")};
    // The model's context (in tokens), LLM only. 0 = automatic: at task start Tanara requests
    // the smallest standard step (8k / 16k / 20k / 32k ...) that covers the task (up to the
    // model's maximum). Fixed value: always loaded with that size. Only effective with
    // LM Studio (loaded via its native API); for other servers set the load there.
    int contextLength = 0;
    QVariantMap extra;

    // --- runtime (NOT persisted) gateway hooks, filled only by the Tanara Cloud path ---
    // Headers added to every request (X-Tanara-Client, Accept-Language, X-Tanara-Job-Id ...).
    QList<QPair<QByteArray, QByteArray>> extraHeaders;
    // Feedback of every HTTP response (success, error, network error) to the caller; the cloud
    // layer reads the load, the write-back, the request id and the structured error from it.
    std::function<void(const HttpExchange&)> onExchange;
};

struct AppSettings {
    QString audioDir;            // where recordings are saved
    QString notesDir;            // where the summary .md copy goes (vault Meetings/)
    QString metadataDir;         // pl. ~/.tanara
    QString userSpeakerName;     // fixed name of the mic track (e.g. "Adam")
    bool autoRecordAllDevices = true;  // true = record every device (silent ones are dropped afterwards)
    QStringList languageHints{QStringLiteral("hu")};

    // UI language: "auto" (system locale decides) | "hu" | "en". The source language is Hungarian;
    // a restart is needed after switching (no runtime retranslate). See Localization.h.
    QString uiLanguage{QStringLiteral("auto")};

    // Recording audio quality (per-track Opus bitrate). The raw per-track .ogg files are also
    // used by the STT, so even the lowest step is "STT-safe" (24 kbps). Lower step =
    // smaller files. Values: "best"|"high"|"medium"|"low".
    QString audioQuality{QStringLiteral("best")};

    // Timing of the mixdown after recording stops. "auto" = in the background,
    // non-blocking, immediately; "manual" = only on manual start (the review panel's button).
    // The mixdown is needed ONLY for listening (the STT works from the per-track .ogg files),
    // so stop() never waits for it and the main thread does not freeze.
    QString mixdownMode{QStringLiteral("auto")};

    // System prompts of the summary LLM. EMPTY = the file override / built-in default
    // (PromptLibrary) applies (so future improvements of the code default take effect
    // automatically until the user writes their own).
    //  summaryPrompt - single-step prompt for a meeting with ONE part (short) ("single":
    //                  memo notes + executive summary and lists in one call),
    //  notesPrompt   - per-part notes for a longer meeting ("notes"),
    //  mergePrompt   - combining the part notes ("merge").
    QString summaryPrompt;
    QString notesPrompt;
    QString mergePrompt;

    // The two editable prompts of the complex (multi-pass) summary. EMPTY = the code default
    // (ComplexSummaryService::defaultTopicPrompt() / defaultAnalysisPrompt()).
    QString topicExtractionPrompt;   // pass 1: topic extraction
    QString topicAnalysisPrompt;     // pass 2: per-topic analysis

    // The summary's TARGET LANGUAGE (free text, e.g. Hungarian, English or German); it goes into
    // the prompts' {{NYELV}} placeholder (PromptLibrary::applySummaryLanguage).
    // Independent of the UI language. The structural section headings (the Hungarian decisions / action-item headings)
    // stay Hungarian regardless of the language (the parser matches on them).
    QString summaryLanguage{QStringLiteral("magyar")};

    // Multi-provider: the selected provider id per type + per-provider config (so switching
    // does not delete the other provider's settings). The old single `stt`/`llm` shape is
    // migrated from the JSON (see JsonSerialization).
    // Tanara Cloud (the gateway contract: docs/cloud-gateway-api.yaml).
    //  cloudEnabled - the sign-in cloud mode is live ("after launch"); false = the Settings
    //                 Tanara Cloud section shows the "Coming soon" (waitlist) panel.
    //                 The TANARA_CLOUD_URL environment variable overrides it for development (turns it on).
    //  cloudBaseUrl - gateway base URL (without /v1); empty = the production default.
    //  cloudSttTier / cloudLlmTier - Fast / Accurate tier per task (fast | accurate).
    //  cloudSttModel / cloudLlmModel - Expert mode: a concrete catalog id; empty = the tier's virtual model.
    //  waitlistEmail - address subscribed from the "Coming soon" panel (non-empty = do not ask again).
    bool    cloudEnabled = false;
    QString cloudBaseUrl;
    QString cloudSttTier{QStringLiteral("accurate")};
    QString cloudLlmTier{QStringLiteral("accurate")};
    QString cloudSttModel;
    QString cloudLlmModel;
    QString waitlistEmail;
    //  cloudEstimateBeforeRun - always show a cost estimate + confirmation before Tanara Cloud
    //                 processing (on by default). When off, the run starts without an estimate; the
    //                 error branches (out of balance, terms of service) still stop it.
    bool    cloudEstimateBeforeRun = true;

    // User-facing names of audio devices: raw OS device name -> friendly name. Shown everywhere
    // the device or its track appears by name (recorder, Tracks tab, notification).
    // Resolution happens in ONE place: tanara::devicenames (audio/TrackCatalog.h); the
    // SettingsManager loads it on every load / save.
    QMap<QString, QString> deviceNames;

    // The "Getting started" window (OnboardingWindow.qml) has already run: it was closed, skipped
    // or completed. Only turns off the automatic opening (once on first launch); it can be
    // reopened any time from File > "Getting started..." and from Settings.
    bool onboardingDone = false;

    QString sttProviderId{QStringLiteral("soniox")};
    QString llmProviderId{QStringLiteral("openai-compat")};
    QMap<QString, ProviderConfig> sttConfigs;   // id -> config
    QMap<QString, ProviderConfig> llmConfigs;   // id -> config

    // Back-compat accessors: the runtime config of the selected provider.
    // Most call sites can switch to these (s.stt -> s.sttSelected()).
    ProviderConfig sttSelected() const { return sttConfigs.value(sttProviderId); }
    ProviderConfig llmSelected() const { return llmConfigs.value(llmProviderId); }

    // Embedding model for tag suggestions. Empty id = none ("basic" level: suggestions come
    // from shared participants, phrases and the title). See embedding/EmbeddingProviderRegistry.h:
    // "openai-compat-embedding" | "tanara-hosted-embedding".
    QString embeddingProviderId;
    QMap<QString, ProviderConfig> embeddingConfigs;   // id -> config
    ProviderConfig embeddingSelected() const { return embeddingConfigs.value(embeddingProviderId); }
    // Tag suggestions on/off, and whether the language model suggests after the summary.
    bool tagSuggestions = true;
    bool llmTagSuggestions = true;

    // To review (focus view): the line stepped onto on the card plays by itself (line + 0.3 s).
    // Written by the view header's toggle; on by default.
    bool reviewAutoPlay = true;

    // Enabled speaker-embedding models (VoiceModelRegistry ids), alphabetical. Every one counts
    // for voiceprints and analysis (fusion); a missing model file is skipped at run time, the
    // setting stays. The "voiceModels" key of settings.json; missing = ["wespeaker-resnet34-lm"].
    QStringList voiceModels{QStringLiteral("wespeaker-resnet34-lm")};

    // Meeting watcher (background detector + tray). The watcher reads it; the analyzer/recorder
    // does not depend on it. Empty detectorId = the registry picks the first available detector.
    bool detectorEnabled = true;              // active-call detection on/off
    int  detectorIntervalSec = 8;             // poll interval (s); sensible range 5-30
    bool watcherAutostart = false;            // whether the watcher starts at login
    // During recording the recorder also watches the call: when it ends (the app quits / releases
    // the microphone), it asks whether to stop. It never stops by itself.
    bool askStopOnCallEnd = true;
    // Silence watch during recording: if ALL tracks stay silent this many minutes, it asks
    // whether to stop (with Teams/browser, mic release is not always visible to the detector;
    // this net is INDEPENDENT of the detector). 0 = off.
    int  silenceAskMinutes = 3;
    QString detectorId;                       // empty = MeetingDetectorRegistry::createBest()
    // Known call apps (binary/app-name fragments); the detector signals on these.
    QStringList knownCallApps{
        QStringLiteral("zoom"), QStringLiteral("teams"), QStringLiteral("webex"),
        QStringLiteral("slack"), QStringLiteral("discord"), QStringLiteral("meet"),
        QStringLiteral("skype"), QStringLiteral("chromium"), QStringLiteral("firefox")};
};

// Recording quality step -> per-track Opus bitrate (kbps). Unknown = 64 ("best").
// The lowest step (24 kbps) is the "STT floor": we do not go below it, so the accuracy of
// transcription working from the per-track .ogg does not degrade.
inline int opusBitrateKbps(const QString& quality) {
    if (quality == QStringLiteral("low"))    return 24;
    if (quality == QStringLiteral("medium")) return 32;
    if (quality == QStringLiteral("high"))   return 48;
    return 64;   // "best" / unknown
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
Q_DECLARE_METATYPE(tanara::SourceSpan)
Q_DECLARE_METATYPE(tanara::SummaryStatement)
Q_DECLARE_METATYPE(QVector<tanara::SummaryStatement>)
Q_DECLARE_METATYPE(tanara::Summary)
Q_DECLARE_METATYPE(tanara::SummaryTopic)
Q_DECLARE_METATYPE(QVector<tanara::SummaryTopic>)
Q_DECLARE_METATYPE(tanara::TopicAnalysis)
Q_DECLARE_METATYPE(tanara::Meeting)
Q_DECLARE_METATYPE(tanara::Participant)
Q_DECLARE_METATYPE(QVector<tanara::Participant>)
Q_DECLARE_METATYPE(tanara::Voiceprint)
Q_DECLARE_METATYPE(tanara::VoiceMatch)
Q_DECLARE_METATYPE(tanara::ProviderConfig)
Q_DECLARE_METATYPE(tanara::AppSettings)

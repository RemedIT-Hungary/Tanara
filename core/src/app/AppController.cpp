#include "tanara/app/AppController.h"

#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/text/Text.h"
#include "tanara/foundation/paths/Paths.h"
#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/watcher/detect/DetectorRegistry.h"
#include "tanara/watcher/detect/IMeetingDetector.h"
#include <QTimer>
#include "tanara/app/SettingsManager.h"
#include "tanara/recording/audio/DeviceManager.h"
#include "tanara/recording/audio/DeviceMonitor.h"
#include "tanara/recording/audio/RecordingSession.h"
#include "tanara/library/store/MeetingStore.h"
#include "tanara/library/store/MeetingArchive.h"
#include "tanara/foundation/KeyStore.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/people/PeopleService.h"
#include "tanara/library/people/PeopleStats.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/processing/voiceid/VoiceEmbedder.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"
#include "tanara/providers/stt/ISttProvider.h"
#include "tanara/providers/chat/ILlmProvider.h"
#include "tanara/providers/chat/LlmServer.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/processing/summary/SummaryService.h"
#include "tanara/processing/summary/ComplexSummaryService.h"
#include "tanara/processing/summary/TranscriptMerger.h"
#include "tanara/foundation/i18n/Localization.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/jobs/JobErrors.h"
#include "tanara/library/jobs/JobStats.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/MeetingNotes.h"
#include "tanara/recording/audio/MixdownPlan.h"
#include "tanara/recording/audio/TrackTiming.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/library/tracks/WaveformService.h"
#include "tanara/editing/session/PeopleDirectory.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/editing/session/TrackSpeech.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/library/tags/TagNames.h"
#include "tanara/library/tags/MeetingProfiles.h"
#include "tanara/library/tags/TagSuggester.h"
#include "tanara/processing/analysis/LlmTagSuggester.h"
#include "tanara/library/index/EmbeddingIndex.h"
#include "tanara/library/index/EmbeddingPreparer.h"
#include "tanara/providers/embedding/EmbeddingProviderRegistry.h"
#include "tanara/providers/embedding/IEmbeddingProvider.h"
#include "tanara/editing/undo/MeetingUndo.h"
#include "tanara/library/store/JsonSerialization.h"
#include "tanara/processing/voiceid/VoiceModelDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QHash>
#include <QSet>
#include <QFileInfo>
#include <QDateTime>
#include <QPointer>
#include <QRegularExpression>
#include <QUuid>
#include <QElapsedTimer>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace tanara::app {

namespace {

QString expandTilde(QString p) {
    if (p == QStringLiteral("~")) return QDir::homePath();
    if (p.startsWith(QStringLiteral("~/"))) return QDir::homePath() + p.mid(1);
    return p;
}

// Always LF line endings (WITHOUT QIODevice::Text): on Windows the Text mode would write CRLF, but
// the readback (SummaryStore, transcript) reads raw, so a \r would end up in the text.
bool writeTextFile(const QString& path, const QString& text) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(text.toUtf8());
    return f.commit();
}

void writeTokensJson(const QString& path, const MergedTranscript& mt) {
    QJsonArray arr;
    for (const auto& t : mt.tokens) {
        QJsonObject o;
        o["text"] = t.text;
        o["speaker"] = t.speaker;
        o["startMs"] = double(t.startMs);
        o["endMs"] = double(t.endMs);
        o["confidence"] = t.confidence;
        o["trackId"] = t.trackId;
        arr.append(o);
    }
    QJsonObject root;
    root["language"] = mt.language;
    if (mt.emissionTiming) root["timing"] = QStringLiteral("emission");   // the lines need padding (Soniox)
    root["tokens"] = arr;
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void writeSegmentsJson(const QString& path, const QVector<Utterance>& segs) {
    QJsonArray arr;
    for (const auto& u : segs) {
        QJsonObject o;
        o["startMs"] = double(u.startMs);
        o["endMs"]   = double(u.endMs);
        o["speaker"] = u.speaker;
        o["text"]    = u.text;
        arr.append(o);
    }
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented)); f.commit(); }
}

// Putting the names onto the transcript: speakeredit::applyResolvedSpeakers(mt, meeting) applies the
// manual line corrections (transcript.speakers.json) in addition to the speakerMap.

MergedTranscript readTokensJson(const QString& path) {
    MergedTranscript mt;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return mt;
    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    mt.language = root["language"].toString();
    mt.emissionTiming = root["timing"].toString() == QLatin1String("emission");
    for (const auto& v : root["tokens"].toArray()) {
        const auto o = v.toObject();
        TranscriptToken t;
        t.text = o["text"].toString();
        t.speaker = o["speaker"].toString();
        t.startMs = qint64(o["startMs"].toDouble());
        t.endMs = qint64(o["endMs"].toDouble());
        t.confidence = o["confidence"].toDouble();
        t.trackId = o["trackId"].toString();
        mt.tokens.append(t);
    }
    return mt;
}

// The EDITABLE topic list of the complex summary's pass 1 (summary.topics.json).
void writeTopicsJson(const QString& path, const QVector<SummaryTopic>& topics) {
    QJsonArray arr;
    for (const auto& t : topics) {
        QJsonObject o;
        o["id"]      = t.id;
        o["title"]   = t.title;
        o["summary"] = t.summary;
        arr.append(o);
    }
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

QVector<SummaryTopic> readTopicsJson(const QString& path) {
    QVector<SummaryTopic> topics;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return topics;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
        const auto o = v.toObject();
        SummaryTopic t;
        t.id      = o["id"].toString();
        t.title   = o["title"].toString();
        t.summary = o["summary"].toString();
        if (!t.title.trimmed().isEmpty())
            topics.append(t);
    }
    return topics;
}

// The FINISHED analyses of the complex summary's pass 2 (summary.analyses.json), persisted per topic
// IMMEDIATELY, so work already paid for is not lost after a cancel/error.
void writeAnalysesJson(const QString& path, const QVector<TopicAnalysis>& analyses) {
    QJsonArray arr;
    for (const auto& a : analyses) {
        QJsonObject o;
        o["topicId"] = a.topicId;
        o["title"]   = a.title;
        o["detail"]  = a.detail;
        o["decisions"] = QJsonArray::fromStringList(a.decisions);
        QJsonArray items;
        for (const ActionItem& ai : a.actionItems) {
            QJsonObject io;
            io["text"] = ai.text; io["owner"] = ai.owner; io["due"] = ai.due;
            items.append(io);
        }
        o["actionItems"] = items;
        arr.append(o);
    }
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

QVector<TopicAnalysis> readAnalysesJson(const QString& path) {
    QVector<TopicAnalysis> analyses;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return analyses;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
        const auto o = v.toObject();
        TopicAnalysis a;
        a.topicId = o["topicId"].toString();
        a.title   = o["title"].toString();
        a.detail  = o["detail"].toString();
        for (const auto& dv : o["decisions"].toArray()) a.decisions << dv.toString();
        for (const auto& iv : o["actionItems"].toArray()) {
            const auto io = iv.toObject();
            ActionItem ai;
            ai.text = io["text"].toString(); ai.owner = io["owner"].toString();
            ai.due = io["due"].toString();
            a.actionItems.append(ai);
        }
        if (!a.topicId.isEmpty())
            analyses.append(a);
    }
    return analyses;
}

// Writing/overwriting a finished analysis into summary.analyses.json (by topicId).
void upsertAnalysisJson(const QString& path, const TopicAnalysis& a) {
    QVector<TopicAnalysis> all = readAnalysesJson(path);
    bool replaced = false;
    for (TopicAnalysis& existing : all)
        if (existing.topicId == a.topicId) { existing = a; replaced = true; break; }
    if (!replaced) all.append(a);
    writeAnalysesJson(path, all);
}

// The union of the topics' open questions (in order, without repeats, at most 5).
QStringList openQuestionsOf(const QVector<TopicAnalysis>& topics) {
    QStringList out;
    for (const TopicAnalysis& a : topics)
        for (const QString& q : a.openQuestions)
            if (!out.contains(q) && out.size() < processing::summarypipe::kMaxOpenQuestions) out << q;
    return out;
}

// The complex summary's markdown: a global head (executive summary + merged action items) +
// per-topic sections (summary + decisions + action items). It follows Summary::renderMarkdown's style
// (`- [ ]` action-item checklists).
QString renderComplexMarkdown(const QString& execSummary, const QVector<ActionItem>& items,
                              const QVector<TopicAnalysis>& topics) {
    auto renderItem = [](const ActionItem& ai) {
        QString s = QStringLiteral("- [ ] ") + ai.text;
        if (!ai.owner.isEmpty()) s += QStringLiteral(" — ") + ai.owner;
        if (!ai.due.isEmpty())   s += QStringLiteral(" (") + ai.due + QStringLiteral(")");
        return s + QStringLiteral("\n");
    };
    QString md;
    if (!execSummary.isEmpty())
        md += QStringLiteral("## Vezetői összefoglaló\n\n") + execSummary + QStringLiteral("\n\n");
    const QStringList open = openQuestionsOf(topics);
    if (!open.isEmpty()) {
        md += QStringLiteral("## Nyitott kérdések\n\n");
        for (const QString& q : open) md += QStringLiteral("- ") + q + QStringLiteral("\n");
        md += QStringLiteral("\n");
    }
    if (!items.isEmpty()) {
        md += QStringLiteral("## Teendők (összevont)\n\n");
        for (const ActionItem& ai : items) md += renderItem(ai);
        md += QStringLiteral("\n");
    }
    if (!topics.isEmpty()) {
        md += QStringLiteral("## Témák\n\n");
        int n = 1;
        for (const TopicAnalysis& t : topics) {
            md += QStringLiteral("### %1. %2\n\n").arg(n++).arg(t.title);
            md += t.renderMarkdown();   // detail + decisions + action items (Types/SummaryService)
        }
    }
    return md;
}

// Which track to take audio from for a raw speaker label:
//  - an exact match with a track's fixed speaker (mic) -> that track;
//  - otherwise (a diarized "Remote N") -> the loopback track;
//  - last fallback: the first track.
const Track* resolveTrackForLabel(const Meeting& m, const MergedTranscript& mt,
                                  const QString& rawLabel) {
    // 1) Primarily by the trackId of the label's tokens (diarized mic/loopback speakers, "Microphone N" /
    //    "Remote N", thus resolve to the RIGHT track).
    QString tid;
    for (const TranscriptToken& t : mt.tokens)
        if (t.speaker == rawLabel && !t.trackId.isEmpty()) { tid = t.trackId; break; }
    if (!tid.isEmpty())
        for (const Track& t : m.tracks)
            if (t.id == tid) return &t;
    // 2) Fallback: an exact track speaker name match (the fixed mic name).
    for (const Track& t : m.tracks)
        if (t.speakerLabel == rawLabel) return &t;
    // 3) Fallback: the first loopback track.
    for (const Track& t : m.tracks)
        if (t.kind == TrackKind::Loopback) return &t;
    return m.tracks.isEmpty() ? nullptr : &m.tracks.first();
}

// A speaker's representative embedding (with every model in use): from their longest utterances it
// collects ~3-12 s of audio from the given track, and computes one vector per model from ONE concatenated
// PCM. Empty = not enough audio / an error. The utterances are in meeting time; offsetMs is the audio
// file's start in the meeting (Track::startOffsetMs for a track file, 0 for the mixdown); the part before
// the file is dropped. refs (optional): the spans used in "fileRel#start-end" form (meeting time), the
// voiceprint's sourceRefs.
EmbeddingSet embeddingForLabel(const processing::VoiceEmbedderSet& set, const QString& audioPath, qint64 offsetMs,
                               const MergedTranscript& mt, const QString& rawLabel,
                               const QString& fileRel = QString(), QStringList* refs = nullptr) {
    QVector<Utterance> utts;
    for (const Utterance& u : mt.segments())
        if (u.speaker == rawLabel) utts.append(u);
    std::sort(utts.begin(), utts.end(), [](const Utterance& a, const Utterance& b) {
        return (a.endMs - a.startMs) > (b.endMs - b.startMs);
    });
    const QString& path = audioPath;
    QVector<float> pcm;
    qint64 accMs = 0;
    for (const Utterance& u : utts) {
        if (accMs >= 3000 || pcm.size() > 16000 * 12) break;
        const tanara::tracktiming::FileRange r = tanara::tracktiming::meetingToFileRange(offsetMs, u.startMs, u.endMs);
        if (!r.valid()) continue;   // the track was not sounding yet at this time
        const QVector<float> part = processing::VoiceEmbedder::decodePcm16kMono(path, r.startMs, r.endMs);
        if (part.isEmpty()) continue;
        pcm += part;
        accMs += (r.endMs - r.startMs);
        if (refs)
            *refs << QStringLiteral("%1#%2-%3").arg(fileRel).arg(r.startMs + offsetMs).arg(r.endMs + offsetMs);
    }
    if (pcm.isEmpty()) return {};
    return set.embedPcm(pcm);
}

// Parsing a sample reference ("file#startMs-endMs", meeting time); false if there is no range.
bool parseRangeRef(const QString& ref, QString* file, qint64* startMs, qint64* endMs) {
    const int hash = ref.lastIndexOf(QLatin1Char('#'));
    if (hash <= 0) return false;
    *file = ref.left(hash);
    const QString range = ref.mid(hash + 1);
    const int dash = range.indexOf(QLatin1Char('-'));
    if (dash <= 0) return false;
    bool ok1 = false, ok2 = false;
    *startMs = range.left(dash).toLongLong(&ok1);
    *endMs = range.mid(dash + 1).toLongLong(&ok2);
    return ok1 && ok2 && *endMs > *startMs;
}

// One voiceprint with ONE model, recomputed from its audio (lazy backfill). It concatenates the PCM of
// the spans (sourceRefs, failing that the sampleRef) and embeds it as a whole, the same way
// enrollSpeaker's voiceprint is made (embeddingForLabel); this differs slightly from the editor's
// weighted per-line average, but is self-consistent per model. The spans are in meeting time: for a track
// file the track's offset is subtracted. missingAudio: the referenced file is missing.
QVector<float> embedPrintSource(const processing::VoiceEmbedderSet& set, const QString& modelId, const Meeting& m,
                                const Voiceprint& vp, bool* missingAudio) {
    *missingAudio = false;
    const QStringList refs = vp.sourceRefs.isEmpty() ? QStringList{vp.sampleRef} : vp.sourceRefs;
    QVector<float> pcm;
    for (const QString& ref : refs) {
        QString file;
        qint64 s = 0, e = 0;
        if (!parseRangeRef(ref, &file, &s, &e)) continue;
        const QString abs = QDir(m.folder).filePath(file);
        if (!QFileInfo::exists(abs)) { *missingAudio = true; continue; }
        qint64 offset = 0;
        for (const Track& t : m.tracks)
            if (t.file == file) { offset = t.startOffsetMs; break; }
        const tanara::tracktiming::FileRange r = tanara::tracktiming::meetingToFileRange(offset, s, e);
        if (!r.valid()) continue;
        pcm += processing::VoiceEmbedder::decodePcm16kMono(abs, r.startMs, r.endMs);
    }
    if (pcm.isEmpty()) return {};
    return set.embedPcmWith(pcm, {modelId}).value(modelId);
}

// Voiceprints from one sample: one per model (same sampleRef / source / createdAt / sourceRefs, own id
// and model). The template's embedding, id and model fields do not count.
QVector<Voiceprint> printsFromSet(const EmbeddingSet& set, const Voiceprint& tmpl) {
    QVector<Voiceprint> out;
    for (auto it = set.cbegin(); it != set.cend(); ++it) {
        if (it.value().isEmpty()) continue;
        Voiceprint vp = tmpl;
        vp.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        vp.model = it.key();
        vp.embedding = it.value();
        vp.dim = it.value().size();
        out.append(vp);
    }
    return out;
}

// The reference of the playable representative segment stored with the voiceprint:
// "track_file#startMs-endMs" (the longest utterance). The times are in MEETING time (as in the
// transcript); for a track file the player converts to file time with the track's offset (PeopleService).
// For playback the People panel resolves the folder from sourceMeetingId.
QString representativeSampleRef(const MergedTranscript& mt, const QString& rawLabel,
                                const QString& fileRel) {
    qint64 bestS = -1, bestE = -1, bestDur = -1;
    for (const Utterance& u : mt.segments())
        if (u.speaker == rawLabel && (u.endMs - u.startMs) > bestDur) {
            bestDur = u.endMs - u.startMs; bestS = u.startMs; bestE = u.endMs;
        }
    if (bestS < 0)
        return fileRel;
    return QStringLiteral("%1#%2-%3").arg(fileRel).arg(bestS).arg(bestE);
}

// The voice-ID audio source: NOW the mixdown (the transcript is made from it too, so the diarized
// "Speaker N" labels' time windows fit the mix directly). If there is no mixdown (an old, per-track
// meeting), for back-compat we fall back to the label's resolved track.
struct VoiceSource { QString absPath; QString fileRel; QString trackId; QString device;
                     qint64 offsetMs = 0; };   // the file's start in the meeting
VoiceSource resolveVoiceSource(const Meeting& m, const MergedTranscript& mt,
                               const QString& rawLabel) {
    const QString mixRel = m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3")
                                                   : m.mixdownFile;
    const QString mixAbs = QDir(m.folder).filePath(mixRel);
    if (QFile::exists(mixAbs))
        return { mixAbs, mixRel, QStringLiteral("mixdown"), QString() };
    if (const Track* t = resolveTrackForLabel(m, mt, rawLabel))
        return { QDir(m.folder).filePath(t->file), t->file, t->id, t->deviceName, t->startOffsetMs };
    return {};
}

using processing::participants::clusterEmbeddings;   // agglomerative clustering (processing/analysis/ParticipantAnalysis.h)

// Cosine threshold for auto-identification (validation: same 0.77, cross <= 0.35).
constexpr double kVoiceMatchThreshold = 0.5;

QStringList loadLastDevices(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    QStringList out;
    for (const auto& v : root["lastDevices"].toArray()) out << v.toString();
    return out;
}

void saveLastDevices(const QString& path, const QStringList& names) {
    QJsonArray arr;
    for (const auto& n : names) arr.append(n);
    QJsonObject root;
    root["lastDevices"] = arr;
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(root).toJson(QJsonDocument::Compact)); f.commit(); }
}

QString sttPhase(SttJobState s) {
    switch (s) {
    case SttJobState::Uploading:  return QCoreApplication::translate("AppController", "Hang feltöltése…");
    case SttJobState::Queued:     return QCoreApplication::translate("AppController", "Várakozás a Soniox sorban…");
    case SttJobState::Processing: return QCoreApplication::translate("AppController", "Átírás folyamatban…");
    case SttJobState::Completed:  return QCoreApplication::translate("AppController", "Sáv kész");
    case SttJobState::Failed:     return QCoreApplication::translate("AppController", "Hiba");
    default:                   return QStringLiteral("…");
    }
}

QString slugify(const QString& s) {
    return text::slug(s, QStringLiteral("meeting"));
}

// The last FAILED HTTP exchange of a provider call (for the remembered error's technical line).
struct FailureSink { bool has = false; cloud::HttpExchange ex; };
using FailureSinkPtr = std::shared_ptr<FailureSink>;

// Extends the config's response hook: besides the existing (cloud) hook, non-2xx responses and network
// errors are also stored in the sink. The failure of cleanup DELETEs does not count.
void captureFailures(ProviderConfig& cfg, const FailureSinkPtr& sink) {
    const auto prev = cfg.onExchange;
    cfg.onExchange = [prev, sink](const cloud::HttpExchange& ex) {
        if (prev) prev(ex);
        const bool ok = ex.status >= 200 && ex.status < 300;
        if (!ok && ex.method != "DELETE") { sink->has = true; sink->ex = ex; }
    };
}

} // namespace

struct AppController::Impl {
    SettingsManager* settings = nullptr;
    recording::DeviceManager*   devices  = nullptr;
    library::MeetingStore*    store    = nullptr;
    recording::RecordingSession* session = nullptr;
    recording::RecordingSession::EngineFactory recordingEngineFactory;   // test seam; empty = the real one
    recording::DeviceMonitor*   monitor = nullptr;
    KeyStore         keyStore;
    std::unique_ptr<library::PeopleStore> people;
    library::PeopleStats*   peopleStats = nullptr;
    library::PeopleService* peopleService = nullptr;
    std::unique_ptr<library::VoiceprintStore> voiceprints;
    QString          voiceModelPath;    // expected location of the base model (diagnostics)
    QString          appDir;            // location of the packaged models (<appDir>/models)
    // The speaker models in use (enabled intersect available): the main thread's set, loads lazily.
    processing::VoiceEmbedderSet voiceSet;
    QStringList      voiceModelIds;     // ids of voiceSet (alphabetical)
    processing::PcmEmbedderLoader voiceLoader;      // test seam; empty = the real VoiceEmbedder
    // Lazy voiceprint backfill (backfillVoiceprints): a 1-thread pool, one run at a time.
    QThreadPool      backfillPool;
    bool             backfillRunning = false;
    bool             backfillAgain = false;
    std::shared_ptr<std::atomic<bool>> backfillCancel = std::make_shared<std::atomic<bool>>(false);
    QSet<QString>    backfillLogged;    // voiceprints already logged because of missing audio
    // Per-track speech share and pre-transcript voice analysis (background thread; the GUI thread does not wait).
    QThreadPool      analysisPool;
    QSet<QString>    speechCheckRunning;  // meetingIds being measured now
    QSet<QString>    speechChecked;       // already measured in this process (not repeated even on error)
    QHash<QString, MeetingActivity> activityView;   // activity for the speech strip (every track), with a fingerprint
    QSet<QString>    activityViewRunning; // meetingIds being computed now
    QHash<QString, std::shared_ptr<std::atomic<bool>>> analysisRuns;   // meetingId -> cancel flag
    std::shared_ptr<std::atomic<bool>> analysisShutdown = std::make_shared<std::atomic<bool>>(false);
    std::function<QStringList(const QString&)> personTags;   // test seam (a person's tags)
    processing::participants::CandidateContext candidateContext() const;
    bool             voiceModelsApplied = false;
    // The editor's model list: the models in use; if none is usable (no model file), the enabled ones:
    // with vectors computed earlier in the meeting's cache the analysis works anyway.
    QStringList editorModelIds(const SettingsManager* st) const {
        return voiceModelIds.isEmpty() ? st->enabledVoiceModels() : voiceModelIds;
    }
    int              backfillAdded = 0;
    // ---- shared undo + backfill report ----
    QHash<QString, QPointer<editing::MeetingUndoService>> undoServices;   // meetingId -> shared stack
    QHash<QString, int> undoScopeDepth;                           // nesting of ParticipantUndoScope
    QVector<processing::VoiceprintBackfillSkip> backfillSkipped;              // samples skipped by the latest backfill
    int              backfillDone = 0;
    int              backfillTotal = 0;
    void noteBackfillSkip(const QString& person, const QString& model, const Voiceprint& vp,
                          processing::VoiceprintBackfillSkip::Reason reason);
    // A voiceprint to backfill: the sibling group's representative + the missing model + the source meeting.
    struct BackfillJob { QString person; QString model; Voiceprint print; Meeting meeting; };
    void finishBackfillJob(const BackfillJob& job, const QVector<float>& vec, bool missingAudio);
    void finishBackfill();
    RecordingState   state = RecordingState::Idle;
    QString          audioDir;
    QString          metaDir;
    QString          notesDir;
    QString          statePath;
    QStringList      lastDevices;
    QString          currentFolder;

    // Level monitoring + track handling during recording (for the floating recorder).
    bool        monitorWanted = false;          // the UI requested level monitoring
    bool        monitorLegacy = false;          // the start/stopLevelMonitoring (recorder) request
    QHash<QObject*, QMetaObject::Connection> monitorHolders;   // consumers of retainLevelMonitoring (Settings)
    QStringList monitorNames;                   // the monitor's current device set
    QStringList recNames;                       // the recording's tracks (index -> device name)
    QStringList recClosed;                      // devices detached mid-run
    QStringList recOff;                         // switched off by the user mid-run
    QHash<QString, int> recMissing;             // in how many consecutive enumerations it was missing
    QHash<QString, int> recReturning;           // detached device: in how many consecutive enumerations it is present again
    bool        autoMixdown = true;             // automatic mixdown after recording
    QHash<QString, MergedTranscript> mergedCache;

    // Call-end watching during recording (see startCallEndMonitor).
    std::unique_ptr<watcher::IMeetingDetector> callDetector;
    QTimer* callTimer = nullptr;
    bool    callSeenActive = false;   // whether we saw an active call during the recording
    int     callInactivePolls = 0;    // consecutive inactive polls after the active one
    QString callAppName;

    // Silence watch during recording (silenceAskMinutes).
    QTimer* silenceTimer = nullptr;
    qint64  lastLoudMs = 0;        // timestamp of the last "loud" RMS (monotonic)
    bool    silenceAsked = false;  // we already asked for this silent stretch

    // Topic-analysis job queue (complex pass 2): ONE LLM call runs at a time (local model, parallel=1),
    // the other topics wait in line. Each topic can be (re)started separately.
    struct TopicJob { QString meetingId; SummaryTopic topic; };
    QVector<TopicJob> topicJobQueue;
    bool    topicJobActive = false;
    QString activeTopicMeetingId;   // addressee of the running job (dedup + lifecycle signals)
    QString activeTopicId;
    QSet<QString> reduceWhenDone;             // meetingIds where an auto-reduce follows at the end of the queue
    QHash<QString, QPair<int,int>> jobCounts; // meetingId -> (ok, fail) in the current batch

    // Transcript-editor sessions (one per meeting; see speakerEditor()).
    QHash<QString, QPointer<editing::SpeakerEditor>> speakerEditors;
    bool editorEmitting = false;   // the editor's own speakerMap signal is going out (do not reload)

    // Tanara Cloud.
    cloud::CloudAccount* cloud = nullptr;
    bool cloudLive = false;
    bool cloudTeaser = false;
    // Per meeting the cloud run of the open complex summary (topic gathering -> topic analyses -> reduce):
    // ONE X-Tanara-Job-Id, the per-call charges summed up (the "12 parts" case).
    // On completion (done / error / cancel) it is removed from here; a continuation is thus a new run.
    QHash<QString, CloudRunPtr> complexRuns;

    // ---- structured layer (redesigned main window) ----------------------------------
    AppController*     q = nullptr;
    library::MeetingJobTracker* jobs = nullptr;
    library::MeetingLibrary*    library = nullptr;
    library::TrackCatalog*      tracks = nullptr;
    library::WaveformService*   waveforms = nullptr;
    library::AudioImporter*     importer = nullptr;
    std::unique_ptr<library::JobStats> jobStats;      // speed of earlier runs (for estimates)

    // ---- tags ----
    library::TagService*        tags = nullptr;
    library::MeetingProfiles*   profiles = nullptr;
    library::EmbeddingIndex*    embeddings = nullptr;
    library::EmbeddingPreparer* embeddingPreparer = nullptr;
    processing::LlmTagSuggester*   llmTags = nullptr;
    QString            llmTagsMeetingId;         // the meeting of the running LLM suggestion
    QPointer<QObject>  llmTagsProvider;
    CloudRunPtr        llmTagsRun;
    QHash<QString, QVector<TagSuggestion>> similarCache;    // meetingId -> the similarity-based list
    QHash<QString, QVector<TagSuggestion>> pendingTags;     // meetingId -> the last list published
    QSet<QString>      tagRequests;              // requests waiting for the profiles to be ready
    // The similarity-based list is made on a background thread. At most one runs per meeting; a request
    // arriving meanwhile gets its result. tagDataGen grows on every cache clear: a result computed from an
    // older state is dropped and (if still awaited) recomputed.
    struct TagRun {
        bool running = false;
        bool publish = false;    // requestTagSuggestions is waiting for it
        bool llm = false;        // requestLlmTagSuggestions is waiting for it (for the candidate list)
    };
    QHash<QString, TagRun> tagRuns;
    quint64            tagDataGen = 0;
    QThreadPool        tagPool;                  // 1 thread; ~AppController waits for it
    QString            embeddingKey;             // provider|baseUrl|model, to detect a model change
    QStringList        recordingTagIds;          // tags given during recording

    // A running transcription: the connection of the job / the mixdown chain, for cancelling.
    struct TranscribeRun {
        QPointer<providers::SttJob> job;
        std::shared_ptr<QMetaObject::Connection> mixConn;
        bool mixPhase = false;         // still waiting for the mixdown
        bool ownsMixdown = false;      // this transcription started the mixdown
        bool cancelRequested = false;
    };
    QHash<QString, TranscribeRun> transcribeRuns;
    QSet<QString> clearSpeakersOnTranscript;   // re-transcription: the old names are cleared with the new transcript
    // Meetings where the user TURNED OFF the automatic voice identification after transcription (the
    // toggle). A process-wide (not persisted) choice.
    QSet<QString> skipIdentifyAfterTranscribe;

    QHash<QString, QPointer<QProcess>> mixdownProcs;   // meetingId -> running ffmpeg
    QSet<QString> mixdownCancelled;
    QHash<QString, QString> mixdownFailReason;   // the reason of the last mix that did not even start (missing track)
    // Per meeting the transcript's "generation": every new transcript write increments it. The background
    // identification uses it (and the tokens file's state) to see that a new transcript arrived meanwhile.
    QHash<QString, int> transcriptGen;

    // A running LLM call (summary / topic gathering / combining / the active topic analysis).
    struct LlmRun {
        QPointer<QObject> svc; QPointer<QObject> provider; CloudRunPtr cloudRun;
        QPointer<providers::LlmModelPreparer> preparer;   // the model preparation (LM Studio) while it lasts
    };
    QHash<QString, LlmRun> llmRuns;            // llmKey(meetingId, kind) -> run
    LlmRun activeTopicRun;
    QHash<QString, int> topicBatchTotal;       // meetingId -> number of topics queued into the current batch

    // ---- LLM context (see providers/chat/LlmContext.h, providers/chat/LlmServer.h) ----
    // A run's context knowledge for classifying errors: the estimated need, and what the server probe / the
    // load said.
    struct LlmContextState { int need = 0; bool lmStudio = false; int loadedContext = -1; };
    using LlmContextPtr = std::shared_ptr<LlmContextState>;
    // The minimum requested by the user ("Load with a larger context"): the next preparation uses it ONCE.
    // Key: server root|model.
    QHash<QString, int> contextFloor;
    // For a server without a native API, the context learned from the error message: the next summary
    // already adapts the parts to it (in memory only).
    QHash<QString, int> learnedContext;
    static QString serverKey(const ProviderConfig& cfg) {
        return providers::llmctx::nativeApiRoot(cfg.baseUrl) + QLatin1Char('|') + cfg.model.trimmed();
    }
    // Tanara is currently running ANOTHER LLM request too (then we do not unload the model).
    bool otherLlmBusy() const { return llmRuns.size() + (topicJobActive ? 1 : 0) > 1; }
    // Preparing the model before an LLM task. For Cloud (and if there is nothing to do) `go` runs at once;
    // for LM Studio, if needed, a load / reload as the task's "model" stage. The caller binds the returned
    // preparer to the run (for cancelling).
    providers::LlmModelPreparer* prepareLlm(const QString& meetingId, JobKind kind, const ProviderConfig& cfg,
                                 bool cloud, const LlmContextPtr& ctx, std::function<void()> go,
                                 std::function<void(const JobError&)> onFail);

    // A running (background-thread) identification.
    struct IdentifyRun { QThread* thread = nullptr; std::shared_ptr<std::atomic<bool>> cancel; bool asStage = false; };
    // The transcript's state at the start of an identification (see startIdentify): if it differs at the
    // end, the result refers to the labels of the OLD transcript, so we drop it.
    QString transcriptStamp(const QString& meetingId, const QString& folder) const {
        const FileStamp fs = FileStamp::of(QDir(folder).filePath(QStringLiteral("transcript.tokens.json")));
        return QStringLiteral("%1/%2/%3").arg(transcriptGen.value(meetingId)).arg(fs.mtimeMs).arg(fs.size);
    }
    QHash<QString, IdentifyRun> identifyRuns;
    // An archive operation (export / import): one at a time, on a background thread.
    QThread* archiveThread = nullptr;
    std::shared_ptr<std::atomic<bool>> archiveCancel;
    QString archiveOp;          // "export:<meetingId>" | "import:<uuid>"

    static QString llmKey(const QString& meetingId, JobKind kind) {
        return meetingId + QLatin1Char('|') + QString::number(int(kind));
    }
    bool transcribeInMixPhase(const QString& meetingId) const {
        const auto it = transcribeRuns.constFind(meetingId);
        return it != transcribeRuns.constEnd() && it->mixPhase;
    }
    bool voiceModelUsable() const;
    void reportMixdownPercent(const QString& meetingId, int pct);
    void updateTopicCounts(const QString& meetingId);
    void abortLlmRun(const LlmRun& run);
    JobError describeLlmFailure(JobKind kind, const QString& raw, const CloudRunPtr& run,
                                const FailureSinkPtr& sink,
                                const std::shared_ptr<LlmContextState>& ctx = nullptr) const;
    void applyIdentification(const QString& meetingId,
                             const QVector<QPair<QString, EmbeddingSet>>& embeddings);
    bool matchSpeaker(Meeting& m, const QString& rawLabel, const EmbeddingSet& embedding);
    void commitIdentification(const Meeting& m, MergedTranscript merged,
                              const QStringList& identifiedLabels);
};

// ==== the shared undo step of the participant operations ===================================
// RAII: BEFORE the operation it takes a snapshot of the meeting's "participant state" (participants,
// approval, description, track inclusion), and AFTER it, if changed, pushes one step onto the meeting's
// shared stack (MeetingUndoService) that restores this state. An editor step created during the operation
// (bindings) is absorbed by the step. When nested, only the outermost records (e.g. removeParticipant ->
// unbindParticipant).
namespace {
QJsonObject participantUndoState(const Meeting& m)
{
    QJsonArray ps;
    for (const Participant& p : m.participants) ps.append(library::toJson(p));
    QJsonArray ts;
    for (const Track& t : m.tracks)
        ts.append(QJsonObject{{QStringLiteral("id"), t.id}, {QStringLiteral("active"), t.active},
                              {QStringLiteral("excludedReason"), t.excludedReason}});
    return QJsonObject{{QStringLiteral("participants"), ps},
                       {QStringLiteral("approval"), m.approval ? QJsonValue(library::toJson(*m.approval)) : QJsonValue()},
                       {QStringLiteral("contextNote"), m.contextNote},
                       {QStringLiteral("tracks"), ts}};
}

quint64 editorTop(editing::SpeakerEditor* ed)
{
    if (!ed) return 0;
    const QVector<quint64> s = ed->undoSerials();
    return s.isEmpty() ? 0 : s.last();
}
} // namespace

struct AppController::ParticipantUndoScope {
    AppController* app = nullptr;
    QString meetingId;
    QString text;
    bool    active = false;
    Meeting before;
    quint64 editorTopBefore = 0;

    ParticipantUndoScope(AppController* a, const QString& id, const QString& label)
        : app(a), meetingId(id), text(label)
    {
        if (id.isEmpty() || app->d->undoScopeDepth[id]++ > 0) return;
        before = app->d->store->load(id);
        if (before.id.isEmpty()) return;
        editorTopBefore = editorTop(app->d->speakerEditors.value(id));
        active = true;
    }
    ~ParticipantUndoScope()
    {
        if (!meetingId.isEmpty() && --app->d->undoScopeDepth[meetingId] <= 0)
            app->d->undoScopeDepth.remove(meetingId);
        if (!active) return;
        const Meeting after = app->d->store->load(meetingId);
        if (after.id.isEmpty() || participantUndoState(after) == participantUndoState(before)) return;
        const quint64 top = editorTop(app->d->speakerEditors.value(meetingId));
        editing::MeetingUndoService* undo = app->meetingUndo(meetingId);
        if (!undo) return;
        QPointer<AppController> self(app);
        const Meeting snap = before;
        undo->pushStep(text, [self, snap]() { if (self) restore(self, snap); },
                       top != editorTopBefore ? top : 0);
    }
    // Writing the snapshot back (the rest of the meeting: title, tags, transcript, is untouched).
    static void restore(AppController* app, const Meeting& snap)
    {
        Meeting m = app->d->store->load(snap.id);
        if (m.id.isEmpty()) return;
        bool tracksChanged = false;
        for (Track& t : m.tracks)
            for (const Track& o : snap.tracks)
                if (o.id == t.id && (o.active != t.active || o.excludedReason != t.excludedReason)) {
                    t.active = o.active;
                    t.excludedReason = o.excludedReason;
                    tracksChanged = true;
                }
        m.participants = snap.participants;
        m.approval = snap.approval;
        m.contextNote = snap.contextNote;
        if (tracksChanged) m.mixdownDirty = true;
        app->d->store->saveMeeting(m);
        emit app->participantsChanged(m.id);
        if (tracksChanged) {
            emit app->tracksChanged(m.id);
            bool anyIncluded = false;
            for (const Track& t : m.tracks) anyIncluded = anyIncluded || t.included();
            if (anyIncluded) app->regenerateMixdown(m.id);
        }
    }
};

// State of a cloud processing run. Filled by the providers' response hook (onExchange).
struct AppController::CloudRun {
    QString meetingId;
    QString kind;            // transcribe | summary | topics | complex
    QString jobId;           // X-Tanara-Job-Id: a new random value per run
    qint64  chargedMicros = 0;
    QString currency;
    int     calls = 0;       // successful charging calls
    cloud::Money   balance;         // the last known balance (after a charge / write-back)
    QString vatMode;
    cloud::CloudError lastError;    // the last non-2xx (non-DELETE) response
    bool    refunded = false;
    cloud::Money   refund;
    QString fileId, transcriptionId;
    QString lastRequestId;

    cloud::Money charged() const { return cloud::Money{ chargedMicros, currency.isEmpty() ? QStringLiteral("USD") : currency }; }
    void addCharge(const cloud::ChargeInfo& c) {
        if (!c.valid) return;
        chargedMicros += c.charge.micros;
        if (currency.isEmpty()) currency = c.charge.currency;
        if (c.balance.isValid()) balance = c.balance;
        if (!c.vatMode.isEmpty()) vatMode = c.vatMode;
        ++calls;
    }
};

bool AppController::Impl::voiceModelUsable() const
{
    if (voiceSet.isEmpty()) return false;
#ifdef TANARA_HAVE_VOICEID
    return true;
#else
    return bool(voiceLoader);   // the build has no voice ID; only with the test loader
#endif
}

// The running ffmpeg's real percentage into the structured state: for a standalone mix the Mixdown
// task, as part of a transcription the transcription task's "mixdown" stage.
void AppController::Impl::reportMixdownPercent(const QString& meetingId, int pct)
{
    if (jobs->isRunning(meetingId, JobKind::Mixdown))
        jobs->setPercent(meetingId, JobKind::Mixdown, pct);
    if (transcribeInMixPhase(meetingId))
        jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("mixdown"),
                       StageState::Running, pct);
}

void AppController::Impl::updateTopicCounts(const QString& meetingId)
{
    if (!jobs->isRunning(meetingId, JobKind::AnalyzeTopics)) return;
    const QPair<int, int> c = jobCounts.value(meetingId);
    jobs->setCounts(meetingId, JobKind::AnalyzeTopics, c.first + c.second,
                    topicBatchTotal.value(meetingId));
}

// Cancelling a running LLM call: we detach the service's signals, delete the provider (with it the job
// and the network request); the connection to the provider breaks, no result arrives.
void AppController::Impl::abortLlmRun(const LlmRun& run)
{
    if (run.preparer) {
        run.preparer->cancel();   // the query / load request is dropped (no signal comes)
        run.preparer->deleteLater();
    }
    if (auto* summary = qobject_cast<processing::SummaryService*>(run.svc.data()))
        summary->cancel();   // the running call is cancelled, the next one does not start (the part notes stay)
    if (run.svc) {
        run.svc->disconnect(q);
        run.svc->deleteLater();
    }
    if (run.provider) run.provider->deleteLater();
}

JobError AppController::Impl::describeLlmFailure(JobKind kind, const QString& raw,
                                                 const CloudRunPtr& run,
                                                 const FailureSinkPtr& sink,
                                                 const LlmContextPtr& ctx) const
{
    if (run && run->lastError.isError())
        return library::describeCloudFailure(kind, run->lastError);
    library::ContextFailureHint hint;
    hint.cloud = bool(run);
    if (ctx) {
        hint.lmStudio = ctx->lmStudio && !run;
        if (ctx->need > 0) hint.recommendedContext = providers::llmctx::contextStepFor(ctx->need);
    }
    return describeJobFailure(kind, raw, (sink && sink->has) ? &sink->ex : nullptr, &hint);
}

providers::LlmModelPreparer* AppController::Impl::prepareLlm(const QString& meetingId, JobKind kind,
                                                  const ProviderConfig& cfg, bool cloud,
                                                  const LlmContextPtr& ctx, std::function<void()> go,
                                                  std::function<void(const JobError&)> onFail)
{
    // Behind Tanara Cloud there is no native API: neither a probe nor a load.
    if (cloud) { go(); return nullptr; }
    auto* prep = new providers::LlmModelPreparer(cfg, q);
    prep->setBusyCheck([this] { return otherLlmBusy(); });
    const int floor = contextFloor.take(serverKey(cfg));
    QObject::connect(prep, &providers::LlmModelPreparer::loadingStarted, prep, [this, prep, meetingId, kind](int ctxLen) {
        // Visible first stage: "Loading the model" (cancellable like the rest of the task).
        QVector<JobStage> stages = jobs->job(meetingId, kind).stages;
        const QStringList others = prep->serverInfo().otherLoaded;
        QString detail = AppController::tr("%1 tokenes kontextus, egy szálon").arg(QStringLiteral("%L1").arg(ctxLen));
        if (!others.isEmpty())
            detail += AppController::tr(" · a szerveren más modell is betöltve: %1").arg(others.join(QStringLiteral(", ")));
        stages.prepend({ QStringLiteral("model"), AppController::tr("Modell betöltése"), StageState::Running, -1, detail });
        jobs->setStages(meetingId, kind, stages);
        jobs->setMessage(meetingId, kind, AppController::tr("Modell betöltése…"));
        emit q->jobProgress(meetingId, AppController::tr("Modell betöltése (%1 tokenes kontextus)…")
                                           .arg(QStringLiteral("%L1").arg(ctxLen)));
    });
    QObject::connect(prep, &providers::LlmModelPreparer::ready, prep, [this, prep, meetingId, kind, ctx, go] {
        ctx->lmStudio = prep->serverInfo().isLmStudio();
        ctx->loadedContext = prep->loadedContext();
        if (jobs->job(meetingId, kind).stage(QStringLiteral("model")))
            jobs->setStage(meetingId, kind, QStringLiteral("model"), StageState::Done);
        prep->deleteLater();
        go();
    });
    QObject::connect(prep, &providers::LlmModelPreparer::failed, prep, [this, prep, meetingId, kind, onFail](JobError e) {
        e.kind = kind;
        if (jobs->job(meetingId, kind).stage(QStringLiteral("model")))
            jobs->setStage(meetingId, kind, QStringLiteral("model"), StageState::Failed);
        prep->deleteLater();
        onFail(e);
    });
    prep->start(ctx->need, floor);
    return prep;
}

// Matching the speaker embeddings computed on a background thread against the voiceprint DB + saving
// (main thread). The same rule as in autoIdentifyMeeting: only the still anonymous labels, above the
// threshold; transcript.md is regenerated with the names; speakerMapChanged signal.
void AppController::Impl::applyIdentification(
    const QString& meetingId, const QVector<QPair<QString, EmbeddingSet>>& embeddings)
{
    if (embeddings.isEmpty() || !voiceprints) return;
    Meeting m = store->load(meetingId);
    if (m.id.isEmpty()) return;
    QStringList identified;
    for (const auto& pair : embeddings) {
        if (pair.second.isEmpty() || m.speakerMap.contains(pair.first)) continue;
        if (matchSpeaker(m, pair.first, pair.second)) identified << pair.first;
    }
    if (identified.isEmpty()) return;
    commitIdentification(m, readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json"))),
                         identified);
}

// THE identification rule in ONE place (shared by the synchronous autoIdentifyMeeting and the
// asynchronous path): the speaker's embedding against the voiceprint DB; above the threshold the name goes
// into the speakerMap, the person into the name list, the score into the overlay (the editor's
// "recognized by voice (82%)").
bool AppController::Impl::matchSpeaker(Meeting& m, const QString& rawLabel,
                                       const EmbeddingSet& embedding)
{
    if (!voiceprints || embedding.isEmpty()) return false;
    const VoiceMatch match = voiceprints->bestMatch(embedding, embedding.keys());
    if (match.score < kVoiceMatchThreshold || match.name.isEmpty()) return false;
    m.speakerMap.insert(rawLabel, match.name);
    if (people) people->add(match.name);
    editing::speakeredit::recordIdentification(m.folder, rawLabel, match.name, match.score);
    return true;
}

// Saving the identification result: meeting.json, transcript.md with the resolved names, marking the
// summary stale (if there is one), and the signals.
void AppController::Impl::commitIdentification(const Meeting& m, MergedTranscript merged,
                                               const QStringList& identifiedLabels)
{
    store->saveMeeting(m);
    if (!merged.tokens.isEmpty()) {
        editing::speakeredit::applyResolvedSpeakers(merged, m);
        writeTextFile(QDir(m.folder).filePath(QStringLiteral("transcript.md")), merged.renderMarkdown());
    }
    if (editing::speakeredit::markSummaryStale(m, identifiedLabels)) emit q->summaryStaleChanged(m.id);
    emit q->speakerMapChanged(m.id);
}

AppController::AppController(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    providers::registerBuiltinProviders();

    d->settings = new SettingsManager(QString(), this);
    const AppSettings s = d->settings->settings();

    // Tanara Cloud mode: TANARA_CLOUD=live|teaser|off overrides settings.cloudEnabled.
    {
        const QString mode = qEnvironmentVariable("TANARA_CLOUD").trimmed().toLower();
        const bool off = mode == QLatin1String("off");
        const bool wantLive = mode == QLatin1String("live") || (mode.isEmpty() && s.cloudEnabled);
        d->cloudLive   = !off && cloud::clientCompiled() && wantLive;
        d->cloudTeaser = !off && !d->cloudLive && cloud::teaserCompiled();
        if (d->cloudLive)
            providers::registerCloudProviders();   // feature flag: without it the "tanara-cloud" id does not even exist
    }
    d->audioDir = expandTilde(s.audioDir);
    d->metaDir  = paths::resolveMetadataDir(s.metadataDir);   // TANARA_HOME-tudatos
    d->notesDir = expandTilde(s.notesDir);
    QDir().mkpath(d->audioDir);
    QDir().mkpath(d->metaDir);

    d->keyStore = KeyStore(QDir(d->metaDir).filePath(QStringLiteral("secrets.json")));

    d->cloud = new cloud::CloudAccount(&d->keyStore, d->metaDir, this);
    {
        const QString envUrl = qEnvironmentVariable("TANARA_CLOUD_URL").trimmed();
        d->cloud->setBaseUrl(!envUrl.isEmpty() ? envUrl : s.cloudBaseUrl);
        d->cloud->setLanguage(activeUiLanguage());
    }
    d->devices = new recording::DeviceManager(this);
    d->store   = new library::MeetingStore(d->audioDir, d->metaDir, this);
    // Bringing orphaned recordings left after a crash (tracks without meeting.json) back into the list.
    d->store->recoverOrphanRecordings();
    // Temporary folders of an interrupted archive import (e.g. quitting during unpacking). Only older ones:
    // the folder of a currently running (e.g. CLI) import is left alone.
    {
        const QDir root(d->store->audioDir());
        const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-6 * 3600);
        for (const QFileInfo& fi : root.entryInfoList({library::MeetingArchive::tempDirPrefix() + QLatin1Char('*')},
                                                      QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot))
            if (fi.lastModified() < cutoff) QDir(fi.absoluteFilePath()).removeRecursively();
    }

    // Structured layer: processing state, library queries, tracks, waveform.
    d->jobs      = new library::MeetingJobTracker(d->store, this);
    d->library   = new library::MeetingLibrary(d->store, d->jobs, this);
    d->tracks    = new library::TrackCatalog(d->store, this);
    d->waveforms = new library::WaveformService(this);
    d->importer  = new library::AudioImporter(d->store, this);
    // Import -> a structured task under the FUTURE meeting's id (there is no such meeting in the library
    // yet; the shell shows it from activeJobs() / the signals).
    connect(d->importer, &library::AudioImporter::started, this, [this](const QString& id, const QString& title) {
        d->jobs->begin(id, JobKind::Import, tr("Importálás"));
        d->jobs->setMessage(id, JobKind::Import, title);
    });
    connect(d->importer, &library::AudioImporter::progress, this,
            [this](const QString& id, int percent, int fileIndex, int fileCount) {
        d->jobs->setPercent(id, JobKind::Import, percent);
        if (fileCount > 1)
            d->jobs->setCounts(id, JobKind::Import, fileIndex, fileCount);
    });
    connect(d->importer, &library::AudioImporter::finished, this, [this](const QString& id, const Meeting& m) {
        d->jobs->finish(id, JobKind::Import);
        emit importFinished(m);
        // Mixdown: exactly as at the end of a recording (see the RecordingSession::finished branch): first the
        // tracks' speech check (a speechless track is left out of the mix).
        if (d->autoMixdown) {
            const bool mix = d->settings->settings().mixdownMode != QStringLiteral("manual");
            if (editing::trackspeech::needsSpeechCheck(m)) startSpeechCheck(m.id, mix);
            else if (mix) regenerateMixdown(m.id);
        }
    });
    connect(d->importer, &library::AudioImporter::failed, this,
            [this](const QString& id, const QString& message, const QString& detail) {
        JobError je;
        je.kind = JobKind::Import;
        je.message = message;
        je.detail = detail;
        d->jobs->fail(id, JobKind::Import, je);
        emit importFailed(id, message, detail);
    });
    connect(d->importer, &library::AudioImporter::cancelled, this, [this](const QString& id) {
        d->jobs->cancelled(id, JobKind::Import);
        emit importCancelled(id);
    });
    d->jobStats  = std::make_unique<library::JobStats>(QDir(d->metaDir).filePath(QStringLiteral("job-stats.json")));
    connect(d->tracks, &library::TrackCatalog::tracksChanged, this, &AppController::tracksChanged);
    // The summary's staleness (speaker-editor layer) is part of the derived state: the library icon's
    // "stale" state and the "Waiting for you" list refresh from it.
    d->jobs->setSummaryStaleProbe([this](const Meeting& m) {
        const SummaryStaleInfo info = summaryStale(m);
        return info.stale ? info.correctedSpeakers : -1;
    });
    connect(this, &AppController::summaryStaleChanged, d->jobs, &library::MeetingJobTracker::notifyStateChanged);

    connect(d->devices, &recording::DeviceManager::devicesChanged, this, &AppController::handleDeviceSetChange);
    connect(d->devices, &recording::DeviceManager::devicesChanged, this, &AppController::devicesChanged);

    d->statePath = QDir(d->metaDir).filePath(QStringLiteral("state.json"));
    d->lastDevices = loadLastDevices(d->statePath);
    d->monitor = new recording::DeviceMonitor(this);
    connect(d->monitor, &recording::DeviceMonitor::levelPeak, this, &AppController::deviceLevelPeak);

    d->people = std::make_unique<library::PeopleStore>(QDir(d->metaDir).filePath(QStringLiteral("people.json")));

    // Voice ID: the voiceprint DB + the model's location: <metaDir>/models/..., if absent, the
    // <appDir>/models/... packaged next to the application (Windows zip / installer).
    d->voiceprints = std::make_unique<library::VoiceprintStore>(
        QDir(d->metaDir).filePath(QStringLiteral("voiceprints.json")));
    d->appDir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
    d->voiceModelPath = processing::VoiceModelRegistry::resolveDefaultModelPath(d->metaDir, d->appDir);
    d->backfillPool.setMaxThreadCount(1);
    d->backfillPool.setObjectName(QStringLiteral("tanara-voiceprint-backfill"));
    d->analysisPool.setMaxThreadCount(2);
    d->analysisPool.setObjectName(QStringLiteral("tanara-participant-analysis"));

    // People window: statistics computed in the background, and the operations (merge, moving a sample ...).
    // Nicknames / the note live in the people.json records.
    d->peopleStats = new library::PeopleStats(d->store, this);
    d->peopleService = new library::PeopleService(this, d->store, d->people.get(), d->voiceprints.get(),
                                         d->peopleStats, this);
    // Whatever affects participants or speaking time affects the statistics too (refreshed coalesced).
    connect(this, &AppController::speakerMapChanged, d->peopleStats, &library::PeopleStats::scheduleRefresh);
    connect(this, &AppController::transcriptReady, d->peopleStats, &library::PeopleStats::scheduleRefresh);
    connect(d->store, &library::MeetingStore::meetingAdded, d->peopleStats, &library::PeopleStats::scheduleRefresh);
    connect(d->store, &library::MeetingStore::meetingUpdated, d->peopleStats, &library::PeopleStats::scheduleRefresh);
    connect(d->store, &library::MeetingStore::meetingRemoved, d->peopleStats, &library::PeopleStats::scheduleRefresh);

    // Tags: the set + per-meeting tags, classic similarity (profiles built on a background thread),
    // embeddings and library preparation, an LLM suggestion after the summary.
    d->tags = new library::TagService(d->store, QDir(d->metaDir).filePath(QStringLiteral("tags.json")), this);
    d->profiles = new library::MeetingProfiles(d->store, this);
    d->tags->setProfiles(d->profiles);
    // Tags on people: manual tags in people.json; the own person is left out.
    d->tags->setPeopleStore(d->people.get());
    d->tags->setSelfNameProvider([this]() {
        return d->settings ? d->settings->settings().userSpeakerName : QString();
    });
    connect(this, &AppController::peopleChanged, d->tags, &library::TagService::invalidatePersonStats);
    d->library->setTagService(d->tags);
    d->embeddings = new library::EmbeddingIndex(d->store, this);
    d->embeddings->setProfiles(d->profiles);
    d->embeddingPreparer = new library::EmbeddingPreparer(
        d->store, d->embeddings, QDir(d->metaDir).filePath(QStringLiteral("embedding-state.json")), this);
    d->llmTags = new processing::LlmTagSuggester(d->tags, this);
    d->tagPool.setMaxThreadCount(1);
    d->tagPool.setObjectName(QStringLiteral("tanara-tag-suggest"));
    connect(d->profiles, &library::MeetingProfiles::idle, this, [this]() {
        const QSet<QString> waiting = d->tagRequests;
        d->tagRequests.clear();
        for (const QString& id : waiting) startTagSuggestionJob(id);
    });
    // The profiles' rows (meeting.json files) and phrases are built on a background thread already at
    // startup; from then on the main thread's readers (tag profile, draft suggestion) never load or compute
    // synchronously, they get the finished snapshot.
    d->profiles->ensureBuilt();
    // The person <-> tag statistics also on a background thread (the main thread reads the finished snapshot).
    d->tags->ensurePersonStats();
    // The similarity-based list stays valid until the tags, transcripts and the index change.
    auto dropTagCache = [this]() { invalidateTagSuggestions(); };
    connect(d->tags, &library::TagService::tagsChanged, this, dropTagCache);
    connect(d->embeddings, &library::EmbeddingIndex::indexChanged, this, dropTagCache);
    connect(d->embeddings, &library::EmbeddingIndex::indexReset, this, dropTagCache);
    // An applied / rejected suggestion: the published list goes out again filtered (without recomputation).
    connect(d->tags, &library::TagService::meetingTagsChanged, this, [this](const QString& id) {
        invalidateTagSuggestions();
        if (d->pendingTags.contains(id)) publishTagSuggestions(id, d->pendingTags.value(id));
    });
    // The participants (manual / approved) and the people's tags changed: the "by participants" suggestion
    // goes out again (the published list keeps the similarity-based part).
    connect(this, &AppController::participantsChanged, this, [this](const QString& id) {
        if (d->pendingTags.contains(id)) publishTagSuggestions(id, d->pendingTags.value(id));
    });
    connect(d->tags, &library::TagService::personTagsChanged, this, [this]() {
        const QStringList ids = d->pendingTags.keys();
        for (const QString& id : ids) publishTagSuggestions(id, d->pendingTags.value(id));
    });
    connect(d->tags, &library::TagService::rejectedChanged, this, [this]() {
        invalidateTagSuggestions();
        const QStringList ids = d->pendingTags.keys();
        for (const QString& id : ids) {
            const QVector<TagSuggestion> before = d->pendingTags.value(id);
            publishTagSuggestions(id, before);
        }
    });
    connect(this, &AppController::transcriptReady, this, [this](const QString& meetingId) {
        invalidateTagSuggestions();
        d->profiles->invalidate(meetingId);
        d->embeddingPreparer->enqueue(meetingId);   // only if there is an embedding provider
    });
    connect(this, &AppController::summaryReady, this, [this](const QString& meetingId) {
        // Only if a tag set already exists: with an empty set the user is not tagging yet, and a model call
        // after every summary (with new-name ideas) would be intrusive and superfluous.
        const AppSettings st = d->settings->settings();
        if (st.tagSuggestions && st.llmTagSuggestions && !d->tags->all().isEmpty())
            requestLlmTagSuggestions(meetingId);
    });
    connect(d->llmTags, &processing::LlmTagSuggester::finished, this,
            [this](const QString& meetingId, const QVector<TagSuggestion>& list) {
        if (d->llmTagsProvider) d->llmTagsProvider->deleteLater();
        d->llmTagsProvider = nullptr;
        finishCloudRun(d->llmTagsRun);
        d->llmTagsRun.reset();
        d->llmTagsMeetingId.clear();
        // An empty LLM response: the earlier suggestions stay.
        publishTagSuggestions(meetingId, list.isEmpty() ? d->pendingTags.value(meetingId) : list);
    });
    connect(d->llmTags, &processing::LlmTagSuggester::failed, this, [this](const QString& meetingId, const QString& error) {
        if (d->llmTagsProvider) d->llmTagsProvider->deleteLater();
        d->llmTagsProvider = nullptr;
        d->llmTagsRun.reset();
        d->llmTagsMeetingId.clear();
        // A suggestion is not an obstacle: on error there is no popup, only a log entry; the earlier list stays.
        qWarning().noquote() << "[tags] LLM-javaslat sikertelen:" << meetingId << error;
        publishTagSuggestions(meetingId, d->pendingTags.value(meetingId));
    });
    connect(d->settings, &SettingsManager::settingsChanged, this, [this]() { applyEmbeddingSettings(true); });
    applyEmbeddingSettings(false);

    // Speaker models: the set per the settings now, again on change (backfill when it grows).
    // The startup backfill runs in the first event-loop turn (under the GUI / the CLI's long commands; for a
    // short CLI command without an event loop it does not even start).
    applyVoiceModels();
    connect(d->settings, &SettingsManager::settingsChanged, this, [this]() { applyVoiceModels(); });
    QTimer::singleShot(0, this, [this]() { backfillVoiceprints(); });

    // Transcript editor: a new summary clears the stale flag; a new transcript drops the manual line
    // corrections (the utterance boundaries changed).
    connect(this, &AppController::summaryReady, this, [this](const QString& meetingId) {
        if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) {
            ed->notifySummaryRegenerated();
        } else {
            const Meeting m = d->store->load(meetingId);
            if (!m.folder.isEmpty()) editing::speakeredit::clearSummaryStale(m.folder);
        }
        emit summaryStaleChanged(meetingId);
    });
    connect(this, &AppController::transcriptReady, this, [this](const QString& meetingId) {
        const Meeting m = d->store->load(meetingId);
        if (m.folder.isEmpty()) return;
        editing::speakeredit::discardForNewTranscript(m.folder);
        if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) ed->reloadTranscript();
    });
}

AppController::~AppController()
{
    // The voiceprint backfill: a queued task is skipped, the running one exits at the next voiceprint.
    d->backfillCancel->store(true);
    d->backfillPool.clear();
    d->backfillPool.waitForDone();
    // The track measurement / voice analysis: the queued ones are skipped, the running one exits at the next window.
    d->analysisShutdown->store(true);
    for (const auto& c : std::as_const(d->analysisRuns)) c->store(true);
    d->analysisPool.clear();
    d->analysisPool.waitForDone();
    // A running tag suggestion reads the profiles / the index: it must finish before them.
    d->tagPool.clear();
    d->tagPool.waitForDone();
    // Stopping and awaiting the background-thread identifications (the thread no longer hands over a result
    // referring to d: the QPointer is null by then).
    for (auto it = d->identifyRuns.begin(); it != d->identifyRuns.end(); ++it) {
        it->cancel->store(true);
        if (it->thread) {
            it->thread->disconnect(this);
            it->thread->wait();
            delete it->thread;
        }
    }
    d->identifyRuns.clear();
    if (d->archiveThread) {
        if (d->archiveCancel) d->archiveCancel->store(true);
        d->archiveThread->disconnect(this);
        d->archiveThread->wait();
        delete d->archiveThread;
        d->archiveThread = nullptr;
    }
}

SettingsManager* AppController::settings() const { return d->settings; }
recording::DeviceManager*   AppController::devices()  const { return d->devices; }
library::MeetingStore*    AppController::store()     const { return d->store; }
library::VoiceprintStore* AppController::voiceprints() const { return d->voiceprints.get(); }
library::PeopleService*   AppController::peopleService() const { return d->peopleService; }
library::PeopleStats*     AppController::peopleStats() const { return d->peopleStats; }
library::TagService*        AppController::tags() const { return d->tags; }
library::MeetingProfiles*   AppController::profiles() const { return d->profiles; }
library::EmbeddingIndex*    AppController::embeddings() const { return d->embeddings; }
library::EmbeddingPreparer* AppController::embeddingPreparer() const { return d->embeddingPreparer; }
QString          AppController::voiceModelPath() const { return d->voiceModelPath; }

QStringList AppController::activeVoiceModelIds() const { return d->voiceModelIds; }

processing::VoiceEmbedderSet AppController::voiceEmbedderSet() const { return d->voiceSet.freshCopy(); }

processing::UtteranceEmbedderFactory AppController::utteranceEmbedderFactory() const
{
    if (!d->voiceModelUsable()) return {};
    return voiceUtteranceEmbedderFactory(d->voiceSet);
}

void AppController::setVoiceEmbedderLoader(processing::PcmEmbedderLoader loader)
{
    d->voiceLoader = std::move(loader);
    d->voiceModelsApplied = false;   // the set is rebuilt together with the loader (without backfill)
    d->voiceModelIds.clear();
    applyVoiceModels();
}

void AppController::applyVoiceModels()
{
    const QStringList enabled = d->settings->enabledVoiceModels();
    processing::VoiceEmbedderSet set = processing::VoiceEmbedderSet::fromSettings(enabled, d->metaDir, d->appDir, d->voiceLoader);
    const QStringList ids = set.modelIds();
    if (d->voiceModelsApplied && ids == d->voiceModelIds) return;   // another setting changed
    bool grew = false;
    for (const QString& id : ids)
        if (!d->voiceModelIds.contains(id)) grew = true;
    const bool first = !d->voiceModelsApplied;
    d->voiceModelsApplied = true;
    d->voiceSet = std::move(set);
    d->voiceModelIds = ids;
    for (const QString& id : enabled)
        if (!ids.contains(id))
            qCInfo(lcVoice).noquote() << "Hangmodell bekapcsolva, de nem használható (ismeretlen id vagy"
                                         " hiányzó fájl):" << id;
    qCInfo(lcVoice).noquote() << "Használt hangmodellek:" << (ids.isEmpty() ? QStringLiteral("-") : ids.join(QStringLiteral(", ")));
    // The open editors continue with the new model list and factory.
    for (const QPointer<editing::SpeakerEditor>& ed : std::as_const(d->speakerEditors)) {
        if (!ed) continue;
        ed->setEmbedderFactory(utteranceEmbedderFactory());
        ed->setVoiceModelIds(d->editorModelIds(d->settings));
    }
    emit voiceModelsChanged();
    if (grew && !first) backfillVoiceprints();
}

bool AppController::voiceprintBackfillRunning() const { return d->backfillRunning; }

void AppController::backfillVoiceprints()
{
    if (!d->voiceprints) return;
    if (d->backfillRunning) { d->backfillAgain = true; return; }
    if (!d->voiceModelUsable()) { emit voiceprintBackfillFinished(0, 0); return; }
    d->backfillSkipped.clear();   // a new run = a new report

    // The task list is made on the main thread (only the main thread reaches the DB); the background thread
    // only decodes and embeds. We load each meeting once.
    d->voiceprints->refresh();
    QVector<Impl::BackfillJob> jobs;
    QHash<QString, Meeting> meetings;
    for (const QString& person : d->voiceprints->people()) {
        for (const QString& model : std::as_const(d->voiceModelIds)) {
            // A sibling-exact list (printsMissingModel works at sampleRef level: it would mix up the same span of two
            // meetings' identically named mixes).
            for (const Voiceprint& vp : d->voiceprints->samplesMissingModel(person, model)) {
                auto it = meetings.constFind(vp.sourceMeetingId);
                if (it == meetings.constEnd())
                    it = meetings.insert(vp.sourceMeetingId,
                                         vp.sourceMeetingId.isEmpty() ? Meeting() : d->store->load(vp.sourceMeetingId));
                if (it->id.isEmpty()) {
                    d->noteBackfillSkip(person, model, vp, processing::VoiceprintBackfillSkip::Reason::MissingMeeting);
                    const QString key = vp.id + QLatin1Char('|') + model;
                    if (!d->backfillLogged.contains(key)) {
                        d->backfillLogged.insert(key);
                        qCInfo(lcVoice).noquote() << "Lenyomat-pótlás: a forrás-megbeszélés nincs meg, kihagyva:"
                                                  << person << vp.sampleRef << model;
                    }
                    continue;
                }
                jobs.append({person, model, vp, it.value()});
            }
        }
    }
    if (jobs.isEmpty()) { emit voiceprintBackfillFinished(0, int(d->backfillSkipped.size())); return; }

    d->backfillRunning = true;
    d->backfillAdded = 0;
    d->backfillDone = 0;                       // k / N progress
    d->backfillTotal = int(jobs.size());
    emit voiceprintBackfillStarted(d->backfillTotal);
    qCInfo(lcVoice).noquote() << "Lenyomat-pótlás indul:" << jobs.size() << "lenyomat";
    const processing::VoiceEmbedderSet proto = d->voiceSet.freshCopy();
    const auto cancel = d->backfillCancel;
    QPointer<AppController> self(this);
    d->backfillPool.start([self, jobs, proto, cancel]() {
        const processing::VoiceEmbedderSet emb = proto.freshCopy();   // the thread's own model instances
        for (const Impl::BackfillJob& job : jobs) {
            if (cancel->load()) break;
            bool missing = false;
            const QVector<float> vec = embedPrintSource(emb, job.model, job.meeting, job.print, &missing);
            QMetaObject::invokeMethod(qApp, [self, job, vec, missing]() {
                if (self) self->d->finishBackfillJob(job, vec, missing);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(qApp, [self]() {
            if (self) self->d->finishBackfill();
        }, Qt::QueuedConnection);
    });
}

// One backfilled vector into the DB (main thread). The DB may have changed meanwhile (a deleted / moved
// sample, another process backfilled): it goes in only if the model is still missing from the group.
void AppController::Impl::finishBackfillJob(const BackfillJob& job, const QVector<float>& vec,
                                            bool missingAudio)
{
    const QString key = job.print.id + QLatin1Char('|') + job.model;
    ++backfillDone;
    emit q->voiceprintBackfillProgress(backfillDone, backfillTotal);
    if (vec.isEmpty()) {
        noteBackfillSkip(job.person, job.model, job.print,
                         missingAudio ? processing::VoiceprintBackfillSkip::Reason::MissingAudio
                                      : processing::VoiceprintBackfillSkip::Reason::Failed);
        if (!backfillLogged.contains(key)) {
            backfillLogged.insert(key);
            qCInfo(lcVoice).noquote() << (missingAudio ? "Lenyomat-pótlás: hiányzó hang, kihagyva:"
                                                       : "Lenyomat-pótlás: nem számolható, kihagyva:")
                                      << job.person << job.print.sampleRef << job.model;
        }
        return;
    }
    voiceprints->refresh();
    const QString sib = library::VoiceprintStore::siblingKey(job.print);
    bool stillMissing = false;
    for (const Voiceprint& p : voiceprints->samplesMissingModel(job.person, job.model))
        if (library::VoiceprintStore::siblingKey(p) == sib) { stillMissing = true; break; }
    if (!stillMissing) return;
    Voiceprint vp = job.print;
    vp.id.clear();   // addPrint generates one
    vp.model = job.model;
    vp.embedding = vec;
    vp.dim = vec.size();
    voiceprints->addPrint(job.person, vp);
    ++backfillAdded;
}

void AppController::Impl::finishBackfill()
{
    backfillRunning = false;
    const int added = backfillAdded;
    backfillAdded = 0;
    if (added > 0) {
        qCInfo(lcVoice).noquote() << "Lenyomat-pótlás kész:" << added << "új lenyomat";
        emit q->voiceprintsChanged();
    }
    emit q->voiceprintBackfillFinished(added, int(backfillSkipped.size()));
    if (backfillAgain) {
        backfillAgain = false;
        q->backfillVoiceprints();
    }
}
RecordingState   AppController::recordingState() const { return d->state; }
QString AppController::currentMeetingFolder() const { return d->currentFolder; }

void AppController::refreshDevices() { d->devices->refresh(); }

void AppController::setRecordingEngineFactory(std::function<std::unique_ptr<recording::AudioEngine>()> factory) {
    d->recordingEngineFactory = std::move(factory);
}

QStringList AppController::lastUsedDeviceNames() const { return d->lastDevices; }

void AppController::setLastUsedDeviceNames(const QStringList& names) {
    d->lastDevices = names;
    saveLastDevices(d->statePath, names);
}

QStringList AppController::knownPeople() const {
    return d->people ? d->people->names() : QStringList();
}

void AppController::renameSpeaker(const QString& meetingId, const QString& rawLabel,
                                 const QString& displayName, bool enroll) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    const QString name = displayName.trimmed();
    if (name.isEmpty() || name == rawLabel) {
        m.speakerMap.remove(rawLabel);
    } else {
        m.speakerMap.insert(rawLabel, name);
        if (d->people) d->people->add(name);
    }
    d->store->saveMeeting(m);

    // Regenerating transcript.md with the FULL mapping (from the raw tokens).
    // segments.json STAYS RAW so the UI can relabel any time.
    MergedTranscript merged =
        readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (!merged.tokens.isEmpty()) {
        editing::speakeredit::applyResolvedSpeakers(merged, m);
        writeTextFile(QDir(m.folder).filePath(QStringLiteral("transcript.md")), merged.renderMarkdown());
    }
    if (editing::speakeredit::markSummaryStale(m, {rawLabel})) emit summaryStaleChanged(meetingId);
    emit speakerMapChanged(meetingId);

    // Manual labeling "teaches" voice ID: we record a voiceprint under the name.
    if (enroll && !name.isEmpty() && name != rawLabel)
        enrollSpeaker(meetingId, rawLabel, name);
}

void AppController::enrollSpeaker(const QString& meetingId, const QString& rawLabel, const QString& name) {
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    // No model: silently skipped (manual labeling works anyway).
    if (!d->voiceModelUsable() || !d->voiceSet.ensureLoaded()) return;

    const MergedTranscript merged =
        readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) return;
    const VoiceSource src = resolveVoiceSource(m, merged, rawLabel);
    if (src.absPath.isEmpty()) return;

    QStringList refs;
    const EmbeddingSet set = embeddingForLabel(d->voiceSet, src.absPath, src.offsetMs, merged, rawLabel,
                                               src.fileRel, &refs);
    if (set.isEmpty()) return;

    Voiceprint tmpl;
    tmpl.sourceMeetingId = m.id;
    tmpl.sourceTrack = src.trackId;
    tmpl.device = src.device;
    tmpl.sampleRef = representativeSampleRef(merged, rawLabel, src.fileRel);
    tmpl.sourceRefs = refs;
    tmpl.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const Voiceprint& vp : printsFromSet(set, tmpl)) d->voiceprints->addPrint(nm, vp);
    emit voiceprintsChanged();
}

void AppController::autoIdentifyMeeting(const QString& meetingId,
                                       const std::function<bool(int,int)>& onProgress) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    if (!d->voiceModelUsable() || !d->voiceSet.ensureLoaded()) return;

    const MergedTranscript merged =
        readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) return;

    // Distinct raw speaker labels.
    QStringList labels;
    for (const TranscriptToken& t : merged.tokens)
        if (!t.speaker.isEmpty() && !labels.contains(t.speaker))
            labels << t.speaker;

    // We match EVERY unnamed speaker (mic + loopback) against the voiceprint DB. There is no
    // loudness/position-based assumption and no auto-enroll: the NAME goes onto the speaker by fingerprint
    // (or the user's one-time manual rename).
    bool mapChanged = false;
    QStringList identified;   // raw labels that got a name now
    int progressDone = 0;
    for (const QString& label : labels) {
        if (onProgress && !onProgress(++progressDone, labels.size()))
            break;   // the user cancelled: the matches already found are saved
        if (m.speakerMap.contains(label))
            continue;   // already named (by hand or an earlier match)
        const VoiceSource src = resolveVoiceSource(m, merged, label);
        if (src.absPath.isEmpty()) continue;
        const EmbeddingSet e = embeddingForLabel(d->voiceSet, src.absPath, src.offsetMs, merged, label);
        if (e.isEmpty()) continue;
        if (d->matchSpeaker(m, label, e)) {
            identified << label;
            mapChanged = true;
        }
    }

    if (mapChanged)
        d->commitIdentification(m, merged, identified);
}

VoiceMatch AppController::testSpeakerMatch(const QString& meetingId, const QString& rawLabel) {
    VoiceMatch none;   // { "", -1 }
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return none;
    if (!d->voiceModelUsable() || !d->voiceSet.ensureLoaded()) return none;
    const MergedTranscript merged =
        readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) return none;
    const VoiceSource src = resolveVoiceSource(m, merged, rawLabel);
    if (src.absPath.isEmpty()) return none;
    const EmbeddingSet e = embeddingForLabel(d->voiceSet, src.absPath, src.offsetMs, merged, rawLabel);
    if (e.isEmpty()) return none;
    return d->voiceprints->bestMatch(e, e.keys());
}

QVector<ParticipantGuess> AppController::identifyParticipants(
    const QString& meetingId, const std::function<bool(int,int)>& onProgress) {
    QVector<ParticipantGuess> out;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return out;
    if (!d->voiceModelUsable() || !d->voiceSet.ensureLoaded()) return out;
    // Clustering runs on the fused vector (with every loadable model); a window counts only if every model gave a vector for it.
    const QStringList modelIds = d->voiceSet.loadedModelIds();

    constexpr qint64 kWinMs = 3000;          // window length
    constexpr int    kTargetWindows = 50;    // number of windows per track (upper limit)
    constexpr double kMergeThreshold = 0.55; // cosine threshold for cluster merging
    constexpr float  kSilenceRms = 0.004f;   // a window quieter than this = silence (skipped)

    // Progress: all windows to process (active tracks x windows per track), for the callback.
    int progressTotal = 0, progressDone = 0;
    if (m.durationMs >= kWinMs) {
        const qint64 step0 = std::max<qint64>(kWinMs, m.durationMs / kTargetWindows);
        int perTrack = 0;
        for (qint64 s = 0; s + kWinMs <= m.durationMs; s += step0) ++perTrack;
        int activeTracks = 0;
        for (const Track& t : m.tracks) if (t.active) ++activeTracks;
        progressTotal = perTrack * activeTracks;
    }

    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        const qint64 D = m.durationMs;
        if (D < kWinMs) continue;
        const QString path = QDir(m.folder).filePath(t.file);
        const qint64 step = std::max<qint64>(kWinMs, D / kTargetWindows);

        QVector<QVector<float>> embs;      // fused vectors
        QVector<EmbeddingSet> sets;        // the same per model (for voiceprint matching)
        QVector<qint64> winStart;
        for (qint64 s = 0; s + kWinMs <= D; s += step) {
            if (onProgress && !onProgress(++progressDone, progressTotal))
                return out;   // the user cancelled
            // The window is in meeting time; a window outside the file of a track that starts later / ends earlier
            // is empty (skipped).
            const tanara::tracktiming::FileRange fr = tanara::tracktiming::fileRange(t, s, s + kWinMs);
            if (!fr.valid()) continue;
            QVector<float> pcm = processing::VoiceEmbedder::decodePcm16kMono(path, fr.startMs, fr.endMs);
            if (pcm.isEmpty()) continue;
            double sum = 0.0;
            for (float x : pcm) sum += static_cast<double>(x) * x;
            const double rms = std::sqrt(sum / static_cast<double>(pcm.size()));
            if (rms < kSilenceRms) continue;          // silence -> skipped
            const EmbeddingSet set = d->voiceSet.embedPcmWith(pcm, modelIds);
            const QVector<float> e = processing::fusion::fuse(set, modelIds);
            if (!e.isEmpty()) { embs.append(e); sets.append(set); winStart.append(s); }
        }
        if (embs.isEmpty()) continue;

        const QVector<int> labels = clusterEmbeddings(embs, kMergeThreshold);
        QMap<int, QVector<int>> groups;
        for (int i = 0; i < labels.size(); ++i) groups[labels[i]].append(i);

        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            const QVector<int>& idxs = it.value();
            // Noise filter: a 1-window cluster is dropped if there is another (overlap/click).
            if (idxs.size() < 2 && groups.size() > 1) continue;

            QVector<float> cent(embs[idxs[0]].size(), 0.0f);
            for (int i : idxs)
                for (int k = 0; k < cent.size(); ++k) cent[k] += embs[i][k];
            for (int k = 0; k < cent.size(); ++k) cent[k] /= idxs.size();
            cent = library::VoiceprintStore::l2normalize(cent);

            // Per-model centroid for matching (voiceprints are stored per model).
            EmbeddingSet centSet;
            for (const QString& id : modelIds) {
                QVector<float> c;
                for (int i : idxs) {
                    const QVector<float>& v = sets[i].value(id);
                    if (c.isEmpty()) c.fill(0.0f, v.size());
                    for (int k = 0; k < c.size() && k < v.size(); ++k) c[k] += v[k];
                }
                centSet.insert(id, library::VoiceprintStore::l2normalize(c));
            }
            const VoiceMatch mt = d->voiceprints->bestMatch(centSet, modelIds);
            // Representative window = the nearest to the centroid (medoid).
            qint64 repStart = winStart[idxs[0]]; double bestSim = -2.0;
            for (int i : idxs) {
                const double s = library::VoiceprintStore::cosineSimilarity(cent, embs[i]);
                if (s > bestSim) { bestSim = s; repStart = winStart[i]; }
            }

            ParticipantGuess g;
            g.trackId = t.id;
            g.deviceName = t.deviceName;
            g.name = (mt.score >= kVoiceMatchThreshold) ? mt.name : QString();
            g.score = mt.score;
            g.windows = idxs.size();
            g.sampleRef = QStringLiteral("%1#%2-%3").arg(t.file).arg(repStart).arg(repStart + kWinMs);
            out.append(g);
        }
    }
    // The most windows first (dominant speakers first).
    std::sort(out.begin(), out.end(),
              [](const ParticipantGuess& a, const ParticipantGuess& b) { return a.windows > b.windows; });
    return out;
}

void AppController::enrollVoiceprintFromSample(const QString& name, const QString& meetingId,
                                              const QString& trackId, qint64 startMs, qint64 endMs) {
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    if (!d->voiceModelUsable() || !d->voiceSet.ensureLoaded()) return;
    const Track* track = nullptr;
    for (const Track& t : m.tracks) if (t.id == trackId) { track = &t; break; }
    if (!track) return;
    const QString path = QDir(m.folder).filePath(track->file);
    // startMs/endMs in meeting time (like the sampleRef) -> the track file's time.
    const tanara::tracktiming::FileRange fr = tanara::tracktiming::fileRange(*track, startMs, endMs);
    if (!fr.valid()) return;
    const EmbeddingSet set =
        d->voiceSet.embedPcm(processing::VoiceEmbedder::decodePcm16kMono(path, fr.startMs, fr.endMs));
    if (set.isEmpty()) return;

    Voiceprint tmpl;
    tmpl.sourceMeetingId = m.id;
    tmpl.sourceTrack = track->id;
    tmpl.device = track->deviceName;
    tmpl.sampleRef = QStringLiteral("%1#%2-%3").arg(track->file).arg(startMs).arg(endMs);
    tmpl.sourceRefs = QStringList{tmpl.sampleRef};
    tmpl.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const Voiceprint& vp : printsFromSet(set, tmpl)) d->voiceprints->addPrint(nm, vp);
    if (d->people) d->people->add(nm);
    emit voiceprintsChanged();
    emit peopleChanged();
}

void AppController::renamePerson(const QString& oldName, const QString& newName) {
    const QString o = oldName.trimmed(), n = newName.trimmed();
    if (o.isEmpty() || n.isEmpty() || o == n) return;
    if (d->tags) d->tags->renamePersonRefs(o, n);   // rejections + undo (BEFORE the rename)
    if (d->people) d->people->rename(o, n);   // the whole record goes (nicknames, note, tags)
    if (d->voiceprints) { d->voiceprints->renamePerson(o, n); emit voiceprintsChanged(); }

    const QVector<Meeting> all = d->store->loadAll();
    for (const Meeting& idx : all) {
        Meeting m = d->store->load(idx.id);
        bool changed = false;
        // 1) Rewriting the existing mapping VALUES (diarized -> name).
        for (auto it = m.speakerMap.begin(); it != m.speakerMap.end(); ++it)
            if (it.value() == o) { it.value() = n; changed = true; }
        // 2) Renaming the frozen track speaker (mic): updating track.speakerLabel, and, since the raw tokens
        //    still say "o", an o -> n mapping so the already finished transcript shows the fresh name too.
        bool micHadOld = false;
        for (Track& tr : m.tracks)
            if (tr.speakerLabel == o) { tr.speakerLabel = n; micHadOld = true; }
        if (micHadOld) { m.speakerMap.insert(o, n); changed = true; }
        // 3) The person name of manually added participants (transcript.speakers.json).
        if (editing::speakeredit::renamePersonInOverlay(m.folder, o, n)) changed = true;

        if (!changed) continue;
        d->store->saveMeeting(m);
        MergedTranscript merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
        if (!merged.tokens.isEmpty()) {
            editing::speakeredit::applyResolvedSpeakers(merged, m);
            writeTextFile(QDir(m.folder).filePath(QStringLiteral("transcript.md")), merged.renderMarkdown());
        }
        emit speakerMapChanged(m.id);
    }
    emit peopleChanged();
}

void AppController::removePerson(const QString& name) {
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return;
    if (d->people) d->people->remove(nm);
    if (d->tags) d->tags->forgetPerson(nm);
    if (d->voiceprints) { d->voiceprints->removePerson(nm); emit voiceprintsChanged(); }

    const QVector<Meeting> all = d->store->loadAll();
    for (const Meeting& idx : all) {
        Meeting m = d->store->load(idx.id);
        bool changed = false;
        const QList<QString> keys = m.speakerMap.keys();
        for (const QString& key : keys)
            if (m.speakerMap.value(key) == nm) { m.speakerMap.remove(key); changed = true; }
        if (editing::speakeredit::removePersonFromOverlay(m.folder, nm)) changed = true;
        if (!changed) continue;
        d->store->saveMeeting(m);
        MergedTranscript merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
        if (!merged.tokens.isEmpty()) {
            editing::speakeredit::applyResolvedSpeakers(merged, m);
            writeTextFile(QDir(m.folder).filePath(QStringLiteral("transcript.md")), merged.renderMarkdown());
        }
        emit speakerMapChanged(m.id);
    }
    emit peopleChanged();
}

void AppController::renameMeeting(const QString& meetingId, const QString& newTitle) {
    const QString t = newTitle.trimmed();
    if (t.isEmpty()) return;
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    m.title = t;
    d->store->saveMeeting(m);   // meeting.json + the index refresh -> meetingUpdated signal
}

void AppController::setMeetingContextNote(const QString& meetingId, const QString& note) {
    ParticipantUndoScope undoScope(this, meetingId, tr("Leírás módosítva"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QString n = note.trimmed();
    if (m.contextNote == n) return;   // no change
    m.contextNote = n;
    d->store->saveMeeting(m);
}

void AppController::setMeetingDetectedCall(const QString& meetingId, const QString& appName) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QString a = appName.trimmed();
    if (m.detectedCallApp == a) return;
    m.detectedCallApp = a;
    d->store->saveMeeting(m);
}

void AppController::deleteMeeting(const QString& meetingId) {
    if (meetingId.isEmpty()) return;
    // Stopping the running tasks before the folder disappears under them.
    cancelAllJobs(meetingId);
    if (const auto it = d->analysisRuns.constFind(meetingId); it != d->analysisRuns.constEnd())
        it.value()->store(true);   // the voice analysis's result no longer goes to the deleted meeting
    d->waveforms->cancel(meetingId);
    closeSpeakerEditor(meetingId);   // the open speaker editor must not write into the deleted folder
    d->mergedCache.remove(meetingId);
    d->store->deleteMeeting(meetingId);   // the meetingRemoved signal from the store
}

void AppController::restoreTrack(const QString& meetingId, const QString& trackId) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    bool changed = false;
    for (Track& t : m.tracks)
        if (t.id == trackId && !t.included()) {
            t.active = true;
            t.excludedReason.clear();
            changed = true;
        }
    if (!changed) return;
    m.mixdownDirty = true;   // the active track set changed, so the mixdown is stale
    d->store->saveMeeting(m);
    emit tracksChanged(meetingId);
}

void AppController::deleteTrack(const QString& meetingId, const QString& trackId) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    QVector<Track> kept;
    bool removed = false;
    for (const Track& t : m.tracks) {
        if (t.id == trackId) {
            // PHYSICAL deletion of the audio file (an explicit user action), unless the file is the MIXDOWN (in an
            // old meeting.json it may be listed as a track), or another track also refers to this file: then only the
            // track entry is deleted.
            bool shared = t.file.isEmpty() || (!m.mixdownFile.isEmpty() && t.file == m.mixdownFile);
            for (const Track& o : m.tracks)
                if (o.id != trackId && o.file == t.file) shared = true;
            if (!shared) {
                QFile::remove(QDir(m.folder).filePath(t.file));
                library::WaveformService::removeCache(QDir(m.folder).filePath(t.file));   // the waveform cache too
            }
            removed = true;
        } else {
            kept.push_back(t);
        }
    }
    if (!removed) return;
    m.tracks = kept;
    m.mixdownDirty = true;   // the active track set changed, so the mixdown is stale
    d->store->saveMeeting(m);
    emit tracksChanged(meetingId);
}

void AppController::regenerateMixdown(const QString& meetingId) {
    // A mix is already running on this meeting: a no-op (the running one's result will do for everyone).
    if (d->mixdownProcs.contains(meetingId)) return;

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;

    d->mixdownFailReason.remove(meetingId);
    const QString outRel = QStringLiteral("mixdown.mp3");
    const QString outPath = QDir(m.folder).filePath(outRel);

    // Only the active tracks that exist on disk go into the mix, those starting later shifted into place
    // (startOffsetMs -> adelay). The filter graph: MixdownPlan.
    const recording::MixdownPlan plan = recording::MixdownPlan::fromMeeting(m);
    const QStringList& missing = plan.missing;
    // With an active track missing we do not replace the EXISTING (complete) mix with a partial one, neither
    // for a manual remix nor for the automatic one before transcription.
    const bool haveMixdown = QFileInfo(outPath).isFile()
        || (!m.mixdownFile.isEmpty() && QFileInfo(QDir(m.folder).filePath(m.mixdownFile)).isFile());
    if (!missing.isEmpty() && haveMixdown) {
        const QString msg = tr("Hiányzik a(z) „%1” sáv hangfájlja — a meglévő lekeverés megmaradt. "
                               "Keresd meg a fájlt a Sávok fülön, vagy dobd el a sávot, és keverd újra.")
                                .arg(missing.join(QStringLiteral("”, „")));
        d->mixdownFailReason.insert(meetingId, msg);
        emit errorOccurred(msg);
        emit mixdownUpdated(meetingId, false);
        return;
    }
    if (plan.inputs.isEmpty()) {
        emit errorOccurred(tr("Nincs aktív hangsáv a lekeveréshez."));
        emit mixdownUpdated(meetingId, false);
        return;
    }

    // We mix into a half-made file and replace the old one only after SUCCESS: on cancel or error the
    // earlier (still playable) mix stays untouched.
    const QString partPath = QDir(m.folder).filePath(QStringLiteral("mixdown.part.mp3"));

    const QStringList args = plan.ffmpegArgs(partPath);
    const qint64 totalMs = m.durationMs;   // the denominator of the percentage

    // An asynchronous QProcess: it does NOT block the UI (even a 90-minute mix may run), nor does it block
    // starting a new recording (a separate child process + a separate capture engine).
    auto* proc = new QProcess(this);
    proc->setProgram(QStringLiteral("ffmpeg"));
    proc->setArguments(args);
    d->mixdownProcs.insert(meetingId, proc);
    // Structured state: a standalone mix is its own (cancellable) task; a mix running as part of a
    // transcription moves the transcription task's "mixdown" stage (see reportMixdownPercent).
    if (!d->transcribeInMixPhase(meetingId))
        d->jobs->begin(meetingId, JobKind::Mixdown, tr("Lekeverés"));

    // Parsing stdout: out_time_us=<microseconds> -> a percentage relative to the recording's length.
    connect(proc, &QProcess::readyReadStandardOutput, this,
            [this, proc, meetingId, totalMs]() {
        if (totalMs <= 0) return;
        const QByteArray chunk = proc->readAllStandardOutput();
        int lastPct = -1;
        for (const QByteArray& line : chunk.split('\n')) {
            const int eq = line.indexOf('=');
            if (eq < 0) continue;
            const QByteArray key = line.left(eq).trimmed();
            const QByteArray val = line.mid(eq + 1).trimmed();
            qint64 outMs = -1;
            if (key == "out_time_us")      outMs = val.toLongLong() / 1000;
            else if (key == "out_time_ms") outMs = val.toLongLong() / 1000;  // (ffmpeg: actually microseconds)
            if (outMs >= 0) {
                const qint64 p = outMs * 100 / totalMs;
                lastPct = static_cast<int>(p < 0 ? 0 : (p > 99 ? 99 : p));
            }
        }
        if (lastPct >= 0) {
            emit mixdownProgress(meetingId, lastPct);
            d->reportMixdownPercent(meetingId, lastPct);
        }
    });
    // The closing in ONE place (normal exit, error, cancel, an ffmpeg that did not even start).
    auto done = [this, proc, meetingId, outRel, outPath, partPath](bool exitedOk) {
        if (d->mixdownProcs.value(meetingId) != proc) return;   // we already closed it
        d->mixdownProcs.remove(meetingId);
        const bool cancelled = d->mixdownCancelled.remove(meetingId);
        bool ok = exitedOk && !cancelled;
        // Replacing so the old mix survives a failure (not "delete, then rename").
        if (ok)
            ok = replaceFile(partPath, outPath);
        if (!ok) QFile::remove(partPath);   // no half-made file stays in the folder
        if (ok) {
            // A fresh meeting (it may have changed meanwhile) -> set mixdownFile + clear dirty.
            Meeting mm = d->store->load(meetingId);
            if (!mm.id.isEmpty()) {
                mm.mixdownFile  = outRel;
                mm.mixdownDirty = false;
                d->store->saveMeeting(mm);
            }
            emit mixdownProgress(meetingId, 100);
        } else if (!cancelled) {
            emit errorOccurred(tr("A lekeverés (ffmpeg) sikertelen."));
        }
        if (d->jobs->isRunning(meetingId, JobKind::Mixdown)) {
            if (ok) d->jobs->finish(meetingId, JobKind::Mixdown);
            else if (cancelled) d->jobs->cancelled(meetingId, JobKind::Mixdown);
            else {
                JobError je;
                je.message = tr("A lekeverés nem sikerült.");
                je.detail = QString::fromUtf8(proc->readAllStandardError()).simplified().left(200);
                d->jobs->fail(meetingId, JobKind::Mixdown, je);
            }
        }
        emit mixdownUpdated(meetingId, ok);
        emit tracksChanged(meetingId);   // the view should refresh (button state, mixdownFile)
        proc->deleteLater();
        // The pre-transcript voice analysis works from the raw tracks, independent of the mixdown; we try here
        // only because a mix can also run after a track change (an excluded track).
        if (ok) maybeAnalyzeParticipants(meetingId);
    };
    connect(proc, &QProcess::finished, this, [done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit && code == 0);
    });
    connect(proc, &QProcess::errorOccurred, this, [done](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) done(false);   // (no finished signal then)
    });

    // Progress is signaled by mixdownProgress (0..100) to the non-modal UI, NOT jobProgress (that would turn
    // on the global busy flag, which would needlessly disable the action buttons during a background
    // mixdown). Starting 0%:
    emit mixdownProgress(meetingId, 0);
    d->reportMixdownPercent(meetingId, 0);
    proc->start();
}

void AppController::setUserSpeakerName(const QString& name) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    AppSettings s = d->settings->settings();
    const QString old = s.userSpeakerName.trimmed();
    if (old == n) {
        if (d->people) d->people->add(n);
        emit peopleChanged();
        return;
    }
    s.userSpeakerName = n;
    d->settings->setSettings(s);
    // The name change should propagate to EVERY meeting: the mic track's frozen speaker name, the mappings
    // and the voiceprints are rewritten too (renamePerson handles all of this).
    if (!old.isEmpty())
        renamePerson(old, n);                 // emits peopleChanged + speakerMapChanged from inside
    else {
        if (d->people) d->people->add(n);
        emit peopleChanged();
    }
}

void AppController::startLevelMonitoring() {
    d->monitorLegacy = true;
    beginLevelMonitoring();
}

void AppController::stopLevelMonitoring() {
    d->monitorLegacy = false;
    if (!d->monitorHolders.isEmpty()) return;   // another consumer (retain) still requests it
    endLevelMonitoring();
}

void AppController::retainLevelMonitoring(QObject* owner) {
    if (!owner || d->monitorHolders.contains(owner)) return;
    // When the consumer goes away, its request goes too: the microphone must not stay open afterwards.
    d->monitorHolders.insert(owner, connect(owner, &QObject::destroyed, this,
                                            [this, owner] { releaseLevelMonitoring(owner); }));
    if (!(d->monitorWanted && d->monitor && d->monitor->active()))
        beginLevelMonitoring();
}

void AppController::releaseLevelMonitoring(QObject* owner) {
    if (!d->monitorHolders.contains(owner)) return;
    disconnect(d->monitorHolders.take(owner));
    if (d->monitorHolders.isEmpty() && !d->monitorLegacy)
        endLevelMonitoring();
}

void AppController::beginLevelMonitoring() {
    d->monitorWanted = true;
    if (d->state == RecordingState::Stopping || d->state == RecordingState::Encoding) return;
    d->devices->refresh();
    restartLevelMonitor(/*force*/ true);
}

void AppController::endLevelMonitoring() {
    d->monitorWanted = false;
    d->monitorNames.clear();
    if (d->monitor) d->monitor->stop();
}

void AppController::setAutoMixdownAfterRecording(bool on) { d->autoMixdown = on; }

void AppController::restartLevelMonitor(bool force) {
    if (!d->monitor || !d->monitorWanted) return;
    const bool recording = d->state == RecordingState::Recording;
    if (d->state != RecordingState::Idle && !recording) return;
    QVector<AudioDeviceInfo> list;
    QStringList names;
    for (const AudioDeviceInfo& dev : d->devices->captureDevices()) {
        // During recording the devices on a track are owned and measured by the RecordingSession.
        if (recording && d->recNames.contains(dev.name) && !d->recClosed.contains(dev.name)
            && !d->recOff.contains(dev.name))
            continue;
        list.push_back(dev);
        names << dev.name;
    }
    if (!force && d->monitor->active() && names == d->monitorNames) return;
    QElapsedTimer perfTimer;
    perfTimer.start();
    const QStringList before = d->monitorNames;
    d->monitorNames = names;
    d->monitor->start(list);   // for an empty list it stops; the open runs on the monitor's background thread
    qCDebug(lcPerf).noquote() << QStringLiteral("szintmérő újraindítás kérve (%1 → %2 eszköz, force=%3): %4 ms a fő szálon")
                                 .arg(before.size()).arg(names.size()).arg(force).arg(perfTimer.elapsed());
}

void AppController::handleDeviceSetChange() {
    const QVector<AudioDeviceInfo> present = d->devices->captureDevices();
    if (d->state == RecordingState::Recording && d->session && !present.isEmpty()) {
        // Hot-plug during recording: the track of a recorded device missing from the enumeration TWICE in a row
        // is safely closed (a single transient absence must not close a track; and an empty list, an unreachable
        // audio system, closes nothing).
        bool recheck = false;
        const QStringList rec = d->recNames;
        for (const QString& name : rec) {
            if (d->recClosed.contains(name) || d->recOff.contains(name)) continue;
            bool found = false;
            for (const AudioDeviceInfo& dev : present)
                if (dev.name == name) { found = true; break; }
            if (found) { d->recMissing.remove(name); continue; }
            if (++d->recMissing[name] >= 2) {
                qCWarning(lcApp).noquote() << "Rögzített eszköz leválasztva, a sávja lezárul:" << name;
                d->recMissing.remove(name);
                d->session->closeTrack(name);
            } else {
                recheck = true;
            }
        }
        // A re-plugged device: one that the detach closed (not switched off by the user) and that is present again
        // in the enumeration TWICE in a row (looks stable), is recorded again by itself: a new span starts, the
        // recording so far stays untouched. The user rightly expects not to have to switch it back on by hand
        // after a short USB dropout.
        const QStringList closedNow = d->recClosed;
        for (const QString& name : closedNow) {
            const AudioDeviceInfo* dev = nullptr;
            for (const AudioDeviceInfo& cand : present)
                if (cand.name == name) { dev = &cand; break; }
            if (!dev) { d->recReturning.remove(name); continue; }
            if (++d->recReturning[name] < 2) { recheck = true; continue; }
            d->recReturning.remove(name);
            qCInfo(lcApp).noquote() << "Leválasztott eszköz visszatért, a rögzítése folytatódik (új szakasz):" << name;
            emit recordingDeviceReturned(name);
            if (!d->session->addDevice(*dev))
                qCWarning(lcApp).noquote() << "A visszatért eszköz sávja nem indult el:" << name;
        }
        if (recheck)
            QTimer::singleShot(1200, this, [this] {
                if (d->state == RecordingState::Recording) d->devices->refresh();
            });
    }
    if (d->monitorWanted && d->monitor && d->monitor->active())
        restartLevelMonitor(/*force*/ false);
}

QStringList AppController::recordingDeviceNames() const {
    QStringList out;
    for (const QString& n : d->recNames)
        if (!d->recClosed.contains(n) && !d->recOff.contains(n) && !out.contains(n)) out << n;
    return out;
}

QStringList AppController::disconnectedRecordingDeviceNames() const { return d->recClosed; }

bool AppController::addRecordingDevice(const AudioDeviceInfo& device) {
    if (d->state != RecordingState::Recording || !d->session) return false;
    return d->session->addDevice(device);
}

bool AppController::stopRecordingDevice(const QString& deviceName) {
    if (d->state != RecordingState::Recording || !d->session) return false;
    const QStringList open = recordingDeviceNames();
    if (!open.contains(deviceName) || open.size() < 2) return false;   // the last open track stays
    if (!d->recOff.contains(deviceName)) d->recOff << deviceName;
    d->session->closeTrack(deviceName);   // → trackClosed → recordingTrackClosed
    return true;
}

void AppController::setSecret(const QString& name, const QString& value) { d->keyStore.set(name, value); }
bool AppController::hasSecret(const QString& name) const { return !d->keyStore.get(name).isEmpty(); }
QString AppController::secret(const QString& name) const { return d->keyStore.get(name); }

void AppController::startRecording(const QString& title, const QVector<AudioDeviceInfo>& devices)
{
    if (d->state == RecordingState::Recording || d->state == RecordingState::Stopping) {
        emit errorOccurred(tr("Már folyik felvétel."));
        return;
    }
    // The monitor releases the devices before recording (the request, monitorWanted, stays: during recording it
    // restarts on the devices not on a track). The shutdown runs asynchronously on the monitor's background
    // thread: for a short time a device may be open in parallel with the recording (in shared mode that is no obstacle).
    d->monitorNames.clear();
    if (d->monitor) d->monitor->stop();
    d->recNames.clear();
    d->recClosed.clear();
    d->recOff.clear();
    d->recMissing.clear();
    d->recReturning.clear();
    QVector<AudioDeviceInfo> use = devices;
    if (use.isEmpty()) {
        d->devices->refresh();
        use = d->devices->autoRecordDevices();   // line-in/AUX left out of the auto set
    }
    if (use.isEmpty()) {
        emit errorOccurred(tr("Nincs felvehető hangeszköz."));
        return;
    }

    // We remember the devices used (to pre-check at startup).
    QStringList usedNames;
    for (const auto& dvc : use) usedNames << dvc.name;
    setLastUsedDeviceNames(usedNames);

    d->recordingTagIds.clear();   // the previous recording's tags are not inherited
    const AppSettings s = d->settings->settings();
    auto* sess = new recording::RecordingSession(d->audioDir, title, s.userSpeakerName,
                                      opusBitrateKbps(s.audioQuality), this);
    if (d->recordingEngineFactory) sess->setEngineFactory(d->recordingEngineFactory);
    d->session = sess;

    connect(sess, &recording::RecordingSession::stateChanged, this, [this, sess](RecordingState st) {
        d->state = st;
        // The meeting folder already exists in start(), so it is available at the START of the recording (not
        // only at finished). Needed for the --record lock and currentMeetingFolder().
        if (st == RecordingState::Recording) {
            d->currentFolder = sess->folder();
            d->recNames.clear();
            for (const AudioDeviceInfo& dev : sess->trackDevices()) d->recNames << dev.name;
            startCallEndMonitor();
            restartLevelMonitor(/*force*/ true);   // only if the UI requested it (monitorWanted)
        } else if (st == RecordingState::Stopping) {
            // During the shutdown the monitor also stops; at the recording's end (if needed) it restarts.
            d->monitorNames.clear();
            if (d->monitor) d->monitor->stop();
        } else if (st == RecordingState::Idle) {
            stopCallEndMonitor();
        }
        emit recordingStateChanged(st);
    });
    connect(sess, &recording::RecordingSession::trackLevel, this, [this](int idx, float rms, float peak) {
        if (idx >= 0 && idx < d->recNames.size())
            emit deviceLevelPeak(d->recNames.at(idx), rms, peak);
    });
    connect(sess, &recording::RecordingSession::trackAdded, this, [this](int, const QString& name) {
        d->recNames << name;
        d->recClosed.removeAll(name);   // a re-plugged, re-recorded device
        d->recOff.removeAll(name);      // a re-enabled device: a new span
        if (!d->lastDevices.contains(name)) {
            QStringList names = d->lastDevices;
            names << name;
            setLastUsedDeviceNames(names);
        }
        restartLevelMonitor(/*force*/ false);
        emit recordingTrackAdded(name);
    });
    connect(sess, &recording::RecordingSession::trackClosed, this, [this](int, const QString& name) {
        // Switched off by the user, so not "detached"; the monitor measures its level again.
        if (!d->recOff.contains(name) && !d->recClosed.contains(name)) d->recClosed << name;
        restartLevelMonitor(/*force*/ false);
        emit recordingTrackClosed(name);
    });
    // Silence watch: if any track's RMS at speech level is "loud", the timestamp refreshes and the silence
    // question re-arms. The threshold is in line with RecordingSession's kSilencePeak.
    connect(sess, &recording::RecordingSession::levelMeterUpdated, this, [this](int, float rms) {
        if (rms >= 0.008f) {
            d->lastLoudMs = QDateTime::currentMSecsSinceEpoch();
            d->silenceAsked = false;
        }
    });
    {
        const int minutes = d->settings->settings().silenceAskMinutes;
        if (d->silenceTimer) { d->silenceTimer->stop(); d->silenceTimer->deleteLater(); d->silenceTimer = nullptr; }
        if (minutes > 0) {
            d->lastLoudMs = QDateTime::currentMSecsSinceEpoch();
            d->silenceAsked = false;
            d->silenceTimer = new QTimer(this);
            connect(d->silenceTimer, &QTimer::timeout, this, [this, minutes]() {
                if (d->state != RecordingState::Recording || d->silenceAsked) return;
                const qint64 quiet = QDateTime::currentMSecsSinceEpoch() - d->lastLoudMs;
                if (quiet >= qint64(minutes) * 60000) {
                    d->silenceAsked = true;
                    qCInfo(lcApp) << "Csend felvétel közben:" << minutes << "perc → rákérdezés";
                    emit silenceDetected(minutes);
                }
            });
            d->silenceTimer->start(10000);
        }
    }
    connect(sess, &recording::RecordingSession::elapsedChanged, this, &AppController::elapsedChanged);
    connect(sess, &recording::RecordingSession::failed, this, [this](QString e) {
        stopCallEndMonitor();
        emit errorOccurred(e);
        if (d->session) { d->session->deleteLater(); d->session = nullptr; }
        d->state = RecordingState::Idle;
        d->recNames.clear();
        d->recClosed.clear();
        d->recOff.clear();
        emit recordingStateChanged(d->state);
        restartLevelMonitor(/*force*/ true);
    });
    connect(sess, &recording::RecordingSession::finished, this, [this](Meeting m) {
        stopCallEndMonitor();
        d->currentFolder = m.folder;
        // The tags given during recording (the meeting enters the store only now).
        for (const QString& id : std::as_const(d->recordingTagIds))
            if (d->tags->tag(id).isValid() && !m.tagIds.contains(id)) m.tagIds << id;
        d->recordingTagIds.clear();
        d->store->saveMeeting(m);
        if (d->session) { d->session->deleteLater(); d->session = nullptr; }
        d->state = RecordingState::Idle;
        d->recNames.clear();
        d->recClosed.clear();
        d->recOff.clear();
        emit recordingFinished(m);
        emit recordingStateChanged(d->state);
        restartLevelMonitor(/*force*/ true);
        // The mixdown is decoupled from stop(): we start it here ASYNCHRONOUSLY, only if the setting is "auto".
        // It does not block, so a new recording can start at once. In manual mode the user starts it with the
        // review panel's "Mix" button. (The mixdown is needed only for listening; for transcription the per-track
        // .ogg files suffice.)
        // In the standalone recorder process (setAutoMixdownAfterRecording(false)) it does not start: that process
        // exits after recording, and the analyzer makes the mix when needed.
        // Before it, the tracks' speech check (on a background thread): a speechless track is left out of the mix
        // (Track::excludedReason "noSpeech"); the mix starts at the end of the measurement.
        if (d->autoMixdown) {
            const bool mix = d->settings->settings().mixdownMode != QStringLiteral("manual")
                && m.mixdownFile.isEmpty();
            if (editing::trackspeech::needsSpeechCheck(m)) startSpeechCheck(m.id, mix);
            else if (mix) regenerateMixdown(m.id);
            // The pre-transcript voice analysis from the raw tracks, right after the recording (if the speech
            // measurement is still pending, its end starts it).
            maybeAnalyzeParticipants(m.id);
        }
    });

    sess->start(use);
}

// ---- call-end watching during recording ---------------------------------------------
// The same detector core the tanara-watcher uses. The detector excludes our own capture by selfBinary.
// It only SIGNALS (callEnded); the user decides about stopping.
void AppController::startCallEndMonitor()
{
    stopCallEndMonitor();
    const AppSettings s = d->settings->settings();
    if (!s.askStopOnCallEnd)
        return;
    watcher::registerBuiltinDetectors();
    d->callDetector.reset(s.detectorId.isEmpty()
        ? watcher::MeetingDetectorRegistry::instance().createBest()
        : watcher::MeetingDetectorRegistry::instance().create(s.detectorId));
    if (!d->callDetector) {
        qCDebug(lcApp) << "Hívás-vég figyelés: nincs elérhető detektor ezen a platformon.";
        return;
    }
    d->callDetector->configure(s.knownCallApps, QStringLiteral("tanara"));
    d->callSeenActive = false;
    d->callInactivePolls = 0;
    d->callAppName.clear();
    d->callTimer = new QTimer(this);
    connect(d->callTimer, &QTimer::timeout, this, &AppController::pollCallEnd);
    d->callTimer->start(qMax(1, s.detectorIntervalSec) * 1000);
    pollCallEnd();
}

void AppController::stopCallEndMonitor()
{
    if (d->silenceTimer) { d->silenceTimer->stop(); d->silenceTimer->deleteLater(); d->silenceTimer = nullptr; }
    if (d->callTimer) { d->callTimer->stop(); d->callTimer->deleteLater(); d->callTimer = nullptr; }
    d->callDetector.reset();
    d->callSeenActive = false;
    d->callInactivePolls = 0;
}

void AppController::pollCallEnd()
{
    if (!d->callDetector || d->state != RecordingState::Recording)
        return;
    const watcher::MeetingSignal sig = d->callDetector->poll();
    if (sig.active) {
        d->callSeenActive = true;
        d->callInactivePolls = 0;
        if (!sig.appName.isEmpty()) d->callAppName = sig.appName;
        return;
    }
    if (!d->callSeenActive)
        return;   // we have not even seen a call yet (a manual recording without a call): nothing to signal
    // Debounce: 2 consecutive inactive polls (a momentary microphone release must not ask).
    if (++d->callInactivePolls >= 2) {
        const QString app = d->callAppName;
        d->callSeenActive = false;   // it re-arms if the call becomes active again later
        d->callInactivePolls = 0;
        qCInfo(lcApp).noquote() << "Hívás véget ért felvétel közben:" << app;
        emit callEnded(app);
    }
}

void AppController::stopRecording()
{
    if (d->session) d->session->stop();
}

static QString resolvedPrompt(const AppSettings& s, const QString& userOverride, const char* id);
static processing::SummaryRequest summaryRequestFor(const AppSettings& s, const MergedTranscript& merged,
                                        const Meeting& m, const QString& model, int maxTokens);

providers::ReadinessResult AppController::canRun(providers::WorkflowStep step, const QString& meetingId) const
{
    const Meeting meeting = d->store->load(meetingId);
    providers::ReadinessModel model(d->settings->settings(),
                         [this](const QString& k) { return !d->keyStore.get(k).isEmpty(); });
    providers::ReadinessResult r = model.check(step, meeting);
    if (!r.runnable || !usesCloud(step))
        return r;

    // Tanara Cloud-specific obstacles (exactly one CTA in the task strip).
    if (d->cloud->clientTooOld()) {
        providers::ReadinessResult c;
        c.blockerKind   = providers::BlockerKind::Cloud;
        c.detail        = tr("A Tanara Cloudhoz frissítés kell (legalább %1).").arg(d->cloud->minClient());
        c.fixActionHint = QStringLiteral("cloud:update");
        c.providerId    = cloud::ProviderId;
        return c;
    }
    const cloud::AccountInfo acc = d->cloud->account();
    if (acc.valid && acc.balanceEmpty) {
        providers::ReadinessResult c;
        c.blockerKind   = providers::BlockerKind::Cloud;
        c.detail        = tr("töltsd fel az egyenleged");
        c.fixActionHint = QStringLiteral("cloud:topup");
        c.providerId    = cloud::ProviderId;
        return c;
    }
    return r;
}

// ---- Tanara Cloud ------------------------------------------------------------------------

cloud::CloudAccount* AppController::cloud() const { return d->cloud; }
bool AppController::cloudLive() const { return d->cloudLive; }
bool AppController::cloudTeaser() const { return d->cloudTeaser; }

bool AppController::usesCloud(providers::WorkflowStep step) const
{
    if (!d->cloudLive) return false;
    const AppSettings s = d->settings->settings();
    if (step == providers::WorkflowStep::Transcribe) return s.sttProviderId == cloud::ProviderId;
    if (step == providers::WorkflowStep::Summarize)  return s.llmProviderId == cloud::ProviderId;
    return false;
}

QString AppController::cloudModelFor(providers::WorkflowStep step) const
{
    const AppSettings s = d->settings->settings();
    const bool stt = step == providers::WorkflowStep::Transcribe;
    const QString expert = stt ? s.cloudSttModel : s.cloudLlmModel;
    // The Expert model is valid only if it is still in the catalog (a retired model goes back to the tier's
    // virtual model; the UI signals it).
    if (!expert.isEmpty()) {
        const QVector<cloud::CloudModel> cat = d->cloud->models();
        if (cat.isEmpty() || findModel(cat, expert).has_value())
            return expert;
    }
    return cloud::virtualModelId(stt ? QStringLiteral("stt") : QStringLiteral("llm"),
                          stt ? s.cloudSttTier : s.cloudLlmTier);
}

namespace {
// max_tokens of cloud LLM calls: a moderate value because of the coverage rule (priced at prompt +
// max_tokens); the JSON summary and the topic analysis fit well within it.
constexpr int kCloudMaxTokens = 4000;
constexpr int kReduceInputChars = 6000;   // the reduce's input: a digest of the topic analyses (estimate)
constexpr int kNotesCharsPerPart = 3000;  // length of one part note in the merge's input (estimate)
}

cloud::EstimateRequest AppController::makeEstimateRequest(const QString& meetingId, const QString& task,
                                                   const QString& summaryMode) const
{
    const Meeting m = d->store->load(meetingId);
    const AppSettings s = d->settings->settings();
    cloud::EstimateRequest r;
    r.task = task;
    r.durationMs = m.durationMs;
    for (const Track& t : m.tracks) if (t.active) ++r.tracks;
    r.language = s.languageHints.value(0);
    if (task != QLatin1String("summarize"))
        r.sttModel = cloudModelFor(providers::WorkflowStep::Transcribe);
    if (task != QLatin1String("transcribe")) {
        r.llmModel = cloudModelFor(providers::WorkflowStep::Summarize);
        r.summaryMode = summaryMode.isEmpty() ? QStringLiteral("quick") : summaryMode;
        // The actual input: system prompt + context + transcript (markdown).
        MergedTranscript merged = d->mergedCache.value(meetingId);
        if (merged.tokens.isEmpty() && !m.folder.isEmpty())
            merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
        if (!merged.tokens.isEmpty()) {
            editing::speakeredit::applyResolvedSpeakers(merged, m);
            const int transcript = merged.renderMarkdown().size() + m.contextNote.size();
            r.transcriptChars = transcript;
            if (r.summaryMode == QLatin1String("complex")) {
                const int topicIn    = transcript + resolvedPrompt(s, s.topicExtractionPrompt, "topic").size();
                const int analysisIn = transcript + resolvedPrompt(s, s.topicAnalysisPrompt, "analysis").size() + 200;
                // The topic count is unknown before extraction (null -> the gateway's statistics); if an edited topic
                // list already exists, we request only the not-yet-analyzed ones.
                const QVector<SummaryTopic> topics =
                    readTopicsJson(QDir(m.folder).filePath(QStringLiteral("summary.topics.json")));
                if (topics.isEmpty()) {
                    r.llmCalls.append({ 1, topicIn, kCloudMaxTokens });
                    r.llmCalls.append({ -1, analysisIn, kCloudMaxTokens });
                } else {
                    QSet<QString> done;
                    for (const TopicAnalysis& a : readAnalysesJson(QDir(m.folder).filePath(QStringLiteral("summary.analyses.json"))))
                        done.insert(a.topicId);
                    int missing = 0;
                    for (const SummaryTopic& t : topics) if (!done.contains(t.id)) ++missing;
                    if (missing > 0) r.llmCalls.append({ missing, analysisIn, kCloudMaxTokens });
                }
                r.llmCalls.append({ 1, kReduceInputChars, kCloudMaxTokens });
            } else {
                // Quick summary: 1 part -> one call; n parts -> the note calls of the parts without a finished note + one
                // merge call (see SummaryService::plan).
                const processing::SummaryRequest req = summaryRequestFor(s, merged, m, r.llmModel, kCloudMaxTokens);
                const processing::SummaryPlan plan = processing::SummaryService::plan(req);
                const int ctx = int(m.contextNote.size()) + 200;
                if (plan.singleCall) {
                    r.llmCalls.append({ 1, transcript + int(req.singlePrompt.size()), kCloudMaxTokens });
                } else if (plan.parts > 0) {
                    const int missing = plan.parts - plan.cachedParts;
                    if (missing > 0) {
                        qint64 total = 0;
                        for (int c : plan.partChars) total += c;
                        const int perPart = int((total + plan.parts - 1) / plan.parts);
                        r.llmCalls.append({ missing, perPart + int(req.notesPrompt.size()) + ctx,
                                            req.notesMaxTokens });
                    }
                    r.llmCalls.append({ 1, int(req.mergePrompt.size()) + ctx + plan.parts * kNotesCharsPerPart,
                                        kCloudMaxTokens });
                }
            }
        }
    }
    return r;
}

AppController::CloudRunPtr AppController::newCloudRun(const QString& meetingId, const QString& kind) const
{
    auto run = std::make_shared<CloudRun>();
    run->meetingId = meetingId;
    run->kind = kind;
    run->jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return run;
}

ProviderConfig AppController::cloudConfig(providers::WorkflowStep step, const CloudRunPtr& run,
                                          const QString& summaryMode) const
{
    ProviderConfig cfg;
    cfg.type = cloud::ProviderId;
    cfg.baseUrl = d->cloud->apiBase();
    cfg.apiKey = d->cloud->apiKey();
    cfg.model = cloudModelFor(step);
    cfg.temperature = 0.2;
    cfg.maxTokens = kCloudMaxTokens;
    cfg.extraHeaders = d->cloud->requestHeaders(run ? run->jobId : QString(),
                                                step == providers::WorkflowStep::Summarize ? summaryMode : QString());
    QPointer<cloud::CloudAccount> acc = d->cloud;
    cfg.onExchange = [acc, run](const cloud::HttpExchange& ex) {
        if (acc) acc->observeExchange(ex);
        if (!run) return;
        const QString rid = QString::fromUtf8(ex.headers.value(QByteArrayLiteral("x-tanara-request-id")));
        if (!rid.isEmpty()) run->lastRequestId = rid;
        const bool ok = ex.status >= 200 && ex.status < 300;
        if (!ok) {
            if (ex.method != "DELETE")   // the failure of cleanup DELETEs must not overwrite the real error
                run->lastError = cloud::parseCloudError(ex.status, ex.headers, ex.body, ex.networkError);
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(ex.body).object();
        static const QRegularExpression statusPath(QStringLiteral("/transcriptions/[^/]+$"));
        if (ex.method == "POST" && ex.path.endsWith(QLatin1String("/files"))) {
            run->fileId = o.value(QStringLiteral("id")).toString();
        } else if (ex.method == "POST" && ex.path.endsWith(QLatin1String("/transcriptions"))) {
            run->transcriptionId = o.value(QStringLiteral("id")).toString();
            cloud::ChargeInfo c = cloud::chargeInfoFromJson(o.value(QStringLiteral("tanara")).toObject());
            if (!c.valid) c = cloud::chargeInfoFromHeaders(ex.headers);
            run->addCharge(c);
            // The id of the transcription in progress (for the startup write-back check).
            if (acc) acc->addPendingTranscription(run->transcriptionId, run->fileId);
        } else if (ex.method == "POST" && ex.path.endsWith(QLatin1String("/chat/completions"))) {
            cloud::ChargeInfo c = cloud::chargeInfoFromJson(o.value(QStringLiteral("usage")).toObject()
                                                  .value(QStringLiteral("tanara_charge")).toObject());
            if (!c.valid) c = cloud::chargeInfoFromHeaders(ex.headers);
            run->addCharge(c);
        } else if (ex.method == "GET" && statusPath.match(ex.path).hasMatch()) {
            // The write-back shows in the response that first gives status: error; the provider sends the DELETE
            // ONLY after that (we read out the tanara field first).
            const cloud::TranscriptionJobInfo j = cloud::transcriptionJobInfoFromJson(o);
            if (j.refunded) {
                run->refunded = true;
                run->refund = j.refund;
                if (j.balance.isValid()) {
                    run->balance = j.balance;
                    if (acc) emit acc->balanceChanged(j.balance);
                }
            }
        }
    };
    return cfg;
}

ProviderConfig AppController::llmConfigFor(CloudRunPtr& run, const QString& meetingId,
                                           const QString& kind, const QString& summaryMode) const
{
    if (!usesCloud(providers::WorkflowStep::Summarize)) {
        ProviderConfig cfg = d->settings->settings().llmSelected();
        cfg.apiKey = d->keyStore.get(keys::LlmApiKey);   // LM Studio: may be empty
        return cfg;
    }
    if (!run) run = newCloudRun(meetingId, kind);
    return cloudConfig(providers::WorkflowStep::Summarize, run, summaryMode);
}

void AppController::finishCloudRun(const CloudRunPtr& run)
{
    if (!run) return;
    if (!run->transcriptionId.isEmpty())
        d->cloud->removePendingTranscription(run->transcriptionId);
    if (run->calls > 0)
        emit cloudCharged(run->meetingId, run->kind, run->charged(), run->calls, run->balance, run->vatMode);
    if (run->balance.isValid() || run->calls > 0)
        d->cloud->refreshAccount();   // refreshing low_balance / hours_left / notices
}

void AppController::failCloudRun(const CloudRunPtr& run, const QString& fallbackMessage)
{
    if (run && !run->transcriptionId.isEmpty())
        d->cloud->removePendingTranscription(run->transcriptionId);
    if (run && run->refunded) {
        emit cloudRefunded(run->meetingId, run->refund, run->balance, run->lastRequestId);
        d->cloud->refreshAccount();
        return;
    }
    if (run && run->lastError.isError()) {
        if (run->lastError.kind == cloud::CloudErrorKind::ClientTooOld)
            emit d->cloud->clientTooOldDetected(run->lastError.minClient);
        emit cloudError(run->meetingId, run->kind, run->lastError, run->charged());
        if (run->calls > 0) d->cloud->refreshAccount();
        return;
    }
    emit errorOccurred(fallbackMessage);
}

void AppController::transcribeMeeting(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::Transcribe))
        return;   // already running on this meeting (a double click / two views)

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) {
        d->clearSpeakersOnTranscript.remove(meetingId);
        emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId));
        return;
    }
    // The voice analysis runs in parallel with transcription (if it has not run on these tracks yet).
    maybeAnalyzeParticipants(meetingId);

    providers::ReadinessResult res = canRun(providers::WorkflowStep::Transcribe, meetingId);
    if (!res.runnable) {
        d->clearSpeakersOnTranscript.remove(meetingId);
        emit errorOccurred(res.detail);
        return;
    }

    // A standalone identification refers to the labels of the CURRENT transcript: we stop it so its names
    // do not land on the NEW transcript about to arrive (its result is dropped at completion by the
    // transcript-change check too; see startIdentify).
    cancelJob(meetingId, JobKind::Identify);

    // The transcript is made from the MIXDOWN (a single audio stream: no per-track overlap
    // merging/duplication, 1x Soniox cost instead of ~Nx). If the mixdown is missing or stale, we make it first
    // and start the transcription on the mixdownUpdated signal.
    const QString mixPath =
        QDir(m.folder).filePath(m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3")
                                                        : m.mixdownFile);
    const bool needMix = m.mixdownFile.isEmpty() || m.mixdownDirty || !QFile::exists(mixPath);

    // Structured task: the stage list. The mixdown is a stage only if needed; identification only if there
    // is a voice model.
    {
        int activeTracks = 0;
        for (const Track& t : m.tracks) if (t.active) ++activeTracks;
        QVector<JobStage> stages;
        if (needMix)
            stages.append({QStringLiteral("mixdown"), tr("Lekeverés"), StageState::Waiting, -1, QString()});
        stages.append({QStringLiteral("upload"), tr("Feltöltés"), StageState::Waiting, -1,
                       tr("%1 perc, %2 sáv").arg(qMax<qint64>(1, (m.durationMs + 30000) / 60000))
                                            .arg(activeTracks)});
        stages.append({QStringLiteral("transcribe"), tr("Átírás"), StageState::Waiting, -1, QString()});
        stages.append({QStringLiteral("diarize"), tr("Beszélők szétválasztása"), StageState::Waiting, -1, QString()});
        if (d->voiceModelUsable())
            stages.append({QStringLiteral("identify"), tr("Résztvevők azonosítása"),
                           d->skipIdentifyAfterTranscribe.contains(meetingId) ? StageState::Skipped
                                                                              : StageState::Waiting,
                           -1, QString()});
        d->jobs->begin(meetingId, JobKind::Transcribe, tr("Átírás folyamatban"), stages);
    }
    d->transcribeRuns.insert(meetingId, Impl::TranscribeRun{});

    if (needMix) {
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(this, &AppController::mixdownUpdated, this,
            [this, meetingId, conn](const QString& id, bool ok) {
                if (id != meetingId) return;
                QObject::disconnect(*conn);
                auto it = d->transcribeRuns.find(meetingId);
                if (it == d->transcribeRuns.end()) return;   // cancelled meanwhile
                it->mixPhase = false;
                it->mixConn.reset();
                if (ok) {
                    d->jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("mixdown"),
                                      StageState::Done);
                    transcribeFromMixdown(meetingId);
                    return;
                }
                d->transcribeRuns.erase(it);
                d->clearSpeakersOnTranscript.remove(meetingId);
                const QString msg = tr("A lekeverés sikertelen — az átírás nem indult.");
                JobError je;
                je.message = msg;
                je.detail = d->mixdownFailReason.take(meetingId);   // e.g. which track's file is missing
                d->jobs->fail(meetingId, JobKind::Transcribe, je);
                emit errorOccurred(msg);
            });
        Impl::TranscribeRun& tr_ = d->transcribeRuns[meetingId];
        tr_.mixPhase = true;
        tr_.mixConn = conn;
        // If a standalone mix was already running, we wait for it; on cancel we do not stop that one.
        tr_.ownsMixdown = !d->mixdownProcs.contains(meetingId);
        d->jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("mixdown"),
                          StageState::Running, 0);
        emit jobProgress(m.id, tr("Lekeverés az átíráshoz…"));
        regenerateMixdown(meetingId);
        return;
    }
    transcribeFromMixdown(meetingId);
}

void AppController::retranscribeMeeting(const QString& meetingId, bool keepBackup)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    if (d->jobs->isRunning(meetingId, JobKind::Transcribe))
        return;   // a transcription is already running on this meeting
    // The current transcript (with the names and manual corrections) stays as a copy, if requested.
    if (keepBackup) editing::speakeredit::backupTranscript(m);
    // Both the speaker assignments (speakerMap) AND the manual line corrections (overlay) are cleared when the
    // NEW transcript arrives, not beforehand: the speakerMap in the transcription's finished branch, the
    // overlay in the transcriptReady hook. If the re-transcription is cancelled or fails, the old transcript
    // stays untouched with its names and per-line corrections.
    d->clearSpeakersOnTranscript.insert(meetingId);
    transcribeMeeting(meetingId);
}

void AppController::transcribeFromMixdown(const QString& meetingId)
{
    // Closing the task with an error before the provider call (an internal helper).
    auto abortJob = [this, meetingId](const QString& msg) {
        d->transcribeRuns.remove(meetingId);
        d->clearSpeakersOnTranscript.remove(meetingId);
        JobError je;
        je.message = msg;
        d->jobs->fail(meetingId, JobKind::Transcribe, je);
        emit errorOccurred(msg);
    };

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) {
        d->transcribeRuns.remove(meetingId);
        d->clearSpeakersOnTranscript.remove(meetingId);
        d->jobs->cancelled(meetingId, JobKind::Transcribe);
        return;
    }

    const AppSettings s = d->settings->settings();
    const QString sttId = s.sttProviderId;
    ProviderConfig cfg = s.sttSelected();
    // Tanara Cloud: the same Soniox provider with the gateway config; the run (job id, charge,
    // write-back, error) is filled from the response hook.
    CloudRunPtr run;
    bool cloudDiarization = true;
    if (usesCloud(providers::WorkflowStep::Transcribe)) {
        run = newCloudRun(meetingId, QStringLiteral("transcribe"));
        cfg = cloudConfig(providers::WorkflowStep::Transcribe, run, QString());
        const std::optional<cloud::CloudModel> cm = findModel(d->cloud->models(), cfg.model);
        cloudDiarization = !cm.has_value() || cm->diarization;
    } else {
        // The API key's slot is provider-dependent: the descriptor's secretKey tells it (soniox.apiKey /
        // stt.whisper.apiKey / ...); we do not hardcode it to Soniox.
        const providers::ProviderDescriptor sttDesc = providers::SttProviderRegistry::instance().descriptor(sttId);
        for (const providers::ConfigField& f : sttDesc.fields)
            if (f.isSecret && !f.secretKey.isEmpty()) {
                cfg.apiKey = d->keyStore.get(f.secretKey);
                break;
            }
    }
    // Remembering the last failed HTTP exchange (in own-key mode too): this becomes the remembered error's
    // technical line ("HTTP 401 | invalid_api_key").
    const auto sink = std::make_shared<FailureSink>();
    captureFailures(cfg, sink);

    providers::ISttProvider* provider = providers::SttProviderRegistry::instance().create(sttId, cfg, this);
    if (!provider) {
        abortJob(tr("Ismeretlen STT-provider: %1.").arg(sttId));
        return;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    emit jobProgress(m.id, tr("Átírás indítása…"));

    const QString mixPath =
        QDir(m.folder).filePath(m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3")
                                                        : m.mixdownFile);

    // A SINGLE request with the mixdown. Speaker separation comes from Soniox diarization ("Speaker N"
    // labels); the NAME is resolved afterwards by voice ID / manual rename.
    providers::SttRequest req;
    req.audioFilePath = mixPath;
    req.trackId = QStringLiteral("mixdown");
    req.languageHints = s.languageHints;
    // Context envelope -> Soniox's structured "context" object (general/text/terms): the title + the user's
    // note (names, jargon, mishearing corrections) + the participant names. The detected call app is NOT sent
    // (it tells the transcriber nothing).
    library::meetingnotes::fillSttContext(req, m);
    req.diarization = true;
    if (run && !cloudDiarization) {
        // The chosen cloud tier does not diarize (e.g. Fast): everyone becomes "Speaker 1" (the recording's tracks
        // stay; the pre-run warning covered this).
        req.diarization = false;
        req.speakerLabel = QStringLiteral("1");
    }
    const bool diarizing = req.diarization;

    providers::SttJob* job = provider->transcribe(req);

    // Structured state: the job for cancelling, the earlier runs' speed for the estimate (with the same
    // provider/model); without a sample there is no estimate.
    const QString statsKey = QStringLiteral("transcribe/%1/%2").arg(sttId, cfg.model);
    {
        Impl::TranscribeRun& tr_ = d->transcribeRuns[meetingId];
        tr_.job = job;
        tr_.mixPhase = false;
        const int est = d->jobStats->estimateSec(statsKey, double(m.durationMs) / 1000.0);
        if (est >= 0) {
            const JobProgress jp = d->jobs->job(meetingId, JobKind::Transcribe);
            const int elapsed = jp.startedAt.isValid()
                ? int(jp.startedAt.secsTo(QDateTime::currentDateTime())) : 0;
            d->jobs->setEstimate(meetingId, JobKind::Transcribe, elapsed + est);
        }
        if (!diarizing)
            d->jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("diarize"), StageState::Skipped);
        d->jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("upload"), StageState::Running, 0);
    }
    auto sttClock = std::make_shared<QElapsedTimer>();
    sttClock->start();

    connect(job, &providers::SttJob::uploadProgress, this, [this, meetingId](qint64 sent, qint64 total) {
        if (total <= 0) return;
        // Real byte ratio. After 100% the provider's state change closes the stage.
        const JobProgress jp = d->jobs->job(meetingId, JobKind::Transcribe);
        const JobStage* st = jp.stage(QStringLiteral("upload"));
        if (st && st->state == StageState::Running)
            d->jobs->setStagePercent(meetingId, JobKind::Transcribe, QStringLiteral("upload"),
                                     int(sent * 100 / total));
    });
    connect(job, &providers::SttJob::stateChanged, this, [this, id = m.id, diarizing](SttJobState st) {
        emit jobProgress(id, sttPhase(st));
        // During transcription the providers give NO percentage, only a state (upload / queued / processing).
        // The provider does speaker separation in the same pass as transcription, so the two stages run and
        // close together.
        const JobKind k = JobKind::Transcribe;
        if (st == SttJobState::Uploading) {
            d->jobs->setStage(id, k, QStringLiteral("upload"), StageState::Running, 0);
        } else if (st == SttJobState::Queued || st == SttJobState::Processing) {
            d->jobs->setStage(id, k, QStringLiteral("upload"), StageState::Done);
            d->jobs->setStage(id, k, QStringLiteral("transcribe"), StageState::Running);
            d->jobs->setStageDetail(id, k, QStringLiteral("transcribe"),
                                    st == SttJobState::Queued ? tr("sorban áll") : QString());
            if (diarizing)
                d->jobs->setStage(id, k, QStringLiteral("diarize"), StageState::Running);
        }
    });
    // The detailed poll message (elapsed time + heartbeat) should also reach the UI, so that during a long
    // async processing it shows it is running and the connection is alive.
    connect(job, &providers::SttJob::progress, this, [this, id = m.id](int, const QString& msg) {
        emit jobProgress(id, msg);
        d->jobs->setMessage(id, JobKind::Transcribe, msg);
    });
    connect(job, &providers::SttJob::finished, this,
            [this, meetingId, providerObj, run, job, statsKey, sttClock, diarizing](const TrackTranscript& result) {
        d->transcribeRuns.remove(meetingId);
        if (providerObj) providerObj->deleteLater();
        // The job still sends its best-effort cleanup DELETEs; after that it can be deleted.
        QTimer::singleShot(30000, job, &QObject::deleteLater);

        // A FRESH meeting: transcription may run for minutes, during which the user may have renamed or edited
        // (or deleted) the meeting; saving the old instance would overwrite that.
        Meeting mm = d->store->load(meetingId);
        if (mm.id.isEmpty()) {
            d->clearSpeakersOnTranscript.remove(meetingId);
            d->jobs->cancelled(meetingId, JobKind::Transcribe);
            finishCloudRun(run);
            return;
        }

        // An EMPTY result (the provider returned not a single word) = an ERROR: we do not write it over the
        // existing transcript. The earlier transcript, speaker names and manual corrections are untouched.
        if (result.tokens.isEmpty()) {
            d->clearSpeakersOnTranscript.remove(meetingId);
            const QString msg = mm.hasTranscript
                ? tr("Az átírás üres eredményt adott (a szolgáltató nem talált beszédet a felvételen) — "
                     "a korábbi átirat megmaradt.")
                : tr("Az átírás üres eredményt adott: a szolgáltató nem talált beszédet a felvételen.");
            JobError je;
            je.message = msg;
            d->jobs->fail(meetingId, JobKind::Transcribe, je);
            finishCloudRun(run);   // the run closes (any charge shows up this way too)
            emit errorOccurred(msg);
            return;
        }
        // Whether there was a transcript BEFORE the run (independent of the flag; see below).
        const bool hadTranscript = mm.hasTranscript
            || QFile::exists(QDir(mm.folder).filePath(QStringLiteral("transcript.tokens.json")));

        TrackTranscript res = result;
        // We translate Soniox's diarization ids (1,2,...) into a neutral "Speaker N" label.
        for (TranscriptToken& tok : res.tokens)
            tok.speaker = tok.speaker.isEmpty()
                ? QStringLiteral("Beszélő")
                : QStringLiteral("Beszélő %1").arg(tok.speaker);

        QVector<TrackTranscript> single{res};
        MergedTranscript merged = processing::mergeTranscripts(single);
        const QString mdPath = QDir(mm.folder).filePath(QStringLiteral("transcript.md"));
        writeTextFile(mdPath, merged.renderMarkdown());
        writeTokensJson(QDir(mm.folder).filePath(QStringLiteral("transcript.tokens.json")), merged);
        writeSegmentsJson(QDir(mm.folder).filePath(QStringLiteral("transcript.segments.json")), merged.segments());
        d->mergedCache.insert(mm.id, merged);
        ++d->transcriptGen[meetingId];   // a new transcript: the running identifications' results are stale
        // Re-transcription: the new diarization's "Speaker N" labels may mean OTHER people, so the old
        // assignments are cleared together with the NEW transcript. NOT only on retranscribeMeeting's flag: that
        // is lost on failure, and a plain (retried) transcription after it would leave the old names on the new
        // labels. So whenever a transcript already existed, we always clear.
        const bool flagged = d->clearSpeakersOnTranscript.remove(meetingId);
        if (flagged || hadTranscript)
            mm.speakerMap.clear();
        mm.hasTranscript = true;
        d->store->saveMeeting(mm);

        d->jobStats->addSample(statsKey, double(sttClock->elapsed()) / 1000.0,
                               double(mm.durationMs) / 1000.0);
        const JobKind k = JobKind::Transcribe;
        d->jobs->setStage(meetingId, k, QStringLiteral("upload"), StageState::Done);
        d->jobs->setStage(meetingId, k, QStringLiteral("transcribe"), StageState::Done);
        if (diarizing)
            d->jobs->setStage(meetingId, k, QStringLiteral("diarize"), StageState::Done);
        d->jobs->markIdentified(meetingId, false);   // identification has not run on the new transcript yet

        emit transcriptReady(mm.id, mdPath);
        finishCloudRun(run);   // "This transcription cost $0.41." (cloud runs only)
        // Voice ID: auto-matching the diarized speakers against the voiceprint DB, on a background thread, as
        // the last stage of the transcription task (the UI stays usable, cancellable). (If the user skipped it
        // with the toggle, the transcript finishes with anonymous speakers.) The "Who was there?" decision
        // (approval / "only me") was made before the transcript: we apply that, and automatic identification is
        // skipped. Otherwise the candidates' binding + identification.
        const bool decided = applyParticipantsToTranscript(meetingId);
        if (decided && d->voiceModelUsable())
            d->jobs->setStage(meetingId, JobKind::Transcribe, QStringLiteral("identify"),
                              StageState::Skipped);
        if (decided || d->skipIdentifyAfterTranscribe.contains(meetingId)
            || !startIdentify(meetingId, /*asTranscribeStage*/ true))
            d->jobs->finish(meetingId, JobKind::Transcribe);
    });
    connect(job, &providers::SttJob::failed, this, [this, meetingId, providerObj, run, job, sink](QString e) {
        const Impl::TranscribeRun info = d->transcribeRuns.take(meetingId);
        d->clearSpeakersOnTranscript.remove(meetingId);
        if (providerObj) providerObj->deleteLater();
        QTimer::singleShot(30000, job, &QObject::deleteLater);   // after the cleanup DELETEs

        if (info.cancelRequested) {
            // Cancel: no error, no errorOccurred. At the provider the job cleaned up (the uploaded audio +
            // transcript deleted); the earlier transcript (if any) is untouched.
            if (run) {
                if (!run->transcriptionId.isEmpty())
                    d->cloud->removePendingTranscription(run->transcriptionId);
                if (run->calls > 0 || run->balance.isValid())
                    d->cloud->refreshAccount();
            }
            d->jobs->cancelled(meetingId, JobKind::Transcribe);
            return;
        }
        JobError je = (run && run->lastError.isError())
            ? library::describeCloudFailure(JobKind::Transcribe, run->lastError)
            : library::describeJobFailure(JobKind::Transcribe, e, sink->has ? &sink->ex : nullptr);
        d->jobs->fail(meetingId, JobKind::Transcribe, je);
        failCloudRun(run, tr("Átírás-hiba: %1").arg(e));
    });
}

// The system prompt actually to be sent: settings override -> file override
// (<metadataDir>/prompts/<id>.md) -> built-in default (PromptLibrary), finally with the target language
// applied ({{NYELV}} placeholder / directive).
static QString resolvedPrompt(const AppSettings& s, const QString& userOverride, const char* id)
{
    const QString base = userOverride.trimmed().isEmpty()
        ? processing::promptDefault(QLatin1String(id), s.metadataDir)
        : userOverride;
    return processing::applySummaryLanguage(base, s.summaryLanguage);
}

void AppController::summarizeMeeting(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::Summarize))
        return;   // already running

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    providers::ReadinessResult res = canRun(providers::WorkflowStep::Summarize, meetingId);
    if (!res.runnable) { emit errorOccurred(res.detail); return; }

    MergedTranscript merged = d->mergedCache.value(meetingId);
    if (merged.tokens.isEmpty())
        merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    // A content guard: canRun(Summarize) gates on the meeting.hasTranscript flag, but if tokens.json is
    // missing/empty (deleted by hand, a partial write, a wrongly copied meeting), no summary should start on
    // an empty transcript; this mirrors the original content-based guard.
    if (merged.tokens.isEmpty()) {
        emit errorOccurred(tr("Nincs átirat — előbb futtass átírást."));
        return;
    }
    editing::speakeredit::applyResolvedSpeakers(merged, m);   // Gemma should see the real names

    const AppSettings s = d->settings->settings();
    const QString llmId = s.llmProviderId;
    CloudRunPtr run;   // cloud: ONE run (one job id) for all the note and merge calls
    ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("summary"), QStringLiteral("quick"));
    const auto sink = std::make_shared<FailureSink>();
    captureFailures(cfg, sink);
    providers::ILlmProvider* provider = providers::LlmProviderRegistry::instance().create(llmId, cfg, this);
    if (!provider) {
        emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(llmId));
        return;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new processing::SummaryService(provider, this);
    emit jobProgress(meetingId, run ? tr("Összefoglalás a Tanara Cloudban…")
                                    : tr("Összefoglalás a helyi modellel…"));

    processing::SummaryRequest req = summaryRequestFor(s, merged, m, cfg.model, cfg.maxTokens);
    req.temperature = cfg.temperature;
    // For the source references: the utterances with their current (resolved) speaker; this becomes the
    // summary's speaker snapshot (the targeted staleness compares against it).
    req.sourceLines = editing::speakeredit::resolvedSourceLines(m);
    req.adaptToContext = !run;   // with Tanara Cloud the context is the gateway's business
    const processing::SummaryPlan plan = processing::SummaryService::plan(req);
    const auto ctx = std::make_shared<Impl::LlmContextState>();
    ctx->need = processing::SummaryService::contextNeed(req);

    // Structured task: for a short meeting one stage (one call), for a longer one "Notes per part" (k / n
    // parts) + "Summarizing". The estimate comes only from earlier runs measured with the same model (unit:
    // the transcript's length).
    const QString key = Impl::llmKey(meetingId, JobKind::Summarize);
    const QString statsKey = QStringLiteral("summary-notes/%1/%2").arg(llmId, cfg.model);
    const double units = double(merged.renderMarkdown().size());
    QVector<JobStage> stages;
    if (plan.singleCall) {
        stages.append({ QStringLiteral("single"), tr("Összefoglalás"), StageState::Waiting, -1, QString() });
    } else {
        // Parts finished in an earlier run are visible at startup (during the model load too).
        QString notesDetail = tr("%n rész", nullptr, plan.parts);
        if (plan.cachedParts > 0) notesDetail += tr(" · %n korábbi futásból", nullptr, plan.cachedParts);
        stages.append({ QStringLiteral("notes"), tr("Jegyzetelés részenként"), StageState::Waiting, -1,
                        notesDetail });
        stages.append({ QStringLiteral("merge"), tr("Összegzés"), StageState::Waiting, -1, QString() });
    }
    d->jobs->begin(meetingId, JobKind::Summarize, tr("Összefoglaló készítése"), stages);
    if (!plan.singleCall)
        d->jobs->setCounts(meetingId, JobKind::Summarize, plan.cachedParts, plan.parts);
    d->jobs->setEstimate(meetingId, JobKind::Summarize, d->jobStats->estimateSec(statsKey, units));
    d->llmRuns.insert(key, Impl::LlmRun{svc, providerObj, run});
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();

    // Context-adapted re-split: the stages switch to per-part notes.
    connect(svc, &processing::SummaryService::partsChanged, this, [this, meetingId](int parts, int cached) {
        if (!d->jobs->isRunning(meetingId, JobKind::Summarize)) return;
        QVector<JobStage> stages;
        if (const JobStage* model = d->jobs->job(meetingId, JobKind::Summarize).stage(QStringLiteral("model")))
            stages.append(*model);
        stages.append({ QStringLiteral("notes"), tr("Jegyzetelés részenként"), StageState::Waiting, -1,
                        tr("%n rész (a modell kontextusához igazítva)", nullptr, parts) });
        stages.append({ QStringLiteral("merge"), tr("Összegzés"), StageState::Waiting, -1, QString() });
        d->jobs->setStages(meetingId, JobKind::Summarize, stages);
        d->jobs->setCounts(meetingId, JobKind::Summarize, cached, parts);
    });
    connect(svc, &processing::SummaryService::contextLimitDetected, this, [this, cfg, ctx](int tokens) {
        if (!ctx->lmStudio) d->learnedContext.insert(Impl::serverKey(cfg), tokens);
    });
    connect(svc, &processing::SummaryService::progress, this,
            [this, meetingId, cached = plan.cachedParts](const QString& stage, int done, int total) {
        if (!d->jobs->isRunning(meetingId, JobKind::Summarize)) return;
        if (stage == QLatin1String("single")) {
            d->jobs->setStage(meetingId, JobKind::Summarize, stage, StageState::Running);
            return;
        }
        if (stage == QLatin1String("notes")) {
            QString detail = done < total ? tr("%1/%2. rész").arg(done + 1).arg(total)
                                          : tr("%n rész kész", nullptr, total);
            if (cached > 0) detail += tr(" · %n korábbi futásból", nullptr, cached);
            d->jobs->setStage(meetingId, JobKind::Summarize, stage,
                              done < total ? StageState::Running : StageState::Done, -1, detail);
            d->jobs->setCounts(meetingId, JobKind::Summarize, done, total);
            if (done < total)
                emit jobProgress(meetingId, tr("Jegyzetelés: %1/%2. rész…").arg(done + 1).arg(total));
            return;
        }
        if (stage == QLatin1String("merge")) {
            d->jobs->setStage(meetingId, JobKind::Summarize, QStringLiteral("merge"), StageState::Running);
            emit jobProgress(meetingId, tr("Összegzés a részjegyzetekből…"));
        }
    });
    connect(svc, &processing::SummaryService::summaryReady, this,
            [this, meetingId, providerObj, svc, run, key, statsKey, units, clock, llmId,
             model = cfg.model, note = req.contextNotes](const Summary& sum) {
        d->llmRuns.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        // A FRESH meeting (it may have changed / been deleted during the summarization).
        Meeting mm = d->store->load(meetingId);
        if (mm.id.isEmpty()) {
            d->jobs->cancelled(meetingId, JobKind::Summarize);
            finishCloudRun(run);
            return;
        }
        const QString md = sum.renderMarkdown();
        const QString mdPath = QDir(mm.folder).filePath(QStringLiteral("summary.md"));
        writeTextFile(mdPath, md);
        // The structured form + the provenance (summary.json); the summary view draws from this.
        library::SummaryDocument doc;
        doc.exists = true;
        doc.summary = sum;
        doc.markdown = md;
        doc.meta.createdAt = QDateTime::currentDateTime();
        doc.meta.providerId = llmId;
        doc.meta.model = model;
        doc.meta.mode = library::SummaryMode::Quick;
        doc.meta.contextNote = note;            // the note it was made with
        doc.meta.contextNoteKnown = true;
        library::summarystore::save(mm.folder, doc);
        // a copy into the notes (vault) folder
        QDir().mkpath(d->notesDir);
        const QString noteName = QStringLiteral("%1 %2.md")
            .arg(mm.startedAt.toString(QStringLiteral("yyyy-MM-dd")), slugify(mm.title));
        writeTextFile(QDir(d->notesDir).filePath(noteName), md);
        mm.hasSummary = true;
        d->store->saveMeeting(mm);
        d->jobStats->addSample(statsKey, double(clock->elapsed()) / 1000.0, units);
        d->jobs->finish(meetingId, JobKind::Summarize);
        emit summaryReady(mm.id, mdPath);
        finishCloudRun(run);
    });
    connect(svc, &processing::SummaryService::summaryFailed, this,
            [this, meetingId, providerObj, svc, run, key, sink, ctx](const QString& e) {
        d->llmRuns.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        const JobError je = d->describeLlmFailure(JobKind::Summarize, e, run, sink, ctx);
        d->jobs->fail(meetingId, JobKind::Summarize, je);
        failCloudRun(run, tr("Összefoglaló hiba: %1").arg(je.message));
    });

    // Preparing the model (LM Studio: loading with the needed context), then it starts.
    providers::LlmModelPreparer* prep = d->prepareLlm(meetingId, JobKind::Summarize, cfg, bool(run), ctx,
        [this, svc = QPointer<processing::SummaryService>(svc), req, ctx, cfg, clock]() mutable {
            if (!svc) return;
            clock->restart();   // the model load does not count in the run's speed (estimate)
            req.contextLimit = ctx->loadedContext > 0 ? ctx->loadedContext
                                                      : d->learnedContext.value(Impl::serverKey(cfg));
            svc->summarize(req);
        },
        [this, meetingId, providerObj = QPointer<QObject>(providerObj),
         svc = QPointer<processing::SummaryService>(svc), key](const JobError& je) {
            d->llmRuns.remove(key);
            if (providerObj) providerObj->deleteLater();
            if (svc) svc->deleteLater();
            d->jobs->fail(meetingId, JobKind::Summarize, je);
            emit errorOccurred(tr("Összefoglaló hiba: %1").arg(je.message));
        });
    if (prep && d->llmRuns.contains(key)) d->llmRuns[key].preparer = prep;
}

// The quick summary request from the settings: the three prompts (override -> file -> built-in, with the
// target language), the context and the part-note cache path. model / maxTokens come from the provider
// config actually used (cloud: a moderate max_tokens because of the coverage rule).
static processing::SummaryRequest summaryRequestFor(const AppSettings& s, const MergedTranscript& merged,
                                        const Meeting& m, const QString& model, int maxTokens)
{
    constexpr int kNotesMaxTokens = 3000;   // one part note fits well within it
    processing::SummaryRequest req;
    req.transcript = merged;
    req.contextNotes = m.contextNote.trimmed();
    req.singlePrompt = resolvedPrompt(s, s.summaryPrompt, "single");
    req.notesPrompt = resolvedPrompt(s, s.notesPrompt, "notes");
    req.mergePrompt = resolvedPrompt(s, s.mergePrompt, "merge");
    req.language = s.summaryLanguage;
    req.model = model;
    req.maxTokens = maxTokens > 0 ? maxTokens : 8000;
    req.notesMaxTokens = qMin(req.maxTokens, kNotesMaxTokens);
    if (!m.folder.isEmpty())
        req.cachePath = QDir(m.folder).filePath(processing::SummaryService::cacheFileName());
    return req;
}

// ---- complex (multi-pass) summary ------------------------------------------

void AppController::extractMeetingTopics(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::ExtractTopics))
        return;   // already running

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    // If a (possibly edited) topic list already exists, we return that; no re-extraction.
    const QString topicsPath = QDir(m.folder).filePath(QStringLiteral("summary.topics.json"));
    const QVector<SummaryTopic> existing = readTopicsJson(topicsPath);
    if (!existing.isEmpty()) { emit topicsReady(m.id, existing); return; }

    providers::ReadinessResult res = canRun(providers::WorkflowStep::Summarize, meetingId);
    if (!res.runnable) { emit errorOccurred(res.detail); return; }

    MergedTranscript merged = d->mergedCache.value(meetingId);
    if (merged.tokens.isEmpty())
        merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) { emit errorOccurred(tr("Nincs átirat — előbb futtass átírást.")); return; }
    editing::speakeredit::applyResolvedSpeakers(merged, m);
    const QString transcriptMd = merged.renderMarkdown();

    const AppSettings s = d->settings->settings();
    CloudRunPtr run;
    ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("topics"), QStringLiteral("complex"));
    const auto sink = std::make_shared<FailureSink>();
    captureFailures(cfg, sink);
    providers::ILlmProvider* provider = providers::LlmProviderRegistry::instance().create(s.llmProviderId, cfg, this);
    if (!provider) { emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(s.llmProviderId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new processing::ComplexSummaryService(provider, this);
    svc->setLanguage(s.summaryLanguage);
    emit jobProgress(meetingId, run ? tr("Témák kigyűjtése a Tanara Cloudban…")
                                    : tr("Témák kigyűjtése a helyi modellel…"));

    const QString key = Impl::llmKey(meetingId, JobKind::ExtractTopics);
    const QString statsKey = QStringLiteral("topics/%1/%2").arg(s.llmProviderId, cfg.model);
    const double units = double(transcriptMd.size());
    d->jobs->begin(meetingId, JobKind::ExtractTopics, tr("Témák javaslása"));
    d->jobs->setEstimate(meetingId, JobKind::ExtractTopics, d->jobStats->estimateSec(statsKey, units));
    d->llmRuns.insert(key, Impl::LlmRun{svc, providerObj, run});
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();

    connect(svc, &processing::ComplexSummaryService::topicsReady, this,
            [this, id = m.id, topicsPath, providerObj, svc, run, key, statsKey, units, clock]
            (const QVector<SummaryTopic>& topics) {
        d->llmRuns.remove(key);
        if (QFileInfo(topicsPath).absoluteDir().exists())   // (the meeting may have been deleted meanwhile)
            writeTopicsJson(topicsPath, topics);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        // Cloud: topic gathering is the FIRST part of the complex run; the run (and the X-Tanara-Job-Id) stays
        // open, the topic analyses and the merge use the same one; at the end of the run the total is shown
        // (one run = one id). Here only the balance chip refreshes.
        if (run) {
            d->complexRuns.insert(id, run);
            if (run->balance.isValid() || run->calls > 0)
                d->cloud->refreshAccount();
        }
        d->jobStats->addSample(statsKey, double(clock->elapsed()) / 1000.0, units);
        d->jobs->finish(id, JobKind::ExtractTopics);
        emit topicsChanged(id);
        emit topicsReady(id, topics);
    });
    const QString topicPrompt = resolvedPrompt(s, s.topicExtractionPrompt, "topic");
    const auto ctx = std::make_shared<Impl::LlmContextState>();
    ctx->need = providers::llmctx::callContextNeed(topicPrompt.size() + transcriptMd.size()
                                        + m.contextNote.size() + 300, cfg.maxTokens);
    connect(svc, &processing::ComplexSummaryService::failed, this,
            [this, id = m.id, providerObj, svc, run, key, sink, ctx](const QString& e) {
        d->llmRuns.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        const JobError je = d->describeLlmFailure(JobKind::ExtractTopics, e, run, sink, ctx);
        d->jobs->fail(id, JobKind::ExtractTopics, je);
        failCloudRun(run, tr("Téma-kinyerés hiba: %1").arg(je.message));
    });

    providers::LlmModelPreparer* prep = d->prepareLlm(m.id, JobKind::ExtractTopics, cfg, bool(run), ctx,
        [svc = QPointer<processing::ComplexSummaryService>(svc), transcriptMd, note = m.contextNote.trimmed(),
         topicPrompt, cfg, clock]() {
            clock->restart();   // the model load does not count in the run's speed (estimate)
            if (svc) svc->requestTopics(transcriptMd, note, topicPrompt, cfg.model, cfg.temperature, cfg.maxTokens);
        },
        [this, id = m.id, providerObj = QPointer<QObject>(providerObj),
         svc = QPointer<processing::ComplexSummaryService>(svc), key](const JobError& je) {
            d->llmRuns.remove(key);
            if (providerObj) providerObj->deleteLater();
            if (svc) svc->deleteLater();
            d->jobs->fail(id, JobKind::ExtractTopics, je);
            emit errorOccurred(tr("Téma-kinyerés hiba: %1").arg(je.message));
        });
    if (prep && d->llmRuns.contains(key)) d->llmRuns[key].preparer = prep;
}

QVector<TopicAnalysis> AppController::topicAnalyses(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return {};
    return readAnalysesJson(QDir(m.folder).filePath(QStringLiteral("summary.analyses.json")));
}

void AppController::generateComplexSummary(const QString& meetingId, const QVector<SummaryTopic>& topics)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    if (topics.isEmpty()) { emit errorOccurred(tr("Nincs téma a komplex összefoglalóhoz.")); return; }

    // Persisting the (edited) topic list: it stays resumable.
    writeTopicsJson(QDir(m.folder).filePath(QStringLiteral("summary.topics.json")), topics);
    emit topicsChanged(meetingId);

    // Only the topics NOT YET ANALYZED go into the queue (finished analyses are on disk), so after a
    // cancel/error a restart resumes where it stopped. Rerunning an already finished topic can be requested
    // with the card's own button (analyzeTopic).
    const QVector<TopicAnalysis> existing =
        readAnalysesJson(QDir(m.folder).filePath(QStringLiteral("summary.analyses.json")));
    QSet<QString> doneIds;
    for (const TopicAnalysis& a : existing) doneIds.insert(a.topicId);

    QVector<SummaryTopic> missing;
    for (const SummaryTopic& t : topics)
        if (!doneIds.contains(t.id)) missing.append(t);

    // Cloud: the complex summary (topic gathering + topic analyses + reduce) is ONE run with one
    // X-Tanara-Job-Id. If the topic-gathering run (or an already running batch) is still open, we continue it;
    // otherwise (e.g. continuing after a cancel / error, where the run has already closed) a new run starts
    // with a new id.
    if (usesCloud(providers::WorkflowStep::Summarize)) {
        CloudRunPtr run = d->complexRuns.value(meetingId);
        if (!run) {
            run = newCloudRun(meetingId, QStringLiteral("complex"));
            d->complexRuns.insert(meetingId, run);
        }
        run->kind = QStringLiteral("complex");
    }
    if (missing.isEmpty()) { finalizeComplexSummary(meetingId); return; }
    enqueueTopicAnalyses(meetingId, missing, /*reduceWhenDone*/ true);
}

void AppController::analyzeTopic(const QString& meetingId, const SummaryTopic& topic)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    if (topic.id.isEmpty() || topic.title.trimmed().isEmpty()) {
        emit errorOccurred(tr("A témához cím kell az elemzéshez."));
        return;
    }

    // Carrying the (edited) topic into topics.json: the reduce's order/content comes from there.
    const QString topicsPath = QDir(m.folder).filePath(QStringLiteral("summary.topics.json"));
    QVector<SummaryTopic> topics = readTopicsJson(topicsPath);
    bool found = false;
    for (SummaryTopic& t : topics)
        if (t.id == topic.id) { t = topic; found = true; break; }
    if (!found) topics.append(topic);
    writeTopicsJson(topicsPath, topics);
    emit topicsChanged(meetingId);

    enqueueTopicAnalyses(meetingId, { topic }, /*reduceWhenDone*/ false);
}

void AppController::enqueueTopicAnalyses(const QString& meetingId,
                                         const QVector<SummaryTopic>& topics, bool reduceWhenDone)
{
    if (reduceWhenDone) d->reduceWhenDone.insert(meetingId);
    if (!d->jobCounts.contains(meetingId)) d->jobCounts.insert(meetingId, {0, 0});

    int added = 0;
    for (const SummaryTopic& t : topics) {
        // Dedup: if the same topic is already running or queued, it does not go in again.
        if (d->topicJobActive && d->activeTopicMeetingId == meetingId && d->activeTopicId == t.id)
            continue;
        const bool queued = std::any_of(d->topicJobQueue.cbegin(), d->topicJobQueue.cend(),
            [&](const Impl::TopicJob& j) { return j.meetingId == meetingId && j.topic.id == t.id; });
        if (queued)
            continue;
        d->topicJobQueue.append({ meetingId, t });
        ++added;
        d->jobs->clearTopicError(meetingId, t.id);   // a new attempt: the old error loses its validity
        emit topicAnalysisQueued(meetingId, t.id);
        emit topicStatusChanged(meetingId, t.id);
    }
    // Structured task: ONE "Analyzing topics" per meeting (with count progress).
    if (added > 0 || d->jobs->isRunning(meetingId, JobKind::AnalyzeTopics)) {
        d->topicBatchTotal[meetingId] += added;
        if (!d->jobs->isRunning(meetingId, JobKind::AnalyzeTopics))
            d->jobs->begin(meetingId, JobKind::AnalyzeTopics, tr("Témák elemzése"));
        d->updateTopicCounts(meetingId);
    }
    if (!d->topicJobActive)
        startNextTopicJob();
}

void AppController::startNextTopicJob()
{
    // Did we reach the end of the queue? We close the batch per meeting: queueFinished + (if requested and
    // nothing failed) auto-reduce. Jobs of several meetings may in principle mix in the queue, so we close a
    // meeting only when no job is waiting for it.
    if (d->topicJobQueue.isEmpty()) {
        d->topicJobActive = false;
        d->activeTopicMeetingId.clear();
        d->activeTopicId.clear();
        d->activeTopicRun = {};
        const auto counts = d->jobCounts;
        d->jobCounts.clear();
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
            const QString& id = it.key();
            const int ok = it.value().first, fail = it.value().second;
            // Structured closing: a failed topic -> the summary step's error (the per-topic details are in
            // topicStatuses); otherwise done.
            d->topicBatchTotal.remove(id);
            if (d->jobs->isRunning(id, JobKind::AnalyzeTopics)) {
                if (fail > 0) {
                    JobError je;
                    je.message = tr("%1 téma elemzése nem sikerült.").arg(fail);
                    d->jobs->fail(id, JobKind::AnalyzeTopics, je);
                } else {
                    d->jobs->finish(id, JobKind::AnalyzeTopics);
                }
            }
            emit topicAnalysisQueueFinished(id, ok, fail);
            const bool wantReduce = d->reduceWhenDone.remove(id);
            if (wantReduce && fail == 0) {
                finalizeComplexSummary(id);   // the cloud run continues with the reduce
                continue;
            }
            if (wantReduce)
                emit jobProgress(id, tr(
                    "%1 téma elemzése nem sikerült — futtasd újra a kártyáján, majd kérd a végső összegzést.")
                    .arg(fail));
            // A cloud run closing without a reduce (rerunning one topic, or a non-cloud error): the sum of the
            // charges so far goes to the cost feedback.
            const CloudRunPtr run = d->complexRuns.take(id);
            if (run && !run->lastError.isError())
                finishCloudRun(run);
        }
        return;
    }

    const Impl::TopicJob job = d->topicJobQueue.takeFirst();
    // An early (pre-provider-call) failure: a remembered topic error + the queue goes on.
    auto failEarly = [this, &job](const QString& msg) {
        d->jobCounts[job.meetingId].second++;
        JobError je;
        je.message = msg;
        d->jobs->setTopicError(job.meetingId, job.topic.id, je);
        d->updateTopicCounts(job.meetingId);
        emit topicAnalysisFailed(job.meetingId, job.topic.id, msg);
        emit topicStatusChanged(job.meetingId, job.topic.id);
        startNextTopicJob();
    };

    Meeting m = d->store->load(job.meetingId);
    if (m.id.isEmpty()) { failEarly(tr("Ismeretlen meeting.")); return; }

    MergedTranscript merged = d->mergedCache.value(m.id);
    if (merged.tokens.isEmpty())
        merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) { failEarly(tr("Nincs átirat — előbb futtass átírást.")); return; }
    editing::speakeredit::applyResolvedSpeakers(merged, m);

    const AppSettings s = d->settings->settings();
    CloudRunPtr run = d->complexRuns.value(m.id);
    if (usesCloud(providers::WorkflowStep::Summarize) && !run) {   // rerunning one topic without a batch
        run = newCloudRun(m.id, QStringLiteral("complex"));
        d->complexRuns.insert(m.id, run);
    }
    if (run) run->kind = QStringLiteral("complex");   // after topic gathering, the analysis stage
    ProviderConfig cfg = llmConfigFor(run, m.id, QStringLiteral("complex"), QStringLiteral("complex"));
    const auto sink = std::make_shared<FailureSink>();
    captureFailures(cfg, sink);
    providers::ILlmProvider* provider = providers::LlmProviderRegistry::instance().create(s.llmProviderId, cfg, this);
    if (!provider) { failEarly(tr("Ismeretlen LLM-provider: %1.").arg(s.llmProviderId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new processing::ComplexSummaryService(provider, this);
    svc->setLanguage(s.summaryLanguage);

    d->topicJobActive = true;
    d->activeTopicMeetingId = m.id;
    d->activeTopicId = job.topic.id;
    d->activeTopicRun = Impl::LlmRun{svc, providerObj, run};
    d->jobs->setMessage(m.id, JobKind::AnalyzeTopics, tr("„%1” téma elemzése…").arg(job.topic.title));
    emit topicAnalysisStarted(m.id, job.topic.id);
    emit topicStatusChanged(m.id, job.topic.id);
    emit jobProgress(m.id, tr("„%1” téma elemzése…").arg(job.topic.title));

    const QString analysesPath = QDir(m.folder).filePath(QStringLiteral("summary.analyses.json"));
    auto cleanup = [providerObj, svc]() {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
    };

    connect(svc, &processing::ComplexSummaryService::topicAnalysisReady, this,
            [this, meetingId = m.id, analysesPath, cleanup](const TopicAnalysis& a) {
        if (QFileInfo(analysesPath).absoluteDir().exists())
            upsertAnalysisJson(analysesPath, a);   // IMMEDIATELY to disk: the work must not be lost
        d->jobCounts[meetingId].first++;
        cleanup();
        // The topic is no longer "running": the state query (topicStatuses) now gives "done".
        d->activeTopicId.clear();
        d->activeTopicRun = {};
        d->jobs->clearTopicError(meetingId, a.topicId);
        d->updateTopicCounts(meetingId);
        emit topicAnalysisReady(meetingId, a);
        emit topicStatusChanged(meetingId, a.topicId);
        startNextTopicJob();
    });
    const QString transcriptMd = merged.renderMarkdown();
    const QString analysisPrompt = resolvedPrompt(s, s.topicAnalysisPrompt, "analysis");
    const auto ctx = std::make_shared<Impl::LlmContextState>();
    const auto prepError = std::make_shared<JobError>();   // the preparation's error (if that failed)
    ctx->need = providers::llmctx::callContextNeed(analysisPrompt.size() + transcriptMd.size() + m.contextNote.size()
                                        + job.topic.title.size() + job.topic.summary.size() + 400,
                                        cfg.maxTokens);
    connect(svc, &processing::ComplexSummaryService::failed, this,
            [this, meetingId = m.id, topicId = job.topic.id, cleanup, run, sink, ctx, prepError](const QString& raw) {
        d->jobCounts[meetingId].second++;
        cleanup();
        d->activeTopicId.clear();
        d->activeTopicRun = {};
        const JobError je = prepError->isValid()
            ? *prepError : d->describeLlmFailure(JobKind::AnalyzeTopics, raw, run, sink, ctx);
        const QString e = je.message;
        d->jobs->setTopicError(meetingId, topicId, je);
        if (run && run->lastError.isError()) {
            // Cloud error (402 / 403 / 426 / 429 / 503 ...): the meeting's remaining topics would get the same, so we
            // take them out of the queue; the partial-failure dialog shows the amount charged so far and "Continue"
            // (it pays only for the remaining parts).
            emit topicAnalysisFailed(meetingId, topicId, run->lastError.message.isEmpty() ? e : run->lastError.message);
            emit topicStatusChanged(meetingId, topicId);
            for (int i = d->topicJobQueue.size() - 1; i >= 0; --i) {
                if (d->topicJobQueue.at(i).meetingId != meetingId) continue;
                const QString dropped = d->topicJobQueue.takeAt(i).topic.id;
                d->jobCounts[meetingId].second++;
                emit topicAnalysisFailed(meetingId, dropped, tr("Megszakítva"));
                emit topicStatusChanged(meetingId, dropped);
            }
            d->reduceWhenDone.remove(meetingId);
            d->complexRuns.remove(meetingId);
            failCloudRun(run, e);
        } else {
            emit topicAnalysisFailed(meetingId, topicId, e);   // the other topics go on
            emit topicStatusChanged(meetingId, topicId);
        }
        d->updateTopicCounts(meetingId);
        startNextTopicJob();
    });

    providers::LlmModelPreparer* prep = d->prepareLlm(m.id, JobKind::AnalyzeTopics, cfg, bool(run), ctx,
        [svc = QPointer<processing::ComplexSummaryService>(svc), transcriptMd, topic = job.topic,
         note = m.contextNote.trimmed(), analysisPrompt, cfg]() {
            if (svc) svc->requestTopicAnalysis(transcriptMd, topic, note, analysisPrompt,
                                               cfg.model, cfg.temperature, cfg.maxTokens);
        },
        [svc = QPointer<processing::ComplexSummaryService>(svc), prepError](const JobError& je) {
            // The same path as for a provider error: the topic's error is remembered, the queue goes on.
            *prepError = je;
            if (svc) emit svc->failed(je.message);
        });
    if (prep) d->activeTopicRun.preparer = prep;
}

void AppController::finalizeComplexSummary(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::Summarize))
        return;   // a merge is already running

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    // The analyses on disk, in the (edited) order of topics.json; orphan analyses (belonging to a deleted
    // topic) are left out.
    const QVector<SummaryTopic> topics =
        readTopicsJson(QDir(m.folder).filePath(QStringLiteral("summary.topics.json")));
    const QVector<TopicAnalysis> all =
        readAnalysesJson(QDir(m.folder).filePath(QStringLiteral("summary.analyses.json")));
    QVector<TopicAnalysis> ordered;
    for (const SummaryTopic& t : topics)
        for (const TopicAnalysis& a : all)
            if (a.topicId == t.id) { ordered.append(a); break; }
    if (ordered.isEmpty()) {
        emit errorOccurred(tr("Nincs kész téma-elemzés — előbb futtasd a témánkénti elemzést."));
        return;
    }

    const AppSettings s = d->settings->settings();
    CloudRunPtr run = d->complexRuns.take(meetingId);   // the batch's run (if any) continues
    ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("complex"), QStringLiteral("complex"));
    const auto sink = std::make_shared<FailureSink>();
    captureFailures(cfg, sink);
    providers::ILlmProvider* provider = providers::LlmProviderRegistry::instance().create(s.llmProviderId, cfg, this);
    if (!provider) { emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(s.llmProviderId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new processing::ComplexSummaryService(provider, this);
    svc->setLanguage(s.summaryLanguage);
    svc->setReducePrompt(resolvedPrompt(s, QString(), "reduce"));
    emit jobProgress(meetingId, tr("Összegzés (vezetői összefoglaló + teendők)…"));

    const QString key = Impl::llmKey(meetingId, JobKind::Summarize);
    d->jobs->begin(meetingId, JobKind::Summarize, tr("Összegzés készítése"));
    d->llmRuns.insert(key, Impl::LlmRun{svc, providerObj, run});

    auto cleanup = [providerObj, svc]() {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
    };

    const auto ctx = std::make_shared<Impl::LlmContextState>();
    {
        qint64 chars = resolvedPrompt(s, QString(), "reduce").size() + m.contextNote.size() + 400;
        for (const TopicAnalysis& a : ordered) chars += a.renderMarkdown().size() + a.title.size() + 40;
        ctx->need = providers::llmctx::callContextNeed(chars, cfg.maxTokens);
    }
    connect(svc, &processing::ComplexSummaryService::failed, this,
            [this, meetingId, cleanup, run, key, sink, ctx](const QString& e) {
        d->llmRuns.remove(key);
        cleanup();
        const JobError je = d->describeLlmFailure(JobKind::Summarize, e, run, sink, ctx);
        d->jobs->fail(meetingId, JobKind::Summarize, je);
        failCloudRun(run, tr("Komplex összefoglaló hiba: %1").arg(je.message));
    });
    connect(svc, &processing::ComplexSummaryService::reduceReady, this,
            [this, m, ordered, cleanup, run, key, llmId = s.llmProviderId, model = cfg.model]
            (const QString& execSummary, const QVector<ActionItem>&) {
        d->llmRuns.remove(key);
        // Action items from CODE (not from the LLM): we collect the per-topic analyses' action items with a
        // normalized-text dedup. Deterministic, model-independent; only the executive summary is left to the LLM
        // reduce (less room for "thinking out loud").
        QVector<ActionItem> mergedItems;
        QSet<QString> seen;
        for (const TopicAnalysis& a : ordered)
            for (const ActionItem& ai : a.actionItems) {
                const QString norm = ai.text.simplified().toLower();
                if (norm.isEmpty() || seen.contains(norm))
                    continue;
                seen.insert(norm);
                mergedItems.append(ai);
            }
        const QString md = renderComplexMarkdown(execSummary, mergedItems, ordered);
        Meeting mm = d->store->load(m.id);
        if (mm.id.isEmpty()) {   // the meeting was deleted meanwhile
            cleanup();
            d->jobs->cancelled(m.id, JobKind::Summarize);
            finishCloudRun(run);
            return;
        }
        const QString mdPath = QDir(mm.folder).filePath(QStringLiteral("summary.md"));
        writeTextFile(mdPath, md);
        // Structured form + metadata (summary.json): executive summary, merged action items, the union of the
        // topics' decisions, and the topic analyses themselves, in order.
        library::SummaryDocument doc;
        doc.exists = true;
        doc.markdown = md;
        doc.summary.execSummary = execSummary;
        doc.summary.actionItems = mergedItems;
        for (const TopicAnalysis& a : ordered)
            for (const QString& dec : a.decisions)
                if (!doc.summary.decisions.contains(dec)) doc.summary.decisions << dec;
        doc.summary.openQuestions = openQuestionsOf(ordered);
        doc.topics = ordered;
        doc.meta.createdAt = QDateTime::currentDateTime();
        doc.meta.providerId = llmId;
        doc.meta.model = model;
        doc.meta.mode = library::SummaryMode::Topics;
        doc.meta.contextNote = m.contextNote.trimmed();   // at the start of the final merge
        doc.meta.contextNoteKnown = true;
        library::summarystore::save(mm.folder, doc);
        QDir().mkpath(d->notesDir);
        const QString noteName = QStringLiteral("%1 %2.md")
            .arg(mm.startedAt.toString(QStringLiteral("yyyy-MM-dd")), slugify(mm.title));
        writeTextFile(QDir(d->notesDir).filePath(noteName), md);
        mm.hasSummary = true;
        d->store->saveMeeting(mm);
        cleanup();
        d->jobs->finish(mm.id, JobKind::Summarize);
        emit summaryReady(mm.id, mdPath);
        finishCloudRun(run);   // "The summary cost $0.46 (12 parts)."
    });

    providers::LlmModelPreparer* prep = d->prepareLlm(meetingId, JobKind::Summarize, cfg, bool(run), ctx,
        [svc = QPointer<processing::ComplexSummaryService>(svc), ordered, note = m.contextNote.trimmed(), cfg]() {
            if (svc) svc->requestReduce(ordered, note, cfg.model, cfg.temperature, cfg.maxTokens);
        },
        [this, meetingId, key, providerObj = QPointer<QObject>(providerObj),
         svc = QPointer<processing::ComplexSummaryService>(svc)](const JobError& je) {
            d->llmRuns.remove(key);
            if (providerObj) providerObj->deleteLater();
            if (svc) svc->deleteLater();
            d->jobs->fail(meetingId, JobKind::Summarize, je);
            emit errorOccurred(tr("Komplex összefoglaló hiba: %1").arg(je.message));
        });
    if (prep && d->llmRuns.contains(key)) d->llmRuns[key].preparer = prep;
}

// =========================================================================================
// Structured layer for the redesigned main window: state queries, cancel, topics, asynchronous
// identification, waveform. (It lives alongside the old jobProgress / errorOccurred.)
// =========================================================================================

library::MeetingJobTracker* AppController::jobs() const      { return d->jobs; }
library::MeetingLibrary*    AppController::library() const   { return d->library; }
library::TrackCatalog*      AppController::tracks() const    { return d->tracks; }
library::WaveformService*   AppController::waveforms() const { return d->waveforms; }
library::AudioImporter*     AppController::importer() const  { return d->importer; }

QString AppController::importAudio(const library::ImportRequest& request)
{
    if (d->importer->busy()) {
        emit errorOccurred(tr("Már fut egy importálás — várd meg, vagy szakítsd meg."));
        return {};
    }
    const AppSettings s = d->settings->settings();
    d->importer->setOpusBitrateKbps(opusBitrateKbps(s.audioQuality));
    d->importer->setUserSpeakerName(s.userSpeakerName);
    return d->importer->start(request);
}

// ---- meeting archive ---------------------------------------------------------------

namespace {

QVector<Tag> archiveTags(library::TagService* tags, const Meeting& m)
{
    QVector<Tag> out;
    for (const QString& id : m.tagIds) {
        const Tag t = tags ? tags->tag(id) : Tag{};
        if (t.isValid()) out.append(t);
    }
    return out;
}

// Pulling in the unpacked folder + resolving the tags to the local set. Runs on the main thread.
Meeting adoptArchiveFolder(library::MeetingStore* store, library::TagService* tags, const QString& folder,
                           const library::ArchiveManifest& manifest, QString* error)
{
    Meeting m = store->adoptMeetingFolder(folder, error);
    if (m.id.isEmpty() || !tags) return m;
    QStringList ids;
    QStringList pending = m.tagIds;   // identifiers not in the manifest but known locally
    for (const Tag& t : manifest.tags) {
        Tag local = tags->tag(t.id);
        if (!local.isValid()) local = tags->byName(t.name);
        if (!local.isValid()) local = tags->create(t.name);
        if (local.isValid() && !ids.contains(local.id)) ids << local.id;
        pending.removeAll(t.id);
    }
    for (const QString& id : pending)
        if (tags->tag(id).isValid() && !ids.contains(id)) ids << id;
    if (ids != m.tagIds) {
        tags->setTags(m.id, ids, TagSource::Bulk);
        m.tagIds = ids;
    }
    return m;
}

} // namespace

bool AppController::archiveBusy() const { return d->archiveThread != nullptr; }

bool AppController::exportMeetingArchiveNow(const QString& meetingId, const QString& zipPath,
                                            QString* error, std::function<void(int)> progress)
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) {
        if (error) *error = tr("Nincs ilyen megbeszélés: %1").arg(meetingId);
        return false;
    }
    if (d->state != RecordingState::Idle && !d->currentFolder.isEmpty()
        && QDir(d->currentFolder).absolutePath() == QDir(m.folder).absolutePath()) {
        if (error) *error = tr("Felvétel közben a megbeszélés nem exportálható.");
        return false;
    }
    return library::MeetingArchive::exportMeeting(m, zipPath, error, std::move(progress), archiveTags(d->tags, m));
}

Meeting AppController::importMeetingArchiveNow(const QString& zipPath, QString* error,
                                               std::function<void(int)> progress)
{
    const QString root = QDir(d->store->audioDir()).absolutePath();
    QDir().mkpath(root);
    library::ArchiveManifest manifest;
    const QString folder = library::MeetingArchive::importArchive(zipPath, root, error, std::move(progress), &manifest);
    if (folder.isEmpty()) return {};
    return adoptArchiveFolder(d->store, d->tags, folder, manifest, error);
}

QString AppController::exportMeetingArchive(const QString& meetingId, const QString& zipPath)
{
    if (archiveBusy()) {
        emit errorOccurred(tr("Már fut egy archívum-művelet — várd meg, vagy szakítsd meg."));
        return {};
    }
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) {
        emit errorOccurred(tr("Nincs ilyen megbeszélés: %1").arg(meetingId));
        return {};
    }
    if (d->state != RecordingState::Idle && !d->currentFolder.isEmpty()
        && QDir(d->currentFolder).absolutePath() == QDir(m.folder).absolutePath()) {
        emit errorOccurred(tr("Felvétel közben a megbeszélés nem exportálható."));
        return {};
    }
    const QVector<Tag> tags = archiveTags(d->tags, m);
    const QString opId = QStringLiteral("export:") + meetingId;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    auto ok = std::make_shared<bool>(false);
    auto error = std::make_shared<QString>();
    d->jobs->begin(meetingId, JobKind::Export, tr("Exportálás archívumba"));
    d->jobs->setMessage(meetingId, JobKind::Export, QFileInfo(zipPath).fileName());
    d->jobs->setPercent(meetingId, JobKind::Export, 0);

    QPointer<AppController> self(this);
    QThread* th = QThread::create([self, m, zipPath, tags, cancel, ok, error, opId]() {
        *ok = library::MeetingArchive::exportMeeting(m, zipPath, error.get(), [self, opId, mid = m.id](int pct) {
            QMetaObject::invokeMethod(qApp, [self, opId, mid, pct]() {
                if (!self || self->d->archiveOp != opId) return;
                if (self->d->jobs->isRunning(mid, JobKind::Export))
                    self->d->jobs->setPercent(mid, JobKind::Export, pct);
                emit self->archiveProgress(opId, pct);
            }, Qt::QueuedConnection);
        }, tags, cancel.get());
    });
    d->archiveThread = th;
    d->archiveCancel = cancel;
    d->archiveOp = opId;
    connect(th, &QThread::finished, this, [this, th, meetingId, zipPath, cancel, ok, error, opId]() {
        d->archiveThread = nullptr;
        d->archiveCancel.reset();
        d->archiveOp.clear();
        th->deleteLater();
        if (*ok) {
            d->jobs->finish(meetingId, JobKind::Export);
            emit archiveFinished(opId, true, meetingId, zipPath, QString());
        } else if (cancel->load()) {
            d->jobs->cancelled(meetingId, JobKind::Export);
            emit archiveFinished(opId, false, meetingId, QString(), tr("Megszakítva."));
        } else {
            JobError je;
            je.kind = JobKind::Export;
            je.message = *error;
            je.when = QDateTime::currentDateTime();
            d->jobs->fail(meetingId, JobKind::Export, je);
            emit archiveFinished(opId, false, meetingId, QString(), *error);
        }
    });
    th->start();
    return opId;
}

QString AppController::importMeetingFolder(const QString& folderIn)
{
    if (archiveBusy()) {
        emit errorOccurred(tr("Már fut egy archívum-művelet — várd meg, vagy szakítsd meg."));
        return {};
    }
    const QString folder = QFileInfo(folderIn).absoluteFilePath();
    const QString root = QDir(d->store->audioDir()).absolutePath();
    const QString opId = QStringLiteral("import:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto staged = std::make_shared<QString>();
    auto error = std::make_shared<QString>();
    // A source under the recordings folder stays in place (no copy); everything else first goes into the
    // .import-* temporary folder on a background thread, so the GUI does not freeze.
    const bool inPlace = QFileInfo(folder).dir().absolutePath() == root;

    QPointer<AppController> self(this);
    library::MeetingStore* store = d->store;
    QThread* th = QThread::create([self, folder, inPlace, store, staged, error, opId]() {
        if (inPlace) { *staged = folder; return; }
        *staged = store->stageFolderForImport(folder, error.get());
        QMetaObject::invokeMethod(qApp, [self, opId]() {
            if (self && self->d->archiveOp == opId) emit self->archiveProgress(opId, 100);
        }, Qt::QueuedConnection);
    });
    d->archiveThread = th;
    d->archiveCancel.reset();
    d->archiveOp = opId;
    connect(th, &QThread::finished, this, [this, th, folder, staged, error, opId]() {
        d->archiveThread = nullptr;
        d->archiveOp.clear();
        th->deleteLater();
        QString message = *error;
        Meeting m;
        if (!staged->isEmpty())
            m = adoptArchiveFolder(d->store, d->tags, *staged, library::ArchiveManifest{}, &message);
        if (m.id.isEmpty())
            emit archiveFinished(opId, false, QString(), folder, message);
        else
            emit archiveFinished(opId, true, m.id, folder, QString());
    });
    th->start();
    return opId;
}

QString AppController::importMeetingArchive(const QString& zipPath)
{
    if (archiveBusy()) {
        emit errorOccurred(tr("Már fut egy archívum-művelet — várd meg, vagy szakítsd meg."));
        return {};
    }
    const QString root = QDir(d->store->audioDir()).absolutePath();
    QDir().mkpath(root);
    const QString opId = QStringLiteral("import:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto folder = std::make_shared<QString>();
    auto error = std::make_shared<QString>();
    auto manifest = std::make_shared<library::ArchiveManifest>();

    QPointer<AppController> self(this);
    QThread* th = QThread::create([self, zipPath, root, folder, error, manifest, opId]() {
        *folder = library::MeetingArchive::importArchive(zipPath, root, error.get(), [self, opId](int pct) {
            QMetaObject::invokeMethod(qApp, [self, opId, pct]() {
                if (self && self->d->archiveOp == opId) emit self->archiveProgress(opId, pct);
            }, Qt::QueuedConnection);
        }, manifest.get());
    });
    d->archiveThread = th;
    d->archiveCancel.reset();   // unpacking cannot be cancelled (it is short, and cleans up at the end anyway)
    d->archiveOp = opId;
    connect(th, &QThread::finished, this, [this, th, zipPath, folder, error, manifest, opId]() {
        d->archiveThread = nullptr;
        d->archiveOp.clear();
        th->deleteLater();
        QString message = *error;
        Meeting m;
        if (!folder->isEmpty())
            m = adoptArchiveFolder(d->store, d->tags, *folder, *manifest, &message);
        if (m.id.isEmpty())
            emit archiveFinished(opId, false, QString(), zipPath, message);
        else
            emit archiveFinished(opId, true, m.id, zipPath, QString());
    });
    th->start();
    return opId;
}

MeetingProcessingState AppController::processingState(const QString& meetingId) const
{
    return d->jobs->state(meetingId);
}

// ---- tags and tag suggestions ------------------------------------------------------------

void AppController::publishTagSuggestions(const QString& meetingId, QVector<TagSuggestion> list)
{
    // The suggestion from the participants' manual tags ("By participants") is always computed fresh: earlier
    // People items drop out, new ones stand first (the row's source tag comes from these). If a tag is also
    // suggested by similarity, one item remains and the reasons add up. It works before transcription too
    // (manual / approved participants).
    list.erase(std::remove_if(list.begin(), list.end(), [](const TagSuggestion& sg) {
        return sg.source == SuggestionSource::People;
    }), list.end());
    if (d->settings->settings().tagSuggestions) {
        QVector<TagSuggestion> people = d->tags->suggestMeetingTagsFromParticipants(meetingId);
        for (TagSuggestion& p : people) {
            const auto dup = std::find_if(list.begin(), list.end(), [&](const TagSuggestion& sg) {
                return !sg.isNew && sg.tagId == p.tagId;
            });
            if (dup == list.end()) continue;
            p.reasons += dup->reasons;
            p.similarMeetings = dup->similarMeetings;
            list.erase(dup);
        }
        list = people + list;
    }
    // An already applied and a rejected suggestion are dropped (the LLM's new-name idea too, if a tag with
    // that name has since been put on the meeting).
    const QStringList applied = d->tags->tagsOf(meetingId);
    QStringList appliedKeys;
    for (const QString& id : applied) appliedKeys << library::tagKey(d->tags->tag(id).name);
    list.erase(std::remove_if(list.begin(), list.end(), [&](const TagSuggestion& sg) {
        if (sg.isNew) return appliedKeys.contains(library::tagKey(sg.name)) || d->tags->isRejected(meetingId, sg.name);
        return applied.contains(sg.tagId) || !d->tags->tag(sg.tagId).isValid()
            || d->tags->isRejected(meetingId, sg.tagId);
    }), list.end());
    d->pendingTags.insert(meetingId, list);
    emit tagSuggestionsReady(meetingId, list);
}

// Publishing the cached similarity-based list (the computation runs on a background thread:
// startTagSuggestionJob).
void AppController::computeTagSuggestions(const QString& meetingId)
{
    tanara::PerfScope perfScope("AppController::computeTagSuggestions", 5);
    const auto it = d->similarCache.constFind(meetingId);
    if (it != d->similarCache.constEnd()) publishTagSuggestions(meetingId, *it);
}

void AppController::invalidateTagSuggestions()
{
    d->similarCache.clear();
    ++d->tagDataGen;
}

// The profiles (rows + transcript phrases) are built on a background thread; at the end of the round
// (idle) the suggestion computation starts.
void AppController::scheduleTagSuggestions(const QString& meetingId)
{
    d->tagRequests.insert(meetingId);
    d->profiles->ensureBuilt();
}

// Threading: TagService belongs to the main thread, so a small snapshot of the tag data (TagSnapshot) is
// made here, on the main thread; the background thread uses only that and the thread-safe readers of
// MeetingProfiles / EmbeddingIndex. The result comes back to the main thread via a queued call
// (finishTagSuggestionJob). ~AppController waits for the running computation.
void AppController::startTagSuggestionJob(const QString& meetingId)
{
    auto run = d->tagRuns.find(meetingId);
    if (run == d->tagRuns.end() || (!run->publish && !run->llm)) {
        d->tagRuns.remove(meetingId);
        return;
    }
    if (run->running) return;   // the running one's result answers (it restarts on a data change)
    const auto cached = d->similarCache.constFind(meetingId);
    if (cached != d->similarCache.constEnd()) {
        finishTagSuggestionJob(meetingId, d->tagDataGen, *cached);
        return;
    }
    run->running = true;
    library::TagSnapshot snap;
    {
        tanara::PerfScope perfScope("AppController::startTagSuggestionJob (pillanatkép, fő szál)", 5);
        snap = library::TagSuggester::snapshot(*d->tags, meetingId);
    }
    const quint64 gen = d->tagDataGen;
    const library::MeetingProfiles* profiles = d->profiles;
    const library::EmbeddingIndex* index = d->embeddings;
    d->tagPool.start([this, snap, gen, profiles, index]() {
        QVector<TagSuggestion> list;
        {
            tanara::PerfScope perfScope("TagSuggester::suggestFrom (háttérszál)", 0);
            list = library::TagSuggester::suggestFrom(snap, profiles, index);
        }
        const QString meetingId = snap.meetingId;
        QMetaObject::invokeMethod(this, [this, meetingId, gen, list]() {
            finishTagSuggestionJob(meetingId, gen, list);
        }, Qt::QueuedConnection);
    });
}

void AppController::finishTagSuggestionJob(const QString& meetingId, quint64 dataGen, QVector<TagSuggestion> list)
{
    auto run = d->tagRuns.find(meetingId);
    if (run == d->tagRuns.end()) return;
    run->running = false;
    if (dataGen != d->tagDataGen) {
        // The tags / transcript / index changed meanwhile: the result is stale.
        if (run->publish || run->llm) scheduleTagSuggestions(meetingId);
        else d->tagRuns.erase(run);
        return;
    }
    const Impl::TagRun done = *run;
    d->tagRuns.erase(run);
    d->similarCache.insert(meetingId, list);
    if (done.publish) publishTagSuggestions(meetingId, list);
    if (done.llm) startLlmTagSuggestions(meetingId, list);
}

void AppController::requestTagSuggestions(const QString& meetingId)
{
    if (meetingId.isEmpty()) return;
    emit tagSuggestionsComputing(meetingId);
    if (!d->settings->settings().tagSuggestions) {
        publishTagSuggestions(meetingId, {});
        return;
    }
    if (d->similarCache.contains(meetingId)) {
        computeTagSuggestions(meetingId);
        return;
    }
    d->tagRuns[meetingId].publish = true;
    if (!d->tagRuns.value(meetingId).running) scheduleTagSuggestions(meetingId);
}

void AppController::requestCooccurSuggestions(const QString& meetingId, const QString& tagId)
{
    if (!d->settings->settings().tagSuggestions) return;
    const library::TagSuggester suggester(d->tags, d->profiles, d->embeddings);
    const QVector<TagSuggestion> list = suggester.cooccur(meetingId, tagId);
    if (list.isEmpty()) return;
    emit tagSuggestionsComputing(meetingId);
    publishTagSuggestions(meetingId, list);
}

void AppController::requestLlmTagSuggestions(const QString& meetingId)
{
    const AppSettings s = d->settings->settings();
    if (!s.tagSuggestions) return;
    const library::SummaryDocument doc = summaryDocument(meetingId);
    if (!doc.exists || doc.markdown.trimmed().isEmpty()) return;
    emit tagSuggestionsComputing(meetingId);
    // The candidate list (similarity-based suggestions) from the cache, or made on a background thread.
    if (d->similarCache.contains(meetingId)) {
        startLlmTagSuggestions(meetingId, d->similarCache.value(meetingId));
        return;
    }
    d->tagRuns[meetingId].llm = true;
    if (!d->tagRuns.value(meetingId).running) scheduleTagSuggestions(meetingId);
}

void AppController::startLlmTagSuggestions(const QString& meetingId, const QVector<TagSuggestion>& suggested)
{
    const AppSettings s = d->settings->settings();
    const library::SummaryDocument doc = summaryDocument(meetingId);
    if (!s.tagSuggestions || !doc.exists || doc.markdown.trimmed().isEmpty()) {
        publishTagSuggestions(meetingId, d->pendingTags.value(meetingId));
        return;
    }
    // One LLM suggestion runs at a time (the local model works on one thread).
    if (d->llmTags->isRunning()) {
        const QString prev = d->llmTagsMeetingId;
        d->llmTags->cancel();
        if (d->llmTagsProvider) d->llmTagsProvider->deleteLater();
        d->llmTagsProvider = nullptr;
        d->llmTagsRun.reset();
        if (!prev.isEmpty() && prev != meetingId) publishTagSuggestions(prev, d->pendingTags.value(prev));
    }
    CloudRunPtr run;
    const ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("tags"), QStringLiteral("quick"));
    providers::ILlmProvider* provider = providers::LlmProviderRegistry::instance().create(s.llmProviderId, cfg, this);
    if (!provider) {
        publishTagSuggestions(meetingId, d->pendingTags.value(meetingId));
        return;
    }
    d->llmTagsProvider = dynamic_cast<QObject*>(provider);
    d->llmTagsRun = run;
    d->llmTagsMeetingId = meetingId;
    d->llmTags->start(provider, cfg.model, processing::promptDefault(QStringLiteral("tags"), s.metadataDir),
                      meetingId, doc.markdown, suggested);
}

QVector<TagSuggestion> AppController::pendingTagSuggestions(const QString& meetingId) const
{
    return d->pendingTags.value(meetingId);
}

QVector<TagSuggestion> AppController::draftTagSuggestions(const QString& title) const
{
    if (!d->settings->settings().tagSuggestions) return {};
    const library::TagSuggester suggester(d->tags, d->profiles, d->embeddings);
    return suggester.suggestForDraft(title);
}

void AppController::setRecordingTags(const QStringList& tagIds)
{
    d->recordingTagIds.clear();
    for (const QString& id : tagIds)
        if (!id.isEmpty() && !d->recordingTagIds.contains(id)) d->recordingTagIds << id;
}

QStringList AppController::recordingTags() const { return d->recordingTagIds; }

void AppController::setEmbeddingProvider(const QString& providerId, const ProviderConfig& config)
{
    AppSettings s = d->settings->settings();
    s.embeddingProviderId = providerId;
    if (!providerId.isEmpty()) {
        ProviderConfig cfg = config;
        cfg.type = providerId;
        cfg.apiKey.clear();   // the key belongs in the KeyStore (setSecret), not in the settings
        s.embeddingConfigs.insert(providerId, cfg);
    }
    d->settings->setSettings(s);   // settingsChanged → applyEmbeddingSettings(true)
}

void AppController::applyEmbeddingSettings(bool restart)
{
    const AppSettings s = d->settings->settings();
    const QString id = providers::EmbeddingProviderRegistry::instance().has(s.embeddingProviderId)
        ? s.embeddingProviderId : QString();
    ProviderConfig base = s.embeddingSelected();
    // The Tanara Cloud variant has no model field: the gateway's default model.
    const QString model = id.isEmpty() ? QString()
        : (!base.model.trimmed().isEmpty() ? base.model.trimmed()
                                           : (id == providers::embeddingproviders::CloudId ? QStringLiteral("default") : QString()));
    const QString key = id.isEmpty() || model.isEmpty()
        ? QString() : id + QLatin1Char('|') + base.baseUrl.trimmed() + QLatin1Char('|') + model;
    if (key == d->embeddingKey && restart) return;   // another setting changed
    const QString oldModel = d->embeddingPreparer->state().model;
    const bool hadProvider = !d->embeddingKey.isEmpty();
    d->embeddingKey = key;

    library::EmbeddingPreparer::ProviderFactory factory;
    if (!key.isEmpty()) {
        factory = [this, id](QObject* parent) -> providers::IEmbeddingProvider* {
            const AppSettings cur = d->settings->settings();
            ProviderConfig cfg = cur.embeddingConfigs.value(id);
            cfg.type = id;
            if (id == providers::embeddingproviders::CloudId) {
                // The same sign-in and header set as on the LLM path (/v1/embeddings).
                cfg.baseUrl = d->cloud->apiBase();
                cfg.apiKey = d->cloud->apiKey();
                cfg.extraHeaders = d->cloud->requestHeaders(QUuid::createUuid().toString(QUuid::WithoutBraces));
                QPointer<cloud::CloudAccount> acc = d->cloud;
                cfg.onExchange = [acc](const cloud::HttpExchange& ex) { if (acc) acc->observeExchange(ex); };
            } else {
                cfg.apiKey = d->keyStore.get(providers::embeddingproviders::ApiKeySecret);
            }
            return providers::EmbeddingProviderRegistry::instance().create(id, cfg, parent);
        };
    }
    // A model change: the old embeddings cannot be compared to the new ones, so start over.
    if (restart && hadProvider && !key.isEmpty() && oldModel != model)
        d->embeddings->invalidateAll();
    d->embeddingPreparer->setProvider(key.isEmpty() ? QString() : id, model, factory);
    if (restart && !key.isEmpty()) d->embeddingPreparer->start();
}

library::SummaryDocument AppController::summaryDocument(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return {};
    return library::summarystore::load(m.folder);
}

QVector<SummaryTopic> AppController::meetingTopics(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return {};
    return readTopicsJson(QDir(m.folder).filePath(QStringLiteral("summary.topics.json")));
}

QVector<TopicStatus> AppController::topicStatuses(const QString& meetingId) const
{
    QVector<TopicStatus> out;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return out;
    const QVector<SummaryTopic> topics =
        readTopicsJson(QDir(m.folder).filePath(QStringLiteral("summary.topics.json")));
    QSet<QString> done;
    for (const TopicAnalysis& a : readAnalysesJson(QDir(m.folder).filePath(QStringLiteral("summary.analyses.json"))))
        done.insert(a.topicId);
    const QHash<QString, JobError> errors = d->jobs->topicErrors(meetingId);

    out.reserve(topics.size());
    for (const SummaryTopic& t : topics) {
        TopicStatus st;
        st.topicId = t.id;
        const bool running = d->topicJobActive && d->activeTopicMeetingId == meetingId
                             && d->activeTopicId == t.id;
        const bool queued = std::any_of(d->topicJobQueue.cbegin(), d->topicJobQueue.cend(),
            [&](const Impl::TopicJob& j) { return j.meetingId == meetingId && j.topic.id == t.id; });
        if (running) {
            st.state = TopicState::Running;
        } else if (queued) {
            st.state = TopicState::Queued;
        } else if (errors.contains(t.id)) {
            // The LAST attempt failed (this shows even if a finished analysis exists from earlier; that stays on
            // disk; a new successful run or a retry clears the error).
            st.state = TopicState::Failed;
            st.error = errors.value(t.id).message;
            st.errorDetail = errors.value(t.id).detail;
            st.fixActionHint = errors.value(t.id).fixActionHint;
        } else if (done.contains(t.id)) {
            st.state = TopicState::Done;
        }
        out.append(st);
    }
    return out;
}

QVector<SummaryTopic> AppController::setMeetingTopics(const QString& meetingId,
                                                      const QVector<SummaryTopic>& topics)
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return {}; }

    QVector<SummaryTopic> saved;
    QSet<QString> ids;
    for (SummaryTopic t : topics) {
        t.title = t.title.trimmed();
        t.summary = t.summary.trimmed();
        if (t.title.isEmpty()) continue;                      // a topic without a title cannot be saved
        if (t.id.isEmpty() || ids.contains(t.id))             // a new (or duplicate-id) topic
            t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ids.insert(t.id);
        saved.append(t);
    }
    const QString topicsPath = QDir(m.folder).filePath(QStringLiteral("summary.topics.json"));
    // The deleted topics: out of the queue / the running one cancelled, their remembered error cleared.
    for (const SummaryTopic& old : readTopicsJson(topicsPath)) {
        if (ids.contains(old.id)) continue;
        cancelTopicAnalysis(meetingId, old.id);
        d->jobs->clearTopicError(meetingId, old.id);
    }
    writeTopicsJson(topicsPath, saved);
    emit topicsChanged(meetingId);
    return saved;
}

bool AppController::cancelTopicAnalysis(const QString& meetingId, const QString& topicId)
{
    bool any = false;
    for (int i = d->topicJobQueue.size() - 1; i >= 0; --i) {
        const Impl::TopicJob& j = d->topicJobQueue.at(i);
        if (j.meetingId != meetingId || j.topic.id != topicId) continue;
        d->topicJobQueue.removeAt(i);
        if (d->topicBatchTotal.value(meetingId) > 0) d->topicBatchTotal[meetingId]--;
        any = true;
    }
    const bool active = d->topicJobActive && d->activeTopicMeetingId == meetingId
                        && d->activeTopicId == topicId;
    if (active) {
        d->abortLlmRun(d->activeTopicRun);
        d->activeTopicRun = {};
        d->activeTopicId.clear();
        if (d->topicBatchTotal.value(meetingId) > 0) d->topicBatchTotal[meetingId]--;
        any = true;
    }
    if (!any) return false;
    // After removing a topic the batch is no longer complete, so there is no automatic final summary.
    d->reduceWhenDone.remove(meetingId);
    d->updateTopicCounts(meetingId);
    emit topicAnalysisFailed(meetingId, topicId, tr("Megszakítva"));
    emit topicStatusChanged(meetingId, topicId);
    if (active)
        startNextTopicJob();   // the queue goes on (or closes)
    return true;
}

void AppController::requestLlmContext(int tokens)
{
    if (tokens <= 0 || usesCloud(providers::WorkflowStep::Summarize)) return;
    const ProviderConfig cfg = d->settings->settings().llmSelected();
    d->contextFloor.insert(Impl::serverKey(cfg), tokens);
}

bool AppController::cancelJob(const QString& meetingId, JobKind kind)
{
    switch (kind) {
    case JobKind::Transcribe: {
        auto it = d->transcribeRuns.find(meetingId);
        if (it == d->transcribeRuns.end()) {
            // The transcript is already done, the last (identification) stage is running, so only that one is skipped.
            const auto ir = d->identifyRuns.constFind(meetingId);
            if (ir == d->identifyRuns.constEnd() || !ir->asStage) return false;
            ir->cancel->store(true);
            d->jobs->setCancelling(meetingId, kind);
            return true;
        }
        d->jobs->setCancelling(meetingId, kind);
        if (it->mixPhase) {
            // Mixdown phase: we break the chain; the ffmpeg we started ourselves is stopped (the half-made file is
            // deleted, the earlier mix and the tracks are untouched).
            if (it->mixConn) QObject::disconnect(*it->mixConn);
            const bool owns = it->ownsMixdown;
            d->transcribeRuns.erase(it);
            d->clearSpeakersOnTranscript.remove(meetingId);
            if (owns) {
                if (QProcess* proc = d->mixdownProcs.value(meetingId)) {
                    d->mixdownCancelled.insert(meetingId);
                    proc->kill();
                }
            }
            d->jobs->cancelled(meetingId, kind);
            return true;
        }
        if (it->job) {
            it->cancelRequested = true;
            it->job->cancel();   // a synchronous failed("cancelled"): the failed branch closes it (without an error)
            return true;
        }
        // (In principle it does not occur: neither a mix nor a job.)
        d->transcribeRuns.erase(it);
        d->clearSpeakersOnTranscript.remove(meetingId);
        d->jobs->cancelled(meetingId, kind);
        return true;
    }
    case JobKind::Summarize:
    case JobKind::ExtractTopics: {
        const QString key = Impl::llmKey(meetingId, kind);
        const auto it = d->llmRuns.find(key);
        if (it == d->llmRuns.end()) return false;
        const Impl::LlmRun run = *it;
        d->llmRuns.erase(it);
        d->abortLlmRun(run);
        // Cloud: there is no charge for the cancelled call; the run's charges so far (e.g. topic gathering +
        // analyses) go summed to the cost feedback.
        if (run.cloudRun && !run.cloudRun->lastError.isError())
            finishCloudRun(run.cloudRun);
        d->jobs->cancelled(meetingId, kind);
        return true;
    }
    case JobKind::AnalyzeTopics: {
        bool any = false;
        for (int i = d->topicJobQueue.size() - 1; i >= 0; --i) {
            if (d->topicJobQueue.at(i).meetingId != meetingId) continue;
            const QString dropped = d->topicJobQueue.takeAt(i).topic.id;
            emit topicAnalysisFailed(meetingId, dropped, tr("Megszakítva"));
            emit topicStatusChanged(meetingId, dropped);
            any = true;
        }
        const bool activeMine = d->topicJobActive && d->activeTopicMeetingId == meetingId
                                && !d->activeTopicId.isEmpty();
        if (activeMine) {
            const QString tid = d->activeTopicId;
            d->abortLlmRun(d->activeTopicRun);
            d->activeTopicRun = {};
            d->activeTopicId.clear();
            emit topicAnalysisFailed(meetingId, tid, tr("Megszakítva"));
            emit topicStatusChanged(meetingId, tid);
            any = true;
        }
        if (!any && !d->jobs->isRunning(meetingId, kind)) return false;
        // Closing the batch: no auto-summary, no "N topics failed" message; the analyses already finished stay on
        // disk, a continuation picks up from there.
        const QPair<int, int> counts = d->jobCounts.take(meetingId);
        d->reduceWhenDone.remove(meetingId);
        d->topicBatchTotal.remove(meetingId);
        const CloudRunPtr run = d->complexRuns.take(meetingId);
        if (run && !run->lastError.isError())
            finishCloudRun(run);
        emit topicAnalysisQueueFinished(meetingId, counts.first, counts.second);
        d->jobs->cancelled(meetingId, kind);
        if (activeMine)
            startNextTopicJob();   // other meetings' topics go on
        return true;
    }
    case JobKind::Mixdown: {
        QProcess* proc = d->mixdownProcs.value(meetingId);
        if (!proc || !d->jobs->isRunning(meetingId, kind)) return false;
        d->jobs->setCancelling(meetingId, kind);
        d->mixdownCancelled.insert(meetingId);
        proc->kill();   // the finished branch closes: the half-made file is deleted, the old mix stays
        return true;
    }
    case JobKind::Import: {
        if (d->importer->currentId() != meetingId || !d->jobs->isRunning(meetingId, kind)) return false;
        d->jobs->setCancelling(meetingId, kind);
        d->importer->cancel();   // the cancelled branch closes: the half-made folder is deleted
        return true;
    }
    case JobKind::Export: {
        if (d->archiveOp != QStringLiteral("export:") + meetingId || !d->archiveCancel
            || !d->jobs->isRunning(meetingId, kind))
            return false;
        d->jobs->setCancelling(meetingId, kind);
        d->archiveCancel->store(true);   // the thread stops at the next block, the half-made file is deleted
        return true;
    }
    case JobKind::Identify: {
        const auto ir = d->identifyRuns.constFind(meetingId);
        if (ir == d->identifyRuns.constEnd() || ir->asStage) return false;
        ir->cancel->store(true);   // the thread stops before the next speaker
        d->jobs->setCancelling(meetingId, kind);
        return true;
    }
    }
    return false;
}

void AppController::cancelAllJobs(const QString& meetingId)
{
    for (JobKind k : {JobKind::Transcribe, JobKind::Summarize, JobKind::ExtractTopics,
                      JobKind::AnalyzeTopics, JobKind::Mixdown, JobKind::Identify})
        cancelJob(meetingId, k);
}

void AppController::setIdentifyAfterTranscription(const QString& meetingId, bool enabled)
{
    if (enabled) d->skipIdentifyAfterTranscribe.remove(meetingId);
    else         d->skipIdentifyAfterTranscribe.insert(meetingId);
}

bool AppController::identifyAfterTranscription(const QString& meetingId) const
{
    return !d->skipIdentifyAfterTranscribe.contains(meetingId);
}

bool AppController::voiceIdentificationAvailable() const
{
    return d->voiceModelUsable();
}

bool AppController::identifyMeetingAsync(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::Transcribe))
        return false;   // the transcription identifies by itself at the end
    return startIdentify(meetingId, /*asTranscribeStage*/ false);
}

bool AppController::startIdentify(const QString& meetingId, bool asStage)
{
    if (d->identifyRuns.contains(meetingId) || !d->voiceModelUsable())
        return false;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return false;
    const MergedTranscript merged =
        readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) return false;

    // The still anonymous (unmapped) raw speaker labels and their audio source.
    struct Item { QString label; QString audioPath; qint64 offsetMs = 0; };
    QVector<Item> items;
    QStringList seen;
    for (const TranscriptToken& t : merged.tokens) {
        if (t.speaker.isEmpty() || seen.contains(t.speaker)) continue;
        seen << t.speaker;
        if (m.speakerMap.contains(t.speaker)) continue;
        const VoiceSource src = resolveVoiceSource(m, merged, t.speaker);
        if (!src.absPath.isEmpty()) items.append({t.speaker, src.absPath, src.offsetMs});
    }
    if (items.isEmpty()) {
        d->jobs->markIdentified(meetingId);   // no anonymous speaker, nothing to identify
        return false;
    }

    const int total = int(items.size());
    const JobKind kind = asStage ? JobKind::Transcribe : JobKind::Identify;
    if (asStage) {
        d->jobs->setStage(meetingId, kind, QStringLiteral("identify"), StageState::Running, 0,
                          tr("%1 / %2 beszélő").arg(0).arg(total));
    } else {
        d->jobs->begin(meetingId, kind, tr("Résztvevők azonosítása"));
        d->jobs->setCounts(meetingId, kind, 0, total);
    }

    // Decoding (ffmpeg) + embedding (ONNX) run on a background thread, with its OWN embedder instance (it
    // does not touch the main thread's embedder). Matching against the voiceprint DB and saving happen on the
    // main thread (applyIdentification): only the main thread reaches the DB.
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    auto results = std::make_shared<QVector<QPair<QString, EmbeddingSet>>>();
    const processing::VoiceEmbedderSet proto = d->voiceSet.freshCopy();
    QPointer<AppController> self(this);
    QThread* th = QThread::create([self, items, merged, proto, cancel, results, meetingId,
                                   total, kind, asStage]() {
        const processing::VoiceEmbedderSet emb = proto.freshCopy();   // the thread's own model instances
        if (!emb.ensureLoaded()) return;
        int done = 0;
        for (const Item& it : items) {
            if (cancel->load()) break;
            const EmbeddingSet e = embeddingForLabel(emb, it.audioPath, it.offsetMs, merged, it.label);
            ++done;
            // The result + real count progress back to the main thread.
            QMetaObject::invokeMethod(qApp, [self, results, label = it.label, e, meetingId,
                                             done, total, kind, asStage]() {
                if (!self) return;
                results->append({label, e});
                if (asStage) {
                    self->d->jobs->setStage(meetingId, kind, QStringLiteral("identify"),
                                            StageState::Running, done * 100 / total,
                                            AppController::tr("%1 / %2 beszélő").arg(done).arg(total));
                } else {
                    self->d->jobs->setCounts(meetingId, kind, done, total);
                }
            }, Qt::QueuedConnection);
        }
    });
    d->identifyRuns.insert(meetingId, Impl::IdentifyRun{th, cancel, asStage});
    const QString stampAtStart = d->transcriptStamp(meetingId, m.folder);
    connect(th, &QThread::finished, this,
            [this, th, meetingId, cancel, results, asStage, kind, stampAtStart, folder = m.folder]() {
        d->identifyRuns.remove(meetingId);
        th->deleteLater();
        // If the transcript changed during the run (a new transcription arrived), the embeddings belong to the
        // OLD transcript's labels: we do not write the names onto the new transcript; it counts as cancelled.
        // (Otherwise: even on cancel we save what was found so far.)
        const bool stale = d->transcriptStamp(meetingId, folder) != stampAtStart;
        const bool wasCancelled = cancel->load() || stale;
        if (!stale)
            d->applyIdentification(meetingId, *results);
        if (!wasCancelled)
            d->jobs->markIdentified(meetingId);
        if (asStage) {
            d->jobs->setStage(meetingId, kind, QStringLiteral("identify"),
                              wasCancelled ? StageState::Skipped : StageState::Done);
            d->jobs->finish(meetingId, kind);   // the transcript is done; the task closes successfully
        } else if (wasCancelled) {
            d->jobs->cancelled(meetingId, kind);
        } else {
            d->jobs->finish(meetingId, kind);
        }
    });
    th->start();
    return true;
}

void AppController::requestWaveforms(const QString& meetingId)
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    for (const Track& t : m.tracks) {
        const QString path = QDir(m.folder).filePath(t.file);
        if (!t.file.isEmpty() && QFile::exists(path))
            d->waveforms->request(meetingId, t.id, path);
    }
    if (!m.mixdownFile.isEmpty()) {
        const QString mix = QDir(m.folder).filePath(m.mixdownFile);
        if (QFile::exists(mix))
            d->waveforms->request(meetingId, QStringLiteral("mixdown"), mix);
    }
}

// ---- transcript editor (speaker correction) ----------------------------------

editing::SpeakerEditor* AppController::speakerEditor(const QString& meetingId)
{
    if (editing::SpeakerEditor* existing = d->speakerEditors.value(meetingId)) return existing;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return nullptr;

    auto* ed = new editing::SpeakerEditor(d->store, d->people.get(), d->voiceprints.get(), meetingId, this);
    ed->setUserSpeakerName(d->settings->settings().userSpeakerName);
    // Without voice ID (no model / TANARA_BUILD_VOICEID=OFF) the editor is fully functional even without
    // uncertainty and suggestions.
    ed->setVoiceModelIds(d->editorModelIds(d->settings));
    ed->setEmbedderFactory(utteranceEmbedderFactory());
    // The tag evidence for the candidate ranking (manual tag + learned association).
    ed->setTagEvidenceProvider(d->candidateContext().tagEvidence);
    // Manual tags for the quick "+ #Tag" offer (the test seam, otherwise the TagService).
    {
        library::TagService* ts = d->tags;
        ed->setPersonTagsProvider(d->personTags ? d->personTags
                                                : [ts](const QString& n) { return ts ? ts->tagsOfPerson(n) : QStringList(); });
    }
    d->speakerEditors.insert(meetingId, ed);
    meetingUndo(meetingId);   // the shared undo follows from the editor's first step

    // The editor's side effects go out on the usual signals (old UI, library).
    connect(ed, &editing::SpeakerEditor::speakerMapChanged, this, [this](const QString& id) {
        d->editorEmitting = true;
        emit speakerMapChanged(id);
        d->editorEmitting = false;
    });
    connect(ed, &editing::SpeakerEditor::peopleChanged, this, &AppController::peopleChanged);
    connect(ed, &editing::SpeakerEditor::voiceprintsChanged, this, &AppController::voiceprintsChanged);
    connect(ed, &editing::SpeakerEditor::summaryStaleChanged, this,
            [this, meetingId] { emit summaryStaleChanged(meetingId); });
    // A change from outside (old UI rename, auto-identification, person rename) -> the editor rereads the disk.
    connect(this, &AppController::speakerMapChanged, ed, [this, ed](const QString& id) {
        if (!d->editorEmitting && id == ed->meetingId()) ed->refreshFromDisk();
    });
    connect(this, &AppController::voiceprintsChanged, ed, &editing::SpeakerEditor::speakersChanged);
    return ed;
}

void AppController::closeSpeakerEditor(const QString& meetingId)
{
    if (editing::SpeakerEditor* ed = d->speakerEditors.take(meetingId)) ed->deleteLater();
}

QVector<editing::PersonInfo> AppController::peopleDirectory() const
{
    if (d->people) d->people->refresh();
    return editing::listPeople(d->people.get(), d->voiceprints.get(), d->store);
}

SummaryStaleInfo AppController::summaryStale(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId);
    SummaryStaleInfo info = ed ? ed->summaryStale() : editing::speakeredit::summaryStale(m);
    // The editor persists after every step, so the targeted part can be computed from disk.
    if (!m.id.isEmpty()) editing::speakeredit::fillTargetedStale(m, info);
    return info;
}

QVector<SummaryStatement> AppController::summaryStatements(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return {};
    SummaryStaleInfo info;
    QVector<SummaryStatement> out;
    editing::speakeredit::fillTargetedStale(m, info, &out);
    return out;
}

bool AppController::flagStatement(const QString& meetingId, const QString& statementId)
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || m.folder.isEmpty()) return false;
    if (!library::summarystore::setStatementFlagged(m.folder, statementId, true)) return false;
    emit summaryStatementsChanged(meetingId);
    return true;
}

// The same for an already loaded meeting (the library calls it per row; no further disk read for meeting.json).
SummaryStaleInfo AppController::summaryStale(const Meeting& meeting) const
{
    if (editing::SpeakerEditor* ed = d->speakerEditors.value(meeting.id)) return ed->summaryStale();
    return editing::speakeredit::summaryStale(meeting);
}

editing::RetranscribeImpact AppController::retranscribeImpact(const QString& meetingId) const
{
    if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) return ed->retranscribeImpact();
    return editing::speakeredit::retranscribeImpact(d->store->load(meetingId));
}

void AppController::dismissSummaryStale(const QString& meetingId)
{
    if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) {
        ed->dismissSummaryStale();   // summaryStaleChanged goes out from the editor's signal
        return;
    }
    const Meeting m = d->store->load(meetingId);
    if (!m.folder.isEmpty() && editing::speakeredit::clearSummaryStale(m.folder))
        emit summaryStaleChanged(meetingId);
}


// ---- participants ("Who was there?") and per-track speech share ----------------------------------------
// The pure logic: editing/session/TrackSpeech.h, processing/analysis/ParticipantAnalysis.h. Here only the threading, saving and
// binding through the editor.

processing::participants::CandidateContext AppController::Impl::candidateContext() const
{
    processing::participants::CandidateContext ctx;
    ctx.selfName = settings->settings().userSpeakerName.trimmed();
    library::VoiceprintStore* vp = voiceprints.get();
    ctx.rank = [vp](const EmbeddingSet& set) {
        return vp ? vp->rankedMatches(set, set.keys()) : QVector<VoiceMatch>();
    };
    ctx.hasVoiceprint = [vp](const QString& name) { return vp && vp->printCount(name) > 0; };
    library::TagService* ts = tags;
    // The tag evidence (UX plan rule 4): the TagService snapshot, manual tag and learned association; the
    // configured seam (tests) overrides the manual tags.
    ctx.tagEvidence = [ts, override = personTags](const QString& name, const QStringList& meetingTags,
                                                  const QString& meetingId) {
        if (!ts) return library::TagEvidence();
        library::PersonTagSnapshot snap = ts->personTagSnapshot();
        if (override) snap.manual.insert(library::persontags::personKey(name), override(name));
        return snap.evidence(name, meetingTags, meetingId);
    };
    return ctx;
}

void AppController::setPersonTagsProvider(std::function<QStringList(const QString&)> provider)
{
    d->personTags = std::move(provider);
}

QVector<Participant> AppController::participants(const QString& meetingId) const
{
    return d->store->load(meetingId).participants;
}

bool AppController::participantAnalysisRunning(const QString& meetingId) const
{
    return d->analysisRuns.contains(meetingId);
}

bool AppController::participantApprovalPending(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || m.approval || d->analysisRuns.contains(meetingId)) return false;
    for (const Participant& p : m.participants)
        if (p.source != ParticipantSource::Manual || !p.sides.isEmpty()) return true;
    return false;
}

void AppController::startSpeechCheck(const QString& meetingId, bool thenMixdown)
{
    if (d->speechCheckRunning.contains(meetingId)) return;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    d->speechCheckRunning.insert(meetingId);
    const QString folder = m.folder;
    const auto stop = d->analysisShutdown;
    QPointer<AppController> self(this);
    d->analysisPool.start([self, m, folder, stop, meetingId, thenMixdown]() {
        if (stop->load()) return;
        const QString fp = processing::trackactivity::activityFingerprint(m, folder);
        MeetingActivity act = MeetingActivity::load(folder, fp);
        if (act.isEmpty()) {
            act = processing::computeMeetingActivity(m, folder);
            if (!act.isEmpty()) act.save(folder);
        }
        QMetaObject::invokeMethod(qApp, [self, meetingId, act, thenMixdown]() {
            if (self) self->finishSpeechCheck(meetingId, act, thenMixdown);
        }, Qt::QueuedConnection);
    });
}

void AppController::finishSpeechCheck(const QString& meetingId, const MeetingActivity& activity,
                                      bool thenMixdown)
{
    d->speechCheckRunning.remove(meetingId);
    d->speechChecked.insert(meetingId);   // on error (an undecodable track) we do not repeat either
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    const editing::trackspeech::SpeechCheckResult r = editing::trackspeech::applySpeechRatios(m, activity);
    if (r.changed()) {
        if (!r.excluded.isEmpty()) {
            m.mixdownDirty = true;
            qCInfo(lcAudio).noquote() << "Beszéd nélküli sáv kimarad a lekeverésből:"
                                      << r.excluded.join(QStringLiteral(", "));
            // The full measurement becomes the speech strip's cache (so the excluded track shows too and need not be
            // decoded again); the analysis's cache is that of the included tracks.
            activity.save(m.folder, MeetingActivity::viewFilePath(m.folder));
            // The track-activity cache belongs to the included tracks: we save it again without the excluded ones.
            MeetingActivity kept;
            kept.fingerprint = processing::trackactivity::activityFingerprint(m, m.folder);
            for (const TrackActivity& t : activity.tracks)
                if (!r.excluded.contains(t.trackId)) kept.tracks.append(t);
            if (!kept.isEmpty()) kept.save(m.folder);
        }
        d->store->saveMeeting(m);
    }
    // We signal even if nothing changed: the view leaves the "measuring" state only on this.
    emit tracksChanged(meetingId);
    if (thenMixdown) regenerateMixdown(meetingId);
    // The speechless tracks are filtered out: the voice analysis can go on over the remaining raw tracks.
    if (d->autoMixdown) maybeAnalyzeParticipants(meetingId);
}

bool AppController::speechCheckRunning(const QString& meetingId) const
{
    return d->speechCheckRunning.contains(meetingId);
}

bool AppController::checkTrackSpeech(const QString& meetingId)
{
    if (d->speechCheckRunning.contains(meetingId) || d->speechChecked.contains(meetingId)) return false;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || !editing::trackspeech::needsSpeechCheck(m)) return false;
    startSpeechCheck(meetingId, /*thenMixdown*/ false);
    return true;
}

// ---- listening to tracks: speech strip, speaker -> track binding ---------------------------------

namespace {

// The leading ids of the meeting's included logical tracks on a given side (in track order).
QStringList includedLeaders(const Meeting& m, bool mic, bool loop)
{
    QStringList out;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        const Track& t = m.tracks.at(i);
        if (!t.included()) continue;
        if (!((mic && t.kind == TrackKind::Mic) || (loop && t.kind == TrackKind::Loopback))) continue;
        const QString id = m.tracks.at(lead).id;
        if (!out.contains(id)) out << id;
    }
    return out;
}

// Track ids mapped to the leading ids of the meeting's logical tracks (unknown ones dropped, no repeats).
QStringList normalizeTrackIds(const Meeting& m, const QStringList& trackIds)
{
    QStringList out;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    for (const QString& id : trackIds) {
        for (int i = 0; i < m.tracks.size(); ++i) {
            if (m.tracks.at(i).id != id) continue;
            const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
            const QString leadId = m.tracks.at(lead).id;
            if (!out.contains(leadId)) out << leadId;
            break;
        }
    }
    return out;
}

const Participant* participantById(const Meeting& m, const QString& id)
{
    for (const Participant& p : m.participants)
        if (!id.isEmpty() && p.id == id) return &p;
    return nullptr;
}

} // namespace

MeetingActivity AppController::trackActivity(const QString& meetingId)
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || m.tracks.isEmpty()) return {};
    // The strip shows EVERY track (the excluded one too): the fingerprint treats them all as active.
    Meeting all = m;
    for (Track& t : all.tracks) t.active = true;
    const QString fp = processing::trackactivity::activityFingerprint(all, m.folder);
    if (const auto it = d->activityView.constFind(meetingId);
        it != d->activityView.constEnd() && it->fingerprint == fp)
        return *it;
    MeetingActivity a = MeetingActivity::load(m.folder, fp);   // if every track is included, this one
    if (a.isEmpty()) a = MeetingActivity::load(m.folder, fp, MeetingActivity::viewFilePath(m.folder));
    if (!a.isEmpty()) {
        d->activityView.insert(meetingId, a);
        return a;
    }
    if (!d->activityViewRunning.contains(meetingId)) {
        d->activityViewRunning.insert(meetingId);
        const auto stop = d->analysisShutdown;
        QPointer<AppController> self(this);
        const QString folder = m.folder;
        d->analysisPool.start([self, all, folder, fp, stop, meetingId]() {
            MeetingActivity act;
            act.fingerprint = fp;
            if (!stop->load()) {
                act = processing::computeMeetingActivity(all, folder);
                act.fingerprint = fp;
                if (!act.isEmpty()) act.save(folder, MeetingActivity::viewFilePath(folder));
            }
            QMetaObject::invokeMethod(qApp, [self, meetingId, act]() {
                if (self) self->finishTrackActivity(meetingId, act);
            }, Qt::QueuedConnection);
        });
    }
    MeetingActivity pending;
    pending.fingerprint = fp;
    return pending;
}

bool AppController::trackActivityComputing(const QString& meetingId) const
{
    return d->activityViewRunning.contains(meetingId);
}

void AppController::finishTrackActivity(const QString& meetingId, const MeetingActivity& activity)
{
    d->activityViewRunning.remove(meetingId);
    // An empty result also goes in with its fingerprint: we do not recompute until the tracks change.
    d->activityView.insert(meetingId, activity);
    emit trackActivityReady(meetingId);
}

QStringList AppController::speakerKeysFor(const Meeting& m, const QString& id)
{
    if (!m.hasTranscript || id.isEmpty()) return {};
    editing::SpeakerEditor* ed = speakerEditor(m.id);
    if (!ed || !ed->hasTranscript()) return {};
    const QVector<editing::EditorSpeaker> speakers = ed->speakers();
    const Participant* p = participantById(m, id);
    if (!p) {
        for (const editing::EditorSpeaker& s : speakers)
            if (s.key == id) return {id};
        return {};
    }
    QStringList raws;   // the bound raw speakers ("Speaker 1" or "Speaker 1@mic")
    for (const QString& r : p->rawSpeakerIds) raws << r.section(QLatin1Char('@'), 0, 0);
    QStringList keys;
    for (const editing::EditorSpeaker& s : speakers) {
        const bool byName = !p->personName.isEmpty()
                         && s.personName.compare(p->personName, Qt::CaseInsensitive) == 0;
        const bool byRaw = !s.rawLabel.isEmpty() && raws.contains(s.rawLabel);
        if ((byName || byRaw) && !keys.contains(s.key)) keys << s.key;
    }
    return keys;
}

bool AppController::setParticipantTracks(const QString& meetingId, const QString& id,
                                         const QStringList& trackIds)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || id.isEmpty()) return false;
    const QStringList ids = normalizeTrackIds(m, trackIds);
    int pIdx = -1;
    for (int i = 0; i < m.participants.size(); ++i)
        if (m.participants.at(i).id == id) pIdx = i;
    const QStringList keys = speakerKeysFor(m, id);
    if (pIdx < 0 && !keys.isEmpty()) {
        // Speaker key: the participant belonging to it (if any) gets it too.
        for (int i = 0; i < m.participants.size() && pIdx < 0; ++i)
            if (speakerKeysFor(m, m.participants.at(i).id).contains(id)) pIdx = i;
    }
    if (pIdx < 0 && keys.isEmpty()) return false;
    bool changed = false;
    if (pIdx >= 0 && m.participants.at(pIdx).tracks != ids) {
        Participant& p = m.participants[pIdx];
        p.tracks = ids;
        // The track-side evidence from the manual binding (the voice evidence is unchanged).
        double score = -1.0;
        const bool heard = !p.sides.isEmpty() || p.talkShare > 0.0;
        for (const Evidence& e : p.evidence)
            if (e.kind == EvidenceKind::Voice) score = e.value;
        p.evidence = processing::participants::participantEvidence(p, heard, score, m, d->candidateContext());
        d->store->saveMeeting(m);
        changed = true;
    }
    if (!keys.isEmpty())
        if (editing::SpeakerEditor* ed = speakerEditor(meetingId))
            for (const QString& key : keys)
                changed = ed->setTrackAssignment(key, ids) || changed;
    if (changed) emit participantsChanged(meetingId);
    return changed;
}

ParticipantTrackBinding AppController::participantTracks(const QString& meetingId, const QString& id)
{
    ParticipantTrackBinding b;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return b;
    const Participant* p = participantById(m, id);
    const QStringList keys = speakerKeysFor(m, id);
    editing::SpeakerEditor* ed = keys.isEmpty() ? nullptr : speakerEditor(meetingId);
    // Manual: after the transcript the editor's assignment is the truth (the "Why them?" chips), before it the participant's.
    if (ed) {
        for (const QString& key : keys) {
            const QStringList t = ed->trackAssignment(key);
            if (!t.isEmpty()) {
                b.trackIds = t;
                b.manual = true;
                return b;
            }
        }
    } else if (p && !p->tracks.isEmpty()) {
        b.trackIds = normalizeTrackIds(m, p->tracks);
        b.manual = !b.trackIds.isEmpty();
        if (b.manual) return b;
    }
    // Inferred: the editor's side analysis, otherwise the voice analysis's sides.
    bool mic = false, loop = false;
    if (ed) {
        const editing::SideReport r = ed->sideReport();
        for (const editing::PersonSide& ps : r.persons) {
            if (!keys.contains(ps.speakerKey)) continue;
            mic = mic || ps.side == editing::Side::Local || ps.side == editing::Side::Mixed;
            loop = loop || ps.side == editing::Side::Remote || ps.side == editing::Side::Mixed;
        }
    }
    if (!mic && !loop && p) {
        mic = p->sides.contains(QStringLiteral("mic"));
        loop = p->sides.contains(QStringLiteral("loopback"));
    }
    b.trackIds = includedLeaders(m, mic, loop);
    return b;
}

void AppController::copyParticipantTracksToOverlay(const QString& meetingId)
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || !m.hasTranscript) return;
    editing::SpeakerEditor* ed = speakerEditor(meetingId);
    if (!ed || !ed->hasTranscript()) return;
    for (const Participant& p : m.participants) {
        if (p.tracks.isEmpty()) continue;
        for (const QString& key : speakerKeysFor(m, p.id))
            if (ed->trackAssignment(key).isEmpty()) ed->setTrackAssignment(key, p.tracks);
    }
}

void AppController::includeTrack(const QString& meetingId, const QString& trackId)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Sáv beemelve"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    int lead = -1;
    for (int i = 0; i < m.tracks.size(); ++i)
        if (m.tracks.at(i).id == trackId) lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
    if (lead < 0) return;
    bool changed = false;
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int l = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        Track& t = m.tracks[i];
        if (l != lead || t.included()) continue;
        t.active = true;
        t.excludedReason.clear();
        changed = true;
    }
    if (!changed) return;
    m.mixdownDirty = true;
    d->store->saveMeeting(m);
    emit tracksChanged(meetingId);
    regenerateMixdown(meetingId);
}

void AppController::excludeTrack(const QString& meetingId, const QString& trackId)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Sáv kivéve"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    int lead = -1;
    for (int i = 0; i < m.tracks.size(); ++i)
        if (m.tracks.at(i).id == trackId) lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
    if (lead < 0) return;
    bool changed = false;
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int l = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        Track& t = m.tracks[i];
        if (l != lead || t.excludedReason == editing::trackspeech::kManual) continue;
        t.active = false;
        t.excludedReason = editing::trackspeech::kManual;
        changed = true;
    }
    if (!changed) return;
    m.mixdownDirty = true;
    d->store->saveMeeting(m);
    emit tracksChanged(meetingId);
    bool anyIncluded = false;
    for (const Track& t : m.tracks) anyIncluded = anyIncluded || t.included();
    if (anyIncluded) regenerateMixdown(meetingId);
}

bool AppController::maybeAnalyzeParticipants(const QString& meetingId)
{
    if (!d->voiceModelUsable() || d->analysisRuns.contains(meetingId)) return false;
    if (d->speechCheckRunning.contains(meetingId)) return false;   // the end of the measurement calls again
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || m.tracks.isEmpty() || m.approval.has_value()) return false;
    // The speechless tracks are not filtered out yet: the measurement first (its end calls again).
    if (!m.hasTranscript && editing::trackspeech::needsSpeechCheck(m) && !d->speechChecked.contains(meetingId))
        return false;
    if (processing::ParticipantAnalysisFile::load(m.folder).tracksKey == processing::trackactivity::activityFingerprint(m, m.folder))
        return false;   // it already ran on these same tracks
    return analyzeParticipants(meetingId);
}

bool AppController::analyzeParticipants(const QString& meetingId)
{
    if (d->analysisRuns.contains(meetingId)) return false;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || !d->voiceModelUsable()) return false;
    if (m.approval && m.approval->solo) return false;   // "only me": nothing to identify

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    d->analysisRuns.insert(meetingId, cancel);
    emit participantAnalysisStarted(meetingId);
    const processing::VoiceEmbedderSet proto = d->voiceSet.freshCopy();
    const auto stop = d->analysisShutdown;
    QPointer<AppController> self(this);
    d->analysisPool.start([self, m, proto, cancel, stop, meetingId]() {
        const QString fp = processing::trackactivity::activityFingerprint(m, m.folder);
        ClusterSet cs;
        if (!stop->load() && !cancel->load()) {
            MeetingActivity act = MeetingActivity::load(m.folder, fp);
            if (act.isEmpty()) {
                act = processing::computeMeetingActivity(m, m.folder);
                if (!act.isEmpty()) act.save(m.folder);
            }
            const processing::VoiceEmbedderSet emb = proto.freshCopy();   // the thread's own model instances
            // Progress: from the window index the track (k / N, only tracks with speech) and the percentage; a signal
            // goes to the main thread only on change.
            QVector<int> ends;   // cumulative window count of the tracks with speech
            int windows = 0;
            for (const TrackActivity& ta : act.tracks) {
                const int n = int(processing::participants::speechWindows(ta, QString()).size());
                if (n == 0) continue;
                windows += n;
                ends.append(windows);
            }
            auto last = std::make_shared<QPair<int, int>>(-1, -1);
            auto report = [self, meetingId, ends, last](int done, int total) {
                if (total <= 0 || ends.isEmpty()) return;
                int track = 1;
                while (track < ends.size() && done > ends.at(track - 1)) ++track;
                const int pct = qBound(0, done * 100 / total, 100);
                if (last->first == track && last->second == pct) return;
                *last = {track, pct};
                const int tracks = int(ends.size());
                QMetaObject::invokeMethod(qApp, [self, meetingId, track, tracks, pct]() {
                    if (self && self->d->analysisRuns.contains(meetingId))
                        emit self->participantAnalysisProgress(meetingId, track, tracks, pct);
                }, Qt::QueuedConnection);
            };
            cs = processing::participants::computeVoiceClusters(
                m, act, emb, processing::participants::filePcmReader(m),
                [cancel, stop, report](int done, int total) {
                    report(done, total);
                    return !cancel->load() && !stop->load();
                });
        } else {
            cs.cancelled = true;
        }
        QMetaObject::invokeMethod(qApp, [self, meetingId, cs, fp]() {
            if (self) self->finishParticipantAnalysis(meetingId, cs, fp);
        }, Qt::QueuedConnection);
    });
    return true;
}

void AppController::finishParticipantAnalysis(const QString& meetingId, ClusterSet clusters,
                                              const QString& tracksKey)
{
    d->analysisRuns.remove(meetingId);
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    if (clusters.cancelled || !clusters.error.isEmpty()) {
        if (!clusters.error.isEmpty())
            qCInfo(lcVoice).noquote() << "Résztvevő-elemzés nem futott:" << clusters.error;
        emit participantAnalysisFinished(meetingId);
        return;
    }
    if (m.approval && m.approval->solo) {   // it became "only me" meanwhile: the analysis result is not needed
        emit participantAnalysisFinished(meetingId);
        return;
    }
    m.participants = processing::participants::buildParticipants(clusters, m, d->candidateContext());

    processing::ParticipantAnalysisFile file;
    file.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    file.modelIds = clusters.modelIds;
    file.tracksKey = tracksKey;
    file.clusters = clusters.clusters;
    if (!file.save(m.folder))
        qCWarning(lcVoice).noquote() << "Résztvevő-elemzés: nem írható:" << processing::ParticipantAnalysisFile::filePath(m.folder);

    // If the transcript already exists: the binding runs now (otherwise when the transcript arrives).
    const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(m.folder);
    if (!lines.isEmpty()) processing::participants::bindRawSpeakers(m.participants, file.clusters, lines);
    d->store->saveMeeting(m);
    qCInfo(lcVoice).noquote() << "Résztvevő-elemzés kész:" << m.participants.size() << "jelölt,"
                              << clusters.clusters.size() << "klaszter," << clusters.windows << "ablak";
    emit participantsChanged(meetingId);
    emit participantAnalysisFinished(meetingId);
}

bool AppController::bindRawSpeakers(const QString& meetingId)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || m.participants.isEmpty()) return false;
    const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(m.folder);
    const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(m.folder);
    if (file.isEmpty() || lines.isEmpty()) return false;
    if (!processing::participants::bindRawSpeakers(m.participants, file.clusters, lines)) return false;
    d->store->saveMeeting(m);
    emit participantsChanged(meetingId);
    return true;
}

bool AppController::applySpeakerBindings(const QString& meetingId, const QVector<SpeakerBinding>& bindings,
                                         const QString& undoText)
{
    if (bindings.isEmpty()) return false;
    editing::SpeakerEditor* ed = speakerEditor(meetingId);
    if (!ed || !ed->hasTranscript()) return false;
    return ed->applyBindings(bindings, undoText);
}

bool AppController::applyParticipantsToTranscript(const QString& meetingId)
{
    bindRawSpeakers(meetingId);
    // A manual track binding set before the transcript is carried into the speakers' assignment (one piece of data).
    copyParticipantTracksToOverlay(meetingId);
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || !m.approval || m.approval->skipped) return false;
    const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(m.folder);
    if (lines.isEmpty()) return false;
    QVector<SpeakerBinding> bindings;
    if (m.approval->solo) {
        const QString self = d->settings->settings().userSpeakerName.trimmed();
        if (self.isEmpty()) return false;
        QStringList raws;
        for (const TranscriptLine& l : lines)
            if (!l.rawLabel.isEmpty() && !raws.contains(l.rawLabel)) raws << l.rawLabel;
        for (const QString& r : raws) bindings.append({r, self, {}});
        applySpeakerBindings(meetingId, bindings, tr("Csak én beszéltem"));
        return true;
    }
    QStringList accepted;
    for (const Participant& p : m.participants)
        if (p.approved) accepted << p.id;
    const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(m.folder);
    const MeetingActivity act =
        MeetingActivity::load(m.folder, processing::trackactivity::activityFingerprint(m, m.folder));
    bindings = processing::participants::approvalBindings(m.participants, accepted, lines, file.clusters,
                                              act.isEmpty() ? nullptr : &act, &m);
    applySpeakerBindings(meetingId, bindings, tr("Résztvevők jóváhagyása"));
    return true;
}

bool AppController::approveParticipants(const QString& meetingId, const QVector<Participant>& accepted)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Résztvevők jóváhagyva"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return false;
    const processing::participants::CandidateContext ctx = d->candidateContext();
    QStringList acceptedIds;
    for (const Participant& a : accepted) {
        int idx = -1;
        for (int i = 0; i < m.participants.size(); ++i)
            if (!a.id.isEmpty() && m.participants.at(i).id == a.id) idx = i;
        if (idx < 0) {
            // The new person added in the dialog.
            Participant p = a;
            p.personName = a.personName.trimmed();
            if (p.personName.isEmpty()) continue;
            p.id = QStringLiteral("m-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
            p.source = ParticipantSource::Manual;
            p.evidence = processing::participants::participantEvidence(p, !p.sides.isEmpty(), -1.0, m, ctx);
            m.participants.append(p);
            idx = m.participants.size() - 1;
        } else if (!a.personName.trimmed().isEmpty()) {
            m.participants[idx].personName = a.personName.trimmed();   // name edited
        }
        acceptedIds << m.participants.at(idx).id;
    }

    ParticipantApproval ap;
    ap.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    ap.modelIds = processing::ParticipantAnalysisFile::load(m.folder).modelIds;
    for (Participant& p : m.participants) {
        p.approved = acceptedIds.contains(p.id) && !p.personName.isEmpty();
        if (p.approved && d->people) d->people->add(p.personName);
        ap.candidates.append({p.id, p.personName, processing::participants::participantGroup(p), p.approved,
                              p.rawSpeakerIds});
    }
    m.approval = ap;
    d->store->saveMeeting(m);
    emit participantsChanged(meetingId);
    // If the transcript already exists: the binding now (one undo step); otherwise when the transcript arrives.
    if (m.hasTranscript) applyParticipantsToTranscript(meetingId);
    return true;
}

bool AppController::skipApproval(const QString& meetingId)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Jóváhagyás kihagyva"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return false;
    ParticipantApproval ap;
    ap.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    ap.modelIds = processing::ParticipantAnalysisFile::load(m.folder).modelIds;
    ap.skipped = true;
    for (const Participant& p : m.participants)
        ap.candidates.append({p.id, p.personName, processing::participants::participantGroup(p), false, p.rawSpeakerIds});
    m.approval = ap;
    d->store->saveMeeting(m);
    emit participantsChanged(meetingId);
    return true;
}

bool AppController::unbindParticipant(const QString& meetingId, const QString& participantId)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Nem volt ott"));
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return false;
    int idx = -1;
    for (int i = 0; i < m.participants.size(); ++i)
        if (m.participants.at(i).id == participantId) idx = i;
    if (idx < 0) return false;
    Participant& p = m.participants[idx];
    undoScope.text = tr("Nem volt ott: %1").arg(p.personName);

    // Its lines go anonymous: the bound raw speakers (for a two-sided one only that side's lines), and every
    // speaker bound to the person (e.g. from an earlier automatic identification).
    QVector<SpeakerBinding> bindings;
    if (m.hasTranscript) {
        const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(m.folder);
        const processing::ParticipantAnalysisFile file = processing::ParticipantAnalysisFile::load(m.folder);
        const MeetingActivity act =
            MeetingActivity::load(m.folder, processing::trackactivity::activityFingerprint(m, m.folder));
        Participant named = p;   // only their bindings (with the name, so two-sided lines come out)
        if (named.personName.isEmpty()) named.personName = QStringLiteral("?");
        for (SpeakerBinding b : processing::participants::approvalBindings({named}, {p.id}, lines, file.clusters,
                                                               act.isEmpty() ? nullptr : &act)) {
            b.personName.clear();
            bindings.append(b);
        }
        if (!p.personName.isEmpty())
            if (editing::SpeakerEditor* ed = speakerEditor(meetingId))
                for (const editing::EditorSpeaker& s : ed->speakers())
                    if (s.personName.compare(p.personName, Qt::CaseInsensitive) == 0)
                        bindings.append({s.key, QString(), {}});
    }
    p.approved = false;
    p.rawSpeakerIds.clear();
    if (m.approval)
        for (ParticipantApprovalEntry& e : m.approval->candidates)
            if (e.participantId == participantId) e.checked = false;
    d->store->saveMeeting(m);
    applySpeakerBindings(meetingId, bindings, tr("Nem volt ott: %1").arg(p.personName));
    emit participantsChanged(meetingId);
    return true;
}

QString AppController::addParticipant(const QString& meetingId, const QString& personName)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Résztvevő felvéve: %1").arg(personName.trimmed()));
    const QString name = personName.trimmed();
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || name.isEmpty()) return {};
    for (const Participant& p : m.participants)
        if (p.personName.compare(name, Qt::CaseInsensitive) == 0) return p.id;
    Participant p;
    p.id = QStringLiteral("m-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
    p.personName = name;
    p.source = ParticipantSource::Manual;
    p.evidence = processing::participants::participantEvidence(p, false, -1.0, m, d->candidateContext());
    m.participants.append(p);
    d->store->saveMeeting(m);
    if (d->people) d->people->add(name);
    emit participantsChanged(meetingId);
    return p.id;
}

bool AppController::setSoloMeeting(const QString& meetingId)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Csak én beszéltem"));
    const QString self = d->settings->settings().userSpeakerName.trimmed();
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || self.isEmpty()) return false;
    if (const auto it = d->analysisRuns.constFind(meetingId); it != d->analysisRuns.constEnd())
        it.value()->store(true);   // the running analysis's result is no longer needed

    bool have = false;
    for (Participant& p : m.participants) {
        const bool me = p.personName.compare(self, Qt::CaseInsensitive) == 0;
        p.approved = me;
        have = have || me;
    }
    if (!have) {
        Participant p;
        p.id = QStringLiteral("m-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
        p.personName = self;
        p.source = ParticipantSource::Manual;
        p.approved = true;
        p.evidence = processing::participants::participantEvidence(p, false, -1.0, m, d->candidateContext());
        m.participants.append(p);
    }
    ParticipantApproval ap;
    ap.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    ap.solo = true;
    for (const Participant& p : m.participants)
        ap.candidates.append({p.id, p.personName, processing::participants::participantGroup(p), p.approved, p.rawSpeakerIds});
    m.approval = ap;
    d->store->saveMeeting(m);
    if (m.hasTranscript) applyParticipantsToTranscript(meetingId);
    emit participantsChanged(meetingId);
    return true;
}

// ==== shared undo, permanent participant delete, participants after transcription, model operations ====

editing::MeetingUndoService* AppController::meetingUndo(const QString& meetingId)
{
    if (meetingId.isEmpty()) return nullptr;
    if (editing::MeetingUndoService* u = d->undoServices.value(meetingId)) {
        if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) u->attachSpeakerEditor(ed);
        return u;
    }
    if (d->store->load(meetingId).id.isEmpty()) return nullptr;
    auto* u = new editing::MeetingUndoService(meetingId, this);
    u->attachTagService(d->tags);
    if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) u->attachSpeakerEditor(ed);
    d->undoServices.insert(meetingId, u);
    connect(d->store, &library::MeetingStore::meetingRemoved, u, [this, u](const QString& id) {
        if (id != u->meetingId()) return;
        d->undoServices.remove(id);
        u->deleteLater();
    });
    return u;
}

bool AppController::removeParticipant(const QString& meetingId, const QString& participantId)
{
    ParticipantUndoScope undoScope(this, meetingId, tr("Résztvevő eltávolítva"));
    const Meeting m0 = d->store->load(meetingId);
    if (m0.id.isEmpty()) return false;
    const auto has = [&participantId](const Participant& p) { return p.id == participantId; };
    const auto it0 = std::find_if(m0.participants.cbegin(), m0.participants.cend(), has);
    if (it0 == m0.participants.cend()) {
        // There is no participant record (the meeting got names before participant analysis): the id is the
        // transcript speaker's key; its lines go anonymous (one step).
        editing::SpeakerEditor* ed = m0.hasTranscript ? speakerEditor(meetingId) : nullptr;
        if (!ed) return false;
        for (const editing::EditorSpeaker& s : ed->speakers()) {
            if (s.key != participantId || s.anonymous) continue;
            undoScope.text = tr("Nem volt ott: %1").arg(s.personName);
            applySpeakerBindings(meetingId, {{s.key, QString(), {}}}, undoScope.text);
            emit participantsChanged(meetingId);
            return true;
        }
        return false;
    }
    if (!it0->personName.isEmpty()) undoScope.text = tr("Résztvevő eltávolítva: %1").arg(it0->personName);

    // Its lines go anonymous (one editor step, which the shared step absorbs), then the deletion.
    unbindParticipant(meetingId, participantId);
    Meeting m = d->store->load(meetingId);
    m.participants.erase(std::remove_if(m.participants.begin(), m.participants.end(), has), m.participants.end());
    if (m.approval) {
        auto& c = m.approval->candidates;
        c.erase(std::remove_if(c.begin(), c.end(),
                               [&](const ParticipantApprovalEntry& e) { return e.participantId == participantId; }),
                c.end());
    }
    d->store->saveMeeting(m);
    emit participantsChanged(meetingId);
    return true;
}

QVector<Participant> AppController::unheardParticipants(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty() || !m.hasTranscript) return {};
    // The persons of the transcript's speakers: from the open editor, otherwise resolved from disk.
    QSet<QString> heard;
    if (editing::SpeakerEditor* ed = d->speakerEditors.value(meetingId)) {
        for (const editing::EditorSpeaker& s : ed->speakers())
            if (s.utteranceCount > 0 && !s.personName.isEmpty()) heard.insert(s.personName.toLower());
    } else {
        const QVector<TranscriptLine> lines = editing::speakeredit::loadTranscriptLines(m.folder);
        const SpeakerOverlay ov = editing::speakeredit::loadOverlayFor(m.folder, lines);
        QSet<QString> keys;
        for (const TranscriptLine& l : lines) keys.insert(editing::speakeredit::resolveSpeakerKey(ov, l));
        for (const QString& k : std::as_const(keys)) {
            const QString person = editing::speakeredit::speakerPerson(ov, m.speakerMap, k);
            if (!person.isEmpty()) heard.insert(person.toLower());
        }
    }
    QVector<Participant> out;
    for (const Participant& p : m.participants)
        if (!p.personName.isEmpty() && p.rawSpeakerIds.isEmpty() && !heard.contains(p.personName.toLower()))
            out.append(p);
    return out;
}

QVector<processing::VoiceprintBackfillSkip> AppController::backfillSkippedSamples() const
{
    QVector<processing::VoiceprintBackfillSkip> out = d->backfillSkipped;
    std::stable_sort(out.begin(), out.end(), [](const processing::VoiceprintBackfillSkip& a, const processing::VoiceprintBackfillSkip& b) {
        return a.person.localeAwareCompare(b.person) < 0;
    });
    return out;
}

void AppController::Impl::noteBackfillSkip(const QString& person, const QString& model, const Voiceprint& vp,
                                           processing::VoiceprintBackfillSkip::Reason reason)
{
    for (const processing::VoiceprintBackfillSkip& s : std::as_const(backfillSkipped))
        if (s.person == person && s.model == model && s.sampleRef == vp.sampleRef
            && s.sourceMeetingId == vp.sourceMeetingId)
            return;
    backfillSkipped.append({person, model, vp.sampleRef, vp.sourceMeetingId, reason});
}

void AppController::rescanVoiceModels()
{
    const QStringList before = d->voiceModelIds;
    applyVoiceModels();   // it signals by itself on change (and backfills on growth)
    if (d->voiceModelIds == before) emit voiceModelsChanged();   // the file state may have changed anyway
}

bool AppController::deleteVoiceModelFile(const QString& id, QString* error)
{
    const std::optional<processing::VoiceModelSpec> spec = processing::VoiceModelRegistry::spec(id);
    if (!spec) {
        if (error) *error = QStringLiteral("Unknown voice model: %1").arg(id);
        return false;
    }
    if (!processing::VoiceModelDownloader::deleteModelFile(*spec, d->metaDir, error)) return false;
    qCInfo(lcVoice).noquote() << "Hangmodell-fájl törölve:" << id;
    rescanVoiceModels();
    return true;
}


} // namespace tanara::app
#include "tanara/AppController.h"

#include "tanara/Logging.h"
#include "tanara/Paths.h"
#include "tanara/PromptLibrary.h"
#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/IMeetingDetector.h"
#include <QTimer>
#include "tanara/SettingsManager.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/audio/DeviceMonitor.h"
#include "tanara/audio/RecordingSession.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/KeyStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/PersonDetailsStore.h"
#include "tanara/people/PeopleService.h"
#include "tanara/people/PeopleStats.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceEmbedder.h"
#include "tanara/stt/ISttProvider.h"
#include "tanara/llm/ILlmProvider.h"
#include "tanara/provider/ProviderRegistry.h"
#include "tanara/SummaryService.h"
#include "tanara/ComplexSummaryService.h"
#include "tanara/TranscriptMerger.h"
#include "tanara/Localization.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/jobs/JobErrors.h"
#include "tanara/jobs/JobStats.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/audio/WaveformService.h"
#include "tanara/edit/PeopleDirectory.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"

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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFileInfo>
#include <QDateTime>
#include <QPointer>
#include <QRegularExpression>
#include <QUuid>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace tanara {

namespace {

QString expandTilde(QString p) {
    if (p == QStringLiteral("~")) return QDir::homePath();
    if (p.startsWith(QStringLiteral("~/"))) return QDir::homePath() + p.mid(1);
    return p;
}

bool writeTextFile(const QString& path, const QString& text) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
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

// A nevek ráírása az átiratra: speakeredit::applyResolvedSpeakers(mt, meeting) — a
// speakerMap MELLETT a kézi sor-javításokat (transcript.speakers.json) is alkalmazza.

MergedTranscript readTokensJson(const QString& path) {
    MergedTranscript mt;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return mt;
    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    mt.language = root["language"].toString();
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

// A komplex összefoglaló 1. körének SZERKESZTHETŐ téma-listája (summary.topics.json).
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

// A komplex összefoglaló 2. körének KÉSZ elemzései (summary.analyses.json) — témánként
// AZONNAL perzisztálva, hogy megszakítás/hiba után ne vesszen el a már kifizetett munka.
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

// Egy kész elemzés beírása/felülírása a summary.analyses.json-ba (topicId szerint).
void upsertAnalysisJson(const QString& path, const TopicAnalysis& a) {
    QVector<TopicAnalysis> all = readAnalysesJson(path);
    bool replaced = false;
    for (TopicAnalysis& existing : all)
        if (existing.topicId == a.topicId) { existing = a; replaced = true; break; }
    if (!replaced) all.append(a);
    writeAnalysesJson(path, all);
}

// A komplex összefoglaló markdown-ja: globális fej (vezetői összefoglaló + összevont
// teendők) + témánkénti szekciók (összegző + döntések + teendők). A Summary::renderMarkdown
// stílusát követi (`- [ ]` teendő-checklisták).
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
            md += t.renderMarkdown();   // detail + döntések + teendők (Types/SummaryService)
        }
    }
    return md;
}

// Melyik sávból vegyünk hangot egy nyers beszélő-címkéhez:
//  - pontos egyezés egy sáv fix beszélőjével (mic) → az a sáv;
//  - egyébként (diarizált "Távoli N") → a loopback sáv;
//  - végső fallback: az első sáv.
const Track* resolveTrackForLabel(const Meeting& m, const MergedTranscript& mt,
                                  const QString& rawLabel) {
    // 1) Elsődlegesen a címke tokenjeinek trackId-je alapján (a diarizált mic/loopback
    //    beszélők — „Mikrofon N" / „Távoli N" — így a HELYES sávra oldódnak fel).
    QString tid;
    for (const TranscriptToken& t : mt.tokens)
        if (t.speaker == rawLabel && !t.trackId.isEmpty()) { tid = t.trackId; break; }
    if (!tid.isEmpty())
        for (const Track& t : m.tracks)
            if (t.id == tid) return &t;
    // 2) Fallback: pontos sáv-beszélőnév egyezés (fix mic-név).
    for (const Track& t : m.tracks)
        if (t.speakerLabel == rawLabel) return &t;
    // 3) Fallback: az első loopback sáv.
    for (const Track& t : m.tracks)
        if (t.kind == TrackKind::Loopback) return &t;
    return m.tracks.isEmpty() ? nullptr : &m.tracks.first();
}

// Egy beszélő reprezentatív embeddingje: a leghosszabb utterance-eiből ~3–12 s hangot
// gyűjt a megadott sávból, és egyetlen embeddinget számol. Üres = nincs elég hang/hiba.
QVector<float> embeddingForLabel(VoiceEmbedder& emb, const QString& audioPath,
                                 const MergedTranscript& mt, const QString& rawLabel) {
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
        pcm += VoiceEmbedder::decodePcm16kMono(path, u.startMs, u.endMs);
        accMs += (u.endMs - u.startMs);
    }
    if (pcm.isEmpty()) return {};
    return emb.embedPcm(pcm);
}

// A lenyomathoz eltárolt, visszahallgatható reprezentatív szegmens hivatkozása:
// "track_fájl#startMs-endMs" (a leghosszabb utterance). Lejátszáshoz a People-panel
// a sourceMeetingId-ből oldja fel a mappát.
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

// A voice-ID hangforrása: MOST a mixdown (a leirat is abból készül, így a diarizált
// „Beszélő N" címkék időablakai közvetlenül a mixre illeszkednek). Ha nincs mixdown
// (régi, per-sáv meeting), back-compat: a címke feloldott sávjára esünk vissza.
struct VoiceSource { QString absPath; QString fileRel; QString trackId; QString device; };
VoiceSource resolveVoiceSource(const Meeting& m, const MergedTranscript& mt,
                               const QString& rawLabel) {
    const QString mixRel = m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3")
                                                   : m.mixdownFile;
    const QString mixAbs = QDir(m.folder).filePath(mixRel);
    if (QFile::exists(mixAbs))
        return { mixAbs, mixRel, QStringLiteral("mixdown"), QString() };
    if (const Track* t = resolveTrackForLabel(m, mt, rawLabel))
        return { QDir(m.folder).filePath(t->file), t->file, t->id, t->deviceName };
    return {};
}

// Agglomeratív klaszterezés cosine-centroid alapján: minden embedding-hez klaszter-címke
// (a címke a klaszter egy reprezentáns indexe). mergeThreshold felett von össze.
QVector<int> clusterEmbeddings(const QVector<QVector<float>>& embs, double mergeThreshold) {
    const int n = embs.size();
    QVector<int> label(n);
    for (int i = 0; i < n; ++i) label[i] = i;
    if (n <= 1) return label;

    QVector<QVector<float>> cent = embs;   // L2-normalizált embeddingek
    QVector<int> sz(n, 1);
    QVector<bool> alive(n, true);
    for (;;) {
        double best = -2.0; int a = -1, b = -1;
        for (int i = 0; i < n; ++i) if (alive[i])
            for (int j = i + 1; j < n; ++j) if (alive[j]) {
                const double s = VoiceprintStore::cosineSimilarity(cent[i], cent[j]);
                if (s > best) { best = s; a = i; b = j; }
            }
        if (a < 0 || best < mergeThreshold) break;
        const int na = sz[a], nb = sz[b];
        QVector<float> mrg(cent[a].size());
        for (int k = 0; k < mrg.size(); ++k)
            mrg[k] = (cent[a][k] * na + cent[b][k] * nb) / (na + nb);
        cent[a] = VoiceprintStore::l2normalize(mrg);
        sz[a] = na + nb; alive[b] = false;
        for (int i = 0; i < n; ++i) if (label[i] == b) label[i] = a;
    }
    return label;
}

// Cosine-küszöb az auto-azonosításhoz (a validáció: azonos 0.77, kereszt ≤0.35).
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

QString sttPhase(JobState s) {
    switch (s) {
    case JobState::Uploading:  return QCoreApplication::translate("AppController", "Hang feltöltése…");
    case JobState::Queued:     return QCoreApplication::translate("AppController", "Várakozás a Soniox sorban…");
    case JobState::Processing: return QCoreApplication::translate("AppController", "Átírás folyamatban…");
    case JobState::Completed:  return QCoreApplication::translate("AppController", "Sáv kész");
    case JobState::Failed:     return QCoreApplication::translate("AppController", "Hiba");
    default:                   return QStringLiteral("…");
    }
}

QString slugify(const QString& s) {
    QString out;
    for (QChar c : s) out += (c.isLetterOrNumber() ? c : QChar('-'));
    while (out.contains(QStringLiteral("--"))) out.replace(QStringLiteral("--"), QStringLiteral("-"));
    return out.isEmpty() ? QStringLiteral("meeting") : out;
}

// Egy szolgáltató-hívás utolsó SIKERTELEN HTTP-váltása (a megmaradó hiba technikai sorához).
struct FailureSink { bool has = false; HttpExchange ex; };
using FailureSinkPtr = std::shared_ptr<FailureSink>;

// A config válasz-hookját kiegészíti: a meglévő (cloud) hook mellett a nem-2xx válaszokat és
// a hálózati hibákat a sinkbe is elteszi. A takarító DELETE-ek hibája nem számít.
void captureFailures(ProviderConfig& cfg, const FailureSinkPtr& sink) {
    const auto prev = cfg.onExchange;
    cfg.onExchange = [prev, sink](const HttpExchange& ex) {
        if (prev) prev(ex);
        const bool ok = ex.status >= 200 && ex.status < 300;
        if (!ok && ex.method != "DELETE") { sink->has = true; sink->ex = ex; }
    };
}

} // namespace

struct AppController::Impl {
    SettingsManager* settings = nullptr;
    DeviceManager*   devices  = nullptr;
    MeetingStore*    store    = nullptr;
    RecordingSession* session = nullptr;
    DeviceMonitor*   monitor = nullptr;
    KeyStore         keyStore;
    std::unique_ptr<PeopleStore> people;
    std::unique_ptr<PersonDetailsStore> details;   // becenevek + megjegyzés (people-details.json)
    PeopleStats*   peopleStats = nullptr;
    PeopleService* peopleService = nullptr;
    std::unique_ptr<VoiceprintStore> voiceprints;
    std::unique_ptr<VoiceEmbedder>   embedder;   // lusta betöltés (első használatkor)
    QString          voiceModelPath;
    QNetworkAccessManager* nam = nullptr;   // LLM-modellek lekéréséhez
    RecordingState   state = RecordingState::Idle;
    QString          audioDir;
    QString          metaDir;
    QString          notesDir;
    QString          statePath;
    QStringList      lastDevices;
    QString          currentFolder;

    // Szintfigyelés + felvétel közbeni sáv-kezelés (a lebegő felvevőhöz).
    bool        monitorWanted = false;          // a UI kérte a szintfigyelést
    bool        monitorLegacy = false;          // start/stopLevelMonitoring (a felvevő) kérése
    QHash<QObject*, QMetaObject::Connection> monitorHolders;   // retainLevelMonitoring fogyasztói (Beállítások)
    bool        monitorDuringRecording = false; // felvétel alatt is (a nem rögzített eszközökön)
    QStringList monitorNames;                   // a figyelő aktuális eszköz-halmaza
    QStringList recNames;                       // a felvétel sávjai (index → eszköznév)
    QStringList recClosed;                      // menet közben leválasztott eszközök
    QHash<QString, int> recMissing;             // hány egymást követő felsorolásból hiányzott
    bool        autoMixdown = true;             // felvétel utáni automatikus lekeverés
    QHash<QString, MergedTranscript> mergedCache;

    // Hívás-vég figyelés felvétel közben (lásd startCallEndMonitor).
    std::unique_ptr<IMeetingDetector> callDetector;
    QTimer* callTimer = nullptr;
    bool    callSeenActive = false;   // láttunk-e aktív hívást a felvétel alatt
    int     callInactivePolls = 0;    // egymást követő inaktív pollok az aktív után
    QString callAppName;

    // Csend-figyelés felvétel közben (silenceAskMinutes).
    QTimer* silenceTimer = nullptr;
    qint64  lastLoudMs = 0;        // utolsó „hangos” RMS időbélyege (monotonic)
    bool    silenceAsked = false;  // már kérdeztünk erre a csend-szakaszra

    // Téma-elemzés job-sor (komplex 2. kör): egyszerre EGY LLM-hívás fut (lokális modell,
    // parallel=1), a többi téma sorban áll. Témánként külön (újra)indítható.
    struct TopicJob { QString meetingId; SummaryTopic topic; };
    QVector<TopicJob> topicJobQueue;
    bool    topicJobActive = false;
    QString activeTopicMeetingId;   // az épp futó job címzése (dedup + életciklus-jelek)
    QString activeTopicId;
    QSet<QString> reduceWhenDone;             // meetingId-k, ahol a sor végén auto-reduce jön
    QHash<QString, QPair<int,int>> jobCounts; // meetingId → (ok, fail) az aktuális batch-ben

    // Átirat-szerkesztő munkamenetek (meetingenként egy; lásd speakerEditor()).
    QHash<QString, QPointer<SpeakerEditor>> speakerEditors;
    bool editorEmitting = false;   // a szerkesztő saját speakerMap-jele megy ki (ne töltsön vissza)

    // Tanara Cloud.
    CloudAccount* cloud = nullptr;
    bool cloudLive = false;
    bool cloudTeaser = false;
    // Meetingenként a nyitott komplex összefoglaló cloud-futása (témagyűjtés → téma-elemzések
    // → reduce): EGY X-Tanara-Job-Id, a hívásonkénti terhelések összesítve (K-07 „12 rész”).
    // Lezáráskor (kész / hiba / megszakítás) kikerül innen; a folytatás így új futás.
    QHash<QString, CloudRunPtr> complexRuns;

    // ---- strukturált réteg (újratervezett főablak) ----------------------------------
    AppController*     q = nullptr;
    MeetingJobTracker* jobs = nullptr;
    MeetingLibrary*    library = nullptr;
    TrackCatalog*      tracks = nullptr;
    WaveformService*   waveforms = nullptr;
    AudioImporter*     importer = nullptr;
    std::unique_ptr<JobStats> jobStats;      // korábbi futások sebessége (becsléshez)

    // Futó átírás: megszakításhoz a job / a lekeverés-lánc kapcsolata.
    struct TranscribeRun {
        QPointer<SttJob> job;
        std::shared_ptr<QMetaObject::Connection> mixConn;
        bool mixPhase = false;         // még a lekeverésre várunk
        bool ownsMixdown = false;      // a keverést ez az átírás indította
        bool cancelRequested = false;
    };
    QHash<QString, TranscribeRun> transcribeRuns;
    QSet<QString> clearSpeakersOnTranscript;   // újra-átírás: a régi nevek az új átirattal törlődnek
    // Azok a meetingek, amelyeknél a felhasználó KIKAPCSOLTA az átírás utáni automatikus
    // hang-azonosítást (M03 kapcsoló). Folyamat-szintű (nem perzisztált) választás.
    QSet<QString> skipIdentifyAfterTranscribe;

    QHash<QString, QPointer<QProcess>> mixdownProcs;   // meetingId → futó ffmpeg
    QSet<QString> mixdownCancelled;
    QHash<QString, QString> mixdownFailReason;   // az utolsó el sem indított keverés oka (hiányzó sáv)
    // Meetingenként az átirat „nemzedéke”: minden új átirat-kiírás lépteti. A háttérben futó
    // azonosítás ebből (és a tokens-fájl állapotából) látja, hogy közben új átirat érkezett.
    QHash<QString, int> transcriptGen;

    // Futó LLM-hívás (összefoglaló / témagyűjtés / összegzés / aktív téma-elemzés).
    struct LlmRun { QPointer<QObject> svc; QPointer<QObject> provider; CloudRunPtr cloudRun; };
    QHash<QString, LlmRun> llmRuns;            // llmKey(meetingId, kind) → futás
    LlmRun activeTopicRun;
    QHash<QString, int> topicBatchTotal;       // meetingId → az aktuális kötegbe sorolt témák száma

    // Futó (háttérszálas) azonosítás.
    struct IdentifyRun { QThread* thread = nullptr; std::shared_ptr<std::atomic<bool>> cancel; bool asStage = false; };
    // Az átirat állapota egy azonosítás indulásakor (lásd startIdentify): ha a végére más,
    // az eredmény a RÉGI átirat címkéire vonatkozik → eldobjuk.
    QString transcriptStamp(const QString& meetingId, const QString& folder) const {
        const FileStamp fs = FileStamp::of(QDir(folder).filePath(QStringLiteral("transcript.tokens.json")));
        return QStringLiteral("%1/%2/%3").arg(transcriptGen.value(meetingId)).arg(fs.mtimeMs).arg(fs.size);
    }
    QHash<QString, IdentifyRun> identifyRuns;

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
                                const FailureSinkPtr& sink) const;
    void applyIdentification(const QString& meetingId,
                             const QVector<QPair<QString, QVector<float>>>& embeddings);
    bool matchSpeaker(Meeting& m, const QString& rawLabel, const QVector<float>& embedding);
    void commitIdentification(const Meeting& m, MergedTranscript merged,
                              const QStringList& identifiedLabels);
};

// Egy cloud feldolgozás-futás állapota. A providerek válasz-hookja (onExchange) tölti.
struct AppController::CloudRun {
    QString meetingId;
    QString kind;            // transcribe | summary | topics | complex
    QString jobId;           // X-Tanara-Job-Id — futásonként új véletlen érték
    qint64  chargedMicros = 0;
    QString currency;
    int     calls = 0;       // sikeres terhelő hívások
    Money   balance;         // az utolsó ismert egyenleg (terhelés / visszaírás után)
    QString vatMode;
    CloudError lastError;    // az utolsó nem-2xx (nem-DELETE) válasz
    bool    refunded = false;
    Money   refund;
    QString fileId, transcriptionId;
    QString lastRequestId;

    Money charged() const { return Money{ chargedMicros, currency.isEmpty() ? QStringLiteral("USD") : currency }; }
    void addCharge(const ChargeInfo& c) {
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
#ifdef TANARA_HAVE_VOICEID
    return QFileInfo::exists(voiceModelPath);
#else
    return false;   // a build nem tartalmaz voice-ID-t (TANARA_BUILD_VOICEID=OFF)
#endif
}

// A futó ffmpeg valós százaléka a strukturált állapotba: önálló keverésnél a Mixdown feladat,
// átírás részeként az átírás-feladat „mixdown” szakasza.
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

// Futó LLM-hívás megszakítása: a service jeleit leválasztjuk, a providert (vele a jobot és a
// hálózati kérést) töröljük — a szolgáltató felé a kapcsolat bomlik, eredmény nem érkezik.
void AppController::Impl::abortLlmRun(const LlmRun& run)
{
    if (run.svc) {
        run.svc->disconnect(q);
        run.svc->deleteLater();
    }
    if (run.provider) run.provider->deleteLater();
}

JobError AppController::Impl::describeLlmFailure(JobKind kind, const QString& raw,
                                                 const CloudRunPtr& run,
                                                 const FailureSinkPtr& sink) const
{
    if (run && run->lastError.isError())
        return describeCloudFailure(kind, run->lastError);
    return describeJobFailure(kind, raw, (sink && sink->has) ? &sink->ex : nullptr);
}

// A háttérszálon számolt beszélő-embeddingek párosítása a lenyomat-DB ellen + mentés (fő
// szál). Ugyanaz a szabály, mint az autoIdentifyMeeting-ben: csak a még névtelen címkék,
// küszöb felett; a transcript.md a nevekkel újragenerálódik; speakerMapChanged jel.
void AppController::Impl::applyIdentification(
    const QString& meetingId, const QVector<QPair<QString, QVector<float>>>& embeddings)
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

// AZ azonosítás-szabály EGY helyen (a szinkron autoIdentifyMeeting és az aszinkron út közös):
// a beszélő embeddingje a lenyomat-DB ellen; küszöb felett a név a speakerMap-be kerül, a
// személy a névlistába, a pontszám az overlay-be (a szerkesztő „hang alapján felismerve (82%)”).
bool AppController::Impl::matchSpeaker(Meeting& m, const QString& rawLabel,
                                       const QVector<float>& embedding)
{
    if (!voiceprints || embedding.isEmpty()) return false;
    const VoiceMatch match = voiceprints->bestMatch(embedding);
    if (match.score < kVoiceMatchThreshold || match.name.isEmpty()) return false;
    m.speakerMap.insert(rawLabel, match.name);
    if (people) people->add(match.name);
    speakeredit::recordIdentification(m.folder, rawLabel, match.name, match.score);
    return true;
}

// Az azonosítás eredményének mentése: meeting.json, transcript.md a feloldott nevekkel, az
// összefoglaló elavultnak jelölése (ha van), és a jelek.
void AppController::Impl::commitIdentification(const Meeting& m, MergedTranscript merged,
                                               const QStringList& identifiedLabels)
{
    store->saveMeeting(m);
    if (!merged.tokens.isEmpty()) {
        speakeredit::applyResolvedSpeakers(merged, m);
        writeTextFile(QDir(m.folder).filePath(QStringLiteral("transcript.md")), merged.renderMarkdown());
    }
    if (speakeredit::markSummaryStale(m, identifiedLabels)) emit q->summaryStaleChanged(m.id);
    emit q->speakerMapChanged(m.id);
}

AppController::AppController(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    registerBuiltinProviders();

    d->settings = new SettingsManager(QString(), this);
    const AppSettings s = d->settings->settings();

    // Tanara Cloud mód: TANARA_CLOUD=live|teaser|off felülírja a settings.cloudEnabled-et.
    {
        const QString mode = qEnvironmentVariable("TANARA_CLOUD").trimmed().toLower();
        const bool off = mode == QLatin1String("off");
        const bool wantLive = mode == QLatin1String("live") || (mode.isEmpty() && s.cloudEnabled);
        d->cloudLive   = !off && cloud::clientCompiled() && wantLive;
        d->cloudTeaser = !off && !d->cloudLive && cloud::teaserCompiled();
        if (d->cloudLive)
            registerCloudProviders();   // feature-flag: enélkül a „tanara-cloud” id nem is létezik
    }
    d->audioDir = expandTilde(s.audioDir);
    d->metaDir  = paths::resolveMetadataDir(s.metadataDir);   // TANARA_HOME-tudatos
    d->notesDir = expandTilde(s.notesDir);
    QDir().mkpath(d->audioDir);
    QDir().mkpath(d->metaDir);

    d->keyStore = KeyStore(QDir(d->metaDir).filePath(QStringLiteral("secrets.json")));

    d->cloud = new CloudAccount(&d->keyStore, d->metaDir, this);
    {
        const QString envUrl = qEnvironmentVariable("TANARA_CLOUD_URL").trimmed();
        d->cloud->setBaseUrl(!envUrl.isEmpty() ? envUrl : s.cloudBaseUrl);
        d->cloud->setLanguage(activeUiLanguage());
    }
    d->devices = new DeviceManager(this);
    d->store   = new MeetingStore(d->audioDir, d->metaDir, this);
    // Crash után árván maradt felvételek (sávok meeting.json nélkül) visszahozása a listába.
    d->store->recoverOrphanRecordings();

    // Strukturált réteg: feldolgozási állapot, könyvtár-lekérdezések, sávok, hullámforma.
    d->jobs      = new MeetingJobTracker(d->store, this);
    d->library   = new MeetingLibrary(d->store, d->jobs, this);
    d->tracks    = new TrackCatalog(d->store, this);
    d->waveforms = new WaveformService(this);
    d->importer  = new AudioImporter(d->store, this);
    // Importálás → strukturált feladat a LEENDŐ meeting azonosítóján (a könyvtárban még
    // nincs ilyen meeting; a héj az activeJobs()-ból / a jelekből mutatja).
    connect(d->importer, &AudioImporter::started, this, [this](const QString& id, const QString& title) {
        d->jobs->begin(id, JobKind::Import, tr("Importálás"));
        d->jobs->setMessage(id, JobKind::Import, title);
    });
    connect(d->importer, &AudioImporter::progress, this,
            [this](const QString& id, int percent, int fileIndex, int fileCount) {
        d->jobs->setPercent(id, JobKind::Import, percent);
        if (fileCount > 1)
            d->jobs->setCounts(id, JobKind::Import, fileIndex, fileCount);
    });
    connect(d->importer, &AudioImporter::finished, this, [this](const QString& id, const Meeting& m) {
        d->jobs->finish(id, JobKind::Import);
        emit importFinished(m);
        // Lekeverés: pontosan úgy, mint egy felvétel végén (lásd a RecordingSession::finished ágat).
        if (d->autoMixdown && d->settings->settings().mixdownMode != QStringLiteral("manual"))
            regenerateMixdown(m.id);
    });
    connect(d->importer, &AudioImporter::failed, this,
            [this](const QString& id, const QString& message, const QString& detail) {
        JobError je;
        je.kind = JobKind::Import;
        je.message = message;
        je.detail = detail;
        d->jobs->fail(id, JobKind::Import, je);
        emit importFailed(id, message, detail);
    });
    connect(d->importer, &AudioImporter::cancelled, this, [this](const QString& id) {
        d->jobs->cancelled(id, JobKind::Import);
        emit importCancelled(id);
    });
    d->jobStats  = std::make_unique<JobStats>(QDir(d->metaDir).filePath(QStringLiteral("job-stats.json")));
    connect(d->tracks, &TrackCatalog::tracksChanged, this, &AppController::tracksChanged);
    // Az összefoglaló elavultsága (beszélő-szerkesztő réteg) a levezetett állapot része: a
    // könyvtár-ikon „elavult” állapota és az „Ezek várnak rád” lista ebből frissül.
    d->jobs->setSummaryStaleProbe([this](const Meeting& m) {
        const SummaryStaleInfo info = summaryStale(m);
        return info.stale ? info.correctedSpeakers : -1;
    });
    connect(this, &AppController::summaryStaleChanged, d->jobs, &MeetingJobTracker::notifyStateChanged);

    connect(d->devices, &DeviceManager::devicesChanged, this, &AppController::handleDeviceSetChange);
    connect(d->devices, &DeviceManager::devicesChanged, this, &AppController::devicesChanged);

    d->statePath = QDir(d->metaDir).filePath(QStringLiteral("state.json"));
    d->lastDevices = loadLastDevices(d->statePath);
    d->monitor = new DeviceMonitor(this);
    connect(d->monitor, &DeviceMonitor::level, this, &AppController::deviceLevel);
    connect(d->monitor, &DeviceMonitor::levelPeak, this, &AppController::deviceLevelPeak);

    d->people = std::make_unique<PeopleStore>(QDir(d->metaDir).filePath(QStringLiteral("people.json")));

    // Voice-ID: lenyomat-DB + a modell várt helye (~/.tanara/models/...).
    d->voiceprints = std::make_unique<VoiceprintStore>(
        QDir(d->metaDir).filePath(QStringLiteral("voiceprints.json")));
    d->voiceModelPath = QDir(d->metaDir).filePath(
        QStringLiteral("models/campplus_sv_zh_en_16k.onnx"));

    // Személyek ablak: kiegészítő adatok a people.json MELLETT (a régi buildek érintetlenek),
    // háttérben számolt statisztika, és a műveletek (összevonás, minta-áthelyezés …).
    d->details = std::make_unique<PersonDetailsStore>(
        QDir(d->metaDir).filePath(QStringLiteral("people-details.json")));
    d->peopleStats = new PeopleStats(d->store, this);
    d->peopleService = new PeopleService(this, d->store, d->people.get(), d->voiceprints.get(),
                                         d->details.get(), d->peopleStats, this);
    // Ami a résztvevőket vagy a beszédidőt érinti, az a statisztikát is (összevontan frissül).
    connect(this, &AppController::speakerMapChanged, d->peopleStats, &PeopleStats::scheduleRefresh);
    connect(this, &AppController::transcriptReady, d->peopleStats, &PeopleStats::scheduleRefresh);
    connect(d->store, &MeetingStore::meetingAdded, d->peopleStats, &PeopleStats::scheduleRefresh);
    connect(d->store, &MeetingStore::meetingUpdated, d->peopleStats, &PeopleStats::scheduleRefresh);
    connect(d->store, &MeetingStore::meetingRemoved, d->peopleStats, &PeopleStats::scheduleRefresh);

    // Átirat-szerkesztő: az új összefoglaló törli az elavult-jelzőt; az új átirat eldobja a
    // kézi sor-javításokat (a megszólalások határai megváltoztak).
    connect(this, &AppController::summaryReady, this, [this](const QString& meetingId) {
        if (SpeakerEditor* ed = d->speakerEditors.value(meetingId)) {
            ed->notifySummaryRegenerated();
        } else {
            const Meeting m = d->store->load(meetingId);
            if (!m.folder.isEmpty()) speakeredit::clearSummaryStale(m.folder);
        }
        emit summaryStaleChanged(meetingId);
    });
    connect(this, &AppController::transcriptReady, this, [this](const QString& meetingId) {
        const Meeting m = d->store->load(meetingId);
        if (m.folder.isEmpty()) return;
        speakeredit::discardForNewTranscript(m.folder);
        if (SpeakerEditor* ed = d->speakerEditors.value(meetingId)) ed->reloadTranscript();
    });
}

AppController::~AppController()
{
    // A háttérszálas azonosítások leállítása és bevárása (a szál a d-re hivatkozó
    // eredményt már nem adja át: a QPointer addigra null).
    for (auto it = d->identifyRuns.begin(); it != d->identifyRuns.end(); ++it) {
        it->cancel->store(true);
        if (it->thread) {
            it->thread->disconnect(this);
            it->thread->wait();
            delete it->thread;
        }
    }
    d->identifyRuns.clear();
}

SettingsManager* AppController::settings() const { return d->settings; }
DeviceManager*   AppController::devices()  const { return d->devices; }
MeetingStore*    AppController::store()     const { return d->store; }
VoiceprintStore* AppController::voiceprints() const { return d->voiceprints.get(); }
PeopleService*   AppController::peopleService() const { return d->peopleService; }
PeopleStats*     AppController::peopleStats() const { return d->peopleStats; }
QString          AppController::voiceModelPath() const { return d->voiceModelPath; }
RecordingState   AppController::recordingState() const { return d->state; }
QString AppController::currentMeetingFolder() const { return d->currentFolder; }

void AppController::refreshDevices() { d->devices->refresh(); }

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

    // transcript.md újragenerálása a TELJES leképezéssel (a nyers tokenekből).
    // A segments.json NYERS marad, hogy a UI bármikor újra tudjon címkézni.
    MergedTranscript merged =
        readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (!merged.tokens.isEmpty()) {
        speakeredit::applyResolvedSpeakers(merged, m);
        writeTextFile(QDir(m.folder).filePath(QStringLiteral("transcript.md")), merged.renderMarkdown());
    }
    if (speakeredit::markSummaryStale(m, {rawLabel})) emit summaryStaleChanged(meetingId);
    emit speakerMapChanged(meetingId);

    // A kézi címkézés „tanítja" a voice-ID-t: lenyomatot rögzítünk a név alá.
    if (enroll && !name.isEmpty() && name != rawLabel)
        enrollSpeaker(meetingId, rawLabel, name);
}

void AppController::enrollSpeaker(const QString& meetingId, const QString& rawLabel, const QString& name) {
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;

    auto ensureEmb = [this]() -> VoiceEmbedder* {
        if (!d->embedder) {
            if (!QFileInfo::exists(d->voiceModelPath)) return nullptr;
            auto e = std::make_unique<VoiceEmbedder>(d->voiceModelPath);
            if (!e->isValid()) return nullptr;
            d->embedder = std::move(e);
        }
        return d->embedder.get();
    };
    VoiceEmbedder* emb = ensureEmb();
    if (!emb) return;   // nincs modell → csendben kihagyjuk (a kézi címkézés így is működik)

    const MergedTranscript merged =
        readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) return;
    const VoiceSource src = resolveVoiceSource(m, merged, rawLabel);
    if (src.absPath.isEmpty()) return;

    const QVector<float> embedding = embeddingForLabel(*emb, src.absPath, merged, rawLabel);
    if (embedding.isEmpty()) return;

    Voiceprint vp;
    vp.embedding = embedding;
    vp.sourceMeetingId = m.id;
    vp.sourceTrack = src.trackId;
    vp.device = src.device;
    vp.sampleRef = representativeSampleRef(merged, rawLabel, src.fileRel);
    vp.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    d->voiceprints->addPrint(nm, vp);
    emit voiceprintsChanged();
}

void AppController::autoIdentifyMeeting(const QString& meetingId,
                                       const std::function<bool(int,int)>& onProgress) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;

    auto ensureEmb = [this]() -> VoiceEmbedder* {
        if (!d->embedder) {
            if (!QFileInfo::exists(d->voiceModelPath)) return nullptr;
            auto e = std::make_unique<VoiceEmbedder>(d->voiceModelPath);
            if (!e->isValid()) return nullptr;
            d->embedder = std::move(e);
        }
        return d->embedder.get();
    };
    VoiceEmbedder* emb = ensureEmb();
    if (!emb) return;

    const MergedTranscript merged =
        readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) return;

    // Distinct nyers beszélő-címkék.
    QStringList labels;
    for (const TranscriptToken& t : merged.tokens)
        if (!t.speaker.isEmpty() && !labels.contains(t.speaker))
            labels << t.speaker;

    // MINDEN nevesítetlen beszélőt (mic + loopback) a voiceprint-DB ellen párosítunk.
    // Nincs hangerő/pozíció-alapú feltételezés és nincs auto-enroll: a NÉV a fingerprint
    // (vagy a felhasználó egyszeri kézi átnevezése) alapján kerül a beszélőre.
    bool mapChanged = false;
    QStringList identified;   // most nevet kapott nyers címkék
    int progressDone = 0;
    for (const QString& label : labels) {
        if (onProgress && !onProgress(++progressDone, labels.size()))
            break;   // a felhasználó megszakította → a már megtalált matchek mentődnek
        if (m.speakerMap.contains(label))
            continue;   // már nevesített (kézzel vagy korábbi match)
        const VoiceSource src = resolveVoiceSource(m, merged, label);
        if (src.absPath.isEmpty()) continue;
        const QVector<float> e = embeddingForLabel(*emb, src.absPath, merged, label);
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
    if (!d->embedder) {
        if (!QFileInfo::exists(d->voiceModelPath)) return none;
        auto e = std::make_unique<VoiceEmbedder>(d->voiceModelPath);
        if (!e->isValid()) return none;
        d->embedder = std::move(e);
    }
    const MergedTranscript merged =
        readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) return none;
    const VoiceSource src = resolveVoiceSource(m, merged, rawLabel);
    if (src.absPath.isEmpty()) return none;
    const QVector<float> e = embeddingForLabel(*d->embedder, src.absPath, merged, rawLabel);
    if (e.isEmpty()) return none;
    return d->voiceprints->bestMatch(e);
}

QVector<ParticipantGuess> AppController::identifyParticipants(
    const QString& meetingId, const std::function<bool(int,int)>& onProgress) {
    QVector<ParticipantGuess> out;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return out;

    if (!d->embedder) {
        if (!QFileInfo::exists(d->voiceModelPath)) return out;
        auto e = std::make_unique<VoiceEmbedder>(d->voiceModelPath);
        if (!e->isValid()) return out;
        d->embedder = std::move(e);
    }
    VoiceEmbedder* emb = d->embedder.get();

    constexpr qint64 kWinMs = 3000;          // ablak-hossz
    constexpr int    kTargetWindows = 50;    // ablakok száma/sáv (felső korlát)
    constexpr double kMergeThreshold = 0.55; // klaszter-összevonás cosine-küszöbe
    constexpr float  kSilenceRms = 0.004f;   // ennél halkabb ablak = csend (kihagy)

    // Progress: az összes feldolgozandó ablak (aktív sávok × ablak/sáv) — a callbackhez.
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

        QVector<QVector<float>> embs;
        QVector<qint64> winStart;
        for (qint64 s = 0; s + kWinMs <= D; s += step) {
            if (onProgress && !onProgress(++progressDone, progressTotal))
                return out;   // a felhasználó megszakította
            QVector<float> pcm = VoiceEmbedder::decodePcm16kMono(path, s, s + kWinMs);
            if (pcm.isEmpty()) continue;
            double sum = 0.0;
            for (float x : pcm) sum += static_cast<double>(x) * x;
            const double rms = std::sqrt(sum / static_cast<double>(pcm.size()));
            if (rms < kSilenceRms) continue;          // csend → kihagy
            const QVector<float> e = emb->embedPcm(pcm);
            if (!e.isEmpty()) { embs.append(e); winStart.append(s); }
        }
        if (embs.isEmpty()) continue;

        const QVector<int> labels = clusterEmbeddings(embs, kMergeThreshold);
        QMap<int, QVector<int>> groups;
        for (int i = 0; i < labels.size(); ++i) groups[labels[i]].append(i);

        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            const QVector<int>& idxs = it.value();
            // Zaj-szűrés: 1-ablakos klasztert eldobunk, ha van más is (átfedés/kattanás).
            if (idxs.size() < 2 && groups.size() > 1) continue;

            QVector<float> cent(embs[idxs[0]].size(), 0.0f);
            for (int i : idxs)
                for (int k = 0; k < cent.size(); ++k) cent[k] += embs[i][k];
            for (int k = 0; k < cent.size(); ++k) cent[k] /= idxs.size();
            cent = VoiceprintStore::l2normalize(cent);

            const VoiceMatch mt = d->voiceprints->bestMatch(cent);
            // Reprezentatív ablak = a centroidhoz legközelebbi (medoid).
            qint64 repStart = winStart[idxs[0]]; double bestSim = -2.0;
            for (int i : idxs) {
                const double s = VoiceprintStore::cosineSimilarity(cent, embs[i]);
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
    // Legtöbb ablak elöl (a domináns beszélők előre).
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
    if (!d->embedder) {
        if (!QFileInfo::exists(d->voiceModelPath)) return;
        auto e = std::make_unique<VoiceEmbedder>(d->voiceModelPath);
        if (!e->isValid()) return;
        d->embedder = std::move(e);
    }
    const Track* track = nullptr;
    for (const Track& t : m.tracks) if (t.id == trackId) { track = &t; break; }
    if (!track) return;
    const QString path = QDir(m.folder).filePath(track->file);
    const QVector<float> e = d->embedder->embedFile(path, startMs, endMs);
    if (e.isEmpty()) return;

    Voiceprint vp;
    vp.embedding = e;
    vp.sourceMeetingId = m.id;
    vp.sourceTrack = track->id;
    vp.device = track->deviceName;
    vp.sampleRef = QStringLiteral("%1#%2-%3").arg(track->file).arg(startMs).arg(endMs);
    vp.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    d->voiceprints->addPrint(nm, vp);
    if (d->people) d->people->add(nm);
    emit voiceprintsChanged();
    emit peopleChanged();
}

void AppController::renamePerson(const QString& oldName, const QString& newName) {
    const QString o = oldName.trimmed(), n = newName.trimmed();
    if (o.isEmpty() || n.isEmpty() || o == n) return;
    if (d->people) d->people->rename(o, n);
    if (d->details) d->details->rename(o, n, /*oldNameAsAlias*/ false);
    if (d->voiceprints) { d->voiceprints->renamePerson(o, n); emit voiceprintsChanged(); }

    const QVector<Meeting> all = d->store->loadAll();
    for (const Meeting& idx : all) {
        Meeting m = d->store->load(idx.id);
        bool changed = false;
        // 1) Meglévő leképezés-ÉRTÉKEK átírása (diarizált → név).
        for (auto it = m.speakerMap.begin(); it != m.speakerMap.end(); ++it)
            if (it.value() == o) { it.value() = n; changed = true; }
        // 2) Befagyott sáv-beszélő (mic) átnevezése: a track.speakerLabel frissítése,
        //    és — mivel a nyers tokenek továbbra is "o"-t mondanak — egy o→n leképezés,
        //    hogy a már elkészült átirat is a friss nevet mutassa.
        bool micHadOld = false;
        for (Track& tr : m.tracks)
            if (tr.speakerLabel == o) { tr.speakerLabel = n; micHadOld = true; }
        if (micHadOld) { m.speakerMap.insert(o, n); changed = true; }
        // 3) A kézzel felvett résztvevők (transcript.speakers.json) személyneve.
        if (speakeredit::renamePersonInOverlay(m.folder, o, n)) changed = true;

        if (!changed) continue;
        d->store->saveMeeting(m);
        MergedTranscript merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
        if (!merged.tokens.isEmpty()) {
            speakeredit::applyResolvedSpeakers(merged, m);
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
    if (d->details) d->details->remove(nm);
    if (d->voiceprints) { d->voiceprints->removePerson(nm); emit voiceprintsChanged(); }

    const QVector<Meeting> all = d->store->loadAll();
    for (const Meeting& idx : all) {
        Meeting m = d->store->load(idx.id);
        bool changed = false;
        const QList<QString> keys = m.speakerMap.keys();
        for (const QString& key : keys)
            if (m.speakerMap.value(key) == nm) { m.speakerMap.remove(key); changed = true; }
        if (speakeredit::removePersonFromOverlay(m.folder, nm)) changed = true;
        if (!changed) continue;
        d->store->saveMeeting(m);
        MergedTranscript merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
        if (!merged.tokens.isEmpty()) {
            speakeredit::applyResolvedSpeakers(merged, m);
            writeTextFile(QDir(m.folder).filePath(QStringLiteral("transcript.md")), merged.renderMarkdown());
        }
        emit speakerMapChanged(m.id);
    }
    emit peopleChanged();
}

QStringList AppController::meetingsForPerson(const QString& name) const {
    QStringList out;
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return out;
    const QVector<Meeting> all = d->store->loadAll();
    for (const Meeting& idx : all) {
        const Meeting m = d->store->load(idx.id);
        bool present = false;
        for (const QString& v : m.speakerMap) if (v == nm) { present = true; break; }
        if (!present)
            for (const Track& tr : m.tracks) if (tr.speakerLabel == nm) { present = true; break; }
        if (present)
            out << QStringLiteral("%1 (%2)")
                       .arg(m.title, m.startedAt.toString(QStringLiteral("yyyy-MM-dd")));
    }
    return out;
}

void AppController::renameMeeting(const QString& meetingId, const QString& newTitle) {
    const QString t = newTitle.trimmed();
    if (t.isEmpty()) return;
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    m.title = t;
    d->store->saveMeeting(m);   // meeting.json + index frissül → meetingUpdated jel
}

void AppController::setMeetingContextNote(const QString& meetingId, const QString& note) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    const QString n = note.trimmed();
    if (m.contextNote == n) return;   // nincs változás
    m.contextNote = n;
    d->store->saveMeeting(m);
}

void AppController::deleteMeeting(const QString& meetingId) {
    if (meetingId.isEmpty()) return;
    // Futó feladatok leállítása, mielőtt a mappa eltűnik alóluk.
    cancelAllJobs(meetingId);
    d->waveforms->cancel(meetingId);
    closeSpeakerEditor(meetingId);   // a nyitott beszélő-szerkesztő ne írjon a törölt mappába
    d->mergedCache.remove(meetingId);
    d->store->deleteMeeting(meetingId);   // meetingRemoved jel a store-ból
}

void AppController::restoreTrack(const QString& meetingId, const QString& trackId) {
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;
    bool changed = false;
    for (Track& t : m.tracks)
        if (t.id == trackId && !t.active) { t.active = true; changed = true; }
    if (!changed) return;
    m.mixdownDirty = true;   // megváltozott az aktív sáv-halmaz → a mixdown elavult
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
            // A hangfájl FIZIKAI törlése (explicit user-művelet) — kivéve, ha a fájl a
            // LEKEVERÉS (régi meeting.json-ban sávként szerepelhet), vagy más sáv is erre a
            // fájlra hivatkozik: ilyenkor csak a sáv-bejegyzés törlődik.
            bool shared = t.file.isEmpty() || (!m.mixdownFile.isEmpty() && t.file == m.mixdownFile);
            for (const Track& o : m.tracks)
                if (o.id != trackId && o.file == t.file) shared = true;
            if (!shared) {
                QFile::remove(QDir(m.folder).filePath(t.file));
                WaveformService::removeCache(QDir(m.folder).filePath(t.file));   // a hullámforma-cache is
            }
            removed = true;
        } else {
            kept.push_back(t);
        }
    }
    if (!removed) return;
    m.tracks = kept;
    m.mixdownDirty = true;   // megváltozott az aktív sáv-halmaz → a mixdown elavult
    d->store->saveMeeting(m);
    emit tracksChanged(meetingId);
}

void AppController::regenerateMixdown(const QString& meetingId) {
    // Fut már egy keverés ezen a meetingen → no-op (a futó eredménye mindenkinek jó lesz).
    if (d->mixdownProcs.contains(meetingId)) return;

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;

    d->mixdownFailReason.remove(meetingId);
    const QString outRel = QStringLiteral("mixdown.mp3");
    const QString outPath = QDir(m.folder).filePath(outRel);

    // Csak az aktív, lemezen meglévő sávok kerülnek a keverékbe.
    QStringList inArgs;
    QStringList missing;   // aktív sávok, amelyeknek nincs meg a hangfájlja (megjelenített név)
    int inputs = 0;
    const QVector<TrackView> views = TrackCatalog::tracks(m);
    for (int i = 0; i < m.tracks.size(); ++i) {
        const Track& t = m.tracks.at(i);
        if (!t.active) continue;
        const QString path = QDir(m.folder).filePath(t.file);
        if (t.file.isEmpty() || !QFileInfo(path).isFile()) {
            missing << views.value(i).displayName;
            continue;
        }
        inArgs << QStringLiteral("-i") << path;
        ++inputs;
    }
    // Hiányzó aktív sáv mellett a MEGLÉVŐ (teljes) keveréket nem cseréljük le egy
    // részlegesre — sem kézi újrakeverésnél, sem az átírás előtti automatikusnál.
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
    if (inputs == 0) {
        emit errorOccurred(tr("Nincs aktív hangsáv a lekeveréshez."));
        emit mixdownUpdated(meetingId, false);
        return;
    }

    // Félkész fájlba keverünk, és csak SIKER után cseréljük le a régit — megszakítás vagy
    // hiba esetén a korábbi (még lejátszható) keverék érintetlen marad.
    const QString partPath = QDir(m.folder).filePath(QStringLiteral("mixdown.part.mp3"));

    QStringList args{QStringLiteral("-hide_banner"),
                     QStringLiteral("-loglevel"), QStringLiteral("error")};
    args += inArgs;
    // Loudness-normalizálás (EBU R128, -16 LUFS, true-peak -1.5 dBTP) — felhozza a
    // halk beszédet kényelmes lejátszási hangerőre (lásd RecordingSession). STT-t nem érint.
    const QString kLoudnorm = QStringLiteral(
        "loudnorm=I=-16,acompressor=threshold=-24dB:ratio=4:makeup=10,alimiter=limit=0.97");
    if (inputs > 1) {
        args << QStringLiteral("-filter_complex")
             << QStringLiteral("amix=inputs=%1:duration=longest:normalize=0,%2")
                    .arg(inputs).arg(kLoudnorm);
    } else {
        args << QStringLiteral("-af") << kLoudnorm;   // 1 aktív sáv → csak normalizálás
    }
    // SZTEREÓ kimenet (dual-mono) — a Qt6/PipeWire mono streamet halkan/egy csatornára
    // játszhat; sztereóval mindkét hangszóró megszólal.
    args << QStringLiteral("-ac") << QStringLiteral("2")
         << QStringLiteral("-c:a") << QStringLiteral("libmp3lame")
         << QStringLiteral("-q:a") << QStringLiteral("4");
    // Valós haladás: az ffmpeg kulcs=érték sorokat ír a stdoutra (out_time_us=…), amiből a
    // felvétel hosszához mérve százalékot számolunk → mixdownProgress() a nem-modális UI-nak.
    args << QStringLiteral("-progress") << QStringLiteral("pipe:1") << QStringLiteral("-nostats")
         << QStringLiteral("-y") << partPath;
    const qint64 totalMs = m.durationMs;   // a százalék nevezője

    // Aszinkron QProcess — NEM blokkolja a UI-t (egy 90 perces keverés is futhat), és nem
    // blokkolja új felvétel indítását sem (külön child-process + külön capture-engine).
    auto* proc = new QProcess(this);
    proc->setProgram(QStringLiteral("ffmpeg"));
    proc->setArguments(args);
    d->mixdownProcs.insert(meetingId, proc);
    // Strukturált állapot: önálló keverés → saját (megszakítható) feladat; az átírás részeként
    // futó keverés az átírás-feladat „mixdown” szakaszát mozgatja (lásd reportMixdownPercent).
    if (!d->transcribeInMixPhase(meetingId))
        d->jobs->begin(meetingId, JobKind::Mixdown, tr("Lekeverés"));

    // stdout-parse: out_time_us=<mikroszekundum> → százalék a felvétel hosszához mérve.
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
            else if (key == "out_time_ms") outMs = val.toLongLong() / 1000;  // (ffmpeg: valójában µs)
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
    // A lezárás EGY helyen (normál kilépés, hiba, megszakítás, el sem indult ffmpeg).
    auto done = [this, proc, meetingId, outRel, outPath, partPath](bool exitedOk) {
        if (d->mixdownProcs.value(meetingId) != proc) return;   // már lezártuk
        d->mixdownProcs.remove(meetingId);
        const bool cancelled = d->mixdownCancelled.remove(meetingId);
        bool ok = exitedOk && !cancelled;
        // Csere úgy, hogy a régi keverék hibánál megmarad (nem „töröld, aztán nevezd át”).
        if (ok)
            ok = replaceFile(partPath, outPath);
        if (!ok) QFile::remove(partPath);   // félkész fájl ne maradjon a mappában
        if (ok) {
            // Friss meeting (közben módosulhatott) → mixdownFile + dirty törlése.
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
        emit tracksChanged(meetingId);   // a nézet frissüljön (gomb-állapot, mixdownFile)
        proc->deleteLater();
    };
    connect(proc, &QProcess::finished, this, [done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit && code == 0);
    });
    connect(proc, &QProcess::errorOccurred, this, [done](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) done(false);   // (ilyenkor nincs finished jel)
    });

    // A haladást a mixdownProgress (0..100) jelzi a nem-modális UI-nak — NEM a jobProgress
    // (az a globális busy-jelzőt kapcsolná be, ami egy háttér-lekeverés alatt feleslegesen
    // letiltaná az akció-gombokat). Indító 0%:
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
    // A névváltás propagálódjon MINDEN meetingre: a mic-sáv befagyott beszélőneve,
    // a leképezések és a voiceprintek is átíródnak (renamePerson ezt mind kezeli).
    if (!old.isEmpty())
        renamePerson(old, n);                 // emit peopleChanged + speakerMapChanged-eket bentről
    else {
        if (d->people) d->people->add(n);
        emit peopleChanged();
    }
}

void AppController::fetchLlmModels() {
    if (!d->nam) d->nam = new QNetworkAccessManager(this);
    QString base = d->settings->settings().llmSelected().baseUrl;
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    QNetworkRequest req{QUrl(base + QStringLiteral("/models"))};
    const QString key = d->keyStore.get(keys::LlmApiKey);
    if (!key.isEmpty())
        req.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + key.toUtf8());
    QNetworkReply* reply = d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit llmModelsFailed(reply->errorString());
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QStringList models;
        for (const QJsonValue& v : root.value(QStringLiteral("data")).toArray()) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            if (!id.isEmpty()) models << id;
        }
        models.sort();
        emit llmModelsFetched(models);
    });
}

void AppController::startLevelMonitoring() {
    d->monitorLegacy = true;
    beginLevelMonitoring();
}

void AppController::stopLevelMonitoring() {
    d->monitorLegacy = false;
    if (!d->monitorHolders.isEmpty()) return;   // egy másik fogyasztó (retain) még kéri
    endLevelMonitoring();
}

void AppController::retainLevelMonitoring(QObject* owner) {
    if (!owner || d->monitorHolders.contains(owner)) return;
    // Ha a fogyasztó megszűnik, a kérése is: a mikrofon ne maradjon nyitva utána.
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
    if (d->state != RecordingState::Idle && !d->monitorDuringRecording) return;
    if (d->state == RecordingState::Stopping || d->state == RecordingState::Encoding) return;
    d->devices->refresh();
    restartLevelMonitor(/*force*/ true);
}

void AppController::endLevelMonitoring() {
    d->monitorWanted = false;
    d->monitorNames.clear();
    if (d->monitor) d->monitor->stop();
}

void AppController::setMonitorDuringRecording(bool on) { d->monitorDuringRecording = on; }
void AppController::setAutoMixdownAfterRecording(bool on) { d->autoMixdown = on; }

void AppController::restartLevelMonitor(bool force) {
    if (!d->monitor || !d->monitorWanted) return;
    const bool recording = d->state == RecordingState::Recording;
    if (d->state != RecordingState::Idle && !(recording && d->monitorDuringRecording)) return;
    QVector<AudioDeviceInfo> list;
    QStringList names;
    for (const AudioDeviceInfo& dev : d->devices->captureDevices()) {
        // Felvétel alatt a sávon lévő eszközöket a RecordingSession birtokolja és méri.
        if (recording && d->recNames.contains(dev.name) && !d->recClosed.contains(dev.name))
            continue;
        list.push_back(dev);
        names << dev.name;
    }
    if (!force && d->monitor->active() && names == d->monitorNames) return;
    d->monitorNames = names;
    d->monitor->start(list);   // üres listára leáll
}

void AppController::handleDeviceSetChange() {
    const QVector<AudioDeviceInfo> present = d->devices->captureDevices();
    if (d->state == RecordingState::Recording && d->session && !present.isEmpty()) {
        // Hot-plug felvétel közben: a felsorolásból KÉTSZER egymás után hiányzó rögzített
        // eszköz sávját biztonságosan lezárjuk (egyetlen átmeneti hiány ne zárjon sávot; az
        // üres lista — elérhetetlen hangrendszer — pedig semmit nem zár le).
        bool recheck = false;
        const QStringList rec = d->recNames;
        for (const QString& name : rec) {
            if (d->recClosed.contains(name)) continue;
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
        if (!d->recClosed.contains(n)) out << n;
    return out;
}

QStringList AppController::disconnectedRecordingDeviceNames() const { return d->recClosed; }

bool AppController::addRecordingDevice(const AudioDeviceInfo& device) {
    if (d->state != RecordingState::Recording || !d->session) return false;
    return d->session->addDevice(device);
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
    // A monitor felszabadítja az eszközöket a felvétel előtt (a kérés — monitorWanted —
    // megmarad: setMonitorDuringRecording mellett a felvétel alatt a maradékon újraindul).
    d->monitorNames.clear();
    if (d->monitor) d->monitor->stop();
    d->recNames.clear();
    d->recClosed.clear();
    d->recMissing.clear();
    QVector<AudioDeviceInfo> use = devices;
    if (use.isEmpty()) {
        d->devices->refresh();
        use = d->devices->autoRecordDevices();   // line-in/AUX kihagyva az auto-halmazból
    }
    if (use.isEmpty()) {
        emit errorOccurred(tr("Nincs felvehető hangeszköz."));
        return;
    }

    // Megjegyezzük a használt eszközöket (induláskor előpipáláshoz).
    QStringList usedNames;
    for (const auto& dvc : use) usedNames << dvc.name;
    setLastUsedDeviceNames(usedNames);

    const AppSettings s = d->settings->settings();
    auto* sess = new RecordingSession(d->audioDir, title, s.userSpeakerName,
                                      opusBitrateKbps(s.audioQuality), this);
    d->session = sess;

    connect(sess, &RecordingSession::stateChanged, this, [this, sess](RecordingState st) {
        d->state = st;
        // A meeting-mappa a start()-ban már létrejött → a felvétel ELEJÉN elérhető
        // (nem csak a finished-nél). Kell a --record lockhoz + a currentMeetingFolder()-höz.
        if (st == RecordingState::Recording) {
            d->currentFolder = sess->folder();
            d->recNames.clear();
            for (const AudioDeviceInfo& dev : sess->trackDevices()) d->recNames << dev.name;
            startCallEndMonitor();
            restartLevelMonitor(/*force*/ true);   // csak ha a UI kérte (monitorDuringRecording)
        } else if (st == RecordingState::Stopping) {
            // A lezárás alatt a figyelő is leáll; a felvétel végén (ha kell) újraindul.
            d->monitorNames.clear();
            if (d->monitor) d->monitor->stop();
        } else if (st == RecordingState::Idle) {
            stopCallEndMonitor();
        }
        emit recordingStateChanged(st);
    });
    connect(sess, &RecordingSession::levelMeterUpdated, this, &AppController::levelMeterUpdated);
    connect(sess, &RecordingSession::trackLevel, this, [this](int idx, float rms, float peak) {
        if (idx >= 0 && idx < d->recNames.size())
            emit deviceLevelPeak(d->recNames.at(idx), rms, peak);
    });
    connect(sess, &RecordingSession::trackAdded, this, [this](int, const QString& name) {
        d->recNames << name;
        d->recClosed.removeAll(name);   // visszadugott, újra felvett eszköz
        if (!d->lastDevices.contains(name)) {
            QStringList names = d->lastDevices;
            names << name;
            setLastUsedDeviceNames(names);
        }
        restartLevelMonitor(/*force*/ false);
        emit recordingTrackAdded(name);
    });
    connect(sess, &RecordingSession::trackClosed, this, [this](int, const QString& name) {
        if (!d->recClosed.contains(name)) d->recClosed << name;
        emit recordingTrackClosed(name);
    });
    // Csend-figyelés: bármely sáv beszéd-szintű RMS-e „hangos” → időbélyeg frissül,
    // és a csend-kérdés újra-élesedik. Küszöb a RecordingSession kSilencePeak-jével összhangban.
    connect(sess, &RecordingSession::levelMeterUpdated, this, [this](int, float rms) {
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
    connect(sess, &RecordingSession::elapsedChanged, this, &AppController::elapsedChanged);
    connect(sess, &RecordingSession::failed, this, [this](QString e) {
        stopCallEndMonitor();
        emit errorOccurred(e);
        if (d->session) { d->session->deleteLater(); d->session = nullptr; }
        d->state = RecordingState::Idle;
        d->recNames.clear();
        d->recClosed.clear();
        emit recordingStateChanged(d->state);
        restartLevelMonitor(/*force*/ true);
    });
    connect(sess, &RecordingSession::finished, this, [this](Meeting m) {
        stopCallEndMonitor();
        d->currentFolder = m.folder;
        d->store->saveMeeting(m);
        if (d->session) { d->session->deleteLater(); d->session = nullptr; }
        d->state = RecordingState::Idle;
        d->recNames.clear();
        d->recClosed.clear();
        emit recordingFinished(m);
        emit recordingStateChanged(d->state);
        if (d->monitorDuringRecording) restartLevelMonitor(/*force*/ true);
        // Lekeverés (mixdown) leválasztva a stop()-ról: itt indítjuk ASZINKRON, csak ha a
        // beállítás "auto". Nem blokkol → azonnal indítható új felvétel. Kézi módban a
        // felhasználó a review-panel „Lekeverés" gombjával indítja. (A mixdown csak
        // hallgatásra kell; az átíráshoz a per-sáv .ogg-k elegendők.)
        // Az önálló felvevő-folyamatban (setAutoMixdownAfterRecording(false)) nem indul: az a
        // folyamat kilép a felvétel után, a lekeverést az elemző készíti el, amikor kell.
        if (d->autoMixdown && d->settings->settings().mixdownMode != QStringLiteral("manual")
            && m.mixdownFile.isEmpty())
            regenerateMixdown(m.id);
    });

    sess->start(use);
}

// ---- hívás-vég figyelés felvétel közben ---------------------------------------------
// Ugyanaz a detektor-mag, amit a tanara-watcher használ. A saját capture-t a detektor a
// selfBinary alapján kizárja. Csak JELEZ (callEnded) — a leállításról a user dönt.
void AppController::startCallEndMonitor()
{
    stopCallEndMonitor();
    const AppSettings s = d->settings->settings();
    if (!s.askStopOnCallEnd)
        return;
    registerBuiltinDetectors();
    d->callDetector.reset(s.detectorId.isEmpty()
        ? MeetingDetectorRegistry::instance().createBest()
        : MeetingDetectorRegistry::instance().create(s.detectorId));
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
    const MeetingSignal sig = d->callDetector->poll();
    if (sig.active) {
        d->callSeenActive = true;
        d->callInactivePolls = 0;
        if (!sig.appName.isEmpty()) d->callAppName = sig.appName;
        return;
    }
    if (!d->callSeenActive)
        return;   // még nem is láttunk hívást (kézi felvétel hívás nélkül) — nincs mit jelezni
    // Debounce: 2 egymást követő inaktív poll (egy pillanatnyi mikrofon-elengedés ne kérdezzen).
    if (++d->callInactivePolls >= 2) {
        const QString app = d->callAppName;
        d->callSeenActive = false;   // újra-élesedik, ha később megint aktív lesz a hívás
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

ReadinessResult AppController::canRun(WorkflowStep step, const QString& meetingId) const
{
    const Meeting meeting = d->store->load(meetingId);
    ReadinessModel model(d->settings->settings(),
                         [this](const QString& k) { return !d->keyStore.get(k).isEmpty(); });
    ReadinessResult r = model.check(step, meeting);
    if (!r.runnable || !usesCloud(step))
        return r;

    // Tanara Cloud-specifikus akadályok (pontosan egy CTA a folyamat-sávban).
    if (d->cloud->clientTooOld()) {
        ReadinessResult c;
        c.blockerKind   = BlockerKind::Cloud;
        c.detail        = tr("A Tanara Cloudhoz frissítés kell (legalább %1).").arg(d->cloud->minClient());
        c.fixActionHint = QStringLiteral("cloud:update");
        c.providerId    = cloud::ProviderId;
        return c;
    }
    const AccountInfo acc = d->cloud->account();
    if (acc.valid && acc.balanceEmpty) {
        ReadinessResult c;
        c.blockerKind   = BlockerKind::Cloud;
        c.detail        = tr("töltsd fel az egyenleged");
        c.fixActionHint = QStringLiteral("cloud:topup");
        c.providerId    = cloud::ProviderId;
        return c;
    }
    return r;
}

// ---- Tanara Cloud ------------------------------------------------------------------------

CloudAccount* AppController::cloud() const { return d->cloud; }
bool AppController::cloudLive() const { return d->cloudLive; }
bool AppController::cloudTeaser() const { return d->cloudTeaser; }

bool AppController::usesCloud(WorkflowStep step) const
{
    if (!d->cloudLive) return false;
    const AppSettings s = d->settings->settings();
    if (step == WorkflowStep::Transcribe) return s.sttProviderId == cloud::ProviderId;
    if (step == WorkflowStep::Summarize)  return s.llmProviderId == cloud::ProviderId;
    return false;
}

QString AppController::cloudModelFor(WorkflowStep step) const
{
    const AppSettings s = d->settings->settings();
    const bool stt = step == WorkflowStep::Transcribe;
    const QString expert = stt ? s.cloudSttModel : s.cloudLlmModel;
    // Az Expert-modell csak akkor érvényes, ha még a katalógusban van (kivezetett modell →
    // vissza a tier virtuális modelljére; a UI jelzi).
    if (!expert.isEmpty()) {
        const QVector<CloudModel> cat = d->cloud->models();
        if (cat.isEmpty() || findModel(cat, expert).has_value())
            return expert;
    }
    return virtualModelId(stt ? QStringLiteral("stt") : QStringLiteral("llm"),
                          stt ? s.cloudSttTier : s.cloudLlmTier);
}

namespace {
// A cloud LLM-hívások max_tokens-e: a fedezet-szabály (prompt + max_tokens áron) miatt
// mérsékelt érték — a JSON-összefoglaló és a téma-elemzés bőven belefér.
constexpr int kCloudMaxTokens = 4000;
constexpr int kReduceInputChars = 6000;   // a reduce bemenete: a téma-elemzések kivonata (becslés)
}

EstimateRequest AppController::makeEstimateRequest(const QString& meetingId, const QString& task,
                                                   const QString& summaryMode) const
{
    const Meeting m = d->store->load(meetingId);
    const AppSettings s = d->settings->settings();
    EstimateRequest r;
    r.task = task;
    r.durationMs = m.durationMs;
    for (const Track& t : m.tracks) if (t.active) ++r.tracks;
    r.language = s.languageHints.value(0);
    if (task != QLatin1String("summarize"))
        r.sttModel = cloudModelFor(WorkflowStep::Transcribe);
    if (task != QLatin1String("transcribe")) {
        r.llmModel = cloudModelFor(WorkflowStep::Summarize);
        r.summaryMode = summaryMode.isEmpty() ? QStringLiteral("quick") : summaryMode;
        // A tényleges bemenet: rendszer-prompt + kontextus + átirat (markdown).
        MergedTranscript merged = d->mergedCache.value(meetingId);
        if (merged.tokens.isEmpty() && !m.folder.isEmpty())
            merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
        if (!merged.tokens.isEmpty()) {
            speakeredit::applyResolvedSpeakers(merged, m);
            const int transcript = merged.renderMarkdown().size() + m.contextNote.size();
            r.transcriptChars = transcript;
            if (r.summaryMode == QLatin1String("complex")) {
                const int topicIn    = transcript + resolvedPrompt(s, s.topicExtractionPrompt, "topic").size();
                const int analysisIn = transcript + resolvedPrompt(s, s.topicAnalysisPrompt, "analysis").size() + 200;
                // A témaszám a kinyerés előtt ismeretlen (null → a gateway statisztikája);
                // ha már van szerkesztett téma-lista, csak a még elemzetleneket kérjük.
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
                const int in = transcript + resolvedPrompt(s, s.summaryPrompt, "simple").size();
                r.llmCalls.append({ 1, in, kCloudMaxTokens });
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

ProviderConfig AppController::cloudConfig(WorkflowStep step, const CloudRunPtr& run,
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
                                                step == WorkflowStep::Summarize ? summaryMode : QString());
    QPointer<CloudAccount> acc = d->cloud;
    cfg.onExchange = [acc, run](const HttpExchange& ex) {
        if (acc) acc->observeExchange(ex);
        if (!run) return;
        const QString rid = QString::fromUtf8(ex.headers.value(QByteArrayLiteral("x-tanara-request-id")));
        if (!rid.isEmpty()) run->lastRequestId = rid;
        const bool ok = ex.status >= 200 && ex.status < 300;
        if (!ok) {
            if (ex.method != "DELETE")   // a takarító DELETE-ek hibája nem írja felül a valódi hibát
                run->lastError = parseCloudError(ex.status, ex.headers, ex.body, ex.networkError);
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(ex.body).object();
        static const QRegularExpression statusPath(QStringLiteral("/transcriptions/[^/]+$"));
        if (ex.method == "POST" && ex.path.endsWith(QLatin1String("/files"))) {
            run->fileId = o.value(QStringLiteral("id")).toString();
        } else if (ex.method == "POST" && ex.path.endsWith(QLatin1String("/transcriptions"))) {
            run->transcriptionId = o.value(QStringLiteral("id")).toString();
            ChargeInfo c = chargeInfoFromJson(o.value(QStringLiteral("tanara")).toObject());
            if (!c.valid) c = chargeInfoFromHeaders(ex.headers);
            run->addCharge(c);
            // A folyamatban lévő átírás azonosítója (indításkori visszaírás-ellenőrzéshez).
            if (acc) acc->addPendingTranscription(run->transcriptionId, run->fileId);
        } else if (ex.method == "POST" && ex.path.endsWith(QLatin1String("/chat/completions"))) {
            ChargeInfo c = chargeInfoFromJson(o.value(QStringLiteral("usage")).toObject()
                                                  .value(QStringLiteral("tanara_charge")).toObject());
            if (!c.valid) c = chargeInfoFromHeaders(ex.headers);
            run->addCharge(c);
        } else if (ex.method == "GET" && statusPath.match(ex.path).hasMatch()) {
            // A visszaírás abban a válaszban látszik, amelyik először ad status: error-t —
            // a provider a DELETE-et CSAK ez után küldi (a tanara-mezőt előbb kiolvassuk).
            const TranscriptionJobInfo j = transcriptionJobInfoFromJson(o);
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
    if (!usesCloud(WorkflowStep::Summarize)) {
        ProviderConfig cfg = d->settings->settings().llmSelected();
        cfg.apiKey = d->keyStore.get(keys::LlmApiKey);   // LM Studio: lehet üres
        return cfg;
    }
    if (!run) run = newCloudRun(meetingId, kind);
    return cloudConfig(WorkflowStep::Summarize, run, summaryMode);
}

void AppController::finishCloudRun(const CloudRunPtr& run)
{
    if (!run) return;
    if (!run->transcriptionId.isEmpty())
        d->cloud->removePendingTranscription(run->transcriptionId);
    if (run->calls > 0)
        emit cloudCharged(run->meetingId, run->kind, run->charged(), run->calls, run->balance, run->vatMode);
    if (run->balance.isValid() || run->calls > 0)
        d->cloud->refreshAccount();   // low_balance / hours_left / notices frissítése
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
        if (run->lastError.kind == CloudErrorKind::ClientTooOld)
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
        return;   // már fut ezen a meetingen (dupla kattintás / két nézet)

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) {
        d->clearSpeakersOnTranscript.remove(meetingId);
        emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId));
        return;
    }

    ReadinessResult res = canRun(WorkflowStep::Transcribe, meetingId);
    if (!res.runnable) {
        d->clearSpeakersOnTranscript.remove(meetingId);
        emit errorOccurred(res.detail);
        return;
    }

    // Egy önállóan futó azonosítás a MOSTANI átirat címkéire vonatkozik: leállítjuk, hogy a
    // nevei ne kerüljenek rá a hamarosan érkező ÚJ átiratra (az eredményét a befejezésekor
    // az átirat-változás ellenőrzése is eldobja — lásd startIdentify).
    cancelJob(meetingId, JobKind::Identify);

    // A leirat a MIXDOWNból készül (egyetlen hangfolyam → nincs sávonkénti átfedés-
    // összefésülés/duplikáció, ~N× helyett 1× Soniox-költség). Ha a mixdown hiányzik vagy
    // elavult, előbb legyártjuk, és a mixdownUpdated jelre indítjuk az átírást.
    const QString mixPath =
        QDir(m.folder).filePath(m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3")
                                                        : m.mixdownFile);
    const bool needMix = m.mixdownFile.isEmpty() || m.mixdownDirty || !QFile::exists(mixPath);

    // Strukturált feladat: szakasz-lista (M04). A lekeverés csak akkor szakasz, ha kell;
    // az azonosítás csak akkor, ha van hang-modell.
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
                if (it == d->transcribeRuns.end()) return;   // közben megszakították
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
                je.detail = d->mixdownFailReason.take(meetingId);   // pl. melyik sáv fájlja hiányzik
                d->jobs->fail(meetingId, JobKind::Transcribe, je);
                emit errorOccurred(msg);
            });
        Impl::TranscribeRun& tr_ = d->transcribeRuns[meetingId];
        tr_.mixPhase = true;
        tr_.mixConn = conn;
        // Ha már futott egy önálló keverés, arra várunk — megszakításkor azt nem állítjuk le.
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
        return;   // már fut egy átírás ezen a meetingen
    // A mostani átirat (a nevekkel és a kézi javításokkal) másolatként megmarad, ha kérték.
    if (keepBackup) speakeredit::backupTranscript(m);
    // A beszélő-hozzárendelések (speakerMap) ÉS a kézi sor-javítások (overlay) egyaránt az ÚJ
    // átirat megérkezésekor törlődnek, nem előre: a speakerMap az átírás finished-ágában, az
    // overlay a transcriptReady horogban. Ha az újra-átírás megszakad vagy elbukik, a régi
    // átirat a neveivel és a soronkénti javításaival együtt érintetlen marad.
    d->clearSpeakersOnTranscript.insert(meetingId);
    transcribeMeeting(meetingId);
}

void AppController::transcribeFromMixdown(const QString& meetingId)
{
    // A feladat lezárása hibával még a szolgáltató-hívás előtt (belső segéd).
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
    // Tanara Cloud: ugyanaz a Soniox-provider a gateway-configgal; a futás (job id, terhelés,
    // visszaírás, hiba) a válasz-hookból töltődik.
    CloudRunPtr run;
    bool cloudDiarization = true;
    if (usesCloud(WorkflowStep::Transcribe)) {
        run = newCloudRun(meetingId, QStringLiteral("transcribe"));
        cfg = cloudConfig(WorkflowStep::Transcribe, run, QString());
        const std::optional<CloudModel> cm = findModel(d->cloud->models(), cfg.model);
        cloudDiarization = !cm.has_value() || cm->diarization;
    } else {
        // Az API-kulcs slotja provider-függő — a descriptor secretKey-e mondja meg
        // (soniox.apiKey / stt.whisper.apiKey / …), nem hardcode-oljuk a Sonioxra.
        const ProviderDescriptor sttDesc = SttProviderRegistry::instance().descriptor(sttId);
        for (const ConfigField& f : sttDesc.fields)
            if (f.isSecret && !f.secretKey.isEmpty()) {
                cfg.apiKey = d->keyStore.get(f.secretKey);
                break;
            }
    }
    // Az utolsó sikertelen HTTP-váltás megjegyzése (saját kulcsos módban is) — ebből lesz a
    // megmaradó hiba technikai sora („HTTP 401 · invalid_api_key”).
    const auto sink = std::make_shared<FailureSink>();
    captureFailures(cfg, sink);

    ISttProvider* provider = SttProviderRegistry::instance().create(sttId, cfg, this);
    if (!provider) {
        abortJob(tr("Ismeretlen STT-provider: %1.").arg(sttId));
        return;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    emit jobProgress(m.id, tr("Átírás indítása…"));

    // Context-envelope → a Soniox strukturált „context" objektuma (general/text/terms).
    // Forrás: cím (general) + a felhasználó pár szavas leírása (text) + a résztvevő-nevek
    // (terms; az aktív sávok fix beszélői — később naptár-bejegyzés is).
    QMap<QString, QString> ctxGeneral;
    if (!m.title.trimmed().isEmpty())
        ctxGeneral.insert(QStringLiteral("Megbeszélés"), m.title.trimmed());
    QStringList participants;
    for (const Track& t : m.tracks) {
        const QString lbl = t.speakerLabel.trimmed();
        if (t.active && !lbl.isEmpty() && !participants.contains(lbl))
            participants << lbl;
    }

    const QString mixPath =
        QDir(m.folder).filePath(m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3")
                                                        : m.mixdownFile);

    // EGYETLEN kérés a mixdownnal. A beszélő-szeparációt a Soniox diarizációja adja
    // („Beszélő N" címkék); a NEVET utólag a voice-ID / kézi átnevezés oldja fel.
    SttRequest req;
    req.audioFilePath = mixPath;
    req.trackId = QStringLiteral("mixdown");
    req.languageHints = s.languageHints;
    req.contextGeneral = ctxGeneral;
    req.context = m.contextNote.trimmed();
    req.contextTerms = participants;
    req.diarization = true;
    if (run && !cloudDiarization) {
        // A választott cloud-szint nem diarizál (pl. Gyors): mindenki „Beszélő 1” lesz
        // (a felvétel sávjai megmaradnak; a K-04 figyelmeztetett).
        req.diarization = false;
        req.speakerLabel = QStringLiteral("1");
    }
    const bool diarizing = req.diarization;

    SttJob* job = provider->transcribe(req);

    // Strukturált állapot: megszakításhoz a job, becsléshez a korábbi futások sebessége
    // (ugyanazzal a szolgáltatóval/modellel); minta nélkül nincs becslés.
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

    connect(job, &SttJob::uploadProgress, this, [this, meetingId](qint64 sent, qint64 total) {
        if (total <= 0) return;
        // Valós bájt-arány. A 100% után a szolgáltató állapotváltása zárja le a szakaszt.
        const JobProgress jp = d->jobs->job(meetingId, JobKind::Transcribe);
        const JobStage* st = jp.stage(QStringLiteral("upload"));
        if (st && st->state == StageState::Running)
            d->jobs->setStagePercent(meetingId, JobKind::Transcribe, QStringLiteral("upload"),
                                     int(sent * 100 / total));
    });
    connect(job, &SttJob::stateChanged, this, [this, id = m.id, diarizing](JobState st) {
        emit jobProgress(id, sttPhase(st));
        // A szolgáltatók az átírás alatt NEM adnak százalékot — csak állapotot (feltöltés /
        // sorban áll / feldolgozás). A beszélő-szétválasztást a szolgáltató az átírással egy
        // menetben végzi, ezért a két szakasz együtt fut és együtt zárul.
        const JobKind k = JobKind::Transcribe;
        if (st == JobState::Uploading) {
            d->jobs->setStage(id, k, QStringLiteral("upload"), StageState::Running, 0);
        } else if (st == JobState::Queued || st == JobState::Processing) {
            d->jobs->setStage(id, k, QStringLiteral("upload"), StageState::Done);
            d->jobs->setStage(id, k, QStringLiteral("transcribe"), StageState::Running);
            d->jobs->setStageDetail(id, k, QStringLiteral("transcribe"),
                                    st == JobState::Queued ? tr("sorban áll") : QString());
            if (diarizing)
                d->jobs->setStage(id, k, QStringLiteral("diarize"), StageState::Running);
        }
    });
    // A részletes poll-üzenet (eltelt idő + életjel) is jusson ki a UI-ra, hogy a hosszú
    // async feldolgozás alatt látszódjon: fut és a kapcsolat él.
    connect(job, &SttJob::progress, this, [this, id = m.id](int, const QString& msg) {
        emit jobProgress(id, msg);
        d->jobs->setMessage(id, JobKind::Transcribe, msg);
    });
    connect(job, &SttJob::finished, this,
            [this, meetingId, providerObj, run, job, statsKey, sttClock, diarizing](const TrackTranscript& result) {
        d->transcribeRuns.remove(meetingId);
        if (providerObj) providerObj->deleteLater();
        // A job a best-effort takarító DELETE-jeit még kiküldi; utána törölhető.
        QTimer::singleShot(30000, job, &QObject::deleteLater);

        // FRISS meeting: az átírás percekig futhat, közben a felhasználó átnevezhette,
        // szerkeszthette (vagy törölhette) a meetinget — a régi példány mentése felülírná.
        Meeting mm = d->store->load(meetingId);
        if (mm.id.isEmpty()) {
            d->clearSpeakersOnTranscript.remove(meetingId);
            d->jobs->cancelled(meetingId, JobKind::Transcribe);
            finishCloudRun(run);
            return;
        }

        // ÜRES eredmény (a szolgáltató egyetlen szót sem adott vissza) = HIBA: nem írjuk rá a
        // meglévő átiratra. A korábbi átirat, a beszélő-nevek és a kézi javítások érintetlenek.
        if (result.tokens.isEmpty()) {
            d->clearSpeakersOnTranscript.remove(meetingId);
            const QString msg = mm.hasTranscript
                ? tr("Az átírás üres eredményt adott (a szolgáltató nem talált beszédet a felvételen) — "
                     "a korábbi átirat megmaradt.")
                : tr("Az átírás üres eredményt adott: a szolgáltató nem talált beszédet a felvételen.");
            JobError je;
            je.message = msg;
            d->jobs->fail(meetingId, JobKind::Transcribe, je);
            finishCloudRun(run);   // a futás lezárul (az esetleges terhelés így is megjelenik)
            emit errorOccurred(msg);
            return;
        }
        // Volt-e már átirat EZELŐTT a futás előtt (a flag-től függetlenül — lásd lent).
        const bool hadTranscript = mm.hasTranscript
            || QFile::exists(QDir(mm.folder).filePath(QStringLiteral("transcript.tokens.json")));

        TrackTranscript res = result;
        // A Soniox diarizációs id-ket (1,2,…) semleges „Beszélő N" címkére fordítjuk.
        for (TranscriptToken& tok : res.tokens)
            tok.speaker = tok.speaker.isEmpty()
                ? QStringLiteral("Beszélő")
                : QStringLiteral("Beszélő %1").arg(tok.speaker);

        QVector<TrackTranscript> single{res};
        MergedTranscript merged = mergeTranscripts(single);
        const QString mdPath = QDir(mm.folder).filePath(QStringLiteral("transcript.md"));
        writeTextFile(mdPath, merged.renderMarkdown());
        writeTokensJson(QDir(mm.folder).filePath(QStringLiteral("transcript.tokens.json")), merged);
        writeSegmentsJson(QDir(mm.folder).filePath(QStringLiteral("transcript.segments.json")), merged.segments());
        d->mergedCache.insert(mm.id, merged);
        ++d->transcriptGen[meetingId];   // új átirat → a futó azonosítások eredménye elavult
        // Újra-átírás: az új diarizáció „Beszélő N” címkéi MÁS embereket jelölhetnek — a régi
        // hozzárendelések az ÚJ átirattal együtt törlődnek. NEM csak a retranscribeMeeting
        // jelzőjére: az hibánál elvész, és az utána jövő sima (újrapróbált) átírás a régi
        // neveket hagyná az új címkéken. Ezért: ha volt már átirat, mindig törlünk.
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
        d->jobs->markIdentified(meetingId, false);   // az új átiratra még nem futott azonosítás

        emit transcriptReady(mm.id, mdPath);
        finishCloudRun(run);   // K-07: „Ez az átírás $0,41 volt.” (csak cloud-futásnál)
        // Voice-ID: a diarizált beszélők auto-párosítása a lenyomat-DB ellen — háttérszálon,
        // az átírás-feladat utolsó szakaszaként (a UI közben használható, megszakítható).
        // (Ha a felhasználó az M03 kapcsolóval kihagyta, az átirat névtelen beszélőkkel kész.)
        if (d->skipIdentifyAfterTranscribe.contains(meetingId)
            || !startIdentify(meetingId, /*asTranscribeStage*/ true))
            d->jobs->finish(meetingId, JobKind::Transcribe);
    });
    connect(job, &SttJob::failed, this, [this, meetingId, providerObj, run, job, sink](QString e) {
        const Impl::TranscribeRun info = d->transcribeRuns.take(meetingId);
        d->clearSpeakersOnTranscript.remove(meetingId);
        if (providerObj) providerObj->deleteLater();
        QTimer::singleShot(30000, job, &QObject::deleteLater);   // a takarító DELETE-ek után

        if (info.cancelRequested) {
            // Megszakítás: nincs hiba, nincs errorOccurred. A szolgáltatónál a job takarított
            // (feltöltött hang + átírás törölve); a korábbi átirat (ha volt) érintetlen.
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
            ? describeCloudFailure(JobKind::Transcribe, run->lastError)
            : describeJobFailure(JobKind::Transcribe, e, sink->has ? &sink->ex : nullptr);
        d->jobs->fail(meetingId, JobKind::Transcribe, je);
        failCloudRun(run, tr("Átírás-hiba: %1").arg(e));
    });
}

// A ténylegesen küldendő rendszer-prompt: settings-override → fájl-override
// (<metadataDir>/prompts/<id>.md) → beépített default (PromptLibrary), a végén a
// célnyelv alkalmazásával ({{NYELV}} placeholder / direktíva).
static QString resolvedPrompt(const AppSettings& s, const QString& userOverride, const char* id)
{
    const QString base = userOverride.trimmed().isEmpty()
        ? promptDefault(QLatin1String(id), s.metadataDir)
        : userOverride;
    return applySummaryLanguage(base, s.summaryLanguage);
}

void AppController::summarizeMeeting(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::Summarize))
        return;   // már fut

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    ReadinessResult res = canRun(WorkflowStep::Summarize, meetingId);
    if (!res.runnable) { emit errorOccurred(res.detail); return; }

    MergedTranscript merged = d->mergedCache.value(meetingId);
    if (merged.tokens.isEmpty())
        merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    // Tartalmi védőág: a canRun(Summarize) a meeting.hasTranscript flagre kapuz, de ha a
    // tokens.json hiányzik/üres (kézzel törölt, részleges írás, rosszul bemásolt meeting),
    // ne induljon összefoglaló üres átiratra — tükrözi az eredeti tartalom-alapú guardot.
    if (merged.tokens.isEmpty()) {
        emit errorOccurred(tr("Nincs átirat — előbb futtass átírást."));
        return;
    }
    speakeredit::applyResolvedSpeakers(merged, m);   // a Gemma a valódi neveket lássa

    const AppSettings s = d->settings->settings();
    const QString llmId = s.llmProviderId;
    CloudRunPtr run;
    ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("summary"), QStringLiteral("quick"));
    const auto sink = std::make_shared<FailureSink>();
    captureFailures(cfg, sink);
    ILlmProvider* provider = LlmProviderRegistry::instance().create(llmId, cfg, this);
    if (!provider) {
        emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(llmId));
        return;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new SummaryService(provider, this);
    emit jobProgress(meetingId, run ? tr("Összefoglalás a Tanara Cloudban…")
                                    : tr("Összefoglalás a helyi modellel (Gemma)…"));

    // Strukturált feladat (egyetlen, nem-streamelt LLM-hívás: köztes haladás nincs; becslés
    // csak a korábbi, ugyanazzal a modellel mért futásokból — egység: az átirat hossza).
    const QString key = Impl::llmKey(meetingId, JobKind::Summarize);
    const QString statsKey = QStringLiteral("summarize/%1/%2").arg(llmId, cfg.model);
    const double units = double(merged.renderMarkdown().size());
    d->jobs->begin(meetingId, JobKind::Summarize, tr("Összefoglaló készítése"));
    d->jobs->setEstimate(meetingId, JobKind::Summarize, d->jobStats->estimateSec(statsKey, units));
    d->llmRuns.insert(key, Impl::LlmRun{svc, providerObj, run});
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();

    connect(svc, &SummaryService::summaryReady, this,
            [this, meetingId, providerObj, svc, run, key, statsKey, units, clock, llmId,
             model = cfg.model](const Summary& sum) {
        d->llmRuns.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        // FRISS meeting (az összefoglalás alatt módosulhatott / törlődhetett).
        Meeting mm = d->store->load(meetingId);
        if (mm.id.isEmpty()) {
            d->jobs->cancelled(meetingId, JobKind::Summarize);
            finishCloudRun(run);
            return;
        }
        const QString md = sum.renderMarkdown();
        const QString mdPath = QDir(mm.folder).filePath(QStringLiteral("summary.md"));
        writeTextFile(mdPath, md);
        // A strukturált forma + a keletkezés metaadatai (summary.json) — az M07 ebből rajzol.
        SummaryDocument doc;
        doc.exists = true;
        doc.summary = sum;
        doc.markdown = md;
        doc.meta.createdAt = QDateTime::currentDateTime();
        doc.meta.providerId = llmId;
        doc.meta.model = model;
        doc.meta.mode = SummaryMode::Quick;
        summarystore::save(mm.folder, doc);
        // másolat a notes (vault) mappába
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
    connect(svc, &SummaryService::summaryFailed, this,
            [this, meetingId, providerObj, svc, run, key, sink](const QString& e) {
        d->llmRuns.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        d->jobs->fail(meetingId, JobKind::Summarize,
                      d->describeLlmFailure(JobKind::Summarize, e, run, sink));
        failCloudRun(run, tr("Összefoglaló hiba: %1").arg(e));
    });

    svc->summarize(merged, /*contextNotes*/ m.contextNote.trimmed(), /*glossary*/ QStringList(),
                   /*systemPrompt*/ resolvedPrompt(s, s.summaryPrompt, "simple"),
                   cfg.model, cfg.temperature, cfg.maxTokens);
}

// ---- komplex (több körös) összefoglaló ------------------------------------

void AppController::extractMeetingTopics(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::ExtractTopics))
        return;   // már fut

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    // Ha már van (esetleg szerkesztett) téma-lista, azt adjuk vissza — nincs újrakinyerés.
    const QString topicsPath = QDir(m.folder).filePath(QStringLiteral("summary.topics.json"));
    const QVector<SummaryTopic> existing = readTopicsJson(topicsPath);
    if (!existing.isEmpty()) { emit topicsReady(m.id, existing); return; }

    ReadinessResult res = canRun(WorkflowStep::Summarize, meetingId);
    if (!res.runnable) { emit errorOccurred(res.detail); return; }

    MergedTranscript merged = d->mergedCache.value(meetingId);
    if (merged.tokens.isEmpty())
        merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) { emit errorOccurred(tr("Nincs átirat — előbb futtass átírást.")); return; }
    speakeredit::applyResolvedSpeakers(merged, m);
    const QString transcriptMd = merged.renderMarkdown();

    const AppSettings s = d->settings->settings();
    CloudRunPtr run;
    ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("topics"), QStringLiteral("complex"));
    const auto sink = std::make_shared<FailureSink>();
    captureFailures(cfg, sink);
    ILlmProvider* provider = LlmProviderRegistry::instance().create(s.llmProviderId, cfg, this);
    if (!provider) { emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(s.llmProviderId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new ComplexSummaryService(provider, this);
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

    connect(svc, &ComplexSummaryService::topicsReady, this,
            [this, id = m.id, topicsPath, providerObj, svc, run, key, statsKey, units, clock]
            (const QVector<SummaryTopic>& topics) {
        d->llmRuns.remove(key);
        if (QFileInfo(topicsPath).absoluteDir().exists())   // (a meeting közben törlődhetett)
            writeTopicsJson(topicsPath, topics);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        // Cloud: a témagyűjtés a komplex futás ELSŐ része — a futás (és az X-Tanara-Job-Id)
        // nyitva marad, a téma-elemzések és az összegzés ugyanezt használják; a K-07 a
        // futás végén a teljes összeget mutatja (grill-döntés 29: egy futás = egy azonosító).
        // Itt csak az egyenleg-chip frissül.
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
    connect(svc, &ComplexSummaryService::failed, this,
            [this, id = m.id, providerObj, svc, run, key, sink](const QString& e) {
        d->llmRuns.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        d->jobs->fail(id, JobKind::ExtractTopics,
                      d->describeLlmFailure(JobKind::ExtractTopics, e, run, sink));
        failCloudRun(run, tr("Téma-kinyerés hiba: %1").arg(e));
    });

    svc->requestTopics(transcriptMd, m.contextNote.trimmed(),
                       resolvedPrompt(s, s.topicExtractionPrompt, "topic"),
                       cfg.model, cfg.temperature, cfg.maxTokens);
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

    // A (szerkesztett) téma-lista perzisztálása — folytatható marad.
    writeTopicsJson(QDir(m.folder).filePath(QStringLiteral("summary.topics.json")), topics);
    emit topicsChanged(meetingId);

    // Csak a MÉG ELEMZETLEN témák mennek a sorba (a kész elemzés a lemezen van) —
    // megszakítás/hiba után az újraindítás így onnan folytat, ahol tartott. Egy már kész
    // téma újrafuttatása a kártya saját gombjával (analyzeTopic) kérhető.
    const QVector<TopicAnalysis> existing =
        readAnalysesJson(QDir(m.folder).filePath(QStringLiteral("summary.analyses.json")));
    QSet<QString> doneIds;
    for (const TopicAnalysis& a : existing) doneIds.insert(a.topicId);

    QVector<SummaryTopic> missing;
    for (const SummaryTopic& t : topics)
        if (!doneIds.contains(t.id)) missing.append(t);

    // Cloud: a komplex összefoglaló (témagyűjtés + téma-elemzések + reduce) EGY futás, egy
    // X-Tanara-Job-Id-vel. Ha a témagyűjtés futása (vagy egy már futó köteg) még nyitva van,
    // azt folytatjuk; különben — pl. megszakítás / hiba utáni folytatásnál, ahol a futás
    // már lezárult — új futás indul, új azonosítóval.
    if (usesCloud(WorkflowStep::Summarize)) {
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

    // A (szerkesztett) téma átvezetése a topics.json-ba — a reduce sorrendje/tartalma innen jön.
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
        // Dedup: ha ugyanez a téma már fut vagy sorban áll, nem kerül be még egyszer.
        if (d->topicJobActive && d->activeTopicMeetingId == meetingId && d->activeTopicId == t.id)
            continue;
        const bool queued = std::any_of(d->topicJobQueue.cbegin(), d->topicJobQueue.cend(),
            [&](const Impl::TopicJob& j) { return j.meetingId == meetingId && j.topic.id == t.id; });
        if (queued)
            continue;
        d->topicJobQueue.append({ meetingId, t });
        ++added;
        d->jobs->clearTopicError(meetingId, t.id);   // új kísérlet → a régi hiba érvényét veszti
        emit topicAnalysisQueued(meetingId, t.id);
        emit topicStatusChanged(meetingId, t.id);
    }
    // Strukturált feladat: meetingenként EGY „Témák elemzése” (darab-haladással).
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
    // A sor végére értünk? Meetingenként lezárjuk a batch-et: queueFinished + (ha kérték
    // és nem volt bukás) auto-reduce. Több meeting jobjai elvben keveredhetnek a sorban,
    // ezért csak akkor zárunk le egy meetinget, ha már nincs rá váró job.
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
            // Strukturált lezárás: bukott téma → az összefoglaló-lépés hibája (a témánkénti
            // részletek a topicStatuses-ban); különben kész.
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
                finalizeComplexSummary(id);   // a cloud-futás a reduce-szal folytatódik
                continue;
            }
            if (wantReduce)
                emit jobProgress(id, tr(
                    "%1 téma elemzése nem sikerült — futtasd újra a kártyáján, majd kérd a végső összegzést.")
                    .arg(fail));
            // Reduce nélkül záruló cloud-futás (egy téma újrafuttatása, vagy nem-cloud hiba):
            // az eddigi terhelések összege a K-07-be.
            const CloudRunPtr run = d->complexRuns.take(id);
            if (run && !run->lastError.isError())
                finishCloudRun(run);
        }
        return;
    }

    const Impl::TopicJob job = d->topicJobQueue.takeFirst();
    // Korai (szolgáltató-hívás előtti) bukás: megmaradó téma-hiba + a sor megy tovább.
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
    speakeredit::applyResolvedSpeakers(merged, m);

    const AppSettings s = d->settings->settings();
    CloudRunPtr run = d->complexRuns.value(m.id);
    if (usesCloud(WorkflowStep::Summarize) && !run) {   // egy téma újrafuttatása köteg nélkül
        run = newCloudRun(m.id, QStringLiteral("complex"));
        d->complexRuns.insert(m.id, run);
    }
    if (run) run->kind = QStringLiteral("complex");   // a témagyűjtés után az elemzés-szakasz
    ProviderConfig cfg = llmConfigFor(run, m.id, QStringLiteral("complex"), QStringLiteral("complex"));
    const auto sink = std::make_shared<FailureSink>();
    captureFailures(cfg, sink);
    ILlmProvider* provider = LlmProviderRegistry::instance().create(s.llmProviderId, cfg, this);
    if (!provider) { failEarly(tr("Ismeretlen LLM-provider: %1.").arg(s.llmProviderId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new ComplexSummaryService(provider, this);

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

    connect(svc, &ComplexSummaryService::topicAnalysisReady, this,
            [this, meetingId = m.id, analysesPath, cleanup](const TopicAnalysis& a) {
        if (QFileInfo(analysesPath).absoluteDir().exists())
            upsertAnalysisJson(analysesPath, a);   // AZONNAL lemezre — a munka nem veszhet el
        d->jobCounts[meetingId].first++;
        cleanup();
        // A téma már nem „fut”: az állapot-lekérdezés (topicStatuses) innentől „kész”-t ad.
        d->activeTopicId.clear();
        d->activeTopicRun = {};
        d->jobs->clearTopicError(meetingId, a.topicId);
        d->updateTopicCounts(meetingId);
        emit topicAnalysisReady(meetingId, a);
        emit topicStatusChanged(meetingId, a.topicId);
        startNextTopicJob();
    });
    connect(svc, &ComplexSummaryService::failed, this,
            [this, meetingId = m.id, topicId = job.topic.id, cleanup, run, sink](const QString& e) {
        d->jobCounts[meetingId].second++;
        cleanup();
        d->activeTopicId.clear();
        d->activeTopicRun = {};
        d->jobs->setTopicError(meetingId, topicId,
                               d->describeLlmFailure(JobKind::AnalyzeTopics, e, run, sink));
        if (run && run->lastError.isError()) {
            // Cloud-hiba (402 / 403 / 426 / 429 / 503 …): a meeting hátralévő témái ugyanezt
            // kapnák → a sorból kivesszük őket; a K-12 részleges-hiba dialógus mutatja az eddig
            // terhelt összeget és a „Folytatás”-t (csak a hátralévő részekért fizet).
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
            emit topicAnalysisFailed(meetingId, topicId, e);   // a többi téma megy tovább
            emit topicStatusChanged(meetingId, topicId);
        }
        d->updateTopicCounts(meetingId);
        startNextTopicJob();
    });

    svc->requestTopicAnalysis(merged.renderMarkdown(), job.topic, m.contextNote.trimmed(),
                              resolvedPrompt(s, s.topicAnalysisPrompt, "analysis"),
                              cfg.model, cfg.temperature, cfg.maxTokens);
}

void AppController::finalizeComplexSummary(const QString& meetingId)
{
    if (d->jobs->isRunning(meetingId, JobKind::Summarize))
        return;   // már fut egy összegzés

    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    // A lemezen lévő elemzések, a topics.json (szerkesztett) sorrendjében; az árva
    // (törölt témához tartozó) elemzések kimaradnak.
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
    CloudRunPtr run = d->complexRuns.take(meetingId);   // a köteg futása (ha volt) folytatódik
    ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("complex"), QStringLiteral("complex"));
    const auto sink = std::make_shared<FailureSink>();
    captureFailures(cfg, sink);
    ILlmProvider* provider = LlmProviderRegistry::instance().create(s.llmProviderId, cfg, this);
    if (!provider) { emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(s.llmProviderId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new ComplexSummaryService(provider, this);
    svc->setReducePrompt(resolvedPrompt(s, QString(), "reduce"));
    emit jobProgress(meetingId, tr("Összegzés (vezetői összefoglaló + teendők)…"));

    const QString key = Impl::llmKey(meetingId, JobKind::Summarize);
    d->jobs->begin(meetingId, JobKind::Summarize, tr("Összegzés készítése"));
    d->llmRuns.insert(key, Impl::LlmRun{svc, providerObj, run});

    auto cleanup = [providerObj, svc]() {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
    };

    connect(svc, &ComplexSummaryService::failed, this,
            [this, meetingId, cleanup, run, key, sink](const QString& e) {
        d->llmRuns.remove(key);
        cleanup();
        d->jobs->fail(meetingId, JobKind::Summarize,
                      d->describeLlmFailure(JobKind::Summarize, e, run, sink));
        failCloudRun(run, tr("Komplex összefoglaló hiba: %1").arg(e));
    });
    connect(svc, &ComplexSummaryService::reduceReady, this,
            [this, m, ordered, cleanup, run, key, llmId = s.llmProviderId, model = cfg.model]
            (const QString& execSummary, const QVector<ActionItem>&) {
        d->llmRuns.remove(key);
        // Teendők KÓDBÓL (nem az LLM-től): a per-téma elemzések teendőit gyűjtjük össze,
        // normalizált szöveg-dedup. Determinisztikus, modellfüggetlen — az LLM reduce-ának
        // csak a vezetői összefoglaló marad (kevesebb hely a „hangos gondolkodásra").
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
        if (mm.id.isEmpty()) {   // a meeting közben törlődött
            cleanup();
            d->jobs->cancelled(m.id, JobKind::Summarize);
            finishCloudRun(run);
            return;
        }
        const QString mdPath = QDir(mm.folder).filePath(QStringLiteral("summary.md"));
        writeTextFile(mdPath, md);
        // Strukturált forma + metaadat (summary.json): vezetői összefoglaló, összevont
        // teendők, a témák döntéseinek uniója és maguk a téma-elemzések, sorrendben.
        SummaryDocument doc;
        doc.exists = true;
        doc.markdown = md;
        doc.summary.execSummary = execSummary;
        doc.summary.actionItems = mergedItems;
        for (const TopicAnalysis& a : ordered)
            for (const QString& dec : a.decisions)
                if (!doc.summary.decisions.contains(dec)) doc.summary.decisions << dec;
        doc.topics = ordered;
        doc.meta.createdAt = QDateTime::currentDateTime();
        doc.meta.providerId = llmId;
        doc.meta.model = model;
        doc.meta.mode = SummaryMode::Topics;
        summarystore::save(mm.folder, doc);
        QDir().mkpath(d->notesDir);
        const QString noteName = QStringLiteral("%1 %2.md")
            .arg(mm.startedAt.toString(QStringLiteral("yyyy-MM-dd")), slugify(mm.title));
        writeTextFile(QDir(d->notesDir).filePath(noteName), md);
        mm.hasSummary = true;
        d->store->saveMeeting(mm);
        cleanup();
        d->jobs->finish(mm.id, JobKind::Summarize);
        emit summaryReady(mm.id, mdPath);
        finishCloudRun(run);   // K-07: „Az összefoglaló $0,46 volt (12 rész).”
    });

    svc->requestReduce(ordered, m.contextNote.trimmed(), cfg.model, cfg.temperature, cfg.maxTokens);
}

// =========================================================================================
// Strukturált réteg az újratervezett főablakhoz: állapot-lekérdezés, megszakítás, témák,
// aszinkron azonosítás, hullámforma. (A régi jobProgress / errorOccurred mellett él.)
// =========================================================================================

MeetingJobTracker* AppController::jobs() const      { return d->jobs; }
MeetingLibrary*    AppController::library() const   { return d->library; }
TrackCatalog*      AppController::tracks() const    { return d->tracks; }
WaveformService*   AppController::waveforms() const { return d->waveforms; }
AudioImporter*     AppController::importer() const  { return d->importer; }

QString AppController::importAudio(const ImportRequest& request)
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

MeetingProcessingState AppController::processingState(const QString& meetingId) const
{
    return d->jobs->state(meetingId);
}

SummaryDocument AppController::summaryDocument(const QString& meetingId) const
{
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return {};
    return summarystore::load(m.folder);
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
            // Az UTOLSÓ kísérlet bukott (akkor is ez látszik, ha korábbról van kész elemzés —
            // az megmarad a lemezen; új sikeres futás vagy újrapróbálás törli a hibát).
            st.state = TopicState::Failed;
            st.error = errors.value(t.id).message;
            st.errorDetail = errors.value(t.id).detail;
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
        if (t.title.isEmpty()) continue;                      // cím nélküli téma nem menthető
        if (t.id.isEmpty() || ids.contains(t.id))             // új (vagy duplikált id-jű) téma
            t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ids.insert(t.id);
        saved.append(t);
    }
    const QString topicsPath = QDir(m.folder).filePath(QStringLiteral("summary.topics.json"));
    // A törölt témák: sorból ki / futó megszakítva, megmaradt hibájuk törölve.
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
    // Egy téma kivétele után a köteg már nem teljes → nincs automatikus záró összegzés.
    d->reduceWhenDone.remove(meetingId);
    d->updateTopicCounts(meetingId);
    emit topicAnalysisFailed(meetingId, topicId, tr("Megszakítva"));
    emit topicStatusChanged(meetingId, topicId);
    if (active)
        startNextTopicJob();   // a sor megy tovább (vagy lezárul)
    return true;
}

bool AppController::cancelJob(const QString& meetingId, JobKind kind)
{
    switch (kind) {
    case JobKind::Transcribe: {
        auto it = d->transcribeRuns.find(meetingId);
        if (it == d->transcribeRuns.end()) {
            // Az átirat már kész, az utolsó (azonosítás) szakasz fut → csak az marad ki.
            const auto ir = d->identifyRuns.constFind(meetingId);
            if (ir == d->identifyRuns.constEnd() || !ir->asStage) return false;
            ir->cancel->store(true);
            d->jobs->setCancelling(meetingId, kind);
            return true;
        }
        d->jobs->setCancelling(meetingId, kind);
        if (it->mixPhase) {
            // Lekeverés-fázis: a láncot bontjuk; a saját indítású ffmpeg-et leállítjuk (a
            // félkész fájl törlődik, a korábbi keverék és a sávok érintetlenek).
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
            it->job->cancel();   // szinkron failed("cancelled") → a failed-ág zár le (hiba nélkül)
            return true;
        }
        // (Elvileg nem fordul elő: sem keverés, sem job.)
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
        // Cloud: a megszakított hívásért nincs terhelés; a futás eddigi (pl. témagyűjtés +
        // elemzések) terhelései összesítve mennek a K-07-be.
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
        // A köteg lezárása: nincs auto-összegzés, nincs „N téma nem sikerült” üzenet; a már
        // kész elemzések a lemezen maradnak, a folytatás onnan megy tovább.
        const QPair<int, int> counts = d->jobCounts.take(meetingId);
        d->reduceWhenDone.remove(meetingId);
        d->topicBatchTotal.remove(meetingId);
        const CloudRunPtr run = d->complexRuns.take(meetingId);
        if (run && !run->lastError.isError())
            finishCloudRun(run);
        emit topicAnalysisQueueFinished(meetingId, counts.first, counts.second);
        d->jobs->cancelled(meetingId, kind);
        if (activeMine)
            startNextTopicJob();   // más meetingek témái mennek tovább
        return true;
    }
    case JobKind::Mixdown: {
        QProcess* proc = d->mixdownProcs.value(meetingId);
        if (!proc || !d->jobs->isRunning(meetingId, kind)) return false;
        d->jobs->setCancelling(meetingId, kind);
        d->mixdownCancelled.insert(meetingId);
        proc->kill();   // a finished-ág zár le: félkész fájl törölve, a régi keverék marad
        return true;
    }
    case JobKind::Import: {
        if (d->importer->currentId() != meetingId || !d->jobs->isRunning(meetingId, kind)) return false;
        d->jobs->setCancelling(meetingId, kind);
        d->importer->cancel();   // a cancelled-ág zár le: a félkész mappa törölve
        return true;
    }
    case JobKind::Identify: {
        const auto ir = d->identifyRuns.constFind(meetingId);
        if (ir == d->identifyRuns.constEnd() || ir->asStage) return false;
        ir->cancel->store(true);   // a szál a következő beszélő előtt megáll
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
        return false;   // az átírás a végén maga azonosít
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

    // A még névtelen (nem leképezett) nyers beszélő-címkék és a hangforrásuk.
    struct Item { QString label; QString audioPath; };
    QVector<Item> items;
    QStringList seen;
    for (const TranscriptToken& t : merged.tokens) {
        if (t.speaker.isEmpty() || seen.contains(t.speaker)) continue;
        seen << t.speaker;
        if (m.speakerMap.contains(t.speaker)) continue;
        const VoiceSource src = resolveVoiceSource(m, merged, t.speaker);
        if (!src.absPath.isEmpty()) items.append({t.speaker, src.absPath});
    }
    if (items.isEmpty()) {
        d->jobs->markIdentified(meetingId);   // nincs névtelen beszélő → nincs mit azonosítani
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

    // A dekódolás (ffmpeg) + embedding (ONNX) háttérszálon fut, SAJÁT embedder-példánnyal
    // (a fő szál embedderéhez nem nyúl). A párosítás a lenyomat-DB ellen és a mentés a fő
    // szálon történik (applyIdentification) — a DB-t csak a fő szál éri el.
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    auto results = std::make_shared<QVector<QPair<QString, QVector<float>>>>();
    const QString modelPath = d->voiceModelPath;
    QPointer<AppController> self(this);
    QThread* th = QThread::create([self, items, merged, modelPath, cancel, results, meetingId,
                                   total, kind, asStage]() {
        VoiceEmbedder emb(modelPath);
        if (!emb.isValid()) return;
        int done = 0;
        for (const Item& it : items) {
            if (cancel->load()) break;
            const QVector<float> e = embeddingForLabel(emb, it.audioPath, merged, it.label);
            ++done;
            // Eredmény + valós darab-haladás vissza a fő szálra.
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
        // Ha a futás alatt az átirat megváltozott (új átírás érkezett), az embeddingek a RÉGI
        // átirat címkéihez tartoznak: nem írjuk a neveket az új átiratra — megszakítottnak
        // számít. (Különben: megszakításnál is mentjük, amit addig találtunk.)
        const bool stale = d->transcriptStamp(meetingId, folder) != stampAtStart;
        const bool wasCancelled = cancel->load() || stale;
        if (!stale)
            d->applyIdentification(meetingId, *results);
        if (!wasCancelled)
            d->jobs->markIdentified(meetingId);
        if (asStage) {
            d->jobs->setStage(meetingId, kind, QStringLiteral("identify"),
                              wasCancelled ? StageState::Skipped : StageState::Done);
            d->jobs->finish(meetingId, kind);   // az átirat kész — a feladat sikerrel zárul
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

// ---- átirat-szerkesztő (beszélő-javítás) ----------------------------------

SpeakerEditor* AppController::speakerEditor(const QString& meetingId)
{
    if (SpeakerEditor* existing = d->speakerEditors.value(meetingId)) return existing;
    const Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return nullptr;

    auto* ed = new SpeakerEditor(d->store, d->people.get(), d->voiceprints.get(), meetingId, this);
    ed->setUserSpeakerName(d->settings->settings().userSpeakerName);
    // Voice-ID nélkül (nincs modell / TANARA_BUILD_VOICEID=OFF) a szerkesztő bizonytalanság
    // és javaslat nélkül is teljes értékű.
    if (QFileInfo::exists(d->voiceModelPath))
        ed->setEmbedderFactory(voiceUtteranceEmbedderFactory(d->voiceModelPath));
    d->speakerEditors.insert(meetingId, ed);

    // A szerkesztő mellékhatásai a megszokott jeleken mennek ki (régi UI, könyvtár).
    connect(ed, &SpeakerEditor::speakerMapChanged, this, [this](const QString& id) {
        d->editorEmitting = true;
        emit speakerMapChanged(id);
        d->editorEmitting = false;
    });
    connect(ed, &SpeakerEditor::peopleChanged, this, &AppController::peopleChanged);
    connect(ed, &SpeakerEditor::voiceprintsChanged, this, &AppController::voiceprintsChanged);
    connect(ed, &SpeakerEditor::summaryStaleChanged, this,
            [this, meetingId] { emit summaryStaleChanged(meetingId); });
    // Kívülről jövő változás (régi UI átnevezés, auto-azonosítás, személy-átnevezés) →
    // a szerkesztő újraolvassa a lemezt.
    connect(this, &AppController::speakerMapChanged, ed, [this, ed](const QString& id) {
        if (!d->editorEmitting && id == ed->meetingId()) ed->refreshFromDisk();
    });
    connect(this, &AppController::voiceprintsChanged, ed, &SpeakerEditor::speakersChanged);
    return ed;
}

void AppController::closeSpeakerEditor(const QString& meetingId)
{
    if (SpeakerEditor* ed = d->speakerEditors.take(meetingId)) ed->deleteLater();
}

QVector<PersonInfo> AppController::peopleDirectory() const
{
    if (d->details) d->details->refresh();
    return listPeople(d->people.get(), d->voiceprints.get(), d->store, d->details.get());
}

SummaryStaleInfo AppController::summaryStale(const QString& meetingId) const
{
    if (SpeakerEditor* ed = d->speakerEditors.value(meetingId)) return ed->summaryStale();
    return speakeredit::summaryStale(d->store->load(meetingId));
}

// Ugyanez már betöltött meetingre (a könyvtár soronként hívja — nincs újabb lemez-olvasás
// a meeting.json-ért).
SummaryStaleInfo AppController::summaryStale(const Meeting& meeting) const
{
    if (SpeakerEditor* ed = d->speakerEditors.value(meeting.id)) return ed->summaryStale();
    return speakeredit::summaryStale(meeting);
}

RetranscribeImpact AppController::retranscribeImpact(const QString& meetingId) const
{
    if (SpeakerEditor* ed = d->speakerEditors.value(meetingId)) return ed->retranscribeImpact();
    return speakeredit::retranscribeImpact(d->store->load(meetingId));
}

void AppController::dismissSummaryStale(const QString& meetingId)
{
    if (SpeakerEditor* ed = d->speakerEditors.value(meetingId)) {
        ed->dismissSummaryStale();   // a summaryStaleChanged a szerkesztő jeléből megy ki
        return;
    }
    const Meeting m = d->store->load(meetingId);
    if (!m.folder.isEmpty() && speakeredit::clearSummaryStale(m.folder))
        emit summaryStaleChanged(meetingId);
}

} // namespace tanara

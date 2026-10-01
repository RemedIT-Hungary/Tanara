#include "tanara/AppController.h"

#include "tanara/Logging.h"
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
#include <algorithm>
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

void applySpeakerMap(MergedTranscript& mt, const QMap<QString, QString>& map) {
    if (map.isEmpty()) return;
    for (TranscriptToken& t : mt.tokens) {
        const auto it = map.constFind(t.speaker);
        if (it != map.constEnd()) t.speaker = it.value();
    }
}

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

} // namespace

struct AppController::Impl {
    SettingsManager* settings = nullptr;
    DeviceManager*   devices  = nullptr;
    MeetingStore*    store    = nullptr;
    RecordingSession* session = nullptr;
    DeviceMonitor*   monitor = nullptr;
    KeyStore         keyStore;
    std::unique_ptr<PeopleStore> people;
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

    // Tanara Cloud.
    CloudAccount* cloud = nullptr;
    bool cloudLive = false;
    bool cloudTeaser = false;
    // Meetingenként a nyitott komplex összefoglaló cloud-futása (témagyűjtés → téma-elemzések
    // → reduce): EGY X-Tanara-Job-Id, a hívásonkénti terhelések összesítve (K-07 „12 rész”).
    // Lezáráskor (kész / hiba / megszakítás) kikerül innen; a folytatás így új futás.
    QHash<QString, CloudRunPtr> complexRuns;
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

AppController::AppController(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
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
    d->metaDir  = expandTilde(s.metadataDir);
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

    connect(d->devices, &DeviceManager::devicesChanged, this, &AppController::devicesChanged);

    d->statePath = QDir(d->metaDir).filePath(QStringLiteral("state.json"));
    d->lastDevices = loadLastDevices(d->statePath);
    d->monitor = new DeviceMonitor(this);
    connect(d->monitor, &DeviceMonitor::level, this, &AppController::deviceLevel);

    d->people = std::make_unique<PeopleStore>(QDir(d->metaDir).filePath(QStringLiteral("people.json")));

    // Voice-ID: lenyomat-DB + a modell várt helye (~/.tanara/models/...).
    d->voiceprints = std::make_unique<VoiceprintStore>(
        QDir(d->metaDir).filePath(QStringLiteral("voiceprints.json")));
    d->voiceModelPath = QDir(d->metaDir).filePath(
        QStringLiteral("models/campplus_sv_zh_en_16k.onnx"));
}

AppController::~AppController() = default;

SettingsManager* AppController::settings() const { return d->settings; }
DeviceManager*   AppController::devices()  const { return d->devices; }
MeetingStore*    AppController::store()     const { return d->store; }
VoiceprintStore* AppController::voiceprints() const { return d->voiceprints.get(); }
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
        applySpeakerMap(merged, m.speakerMap);
        writeTextFile(QDir(m.folder).filePath(QStringLiteral("transcript.md")), merged.renderMarkdown());
    }
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
        const VoiceMatch match = d->voiceprints->bestMatch(e);
        if (match.score >= kVoiceMatchThreshold && !match.name.isEmpty()) {
            m.speakerMap.insert(label, match.name);
            if (d->people) d->people->add(match.name);
            mapChanged = true;
        }
    }

    if (mapChanged) {
        d->store->saveMeeting(m);
        MergedTranscript md = merged;
        applySpeakerMap(md, m.speakerMap);
        writeTextFile(QDir(m.folder).filePath(QStringLiteral("transcript.md")), md.renderMarkdown());
        emit speakerMapChanged(m.id);
    }
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

        if (!changed) continue;
        d->store->saveMeeting(m);
        MergedTranscript merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
        if (!merged.tokens.isEmpty()) {
            applySpeakerMap(merged, m.speakerMap);
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
    if (d->voiceprints) { d->voiceprints->removePerson(nm); emit voiceprintsChanged(); }

    const QVector<Meeting> all = d->store->loadAll();
    for (const Meeting& idx : all) {
        Meeting m = d->store->load(idx.id);
        bool changed = false;
        const QList<QString> keys = m.speakerMap.keys();
        for (const QString& key : keys)
            if (m.speakerMap.value(key) == nm) { m.speakerMap.remove(key); changed = true; }
        if (!changed) continue;
        d->store->saveMeeting(m);
        MergedTranscript merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
        if (!merged.tokens.isEmpty()) {
            applySpeakerMap(merged, m.speakerMap);
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
            // A hangfájl FIZIKAI törlése (explicit user-művelet).
            QFile::remove(QDir(m.folder).filePath(t.file));
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
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;

    // Csak az aktív, lemezen meglévő sávok kerülnek a keverékbe.
    QStringList inArgs;
    int inputs = 0;
    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        const QString path = QDir(m.folder).filePath(t.file);
        if (!QFile::exists(path)) continue;
        inArgs << QStringLiteral("-i") << path;
        ++inputs;
    }
    if (inputs == 0) {
        emit errorOccurred(tr("Nincs aktív hangsáv a lekeveréshez."));
        emit mixdownUpdated(meetingId, false);
        return;
    }

    const QString outRel = QStringLiteral("mixdown.mp3");
    const QString outPath = QDir(m.folder).filePath(outRel);

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
         << QStringLiteral("-y") << outPath;
    const qint64 totalMs = m.durationMs;   // a százalék nevezője

    // Aszinkron QProcess — NEM blokkolja a UI-t (egy 90 perces keverés is futhat), és nem
    // blokkolja új felvétel indítását sem (külön child-process + külön capture-engine).
    auto* proc = new QProcess(this);
    proc->setProgram(QStringLiteral("ffmpeg"));
    proc->setArguments(args);
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
        if (lastPct >= 0)
            emit mixdownProgress(meetingId, lastPct);
    });
    connect(proc, &QProcess::finished, this,
            [this, proc, meetingId, outRel](int code, QProcess::ExitStatus status) {
        const bool ok = (status == QProcess::NormalExit && code == 0);
        if (ok) {
            // Friss meeting (közben módosulhatott) → mixdownFile + dirty törlése.
            Meeting mm = d->store->load(meetingId);
            if (!mm.id.isEmpty()) {
                mm.mixdownFile  = outRel;
                mm.mixdownDirty = false;
                d->store->saveMeeting(mm);
            }
            emit mixdownProgress(meetingId, 100);
        } else {
            emit errorOccurred(tr("A lekeverés (ffmpeg) sikertelen."));
        }
        emit mixdownUpdated(meetingId, ok);
        emit tracksChanged(meetingId);   // a nézet frissüljön (gomb-állapot, mixdownFile)
        proc->deleteLater();
    });

    // A haladást a mixdownProgress (0..100) jelzi a nem-modális UI-nak — NEM a jobProgress
    // (az a globális busy-jelzőt kapcsolná be, ami egy háttér-lekeverés alatt feleslegesen
    // letiltaná az akció-gombokat). Indító 0%:
    emit mixdownProgress(meetingId, 0);
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
    if (d->state == RecordingState::Recording || d->state == RecordingState::Stopping) return;
    d->devices->refresh();
    d->monitor->start(d->devices->captureDevices());
}

void AppController::stopLevelMonitoring() {
    if (d->monitor) d->monitor->stop();
}

void AppController::setSecret(const QString& name, const QString& value) { d->keyStore.set(name, value); }
bool AppController::hasSecret(const QString& name) const { return !d->keyStore.get(name).isEmpty(); }

void AppController::startRecording(const QString& title, const QVector<AudioDeviceInfo>& devices)
{
    if (d->state == RecordingState::Recording || d->state == RecordingState::Stopping) {
        emit errorOccurred(tr("Már folyik felvétel."));
        return;
    }
    stopLevelMonitoring();   // a monitor felszabadítja az eszközöket a felvétel előtt
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
            startCallEndMonitor();
        } else if (st == RecordingState::Idle) {
            stopCallEndMonitor();
        }
        emit recordingStateChanged(st);
    });
    connect(sess, &RecordingSession::levelMeterUpdated, this, &AppController::levelMeterUpdated);
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
        emit recordingStateChanged(d->state);
    });
    connect(sess, &RecordingSession::finished, this, [this](Meeting m) {
        stopCallEndMonitor();
        d->currentFolder = m.folder;
        d->store->saveMeeting(m);
        if (d->session) { d->session->deleteLater(); d->session = nullptr; }
        d->state = RecordingState::Idle;
        emit recordingFinished(m);
        emit recordingStateChanged(d->state);
        // Lekeverés (mixdown) leválasztva a stop()-ról: itt indítjuk ASZINKRON, csak ha a
        // beállítás "auto". Nem blokkol → azonnal indítható új felvétel. Kézi módban a
        // felhasználó a review-panel „Lekeverés" gombjával indítja. (A mixdown csak
        // hallgatásra kell; az átíráshoz a per-sáv .ogg-k elegendők.)
        if (d->settings->settings().mixdownMode != QStringLiteral("manual")
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
            applySpeakerMap(merged, m.speakerMap);
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
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    ReadinessResult res = canRun(WorkflowStep::Transcribe, meetingId);
    if (!res.runnable) { emit errorOccurred(res.detail); return; }

    // A leirat a MIXDOWNból készül (egyetlen hangfolyam → nincs sávonkénti átfedés-
    // összefésülés/duplikáció, ~N× helyett 1× Soniox-költség). Ha a mixdown hiányzik vagy
    // elavult, előbb legyártjuk, és a mixdownUpdated jelre indítjuk az átírást.
    const QString mixPath =
        QDir(m.folder).filePath(m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3")
                                                        : m.mixdownFile);
    if (m.mixdownFile.isEmpty() || m.mixdownDirty || !QFile::exists(mixPath)) {
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(this, &AppController::mixdownUpdated, this,
            [this, meetingId, conn](const QString& id, bool ok) {
                if (id != meetingId) return;
                QObject::disconnect(*conn);
                if (ok) transcribeFromMixdown(meetingId);
                else emit errorOccurred(
                    tr("A lekeverés sikertelen — az átírás nem indult."));
            });
        emit jobProgress(m.id, tr("Lekeverés az átíráshoz…"));
        regenerateMixdown(meetingId);
        return;
    }
    transcribeFromMixdown(meetingId);
}

void AppController::retranscribeMeeting(const QString& meetingId)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    if (!m.speakerMap.isEmpty()) {
        m.speakerMap.clear();
        d->store->saveMeeting(m);
    }
    transcribeMeeting(meetingId);
}

void AppController::transcribeFromMixdown(const QString& meetingId)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) return;

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

    ISttProvider* provider = SttProviderRegistry::instance().create(sttId, cfg, this);
    if (!provider) {
        emit errorOccurred(tr("Ismeretlen STT-provider: %1.").arg(sttId));
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

    SttJob* job = provider->transcribe(req);
    connect(job, &SttJob::stateChanged, this,
            [this, id = m.id](JobState st) { emit jobProgress(id, sttPhase(st)); });
    // A részletes poll-üzenet (eltelt idő + életjel) is jusson ki a UI-ra, hogy a hosszú
    // async feldolgozás alatt látszódjon: fut és a kapcsolat él.
    connect(job, &SttJob::progress, this,
            [this, id = m.id](int, const QString& msg) { emit jobProgress(id, msg); });
    connect(job, &SttJob::finished, this, [this, m, providerObj, run](const TrackTranscript& tr) mutable {
        TrackTranscript res = tr;
        // A Soniox diarizációs id-ket (1,2,…) semleges „Beszélő N" címkére fordítjuk.
        for (TranscriptToken& tok : res.tokens)
            tok.speaker = tok.speaker.isEmpty()
                ? QStringLiteral("Beszélő")
                : QStringLiteral("Beszélő %1").arg(tok.speaker);

        QVector<TrackTranscript> single{res};
        MergedTranscript merged = mergeTranscripts(single);
        const QString mdPath = QDir(m.folder).filePath(QStringLiteral("transcript.md"));
        writeTextFile(mdPath, merged.renderMarkdown());
        writeTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")), merged);
        writeSegmentsJson(QDir(m.folder).filePath(QStringLiteral("transcript.segments.json")), merged.segments());
        d->mergedCache.insert(m.id, merged);
        m.hasTranscript = true;
        d->store->saveMeeting(m);
        if (providerObj) providerObj->deleteLater();
        emit transcriptReady(m.id, mdPath);
        finishCloudRun(run);   // K-07: „Ez az átírás $0,41 volt.” (csak cloud-futásnál)
        // Voice-ID: a diarizált beszélők auto-párosítása a lenyomat-DB ellen.
        autoIdentifyMeeting(m.id);
    });
    connect(job, &SttJob::failed, this, [this, providerObj, run](QString e) {
        if (providerObj) providerObj->deleteLater();
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
    applySpeakerMap(merged, m.speakerMap);   // a Gemma a valódi neveket lássa

    const AppSettings s = d->settings->settings();
    const QString llmId = s.llmProviderId;
    CloudRunPtr run;
    const ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("summary"), QStringLiteral("quick"));
    ILlmProvider* provider = LlmProviderRegistry::instance().create(llmId, cfg, this);
    if (!provider) {
        emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(llmId));
        return;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new SummaryService(provider, this);
    emit jobProgress(meetingId, run ? tr("Összefoglalás a Tanara Cloudban…")
                                    : tr("Összefoglalás a helyi modellel (Gemma)…"));

    connect(svc, &SummaryService::summaryReady, this, [this, m, providerObj, svc, run](const Summary& sum) mutable {
        const QString md = sum.renderMarkdown();
        const QString mdPath = QDir(m.folder).filePath(QStringLiteral("summary.md"));
        writeTextFile(mdPath, md);
        // másolat a notes (vault) mappába
        QDir().mkpath(d->notesDir);
        const QString noteName = QStringLiteral("%1 %2.md")
            .arg(m.startedAt.toString(QStringLiteral("yyyy-MM-dd")), slugify(m.title));
        writeTextFile(QDir(d->notesDir).filePath(noteName), md);
        m.hasSummary = true;
        d->store->saveMeeting(m);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        emit summaryReady(m.id, mdPath);
        finishCloudRun(run);
    });
    connect(svc, &SummaryService::summaryFailed, this, [this, providerObj, svc, run](const QString& e) {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        failCloudRun(run, tr("Összefoglaló hiba: %1").arg(e));
    });

    svc->summarize(merged, /*contextNotes*/ m.contextNote.trimmed(), /*glossary*/ QStringList(),
                   /*systemPrompt*/ resolvedPrompt(s, s.summaryPrompt, "simple"),
                   cfg.model, cfg.temperature, cfg.maxTokens);
}

// ---- komplex (több körös) összefoglaló ------------------------------------

void AppController::extractMeetingTopics(const QString& meetingId)
{
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
    applySpeakerMap(merged, m.speakerMap);
    const QString transcriptMd = merged.renderMarkdown();

    const AppSettings s = d->settings->settings();
    CloudRunPtr run;
    const ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("topics"), QStringLiteral("complex"));
    ILlmProvider* provider = LlmProviderRegistry::instance().create(s.llmProviderId, cfg, this);
    if (!provider) { emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(s.llmProviderId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new ComplexSummaryService(provider, this);
    emit jobProgress(meetingId, run ? tr("Témák kigyűjtése a Tanara Cloudban…")
                                    : tr("Témák kigyűjtése a helyi modellel…"));

    connect(svc, &ComplexSummaryService::topicsReady, this,
            [this, id = m.id, topicsPath, providerObj, svc, run](const QVector<SummaryTopic>& topics) {
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
        emit topicsReady(id, topics);
    });
    connect(svc, &ComplexSummaryService::failed, this, [this, providerObj, svc, run](const QString& e) {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
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

    enqueueTopicAnalyses(meetingId, { topic }, /*reduceWhenDone*/ false);
}

void AppController::enqueueTopicAnalyses(const QString& meetingId,
                                         const QVector<SummaryTopic>& topics, bool reduceWhenDone)
{
    if (reduceWhenDone) d->reduceWhenDone.insert(meetingId);
    if (!d->jobCounts.contains(meetingId)) d->jobCounts.insert(meetingId, {0, 0});

    for (const SummaryTopic& t : topics) {
        // Dedup: ha ugyanez a téma már fut vagy sorban áll, nem kerül be még egyszer.
        if (d->topicJobActive && d->activeTopicMeetingId == meetingId && d->activeTopicId == t.id)
            continue;
        const bool queued = std::any_of(d->topicJobQueue.cbegin(), d->topicJobQueue.cend(),
            [&](const Impl::TopicJob& j) { return j.meetingId == meetingId && j.topic.id == t.id; });
        if (queued)
            continue;
        d->topicJobQueue.append({ meetingId, t });
        emit topicAnalysisQueued(meetingId, t.id);
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
        const auto counts = d->jobCounts;
        d->jobCounts.clear();
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
            const QString& id = it.key();
            const int ok = it.value().first, fail = it.value().second;
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
    Meeting m = d->store->load(job.meetingId);
    if (m.id.isEmpty()) {
        d->jobCounts[job.meetingId].second++;
        emit topicAnalysisFailed(job.meetingId, job.topic.id, tr("Ismeretlen meeting."));
        startNextTopicJob();
        return;
    }

    MergedTranscript merged = d->mergedCache.value(m.id);
    if (merged.tokens.isEmpty())
        merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) {
        d->jobCounts[m.id].second++;
        emit topicAnalysisFailed(m.id, job.topic.id, tr("Nincs átirat — előbb futtass átírást."));
        startNextTopicJob();
        return;
    }
    applySpeakerMap(merged, m.speakerMap);

    const AppSettings s = d->settings->settings();
    CloudRunPtr run = d->complexRuns.value(m.id);
    if (usesCloud(WorkflowStep::Summarize) && !run) {   // egy téma újrafuttatása köteg nélkül
        run = newCloudRun(m.id, QStringLiteral("complex"));
        d->complexRuns.insert(m.id, run);
    }
    if (run) run->kind = QStringLiteral("complex");   // a témagyűjtés után az elemzés-szakasz
    const ProviderConfig cfg = llmConfigFor(run, m.id, QStringLiteral("complex"), QStringLiteral("complex"));
    ILlmProvider* provider = LlmProviderRegistry::instance().create(s.llmProviderId, cfg, this);
    if (!provider) {
        d->jobCounts[m.id].second++;
        emit topicAnalysisFailed(m.id, job.topic.id,
                                 tr("Ismeretlen LLM-provider: %1.").arg(s.llmProviderId));
        startNextTopicJob();
        return;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new ComplexSummaryService(provider, this);

    d->topicJobActive = true;
    d->activeTopicMeetingId = m.id;
    d->activeTopicId = job.topic.id;
    emit topicAnalysisStarted(m.id, job.topic.id);
    emit jobProgress(m.id, tr("„%1” téma elemzése…").arg(job.topic.title));

    const QString analysesPath = QDir(m.folder).filePath(QStringLiteral("summary.analyses.json"));
    auto cleanup = [providerObj, svc]() {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
    };

    connect(svc, &ComplexSummaryService::topicAnalysisReady, this,
            [this, meetingId = m.id, analysesPath, cleanup](const TopicAnalysis& a) {
        upsertAnalysisJson(analysesPath, a);   // AZONNAL lemezre — a munka nem veszhet el
        d->jobCounts[meetingId].first++;
        cleanup();
        emit topicAnalysisReady(meetingId, a);
        startNextTopicJob();
    });
    connect(svc, &ComplexSummaryService::failed, this,
            [this, meetingId = m.id, topicId = job.topic.id, cleanup, run](const QString& e) {
        d->jobCounts[meetingId].second++;
        cleanup();
        if (run && run->lastError.isError()) {
            // Cloud-hiba (402 / 403 / 426 / 429 / 503 …): a meeting hátralévő témái ugyanezt
            // kapnák → a sorból kivesszük őket; a K-12 részleges-hiba dialógus mutatja az eddig
            // terhelt összeget és a „Folytatás”-t (csak a hátralévő részekért fizet).
            emit topicAnalysisFailed(meetingId, topicId, run->lastError.message.isEmpty() ? e : run->lastError.message);
            for (int i = d->topicJobQueue.size() - 1; i >= 0; --i) {
                if (d->topicJobQueue.at(i).meetingId != meetingId) continue;
                const QString dropped = d->topicJobQueue.takeAt(i).topic.id;
                d->jobCounts[meetingId].second++;
                emit topicAnalysisFailed(meetingId, dropped, tr("Megszakítva"));
            }
            d->reduceWhenDone.remove(meetingId);
            d->complexRuns.remove(meetingId);
            failCloudRun(run, e);
        } else {
            emit topicAnalysisFailed(meetingId, topicId, e);   // a többi téma megy tovább
        }
        startNextTopicJob();
    });

    svc->requestTopicAnalysis(merged.renderMarkdown(), job.topic, m.contextNote.trimmed(),
                              resolvedPrompt(s, s.topicAnalysisPrompt, "analysis"),
                              cfg.model, cfg.temperature, cfg.maxTokens);
}

void AppController::finalizeComplexSummary(const QString& meetingId)
{
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
    const ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("complex"), QStringLiteral("complex"));
    ILlmProvider* provider = LlmProviderRegistry::instance().create(s.llmProviderId, cfg, this);
    if (!provider) { emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(s.llmProviderId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new ComplexSummaryService(provider, this);
    svc->setReducePrompt(resolvedPrompt(s, QString(), "reduce"));
    emit jobProgress(meetingId, tr("Összegzés (vezetői összefoglaló + teendők)…"));

    auto cleanup = [providerObj, svc]() {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
    };

    connect(svc, &ComplexSummaryService::failed, this, [this, cleanup, run](const QString& e) {
        cleanup();
        failCloudRun(run, tr("Komplex összefoglaló hiba: %1").arg(e));
    });
    connect(svc, &ComplexSummaryService::reduceReady, this,
            [this, m, ordered, cleanup, run](const QString& execSummary, const QVector<ActionItem>&) {
        // Teendők KÓDBÓL (nem az LLM-től): a per-téma elemzések teendőit gyűjtjük össze,
        // normalizált szöveg-dedup. Determinisztikus, modellfüggetlen — az LLM reduce-ának
        // csak a vezetői összefoglaló marad (kevesebb hely a „hangos gondolkodásra").
        QVector<ActionItem> mergedItems;
        QSet<QString> seen;
        for (const TopicAnalysis& a : ordered)
            for (const ActionItem& ai : a.actionItems) {
                const QString key = ai.text.simplified().toLower();
                if (key.isEmpty() || seen.contains(key))
                    continue;
                seen.insert(key);
                mergedItems.append(ai);
            }
        const QString md = renderComplexMarkdown(execSummary, mergedItems, ordered);
        Meeting mm = d->store->load(m.id);
        if (mm.id.isEmpty()) mm = m;
        const QString mdPath = QDir(mm.folder).filePath(QStringLiteral("summary.md"));
        writeTextFile(mdPath, md);
        QDir().mkpath(d->notesDir);
        const QString noteName = QStringLiteral("%1 %2.md")
            .arg(mm.startedAt.toString(QStringLiteral("yyyy-MM-dd")), slugify(mm.title));
        writeTextFile(QDir(d->notesDir).filePath(noteName), md);
        mm.hasSummary = true;
        d->store->saveMeeting(mm);
        cleanup();
        emit summaryReady(mm.id, mdPath);
        finishCloudRun(run);   // K-07: „Az összefoglaló $0,46 volt (12 rész).”
    });

    svc->requestReduce(ordered, m.contextNote.trimmed(), cfg.model, cfg.temperature, cfg.maxTokens);
}

} // namespace tanara

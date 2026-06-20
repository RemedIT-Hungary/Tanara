#include "tanara/AppController.h"

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
#include "tanara/TranscriptMerger.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFileInfo>
#include <QDateTime>
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
QVector<float> embeddingForLabel(VoiceEmbedder& emb, const QString& folder,
                                 const MergedTranscript& mt, const QString& rawLabel,
                                 const Track& track) {
    QVector<Utterance> utts;
    for (const Utterance& u : mt.segments())
        if (u.speaker == rawLabel) utts.append(u);
    std::sort(utts.begin(), utts.end(), [](const Utterance& a, const Utterance& b) {
        return (a.endMs - a.startMs) > (b.endMs - b.startMs);
    });
    const QString path = QDir(folder).filePath(track.file);
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
                                const Track& track) {
    qint64 bestS = -1, bestE = -1, bestDur = -1;
    for (const Utterance& u : mt.segments())
        if (u.speaker == rawLabel && (u.endMs - u.startMs) > bestDur) {
            bestDur = u.endMs - u.startMs; bestS = u.startMs; bestE = u.endMs;
        }
    if (bestS < 0)
        return track.file;
    return QStringLiteral("%1#%2-%3").arg(track.file).arg(bestS).arg(bestE);
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
    case JobState::Uploading:  return QStringLiteral("Hang feltöltése…");
    case JobState::Queued:     return QStringLiteral("Várakozás a Soniox sorban…");
    case JobState::Processing: return QStringLiteral("Átírás folyamatban…");
    case JobState::Completed:  return QStringLiteral("Sáv kész");
    case JobState::Failed:     return QStringLiteral("Hiba");
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
};

AppController::AppController(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    registerBuiltinProviders();

    d->settings = new SettingsManager(QString(), this);
    const AppSettings s = d->settings->settings();
    d->audioDir = expandTilde(s.audioDir);
    d->metaDir  = expandTilde(s.metadataDir);
    d->notesDir = expandTilde(s.notesDir);
    QDir().mkpath(d->audioDir);
    QDir().mkpath(d->metaDir);

    d->keyStore = KeyStore(QDir(d->metaDir).filePath(QStringLiteral("secrets.json")));
    d->devices = new DeviceManager(this);
    d->store   = new MeetingStore(d->audioDir, d->metaDir, this);

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
    if (m.id.isEmpty()) { emit errorOccurred(QStringLiteral("Ismeretlen meeting: %1").arg(meetingId)); return; }

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
    const Track* track = resolveTrackForLabel(m, merged, rawLabel);
    if (!track) return;

    const QVector<float> embedding = embeddingForLabel(*emb, m.folder, merged, rawLabel, *track);
    if (embedding.isEmpty()) return;

    Voiceprint vp;
    vp.embedding = embedding;
    vp.sourceMeetingId = m.id;
    vp.sourceTrack = track->id;
    vp.device = track->deviceName;
    vp.sampleRef = representativeSampleRef(merged, rawLabel, *track);
    vp.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    d->voiceprints->addPrint(nm, vp);
    emit voiceprintsChanged();
}

void AppController::autoIdentifyMeeting(const QString& meetingId) {
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
    for (const QString& label : labels) {
        if (m.speakerMap.contains(label))
            continue;   // már nevesített (kézzel vagy korábbi match)
        const Track* track = resolveTrackForLabel(m, merged, label);
        if (!track) continue;
        const QVector<float> e = embeddingForLabel(*emb, m.folder, merged, label, *track);
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
    const Track* track = resolveTrackForLabel(m, merged, rawLabel);
    if (!track) return none;
    const QVector<float> e = embeddingForLabel(*d->embedder, m.folder, merged, rawLabel, *track);
    if (e.isEmpty()) return none;
    return d->voiceprints->bestMatch(e);
}

QVector<ParticipantGuess> AppController::identifyParticipants(const QString& meetingId) {
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

    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        const qint64 D = m.durationMs;
        if (D < kWinMs) continue;
        const QString path = QDir(m.folder).filePath(t.file);
        const qint64 step = std::max<qint64>(kWinMs, D / kTargetWindows);

        QVector<QVector<float>> embs;
        QVector<qint64> winStart;
        for (qint64 s = 0; s + kWinMs <= D; s += step) {
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
    if (m.id.isEmpty()) { emit errorOccurred(QStringLiteral("Ismeretlen meeting: %1").arg(meetingId)); return; }
    m.title = t;
    d->store->saveMeeting(m);   // meeting.json + index frissül → meetingUpdated jel
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
        emit errorOccurred(QStringLiteral("Nincs aktív hangsáv a lekeveréshez."));
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
         << QStringLiteral("-q:a") << QStringLiteral("4")
         << QStringLiteral("-y") << outPath;

    // Aszinkron QProcess — NEM blokkolja a UI-t (egy 90 perces keverés is futhat).
    auto* proc = new QProcess(this);
    proc->setProgram(QStringLiteral("ffmpeg"));
    proc->setArguments(args);
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
            emit jobProgress(meetingId, QStringLiteral("Lekeverés kész."));
        } else {
            emit errorOccurred(QStringLiteral("A lekeverés (ffmpeg) sikertelen."));
        }
        emit mixdownUpdated(meetingId, ok);
        emit tracksChanged(meetingId);   // a nézet frissüljön (gomb-állapot, mixdownFile)
        proc->deleteLater();
    });

    emit jobProgress(meetingId, QStringLiteral("Lekeverés folyamatban…"));
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
        emit errorOccurred(QStringLiteral("Már folyik felvétel."));
        return;
    }
    stopLevelMonitoring();   // a monitor felszabadítja az eszközöket a felvétel előtt
    QVector<AudioDeviceInfo> use = devices;
    if (use.isEmpty()) {
        d->devices->refresh();
        use = d->devices->autoRecordDevices();   // line-in/AUX kihagyva az auto-halmazból
    }
    if (use.isEmpty()) {
        emit errorOccurred(QStringLiteral("Nincs felvehető hangeszköz."));
        return;
    }

    // Megjegyezzük a használt eszközöket (induláskor előpipáláshoz).
    QStringList usedNames;
    for (const auto& dvc : use) usedNames << dvc.name;
    setLastUsedDeviceNames(usedNames);

    const AppSettings s = d->settings->settings();
    auto* sess = new RecordingSession(d->audioDir, title, s.userSpeakerName, this);
    d->session = sess;

    connect(sess, &RecordingSession::stateChanged, this, [this](RecordingState st) {
        d->state = st;
        emit recordingStateChanged(st);
    });
    connect(sess, &RecordingSession::levelMeterUpdated, this, &AppController::levelMeterUpdated);
    connect(sess, &RecordingSession::elapsedChanged, this, &AppController::elapsedChanged);
    connect(sess, &RecordingSession::failed, this, [this](QString e) {
        emit errorOccurred(e);
        if (d->session) { d->session->deleteLater(); d->session = nullptr; }
        d->state = RecordingState::Idle;
        emit recordingStateChanged(d->state);
    });
    connect(sess, &RecordingSession::finished, this, [this](Meeting m) {
        d->currentFolder = m.folder;
        d->store->saveMeeting(m);
        if (d->session) { d->session->deleteLater(); d->session = nullptr; }
        d->state = RecordingState::Idle;
        emit recordingFinished(m);
        emit recordingStateChanged(d->state);
    });

    sess->start(use);
}

void AppController::stopRecording()
{
    if (d->session) d->session->stop();
}

ReadinessResult AppController::canRun(WorkflowStep step, const QString& meetingId) const
{
    const Meeting meeting = d->store->load(meetingId);
    ReadinessModel model(d->settings->settings(),
                         [this](const QString& k) { return !d->keyStore.get(k).isEmpty(); });
    return model.check(step, meeting);
}

void AppController::transcribeMeeting(const QString& meetingId)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(QStringLiteral("Ismeretlen meeting: %1").arg(meetingId)); return; }

    ReadinessResult res = canRun(WorkflowStep::Transcribe, meetingId);
    if (!res.runnable) { emit errorOccurred(res.detail); return; }

    const AppSettings s = d->settings->settings();
    const QString sttId = s.sttProviderId;
    ProviderConfig cfg = s.sttSelected();
    cfg.apiKey = d->keyStore.get(keys::SonioxApiKey);

    ISttProvider* provider = SttProviderRegistry::instance().create(sttId, cfg, this);
    if (!provider) {
        emit errorOccurred(QStringLiteral("Ismeretlen STT-provider: %1.").arg(sttId));
        return;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    emit jobProgress(m.id, QStringLiteral("Átírás indítása…"));

    // Csak az AKTÍV sávokat írjuk át (az eldobott/csendes sávokra nem pazarlunk Soniox-időt).
    // (Hogy van-e legalább egy aktív sáv, azt a fenti canRun() már garantálta.)
    QVector<int> activeIdx;
    for (int i = 0; i < m.tracks.size(); ++i)
        if (m.tracks[i].active) activeIdx << i;

    struct Ctx { int remaining; QVector<TrackTranscript> results; bool failed = false; };
    auto ctx = std::make_shared<Ctx>();
    ctx->remaining = activeIdx.size();
    ctx->results.resize(m.tracks.size());

    for (int i : activeIdx) {
        const Track& t = m.tracks[i];
        SttRequest req;
        req.audioFilePath = QDir(m.folder).filePath(t.file);
        req.trackId = t.id;
        req.speakerLabel = t.speakerLabel;
        req.languageHints = s.languageHints;
        // Diarizáció MINDEN sávra: a mikrofonba is beszélhet egyszerre több ember,
        // ezért a mic-sávot is fel kell bontani beszélőkre (nem fix egy beszélő).
        req.diarization = true;

        SttJob* job = provider->transcribe(req);
        connect(job, &SttJob::stateChanged, this,
                [this, id = m.id, label = t.speakerLabel](JobState st) {
                    emit jobProgress(id, sttPhase(st) + QStringLiteral(" — ") + label);
                });
        connect(job, &SttJob::finished, this, [this, ctx, i, m, providerObj](const TrackTranscript& tr) mutable {
            if (ctx->failed) return;
            TrackTranscript res = tr;
            // A Soniox diarizációs id-ket (1,2,…) emberi címkére fordítjuk.
            if (i < m.tracks.size()) {
                const Track& trk = m.tracks[i];
                if (trk.kind == TrackKind::Loopback) {
                    // Távoli oldal: minden id külön „Távoli N".
                    for (TranscriptToken& tok : res.tokens)
                        tok.speaker = tok.speaker.isEmpty()
                            ? trk.speakerLabel
                            : QStringLiteral("Távoli %1").arg(tok.speaker);
                } else {
                    // Mic: a diarizált beszélők semleges „Mikrofon N" címkét kapnak —
                    // a NEVET a voice-ID adja (nem hangerő/pozíció). Egy beszélő esetén
                    // is így megy; a fingerprint dönti el, ki az (te is).
                    for (TranscriptToken& tok : res.tokens)
                        tok.speaker = tok.speaker.isEmpty()
                            ? trk.speakerLabel
                            : QStringLiteral("Mikrofon %1").arg(tok.speaker);
                }
            }
            ctx->results[i] = res;
            if (--ctx->remaining != 0) return;

            MergedTranscript merged = mergeTranscripts(ctx->results);
            const QString mdPath = QDir(m.folder).filePath(QStringLiteral("transcript.md"));
            writeTextFile(mdPath, merged.renderMarkdown());
            writeTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")), merged);
            writeSegmentsJson(QDir(m.folder).filePath(QStringLiteral("transcript.segments.json")), merged.segments());
            d->mergedCache.insert(m.id, merged);
            m.hasTranscript = true;
            d->store->saveMeeting(m);
            if (providerObj) providerObj->deleteLater();
            emit transcriptReady(m.id, mdPath);
            // Voice-ID: ismert mic-beszélő rögzítése + távoli beszélők auto-párosítása.
            autoIdentifyMeeting(m.id);
        });
        connect(job, &SttJob::failed, this, [this, ctx, providerObj](QString e) {
            if (ctx->failed) return;
            ctx->failed = true;
            if (providerObj) providerObj->deleteLater();
            emit errorOccurred(QStringLiteral("Soniox hiba: %1").arg(e));
        });
    }
}

void AppController::summarizeMeeting(const QString& meetingId)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(QStringLiteral("Ismeretlen meeting: %1").arg(meetingId)); return; }

    ReadinessResult res = canRun(WorkflowStep::Summarize, meetingId);
    if (!res.runnable) { emit errorOccurred(res.detail); return; }

    MergedTranscript merged = d->mergedCache.value(meetingId);
    if (merged.tokens.isEmpty())
        merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    // Tartalmi védőág: a canRun(Summarize) a meeting.hasTranscript flagre kapuz, de ha a
    // tokens.json hiányzik/üres (kézzel törölt, részleges írás, rosszul bemásolt meeting),
    // ne induljon összefoglaló üres átiratra — tükrözi az eredeti tartalom-alapú guardot.
    if (merged.tokens.isEmpty()) {
        emit errorOccurred(QStringLiteral("Nincs átirat — előbb futtass átírást."));
        return;
    }
    applySpeakerMap(merged, m.speakerMap);   // a Gemma a valódi neveket lássa

    const AppSettings s = d->settings->settings();
    const QString llmId = s.llmProviderId;
    ProviderConfig cfg = s.llmSelected();
    cfg.apiKey = d->keyStore.get(keys::LlmApiKey);   // LM Studio: lehet üres
    ILlmProvider* provider = LlmProviderRegistry::instance().create(llmId, cfg, this);
    if (!provider) {
        emit errorOccurred(QStringLiteral("Ismeretlen LLM-provider: %1.").arg(llmId));
        return;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new SummaryService(provider, this);
    emit jobProgress(meetingId, QStringLiteral("Összefoglalás a helyi modellel (Gemma)…"));

    connect(svc, &SummaryService::summaryReady, this, [this, m, providerObj, svc](const Summary& sum) mutable {
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
    });
    connect(svc, &SummaryService::summaryFailed, this, [this, providerObj, svc](const QString& e) {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        emit errorOccurred(QStringLiteral("Összefoglaló hiba: %1").arg(e));
    });

    svc->summarize(merged, /*contextNotes*/ QString(), /*glossary*/ QStringList(),
                   cfg.model, cfg.temperature, cfg.maxTokens);
}

} // namespace tanara

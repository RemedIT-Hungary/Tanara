#include "tanara/AppController.h"

#include "tanara/SettingsManager.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/audio/DeviceMonitor.h"
#include "tanara/audio/RecordingSession.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/KeyStore.h"
#include "tanara/stt/SonioxProvider.h"
#include "tanara/stt/ISttProvider.h"
#include "tanara/llm/OpenAiCompatibleProvider.h"
#include "tanara/SummaryService.h"
#include "tanara/TranscriptMerger.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
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
}

AppController::~AppController() = default;

SettingsManager* AppController::settings() const { return d->settings; }
DeviceManager*   AppController::devices()  const { return d->devices; }
MeetingStore*    AppController::store()     const { return d->store; }
RecordingState   AppController::recordingState() const { return d->state; }
QString AppController::currentMeetingFolder() const { return d->currentFolder; }

void AppController::refreshDevices() { d->devices->refresh(); }

QStringList AppController::lastUsedDeviceNames() const { return d->lastDevices; }

void AppController::setLastUsedDeviceNames(const QStringList& names) {
    d->lastDevices = names;
    saveLastDevices(d->statePath, names);
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
        use = d->devices->captureDevices();
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

void AppController::transcribeMeeting(const QString& meetingId)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(QStringLiteral("Ismeretlen meeting: %1").arg(meetingId)); return; }
    if (m.tracks.isEmpty()) { emit errorOccurred(QStringLiteral("A meetinghez nincs hangsáv.")); return; }

    const AppSettings s = d->settings->settings();
    ProviderConfig cfg = s.stt;
    cfg.apiKey = d->keyStore.get(keys::SonioxApiKey);
    if (cfg.apiKey.isEmpty()) {
        emit errorOccurred(QStringLiteral("Hiányzik a Soniox API-kulcs (Beállítások)."));
        return;
    }

    auto* provider = new SonioxProvider(cfg, this);
    emit jobProgress(m.id, QStringLiteral("Átírás indítása…"));

    struct Ctx { int remaining; QVector<TrackTranscript> results; bool failed = false; };
    auto ctx = std::make_shared<Ctx>();
    ctx->remaining = m.tracks.size();
    ctx->results.resize(m.tracks.size());

    for (int i = 0; i < m.tracks.size(); ++i) {
        const Track& t = m.tracks[i];
        SttRequest req;
        req.audioFilePath = QDir(m.folder).filePath(t.file);
        req.trackId = t.id;
        req.speakerLabel = t.speakerLabel;
        req.languageHints = s.languageHints;
        req.diarization = false;

        SttJob* job = provider->transcribe(req);
        connect(job, &SttJob::stateChanged, this,
                [this, id = m.id, label = t.speakerLabel](JobState st) {
                    emit jobProgress(id, sttPhase(st) + QStringLiteral(" — ") + label);
                });
        connect(job, &SttJob::finished, this, [this, ctx, i, m, provider](const TrackTranscript& tr) mutable {
            if (ctx->failed) return;
            ctx->results[i] = tr;
            if (--ctx->remaining != 0) return;

            MergedTranscript merged = mergeTranscripts(ctx->results);
            const QString mdPath = QDir(m.folder).filePath(QStringLiteral("transcript.md"));
            writeTextFile(mdPath, merged.renderMarkdown());
            writeTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")), merged);
            writeSegmentsJson(QDir(m.folder).filePath(QStringLiteral("transcript.segments.json")), merged.segments());
            d->mergedCache.insert(m.id, merged);
            m.hasTranscript = true;
            d->store->saveMeeting(m);
            provider->deleteLater();
            emit transcriptReady(m.id, mdPath);
        });
        connect(job, &SttJob::failed, this, [this, ctx, provider](QString e) {
            if (ctx->failed) return;
            ctx->failed = true;
            provider->deleteLater();
            emit errorOccurred(QStringLiteral("Soniox hiba: %1").arg(e));
        });
    }
}

void AppController::summarizeMeeting(const QString& meetingId)
{
    Meeting m = d->store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(QStringLiteral("Ismeretlen meeting: %1").arg(meetingId)); return; }

    MergedTranscript merged = d->mergedCache.value(meetingId);
    if (merged.tokens.isEmpty())
        merged = readTokensJson(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (merged.tokens.isEmpty()) {
        emit errorOccurred(QStringLiteral("Nincs átirat — előbb futtass átírást."));
        return;
    }

    const AppSettings s = d->settings->settings();
    ProviderConfig cfg = s.llm;
    cfg.apiKey = d->keyStore.get(keys::LlmApiKey);   // LM Studio: lehet üres
    auto* provider = new OpenAiCompatibleProvider(cfg, this);
    auto* svc = new SummaryService(provider, this);
    emit jobProgress(meetingId, QStringLiteral("Összefoglalás a helyi modellel (Gemma)…"));

    connect(svc, &SummaryService::summaryReady, this, [this, m, provider, svc](const Summary& sum) mutable {
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
        provider->deleteLater();
        svc->deleteLater();
        emit summaryReady(m.id, mdPath);
    });
    connect(svc, &SummaryService::summaryFailed, this, [this, provider, svc](const QString& e) {
        provider->deleteLater();
        svc->deleteLater();
        emit errorOccurred(QStringLiteral("Összefoglaló hiba: %1").arg(e));
    });

    svc->summarize(merged, /*contextNotes*/ QString(), /*glossary*/ QStringList(), s.llm.model);
}

} // namespace tanara

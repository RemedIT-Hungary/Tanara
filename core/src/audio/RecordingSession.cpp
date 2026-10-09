#include "tanara/audio/RecordingSession.h"

#include "tanara/audio/AudioEngine.h"
#include "tanara/audio/RingBuffer.h"
#include "tanara/audio/TrackTiming.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVector>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace tanara {

namespace {

// Slug: ékezetek/szóközök → kötőjeles, lowercase, ASCII-biztos fájlnévrész.
QString slugify(const QString& in) {
    QString s = in.normalized(QString::NormalizationForm_KD);
    static const QRegularExpression nonWord(QStringLiteral("[^A-Za-z0-9]+"));
    s.replace(nonWord, QStringLiteral("-"));
    static const QRegularExpression edges(QStringLiteral("^-+|-+$"));
    s.replace(edges, QString());
    s = s.toLower();
    if (s.isEmpty()) s = QStringLiteral("meeting");
    return s;
}

// Egy sáv lemez-oldali metaadata (a QProcess NEM itt él — azt a worker birtokolja
// a saját szálán, hogy semmilyen cross-thread QProcess hozzáférés ne legyen).
struct TrackMeta {
    int engineIndex = 0;
    QString fileName;          // pl. "track_mic.ogg" (relatív)
    QString slug;
    int channels = 1;
    TrackKind kind = TrackKind::Mic;
    QString deviceName;
    AudioDeviceInfo device;
    bool closed = false;       // menet közben lezárva (leválasztott / kikapcsolt eszköz)
    // A sáv megnyitásának ideje a felvétel 0-pontjától (ms) → Track::startOffsetMs. A fájl a
    // megnyitástól tartó hangot tartalmazza, csend-kitöltés NÉLKÜL.
    qint64 startOffsetMs = 0;
};

constexpr int kSampleRate = 48000;

} // namespace

// A drain worker külön QThread-en él. MINDEN ffmpeg QProcess-t Ő hoz létre, Ő ír
// rá és Ő zárja le — ugyanazon a szálon, így nincs "QSocketNotifier from another
// thread" probléma és nincs adatvesztés-kockázat. A sávokról SAJÁT másolatot tart
// (EncTrack), a fő szál listájához nem nyúl.
//
// Időzítés: a `clock` a felvétel 0-pontja (a capture indítása előtt indul). Egy sáv fájlja
// a megnyitásától kezdődik; a megnyitás ideje a TrackMeta::startOffsetMs (metaadat), a fájl
// elejére NEM kerül csend. Ahol a platform csendben nem ad mintát (Windows WASAPI loopback,
// lásd tracktiming::fillsCaptureGaps), a worker a hiányt a fali órához mérve csenddel pótolja.
class DrainWorker : public QObject {
    Q_OBJECT
public:
    DrainWorker(AudioEngine* engine, QString folder, int opusKbps, QElapsedTimer clock,
                std::optional<bool> fillGaps)
        : engine_(engine), folder_(std::move(folder)), opusKbps_(opusKbps), clock_(clock),
          fillGaps_(fillGaps) {}

    ~DrainWorker() override {
        for (EncTrack& t : tracks_) delete t.proc;
    }

    // A worker szálán futnak (BlockingQueuedConnection-nel hívva). Létrehozza+indítja az
    // encodereket; true ha minden elindult. Sikertelenségnél visszatakarít.
    bool startEncoders(const std::vector<TrackMeta>& metas) {
        for (const TrackMeta& m : metas) {
            if (!openEncoder(m)) {
                for (EncTrack& t : tracks_) { if (t.proc) { t.proc->kill(); delete t.proc; t.proc = nullptr; } }
                tracks_.clear();
                return false;
            }
        }
        timer_ = new QTimer(this);
        timer_->setInterval(20);   // 50 Hz drain
        connect(timer_, &QTimer::timeout, this, &DrainWorker::tick);
        meter_.start();
        timer_->start();
        return true;
    }

    // Felvétel közben hozzáadott sáv: az encoder üresen indul, a fájl a megnyitástól tart
    // (az eltolás metaadat: TrackMeta::startOffsetMs).
    bool addEncoder(const TrackMeta& meta) { return openEncoder(meta); }

    // Egy sáv lezárása menet közben: maradék kiírása, encoder lezárása (megvárva).
    void closeEncoder(int index) {
        if (index < 0 || index >= static_cast<int>(tracks_.size())) return;
        drainTrack(tracks_[static_cast<size_t>(index)]);
        finishTrack(tracks_[static_cast<size_t>(index)]);
    }

public slots:
    // Stopkor: utolsó ürítés + encoderek lezárása, megvárva az ffmpeg-ek befejezését —
    // mind a worker szálon. A végén finalized(): innentől minden sávfájl a lemezen van.
    void finalize() {
        if (finalized_) return;
        finalized_ = true;
        if (timer_) { timer_->stop(); }
        for (EncTrack& t : tracks_) drainTrack(t);
        QStringList failed;
        for (EncTrack& t : tracks_) {
            finishTrack(t);
            if (!t.ok) failed << t.fileName;
        }
        emit finalized(failed);
    }

signals:
    void meter(int trackIndex, float rms, float peak);
    void elapsed(qint64 ms);
    void finalized(QStringList failedFiles);

private slots:
    void tick() {
        for (EncTrack& t : tracks_) drainTrack(t);
        if (meter_.elapsed() >= 33) {   // ~30 Hz
            meter_.restart();
            for (size_t i = 0; i < tracks_.size(); ++i) {
                if (tracks_[i].closed) continue;
                emit meter(static_cast<int>(i), engine_->rms(tracks_[i].engineIndex),
                           engine_->takePeak(tracks_[i].engineIndex));
            }
            emit elapsed(clock_.elapsed());
        }
    }

private:
    struct EncTrack {
        int engineIndex = 0;
        QString fileName;
        int channels = 1;
        QProcess* proc = nullptr;
        qint64 openedAtMs = 0;     // a sáv megnyitása a felvétel órája szerint
        qint64 lastDataMs = 0;     // mikor jött utoljára adat az eszköztől (felvétel-óra)
        qint64 framesWritten = 0;  // a fájlba írt keretek (valódi hang + pótolt csend)
        bool fillGaps = false;     // a néma szakaszok csenddel pótlandók (Windows loopback)
        bool closed = false;
        bool ok = false;           // az encoder hibátlanul lezárta a fájlt
        std::vector<int16_t> scratch;
    };

    bool openEncoder(const TrackMeta& t) {
        auto* proc = new QProcess(this);          // a worker szálon él
        const QString outPath = QDir(folder_).absoluteFilePath(t.fileName);
        const QStringList args{
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("s16le"),
            QStringLiteral("-ar"), QString::number(kSampleRate),
            QStringLiteral("-ac"), QString::number(t.channels),
            QStringLiteral("-i"), QStringLiteral("pipe:0"),
            QStringLiteral("-c:a"), QStringLiteral("libopus"),
            QStringLiteral("-b:a"), QString::number(opusKbps_) + QStringLiteral("k"),
            QStringLiteral("-y"), outPath};
        proc->start(QStringLiteral("ffmpeg"), args);
        if (!proc->waitForStarted(5000)) {
            delete proc;
            return false;
        }
        EncTrack e;
        e.engineIndex = t.engineIndex;
        e.fileName = t.fileName;
        e.channels = t.channels;
        e.proc = proc;
        e.openedAtMs = t.startOffsetMs;
        e.lastDataMs = t.startOffsetMs;
        e.fillGaps = fillGaps_.value_or(tracktiming::fillsCaptureGaps(t.kind));
        tracks_.push_back(std::move(e));
        return true;
    }

    void writePcm(EncTrack& t, const char* data, qint64 bytes) {
        if (t.proc->state() == QProcess::Running) t.proc->write(data, bytes);
        t.framesWritten += bytes / (qint64(sizeof(int16_t)) * t.channels);
    }

    // A néma (adatot nem adó) eszköz hiányzó kereteinek pótlása csenddel — lásd
    // tracktiming::silenceToInsert. Csak fillGaps-es sávnál.
    void fillGap(EncTrack& t) {
        if (!t.fillGaps) return;
        const qint64 now = clock_.elapsed();
        qint64 frames = tracktiming::silenceToInsert(now - t.openedAtMs, now - t.lastDataMs,
                                                     t.framesWritten, kSampleRate);
        static const QByteArray zeros(1 << 16, '\0');
        const qint64 frameBytes = qint64(sizeof(int16_t)) * t.channels;
        while (frames > 0) {
            const qint64 n = qMin<qint64>(frames, zeros.size() / frameBytes);
            writePcm(t, zeros.constData(), n * frameBytes);
            frames -= n;
        }
    }

    void drainTrack(EncTrack& t) {
        if (t.closed || !t.proc) return;
        RingBuffer& ring = engine_->buffer(t.engineIndex);
        size_t avail = ring.available();
        bool gotData = false;
        while (avail > 0) {
            if (t.scratch.size() < avail) t.scratch.resize(avail);
            const size_t got = ring.read(t.scratch.data(), avail);
            if (got == 0) break;
            gotData = true;
            writePcm(t, reinterpret_cast<const char*>(t.scratch.data()),
                     static_cast<qint64>(got * sizeof(int16_t)));
            avail = ring.available();
        }
        if (gotData) t.lastDataMs = clock_.elapsed();
        else fillGap(t);
    }

    // Lezárja a sáv encoderét és megvárja; t.ok = a fájl hibátlanul elkészült.
    void finishTrack(EncTrack& t) {
        if (t.closed || !t.proc) return;
        fillGap(t);   // a sáv végi néma szakasz (fillGaps-es sávnál)
        t.closed = true;
        t.proc->closeWriteChannel();
        if (!t.proc->waitForFinished(30000)) { t.proc->kill(); t.proc->waitForFinished(2000); }
        const QFileInfo fi(QDir(folder_).absoluteFilePath(t.fileName));
        t.ok = t.proc->exitStatus() == QProcess::NormalExit && t.proc->exitCode() == 0
               && fi.exists() && fi.size() > 0;
    }

    AudioEngine* engine_ = nullptr;
    QString folder_;
    int opusKbps_ = 64;          // per-sáv Opus bitráta (a hangminőség-beállításból)
    QElapsedTimer clock_;        // a felvétel 0-pontja (a session órájának másolata)
    std::optional<bool> fillGaps_;   // teszt-felülírás; üres → platform szerint
    std::vector<EncTrack> tracks_;
    QTimer* timer_ = nullptr;
    QElapsedTimer meter_;
    bool finalized_ = false;
};

struct RecordingSession::Impl {
    QString audioDir;
    QString title;
    QString userSpeakerName;
    int opusKbps = 64;           // per-sáv Opus bitráta (hangminőség-beállítás)

    QString folder;
    QString id;
    QDateTime startedAt;
    // A felvétel órája: a capture indítása ELŐTT indul, ez a megbeszélés 0-pontja. Ehhez mért
    // minden sáv-eltolás (startOffsetMs) és a megbeszélés hossza.
    QElapsedTimer wall;
    qint64 durationMs = 0;
    RecordingSession::EngineFactory engineFactory;   // üres → valódi AudioEngine
    std::optional<bool> fillGaps;                    // üres → platform szerint

    RecordingState state = RecordingState::Idle;

    std::unique_ptr<AudioEngine> engine;
    std::vector<TrackMeta> tracks;
    QVector<float> trackPeak;   // sávonkénti csúcs-RMS a felvétel alatt
    QStringList usedSlugs;

    QThread* workerThread = nullptr;
    DrainWorker* worker = nullptr;

    void teardownWorker() {
        if (workerThread) {
            workerThread->quit();
            workerThread->wait(3000);
        }
        delete worker; worker = nullptr;          // a dtor törli a QProcess-eket
        delete workerThread; workerThread = nullptr;
    }

    // Egy (elindult) motor-eszközből sáv-metaadat; a slug egyedi a meetingen belül.
    TrackMeta makeTrack(int engineIndex) {
        const AudioDeviceInfo info = engine->deviceInfo(engineIndex);
        TrackMeta t;
        t.engineIndex = engineIndex;
        t.channels = engine->channels(engineIndex);
        if (t.channels <= 0) t.channels = 1;
        t.kind = info.kind;
        t.deviceName = info.name;
        t.device = info;

        QString baseSlug = slugify(info.name), slug = baseSlug;
        int suffix = 2;
        while (usedSlugs.contains(slug)) slug = baseSlug + QStringLiteral("-") + QString::number(suffix++);
        usedSlugs << slug;
        t.slug = slug;
        t.fileName = QStringLiteral("track_") + slug + QStringLiteral(".ogg");
        return t;
    }
};

RecordingSession::RecordingSession(QString audioDir, QString title,
                                   QString userSpeakerName, int opusKbps, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>()) {
    impl_->audioDir = std::move(audioDir);
    impl_->title = std::move(title);
    impl_->userSpeakerName = std::move(userSpeakerName);
    impl_->opusKbps = opusKbps;
}

RecordingSession::~RecordingSession() {
    if (impl_->state != RecordingState::Idle && impl_->worker) {
        // Megszakított / még lezáratlan felvétel: a sávfájlokat itt is tisztán lezárjuk.
        QMetaObject::invokeMethod(impl_->worker, "finalize", Qt::BlockingQueuedConnection);
        if (impl_->engine) impl_->engine->stop();
        impl_->teardownWorker();
    }
}

RecordingState RecordingSession::state() const { return impl_->state; }

void RecordingSession::setEngineFactory(EngineFactory factory) { impl_->engineFactory = std::move(factory); }
void RecordingSession::setFillCaptureGaps(bool on) { impl_->fillGaps = on; }
QString RecordingSession::folder() const { return impl_->folder; }

QVector<AudioDeviceInfo> RecordingSession::trackDevices() const {
    QVector<AudioDeviceInfo> out;
    for (const TrackMeta& t : impl_->tracks) out.push_back(t.device);
    return out;
}

bool RecordingSession::trackOpen(int trackIndex) const {
    return trackIndex >= 0 && trackIndex < static_cast<int>(impl_->tracks.size())
           && !impl_->tracks[static_cast<size_t>(trackIndex)].closed;
}

void RecordingSession::start(const QVector<AudioDeviceInfo>& devices) {
    if (impl_->state != RecordingState::Idle) {
        emit failed(tr("RecordingSession már fut vagy nem üresjáratban van."));
        return;
    }
    if (devices.isEmpty()) {
        emit failed(tr("Nincs felvételre kijelölt eszköz."));
        return;
    }

    // 1) Meeting-mappa.
    impl_->startedAt = QDateTime::currentDateTime();
    const QString stamp = impl_->startedAt.toString(QStringLiteral("yyyy-MM-dd_HHmm"));
    const QString dirName = stamp + QStringLiteral("_") + slugify(impl_->title);
    QDir base(impl_->audioDir);
    if ((!base.exists() && !base.mkpath(QStringLiteral("."))) || !base.mkpath(dirName)) {
        emit failed(tr("Nem hozható létre a meeting-mappa: ") + dirName);
        return;
    }
    impl_->folder = base.absoluteFilePath(dirName);
    impl_->id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 2) AudioEngine. Az óra a capture előtt indul: a kezdő sávok fájljai ettől tartanak.
    impl_->engine = impl_->engineFactory ? impl_->engineFactory() : std::make_unique<AudioEngine>();
    impl_->wall.start();
    if (!impl_->engine || !impl_->engine->start(devices)) {
        emit failed(tr("Az audio motor nem indult el (nincs elérhető eszköz/backend)."));
        impl_->engine.reset();
        return;
    }

    // 3) Sáv-metaadatok (QProcess NÉLKÜL — azt a worker hozza létre a saját szálán).
    const int n = impl_->engine->count();
    impl_->usedSlugs.clear();
    for (int i = 0; i < n; ++i)
        impl_->tracks.push_back(impl_->makeTrack(i));

    // 4) Worker szál — Ő hozza létre+indítja az encodereket (a saját szálán).
    impl_->trackPeak = QVector<float>(static_cast<int>(impl_->tracks.size()), 0.0f);
    impl_->workerThread = new QThread(this);
    impl_->worker = new DrainWorker(impl_->engine.get(), impl_->folder, impl_->opusKbps,
                                    impl_->wall, impl_->fillGaps);
    impl_->worker->moveToThread(impl_->workerThread);
    connect(impl_->worker, &DrainWorker::meter, this,
            [this](int idx, float rms, float peak) {
                if (idx >= 0 && idx < impl_->trackPeak.size())
                    impl_->trackPeak[idx] = qMax(impl_->trackPeak[idx], rms);
                emit levelMeterUpdated(idx, rms);
                emit trackLevel(idx, rms, peak);
            }, Qt::QueuedConnection);
    connect(impl_->worker, &DrainWorker::elapsed, this,
            [this](qint64 ms) { emit elapsedChanged(ms); }, Qt::QueuedConnection);
    connect(impl_->worker, &DrainWorker::finalized, this,
            [this](const QStringList& failedFiles) { onFinalized(failedFiles); },
            Qt::QueuedConnection);
    impl_->workerThread->start();

    bool ok = false;
    DrainWorker* w = impl_->worker;
    const std::vector<TrackMeta> metas = impl_->tracks;
    QMetaObject::invokeMethod(w, [w, metas] { return w->startEncoders(metas); },
                              Qt::BlockingQueuedConnection, &ok);
    if (!ok) {
        emit failed(tr("Nem indult el az ffmpeg encoder."));
        impl_->engine->stop();
        impl_->teardownWorker();
        impl_->engine.reset();
        impl_->tracks.clear();
        return;
    }

    impl_->state = RecordingState::Recording;
    emit stateChanged(impl_->state);
}

bool RecordingSession::addDevice(const AudioDeviceInfo& device) {
    if (impl_->state != RecordingState::Recording || !impl_->engine || !impl_->worker)
        return false;
    for (const TrackMeta& t : impl_->tracks)
        if (!t.closed && t.deviceName == device.name) return false;   // már sávon van

    // A sáv a megnyitás pillanatától szól: ez az eltolása a felvétel 0-pontjához képest.
    const qint64 offsetMs = impl_->wall.elapsed();
    const int engineIndex = impl_->engine->addDevice(device);
    if (engineIndex < 0) return false;

    TrackMeta meta = impl_->makeTrack(engineIndex);
    meta.startOffsetMs = offsetMs;
    bool ok = false;
    DrainWorker* w = impl_->worker;
    QMetaObject::invokeMethod(w, [w, meta] { return w->addEncoder(meta); },
                              Qt::BlockingQueuedConnection, &ok);
    if (!ok) {
        impl_->engine->closeDevice(engineIndex);
        impl_->usedSlugs.removeAll(meta.slug);
        return false;
    }
    impl_->tracks.push_back(meta);
    impl_->trackPeak.push_back(0.0f);
    emit trackAdded(static_cast<int>(impl_->tracks.size()) - 1, meta.deviceName);
    return true;
}

void RecordingSession::closeTrack(const QString& deviceName) {
    if (impl_->state != RecordingState::Recording || !impl_->engine || !impl_->worker)
        return;
    for (size_t i = 0; i < impl_->tracks.size(); ++i) {
        TrackMeta& t = impl_->tracks[i];
        if (t.closed || t.deviceName != deviceName) continue;
        t.closed = true;
        // Előbb a capture áll le (több adat nem jön), utána a worker kiírja a maradékot és
        // lezárja a fájlt — a saját szálán, a fő szál nem vár rá.
        impl_->engine->closeDevice(t.engineIndex);
        DrainWorker* w = impl_->worker;
        const int idx = static_cast<int>(i);
        QMetaObject::invokeMethod(w, [w, idx] { w->closeEncoder(idx); }, Qt::QueuedConnection);
        emit trackClosed(idx, deviceName);
    }
}

void RecordingSession::stop() {
    if (impl_->state != RecordingState::Recording) {
        emit failed(tr("Nincs futó felvétel a leállításhoz."));
        return;
    }

    impl_->state = RecordingState::Stopping;
    emit stateChanged(impl_->state);
    impl_->durationMs = impl_->wall.elapsed();

    // Encoding: a worker utolsó ürítés + ffmpeg-ek lezárása/várása — a worker szálán, a fő
    // szál NEM vár (a UI él). A capture-t csak a lezárás UTÁN állítjuk le (onFinalized),
    // mert a worker még olvassa a motor körpuffereit.
    impl_->state = RecordingState::Encoding;
    emit stateChanged(impl_->state);
    QMetaObject::invokeMethod(impl_->worker, "finalize", Qt::QueuedConnection);
}

void RecordingSession::onFinalized(const QStringList& failedFiles) {
    if (impl_->state != RecordingState::Encoding)
        return;
    if (impl_->engine) impl_->engine->stop();
    impl_->teardownWorker();
    const qint64 durationMs = impl_->durationMs;
    if (!failedFiles.isEmpty())
        qWarning().noquote() << "RecordingSession: hibásan lezárt sávfájl(ok):"
                             << failedFiles.join(QStringLiteral(", "));

    // Csendes sávok meghatározása ELŐRE (a mixdownhoz is kell): a csúcs-RMS-küszöb
    // alattiak inaktívak (auto-eldobás, reverzibilis — a FÁJL MARAD). Ha MINDEN sáv
    // néma lenne, egyiket sem dobjuk (inkább maradjon meg minden).
    constexpr float kSilencePeak = 0.01f;   // tunálható; reverzibilis, ezért óvatosan alacsony
    const int nTr = static_cast<int>(impl_->tracks.size());
    auto peakOf = [&](int i) { return (i < impl_->trackPeak.size()) ? impl_->trackPeak[i] : 0.0f; };
    bool anyAbove = false;
    for (int i = 0; i < nTr; ++i)
        if (peakOf(i) >= kSilencePeak) anyAbove = true;
    auto isActive = [&](int i) { return anyAbove ? (peakOf(i) >= kSilencePeak) : true; };

    // Mixdown SZÁNDÉKOSAN nem itt készül. Korábban a fő szálon, szinkron
    // `QProcess::waitForFinished()`-sel futott — egy 1,5 órás meetingnél ez 15-20 mp-re
    // BEFAGYASZTOTTA a UI-t. A mixdown viszont KIZÁRÓLAG hallgatásra kell (az STT a nyers
    // per-sáv .ogg-kból megy), ezért leválasztottuk: `mixdownFile` üresen marad, és a
    // lekeverést az AppController gyártja le később aszinkron (auto módban azonnal, kézi
    // módban a review-panel gombjáról) — lásd AppController::regenerateMixdown().

    // Meeting összeállítása.
    Meeting m;
    m.id = impl_->id;
    m.title = impl_->title;
    m.folder = impl_->folder;
    m.startedAt = impl_->startedAt;
    m.durationMs = durationMs;
    m.mixdownFile = QString();   // még nincs lekeverés — az AppController készíti

    // FONTOS: a felhasználót NEM hangerő/pozíció alapján nevezzük el — a beszélő
    // azonosítása a VOICE-ID (fingerprint) feladata (autoIdentifyMeeting). Itt a
    // mic-sávok semleges „Mikrofon N" címkét kapnak; a valódi neveket a lenyomat-DB
    // adja (vagy a felhasználó egyszeri kézi átnevezése, ami betanítja).
    int micNo = 1;
    for (int i = 0; i < nTr; ++i) {
        const auto& t = impl_->tracks[i];
        Track tr;
        tr.id = t.slug;
        tr.deviceName = t.deviceName;
        tr.file = t.fileName;
        tr.kind = t.kind;
        if (t.kind == TrackKind::Mic)
            tr.speakerLabel = QStringLiteral("Mikrofon ") + QString::number(micNo++);
        else
            tr.speakerLabel = QStringLiteral("Rendszer");
        tr.fixedSpeaker = true;
        tr.sampleRate = 48000;
        tr.channels = t.channels;
        tr.peakLevel = peakOf(i);
        tr.active = isActive(i);
        tr.startOffsetMs = t.startOffsetMs;
        m.tracks.push_back(tr);
    }

    // Ami biztosan nincs a lemezen (az encoder el sem készítette), az ne legyen sáv.
    const QString folder = impl_->folder;
    m.tracks.erase(std::remove_if(m.tracks.begin(), m.tracks.end(), [&folder](const Track& t) {
        return !QFileInfo::exists(QDir(folder).absoluteFilePath(t.file));
    }), m.tracks.end());

    impl_->tracks.clear();
    impl_->engine.reset();
    impl_->state = RecordingState::Idle;
    emit stateChanged(impl_->state);
    emit finished(m);
}

} // namespace tanara

#include "RecordingSession.moc"

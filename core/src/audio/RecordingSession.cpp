#include "tanara/audio/RecordingSession.h"

#include "tanara/audio/AudioEngine.h"
#include "tanara/audio/RingBuffer.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVector>

#include <cstdint>
#include <memory>
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

// Egy felvett sáv minden lemez-oldali állapota.
struct TrackProc {
    int engineIndex = 0;               // az AudioEngine slot-indexe
    QProcess* ffmpeg = nullptr;        // raw PCM stdin → Opus
    QString fileName;                  // pl. "track_mic.ogg" (relatív)
    QString slug;
    int channels = 1;
    TrackKind kind = TrackKind::Mic;
    QString deviceName;
    QString speakerLabel;
    std::vector<int16_t> scratch;      // drain ideiglenes puffer (worker szálon)
};

} // namespace

// A drain worker: külön QThread-en él egy QTimer, ami periodikusan kiüríti az
// összes körpuffert és a PCM-bájtokat a megfelelő ffmpeg stdin-jére írja.
// Külön QObject, hogy moveToThread-elhető legyen.
class DrainWorker : public QObject {
    Q_OBJECT
public:
    DrainWorker(AudioEngine* engine, std::vector<TrackProc>* tracks)
        : engine_(engine), tracks_(tracks) {}

public slots:
    void begin() {
        timer_ = new QTimer(this);
        timer_->setInterval(20);  // 50 Hz drain; bőven a 30 Hz meter fölött
        connect(timer_, &QTimer::timeout, this, &DrainWorker::tick);
        meter_.start();
        elapsed_.start();
        timer_->start();
    }

    // Utolsó ürítés stop előtt (a maradék minták kiírása).
    void finalDrain() {
        drainOnce(/*emitMeters=*/false);
    }

    void shutdown() {
        if (timer_) { timer_->stop(); }
    }

signals:
    void meter(int trackIndex, float rms);
    void elapsed(qint64 ms);

private slots:
    void tick() {
        drainOnce(/*emitMeters=*/true);
    }

private:
    void drainOnce(bool emitMeters) {
        const int n = static_cast<int>(tracks_->size());
        for (int i = 0; i < n; ++i) {
            TrackProc& t = (*tracks_)[i];
            RingBuffer& ring = engine_->buffer(t.engineIndex);
            size_t avail = ring.available();
            while (avail > 0) {
                if (t.scratch.size() < avail) t.scratch.resize(avail);
                const size_t got = ring.read(t.scratch.data(), avail);
                if (got == 0) break;
                if (t.ffmpeg && t.ffmpeg->state() == QProcess::Running) {
                    t.ffmpeg->write(reinterpret_cast<const char*>(t.scratch.data()),
                                    static_cast<qint64>(got * sizeof(int16_t)));
                }
                avail = ring.available();
            }
        }

        if (emitMeters && meter_.elapsed() >= 33) {  // ~30 Hz
            meter_.restart();
            for (int i = 0; i < n; ++i) {
                emit meter(i, engine_->rms((*tracks_)[i].engineIndex));
            }
            emit elapsed(elapsed_.elapsed());
        }
    }

    AudioEngine* engine_ = nullptr;
    std::vector<TrackProc>* tracks_ = nullptr;
    QTimer* timer_ = nullptr;
    QElapsedTimer meter_;
    QElapsedTimer elapsed_;
};

struct RecordingSession::Impl {
    QString audioDir;
    QString title;
    QString userSpeakerName;

    QString folder;                    // abszolút meeting-mappa
    QString id;
    QDateTime startedAt;
    QElapsedTimer wall;

    RecordingState state = RecordingState::Idle;

    std::unique_ptr<AudioEngine> engine;
    std::vector<TrackProc> tracks;

    QThread* workerThread = nullptr;
    DrainWorker* worker = nullptr;
};

RecordingSession::RecordingSession(QString audioDir, QString title,
                                   QString userSpeakerName, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>()) {
    impl_->audioDir = std::move(audioDir);
    impl_->title = std::move(title);
    impl_->userSpeakerName = std::move(userSpeakerName);
}

RecordingSession::~RecordingSession() {
    if (impl_->state == RecordingState::Recording) {
        // Best-effort takarítás; nem emittálunk a destruktorból.
        if (impl_->worker) {
            QMetaObject::invokeMethod(impl_->worker, "shutdown", Qt::BlockingQueuedConnection);
        }
        if (impl_->engine) impl_->engine->stop();
        if (impl_->workerThread) {
            impl_->workerThread->quit();
            impl_->workerThread->wait(2000);
        }
        for (auto& t : impl_->tracks) {
            if (t.ffmpeg) {
                t.ffmpeg->closeWriteChannel();
                t.ffmpeg->waitForFinished(3000);
                delete t.ffmpeg;
            }
        }
    }
}

RecordingState RecordingSession::state() const { return impl_->state; }
QString RecordingSession::folder() const { return impl_->folder; }

void RecordingSession::start(const QVector<AudioDeviceInfo>& devices) {
    if (impl_->state != RecordingState::Idle) {
        emit failed(QStringLiteral("RecordingSession már fut vagy nem üresjáratban van."));
        return;
    }
    if (devices.isEmpty()) {
        emit failed(QStringLiteral("Nincs felvételre kijelölt eszköz."));
        return;
    }

    // 1) Meeting-mappa.
    impl_->startedAt = QDateTime::currentDateTime();
    const QString stamp = impl_->startedAt.toString(QStringLiteral("yyyy-MM-dd_HHmm"));
    const QString dirName = stamp + QStringLiteral("_") + slugify(impl_->title);

    QDir base(impl_->audioDir);
    if (!base.exists() && !base.mkpath(QStringLiteral("."))) {
        emit failed(QStringLiteral("Nem hozható létre az audioDir: ") + impl_->audioDir);
        return;
    }
    if (!base.mkpath(dirName)) {
        emit failed(QStringLiteral("Nem hozható létre a meeting-mappa: ") + dirName);
        return;
    }
    impl_->folder = base.absoluteFilePath(dirName);
    impl_->id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 2) AudioEngine indítása.
    impl_->engine = std::make_unique<AudioEngine>();
    if (!impl_->engine->start(devices)) {
        emit failed(QStringLiteral("Az audio motor nem indult el (nincs elérhető eszköz/backend)."));
        impl_->engine.reset();
        return;
    }

    // 3) Eszközönként ffmpeg encoder + TrackProc. Egyedi slug-ok (ütközés esetén
    //    sorszám-utótaggal).
    const int n = impl_->engine->count();
    QStringList usedSlugs;
    int micSeen = 0;
    for (int i = 0; i < n; ++i) {
        const AudioDeviceInfo info = impl_->engine->deviceInfo(i);

        TrackProc t;
        t.engineIndex = i;
        t.channels = impl_->engine->channels(i);
        if (t.channels <= 0) t.channels = 1;
        t.kind = info.kind;
        t.deviceName = info.name;

        QString baseSlug = slugify(info.name);
        QString slug = baseSlug;
        int suffix = 2;
        while (usedSlugs.contains(slug)) {
            slug = baseSlug + QStringLiteral("-") + QString::number(suffix++);
        }
        usedSlugs << slug;
        t.slug = slug;
        t.fileName = QStringLiteral("track_") + slug + QStringLiteral(".ogg");

        // Beszélő-cimkék: első mic → userSpeakerName; további mic-ek → "Beszélő 2/3..";
        // loopback/egyéb → "Rendszer".
        if (info.kind == TrackKind::Mic) {
            if (micSeen == 0) {
                t.speakerLabel = impl_->userSpeakerName;
            } else {
                t.speakerLabel = QStringLiteral("Beszélő ") + QString::number(micSeen + 1);
            }
            ++micSeen;
        } else {
            t.speakerLabel = QStringLiteral("Rendszer");
        }

        QProcess* proc = new QProcess(this);
        const QString outPath = QDir(impl_->folder).absoluteFilePath(t.fileName);
        const QStringList args{
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("s16le"),
            QStringLiteral("-ar"), QStringLiteral("48000"),
            QStringLiteral("-ac"), QString::number(t.channels),
            QStringLiteral("-i"), QStringLiteral("pipe:0"),
            QStringLiteral("-c:a"), QStringLiteral("libopus"),
            QStringLiteral("-b:a"), QStringLiteral("64k"),
            QStringLiteral("-y"), outPath};
        proc->start(QStringLiteral("ffmpeg"), args);
        if (!proc->waitForStarted(5000)) {
            // Egy encoder nem indult: teljes leállás, takarítás.
            emit failed(QStringLiteral("Nem indult el az ffmpeg encoder: ") + t.deviceName);
            delete proc;
            impl_->engine->stop();
            impl_->engine.reset();
            for (auto& done : impl_->tracks) {
                if (done.ffmpeg) { done.ffmpeg->kill(); delete done.ffmpeg; }
            }
            impl_->tracks.clear();
            return;
        }
        t.ffmpeg = proc;
        impl_->tracks.push_back(std::move(t));
    }

    // 4) Drain worker külön szálon. A QProcess-ek a fő szálon élnek (parent=this);
    //    a write() bármely szálról hívható ugyanazon QProcess-re itt szekvenciálisan,
    //    de a biztonság kedvéért a write-okat a worker végzi és csak ő nyúl hozzá.
    impl_->workerThread = new QThread(this);
    impl_->worker = new DrainWorker(impl_->engine.get(), &impl_->tracks);
    impl_->worker->moveToThread(impl_->workerThread);

    connect(impl_->worker, &DrainWorker::meter, this,
            [this](int idx, float rms) { emit levelMeterUpdated(idx, rms); },
            Qt::QueuedConnection);
    connect(impl_->worker, &DrainWorker::elapsed, this,
            [this](qint64 ms) { emit elapsedChanged(ms); }, Qt::QueuedConnection);
    connect(impl_->workerThread, &QThread::started, impl_->worker, &DrainWorker::begin);

    impl_->wall.start();
    impl_->workerThread->start();

    impl_->state = RecordingState::Recording;
    emit stateChanged(impl_->state);
}

void RecordingSession::stop() {
    if (impl_->state != RecordingState::Recording) {
        emit failed(QStringLiteral("Nincs futó felvétel a leállításhoz."));
        return;
    }

    impl_->state = RecordingState::Stopping;
    emit stateChanged(impl_->state);

    const qint64 durationMs = impl_->wall.elapsed();

    // 1) Capture leállítása — a callbackek többé nem írnak a ringekbe.
    if (impl_->engine) impl_->engine->stop();

    // 2) Worker: utolsó ürítés (maradék minták), majd leállás.
    if (impl_->worker) {
        QMetaObject::invokeMethod(impl_->worker, "finalDrain", Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(impl_->worker, "shutdown", Qt::BlockingQueuedConnection);
    }
    if (impl_->workerThread) {
        impl_->workerThread->quit();
        impl_->workerThread->wait(3000);
        delete impl_->worker;
        impl_->worker = nullptr;
        delete impl_->workerThread;
        impl_->workerThread = nullptr;
    }

    // 3) Encoderek lezárása: stdin flush + close, majd várakozás a befejezésre.
    impl_->state = RecordingState::Encoding;
    emit stateChanged(impl_->state);

    for (auto& t : impl_->tracks) {
        if (!t.ffmpeg) continue;
        t.ffmpeg->closeWriteChannel();
        if (!t.ffmpeg->waitForFinished(30000)) {
            t.ffmpeg->kill();
            t.ffmpeg->waitForFinished(2000);
        }
    }

    // 4) Mixdown: az összes sáv → egyetlen .mp3 (amix). Ha 0/1 sáv van, az amix is
    //    helyesen viselkedik (1 bemenettel egyszerű átkódolás).
    QString mixdownRel;
    if (!impl_->tracks.empty()) {
        QStringList args{QStringLiteral("-hide_banner"),
                         QStringLiteral("-loglevel"), QStringLiteral("error")};
        for (const auto& t : impl_->tracks) {
            args << QStringLiteral("-i") << QDir(impl_->folder).absoluteFilePath(t.fileName);
        }
        const int inputs = static_cast<int>(impl_->tracks.size());
        const QString filter =
            QStringLiteral("amix=inputs=%1:duration=longest:normalize=0").arg(inputs);
        mixdownRel = QStringLiteral("mixdown.mp3");
        args << QStringLiteral("-filter_complex") << filter
             << QStringLiteral("-c:a") << QStringLiteral("libmp3lame")
             << QStringLiteral("-q:a") << QStringLiteral("4")
             << QStringLiteral("-y")
             << QDir(impl_->folder).absoluteFilePath(mixdownRel);

        QProcess mix;
        mix.start(QStringLiteral("ffmpeg"), args);
        bool mixOk = mix.waitForStarted(5000);
        if (mixOk) {
            mixOk = mix.waitForFinished(120000) && mix.exitStatus() == QProcess::NormalExit
                    && mix.exitCode() == 0;
        }
        if (!mixOk) {
            mixdownRel.clear();  // nem fatális: a sávok megvannak, mixdown nélkül megyünk tovább
        }
    }

    // 5) Meeting összeállítása.
    Meeting m;
    m.id = impl_->id;
    m.title = impl_->title;
    m.folder = impl_->folder;
    m.startedAt = impl_->startedAt;
    m.durationMs = durationMs;
    m.mixdownFile = mixdownRel;
    for (const auto& t : impl_->tracks) {
        Track tr;
        tr.id = t.slug;
        tr.deviceName = t.deviceName;
        tr.file = t.fileName;
        tr.speakerLabel = t.speakerLabel;
        tr.kind = t.kind;
        tr.fixedSpeaker = true;
        tr.sampleRate = 48000;
        tr.channels = t.channels;
        m.tracks.push_back(tr);
    }

    // 6) Takarítás.
    for (auto& t : impl_->tracks) {
        if (t.ffmpeg) { delete t.ffmpeg; t.ffmpeg = nullptr; }
    }
    impl_->tracks.clear();
    impl_->engine.reset();

    impl_->state = RecordingState::Idle;
    emit stateChanged(impl_->state);
    emit finished(m);
}

} // namespace tanara

#include "RecordingSession.moc"

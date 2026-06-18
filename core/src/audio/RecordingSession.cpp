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

// Egy sáv lemez-oldali metaadata (a QProcess NEM itt él — azt a worker birtokolja
// a saját szálán, hogy semmilyen cross-thread QProcess hozzáférés ne legyen).
struct TrackMeta {
    int engineIndex = 0;
    QString fileName;          // pl. "track_mic.ogg" (relatív)
    QString slug;
    int channels = 1;
    TrackKind kind = TrackKind::Mic;
    QString deviceName;
    QString speakerLabel;
};

} // namespace

// A drain worker külön QThread-en él. MINDEN ffmpeg QProcess-t Ő hoz létre, Ő ír
// rá és Ő zárja le — ugyanazon a szálon, így nincs "QSocketNotifier from another
// thread" probléma és nincs adatvesztés-kockázat.
class DrainWorker : public QObject {
    Q_OBJECT
public:
    DrainWorker(AudioEngine* engine, const std::vector<TrackMeta>* tracks, QString folder)
        : engine_(engine), tracks_(tracks), folder_(std::move(folder)) {}

    ~DrainWorker() override {
        for (QProcess* p : procs_) delete p;
    }

public slots:
    // A worker szálán fut (BlockingQueuedConnection). Létrehozza+indítja az
    // encodereket; true ha minden elindult. Sikertelenségnél visszatakarít.
    bool startEncoders() {
        const int n = static_cast<int>(tracks_->size());
        procs_.assign(n, nullptr);
        scratch_.resize(n);
        for (int i = 0; i < n; ++i) {
            const TrackMeta& t = (*tracks_)[i];
            auto* proc = new QProcess(this);          // a worker szálon él
            const QString outPath = QDir(folder_).absoluteFilePath(t.fileName);
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
                delete proc;
                for (QProcess* p : procs_) { if (p) { p->kill(); delete p; } }
                procs_.clear();
                return false;
            }
            procs_[i] = proc;
        }
        timer_ = new QTimer(this);
        timer_->setInterval(20);   // 50 Hz drain
        connect(timer_, &QTimer::timeout, this, &DrainWorker::tick);
        meter_.start();
        elapsed_.start();
        timer_->start();
        return true;
    }

    // Stopkor (BlockingQueuedConnection): utolsó ürítés + encoderek lezárása,
    // megvárva az ffmpeg-ek befejezését — mind a worker szálon.
    void finalize() {
        if (timer_) { timer_->stop(); }
        drainOnce(/*emitMeters=*/false);
        for (QProcess* p : procs_) {
            if (!p) continue;
            p->closeWriteChannel();
            if (!p->waitForFinished(30000)) { p->kill(); p->waitForFinished(2000); }
        }
    }

signals:
    void meter(int trackIndex, float rms);
    void elapsed(qint64 ms);

private slots:
    void tick() { drainOnce(/*emitMeters=*/true); }

private:
    void drainOnce(bool emitMeters) {
        const int n = static_cast<int>(tracks_->size());
        for (int i = 0; i < n; ++i) {
            RingBuffer& ring = engine_->buffer((*tracks_)[i].engineIndex);
            size_t avail = ring.available();
            while (avail > 0) {
                if (scratch_[i].size() < avail) scratch_[i].resize(avail);
                const size_t got = ring.read(scratch_[i].data(), avail);
                if (got == 0) break;
                QProcess* p = procs_[i];
                if (p && p->state() == QProcess::Running) {
                    p->write(reinterpret_cast<const char*>(scratch_[i].data()),
                             static_cast<qint64>(got * sizeof(int16_t)));
                }
                avail = ring.available();
            }
        }
        if (emitMeters && meter_.elapsed() >= 33) {   // ~30 Hz
            meter_.restart();
            for (int i = 0; i < n; ++i)
                emit meter(i, engine_->rms((*tracks_)[i].engineIndex));
            emit elapsed(elapsed_.elapsed());
        }
    }

    AudioEngine* engine_ = nullptr;
    const std::vector<TrackMeta>* tracks_ = nullptr;
    QString folder_;
    std::vector<QProcess*> procs_;
    std::vector<std::vector<int16_t>> scratch_;
    QTimer* timer_ = nullptr;
    QElapsedTimer meter_;
    QElapsedTimer elapsed_;
};

struct RecordingSession::Impl {
    QString audioDir;
    QString title;
    QString userSpeakerName;

    QString folder;
    QString id;
    QDateTime startedAt;
    QElapsedTimer wall;

    RecordingState state = RecordingState::Idle;

    std::unique_ptr<AudioEngine> engine;
    std::vector<TrackMeta> tracks;
    QVector<float> trackPeak;   // sávonkénti csúcs-RMS a felvétel alatt

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
        if (impl_->worker)
            QMetaObject::invokeMethod(impl_->worker, "finalize", Qt::BlockingQueuedConnection);
        if (impl_->engine) impl_->engine->stop();
        impl_->teardownWorker();
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
    if ((!base.exists() && !base.mkpath(QStringLiteral("."))) || !base.mkpath(dirName)) {
        emit failed(QStringLiteral("Nem hozható létre a meeting-mappa: ") + dirName);
        return;
    }
    impl_->folder = base.absoluteFilePath(dirName);
    impl_->id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 2) AudioEngine.
    impl_->engine = std::make_unique<AudioEngine>();
    if (!impl_->engine->start(devices)) {
        emit failed(QStringLiteral("Az audio motor nem indult el (nincs elérhető eszköz/backend)."));
        impl_->engine.reset();
        return;
    }

    // 3) Sáv-metaadatok (QProcess NÉLKÜL — azt a worker hozza létre a saját szálán).
    const int n = impl_->engine->count();
    QStringList usedSlugs;
    int micSeen = 0;
    for (int i = 0; i < n; ++i) {
        const AudioDeviceInfo info = impl_->engine->deviceInfo(i);
        TrackMeta t;
        t.engineIndex = i;
        t.channels = impl_->engine->channels(i);
        if (t.channels <= 0) t.channels = 1;
        t.kind = info.kind;
        t.deviceName = info.name;

        QString baseSlug = slugify(info.name), slug = baseSlug;
        int suffix = 2;
        while (usedSlugs.contains(slug)) slug = baseSlug + QStringLiteral("-") + QString::number(suffix++);
        usedSlugs << slug;
        t.slug = slug;
        t.fileName = QStringLiteral("track_") + slug + QStringLiteral(".ogg");

        if (info.kind == TrackKind::Mic) {
            t.speakerLabel = (micSeen == 0) ? impl_->userSpeakerName
                                            : QStringLiteral("Beszélő ") + QString::number(micSeen + 1);
            ++micSeen;
        } else {
            t.speakerLabel = QStringLiteral("Rendszer");
        }
        impl_->tracks.push_back(std::move(t));
    }

    // 4) Worker szál — Ő hozza létre+indítja az encodereket (a saját szálán).
    impl_->trackPeak = QVector<float>(static_cast<int>(impl_->tracks.size()), 0.0f);
    impl_->workerThread = new QThread(this);
    impl_->worker = new DrainWorker(impl_->engine.get(), &impl_->tracks, impl_->folder);
    impl_->worker->moveToThread(impl_->workerThread);
    connect(impl_->worker, &DrainWorker::meter, this,
            [this](int idx, float rms) {
                if (idx >= 0 && idx < impl_->trackPeak.size())
                    impl_->trackPeak[idx] = qMax(impl_->trackPeak[idx], rms);
                emit levelMeterUpdated(idx, rms);
            }, Qt::QueuedConnection);
    connect(impl_->worker, &DrainWorker::elapsed, this,
            [this](qint64 ms) { emit elapsedChanged(ms); }, Qt::QueuedConnection);
    impl_->workerThread->start();

    bool ok = false;
    QMetaObject::invokeMethod(impl_->worker, "startEncoders",
                              Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, ok));
    if (!ok) {
        emit failed(QStringLiteral("Nem indult el az ffmpeg encoder."));
        impl_->engine->stop();
        impl_->teardownWorker();
        impl_->engine.reset();
        impl_->tracks.clear();
        return;
    }

    impl_->wall.start();
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

    // Capture leáll → a callbackek nem írnak többé a ringekbe.
    if (impl_->engine) impl_->engine->stop();

    // Encoding: a worker utolsó ürítés + ffmpeg-ek lezárása/várása (a worker szálán).
    impl_->state = RecordingState::Encoding;
    emit stateChanged(impl_->state);
    if (impl_->worker)
        QMetaObject::invokeMethod(impl_->worker, "finalize", Qt::BlockingQueuedConnection);
    impl_->teardownWorker();

    // Mixdown — a fő szálon létrehozott+használt tranziens QProcess (egy szál, OK).
    QString mixdownRel;
    if (!impl_->tracks.empty()) {
        QStringList args{QStringLiteral("-hide_banner"),
                         QStringLiteral("-loglevel"), QStringLiteral("error")};
        for (const auto& t : impl_->tracks)
            args << QStringLiteral("-i") << QDir(impl_->folder).absoluteFilePath(t.fileName);
        const int inputs = static_cast<int>(impl_->tracks.size());
        mixdownRel = QStringLiteral("mixdown.mp3");
        args << QStringLiteral("-filter_complex")
             << QStringLiteral("amix=inputs=%1:duration=longest:normalize=0").arg(inputs)
             << QStringLiteral("-c:a") << QStringLiteral("libmp3lame")
             << QStringLiteral("-q:a") << QStringLiteral("4")
             << QStringLiteral("-y") << QDir(impl_->folder).absoluteFilePath(mixdownRel);
        QProcess mix;
        mix.start(QStringLiteral("ffmpeg"), args);
        bool mixOk = mix.waitForStarted(5000)
                     && mix.waitForFinished(120000)
                     && mix.exitStatus() == QProcess::NormalExit && mix.exitCode() == 0;
        if (!mixOk) mixdownRel.clear();   // nem fatális
    }

    // Meeting összeállítása.
    Meeting m;
    m.id = impl_->id;
    m.title = impl_->title;
    m.folder = impl_->folder;
    m.startedAt = impl_->startedAt;
    m.durationMs = durationMs;
    m.mixdownFile = mixdownRel;
    // Csendes sávok auto-eldobása: a csúcs-RMS-küszöb alattiak active=false-ot kapnak
    // (a FÁJL MARAD a lemezen — utólag visszaállítható/törölhető). Ha MINDEN sáv a
    // küszöb alatt lenne, egyiket sem dobjuk (inkább maradjon meg minden).
    constexpr float kSilencePeak = 0.01f;   // tunálható; reverzibilis, ezért óvatosan alacsony
    bool anyAbove = false;
    for (int i = 0; i < static_cast<int>(impl_->tracks.size()); ++i)
        if (i < impl_->trackPeak.size() && impl_->trackPeak[i] >= kSilencePeak) anyAbove = true;
    for (int i = 0; i < static_cast<int>(impl_->tracks.size()); ++i) {
        const auto& t = impl_->tracks[i];
        Track tr;
        tr.id = t.slug;
        tr.deviceName = t.deviceName;
        tr.file = t.fileName;
        tr.speakerLabel = t.speakerLabel;
        tr.kind = t.kind;
        tr.fixedSpeaker = true;
        tr.sampleRate = 48000;
        tr.channels = t.channels;
        tr.peakLevel = (i < impl_->trackPeak.size()) ? impl_->trackPeak[i] : 0.0f;
        tr.active = anyAbove ? (tr.peakLevel >= kSilencePeak) : true;
        m.tracks.push_back(tr);
    }

    impl_->tracks.clear();
    impl_->engine.reset();
    impl_->state = RecordingState::Idle;
    emit stateChanged(impl_->state);
    emit finished(m);
}

} // namespace tanara

#include "RecordingSession.moc"

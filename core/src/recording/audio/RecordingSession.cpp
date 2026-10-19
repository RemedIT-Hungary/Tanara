#include "tanara/recording/audio/RecordingSession.h"

#include "tanara/recording/audio/AudioEngine.h"
#include "tanara/recording/audio/RingBuffer.h"
#include "tanara/recording/audio/SystemAudioSource.h"
#include "tanara/recording/audio/TrackTiming.h"
#include "tanara/foundation/log/Logging.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVector>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace tanara::recording {

namespace {

// Slug: accents/spaces -> hyphenated, lowercase, an ASCII-safe file-name part.
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

// A track's on-disk metadata (the encoder does NOT live here: the worker owns it on its own
// thread, so there is no cross-thread process access).
struct TrackMeta {
    int engineIndex = 0;
    QString fileName;          // e.g. "track_mic.ogg" (relative)
    QString slug;
    int channels = 1;
    TrackKind kind = TrackKind::Mic;
    QString deviceName;
    AudioDeviceInfo device;
    bool closed = false;       // closed mid-run (a detached / switched-off device)
    // The time the track was opened, from the recording's zero point (ms) -> Track::startOffsetMs. The
    // file holds audio from the open on, WITHOUT silence padding.
    qint64 startOffsetMs = 0;
    // The segment this one continues (a re-opened lost track) -> Track::continuesTrackId.
    QString continuesTrackId;
};

constexpr int kSampleRate = 48000;

qint64 steadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

// The recording clock, shared by the session (main thread) and the drain worker. It is the monotonic
// clock since the start PLUS the sleeps the drain worker detected (the monotonic clock does not
// advance during a suspend on Linux): so it follows the wall clock across a sleep, and a segment
// opened after the wake-up gets its wall-clock place as its offset (wall start -> now), while drift
// and clock setting between ticks do not disturb it.
struct RecordingClock {
    std::function<qint64()> monotonic;   // empty = steady_clock
    std::function<qint64()> wall;        // empty = the system clock
    qint64 monotonicStart = 0;
    qint64 wallStart = 0;
    std::atomic<qint64> slept{0};

    qint64 monotonicNow() const { return monotonic ? monotonic() : steadyNowMs(); }
    qint64 wallNow() const { return wall ? wall() : QDateTime::currentMSecsSinceEpoch(); }
    void start()
    {
        monotonicStart = monotonicNow();
        wallStart = wallNow();
        slept.store(0);
    }
    // The recording-clock position (ms since the recording's zero point).
    qint64 elapsed() const { return monotonicNow() - monotonicStart + slept.load(); }
};

// The drain worker lives on a separate QThread. It creates EVERY encoder (media::EncoderHandle), writes to it and
// closes it, all on the same thread, so there is no "QSocketNotifier from another thread" problem
// and no risk of data loss. It keeps ITS OWN copy of the tracks (EncTrack) and never touches the
// main thread's list.
//
// Timing: `clock` is the recording's zero point (it starts before capture starts). A track's file
// starts at its open; the open time is TrackMeta::startOffsetMs (metadata), and NO silence goes at the
// start of the file. Where the platform silently delivers no samples (Windows WASAPI loopback, see
// tracktiming::fillsCaptureGaps), the worker fills the shortfall with silence measured against the wall clock.
class DrainWorker : public QObject {
    Q_OBJECT
public:
    DrainWorker(media::MediaToolPtr media, AudioEngine* engine, QString folder, int opusKbps,
                std::shared_ptr<RecordingClock> clock, std::optional<bool> fillGaps, qint64 stallWarnMs,
                bool splitOnSleep)
        : media_(std::move(media)), engine_(engine), folder_(std::move(folder)), opusKbps_(opusKbps),
          clock_(std::move(clock)), fillGaps_(fillGaps), stallWarnMs_(stallWarnMs),
          splitOnSleep_(splitOnSleep) {}

    ~DrainWorker() override = default;   // the encoder handles kill + reap still running encoders

    // Run on the worker's thread (called with BlockingQueuedConnection). Creates + starts the encoders;
    // true if all started. On failure it cleans up.
    bool startEncoders(const std::vector<TrackMeta>& metas) {
        for (const TrackMeta& m : metas) {
            if (!openEncoder(m)) {
                for (EncTrack& t : tracks_) { if (t.proc) { t.proc->kill(); t.proc.reset(); } }
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

    // A track added during recording: the encoder starts empty, the file runs from the open (the offset
    // is metadata: TrackMeta::startOffsetMs).
    bool addEncoder(const TrackMeta& meta) { return openEncoder(meta); }

    // Closing a track mid-run: write out the remainder, close the encoder (waiting for it).
    // detached: the device is gone, so the track is reported stalled once (it will not resume); a track
    // the user switched off is not a stall, and a warning it had is withdrawn (resumed).
    void closeEncoder(int index, bool detached) {
        if (index < 0 || index >= static_cast<int>(tracks_.size())) return;
        EncTrack& t = tracks_[static_cast<size_t>(index)];
        drainTrack(t);
        finishTrack(t);
        if (detached && !t.stalled) {
            t.stalled = true;
            emit stalled(index, t.lastDataMs);
        } else if (!detached && t.stalled) {
            t.stalled = false;
            emit resumed(index);
        }
    }

public slots:
    // At stop: a last drain + closing the encoders, waiting for the ffmpegs to finish, all on the worker
    // thread. At the end finalized(): from then on every track file is on disk.
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
    void stalled(int trackIndex, qint64 sinceMs);   // no data since sinceMs (recording clock)
    void resumed(int trackIndex);
    // The machine slept: wall-clock bounds (epoch ms) and the recording clock before / after the jump.
    void slept(qint64 fromWallMs, qint64 toWallMs, qint64 fromMs, qint64 toMs);

private slots:
    void tick() {
        checkSleep();
        for (EncTrack& t : tracks_) drainTrack(t);
        checkStalls();
        if (meter_.elapsed() >= 33) {   // ~30 Hz
            meter_.restart();
            for (size_t i = 0; i < tracks_.size(); ++i) {
                if (tracks_[i].closed) continue;
                emit meter(static_cast<int>(i), engine_->rms(tracks_[i].engineIndex),
                           engine_->takePeak(tracks_[i].engineIndex));
            }
            emit elapsed(clock_->elapsed());
        }
    }

private:
    struct EncTrack {
        int engineIndex = 0;
        QString fileName;
        int channels = 1;
        std::shared_ptr<media::EncoderHandle> proc;   // lives on the worker thread
        qint64 openedAtMs = 0;     // the track's open per the recording clock
        qint64 lastDataMs = 0;     // when data last came from the device (recording clock)
        qint64 framesWritten = 0;  // frames written to the file (real audio + filled silence)
        bool fillGaps = false;     // silent gaps must be filled with silence (loopback track, on every platform)
        bool watchStall = false;   // a data-less stretch is reported (not for ISystemAudioSource::deliversNothingWhenIdle)
        bool stalled = false;      // reported stalled, not yet resumed
        // Cut by a sleep: the session closes it and re-opens the device in a new segment. Until the
        // close arrives its data (from the stale device) is dropped and its gap fill stops at capMs,
        // so the sleep is never filled with silence: the gap is metadata.
        bool suspended = false;
        qint64 capMs = -1;
        bool closed = false;
        bool ok = false;           // the encoder closed the file without error
        std::vector<int16_t> scratch;
    };

    bool openEncoder(const TrackMeta& t) {
        // Created on the worker thread (the encoder's process lives here).
        media::EncodeSpec spec;
        spec.input = media::PcmFormat{kSampleRate, t.channels, media::SampleFormat::S16};
        spec.outputPath = QDir(folder_).absoluteFilePath(t.fileName);
        spec.bitrateKbps = opusKbps_;
        std::shared_ptr<media::EncoderHandle> proc = media_ ? media_->startEncoder(spec) : nullptr;
        if (!proc) return false;
        EncTrack e;
        e.engineIndex = t.engineIndex;
        e.fileName = t.fileName;
        e.channels = t.channels;
        e.proc = std::move(proc);
        e.openedAtMs = t.startOffsetMs;
        e.lastDataMs = t.startOffsetMs;
        e.fillGaps = fillGaps_.value_or(tracktiming::fillsCaptureGaps(t.kind));
        e.watchStall = !systemAudioSource().deliversNothingWhenIdle(t.kind);
        tracks_.push_back(std::move(e));
        return true;
    }

    void writePcm(EncTrack& t, const char* data, qint64 bytes) {
        t.proc->write(data, bytes);   // ignored once the encoder stopped
        t.framesWritten += bytes / (qint64(sizeof(int16_t)) * t.channels);
    }

    // Filling a silent (data-less) device's missing frames with silence: see tracktiming::silenceToInsert.
    // Only for a fillGaps track.
    void fillGap(EncTrack& t) {
        if (!t.fillGaps) return;
        qint64 now = clock_->elapsed();
        if (t.capMs >= 0) now = qMin(now, t.capMs);
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
        if (t.suspended) {   // stale data after the wake-up: not this segment's (see EncTrack::suspended)
            if (t.scratch.size() < 4096) t.scratch.resize(4096);
            while (ring.read(t.scratch.data(), t.scratch.size()) > 0) {}
            return;
        }
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
        if (gotData) {
            t.lastDataMs = clock_->elapsed();
            if (t.stalled) {
                t.stalled = false;
                emit resumed(static_cast<int>(&t - tracks_.data()));
            }
        } else {
            fillGap(t);
        }
    }

    // An open, watched track with no data for stallWarnMs_ is reported once (see tracktiming::isStalled).
    // Filled silence does not count as data: lastDataMs moves only on real samples.
    void checkStalls() {
        const qint64 now = clock_->elapsed();
        for (size_t i = 0; i < tracks_.size(); ++i) {
            EncTrack& t = tracks_[i];
            if (t.closed || t.stalled || t.suspended || !t.watchStall) continue;
            if (!tracktiming::isStalled(now - t.lastDataMs, stallWarnMs_)) continue;
            t.stalled = true;
            emit stalled(static_cast<int>(i), t.lastDataMs);
        }
    }

    // Sleep detection (tracktiming::sleepJumpMs): the wall clock ran ahead of the monotonic one since
    // the previous tick. The recording clock takes the jump over; with splitOnSleep the open tracks
    // are suspended at the sleep's start (the session closes and re-opens them).
    void checkSleep() {
        const qint64 mono = clock_->monotonicNow();
        const qint64 wall = clock_->wallNow();
        if (ticked_) {
            const qint64 dMono = mono - lastTickMono_;
            const qint64 dWall = wall - lastTickWall_;
            if (const qint64 jump = tracktiming::sleepJumpMs(dWall, dMono); jump > 0) {
                const qint64 fromMs = clock_->elapsed();   // the monotonic clock stood still: the sleep's start
                clock_->slept.fetch_add(jump);
                const qint64 toMs = clock_->elapsed();
                if (splitOnSleep_) {
                    for (EncTrack& t : tracks_) {
                        if (t.closed) continue;
                        t.suspended = true;
                        t.capMs = fromMs;
                    }
                }
                emit slept(lastTickWall_, wall, fromMs, toMs);
            } else if (dMono - dWall > tracktiming::kSleepJumpMs) {
                qCWarning(lcAudio).noquote()
                    << QStringLiteral("Recording: the wall clock went back by %1 ms (clock set?); the recording clock is unaffected")
                           .arg(dMono - dWall);
            }
        }
        ticked_ = true;
        lastTickMono_ = mono;
        lastTickWall_ = wall;
    }

    // Closes the track's encoder and waits; t.ok = the file was made without error.
    void finishTrack(EncTrack& t) {
        if (t.closed || !t.proc) return;
        fillGap(t);   // the silent stretch at the track's end (for a fillGaps track)
        t.closed = true;
        const bool exitedOk = t.proc->finish(30000);   // closes the input, waits (killed after 30 s)
        const QFileInfo fi(QDir(folder_).absoluteFilePath(t.fileName));
        t.ok = exitedOk && fi.exists() && fi.size() > 0;
    }

    media::MediaToolPtr media_;
    AudioEngine* engine_ = nullptr;
    QString folder_;
    int opusKbps_ = 64;          // per-track Opus bitrate (from the audio quality setting)
    std::shared_ptr<RecordingClock> clock_;   // the recording clock (shared with the session)
    std::optional<bool> fillGaps_;   // test override; empty = per platform
    qint64 stallWarnMs_ = tracktiming::kStallWarnMs;
    bool splitOnSleep_ = false;      // a sleep suspends the open tracks (the session re-opens them)
    bool ticked_ = false;            // the previous tick's clocks (sleep detection)
    qint64 lastTickMono_ = 0;
    qint64 lastTickWall_ = 0;
    std::vector<EncTrack> tracks_;
    QTimer* timer_ = nullptr;
    QElapsedTimer meter_;
    bool finalized_ = false;
};

struct RecordingSession::Impl {
    media::MediaToolPtr media;
    QString audioDir;
    QString title;
    QString userSpeakerName;
    int opusKbps = 64;           // per-track Opus bitrate (audio quality setting)

    QString folder;
    QString id;
    QDateTime startedAt;
    // The recording clock: it starts BEFORE capture starts and is the meeting's zero point. Every track
    // offset (startOffsetMs) and the meeting's length are measured against it. Shared with the worker.
    std::shared_ptr<RecordingClock> clock = std::make_shared<RecordingClock>();
    qint64 durationMs = 0;
    RecordingSession::EngineFactory engineFactory;   // empty = the real AudioEngine
    std::optional<bool> fillGaps;                    // empty = per platform
    qint64 stallWarnMs = tracktiming::kStallWarnMs;
    QMap<QString, qint64> stalled;                   // device name -> last data (recording clock)

    // Re-open after loss.
    RecordingSession::DeviceLister lister;           // empty = no re-open
    qint64 reopenPollMs = tracktiming::kReopenPollMs;
    qint64 reopenWindowMs = tracktiming::kReopenWindowMs;
    struct PendingReopen {
        AudioDeviceInfo device;    // the lost track's device (matched by id, then by name)
        QString reason;            // tracktiming::kGapSleep | kGapDeviceLost
        QString prevTrackId;       // the lost segment (-> the new one's continuesTrackId)
        qint64 sinceMs = 0;        // recording clock when re-opening began (the window runs from here)
        qint64 gapFromMs = 0;      // where the lost segment ended (recording clock)
        qint64 gapFromWallMs = 0;
    };
    QVector<PendingReopen> pending;
    QTimer* reopenTimer = nullptr;
    QTimer* lossTimer = nullptr;   // watches the engine for devices the backend stopped
    QVector<RecordingGap> gaps;

    RecordingState state = RecordingState::Idle;

    std::unique_ptr<AudioEngine> engine;
    std::vector<TrackMeta> tracks;
    QVector<float> trackPeak;   // per-track peak RMS during recording
    QStringList usedSlugs;

    QThread* workerThread = nullptr;
    DrainWorker* worker = nullptr;

    void teardownWorker() {
        if (workerThread) {
            workerThread->quit();
            workerThread->wait(3000);
        }
        delete worker; worker = nullptr;          // the destructor releases the encoders
        delete workerThread; workerThread = nullptr;
    }

    void stopReopenWatch() {
        if (reopenTimer) reopenTimer->stop();
        if (lossTimer) lossTimer->stop();
        pending.clear();
    }

    // Track metadata from a (started) engine device; the slug is unique within the meeting.
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

    // Whether index is the newest track of its device (a re-added device has a newer track with the
    // same name; late signals of the older, closed one must not touch the device's state).
    bool isCurrentTrack(int index) const {
        if (index < 0 || index >= static_cast<int>(tracks.size())) return false;
        const QString& name = tracks[static_cast<size_t>(index)].deviceName;
        for (size_t j = static_cast<size_t>(index) + 1; j < tracks.size(); ++j)
            if (tracks[j].deviceName == name) return false;
        return true;
    }

    // The pending re-open the device answers (same id, otherwise same name); -1 if none.
    int pendingFor(const AudioDeviceInfo& device) const {
        for (int k = 0; k < pending.size(); ++k)
            if (!device.id.isEmpty() && pending.at(k).device.id == device.id) return k;
        for (int k = 0; k < pending.size(); ++k)
            if (pending.at(k).device.name == device.name) return k;
        return -1;
    }
};

RecordingSession::RecordingSession(media::MediaToolPtr media, QString audioDir, QString title,
                                   QString userSpeakerName, int opusKbps, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>()) {
    impl_->media = std::move(media);
    impl_->audioDir = std::move(audioDir);
    impl_->title = std::move(title);
    impl_->userSpeakerName = std::move(userSpeakerName);
    impl_->opusKbps = opusKbps;
}

RecordingSession::~RecordingSession() {
    impl_->stopReopenWatch();
    if (impl_->state != RecordingState::Idle && impl_->worker) {
        // An aborted / not yet finalized recording: the track files are closed cleanly here too.
        QMetaObject::invokeMethod(impl_->worker, "finalize", Qt::BlockingQueuedConnection);
        if (impl_->engine) impl_->engine->stop();
        impl_->teardownWorker();
    }
}

RecordingState RecordingSession::state() const { return impl_->state; }

void RecordingSession::setEngineFactory(EngineFactory factory) { impl_->engineFactory = std::move(factory); }
void RecordingSession::setFillCaptureGaps(bool on) { impl_->fillGaps = on; }
void RecordingSession::setStallWarnMs(qint64 ms) { impl_->stallWarnMs = ms; }
void RecordingSession::setClockSources(std::function<qint64()> monotonicMs, std::function<qint64()> wallMs)
{
    impl_->clock->monotonic = std::move(monotonicMs);
    impl_->clock->wall = std::move(wallMs);
}
void RecordingSession::setDeviceLister(DeviceLister lister) { impl_->lister = std::move(lister); }
void RecordingSession::setReopenTiming(qint64 pollMs, qint64 windowMs)
{
    impl_->reopenPollMs = qMax<qint64>(1, pollMs);
    impl_->reopenWindowMs = qMax<qint64>(0, windowMs);
    if (impl_->reopenTimer) impl_->reopenTimer->setInterval(int(impl_->reopenPollMs));
}
QMap<QString, qint64> RecordingSession::stalledTracks() const { return impl_->stalled; }
QVector<RecordingGap> RecordingSession::gaps() const { return impl_->gaps; }
QStringList RecordingSession::reopeningDevices() const
{
    QStringList out;
    for (const Impl::PendingReopen& p : impl_->pending) out << p.device.name;
    return out;
}
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

    // 1) Meeting folder.
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
    impl_->gaps.clear();
    impl_->pending.clear();

    // 2) AudioEngine. The clock starts before capture: the initial tracks' files run from this.
    impl_->engine = impl_->engineFactory ? impl_->engineFactory() : std::make_unique<AudioEngine>();
    impl_->clock->start();
    if (!impl_->engine || !impl_->engine->start(devices)) {
        emit failed(tr("Az audio motor nem indult el (nincs elérhető eszköz/backend)."));
        impl_->engine.reset();
        return;
    }

    // 3) Track metadata (WITHOUT the encoder; the worker creates that on its own thread).
    const int n = impl_->engine->count();
    impl_->usedSlugs.clear();
    for (int i = 0; i < n; ++i)
        impl_->tracks.push_back(impl_->makeTrack(i));

    // 4) Worker thread: it creates + starts the encoders (on its own thread).
    impl_->trackPeak = QVector<float>(static_cast<int>(impl_->tracks.size()), 0.0f);
    impl_->workerThread = new QThread(this);
    impl_->worker = new DrainWorker(impl_->media, impl_->engine.get(), impl_->folder, impl_->opusKbps,
                                    impl_->clock, impl_->fillGaps, impl_->stallWarnMs,
                                    /*splitOnSleep*/ bool(impl_->lister));
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
    connect(impl_->worker, &DrainWorker::stalled, this, [this](int idx, qint64 sinceMs) {
        if (!impl_->isCurrentTrack(idx)) return;
        const QString id = impl_->tracks[static_cast<size_t>(idx)].deviceName;
        if (impl_->stalled.contains(id)) return;
        impl_->stalled.insert(id, sinceMs);
        emit trackStalled(id, sinceMs);
    }, Qt::QueuedConnection);
    connect(impl_->worker, &DrainWorker::resumed, this, [this](int idx) {
        if (!impl_->isCurrentTrack(idx)) return;
        const QString id = impl_->tracks[static_cast<size_t>(idx)].deviceName;
        if (impl_->stalled.remove(id) > 0) emit trackResumed(id);
    }, Qt::QueuedConnection);
    connect(impl_->worker, &DrainWorker::slept, this, &RecordingSession::onSlept, Qt::QueuedConnection);
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

    // Re-open after loss: the poll (started on the first loss) and the watch for devices the
    // backend stopped (WASAPI device invalidated, a killed PulseAudio/PipeWire stream).
    if (impl_->lister) {
        if (!impl_->reopenTimer) {
            impl_->reopenTimer = new QTimer(this);
            connect(impl_->reopenTimer, &QTimer::timeout, this, &RecordingSession::pollReopen);
        }
        impl_->reopenTimer->setInterval(int(impl_->reopenPollMs));
        if (!impl_->lossTimer) {
            impl_->lossTimer = new QTimer(this);
            impl_->lossTimer->setInterval(500);
            connect(impl_->lossTimer, &QTimer::timeout, this, &RecordingSession::checkLostDevices);
        }
        impl_->lossTimer->start();
    }

    impl_->state = RecordingState::Recording;
    emit stateChanged(impl_->state);
}

int RecordingSession::openTrack(const AudioDeviceInfo& device, const QString& continuesTrackId) {
    // The track sounds from the moment it opens: this is its offset relative to the recording's zero
    // point. The recording clock includes the sleeps, so after a wake-up this is the wall-clock place.
    const qint64 offsetMs = impl_->clock->elapsed();
    const int engineIndex = impl_->engine->addDevice(device);
    if (engineIndex < 0) return -1;

    TrackMeta meta = impl_->makeTrack(engineIndex);
    meta.startOffsetMs = offsetMs;
    meta.continuesTrackId = continuesTrackId;
    bool ok = false;
    DrainWorker* w = impl_->worker;
    QMetaObject::invokeMethod(w, [w, meta] { return w->addEncoder(meta); },
                              Qt::BlockingQueuedConnection, &ok);
    if (!ok) {
        impl_->engine->closeDevice(engineIndex);
        impl_->usedSlugs.removeAll(meta.slug);
        return -1;
    }
    impl_->tracks.push_back(meta);
    impl_->trackPeak.push_back(0.0f);
    const int index = static_cast<int>(impl_->tracks.size()) - 1;
    emit trackAdded(index, meta.deviceName);
    // A re-added (re-plugged) device: its new track delivers again, the old one's stall is over.
    if (impl_->stalled.remove(meta.deviceName) > 0) emit trackResumed(meta.deviceName);
    return index;
}

bool RecordingSession::addDevice(const AudioDeviceInfo& device) {
    if (impl_->state != RecordingState::Recording || !impl_->engine || !impl_->worker)
        return false;
    for (const TrackMeta& t : impl_->tracks)
        if (!t.closed && t.deviceName == device.name) return false;   // already on a track
    // A device waiting to be re-opened that the user (or the hot-plug) switches on: the same as the
    // re-open succeeding (the segment chain and the gap are kept).
    const int k = impl_->pendingFor(device);
    if (k >= 0) return resolveReopen(k, device);
    return openTrack(device, QString()) >= 0;
}

void RecordingSession::closeTrack(const QString& deviceName, bool detached) {
    if (impl_->state != RecordingState::Recording || !impl_->engine || !impl_->worker)
        return;
    for (size_t i = 0; i < impl_->tracks.size(); ++i) {
        const TrackMeta& t = impl_->tracks[i];
        if (t.closed || t.deviceName != deviceName) continue;
        if (detached)
            beginReopen(static_cast<int>(i), QString::fromLatin1(tracktiming::kGapDeviceLost),
                        impl_->clock->elapsed(), impl_->clock->wallNow());
        closeAt(static_cast<int>(i), detached);
    }
}

void RecordingSession::closeAt(int index, bool detached) {
    TrackMeta& t = impl_->tracks[static_cast<size_t>(index)];
    if (t.closed) return;
    t.closed = true;
    // Capture stops first (no more data comes), then the worker writes out the remainder and closes
    // the file, on its own thread; the main thread does not wait for it.
    impl_->engine->closeDevice(t.engineIndex);
    DrainWorker* w = impl_->worker;
    QMetaObject::invokeMethod(w, [w, index, detached] { w->closeEncoder(index, detached); },
                              Qt::QueuedConnection);
    emit trackClosed(index, t.deviceName);
}

// A capture track is lost: from now on its device is looked for every reopenPollMs (pollReopen).
void RecordingSession::beginReopen(int index, const QString& reason, qint64 gapFromMs, qint64 gapFromWallMs) {
    if (!impl_->lister) return;
    const TrackMeta& t = impl_->tracks[static_cast<size_t>(index)];
    if (impl_->pendingFor(t.device) >= 0) return;
    Impl::PendingReopen p;
    p.device = t.device;
    p.reason = reason;
    p.prevTrackId = t.slug;
    p.sinceMs = impl_->clock->elapsed();
    p.gapFromMs = gapFromMs;
    p.gapFromWallMs = gapFromWallMs;
    impl_->pending.push_back(p);
    qCInfo(lcAudio).noquote() << QStringLiteral("Recording: track \"%1\" lost (%2), re-opening the device every %3 ms for up to %4 s")
                                     .arg(t.deviceName, reason).arg(impl_->reopenPollMs).arg(impl_->reopenWindowMs / 1000.0);
    if (impl_->reopenTimer && !impl_->reopenTimer->isActive()) impl_->reopenTimer->start();
    emit reopenStarted(t.deviceName, reason);
}

// The device of pending[k] is open again (as `device`): its new segment continues the lost one.
bool RecordingSession::resolveReopen(int k, const AudioDeviceInfo& device) {
    const Impl::PendingReopen p = impl_->pending.at(k);
    const int index = openTrack(device, p.prevTrackId);
    if (index < 0) return false;
    impl_->pending.removeAt(k);
    const TrackMeta& seg = impl_->tracks[static_cast<size_t>(index)];
    if (p.reason == QLatin1String(tracktiming::kGapDeviceLost)) {
        // A sleep's gap is recorded once for the whole meeting (onSlept); a lost device's here.
        RecordingGap g;
        g.fromMs = p.gapFromMs;
        g.toMs = seg.startOffsetMs;
        g.reason = p.reason;
        g.fromWall = QDateTime::fromMSecsSinceEpoch(p.gapFromWallMs);
        g.toWall = QDateTime::fromMSecsSinceEpoch(impl_->clock->wallNow());
        g.trackId = seg.slug;
        impl_->gaps.push_back(g);
    }
    // Came back under another name (matched by id): the old name's stall is over too.
    if (p.device.name != device.name && impl_->stalled.remove(p.device.name) > 0)
        emit trackResumed(p.device.name);
    qCInfo(lcAudio).noquote() << QStringLiteral("Recording: \"%1\" re-opened, continuing in a new segment %2 at %3 ms")
                                     .arg(device.name, seg.fileName).arg(seg.startOffsetMs);
    if (impl_->pending.isEmpty() && impl_->reopenTimer) impl_->reopenTimer->stop();
    emit trackReopened(p.device.name, device.name, index);
    return true;
}

void RecordingSession::pollReopen() {
    if (impl_->state != RecordingState::Recording || !impl_->lister) {
        impl_->stopReopenWatch();
        return;
    }
    if (impl_->pending.isEmpty()) { impl_->reopenTimer->stop(); return; }
    // The lister re-enumerates (the owner may close another lost track meanwhile: that only appends).
    const QVector<AudioDeviceInfo> present = impl_->lister();
    const qint64 now = impl_->clock->elapsed();
    int k = 0;
    while (k < impl_->pending.size()) {
        const Impl::PendingReopen& p = impl_->pending.at(k);
        const AudioDeviceInfo* match = nullptr;
        for (const AudioDeviceInfo& d : present)
            if (!p.device.id.isEmpty() && d.id == p.device.id) { match = &d; break; }
        if (!match)
            for (const AudioDeviceInfo& d : present)
                if (d.name == p.device.name) { match = &d; break; }
        int openIndex = -1;   // already back on a track (opened by some other path)
        for (size_t j = 0; j < impl_->tracks.size(); ++j)
            if (!impl_->tracks[j].closed && match && impl_->tracks[j].deviceName == match->name)
                openIndex = static_cast<int>(j);
        if (openIndex >= 0) {
            const QString name = p.device.name, newName = match->name;
            impl_->pending.removeAt(k);
            emit trackReopened(name, newName, openIndex);
            continue;
        }
        if (match) {
            const AudioDeviceInfo dev = *match;
            if (resolveReopen(k, dev)) continue;   // removed from pending: k stays
        }
        if (now - p.sinceMs >= impl_->reopenWindowMs) {
            const QString name = p.device.name, reason = p.reason;
            impl_->pending.removeAt(k);
            qCWarning(lcAudio).noquote() << QStringLiteral("Recording: \"%1\" did not come back within %2 s, its track stays closed")
                                                .arg(name).arg(impl_->reopenWindowMs / 1000.0);
            emit reopenAbandoned(name, reason);
            continue;
        }
        ++k;
    }
    if (impl_->pending.isEmpty()) impl_->reopenTimer->stop();
}

// A device whose capture the backend stopped and that delivers no data is lost: closed as detached
// and re-opened. (A WASAPI loopback delivers nothing while idle: there the stop alone decides.)
void RecordingSession::checkLostDevices() {
    if (impl_->state != RecordingState::Recording || !impl_->engine) return;
    for (size_t i = 0; i < impl_->tracks.size(); ++i) {
        const TrackMeta& t = impl_->tracks[i];
        if (t.closed || !impl_->isCurrentTrack(static_cast<int>(i))) continue;
        if (!impl_->engine->deviceLost(t.engineIndex)) continue;
        const bool idleSilent = systemAudioSource().deliversNothingWhenIdle(t.kind);
        if (!idleSilent && !impl_->stalled.contains(t.deviceName)) continue;
        qCWarning(lcAudio).noquote() << QStringLiteral("Recording: the backend stopped the capture of \"%1\"").arg(t.deviceName);
        closeTrack(t.deviceName, /*detached*/ true);
        return;   // closeTrack may have changed the list; the next check comes soon
    }
}

// The drain worker saw the wall clock jump ahead: the machine slept.
void RecordingSession::onSlept(qint64 fromWallMs, qint64 toWallMs, qint64 fromMs, qint64 toMs) {
    if (impl_->state != RecordingState::Recording) return;
    RecordingGap g;
    g.fromMs = fromMs;
    g.toMs = toMs;
    g.reason = QString::fromLatin1(tracktiming::kGapSleep);
    g.fromWall = QDateTime::fromMSecsSinceEpoch(fromWallMs);
    g.toWall = QDateTime::fromMSecsSinceEpoch(toWallMs);
    impl_->gaps.push_back(g);
    qCWarning(lcAudio).noquote()
        << QStringLiteral("Recording: sleep detected, the wall clock jumped %1 s ahead (%2 -> %3); recording clock %4 -> %5 ms")
               .arg((toMs - fromMs) / 1000)
               .arg(g.fromWall.toString(Qt::ISODate), g.toWall.toString(Qt::ISODate))
               .arg(fromMs).arg(toMs);
    emit systemSlept(fromWallMs, toWallMs);
    if (!impl_->lister) return;
    // Every open track is cut at the sleep's start and continues in a new segment once its device
    // opens again (the stream from before the sleep is stale on most backends).
    for (size_t i = 0; i < impl_->tracks.size(); ++i) {
        if (impl_->tracks[i].closed) continue;
        beginReopen(static_cast<int>(i), QString::fromLatin1(tracktiming::kGapSleep), fromMs, fromWallMs);
        closeAt(static_cast<int>(i), /*detached*/ false);
    }
}

void RecordingSession::stop() {
    if (impl_->state != RecordingState::Recording) {
        emit failed(tr("Nincs futó felvétel a leállításhoz."));
        return;
    }
    if (!impl_->pending.isEmpty())
        qCInfo(lcAudio).noquote() << QStringLiteral("Recording: stopped while re-opening %1; giving up the re-open")
                                         .arg(reopeningDevices().join(QStringLiteral(", ")));
    impl_->stopReopenWatch();

    impl_->state = RecordingState::Stopping;
    emit stateChanged(impl_->state);
    impl_->durationMs = impl_->clock->elapsed();

    // Encoding: the worker's last drain + closing/waiting for the ffmpegs, on the worker thread; the main
    // thread does NOT wait (the UI lives). We stop capture only AFTER the shutdown (onFinalized), because
    // the worker still reads the engine's ring buffers.
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
        qWarning().noquote() << "RecordingSession: track file(s) closed with errors:"
                             << failedFiles.join(QStringLiteral(", "));

    // Every track stays active: a silent track is removed from the mixdown by the speech check AFTER
    // recording (Track::speechRatio / excludedReason, editing/session/TrackSpeech.h; AppController runs it before the
    // mixdown). We keep the peak level for information.
    const int nTr = static_cast<int>(impl_->tracks.size());
    auto peakOf = [&](int i) { return (i < impl_->trackPeak.size()) ? impl_->trackPeak[i] : 0.0f; };

    // The mixdown is INTENTIONALLY not made here. It used to run on the main thread with a synchronous
    // `QProcess::waitForFinished()`, which FROZE the UI for 15-20 s on a 1.5-hour meeting. The mixdown is
    // needed ONLY for listening (the STT works from the raw per-track .ogg files), so it was decoupled:
    // `mixdownFile` stays empty, and AppController makes the mix later, asynchronously (immediately in auto
    // mode, from the review panel's button in manual mode); see AppController::regenerateMixdown().

    // Assembling the meeting.
    Meeting m;
    m.id = impl_->id;
    m.title = impl_->title;
    m.folder = impl_->folder;
    m.startedAt = impl_->startedAt;
    m.durationMs = durationMs;
    m.mixdownFile = QString();   // no mixdown yet: AppController makes it
    m.gaps = impl_->gaps;

    // IMPORTANT: we do NOT name the user by loudness/position: speaker identification is the job of
    // VOICE ID (fingerprint) (autoIdentifyMeeting). Here the mic tracks get a neutral "Microphone N" label;
    // the real names come from the voiceprint DB (or the user's one-time manual rename, which trains it).
    // A re-opened segment keeps the label of the segment it continues (one logical track).
    int micNo = 1;
    QHash<QString, QString> labelOf;   // slug -> speaker label
    for (int i = 0; i < nTr; ++i) {
        const auto& t = impl_->tracks[i];
        Track tr;
        tr.id = t.slug;
        tr.deviceName = t.deviceName;
        tr.file = t.fileName;
        tr.kind = t.kind;
        if (!t.continuesTrackId.isEmpty() && labelOf.contains(t.continuesTrackId))
            tr.speakerLabel = labelOf.value(t.continuesTrackId);
        else if (t.kind == TrackKind::Mic)
            tr.speakerLabel = QStringLiteral("Mikrofon ") + QString::number(micNo++);
        else
            tr.speakerLabel = QStringLiteral("Rendszer");
        labelOf.insert(t.slug, tr.speakerLabel);
        tr.fixedSpeaker = true;
        tr.sampleRate = 48000;
        tr.channels = t.channels;
        tr.peakLevel = peakOf(i);
        tr.active = true;
        tr.startOffsetMs = t.startOffsetMs;
        tr.continuesTrackId = t.continuesTrackId;
        m.tracks.push_back(tr);
    }

    // Whatever is surely not on disk (the encoder never made it) must not become a track.
    const QString folder = impl_->folder;
    m.tracks.erase(std::remove_if(m.tracks.begin(), m.tracks.end(), [&folder](const Track& t) {
        return !QFileInfo::exists(QDir(folder).absoluteFilePath(t.file));
    }), m.tracks.end());

    impl_->tracks.clear();
    impl_->stalled.clear();
    impl_->engine.reset();
    impl_->state = RecordingState::Idle;
    emit stateChanged(impl_->state);
    emit finished(m);
}

} // namespace tanara::recording
#include "RecordingSession.moc"

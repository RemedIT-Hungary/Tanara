#include "tanara/processing/diarization/LocalDiarizer.h"

#include "tanara/foundation/log/Logging.h"
#include "tanara/processing/diarization/SpeakerClustering.h"
#include "tanara/processing/voiceid/EmbeddingSet.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

namespace tanara::processing {

using namespace providers;

namespace {

struct WorkState {
    std::atomic<bool> cancelled{false};
    media::CancelToken decodeCancel;
    DiarizeResult out;
    Error error;
};

Error makeError(Error::Kind kind, const QString& code, const QString& message)
{
    Error e;
    e.kind = kind;
    e.code = code;
    e.message = message;
    return e;
}

QVector<float> decodeTrack(const media::MediaTool& media, const QString& path, const media::CancelToken& cancel,
                           QString* error)
{
    media::DecodeSpec spec;
    spec.path = path;
    spec.format = media::PcmFormat{16000, 1, media::SampleFormat::F32};
    QVector<float> pcm;
    const media::MediaResult r = media.decodePcm(spec, [&pcm](const char* data, qsizetype bytes) {
        const qsizetype n = bytes / qsizetype(sizeof(float));
        const qsizetype at = pcm.size();
        pcm.resize(at + n);
        std::memcpy(pcm.data() + at, data, size_t(n) * sizeof(float));
    }, cancel);
    // Lenient like the voice code: any output counts (a non-zero exit after a full decode is not fatal).
    if (!r.started || r.cancelled || (pcm.isEmpty() && !r.ok())) {
        if (error) *error = r.started ? r.errorText() : QStringLiteral("the media tool could not be started");
        return {};
    }
    return pcm;
}

class LocalDiarizeJob final : public DiarizeJob {
public:
    LocalDiarizeJob(LocalDiarizer::SegmenterLoader loader, media::MediaToolPtr media, DiarizeRequest req,
                    SegmenterOptions opts, ClusteringOptions copts, VoiceEmbedderSet embedders, QObject* parent)
        : DiarizeJob(parent), m_state(std::make_shared<WorkState>())
    {
        auto state = m_state;
        LocalDiarizeJob* job = this;   // alive until the worker is reaped (see the destructor)
        m_thread = QThread::create([state, job, loader = std::move(loader), media = std::move(media),
                                    req = std::move(req), opts, copts, proto = std::move(embedders)]() {
            QElapsedTimer clock;
            clock.start();
            QString err;
            std::unique_ptr<PyannoteSegmenter> seg = loader ? loader(req.segmentationModelId, opts, &err) : nullptr;
            if (!seg) {
                state->error = makeError(Error::Kind::Unsupported, QStringLiteral("no_segmentation_model"), err);
                return;
            }
            // The clustering models: the job's own copy of the set (not thread-safe), loaded here.
            bool needEmbeddings = false;
            for (const DiarizeTrack& t : req.tracks) needEmbeddings = needEmbeddings || (t.hasInput() && t.knownPerson.isNull());
            const bool cluster = needEmbeddings && !proto.isEmpty();
            VoiceEmbedderSet embedders = LocalDiarizer::withThreads(proto, copts.embedThreads);
            QStringList modelIds;
            if (cluster) {
                if (!embedders.ensureLoaded()) {
                    state->error = makeError(Error::Kind::Unsupported, QStringLiteral("no_embedding_model"),
                                             embedders.lastError());
                    return;
                }
                const QStringList loaded = embedders.loadedModelIds();
                for (const QString& id : req.embeddingModelIds)
                    if (loaded.contains(id)) modelIds << id;
                if (modelIds.isEmpty()) modelIds = loaded;
                modelIds.sort();
            }
            // Decode every track first, so the progress can count the windows of all of them.
            QVector<int> idx;
            QVector<QVector<float>> pcms;
            for (int i = 0; i < req.tracks.size() && !state->cancelled; ++i) {
                const DiarizeTrack& t = req.tracks[i];
                if (!t.hasInput()) continue;
                QVector<float> pcm = t.pcm;
                if (!t.path.isEmpty()) {
                    if (!media) {
                        state->error = makeError(Error::Kind::Unsupported, QStringLiteral("no_media_tool"), {});
                        return;
                    }
                    pcm = decodeTrack(*media, t.path, state->decodeCancel, &err);
                    if (pcm.isEmpty() && !state->cancelled) {
                        state->error = makeError(Error::Kind::Input, QStringLiteral("decode_failed"),
                                                 QStringLiteral("%1: %2").arg(t.path, err));
                        return;
                    }
                }
                idx.append(i);
                pcms.append(std::move(pcm));
            }
            // Work units: per track its windows for the segmentation + the same again for the embeddings.
            qint64 total = 0;
            for (const QVector<float>& p : pcms) total += 2 * qint64(seg->windowCount(p.size()));
            auto report = [job, total](qint64 done) {
                QMetaObject::invokeMethod(job, [job, done, total]() { if (!job->isCancelled()) emit job->progress(done, total); },
                                          Qt::QueuedConnection);
            };
            // 1. Segment every track (the leakage rule needs all of them).
            QVector<SegmentationResult> segs;
            qint64 doneBefore = 0;
            for (int k = 0; k < idx.size() && !state->cancelled; ++k) {
                const auto r = seg->segment(pcms[k], [report, doneBefore](int done, int) { report(doneBefore + done); },
                                            &state->cancelled, &err);
                if (!r) {
                    if (state->cancelled) return;
                    state->error = makeError(Error::Kind::Service, QStringLiteral("segmentation_failed"), err);
                    return;
                }
                doneBefore += seg->windowCount(pcms[k].size());
                segs.append(*r);
            }
            if (state->cancelled) return;
            // 2. Cross-track leakage: the leaked speech turns of every track.
            QVector<QVector<leakage::Turn>> suppress(idx.size());
            if (copts.suppressLeakage && idx.size() >= 2) {
                QVector<DiarizeTrack> ts;
                QVector<const SegmentationResult*> sp;
                QVector<const QVector<float>*> pp;
                for (int k = 0; k < idx.size(); ++k) {
                    ts.append(req.tracks[idx[k]]);
                    sp.append(&segs[k]);
                    pp.append(&pcms[k]);
                }
                const auto leaks = LocalDiarizer::detectLeakage(ts, sp, pp, copts.leakage);
                for (int k = 0; k < idx.size(); ++k) {
                    double sec = 0;
                    for (const leakage::Leak& l : leaks[k]) {
                        suppress[k].append(l.span);
                        sec += l.span.end - l.span.start;
                    }
                    if (!leaks[k].isEmpty())
                        qCInfo(lcVoice).noquote()
                            << QStringLiteral("Diarization: track %1: %2 leaked turn(s) (%3 s) dropped (from %4)")
                                   .arg(ts[k].trackId).arg(leaks[k].size()).arg(sec, 0, 'f', 1)
                                   .arg(leaks[k].first().sourceTrackId);
                }
            }
            // 3. Per track: embed + cluster (or the unclustered / hand-bound result).
            QVector<VoiceEmbedderSet> workerSets;
            QVector<LocalDiarizer::SpanEmbedder> embedFns;
            if (cluster) {
                const int workers = std::max(1, copts.embedWorkers);
                workerSets.reserve(workers);
                workerSets.append(std::move(embedders));
                for (int w = 1; w < workers; ++w) workerSets.append(LocalDiarizer::withThreads(proto, copts.embedThreads));
                for (int w = 0; w < workers; ++w) {
                    const VoiceEmbedderSet* set = &workerSets[w];
                    embedFns.append([set, &modelIds](const QVector<float>& pcm) { return set->embedPcmWith(pcm, modelIds); });
                }
            }
            for (int k = 0; k < idx.size() && !state->cancelled; ++k) {
                const DiarizeTrack& t = req.tracks[idx[k]];
                const qint64 windows = seg->windowCount(pcms[k].size());
                if (!t.knownPerson.isNull() || !cluster) {
                    LocalDiarizer::appendTrackResult(t, LocalDiarizer::withoutTurns(segs[k], suppress[k]), opts, &state->out);
                } else {
                    qint64 lastSent = -1;
                    const TrackSpans spans = LocalDiarizer::embedTrack(
                        segs[k], pcms[k], copts, suppress[k], embedFns,
                        [&](int done, int n) {
                            const qint64 at = doneBefore + (n > 0 ? windows * done / n : windows);
                            if (at != lastSent) { lastSent = at; report(at); }
                            return !state->cancelled.load();
                        });
                    if (spans.cancelled || state->cancelled) return;
                    const TrackClusterStats st = LocalDiarizer::clusterSpans(t, segs[k], spans, opts, copts, modelIds, &state->out);
                    qCInfo(lcVoice).noquote()
                        << QStringLiteral("Diarization: track %1: %2 spans (%3 merged), %4 embedded%5, threshold %6, %7 clusters, %8 speakers")
                               .arg(t.trackId).arg(st.spans).arg(st.merged).arg(st.embedded)
                               .arg(st.subsampled ? QStringLiteral(" (subsampled)") : QString())
                               .arg(st.threshold).arg(st.clusters).arg(st.speakers);
                }
                doneBefore += windows;
                report(doneBefore);
                pcms[k] = {};   // free the PCM early
                segs[k] = {};
            }
            if (state->cancelled) return;
            state->out.segmentationModelId = seg->modelId();
            if (cluster) state->out.embeddingModelIds = modelIds;
            std::sort(state->out.turns.begin(), state->out.turns.end(), [](const SpeakerTurn& a, const SpeakerTurn& b) {
                if (a.startMs != b.startMs) return a.startMs < b.startMs;
                if (a.trackId != b.trackId) return a.trackId < b.trackId;
                return a.speakerIndex < b.speakerIndex;
            });
            std::sort(state->out.speakers.begin(), state->out.speakers.end(),
                      [](const DiarizedSpeaker& a, const DiarizedSpeaker& b) {
                          return a.trackId != b.trackId ? a.trackId < b.trackId : a.index < b.index;
                      });
            qCInfo(lcVoice).noquote() << QStringLiteral("Diarization done in %1 ms: %2 turns, %3 speakers")
                                             .arg(clock.elapsed()).arg(state->out.turns.size())
                                             .arg(state->out.speakers.size());
        });
        QPointer<LocalDiarizeJob> self(this);
        connect(m_thread, &QThread::finished, this, [self]() {
            if (!self || self->isCancelled()) return;
            if (self->m_state->error.isError()) self->fail(self->m_state->error);
            else self->complete(self->m_state->out);
        });
        m_thread->start();
    }

    ~LocalDiarizeJob() override
    {
        stop();
        if (m_thread) {
            m_thread->disconnect(this);
            m_thread->wait();
            delete m_thread;
        }
    }

private:
    void stop()
    {
        m_state->cancelled = true;
        m_state->decodeCancel.cancel();
    }
    void onCancel() override { stop(); }

    std::shared_ptr<WorkState> m_state;
    QThread* m_thread = nullptr;
};

} // namespace

LocalDiarizer::LocalDiarizer(SegmenterLoader loader, media::MediaToolPtr media, QObject* parent)
    : QObject(parent), m_loader(std::move(loader)), m_media(std::move(media))
{
    m_base.windowShiftRatio = kDefaultWindowShift;
}

LocalDiarizer::SegmenterLoader LocalDiarizer::catalogLoader(const QString& modelsDir, const QString& appDir,
                                                            OnnxSegmentationOptions ort)
{
    return [modelsDir, appDir, ort](const QString& modelId, const SegmenterOptions& opts,
                                    QString* error) -> std::unique_ptr<PyannoteSegmenter> {
        const QString id = modelId.isEmpty() ? VoiceModelRegistry::defaultSegmentationModelId() : modelId;
        const std::optional<VoiceModelSpec> spec = VoiceModelRegistry::spec(id);
        if (!spec || spec->kind != VoiceModelKind::Segmentation) {
            if (error) *error = QStringLiteral("Unknown segmentation model: %1").arg(id);
            return nullptr;
        }
        return PyannoteSegmenter::open(*spec, modelsDir, appDir, opts, ort, error);
    };
}

bool LocalDiarizer::hasUsableModel(const QString& modelsDir, const QString& appDir)
{
    for (const VoiceModelSpec& s : VoiceModelRegistry::segmentation())
        if (PyannoteSegmenter::isUsable(s, modelsDir, appDir)) return true;
    return false;
}

SegmenterOptions LocalDiarizer::optionsFor(const DiarizeRequest& req, SegmenterOptions base)
{
    if (req.minDurationOn >= 0) base.minDurationOn = req.minDurationOn;
    if (req.minDurationOff >= 0) base.minDurationOff = req.minDurationOff;
    return base;
}

void LocalDiarizer::appendTrackResult(const DiarizeTrack& track, const SegmentationResult& seg,
                                      const SegmenterOptions& opts, DiarizeResult* out)
{
    auto ms = [&](double sec) { return track.startOffsetMs + qint64(std::llround(sec * 1000.0)); };
    if (!track.knownPerson.isNull()) {
        // One speaker: the union of all speech, post-processed again (the merge can close small gaps).
        QVector<segmentation::Span> spans;
        for (const segmentation::Turn& t : seg.turns) spans.append({t.start, t.end});
        std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        QVector<segmentation::Span> merged;
        for (const segmentation::Span& s : spans) {
            if (!merged.isEmpty() && s.start <= merged.last().end) merged.last().end = std::max(merged.last().end, s.end);
            else merged.append(s);
        }
        merged = segmentation::applyMinDuration(merged, opts.minDurationOn, opts.minDurationOff);
        DiarizedSpeaker sp;
        sp.trackId = track.trackId;
        sp.index = 0;
        sp.person = track.knownPerson;
        sp.centroid.trackId = track.trackId;
        for (const segmentation::Span& s : merged) {
            SpeakerTurn t;
            t.trackId = track.trackId;
            t.startMs = ms(s.start);
            t.endMs = ms(s.end);
            t.speakerIndex = 0;
            sp.totalMs += t.durationMs();
            out->turns.append(t);
        }
        if (!merged.isEmpty()) out->speakers.append(sp);
        return;
    }
    // Stitched ids -> dense indices by first appearance (seg.turns is sorted by start).
    QHash<int, int> index;
    QVector<DiarizedSpeaker> speakers;
    for (const segmentation::Turn& st : seg.turns) {
        if (!index.contains(st.speaker)) {
            index.insert(st.speaker, int(speakers.size()));
            DiarizedSpeaker sp;
            sp.trackId = track.trackId;
            sp.index = int(speakers.size());
            sp.centroid.trackId = track.trackId;   // D4 fills the vectors
            speakers.append(sp);
        }
        SpeakerTurn t;
        t.trackId = track.trackId;
        t.startMs = ms(st.start);
        t.endMs = ms(st.end);
        t.speakerIndex = index.value(st.speaker);
        t.overlap = st.overlap;
        speakers[t.speakerIndex].totalMs += t.durationMs();
        out->turns.append(t);
    }
    out->speakers += speakers;
}

ClusteringOptions LocalDiarizer::clusteringOptionsFor(const DiarizeRequest& req, ClusteringOptions base)
{
    if (req.numClusters > 0) base.numClusters = req.numClusters;
    if (req.threshold >= 0) base.threshold = req.threshold;
    return base;
}

namespace {

// Merges sorted-or-not [first, second) frame runs.
QVector<QPair<int, int>> unionRuns(QVector<QPair<int, int>> runs)
{
    std::sort(runs.begin(), runs.end());
    QVector<QPair<int, int>> out;
    for (const auto& r : std::as_const(runs)) {
        if (!out.isEmpty() && r.first <= out.last().second) out.last().second = std::max(out.last().second, r.second);
        else out.append(r);
    }
    return out;
}

int runFrames(const QVector<QPair<int, int>>& runs)
{
    int n = 0;
    for (const auto& r : runs) n += r.second - r.first;
    return n;
}

struct ThresholdEntry {
    QStringList models;              // sorted
    double threshold;
};

const QVector<ThresholdEntry>& thresholdTable()
{
    static const QVector<ThresholdEntry> t = {
        {{QStringLiteral("wespeaker-resnet34-lm")}, kWeSpeakerThreshold},
        {{QStringLiteral("campplus")}, kCamPlusThreshold},
        {{QStringLiteral("campplus"), QStringLiteral("wespeaker-resnet34-lm")}, kFusedThreshold},
    };
    return t;
}

} // namespace

VoiceEmbedderSet LocalDiarizer::withThreads(const VoiceEmbedderSet& set, int intraOpThreads)
{
    QVector<VoiceModelEntry> models = set.models();
    for (VoiceModelEntry& m : models) m.spec.features.intraOpThreads = std::max(1, intraOpThreads);
    return VoiceEmbedderSet(models, set.loader());
}

double LocalDiarizer::defaultThreshold(const QStringList& modelIds)
{
    QStringList ids = modelIds;
    ids.removeDuplicates();
    ids.sort();
    for (const ThresholdEntry& e : thresholdTable())
        if (e.models == ids) return e.threshold;
    double sum = 0;
    int n = 0;
    for (const QString& id : std::as_const(ids))
        for (const ThresholdEntry& e : thresholdTable())
            if (e.models.size() == 1 && e.models.first() == id) { sum += e.threshold; ++n; }
    return n > 0 ? sum / n : kFallbackThreshold;
}

TrackSpans LocalDiarizer::embedTrack(const SegmentationResult& seg, const QVector<float>& pcm,
                                     const ClusteringOptions& copts, const QVector<leakage::Turn>& suppress,
                                     const QVector<SpanEmbedder>& embedders,
                                     const std::function<bool(int, int)>& progress)
{
    using namespace segmentation;
    TrackSpans out;
    const ModelInfo& mi = seg.info;
    const double frameSec = mi.frameStepSec();
    const int minFrames = std::max(copts.minRunFrames,
                                   int(std::ceil(copts.minEmbedSec / std::max(1e-6, frameSec))));

    // The leakage mask: global frames whose centre lies in a suppressed turn.
    QVector<char> masked;
    if (!suppress.isEmpty()) {
        masked.fill(0, seg.numFrames);
        for (const leakage::Turn& t : suppress)
            for (int g = 0; g < seg.numFrames; ++g) {
                const double c = mi.frameTimeSec(g);
                if (c >= t.start && c < t.end) masked[g] = 1;
            }
    }

    // 1. The (window, local speaker) spans, time order (windows, then local speakers).
    QVector<TrackSpans::Span> pairs;
    out.acts.resize(seg.windows.size());
    for (int w = 0; w < seg.windows.size(); ++w) {
        out.acts[w] = seg.windowActivity(w);
        Activity& a = out.acts[w];
        const int start0 = seg.windows[w].startFrame;
        if (!masked.isEmpty())
            for (int f = 0; f < a.frames; ++f) {
                const int g = start0 + f;
                if (g < masked.size() && masked[g])
                    for (int l = 0; l < a.speakers; ++l) a.at(f, l) = 0.0f;
            }
        const QVector<int>& st = seg.windows[w].stitched;
        for (int l = 0; l < a.speakers; ++l) {
            if (l >= st.size() || st[l] < 0) continue;
            TrackSpans::Span sp;
            sp.members.append({w, l});
            sp.stitched = st[l];
            int f = 0;
            while (f < a.frames) {
                if (!(a.at(f, l) > 0.5f && a.countAt(f) == 1)) { ++f; continue; }
                const int s = f;
                while (f < a.frames && a.at(f, l) > 0.5f && a.countAt(f) == 1) ++f;
                if (f - s >= copts.minRunFrames) sp.runs.append({start0 + s, start0 + f});
            }
            sp.cleanFrames = runFrames(sp.runs);
            pairs.append(sp);
        }
    }
    out.pairs = int(pairs.size());

    // 2. Span merging: one stitched speaker's spans in consecutive windows, while the union of their clean
    // speech fits mergeSpanSec.
    if (copts.mergeSpanSec > 0) {
        const int maxFrames = int(copts.mergeSpanSec / std::max(1e-6, frameSec));
        QHash<int, int> open;   // stitched id -> index in out.spans of its open group
        for (TrackSpans::Span& sp : pairs) {
            const int w = sp.members.first().first;
            auto it = open.find(sp.stitched);
            if (it != open.end()) {
                TrackSpans::Span& g = out.spans[it.value()];
                const int lastW = g.members.last().first;
                QVector<QPair<int, int>> u = unionRuns(g.runs + sp.runs);
                if (w - lastW <= 1 && runFrames(u) <= maxFrames) {
                    g.members += sp.members;
                    g.runs = u;
                    g.cleanFrames = runFrames(u);
                    continue;
                }
            }
            open.insert(sp.stitched, int(out.spans.size()));
            out.spans.append(std::move(sp));
        }
    } else {
        out.spans = std::move(pairs);
    }

    // 3. Which spans are embedded: the long-enough ones, subsampled above the cap.
    QVector<int> candidates, groups;
    for (int i = 0; i < out.spans.size(); ++i)
        if (out.spans[i].cleanFrames >= minFrames) {
            candidates.append(i);
            groups.append(out.spans[i].stitched);
        }
    const QVector<int> picked = clustering::subsample(int(candidates.size()), copts.maxEmbeddings, groups);
    out.subsampled = picked.size() < candidates.size();

    // The runs' samples (centred on their frames), concatenated.
    const int half = mi.receptiveFieldShift / 2;
    auto audioOf = [&](const TrackSpans::Span& sp) {
        QVector<float> audio;
        for (const auto& run : sp.runs) {
            const qint64 a = std::max<qint64>(0, qint64(run.first) * mi.receptiveFieldShift + mi.receptiveFieldSize / 2 - half);
            const qint64 b = std::min<qint64>(pcm.size(), qint64(run.second - 1) * mi.receptiveFieldShift
                                                              + mi.receptiveFieldSize / 2 + half);
            if (b > a) audio.append(pcm.mid(a, b - a));
        }
        return audio;
    };
    const int n = int(picked.size());
    const int workers = std::max(1, std::min<int>(int(embedders.size()), n));
    std::atomic<qint64> samples{0};
    if (embedders.isEmpty() || n == 0) {
        // nothing to embed
    } else if (workers == 1) {
        for (int k = 0; k < n; ++k) {
            if (progress && !progress(k, n)) { out.cancelled = true; return out; }
            TrackSpans::Span& sp = out.spans[candidates[picked[k]]];
            const QVector<float> audio = audioOf(sp);
            if (audio.isEmpty() || !embedders.first()) continue;
            samples += audio.size();
            sp.set = embedders.first()(audio);
        }
    } else {
        // A small pool: every worker takes the next span; the calling thread reports the progress and
        // turns a cancel into the stop flag (checked between two spans).
        std::atomic<int> next{0}, done{0};
        std::atomic<bool> stop{false};
        std::mutex m;
        std::condition_variable cv;
        std::vector<std::thread> pool;
        pool.reserve(size_t(workers));
        for (int wk = 0; wk < workers; ++wk)
            pool.emplace_back([&, wk]() {
                const SpanEmbedder& embed = embedders[wk];
                for (;;) {
                    if (stop.load()) break;
                    const int k = next.fetch_add(1);
                    if (k >= n) break;
                    TrackSpans::Span& sp = out.spans[candidates[picked[k]]];
                    const QVector<float> audio = audioOf(sp);
                    if (!audio.isEmpty() && embed) {
                        samples += audio.size();
                        sp.set = embed(audio);
                    }
                    done.fetch_add(1);
                    cv.notify_one();
                }
                cv.notify_one();
            });
        {
            std::unique_lock<std::mutex> lock(m);
            int reported = -1;
            for (;;) {
                const int d = done.load();
                if (d != reported) {
                    reported = d;
                    if (progress && !progress(d, n)) { stop = true; break; }
                }
                if (d >= n) break;
                cv.wait_for(lock, std::chrono::milliseconds(100));
            }
        }
        for (std::thread& t : pool) t.join();
        if (stop.load()) { out.cancelled = true; return out; }
    }
    if (progress && !progress(n, n)) { out.cancelled = true; return out; }
    out.embeddedSec = double(samples.load()) / std::max(1, mi.sampleRate);
    return out;
}

TrackClusterStats LocalDiarizer::clusterSpans(const DiarizeTrack& track, const SegmentationResult& seg,
                                              const TrackSpans& ts, const SegmenterOptions& opts,
                                              const ClusteringOptions& copts, const QStringList& modelIds,
                                              DiarizeResult* out)
{
    using namespace segmentation;
    TrackClusterStats stats;
    const ModelInfo& mi = seg.info;
    stats.spans = ts.pairs;
    stats.merged = int(ts.spans.size());
    stats.subsampled = ts.subsampled;
    stats.embeddedSec = ts.embeddedSec;
    stats.threshold = copts.threshold >= 0 ? copts.threshold : defaultThreshold(modelIds);

    // 4. Clustering (the fused vectors of modelIds) + the small-cluster reassignment.
    QVector<int> embeddedIdx;
    QVector<QVector<float>> fused;
    for (int i = 0; i < ts.spans.size(); ++i) {
        if (ts.spans[i].set.isEmpty()) continue;
        const QVector<float> v = fusion::fuse(ts.spans[i].set, modelIds);
        if (v.isEmpty()) continue;
        embeddedIdx.append(i);
        fused.append(v);
    }
    stats.embedded = int(embeddedIdx.size());
    QVector<int> labels = clustering::clusterCompleteLinkage(fused, stats.threshold, copts.numClusters);
    if (copts.numClusters <= 0) labels = clustering::reassignSmallClusters(labels, fused, copts.minClusterSize);
    stats.clusters = clustering::clusterCount(labels);
    QVector<int> spanLabel(ts.spans.size(), -1);
    for (int k = 0; k < embeddedIdx.size(); ++k) spanLabel[embeddedIdx[k]] = labels[k];

    // 5. The rest inherit the majority cluster of their stitched id.
    QHash<int, QMap<int, int>> votes;   // stitched id -> cluster -> embedded spans
    for (int i = 0; i < ts.spans.size(); ++i)
        if (spanLabel[i] >= 0) ++votes[ts.spans[i].stitched][spanLabel[i]];
    auto majority = [&votes](int stitched) {
        const auto it = votes.constFind(stitched);
        if (it == votes.constEnd()) return -1;
        int best = -1, n = 0;
        for (auto v = it->constBegin(); v != it->constEnd(); ++v)
            if (v.value() > n) { n = v.value(); best = v.key(); }
        return best;
    };
    StitchResult relabel;
    relabel.numStitched = stats.clusters;
    relabel.localToStitched.resize(seg.windows.size());
    for (int w = 0; w < seg.windows.size(); ++w) relabel.localToStitched[w].fill(-1, mi.numSpeakers);
    for (int i = 0; i < ts.spans.size(); ++i) {
        if (spanLabel[i] < 0) spanLabel[i] = majority(ts.spans[i].stitched);
        for (const auto& m : ts.spans[i].members) relabel.localToStitched[m.first][m.second] = spanLabel[i];
    }

    // 6. Aggregate + turns again over the clusters.
    QVector<WindowActivity> wacts;
    wacts.reserve(seg.windows.size());
    for (int w = 0; w < seg.windows.size(); ++w) wacts.append({seg.windows[w].startFrame, ts.acts.value(w)});
    const FrameLabels fl = aggregate(wacts, relabel, seg.numFrames, mi.powersetMaxClasses);
    QVector<Turn> turns = labelsToTurns(fl, mi, opts.minDurationOn, opts.minDurationOff);
    std::sort(turns.begin(), turns.end(), [](const Turn& a, const Turn& b) {
        return a.start != b.start ? a.start < b.start : a.speaker < b.speaker;
    });

    // 7. Speakers by first appearance, their centroids and samples.
    auto ms = [&](double sec) { return track.startOffsetMs + qint64(std::llround(sec * 1000.0)); };
    const QString file = track.path.isEmpty() ? track.trackId : QFileInfo(track.path).fileName();
    QHash<int, int> index;
    QVector<DiarizedSpeaker> speakers;
    QVector<qint64> bestClean;
    for (const Turn& t : std::as_const(turns)) {
        if (!index.contains(t.speaker)) {
            index.insert(t.speaker, int(speakers.size()));
            DiarizedSpeaker sp;
            sp.trackId = track.trackId;
            sp.index = int(speakers.size());
            sp.centroid.trackId = track.trackId;
            speakers.append(sp);
            bestClean.append(-1);
        }
        SpeakerTurn st;
        st.trackId = track.trackId;
        st.startMs = ms(t.start);
        st.endMs = ms(t.end);
        st.speakerIndex = index.value(t.speaker);
        st.overlap = t.overlap;
        DiarizedSpeaker& sp = speakers[st.speakerIndex];
        sp.totalMs += st.durationMs();
        // The sample: the longest clean (not overlapped) turn, at most sampleMaxSec from its start; failing
        // a clean one, the longest at all (bestClean < 0 marks "none clean yet").
        const qint64 len = st.durationMs();
        const qint64 rank = t.overlap ? len : len + (qint64(1) << 40);
        if (rank > bestClean[st.speakerIndex]) {
            bestClean[st.speakerIndex] = rank;
            const qint64 end = std::min(st.endMs, st.startMs + qint64(copts.sampleMaxSec * 1000.0));
            sp.centroid.sampleRef = QStringLiteral("%1#%2-%3").arg(file).arg(st.startMs).arg(end);
        }
        out->turns.append(st);
    }
    for (auto it = index.constBegin(); it != index.constEnd(); ++it) {
        DiarizedSpeaker& sp = speakers[it.value()];
        QVector<int> members;
        for (int k = 0; k < embeddedIdx.size(); ++k)
            if (labels[k] == it.key()) members.append(embeddedIdx[k]);
        sp.centroid.windows = int(members.size());
        for (const QString& model : modelIds) {
            QVector<QVector<float>> vs;
            for (int i : members) vs.append(ts.spans[i].set.value(model));
            QVector<int> all(vs.size());
            std::iota(all.begin(), all.end(), 0);
            VoiceVector vv;
            vv.vector = clustering::centroid(vs, all);
            if (vv.vector.isEmpty()) continue;
            vv.dim = int(vv.vector.size());
            vv.modelId = model;
            sp.centroid.vectors.append(vv);
        }
    }
    stats.speakers = int(speakers.size());
    out->speakers += speakers;
    return stats;
}

TrackClusterStats LocalDiarizer::clusterTrack(const DiarizeTrack& track, const SegmentationResult& seg,
                                              const QVector<float>& pcm, const SegmenterOptions& opts,
                                              const ClusteringOptions& copts, const QStringList& modelIds,
                                              const SpanEmbedder& embed, DiarizeResult* out,
                                              const std::function<bool(int, int)>& progress,
                                              const QVector<leakage::Turn>& suppress)
{
    const TrackSpans ts = embedTrack(seg, pcm, copts, suppress, {embed}, progress);
    if (ts.cancelled) {
        TrackClusterStats st;
        st.cancelled = true;
        st.spans = ts.pairs;
        return st;
    }
    return clusterSpans(track, seg, ts, opts, copts, modelIds, out);
}

QVector<QVector<leakage::Leak>> LocalDiarizer::detectLeakage(const QVector<DiarizeTrack>& tracks,
                                                             const QVector<const SegmentationResult*>& segs,
                                                             const QVector<const QVector<float>*>& pcms,
                                                             const leakage::Options& lopts)
{
    QVector<leakage::TrackInput> in;
    for (int i = 0; i < tracks.size(); ++i) {
        leakage::TrackInput ti;
        ti.trackId = tracks[i].trackId;
        ti.startOffsetMs = tracks[i].startOffsetMs;
        const SegmentationResult* s = segs.value(i, nullptr);
        const QVector<float>* p = pcms.value(i, nullptr);
        if (s && p) {
            ti.frameDb = leakage::frameDb(*p, lopts.frameMs, s->info.sampleRate);
            for (const segmentation::Turn& t : s->turns) ti.speech.append({t.start, t.end});
        }
        in.append(ti);
    }
    return leakage::detect(in, lopts);
}

SegmentationResult LocalDiarizer::withoutTurns(const SegmentationResult& seg, const QVector<leakage::Turn>& drop)
{
    SegmentationResult out = seg;
    if (drop.isEmpty()) return out;
    out.turns.clear();
    for (const segmentation::Turn& t : seg.turns) {
        bool dropped = false;
        for (const leakage::Turn& d : drop)
            if (std::abs(d.start - t.start) < 1e-9 && std::abs(d.end - t.end) < 1e-9) { dropped = true; break; }
        if (!dropped) out.turns.append(t);
    }
    return out;
}

DiarizeJob* LocalDiarizer::diarize(const DiarizeRequest& req)
{
    if (req.isEmpty()) {
        auto* job = new DiarizeJob(this);
        job->failLater(inputError(QStringLiteral("no_input"), QStringLiteral("no track has input")));
        return job;
    }
    return new LocalDiarizeJob(m_loader, m_media, req, optionsFor(req, m_base), clusteringOptionsFor(req, m_cluster),
                               m_embedders, this);
}

} // namespace tanara::processing

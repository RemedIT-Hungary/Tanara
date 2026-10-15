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
#include <numeric>

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
            VoiceEmbedderSet embedders = proto.freshCopy();
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
            qint64 doneBefore = 0;
            for (int k = 0; k < idx.size() && !state->cancelled; ++k) {
                const DiarizeTrack& t = req.tracks[idx[k]];
                const qint64 windows = seg->windowCount(pcms[k].size());
                const auto r = seg->segment(pcms[k], [report, doneBefore](int done, int) { report(doneBefore + done); },
                                            &state->cancelled, &err);
                if (!r) {
                    if (state->cancelled) return;
                    state->error = makeError(Error::Kind::Service, QStringLiteral("segmentation_failed"), err);
                    return;
                }
                if (!t.knownPerson.isNull() || !cluster) {
                    LocalDiarizer::appendTrackResult(t, *r, opts, &state->out);
                } else {
                    qint64 lastSent = -1;
                    const TrackClusterStats st = LocalDiarizer::clusterTrack(
                        t, *r, pcms[k], opts, copts, modelIds,
                        [&embedders, &modelIds](const QVector<float>& pcm) { return embedders.embedPcmWith(pcm, modelIds); },
                        &state->out,
                        [&](int done, int n) {
                            const qint64 at = doneBefore + windows + (n > 0 ? windows * done / n : windows);
                            if (at != lastSent) { lastSent = at; report(at); }
                            return !state->cancelled.load();
                        });
                    if (st.cancelled || state->cancelled) return;
                    qCInfo(lcVoice).noquote()
                        << QStringLiteral("Diarization: track %1: %2 spans, %3 embedded%4, %5 clusters, %6 speakers")
                               .arg(t.trackId).arg(st.spans).arg(st.embedded)
                               .arg(st.subsampled ? QStringLiteral(" (subsampled)") : QString())
                               .arg(st.clusters).arg(st.speakers);
                }
                doneBefore += 2 * windows;
                report(doneBefore);
                pcms[k] = {};   // free the PCM early
            }
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

// One (window, local speaker) pair: its clean runs (global frames [first, second)) and its labels.
struct EmbedSpan {
    int window = 0;
    int local = 0;
    int stitched = -1;
    QVector<QPair<int, int>> runs;
    int cleanFrames = 0;
    int label = -1;                  // cluster (embedded spans), then the inherited one
    EmbeddingSet set;                // the per-model vectors (embedded spans only)
};

} // namespace

TrackClusterStats LocalDiarizer::clusterTrack(const DiarizeTrack& track, const SegmentationResult& seg,
                                              const QVector<float>& pcm, const SegmenterOptions& opts,
                                              const ClusteringOptions& copts, const QStringList& modelIds,
                                              const SpanEmbedder& embed, DiarizeResult* out,
                                              const std::function<bool(int, int)>& progress)
{
    using namespace segmentation;
    TrackClusterStats stats;
    const ModelInfo& mi = seg.info;
    const double frameSec = mi.frameStepSec();
    const int minFrames = std::max(copts.minRunFrames,
                                   int(std::ceil(copts.minEmbedSec / std::max(1e-6, frameSec))));

    // 1. The spans (time order: windows, then local speakers).
    QVector<EmbedSpan> spans;
    QVector<Activity> acts(seg.windows.size());
    for (int w = 0; w < seg.windows.size(); ++w) {
        acts[w] = seg.windowActivity(w);
        const Activity& a = acts[w];
        const QVector<int>& st = seg.windows[w].stitched;
        for (int l = 0; l < a.speakers; ++l) {
            if (l >= st.size() || st[l] < 0) continue;
            EmbedSpan sp;
            sp.window = w;
            sp.local = l;
            sp.stitched = st[l];
            int f = 0;
            while (f < a.frames) {
                if (!(a.at(f, l) > 0.5f && a.countAt(f) == 1)) { ++f; continue; }
                const int start = f;
                while (f < a.frames && a.at(f, l) > 0.5f && a.countAt(f) == 1) ++f;
                if (f - start >= copts.minRunFrames) {
                    sp.runs.append({seg.windows[w].startFrame + start, seg.windows[w].startFrame + f});
                    sp.cleanFrames += f - start;
                }
            }
            spans.append(sp);
        }
    }
    stats.spans = int(spans.size());

    // 2. Which spans are embedded: the long-enough ones, subsampled above the cap.
    QVector<int> candidates, groups;
    for (int i = 0; i < spans.size(); ++i)
        if (spans[i].cleanFrames >= minFrames) {
            candidates.append(i);
            groups.append(spans[i].stitched);
        }
    const QVector<int> picked = clustering::subsample(int(candidates.size()), copts.maxEmbeddings, groups);
    stats.subsampled = picked.size() < candidates.size();

    // 3. Embedding (the runs' samples, centred on their frames, concatenated).
    const int half = mi.receptiveFieldShift / 2;
    QVector<int> embeddedIdx;
    QVector<QVector<float>> fused;
    for (int k = 0; k < picked.size(); ++k) {
        if (progress && !progress(k, int(picked.size()))) { stats.cancelled = true; return stats; }
        EmbedSpan& sp = spans[candidates[picked[k]]];
        QVector<float> audio;
        for (const auto& run : std::as_const(sp.runs)) {
            const qint64 a = std::max<qint64>(0, qint64(run.first) * mi.receptiveFieldShift + mi.receptiveFieldSize / 2 - half);
            const qint64 b = std::min<qint64>(pcm.size(), qint64(run.second - 1) * mi.receptiveFieldShift
                                                              + mi.receptiveFieldSize / 2 + half);
            if (b > a) audio.append(pcm.mid(a, b - a));
        }
        if (audio.isEmpty() || !embed) continue;
        const EmbeddingSet set = embed(audio);
        const QVector<float> v = fusion::fuse(set, modelIds);
        if (v.isEmpty()) continue;
        sp.set = set;
        embeddedIdx.append(candidates[picked[k]]);
        fused.append(v);
    }
    if (progress && !progress(int(picked.size()), int(picked.size()))) { stats.cancelled = true; return stats; }
    stats.embedded = int(embeddedIdx.size());

    // 4. Clustering + the small-cluster reassignment.
    QVector<int> labels = clustering::clusterCompleteLinkage(fused, copts.threshold, copts.numClusters);
    if (copts.numClusters <= 0) labels = clustering::reassignSmallClusters(labels, fused, copts.minClusterSize);
    stats.clusters = clustering::clusterCount(labels);
    for (int k = 0; k < embeddedIdx.size(); ++k) spans[embeddedIdx[k]].label = labels[k];

    // 5. The rest inherit the majority cluster of their stitched id.
    QHash<int, QMap<int, int>> votes;   // stitched id -> cluster -> embedded spans
    for (const EmbedSpan& sp : std::as_const(spans))
        if (sp.label >= 0) ++votes[sp.stitched][sp.label];
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
    for (EmbedSpan& sp : spans) {
        if (sp.label < 0) sp.label = majority(sp.stitched);
        relabel.localToStitched[sp.window][sp.local] = sp.label;
    }

    // 6. Aggregate + turns again over the clusters.
    QVector<WindowActivity> wacts;
    wacts.reserve(seg.windows.size());
    for (int w = 0; w < seg.windows.size(); ++w) wacts.append({seg.windows[w].startFrame, acts[w]});
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
            for (int i : members) vs.append(spans[i].set.value(model));
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

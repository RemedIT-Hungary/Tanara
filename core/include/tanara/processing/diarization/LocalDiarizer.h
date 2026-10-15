#pragma once
//
// LocalDiarizer: the local IDiarizer (Origin::Local). Every track with input is decoded (16 kHz mono
// float) and segmented by PyannoteSegmenter; then (D4) clustered:
//
//   1. Embedding spans: for every (window, local speaker) with a stitched id, the frames where that local
//      speaker talks ALONE (overlapped frames excluded), in runs of >= minRunFrames frames; a span with
//      less than minEmbedSec of such speech is not embedded. The span's samples (concatenated runs) are
//      embedded with every active speaker-embedding model (VoiceEmbedderSet) and fused
//      (library::fusion::fuse: the cosine of two fused vectors = the mean of the per-model cosines).
//   2. Complete-linkage agglomerative clustering (clustering::clusterCompleteLinkage, cosine distance),
//      cut at `threshold` (default 0.5, the sherpa-onnx default) or into `numClusters` (request option);
//      clusters with fewer than minClusterSize embeddings join the nearest large one.
//   3. Cap: the distance matrix is quadratic, so above maxEmbeddings spans (default 7000: ~98 MB of
//      floats; an hour at shift 0.25 is ~2000-3000 spans) a time-uniform subsample is embedded and
//      clustered; every stitched id keeps at least one sampled span.
//   4. Relabelling: a span takes its cluster; a span without an embedding (too short, only overlapped,
//      not sampled) takes the majority cluster of its stitched id (-1 = dropped). The windows' local
//      speakers are mapped to the clusters and aggregated + turned + overlap-flagged again
//      (segmentation::aggregate / labelsToTurns), so overlapped speech survives across clusters.
//   5. Speakers: the clusters that kept a turn, renumbered 0, 1, ... by first appearance per track;
//      totalMs = their speech; centroid = the per-model L2-normalized mean of the member embeddings
//      (IdentifyProbe: vectors, trackId, windows = members, sampleRef = the longest clean turn, at most
//      sampleMaxSec, as "<track file name>#startMs-endMs" in meeting time).
//
// Tracks are clustered independently (indices are unique per track; no cross-track matching). A track
// bound to a person by hand (DiarizeTrack::knownPerson) is one speaker (index 0, person set; no
// embedding): all its speech, merged, is that speaker's.
//
// Without an embedder set (setEmbedderSet not called / empty) the result is the D3 segmentation-only one:
// the stitched local speakers are the speakers and the centroids are empty (the diarize-probe tool and the
// segmentation tests use it; the app always passes the voice models).
//
// The window shift defaults to 0.25 here (D3 measurement: the same turns as 0.1 within about one frame, at
// 2.4x the speed, and 2.4x fewer embeddings); the request does not set it.
//
// The work runs on a worker thread (one model / ORT session per job, the embedder set's own fresh copy);
// progress(done, total): total = 2 x the windows of all tracks (the tracks are decoded first): per track
// its windows count once for the segmentation and once for the embeddings. cancel() (or destroying the
// job) stops the worker after the window batch / span in progress. Errors: no input -> Input "no_input";
// no usable segmentation model -> Unsupported "no_segmentation_model"; an embedder set whose models
// cannot be loaded -> Unsupported "no_embedding_model"; a decode failure -> Input "decode_failed"; an
// inference failure -> Service "segmentation_failed".
#include "tanara/media/MediaTool.h"
#include "tanara/processing/diarization/PyannoteSegmenter.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/providers/voice/IDiarizer.h"

#include <QObject>

#include <functional>
#include <memory>

namespace tanara::processing {

struct ClusteringOptions {
    double threshold = 0.5;          // cosine-distance cut (complete linkage: every pair of a cluster is closer)
    int    numClusters = -1;         // > 0: exactly this many clusters per track (overrides the threshold)
    int    maxEmbeddings = 7000;     // the cap of embedded spans per track (see step 3)
    int    minClusterSize = 3;       // smaller clusters join the nearest large one (1 = off)
    int    minRunFrames = 10;        // a clean run shorter than this (~170 ms) is not used (sherpa-onnx)
    double minEmbedSec = 0.4;        // a span with less clean speech is not embedded
    double sampleMaxSec = 10.0;      // the sampleRef's maximal length
};

// What the clustering of one track did (logs, the probe tool, the tests).
struct TrackClusterStats {
    int spans = 0;                   // (window, local speaker) pairs with a stitched id
    int embedded = 0;                // of which embedded (and clustered)
    int clusters = 0;                // clusters before the turns (after the small-cluster reassignment)
    int speakers = 0;                // speakers with a turn
    bool subsampled = false;         // the cap applied
    bool cancelled = false;
};

class LocalDiarizer : public QObject, public providers::IDiarizer {
    Q_OBJECT
public:
    static inline const QString Id = QStringLiteral("local");

    // Opens a segmenter for a model id ("" = the default model) on the worker thread; nullptr + *error
    // when it cannot. Test seam: a loader that wraps a fake SegmentationModel.
    using SegmenterLoader =
        std::function<std::unique_ptr<PyannoteSegmenter>(const QString& modelId, const SegmenterOptions& opts,
                                                         QString* error)>;

    LocalDiarizer(SegmenterLoader loader, media::MediaToolPtr media, QObject* parent = nullptr);

    // The production loader: VoiceModelRegistry spec -> PyannoteSegmenter::open (integrity check) with
    // `ort` options.
    static SegmenterLoader catalogLoader(const QString& modelsDir, const QString& appDir,
                                         OnnxSegmentationOptions ort = {});
    // Whether a segmentation model is usable (the default one, or else any catalog segmentation model).
    static bool hasUsableModel(const QString& modelsDir, const QString& appDir);

    // The segmenter options the request asks for (defaults where it gives none).
    static SegmenterOptions optionsFor(const providers::DiarizeRequest& req, SegmenterOptions base = {});
    // The clustering options the request asks for (defaults where it gives none).
    static ClusteringOptions clusteringOptionsFor(const providers::DiarizeRequest& req, ClusteringOptions base = {});
    // One track's segmentation -> the port's turns and speakers WITHOUT clustering (the stitched local
    // speakers; a bound track: one speaker). Exposed for the tests.
    static void appendTrackResult(const providers::DiarizeTrack& track, const SegmentationResult& seg,
                                  const SegmenterOptions& opts, providers::DiarizeResult* out);

    // Embeds one 16 kHz mono PCM slice with every clustering model (an empty set / missing model = failed).
    using SpanEmbedder = std::function<EmbeddingSet(const QVector<float>& pcm16k)>;
    // Steps 1-5 for one UNBOUND track: `pcm` is the track's audio the segmentation ran on, `modelIds` the
    // models whose vectors are fused. progress(done, total) over the spans; returning false cancels
    // (stats.cancelled, nothing appended). Exposed for the tests (a fake embedder).
    static TrackClusterStats clusterTrack(const providers::DiarizeTrack& track, const SegmentationResult& seg,
                                          const QVector<float>& pcm, const SegmenterOptions& opts,
                                          const ClusteringOptions& copts, const QStringList& modelIds,
                                          const SpanEmbedder& embed, providers::DiarizeResult* out,
                                          const std::function<bool(int, int)>& progress = {});

    QString name() const override { return Id; }
    providers::DiarizeJob* diarize(const providers::DiarizeRequest& req) override;

    // Base options (window shift, stitching, batch); the request's own options override per job.
    void setBaseOptions(const SegmenterOptions& opts) { m_base = opts; }
    const SegmenterOptions& baseOptions() const { return m_base; }
    void setClusteringOptions(const ClusteringOptions& opts) { m_cluster = opts; }
    // The speaker-embedding models of the clustering (an unloaded set; the job loads its own copy).
    void setEmbedderSet(VoiceEmbedderSet set) { m_embedders = std::move(set); }

    // The default window shift of the diarizer (see the header comment).
    static constexpr double kDefaultWindowShift = 0.25;

private:
    SegmenterLoader m_loader;
    media::MediaToolPtr m_media;
    SegmenterOptions m_base;
    ClusteringOptions m_cluster;
    VoiceEmbedderSet m_embedders;
};

} // namespace tanara::processing

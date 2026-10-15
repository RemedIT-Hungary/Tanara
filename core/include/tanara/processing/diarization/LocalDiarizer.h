#pragma once
//
// LocalDiarizer: the local IDiarizer (Origin::Local). Every track with input is decoded (16 kHz mono
// float) and segmented by PyannoteSegmenter; then (D4) clustered:
//
//   1. Embedding spans: for every (window, local speaker) with a stitched id, the frames where that local
//      speaker talks ALONE (overlapped frames excluded), in runs of >= minRunFrames frames. D6: the spans of
//      one stitched speaker in consecutive windows are MERGED while the union of their clean speech fits
//      mergeSpanSec (15 s): the overlapping windows see the same speech ~1/shift times, so this embeds
//      ~2.4x less audio and gives longer, steadier embeddings (without it the clustering over-split: the
//      D6 calibration had 3-4x the speaker-count error). A span with less than minEmbedSec of clean speech
//      is not embedded. The span's samples (concatenated runs) are embedded with every active
//      speaker-embedding model (VoiceEmbedderSet, embedThreads ORT threads per session, embedWorkers
//      parallel workers) and fused (library::fusion::fuse: the cosine of two fused vectors = the mean of
//      the per-model cosines).
//   2. Complete-linkage agglomerative clustering (clustering::clusterCompleteLinkage, cosine distance),
//      cut at `threshold` (default: the calibrated per-model value, defaultThreshold; see the table at
//      kWeSpeakerThreshold) or into `numClusters` (request option); clusters with fewer than
//      minClusterSize embeddings join the nearest large one.
//   3. Cap: the distance matrix is quadratic, so above maxEmbeddings spans (default 7000: ~98 MB of
//      floats; an hour at shift 0.25 is ~1000 merged spans) a time-uniform subsample is embedded and
//      clustered; every stitched id keeps at least one sampled span.
// Cross-track leakage (D6, TrackLeakage.h, ClusteringOptions::suppressLeakage, default on): all tracks are
// segmented first; a speech turn that another track covers at speech level while it is itself much
// quieter than its own speech (a loudspeaker-to-mic leak, or the user's echo on the loopback) is masked
// out of its track before the embedding and the turns (logged per track).
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
// progress(done, total): total = 2 x the windows of all tracks (the tracks are decoded first): every
// track's windows count once for the segmentation (all tracks first) and once for the embeddings. cancel() (or destroying the
// job) stops the worker after the window batch / span in progress. Errors: no input -> Input "no_input";
// no usable segmentation model -> Unsupported "no_segmentation_model"; an embedder set whose models
// cannot be loaded -> Unsupported "no_embedding_model"; a decode failure -> Input "decode_failed"; an
// inference failure -> Service "segmentation_failed".
#include "tanara/media/MediaTool.h"
#include "tanara/processing/diarization/PyannoteSegmenter.h"
#include "tanara/processing/diarization/TrackLeakage.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"
#include "tanara/providers/voice/IDiarizer.h"

#include <QObject>

#include <functional>
#include <memory>

namespace tanara::processing {

// The calibrated default clustering thresholds (D6, LocalDiarizer::defaultThreshold): the complete-linkage
// cut of the cosine distance per embedding-model set (a fused set's distance is the mean of the per-model
// distances, so it has its own entry). Calibration (2026-10-15): 6 Hungarian meetings, 13 tracks, 26 min,
// Soniox transcript as the (imperfect) oracle, shift 0.25, span merging + leakage suppression on,
// minClusterSize 3; the choice is by the per-track speaker-count error (the confusion was flat), and lies
// between the measured same-speaker distances (p90 ~0.26, max ~0.6) and the different-speaker ones
// (p10: CAM++ ~0.7, WeSpeaker ~0.85). Full tables: the vault plan note, "D6 mérések és kalibráció".
//
//   models                           | threshold | |speaker-count error| per track | exact tracks
//   ---------------------------------+-----------+--------------------------------+-------------
//   wespeaker-resnet34-lm            |   0.75    | 0.17 (0.58 at the old 0.5)     | 10/12 (7)
//   campplus                         |   0.65    | 0.33 (0.50 at 0.5)             |  8/12 (8)
//   campplus + wespeaker-resnet34-lm |   0.70    | 0.17 (0.58 at 0.5)             | 10/12 (7)
//   any other model / set            | the mean of the per-model entries, else kFallbackThreshold
inline constexpr double kWeSpeakerThreshold = 0.75;
inline constexpr double kCamPlusThreshold = 0.65;
inline constexpr double kFusedThreshold = 0.70;
inline constexpr double kFallbackThreshold = 0.5;   // the sherpa-onnx default

struct ClusteringOptions {
    double threshold = -1.0;         // cosine-distance cut; < 0 = the per-model default (defaultThreshold)
    int    numClusters = -1;         // > 0: exactly this many clusters per track (overrides the threshold)
    int    maxEmbeddings = 7000;     // the cap of embedded spans per track (see step 3)
    int    minClusterSize = 3;       // smaller clusters join the nearest large one (1 = off)
    int    minRunFrames = 10;        // a clean run shorter than this (~170 ms) is not used (sherpa-onnx)
    double minEmbedSec = 0.5;        // a span with less clean speech is not embedded (the embedder needs 0.5 s)
    double sampleMaxSec = 10.0;      // the sampleRef's maximal length
    // Span merging (D6): the spans of one stitched speaker in consecutive windows are merged into one
    // span while the union of their clean speech stays within this length (0 = off: one span per
    // (window, local speaker)). The overlapping windows see the same speech several times, so this
    // cuts the embedded audio ~1/shift-fold, and a longer span gives a steadier embedding.
    double mergeSpanSec = 15.0;
    // Embedding speed (D6, measured on a 13-minute track, see the plan note): ONNX Runtime intra-op threads
    // per model session, and parallel workers (each with its OWN model sessions: +~95 MB per model and
    // worker). 4 threads x 1 worker was as fast as 4 workers x 1 thread at half the memory, so the
    // default is threads, not workers.
    int    embedThreads = 4;
    int    embedWorkers = 1;
    // Cross-track leakage suppression (D6, TrackLeakage.h): leaked speech turns are removed from the
    // track they leaked into before the embedding and the turns.
    bool   suppressLeakage = true;
    leakage::Options leakage;
};

// What the clustering of one track did (logs, the probe tool, the tests).
struct TrackClusterStats {
    int spans = 0;                   // (window, local speaker) pairs with a stitched id
    int merged = 0;                  // spans after the merging (= spans when off)
    int embedded = 0;                // of which embedded (and clustered)
    int clusters = 0;                // clusters before the turns (after the small-cluster reassignment)
    int speakers = 0;                // speakers with a turn
    double threshold = 0;            // the cut used
    double embeddedSec = 0;          // audio embedded (per model)
    bool subsampled = false;         // the cap applied
    bool cancelled = false;
};

// The embedded spans of one track (steps 1-3), reusable for several clusterings (thresholds, model
// subsets: the evaluation tool clusters one embedding pass many times).
struct TrackSpans {
    struct Span {
        QVector<QPair<int, int>> members;    // (window, local speaker) pairs
        int stitched = -1;
        QVector<QPair<int, int>> runs;       // clean runs, global frames [first, second), merged
        int cleanFrames = 0;
        EmbeddingSet set;                    // the per-model vectors (embedded spans only)
    };
    QVector<segmentation::Activity> acts;    // per window (after the leakage mask)
    QVector<Span> spans;
    int pairs = 0;
    bool subsampled = false;
    bool cancelled = false;
    double embeddedSec = 0;
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
    // The same model set unloaded, with `intraOpThreads` ONNX Runtime threads per session (the spec's
    // EmbedderConfig; a test loader may ignore it).
    static VoiceEmbedderSet withThreads(const VoiceEmbedderSet& set, int intraOpThreads);

    // The default threshold of an embedding-model set (sorted or not): its calibrated entry, else the
    // mean of the per-model entries, else kFallbackThreshold.
    static double defaultThreshold(const QStringList& modelIds);

    // Steps 1-3 for one unbound track: the spans (leakage-masked by `suppress`, track-time seconds),
    // merged, subsampled, embedded by `embedders` (one per worker thread; each is used by ONE thread
    // only). progress(done, total) is called on the calling thread; returning false cancels.
    static TrackSpans embedTrack(const SegmentationResult& seg, const QVector<float>& pcm,
                                 const ClusteringOptions& copts, const QVector<leakage::Turn>& suppress,
                                 const QVector<SpanEmbedder>& embedders,
                                 const std::function<bool(int, int)>& progress = {});
    // Steps 4-7: clusters the embedded spans with the vectors of `modelIds` fused, appends the turns and
    // the speakers.
    static TrackClusterStats clusterSpans(const providers::DiarizeTrack& track, const SegmentationResult& seg,
                                          const TrackSpans& spans, const SegmenterOptions& opts,
                                          const ClusteringOptions& copts, const QStringList& modelIds,
                                          providers::DiarizeResult* out);
    // The leakage of the tracks (TrackLeakage.h) from their PCM and segmentation: per track the leaked
    // speech turns (track time). Tracks without a segmentation (nullptr) take part as nothing.
    static QVector<QVector<leakage::Leak>> detectLeakage(const QVector<providers::DiarizeTrack>& tracks,
                                                         const QVector<const SegmentationResult*>& segs,
                                                         const QVector<const QVector<float>*>& pcms,
                                                         const leakage::Options& opts);
    // A segmentation without the leaked turns (the hand-bound / unclustered path).
    static SegmentationResult withoutTurns(const SegmentationResult& seg, const QVector<leakage::Turn>& drop);

    // embedTrack (one embedder) + clusterSpans.
    static TrackClusterStats clusterTrack(const providers::DiarizeTrack& track, const SegmentationResult& seg,
                                          const QVector<float>& pcm, const SegmenterOptions& opts,
                                          const ClusteringOptions& copts, const QStringList& modelIds,
                                          const SpanEmbedder& embed, providers::DiarizeResult* out,
                                          const std::function<bool(int, int)>& progress = {},
                                          const QVector<leakage::Turn>& suppress = {});

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

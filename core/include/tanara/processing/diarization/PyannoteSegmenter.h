#pragma once
//
// PyannoteSegmenter: the speaker-segmentation step of the local diarization ("who speaks when" inside
// 10 s windows, without knowing who the speakers are). Input: 16 kHz mono float PCM of ONE track.
// Output: a SegmentationResult - per window the powerset classes of every frame (= the local
// speakers' activity, overlap included), the stitched local-speaker id of every (window, local speaker)
// and the post-processed turns per stitched speaker. The math is in SegmentationMath.h.
//
// The network is behind SegmentationModel (test seam): loadOnnxSegmentationModel() opens the pyannote
// segmentation-3.0 ONNX export with ONNX Runtime (CPU; the custom metadata gives sample rate, window,
// receptive field and powerset shape), a test passes a fake model that returns synthetic logits.
//
// Stitched ids are NOT global speakers: a voice that comes back after more than a window of silence
// gets a new id. D4 embeds every (window, local speaker), clusters them and maps the windows' local
// activity to the clusters (SegmentationResult::windows keeps everything it needs).
//
// Threading: segment() is synchronous and runs on the caller's (worker) thread; one segmenter (and
// its ORT session) per thread. The cancel flag is checked between batches.
#include "tanara/processing/diarization/SegmentationMath.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QString>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace tanara::processing {

// The segmentation network: a batch of windows -> per-frame powerset log-probabilities.
class SegmentationModel {
public:
    virtual ~SegmentationModel() = default;
    virtual const segmentation::ModelInfo& info() const = 0;
    // `count` consecutive windows of info().windowSize samples -> count x framesPerWindow x numClasses
    // log-probabilities in *out. false + *error on failure.
    virtual bool run(const float* samples, int count, QVector<float>* out, QString* error) = 0;
};

struct OnnxSegmentationOptions {
    // ORT intra-op threads. 4 (D3 measurement, 10 min audio, float): RTF 0.033 (1 thread) -> 0.0095 with
    // batch 4; more threads gain little on this small network.
    int intraOpThreads = 4;
};

// Opens an ONNX segmentation model (no integrity check here; see PyannoteSegmenter::open). nullptr +
// *error when the file cannot be loaded, its shape / metadata is not a pyannote powerset model, or the
// build has no ONNX Runtime (TANARA_BUILD_VOICEID=OFF).
std::unique_ptr<SegmentationModel> loadOnnxSegmentationModel(const QString& path, const OnnxSegmentationOptions& opts,
                                                             QString* error);
// The ORT execution providers of this build ("CPUExecutionProvider", ...); empty without ORT.
QStringList onnxExecutionProviders();

struct SegmenterOptions {
    // Window shift / window size, rounded to whole frames. 0.1 = the sherpa-onnx / pyannote default; the
    // D3 measurement found 0.25 nearly identical in turn boundaries at 2.4x the speed - the default is
    // decided in D4/D6 together with the embedding cost (one embedding per window and local speaker).
    double windowShiftRatio = 0.1;
    double minDurationOn = 0.3;      // seconds: shorter turns are dropped
    double minDurationOff = 0.5;     // seconds: shorter gaps inside one speaker's speech are bridged
    segmentation::StitchOptions stitch;
    // Windows per ORT run. 4: ~1.7x faster than 1 at +80 MB arena (8: +190 MB, 16: +360 MB, for little more).
    int batchSize = 4;
};

struct SegmentationWindow {
    qint64 startSample = 0;          // track sample of the window's first sample
    int startFrame = 0;              // global frame of the window's first output frame
    QVector<quint8> classes;         // argmax powerset class per frame (framesPerWindow)
    QVector<int> stitched;           // per local speaker: stitched id, -1 = silent in this window
};

struct SegmentationResult {
    QString modelId;                 // the catalog id (or the path when opened without one)
    segmentation::ModelInfo info;
    qint64 numSamples = 0;
    int windowShift = 0;             // samples
    int numFrames = 0;               // global frames covering the audio
    int numStitched = 0;             // stitched speaker ids 0 .. numStitched-1 (some may have no turn)
    QVector<SegmentationWindow> windows;
    QVector<segmentation::Turn> turns;   // post-processed, sorted by start; times in seconds of the track

    // Window w's 0/1 local activity (framesPerWindow x numSpeakers).
    segmentation::Activity windowActivity(int w) const;
    // Window w's local speaker runs (D4 picks the embedding spans from them).
    QVector<segmentation::LocalTurn> windowTurns(int w) const;
    double durationSec() const { return info.sampleRate > 0 ? double(numSamples) / info.sampleRate : 0.0; }
};

class PyannoteSegmenter {
public:
    using Progress = std::function<void(int doneWindows, int totalWindows)>;

    explicit PyannoteSegmenter(std::unique_ptr<SegmentationModel> model, SegmenterOptions opts = {},
                               QString modelId = QString());
    ~PyannoteSegmenter();

    // Resolves the catalog model (VoiceModelRegistry::resolvePath), checks its integrity and loads it.
    // A file whose size / SHA-256 differs from the pinned one is REFUSED unless the user accepted it
    // ("use it anyway": VoiceModelDownloader::hasAcceptedRecord); a missing file is an error. Logs the
    // model id and its state. nullptr + *error (English) on failure.
    static std::unique_ptr<PyannoteSegmenter> open(const VoiceModelSpec& spec, const QString& modelsDir,
                                                   const QString& appDir, const SegmenterOptions& opts,
                                                   const OnnxSegmentationOptions& ort, QString* error);
    // Whether open() would accept the model file (exists; size check only, the SHA-256 is checked at open).
    static bool isUsable(const VoiceModelSpec& spec, const QString& modelsDir, const QString& appDir);

    const segmentation::ModelInfo& info() const;
    const SegmenterOptions& options() const { return m_opts; }
    const QString& modelId() const { return m_modelId; }
    // The number of windows segment() runs for `numSamples` samples.
    int windowCount(qint64 numSamples) const;

    // Segments one track. std::nullopt + *error on a model failure or when cancelled (*error =
    // "cancelled"). Empty input: an empty result.
    std::optional<SegmentationResult> segment(const QVector<float>& pcm16k, const Progress& progress = {},
                                              const std::atomic<bool>* cancel = nullptr, QString* error = nullptr);

private:
    std::unique_ptr<SegmentationModel> m_model;
    SegmenterOptions m_opts;
    QString m_modelId;
    QVector<QVector<int>> m_powerset;
};

} // namespace tanara::processing

#pragma once
//
// The pure math of the speaker segmentation step (pyannote segmentation-3.0), without ONNX Runtime:
//
//   1. Windowing: 10 s windows with a configurable shift (windowShiftRatio x window); the shift is
//      rounded to a whole number of output frames, so every window's frames sit on ONE global frame
//      grid (frame g covers sample g * rfShift .. + rfSize; its time is the centre).
//   2. Powerset decoding: the model gives, per frame, log-probabilities of 7 "powerset" classes
//      ({}, {0}, {1}, {2}, {0,1}, {0,2}, {1,2}) over 3 LOCAL speakers of the window. The argmax class
//      becomes a 0/1 activity row (frames x 3); overlapped speech = two active local speakers.
//   3. Stitching: the local speakers of overlapping windows are matched to "stitched" speakers with a
//      cost matrix + the Hungarian algorithm (the pyannote `Inference.stitch` / `permutate` idea): the
//      score of (stitched s, local l) is the soft intersection of their activity on the frames the new
//      window shares with the earlier ones; a pair is accepted only above an IoU threshold. A local
//      speaker that matches nothing (e.g. a voice that returns after more than a window of silence)
//      gets a NEW stitched id: stitched ids are not global speakers, D4 clusters them by embedding.
//   4. Aggregation (sherpa-onnx FinalizeLabels): per global frame the speaker COUNT is the rounded mean
//      of the windows' local counts, and the `count` stitched speakers with the highest mean activity
//      are active. Overlapped speech survives (two speakers on one frame).
//   5. Post-processing per speaker: gaps shorter than minDurationOff are bridged first, then turns
//      shorter than minDurationOn are dropped (pyannote Binarize order).
//
// Everything here is deterministic and unit-tested with synthetic logits (test_segmentation_math).
#include <QString>
#include <QVector>

#include <array>

namespace tanara::processing::segmentation {

// The model's constants (from the ONNX custom metadata; the defaults are segmentation-3.0's).
struct ModelInfo {
    QString modelType;               // "pyannote-segmentation-3.0"
    int sampleRate = 16000;
    int windowSize = 160000;         // samples per window (10 s)
    int receptiveFieldSize = 991;    // samples one output frame looks at
    int receptiveFieldShift = 270;   // samples between two output frames (16.875 ms)
    int numSpeakers = 3;             // local speakers per window
    int powersetMaxClasses = 2;      // at most this many simultaneous local speakers
    int numClasses = 7;              // powerset classes
    int framesPerWindow = 589;       // output frames per window

    // Frames of a window per the receptive field: floor((window - rfSize) / rfShift) + 1.
    int computedFramesPerWindow() const;
    double frameStepSec() const { return double(receptiveFieldShift) / sampleRate; }
    // Time (seconds) of global frame g: the centre of its receptive field.
    double frameTimeSec(int g) const
    {
        return (double(g) * receptiveFieldShift + 0.5 * receptiveFieldSize) / sampleRate;
    }
    bool isValid() const;
};

// The powerset classes in pyannote order: by set size, then lexicographic. For (3, 2):
// {}, {0}, {1}, {2}, {0,1}, {0,2}, {1,2}.
QVector<QVector<int>> powersetClasses(int numSpeakers, int maxSetSize);

// A frames x speakers matrix (row-major). Binary (0/1) for one window, fractional when aggregated.
struct Activity {
    int frames = 0;
    int speakers = 0;
    QVector<float> v;

    Activity() = default;
    Activity(int f, int s) : frames(f), speakers(s), v(f * s, 0.0f) {}
    float at(int f, int s) const { return v[f * speakers + s]; }
    float& at(int f, int s) { return v[f * speakers + s]; }
    // The number of frames where speaker s is active (> 0.5).
    int activeFrames(int s) const;
    int countAt(int f) const;        // active speakers on frame f
};

// Argmax powerset class per frame of frames x classes log-probabilities.
QVector<quint8> argmaxClasses(const float* logits, int frames, int classes);
// Powerset classes -> 0/1 local activity (frames x numSpeakers). Out-of-range classes count as silence.
Activity decodePowerset(const QVector<quint8>& classes, const QVector<QVector<int>>& mapping, int numSpeakers);

// ---- windowing ----

// The window shift in samples: ratio * windowSize rounded to a whole number of frames (>= 1 frame,
// <= one window). E.g. 0.1 -> 59 frames = 15930 samples (0.996 s).
int windowShiftSamples(const ModelInfo& info, double windowShiftRatio);
// Window start samples covering `numSamples`: 0, shift, 2*shift ... while the window fits; plus one
// last (zero-padded) window if the end is not covered. Audio no longer than a window: one window.
// No audio: no window.
QVector<qint64> windowStarts(qint64 numSamples, int windowSize, int windowShift);
// Global frames covering numSamples: the frames whose centre lies inside the audio.
int globalFrameCount(const ModelInfo& info, qint64 numSamples);

// ---- the Hungarian algorithm ----

// Minimum-cost assignment of a rows x cols cost matrix (row-major, any shape). Returns, per row, the
// assigned column, or -1 when rows > cols and the row stays unassigned. O(n^2 m). Self-written
// (Kuhn-Munkres with potentials), no third-party code.
QVector<int> hungarianAssign(const QVector<double>& cost, int rows, int cols);

// ---- stitching ----

struct StitchOptions {
    double minIoU = 0.5;             // accept a (stitched, local) pair only if their overlap IoU >= this
    int    minSharedFrames = 10;     // ... and they share at least this many active frames (~170 ms)
};

// One window's input to stitching: its first global frame and its 0/1 local activity.
struct WindowActivity {
    int startFrame = 0;
    Activity activity;
};

struct StitchResult {
    QVector<QVector<int>> localToStitched;   // per window, per local speaker: stitched id (-1 = silent)
    int numStitched = 0;
};

// Stitches the windows (in time order) as described in step 3 above.
StitchResult stitchWindows(const QVector<WindowActivity>& windows, const StitchOptions& opts = {});

// ---- aggregation + turns ----

// Per global frame: the active stitched speakers (at most maxActive, -1 = empty slot).
struct FrameLabels {
    int frames = 0;
    int maxActive = 2;
    QVector<qint32> ids;             // frames x maxActive

    int at(int f, int k) const { return ids[f * maxActive + k]; }
    bool has(int f, int id) const;
    int count(int f) const;
};

// Step 4. `totalFrames` = globalFrameCount (frames past it are dropped).
FrameLabels aggregate(const QVector<WindowActivity>& windows, const StitchResult& stitch, int totalFrames,
                      int maxActive);

// A span of one speaker's speech, in seconds.
struct Span {
    double start = 0;
    double end = 0;
    double duration() const { return end - start; }
};

// Step 5 on one speaker's spans (sorted, non-overlapping): bridge gaps < minOff, then drop spans < minOn.
QVector<Span> applyMinDuration(QVector<Span> spans, double minOn, double minOff);

struct Turn {
    double start = 0;                // seconds, track (file) time
    double end = 0;
    int speaker = 0;                 // stitched id (local, not global)
    bool overlap = false;            // another turn overlaps (part of) this one
};

// The final frame labels -> turns: runs of each speaker (start = frame time of the first frame, end =
// frame time of the frame after the run, as sherpa-onnx does), post-processed, overlap-flagged, sorted
// by start (then speaker).
QVector<Turn> labelsToTurns(const FrameLabels& labels, const ModelInfo& info, double minOn, double minOff);
// Marks every turn that intersects a turn of another speaker (in place).
void flagOverlaps(QVector<Turn>& turns);

// The turns of ONE window's local speakers (D4 picks the embedding spans from them): runs of each
// local speaker in the window's 0/1 activity, in seconds of track time; overlap = another local
// speaker is active on some frame of the run. No min-duration post-processing.
struct LocalTurn {
    int window = 0;
    int localSpeaker = 0;
    int startFrame = 0;              // global frames, [startFrame, endFrame)
    int endFrame = 0;
    double start = 0;
    double end = 0;
    bool overlap = false;
};
QVector<LocalTurn> windowTurns(int windowIndex, const WindowActivity& w, const ModelInfo& info);

} // namespace tanara::processing::segmentation

#pragma once
//
// Cross-track leakage (plan 4.2/3, D6): without headphones a remote voice from the loudspeaker also
// reaches the microphone; on some setups the user's own voice comes back on the loopback as an echo.
// Such a leaked copy must not become a speaker of the track it leaked into.
//
// Pure functions over 16 kHz mono float PCM and speech turns (no ONNX Runtime, no I/O):
//
//   * frame levels: RMS dBFS per frameMs frame (frameDb);
//   * a track's SPEECH LEVEL: the speechPercentile (0.9) percentile of the frame levels inside its own
//     speech turns (the SideAnalysis idea: the level is relative to the track's own loud speech, so the
//     mic gain and the loopback volume do not matter);
//   * a speech turn of track A is a leakage candidate from track B when
//       - at least minCoverage of it is covered by B's speech turns (time overlap in meeting time),
//       - A's level over the turn is at least leakDb below A's speech level (a quiet copy), and
//       - B's level over the covered part is within sourceDb of B's speech level (B really talks), and
//       - B is louder than A, relative to their own speech levels, by at least marginDb.
//     Real double-talk is loud on its own track, so it is kept.
//
// Nothing is assumed from the track kind (no "mic = owner"): the rule runs for every ordered pair of
// tracks; a single-track meeting has no candidate. A track whose speech is ALL leakage (e.g. a muted
// mic that only hears the loudspeaker) keeps its speech: its own speech level is then the leaked level.
//
// Unit-tested with synthetic PCM (test_track_leakage).
#include <QString>
#include <QVector>

namespace tanara::processing::leakage {

struct Options {
    int    frameMs = 50;
    double speechPercentile = 0.90;
    double minCoverage = 0.8;        // share of the turn covered by the source track's speech
    float  leakDb = -12.0f;          // the turn's level vs. its own track's speech level: at most this
    float  sourceDb = -10.0f;        // the source's level vs. its speech level: at least this
    float  marginDb = 6.0f;          // source relative level - turn relative level: at least this
};

struct Turn {
    double start = 0;                // seconds, TRACK (file) time
    double end = 0;
};

// One track's input.
struct TrackInput {
    QString trackId;
    qint64 startOffsetMs = 0;        // track time -> meeting time
    QVector<float> frameDb;          // frameDb(pcm, frameMs), track time
    QVector<Turn> speech;            // the track's speech turns (segmentation), track time
};

// Frame RMS levels in dBFS (silence clamped to -100 dB). 16 kHz.
QVector<float> frameDb(const QVector<float>& pcm16k, int frameMs, int sampleRate = 16000);

// The track's speech level (dBFS): the percentile of the frame levels inside its speech turns; -100 if none.
float speechLevel(const QVector<float>& frameDb, const QVector<Turn>& speech, int frameMs, double percentile);

// Energy-mean level (dBFS) of the frames overlapping [start, end) in track time; -100 if none.
float meanLevel(const QVector<float>& frameDb, double start, double end, int frameMs);

struct Leak {
    int turn = 0;                    // index into the track's speech turns
    Turn span;                       // track time
    QString sourceTrackId;
    float levelDb = 0;               // the turn's level vs. its track's speech level
    float sourceLevelDb = 0;         // the source's level vs. its speech level
};

// Per track (same order as `tracks`): the leaked speech turns.
QVector<QVector<Leak>> detect(const QVector<TrackInput>& tracks, const Options& opts = {});

} // namespace tanara::processing::leakage

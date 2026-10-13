#pragma once
//
// Per-track audio activity (an energy time series) for the track-side check (SideAnalysis.h).
//
// Per logical track (a device's spans together, tracknames::segments) one fixed-frame-length
// (50 ms by default) dB time series in MEETING time: per frame the RMS dB above the track's own noise
// floor. The track files are decoded once to 16 kHz mono s16 (one ffmpeg run per file, with streaming
// processing; the PCM does not stay in memory). Gaps between spans (and the part before the track's start /
// after its end) are NaN: the track has no data there.
//
// The decoder is replaceable (PcmDecoder); unit tests compute with a fake decoder.
// The cache is in the meeting folder: tracks.activity.bin (tied to the track files' fingerprint).
#include "tanara/Types.h"

#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace tanara {

struct TrackActivity {
    QString trackId;                // id of the logical track's first span
    TrackKind kind = TrackKind::Other;
    int frameMs = 50;
    qint64 originMs = 0;            // start of the first frame in MEETING time
    QVector<float> dbAboveFloor;    // per frame >= 0; NaN = no data (a gap)
    float floorDb = 0.0f;           // estimated noise floor (10th percentile of the frame dBs), dBFS

    // How many frames carry data (not NaN).
    int coveredFrames() const;
    qint64 coveredMs() const { return qint64(coveredFrames()) * frameMs; }
    qint64 endMs() const { return originMs + qint64(dbAboveFloor.size()) * frameMs; }
};

struct MeetingActivity {
    QString fingerprint;            // activityFingerprint(): if it changes, we recompute
    QVector<TrackActivity> tracks;

    bool isEmpty() const { return tracks.isEmpty(); }
    static QString filePath(const QString& meetingFolder);   // tracks.activity.bin
    // The display cache (speech strip): of EVERY track, including those left out of the mixdown (for the
    // keep / drop decision). See AppController::trackActivity.
    static QString viewFilePath(const QString& meetingFolder);   // tracks.activity.view.bin
    // An empty result (filled with the fingerprint, with no tracks) if there is no file / the fingerprint
    // differs / the format is unknown. path: the cache file (empty = filePath(meetingFolder)).
    static MeetingActivity load(const QString& meetingFolder, const QString& fingerprint,
                                const QString& path = {});
    bool save(const QString& meetingFolder, const QString& path = {}) const;
};

namespace trackactivity {

inline constexpr int kSampleRate = 16000;
inline constexpr int kDefaultFrameMs = 50;
// A frame quieter than this (digital silence) is clamped here so the log does not go to -inf.
inline constexpr float kMinDb = -90.0f;
// The noise floor is this percentile of the frame dBs.
inline constexpr double kFloorPercentile = 0.10;

// The decoded PCM arrives continuously (16 kHz mono s16): the decoder calls this piece by piece.
using PcmSink = std::function<void(const qint16* samples, qsizetype count)>;
// Decoding one file into the sink. false = failed (*error filled, if not null).
using PcmDecoder = std::function<bool(const QString& absPath, const PcmSink& sink, QString* error)>;

// The real decoder: ffmpeg -> s16le 16 kHz mono, QProcess, continuous reading.
PcmDecoder ffmpegDecoder(const QString& ffmpegPath = QStringLiteral("ffmpeg"));

// Frame RMS -> dBFS (clamped to kMinDb). The pure, chunking-independent computation (for tests too).
QVector<float> frameDb(const QVector<qint16>& pcm, int frameMs);

// The noise floor: the kFloorPercentile percentile of the non-NaN values; empty -> kMinDb.
float estimateFloor(const QVector<float>& db);

// Fingerprint of the track files: per active track file name + size + mtime + offset, and frameMs.
QString activityFingerprint(const Meeting& m, const QString& folder, int frameMs = kDefaultFrameMs);

// Energy of a time window on a track: the mean LINEAR power of the covered (non-NaN) frames, in dB above
// the noise floor (10*log10(mean(10^(d/10)))). A frame counts if it overlaps the window. No covered
// frame -> nullopt.
std::optional<float> windowEnergyDb(const TrackActivity& t, qint64 startMs, qint64 endMs);

} // namespace trackactivity

// Activity of the meeting's active tracks. One TrackActivity per logical track; an inactive span is
// left out, a missing / undecodable file is left out (logged). Empty decoder = ffmpegDecoder(ffmpegPath).
MeetingActivity computeMeetingActivity(const Meeting& m, const QString& folder,
                                       const QString& ffmpegPath = QStringLiteral("ffmpeg"),
                                       int frameMs = trackactivity::kDefaultFrameMs,
                                       const trackactivity::PcmDecoder& decoder = {});

using trackactivity::windowEnergyDb;

} // namespace tanara

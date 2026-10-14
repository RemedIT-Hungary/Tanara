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
// The decoding goes through the media port (media::MediaTool); unit tests compute with a fake decoder
// or a fake tool.
// The cache is in the meeting folder: tracks.activity.bin (tied to the track files' fingerprint).
// The value types (TrackActivity, MeetingActivity) are in domain/Activity.h.
#include "tanara/domain/Activity.h"
#include "tanara/domain/Meeting.h"
#include "tanara/media/MediaTool.h"

#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace tanara::processing {

namespace trackactivity {

inline constexpr int kSampleRate = 16000;
inline constexpr int kDefaultFrameMs = 50;
// A frame quieter than this (digital silence) is clamped here so the log does not go to -inf.
inline constexpr float kMinDb = -90.0f;
// The noise floor is this percentile of the frame dBs.
inline constexpr double kFloorPercentile = 0.10;

// The decoder types of the media port (16 kHz mono s16 here: media::MediaTool::monoS16Decoder).
using PcmSink = media::PcmSink;
using PcmDecoder = media::PcmDecoder;

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
// left out, a missing / undecodable file is left out (logged). Decoded by the media tool (16 kHz mono s16).
MeetingActivity computeMeetingActivity(const Meeting& m, const QString& folder, const media::MediaTool& tool,
                                       int frameMs = trackactivity::kDefaultFrameMs);
// The same with an explicit decoder (tests).
MeetingActivity computeMeetingActivity(const Meeting& m, const QString& folder,
                                       const trackactivity::PcmDecoder& decoder,
                                       int frameMs = trackactivity::kDefaultFrameMs);

// The track activity of the side check: the tracks.activity.bin cache; if absent and there are both an
// active mic and an active loopback track, computed (with the tool) and saved. Without a mic + loopback
// pair: empty. Blocking (call it on a background thread).
MeetingActivity loadOrComputeSideActivity(const Meeting& m, const media::MediaTool& tool);

using trackactivity::windowEnergyDb;

} // namespace tanara::processing
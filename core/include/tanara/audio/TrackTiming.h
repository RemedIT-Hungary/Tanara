#pragma once
//
// Track timing: meeting time <-> track-file time, and filling capture gaps.
//
// A track's file contains the span ACTUALLY recorded: if the track started later (a device enabled
// during recording, the second span of a device switched on and off), the file's zero point is at the
// meeting's `Track::startOffsetMs`. Every time range the program stores (transcript timestamps,
// the "file#start-end" references of voiceprint samples) is meant in MEETING time; before reading from
// a track file it must be converted to file time (meetingToFileRange).
#include "tanara/Types.h"

#include <QtGlobal>

namespace tanara::tracktiming {

// A time range within a file (ms). Invalid (valid() == false) if the requested meeting range falls
// entirely before the track's start, or is empty.
struct FileRange {
    qint64 startMs = -1;
    qint64 endMs = -1;
    bool valid() const { return startMs >= 0 && endMs > startMs; }
};

// Meeting time [startMs, endMs) -> the track file's time. The part before the track's start is cut
// off (there is no audio before the file's zero point); if fileDurationMs >= 0, the part after the
// file's end too. If nothing remains, it returns an invalid range.
FileRange meetingToFileRange(qint64 offsetMs, qint64 startMs, qint64 endMs,
                             qint64 fileDurationMs = -1);

// The same for a track (with its startOffsetMs).
inline FileRange fileRange(const Track& track, qint64 startMs, qint64 endMs,
                           qint64 fileDurationMs = -1)
{
    return meetingToFileRange(track.startOffsetMs, startMs, endMs, fileDurationMs);
}

// ---- filling capture gaps --------------------------------------------------------------
//
// Windows WASAPI loopback (system audio) delivers NO packet while nothing plays on the output: miniaudio
// just keeps waiting (see miniaudio.h, ma_device_read__wasapi: "Keep waiting in loopback mode"), the
// callback does not run, nothing goes into the file. The silent stretches would be missing, the track
// would be shorter and the audio would "slip ahead". So for such tracks the recorder measures the number
// of frames received against the wall clock and fills the shortfall with silence.

// How many frames of silence are missing: the difference between the frames expected by the wall clock
// (expected) and the frames actually written (delivered), if it exceeds the tolerance (tolerance: normal
// buffering latency must not trigger an insertion); otherwise 0. Never negative.
qint64 framesOwed(qint64 expectedFrames, qint64 deliveredFrames, qint64 toleranceFrames);

// Number of frames expected so far by the wall clock (over elapsedMs, at sampleRate).
inline qint64 expectedFrames(qint64 elapsedMs, int sampleRate)
{
    return elapsedMs <= 0 ? 0 : elapsedMs * sampleRate / 1000;
}

// When to fill: only if the device has delivered no data for at least kGapIdleMs (the loopback is really
// "silent"), so no silence ever goes into the audio during speech, and the slow drift between the wall
// clock and the device clock corrects itself in the quiet stretches. After filling, the file lags the wall
// clock by at most kGapToleranceMs.
constexpr qint64 kGapIdleMs = 100;
constexpr qint64 kGapToleranceMs = 20;

// The silence to insert now, in frames: 0 if the device has been quiet for less than kGapIdleMs;
// otherwise framesOwed(frames expected since the track opened, frames written, kGapToleranceMs).
qint64 silenceToInsert(qint64 elapsedSinceOpenMs, qint64 idleMs, qint64 deliveredFrames,
                       int sampleRate);

// Whether gaps must be filled for a track of the given kind. For a loopback track on every platform:
// Windows loopback silently delivers no samples; a Linux PipeWire/PulseAudio monitor delivers even
// silence, BUT if the watched output's stream breaks (the call app switches device, reconnects), it sends
// nothing for minutes. That is how 57 s went missing from the middle of the call track in a recording
// (2026-10-07), the file got shorter and the transcript slipped from its middle. A microphone always delivers samples.
constexpr bool fillsCaptureGaps(TrackKind kind)
{
    return kind == TrackKind::Loopback;
}

} // namespace tanara::tracktiming

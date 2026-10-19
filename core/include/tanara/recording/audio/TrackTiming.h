#pragma once
//
// Track timing: meeting time <-> track-file time, and filling capture gaps.
//
// A track's file contains the span ACTUALLY recorded: if the track started later (a device enabled
// during recording, the second span of a device switched on and off), the file's zero point is at the
// meeting's `Track::startOffsetMs`. Every time range the program stores (transcript timestamps,
// the "file#start-end" references of voiceprint samples) is meant in MEETING time; before reading from
// a track file it must be converted to file time (meetingToFileRange).
#include "tanara/domain/Audio.h"
#include "tanara/domain/Meeting.h"

#include <QtGlobal>

namespace tanara::recording::tracktiming {

// FileRange, meetingToFileRange and fileRange (meeting time -> file time) are in domain/Meeting.h.

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

// ---- stalled capture (live warning) ----------------------------------------------------------
//
// "No data" is not "silence with data": a working microphone always delivers samples, even when the
// room is quiet (that is the level-based silence warning of the recorder UI). A capture track that has
// delivered NO samples at all for kStallWarnMs is stalled: the device or its stream is gone (e.g. the
// machine slept and the audio server dropped the stream), and the recording is losing that track
// while it seems to run. The recorder warns live about it.
constexpr qint64 kStallWarnMs = 5000;
// Which tracks are watched is per platform: not those whose device delivers nothing while idle
// (ISystemAudioSource::deliversNothingWhenIdle: Windows WASAPI loopback, see above).

// Whether the track is stalled: no data for at least thresholdMs (idleMs = now - last data).
constexpr bool isStalled(qint64 idleMs, qint64 thresholdMs = kStallWarnMs)
{
    return idleMs >= thresholdMs;
}

// ---- sleep and lost devices (resume in a new segment) ---------------------------------------
//
// The recording clock is monotonic; on Linux CLOCK_MONOTONIC does not advance while the machine is
// suspended, so a sleep is invisible to it. The drain worker compares, tick by tick, how far the wall
// clock moved with how far the monotonic clock moved: a wall-clock lead of more than kSleepJumpMs
// means the machine slept (or the clock was set forward; both are logged). The recording clock then
// takes the jump over (monotonic + the slept time), so later segments sit at their wall-clock place.
constexpr qint64 kSleepJumpMs = 2000;

// How much the wall clock ran ahead of the monotonic clock between two ticks, if it counts as a
// sleep; 0 otherwise.
constexpr qint64 sleepJumpMs(qint64 wallDeltaMs, qint64 monotonicDeltaMs, qint64 thresholdMs = kSleepJumpMs)
{
    return wallDeltaMs - monotonicDeltaMs > thresholdMs ? wallDeltaMs - monotonicDeltaMs : 0;
}

// A capture track lost (device invalidated / stream gone) or cut by a sleep is re-opened: every
// kReopenPollMs the session re-enumerates the devices and opens the same one (by id, then by name),
// for at most kReopenWindowMs; then it gives up with an error.
constexpr qint64 kReopenPollMs = 1000;
constexpr qint64 kReopenWindowMs = 10 * 60 * 1000;

// Gap reasons (RecordingGap::reason).
inline constexpr char kGapSleep[] = "sleep";
inline constexpr char kGapDeviceLost[] = "device-lost";

} // namespace tanara::recording::tracktiming
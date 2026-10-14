#pragma once
//
// tanara_demo: fictional, deterministic speech activity for the speech strips (TrackActivityStrip)
// of the demo's tracks: mic, call, system, mic2 over a 30:34 meeting. The frames are core
// TrackActivity values (dB above the floor), so the strip's view model processes them exactly like
// a real measurement.
#include "tanara/domain/Activity.h"
#include "tanara/domain/Meeting.h"

namespace tanara_demo {

// The fictional meeting (its tracks and length); the id is any: the demo ignores it.
tanara::Meeting trackActivityMeeting();
// The four tracks' activity frames (500 ms frames).
tanara::MeetingActivity trackActivityFrames();

} // namespace tanara_demo

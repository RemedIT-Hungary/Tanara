#pragma once
//
// The two defaults of a speaker-editor session that come from the processing module (the editing module
// must not include it): the tracks.activity.bin cache reader as the session's fallback track-activity
// source, and the voice models' display names for the recheck feedback. app::Services installs them on
// every session of editing::EditorSessions; code that builds a session by hand (tests) may call it too.
namespace tanara::editing {
class RecheckService;
class SpeakerEditSession;
}

namespace tanara::app {

void installEditorDefaults(editing::SpeakerEditSession& session, editing::RecheckService& recheck);

} // namespace tanara::app

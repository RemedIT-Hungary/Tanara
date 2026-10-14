#include "tanara/app/EditorDefaults.h"

#include "tanara/editing/recheck/RecheckService.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

namespace tanara::app {

namespace {

// The default track-activity source (on a background thread): only the tracks.activity.bin cache. Computing
// a missing one needs the media tool: the owner installs that provider (setActivityProvider, e.g.
// processing::loadOrComputeSideActivity in app::Services).
MeetingActivity cachedActivity(const Meeting& m)
{
    if (m.folder.isEmpty()) return {};
    bool mic = false, loop = false;
    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        mic |= t.kind == TrackKind::Mic;
        loop |= t.kind == TrackKind::Loopback;
    }
    if (!mic || !loop) return {};
    return MeetingActivity::load(m.folder, processing::trackactivity::activityFingerprint(m, m.folder));
}

QString modelDisplayName(const QString& id)
{
    const auto spec = processing::VoiceModelRegistry::spec(id);
    return spec ? spec->displayName : QString();
}

} // namespace

void installEditorDefaults(editing::SpeakerEditSession& session, editing::RecheckService& recheck)
{
    session.setDefaultActivityProvider(cachedActivity);
    recheck.setModelNameResolver(modelDisplayName);
}

} // namespace tanara::app

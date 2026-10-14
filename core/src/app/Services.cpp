#include "tanara/app/Services.h"

#include "tanara/app/SettingsManager.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/library/import/ImportService.h"
#include "tanara/library/store/ArchiveService.h"
#include "tanara/recording/MixdownService.h"
#include "tanara/recording/RecordingService.h"
#include "tanara/watcher/CallMonitor.h"
// --- P16: detector registry ---
#include "tanara/watcher/detect/DetectorRegistry.h"
// --- end P16 ---

namespace tanara::app {

namespace {

recording::RecordingService::Deps recordingDeps(const ServicesDeps& deps, watcher::CallMonitor* calls)
{
    recording::RecordingService::Deps rd;
    rd.store = deps.store;
    rd.tags = deps.tags;
    rd.media = deps.media;
    rd.callMonitor = calls;
    if (SettingsManager* st = deps.settings) {
        const paths::AppPaths& ap = st->paths();
        rd.audioDir = ap.primaryRecordingRoot();
        rd.statePath = ap.stateFile(paths::files::State);
        rd.config = [st]() {
            const AppSettings s = st->settings();
            return recording::RecordingService::Config{s.recording, s.watcher};
        };
    }
    return rd;
}

} // namespace

Services::Services(const ServicesDeps& deps)
    : m_archive(std::make_unique<library::ArchiveService>(deps.store, deps.tags, deps.jobs))
    , m_import(std::make_unique<library::ImportService>(deps.store, deps.media, deps.jobs))
    , m_callMonitor(std::make_unique<watcher::CallMonitor>())
    , m_recording(std::make_unique<recording::RecordingService>(recordingDeps(deps, m_callMonitor.get())))
    , m_mixdown(std::make_unique<recording::MixdownService>(deps.store, deps.jobs, deps.media))
    // --- P16: detector registry ---
    , m_detectors(std::make_unique<watcher::MeetingDetectorRegistry>(watcher::MeetingDetectorRegistry::withBuiltins()))
    // --- end P16 ---
{
    // --- P16: detector registry ---
    m_callMonitor->setDetectorRegistry(m_detectors.get());
    // --- end P16 ---
}

Services::~Services()
{
    // --- P16: detector registry ---
    // The registry is the last member, so it is destroyed first: unhook it from the call monitor.
    m_callMonitor->setDetectorRegistry(nullptr);
    // --- end P16 ---
}

} // namespace tanara::app

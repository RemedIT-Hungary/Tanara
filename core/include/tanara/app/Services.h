#pragma once
//
// app::Services: the composition root of the services split out of AppController (strangler, P12).
// It creates the services from their dependencies (constructor injection) and owns them; the
// AppController owns the Services and hands it out through services().
#include "tanara/media/MediaTool.h"

#include <memory>

namespace tanara::library {
class ArchiveService;
class ImportService;
class MeetingJobTracker;
class MeetingRepository;
class TagService;
}
namespace tanara::recording {
class MixdownService;
class RecordingService;
}
namespace tanara::watcher { class CallMonitor; }
// --- P16: detector registry ---
namespace tanara::watcher { class MeetingDetectorRegistry; }
// --- end P16 ---

namespace tanara::app {

class SettingsManager;

struct ServicesDeps {
    library::MeetingRepository* store = nullptr;
    library::TagService*        tags = nullptr;
    library::MeetingJobTracker* jobs = nullptr;
    media::MediaToolPtr         media;
    SettingsManager*            settings = nullptr;   // settings + AppPaths (recording root, state.json)
};

class Services {
public:
    explicit Services(const ServicesDeps& deps);
    ~Services();
    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;

    library::ArchiveService&     archive() const     { return *m_archive; }
    library::ImportService&      import() const      { return *m_import; }
    watcher::CallMonitor&        callMonitor() const { return *m_callMonitor; }
    recording::RecordingService& recording() const   { return *m_recording; }
    recording::MixdownService&   mixdown() const     { return *m_mixdown; }
    // --- P16: detector registry ---
    // This platform's meeting detectors (an instance, not a singleton; the call monitor uses it).
    watcher::MeetingDetectorRegistry& detectors() const { return *m_detectors; }
    // --- end P16 ---

private:
    std::unique_ptr<library::ArchiveService>     m_archive;
    std::unique_ptr<library::ImportService>      m_import;
    std::unique_ptr<watcher::CallMonitor>        m_callMonitor;   // before the recording (it subscribes)
    std::unique_ptr<recording::RecordingService> m_recording;
    std::unique_ptr<recording::MixdownService>   m_mixdown;
    // --- P16: detector registry ---
    std::unique_ptr<watcher::MeetingDetectorRegistry> m_detectors;
    // --- end P16 ---
};

} // namespace tanara::app

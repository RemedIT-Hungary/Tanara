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

namespace tanara::app {

struct ServicesDeps {
    library::MeetingRepository* store = nullptr;
    library::TagService*        tags = nullptr;
    library::MeetingJobTracker* jobs = nullptr;
    media::MediaToolPtr         media;
};

class Services {
public:
    explicit Services(const ServicesDeps& deps);
    ~Services();
    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;

    library::ArchiveService& archive() const { return *m_archive; }
    library::ImportService&  import() const  { return *m_import; }

private:
    std::unique_ptr<library::ArchiveService> m_archive;
    std::unique_ptr<library::ImportService>  m_import;
};

} // namespace tanara::app

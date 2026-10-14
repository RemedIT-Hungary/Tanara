#include "tanara/app/Services.h"

#include "tanara/library/import/ImportService.h"
#include "tanara/library/store/ArchiveService.h"

namespace tanara::app {

Services::Services(const ServicesDeps& deps)
    : m_archive(std::make_unique<library::ArchiveService>(deps.store, deps.tags, deps.jobs))
    , m_import(std::make_unique<library::ImportService>(deps.store, deps.media, deps.jobs))
{
}

Services::~Services() = default;

} // namespace tanara::app

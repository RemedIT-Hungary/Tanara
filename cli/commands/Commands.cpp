#include "Commands.h"

namespace tanara::cli {

void registerAllCommands(CommandRegistry& registry)
{
    registerRecordingCommands(registry);
    registerWatcherCommands(registry);
    registerLibraryCommands(registry);
    registerProcessingCommands(registry);
    registerEditingCommands(registry);
    registerPeopleCommands(registry);
    registerCloudCommands(registry);
    registerMigrateCommands(registry);
}

} // namespace tanara::cli

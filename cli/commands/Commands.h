#pragma once
//
// The per-module registration functions of tanara-cli (cli/commands/<module>.cpp).
#include "CommandRegistry.h"

namespace tanara::cli {

void registerRecordingCommands(CommandRegistry& registry);
void registerWatcherCommands(CommandRegistry& registry);
void registerLibraryCommands(CommandRegistry& registry);
void registerProcessingCommands(CommandRegistry& registry);
void registerEditingCommands(CommandRegistry& registry);
void registerPeopleCommands(CommandRegistry& registry);
void registerCloudCommands(CommandRegistry& registry);
void registerMigrateCommands(CommandRegistry& registry);

// Every module, in help order.
void registerAllCommands(CommandRegistry& registry);

// Shared by `mixdown` and `align`: remixes mixdown.mp3 of the meeting from its active tracks and
// waits for the result (event loop). 0 = regenerated, 1 = failed / no such meeting.
// TODO(P12b): runs on recording::MixdownService once that is on main.
int regenerateMixdownAndWait(CliContext& ctx, const QString& meetingId, const QString& doneNote);

} // namespace tanara::cli

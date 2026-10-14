// tanara-cli, migrate module (temporary): one-time converters of the stored data formats.
#include "Commands.h"
#include "MigrateCommands.h"
#include "PathsCommands.h"

namespace tanara::cli {

// Run BEFORE any AppController exists: it would read the data these commands convert.
void registerMigrateCommands(CommandRegistry& r)
{
    const QString m = QStringLiteral("migrate");
    r.add({"migrate", m, "One-time: name-keyed people to PersonIds, with a backup",
           "migrate person-ids [--dry-run] [--backup-dir <dir>]", false,
           [](CliContext&, const QStringList& a) { return runMigrateCommand(a); }});
    r.add({"settings", m, "One-time: the old flat settings.json to the v2 sections",
           "settings import-legacy [--from <file>] [--dry-run]", false,
           [](CliContext&, const QStringList& a) { return runSettingsCommand(a); }});
}

} // namespace tanara::cli

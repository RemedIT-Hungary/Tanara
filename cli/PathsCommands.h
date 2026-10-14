#pragma once
// tanara-cli paths show | paths adopt-legacy | settings import-legacy
//
// One-time helpers of the folder / settings format change (cleanup phase 2, P6). Run WITHOUT
// AppController, and `show` / `--dry-run` write nothing. Output is English.
#include <QStringList>

namespace tanara::cli {

// args[1] == "paths": "show" | "adopt-legacy [--from <dir>] [--dry-run]".
int runPathsCommand(const QStringList& args);
// args[1] == "settings": "import-legacy [--from <settings.json>] [--dry-run]".
int runSettingsCommand(const QStringList& args);

} // namespace tanara::cli

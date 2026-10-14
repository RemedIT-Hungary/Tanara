#pragma once
// tanara-cli reslice: re-pads the lines of an existing Soniox transcript from
// transcript.tokens.json (grouping unchanged; tanara/edit/TranscriptReslice.h). Runs WITHOUT
// AppController. --dry-run prints only the differences. Output is English and contains no
// transcript text.
#include <QStringList>

namespace tanara::cli {

// args[1] == "reslice".
int runResliceCommand(const QStringList& args);

} // namespace tanara::cli

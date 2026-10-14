#pragma once
// tanara-cli track-sides: measures the track side check on a meeting folder (SideAnalysis).
// Runs WITHOUT AppController and only READS: tracks.activity.bin is written to the folder only
// with --write-cache. Output is English and contains no transcript text.
#include <QStringList>

namespace tanara::cli {

// args[1] == "track-sides".
int runTrackSidesCommand(const QStringList& args);

} // namespace tanara::cli

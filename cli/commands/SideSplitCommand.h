#pragma once
// tanara-cli side-split: cuts the lines of an existing transcript where the talker moved between the mic
// and the loopback side (tanara/editing/review/SideSplit.h, tanara/editing/session/TranscriptReslice.h),
// carrying the line ids, overlay entries and summary references over. Runs WITHOUT AppController. A dry
// run by default; --apply writes. Output is English and contains no transcript text.
#include <QStringList>

namespace tanara::cli {

// args[1] == "side-split".
int runSideSplitCommand(const QStringList& args);

} // namespace tanara::cli

#pragma once
// tanara-cli diarize-probe: runs the speaker segmentation (the first step of the local diarization)
// on one audio file and prints the turns. A developer / manual check: no meeting is touched, no
// AppController is built. Output is English.
#include <QStringList>

namespace tanara::cli {

// args[1] == "diarize-probe".
int runDiarizeProbeCommand(const QStringList& args);

} // namespace tanara::cli

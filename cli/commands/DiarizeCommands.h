#pragma once
// tanara-cli diarize-probe / diarize-ref / diarize-eval. diarize-probe runs the speaker segmentation (the first step of the local diarization)
// on one audio file and prints the turns. A developer / manual check: no meeting is touched, no
// AppController is built. Output is English.
#include <QStringList>

namespace tanara::cli {

// args[1] == "diarize-probe".
int runDiarizeProbeCommand(const QStringList& args);
// args[1] == "diarize-ref" (DiarizeEvalCommands.cpp): a reference from a meeting's provider transcript.
int runDiarizeRefCommand(const QStringList& args);
// args[1] == "diarize-eval" (DiarizeEvalCommands.cpp): DER + speaker-count error against a reference.
int runDiarizeEvalCommand(const QStringList& args);

} // namespace tanara::cli

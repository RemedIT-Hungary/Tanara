#pragma once
// tanara-cli voice-models / voice-eval: managing and comparing the speaker models.
// Run WITHOUT AppController (settings + model files + reading the meeting folder only), so the
// app's background work (e.g. voiceprint backfill) does not start. Output is English.
#include <QStringList>

namespace tanara::cli {

// args: the full command line (args[1] == "voice-models").
int runVoiceModelsCommand(const QStringList& args);
// args[1] == "voice-eval".
int runVoiceEvalCommand(const QStringList& args);

} // namespace tanara::cli

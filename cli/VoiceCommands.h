#pragma once
// tanara-cli voice-models / voice-eval — a beszélő-modellek kezelése és összevetése.
// AppController NÉLKÜL futnak (csak beállítás + modellfájlok + a meeting-mappa olvasása), így
// nem indul el az alkalmazás háttérmunkája (pl. a lenyomat-pótlás). A kimenet ANGOL.
#include <QStringList>

namespace tanara::cli {

// args: a teljes parancssor (args[1] == "voice-models").
int runVoiceModelsCommand(const QStringList& args);
// args[1] == "voice-eval".
int runVoiceEvalCommand(const QStringList& args);

} // namespace tanara::cli

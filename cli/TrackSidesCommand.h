#pragma once
// tanara-cli track-sides — a sáv-oldal ellenőrzés mérése egy meeting-mappán (SideAnalysis).
// AppController NÉLKÜL fut, és CSAK OLVAS: a tracks.activity.bin cache-t csak --write-cache-sel
// írja a mappába. A kimenet ANGOL, átirat-szöveget nem ír ki.
#include <QStringList>

namespace tanara::cli {

// args[1] == "track-sides".
int runTrackSidesCommand(const QStringList& args);

} // namespace tanara::cli

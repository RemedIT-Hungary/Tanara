#pragma once
// tanara-cli reslice — egy meglévő átirat újraszeletelése a transcript.tokens.json-ból a Soniox
// token-időzítés korrekciójával (tanara/edit/TranscriptReslice.h). AppController NÉLKÜL fut.
// --dry-run: csak a különbségeket írja ki. A kimenet ANGOL, átirat-szöveget nem ír ki.
#include <QStringList>

namespace tanara::cli {

// args[1] == "reslice".
int runResliceCommand(const QStringList& args);

} // namespace tanara::cli

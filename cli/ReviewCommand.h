#pragma once
// tanara-cli review — az Átnézendő csoportok és a beszélő-jelöltek bizonyítékokkal egy meeting-
// mappán (ReviewGroups / CandidateRanker). AppController NÉLKÜL fut, és CSAK OLVAS (a sáv-
// aktivitást, ha nincs cache, a memóriában számolja). A kimenet ANGOL, átirat-szöveget nem ír ki.
#include <QStringList>

namespace tanara::cli {

// args[1] == "review".
int runReviewCommand(const QStringList& args);

} // namespace tanara::cli

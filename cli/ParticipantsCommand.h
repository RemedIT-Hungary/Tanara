#pragma once
// tanara-cli participants — egy meeting-mappa résztvevői és jelöltjei bizonyítékokkal
// (ParticipantAnalysis). AppController NÉLKÜL fut; alapból CSAK OLVAS. --analyze lefuttatja az
// átirat előtti hangelemzést (lokálisan, Soniox nélkül); a mappába csak --write-tal ír
// (meeting.json, participants.analysis.json, tracks.activity.bin). A kimenet ANGOL.
#include <QStringList>

namespace tanara::cli {

// args[1] == "participants".
int runParticipantsCommand(const QStringList& args);

} // namespace tanara::cli

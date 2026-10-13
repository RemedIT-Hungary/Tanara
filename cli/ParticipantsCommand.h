#pragma once
// tanara-cli participants: a meeting folder's participants and candidates with evidence
// (ParticipantAnalysis). Runs WITHOUT AppController and only READS by default. --analyze runs
// the pre-transcript audio analysis (locally, no Soniox); it writes to the folder only with
// --write (meeting.json, participants.analysis.json, tracks.activity.bin). Output is English.
#include <QStringList>

namespace tanara::cli {

// args[1] == "participants".
int runParticipantsCommand(const QStringList& args);

} // namespace tanara::cli

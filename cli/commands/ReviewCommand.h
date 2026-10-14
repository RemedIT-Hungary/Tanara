#pragma once
// tanara-cli review: the review queue groups and speaker candidates with evidence for a
// meeting folder (ReviewGroups / CandidateRanker). Runs WITHOUT AppController and only READS
// (track activity is computed in memory when there is no cache). Output is English and
// contains no transcript text.
#include <QStringList>

namespace tanara::cli {

// args[1] == "review".
int runReviewCommand(const QStringList& args);

// tanara-cli lines: a meeting folder's transcript lines with their markers (NO text), and
// manual setting of the "ignored" marker:
//   lines <meeting-folder> [--json]
//   lines ignore <meeting-folder> <utteranceId...>
//   lines unignore <meeting-folder> <utteranceId...>
// Listing only reads; ignore / unignore write transcript.speakers.json. args[1] == "lines".
int runLinesCommand(const QStringList& args);

} // namespace tanara::cli

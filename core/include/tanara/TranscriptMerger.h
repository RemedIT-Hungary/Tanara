#pragma once
//
// Merges per-track transcripts into one chronological, per-speaker transcript.
// The implementation of MergedTranscript::renderMarkdown() also lives here (in the .cpp).
#include "tanara/domain/Transcript.h"
#include <QVector>

namespace tanara {

// Concatenates the tokens of all tracks, sets trackId/speaker on every token from its
// source track, stable-sorts by startMs, and takes the language from the first track.
MergedTranscript mergeTranscripts(const QVector<TrackTranscript>& tracks);

// Markdown of the speech blocks: per paragraph `[mm:ss]` **Speaker** text, separated by a
// blank line. MergedTranscript::renderMarkdown() and the summary parts use this too.
QString renderUtterancesMarkdown(const QVector<Utterance>& utterances);

} // namespace tanara

#pragma once
//
// Handling of Soniox-style (emission-moment) timing: padding the finished LINES after the fact. Shared
// logic of TranscriptMerger (MergedTranscript::segments) and `tanara-cli reslice`.
// UI-independent, free functions, no QObject.
//
// Measurement (a real meeting, 2026-10): Soniox tokens are ~60 ms wide and mark the moment the token was
// EMITTED (toward the end of the word), not the start of the sound; e.g. "R" 127410-127470, "ok"
// 127530-127590, "i." 127650-127710. A line's start is thus 200-300 ms AFTER the actual sound start; for
// short lines most of the word drops out of playback and of the voice embedding too.
//
// Why lines and not tokens: token-level padding would also narrow the pauses between tokens, so the merger
// (1.5 s pause threshold) would merge what used to be separate lines (measured: 9 lines in one meeting).
// So grouping runs on the ORIGINAL token times, tokens.json stays untouched; only the finished lines'
// start / end move. Whisper-compatible provider word timestamps are real start/end pairs: this does not run on them.
#include "tanara/domain/Transcript.h"

#include <QVector>

namespace tanara::processing::sonioxtiming {

// A line's start moves earlier by at most this much (up to the previous line's end).
constexpr qint64 kLeadMs = 300;
// A line's end moves later by at most this much (up to the next line's start).
constexpr qint64 kTailMs = 100;
// Soniox-style (emission-moment) timing: the median of the token widths is at most this.
constexpr qint64 kNarrowMedianMs = 80;

// Padding of the finished lines (any speaker; the order by startMs stays unchanged):
//   startMs = max(latest end of the EARLIER-starting lines, startMs - kLeadMs), but not
//             later than the original, and >= 0;
//   endMs   = min(earliest (new) start of the LATER-starting lines, endMs + kTailMs), but not
//             earlier than the original.
// Lines with the same start do not constrain each other (they move by the same amount, so the id suffix stays).
// If the input lines do not overlap, neither do the output's; the order does not change.
void padUtterances(QVector<Utterance>& utterances);

// Median of the token widths (endMs - startMs); -1 for an empty list.
qint64 medianTokenWidthMs(const QVector<TranscriptToken>& tokens);

// Whether the timing is Soniox-style (median <= kNarrowMedianMs). False for an empty list.
bool looksLikeEmissionTiming(const QVector<TranscriptToken>& tokens);

} // namespace tanara::processing::sonioxtiming
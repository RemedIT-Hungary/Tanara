#pragma once
//
// Time formatting shared by every module (foundation). Pure functions, no translation: the
// translated, word-based durations ("12 min") stay in the UI layer.
//
// The variants are DELIBERATELY different (each keeps the format of the place it came from):
//  - timestamp: "MM:SS", minutes not capped at 59 (LLM prompts, transcript.md).
//  - clock:     "M:SS" / "H:MM:SS" (meeting time in the UI; padMinutes: "MM:SS").
//  - clockMs:   "MM:SS.d" with tenths of a second (CLI review listings).
//  - hms:       "H:MM:SS", hours always shown (CLI track-side listings).
//  - duration:  "MM:SS" / "H:MM:SS", rounded to the nearest second; "–" for a negative value
//               (job durations).
#include <QString>
#include <QtGlobal>

namespace tanara {
namespace timefmt {

// "MM:SS" (e.g. "75:03"). Negative input is not clamped (callers that need it clamp first).
QString timestamp(qint64 ms);

// "M:SS" below an hour ("MM:SS" with padMinutes), "H:MM:SS" from an hour. Negative = 0.
QString clock(qint64 ms, bool padMinutes = false);

// "MM:SS.d" (tenths of a second), minutes not capped. Negative input is not clamped.
QString clockMs(qint64 ms);

// "H:MM:SS" (hours always present). Negative input is not clamped.
QString hms(qint64 ms);

// Rounded to the nearest second: "MM:SS" below an hour, "H:MM:SS" from an hour; negative = "–".
QString duration(qint64 ms);

} // namespace timefmt
} // namespace tanara

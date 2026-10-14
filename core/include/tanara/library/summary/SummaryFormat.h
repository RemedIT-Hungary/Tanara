#pragma once
//
// SummaryFormat: the small vocabulary of the stored summary format (summary.json / summary.md),
// shared by the summary repository (reading back) and the summarizer (writing). Pure functions.
#include "tanara/domain/Summary.h"

#include <QString>

namespace tanara::library::summaryformat {

// Upper limit of open questions after the summary (a long list hurts, per measurement).
constexpr int kMaxOpenQuestions = 5;

// "mm:ss" / "h:mm:ss" (or "mmm:ss") -> ms; unparseable -> -1.
qint64 parseTimestamp(const QString& s);

// Statement kind <-> its stored name:
// "statement" | "decision" | "todo" | "openQuestion" | "intro" | "memoPoint"
QString kindToString(StatementKind k);
StatementKind kindFromString(const QString& s);

// Memo subsection: the canonical key from a subheading (English or Hungarian, accent- and
// case-insensitive, without decoration such as #, **, colon): "arguments" (Arguments), "open" (Unresolved /
// Open question), otherwise the title itself. Empty = "".
QString memoGroupKey(const QString& heading);
// The key's Hungarian caption for summary.md ("Elhangzott érvek", "Nyitott kérdés"; others: itself).
QString memoGroupLabel(const QString& key);

} // namespace tanara::library::summaryformat

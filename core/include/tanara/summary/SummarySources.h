#pragma once
//
// SummarySources: the quick summary's source references (PURE functions, no I/O and no LLM).
//
// At the end of every sentence of the executive summary, every decision and action item, the model puts a
// source marker: `[t=12:30]` (there may be several: `[t=12:30, t=14:05]`, a range: `[t=12:30-13:10]`), or
// an utterance id: `[u754000]`. json_schema is unreliable with a local model, hence plain text + marker. Here:
//   - extracting and stripping the markers (the stored summary.md / fields are marker-free),
//   - splitting the executive summary into sentences,
//   - the marked time -> the nearest utterance (+-kToleranceMs),
//   - completing the Summary: statements, sourceSpeakers, memo-section speakers,
//   - targeted staleness: which statements' source lines have a speaker that changed since.
#include "tanara/Types.h"
#include "tanara/edit/SpeakerEditTypes.h"

#include <QHash>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {
namespace summarysrc {

// Largest deviation between the marked time and the utterance.
constexpr qint64 kToleranceMs = 5000;

// One utterance of the transcript (for source resolution).
struct SourceLine {
    QString id;             // "u<startMs>" (transcript.segments.json)
    qint64  startMs = 0;
    qint64  endMs = 0;
    QString speakerKey;     // resolved speaker key
    QString speakerName;    // displayed name (as the LLM saw it)
};

// One item of a marker, raw: a time (range) or an utterance id.
struct SourceRef {
    qint64  startMs = -1;   // the marked time (second precision); -1 = not a time
    qint64  endMs = -1;     // for a range its end, otherwise = startMs
    QString utteranceId;    // the `[u123]` form
};

// A text (piece) without markers + the references found in it.
struct MarkedText {
    QString text;
    QVector<SourceRef> refs;
};

// Lines built from the speech blocks (if there is no transcript.segments.json): id = "u<startMs>",
// key = name = the block's speaker.
QVector<SourceLine> linesFromSegments(const QVector<Utterance>& segments);

// Extracting and stripping markers. The text's structure (line breaks) stays; the space before the
// marker and the space before punctuation after the marker disappear ("word [t=1:00]." -> "word."). A
// marker with an unparseable time (e.g. `[t=12:7]`) is stripped too, without a reference.
MarkedText extractMarkers(const QString& text);
QString stripMarkers(const QString& text);

// Splitting into sentences (after . ! ? ... when an uppercase letter / line break / end follows); a
// marker AFTER the sentence also belongs to the sentence ("Sentence. [t=1:00] Next"). The sentences are marker-free.
QVector<MarkedText> splitSentences(const QString& text);

// Reference -> source range. Time: the utterance that contains it (the marked second), otherwise the
// nearest within kToleranceMs; range: the overlapping utterances. An unresolvable reference is dropped.
// Identical utterances merge.
QVector<SourceSpan> resolveRefs(const QVector<SourceRef>& refs, const QVector<SourceLine>& lines,
                                qint64 toleranceMs = kToleranceMs);

// Speakers of the memo sections: the speakers of the utterances within their time frame (in order).
void fillMemoSpeakers(QVector<MemoSection>& memo, const QVector<SourceLine>& lines);

// Completing the Summary built from the model's output: the markers are stripped from every field
// (execSummary, decisions, openQuestions, actionItems, the memo's intros and points), statements and
// sourceSpeakers are filled (the executive summary's sentences, decisions, open questions, action items, the
// memo's intro sentences and points; ids per Types.h), and the memo sections get speakers. Items that are
// empty without a marker are dropped.
void attachSources(Summary& s, const QVector<SourceLine>& lines);

// "statement" | "decision" | "todo" | "openQuestion" | "intro" | "memoPoint"
QString kindToString(StatementKind k);
StatementKind kindFromString(const QString& s);

// Memo subsection: the canonical key from a subheading (English or Hungarian, accent- and
// case-insensitive, without decoration such as #, **, colon): "arguments" (Arguments), "open" (Unresolved /
// Open question), otherwise the title itself. Empty = "".
QString memoGroupKey(const QString& heading);
// The key's Hungarian caption for summary.md ("Arguments made", "Open question"; others: itself).
QString memoGroupLabel(const QString& key);
// Indexes of a section's points per subsection: the body ("") first, then the subsections in order of
// first occurrence. [(key, [point indexes])]
QVector<QPair<QString, QVector<int>>> memoGroups(const MemoSection& m);

// Targeted staleness. then: the speakers recorded when the summary was made (Summary::sourceSpeakers);
// now: the current resolution (utteranceId -> speaker); changedKeys: the speaker keys corrected since the
// summary (empty after "Fine as is" -> nothing is affected). A source line is affected if its name became
// different since, and the old or the new key was corrected. Fills the statements' staleBecause /
// ownerStaleBecause fields and the info's targeted fields (does not touch info.stale / correctedSpeakers).
// info.targeted: whether there is a statement with a source.
void applyTargetedStaleness(QVector<SummaryStatement>& statements,
                            const QMap<QString, SourceSpeaker>& then,
                            const QHash<QString, SourceSpeaker>& now,
                            const QStringList& changedKeys, SummaryStaleInfo& info);

} // namespace summarysrc
} // namespace tanara

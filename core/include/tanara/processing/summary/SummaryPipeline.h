#pragma once
//
// SummaryPipeline: the PURE (I/O- and LLM-free) building blocks of the quick summary.
//
// The process is the same for every length:
//   1. splitting: the transcript into ~15-minute parts, at paragraph boundaries (a short meeting =
//      1 part; a too-short last part merges into the previous one): splitTranscript,
//   2. notes per part (LLM, "notes" prompt): TOPICS / DECISIONS / OPEN / ACTIONS: parseNotes,
//   3. memo: DETERMINISTICALLY from the parts' TOPICS notes (a subject continuing across a part
//      boundary stays one section): assembleMemo,
//   4. summarizing (LLM, "merge" prompt): executive summary + decisions + open questions +
//      action items from the notes (selecting, not copying): renderNotesForMerge + parseMergeJson.
// For a meeting with one part, 2+4 is ONE call ("single" prompt): TOPICS + SUMMARY JSON: parseSingle.
// The orchestration (call order, cache, cancel) lives in SummaryService.
#include "tanara/domain/Transcript.h"
#include "tanara/domain/Summary.h"
#include "tanara/processing/summary/SummarySources.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::processing {
namespace summarypipe {

// Target length of a part and the minimum of the last part (a shorter last part merges into the previous one).
constexpr qint64 kDefaultPartMs = 15 * 60 * 1000;
// Upper limit of open questions after the summary (a long list hurts, per measurement).
constexpr int kMaxOpenQuestions = 5;
// Consecutive memo sections on the same subject merge up to at most this length.
constexpr qint64 kMaxMemoJoinMs = 20 * 60 * 1000;

struct TranscriptPart {
    int     index = 0;          // from 0
    qint64  startMs = 0;        // start of the first paragraph
    qint64  endMs = 0;          // end of the last paragraph
    QString markdown;           // the part's paragraphs (`[mm:ss]` **Speaker** text)
};

// The speech blocks (MergedTranscript::segments) in time order, into ~partMs parts. A paragraph is
// never cut in two. A part closes when the next paragraph starts at least partMs after the part's
// start; a last part shorter than partMs/2 (or under 1500 characters) merges into the previous one.
// Empty input -> an empty list.
QVector<TranscriptPart> splitTranscript(const QVector<Utterance>& segments,
                                        qint64 partMs = kDefaultPartMs);

// The speech blocks into parts of at most maxChars (markdown) length, in order, at paragraph
// boundaries (for the context-adapted re-split). Indexes from 0. An empty list if a single paragraph
// is longer than maxChars by itself (or the input is empty).
QVector<TranscriptPart> splitTranscriptByChars(const QVector<Utterance>& segments, int maxChars);

// A part's notes (the output of the "notes" step, parsed).
struct PartNotes {
    int     index = 0;
    qint64  startMs = 0;
    qint64  endMs = 0;
    QVector<MemoSection> topics;   // the part's subjects (raw material of the memo)
    QStringList decisions;         // in the form "[mm:ss] text", as the model wrote it
    QStringList open;
    QStringList actions;           // "[mm:ss] action - owner - deadline"
    bool isEmpty() const { return topics.isEmpty() && decisions.isEmpty() && open.isEmpty() && actions.isEmpty(); }
};

// "mm:ss" / "h:mm:ss" (or "mmm:ss") -> ms; unparseable -> -1.
qint64 parseTimestamp(const QString& s);
// ms -> "mm:ss" (the minutes do not roll over into hours; the transcript writes it this way too).
QString formatTimestamp(qint64 ms);

// Fault-tolerant parsing of the notes text. Accepts the variants of the headings (## TOPICS,
// **TOPICS**, TOPICS:, the Hungarian equivalents of TOPICS/DECISIONS/OPEN/ACTIONS), a code fence, prose
// before the heading. A subject title is a `###`/`##` line (with an optional [mm:ss-mm:ss] time frame)
// or a bold line; points without a subject title go into an anonymous section. "- none" lines are
// dropped. Missing times are filled from the part's frame and the neighboring subjects. If the text has
// no recognizable heading, its lines become the points of a single subject (the part is not dropped).
// Within a subject: the non-bulleted line(s) after the title are the intro (MemoSection::intro); points
// after a "#### Arguments" / "#### Unresolved" (or any ####) subheading belong to the subsection
// (pointGroups; a #### line is never a section heading). Source markers still stay in the text here
// (the merge input carries them too); buildSummary strips them.
PartNotes parseNotes(const QString& raw, const TranscriptPart& part);

// The memo from the parts' TOPICS notes: concatenated in time order; consecutive sections about
// the same subject (an identical or very similar title, typically a subject continuing across a part
// boundary) merge into one (points in order without repeats, the time frame widens), but only up to
// kMaxMemoJoinMs; beyond that, with an identical title, it stays a separate section.
// A section without a title joins the previous one (if any).
QVector<MemoSection> assembleMemo(const QVector<PartNotes>& parts);

// Input of the summarizing step: the list of speakers + the parts' normalized notes
// (with a "=== PART k of n [mm:ss-mm:ss] ===" header, TOPICS / DECISIONS / OPEN / ACTIONS).
QString renderNotesForMerge(const QVector<PartNotes>& parts, const QStringList& speakers);

// Fields of the short form (the JSON of the merge / single step).
struct MergeResult {
    bool        ok = false;
    QString     error;            // ok == false: a human (Hungarian) explanation of the error
    QString     execSummary;
    QStringList decisions;
    QStringList openQuestions;
    QVector<ActionItem> actionItems;
};

// Permissive JSON extraction: code fence off, prose before/after off (from the first '{' to its
// matching closing '}', respecting brackets inside strings).
QByteArray extractJsonObject(const QString& raw);
// Fixing the common JSON mistakes of small models where it is safe: unescaped inner quotes, a raw
// line break in a string, a trailing comma, a truncated end (closing an open string / array / object),
// "smart" quotes around a key.
QByteArray repairJson(const QByteArray& json);
// Fault-tolerant parsing of the merge / single JSON: a missing field = empty; key aliases
// (open_questions, action_items ...); a string instead of an array and vice versa; an action item may
// be a plain string too. Open questions cut to kMaxOpenQuestions. ok == false if there is no parseable
// object, or none of the known fields is in it.
MergeResult parseMergeJson(const QString& raw);

// The single-step ("single") answer: the part before the JSON is the notes (TOPICS), the JSON is the short form.
struct SingleResult {
    PartNotes   notes;            // empty if the model wrote no notes (e.g. an old custom prompt)
    MergeResult merge;
};
SingleResult parseSingle(const QString& raw, const TranscriptPart& part);

// The speakers (participants) from the speech blocks, in the order of their first utterance.
QStringList speakersOf(const QVector<Utterance>& segments);

// Assembles the full Summary: the short form + memo + participants. Source markers (`[t=mm:ss]`) are
// stripped from the texts, and from lines they become statements + sourceSpeakers + memo-section
// speakers (summarysrc::attachSources). Empty lines -> the statements have no source (the markers are
// stripped anyway).
Summary buildSummary(const MergeResult& merge, const QVector<PartNotes>& parts,
                     const QStringList& participants,
                     const QVector<tanara::summarysrc::SourceLine>& lines = {});

} // namespace summarypipe
} // namespace tanara::processing
#pragma once
//
// Domain: summary value types (statements with source spans, memo, topics) plus the
// source-line and staleness types shared by the summary and the transcript editor.
#include "tanara/domain/Transcript.h"

#include <QHash>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

// ---- summary ----------------------------------------------------------------
struct ActionItem {
    QString text;
    QString owner;
    QString due;
};

// One section of the memo: a subject discussed in the meeting, in time order. The points are
// the specifics (names, numbers, dates; who said it, if it matters). Time is -1 if unknown.
struct MemoSection {
    QString     title;
    qint64      startMs = -1;
    qint64      endMs = -1;
    QStringList points;
    QStringList speakers;   // speakers of the utterances within the section's time span (in order of first utterance)
    // The section's intro: 1-2 sentences on the gist of the subject (no markers; empty = none).
    QString     intro;
    // Subsection of each point, parallel to points (a shorter list = the rest belong to the body):
    // "" = section body, "arguments" = "Arguments made", "open" = "Open question",
    // other text: the model's own subheading. Subsections follow each other in point order.
    QStringList pointGroups;
    QString groupOf(int i) const { return i >= 0 && i < pointGroups.size() ? pointGroups.at(i) : QString(); }
};

// Source of a summary statement in the transcript: time range + ids of the utterances
// (the "u<startMs>" ids of transcript.segments.json).
struct SourceSpan {
    qint64      startMs = -1;
    qint64      endMs = -1;
    QStringList utteranceIds;
};

// Statement: one sentence of the executive summary; Decision / Todo / OpenQuestion: one item of
// decisions / actionItems / openQuestions; MemoIntro: one sentence of a memo section intro;
// MemoPoint: one memo point (in any subsection).
enum class StatementKind { Statement, Decision, Todo, OpenQuestion, MemoIntro, MemoPoint };

// One statement of the summary with source references. Order and id follow the Summary fields:
// "s<k>" is sentence k of execSummary, "d<k>" decisions[k-1], "q<k>" openQuestions[k-1],
// "t<k>" actionItems[k-1], "i<n>.<k>" sentence k of memo[n-1].intro, "m<n>.<k>"
// memo[n-1].points[k-1]. Empty sourceSpans = "no source".
struct SummaryStatement {
    QString       id;               // "s1", "d1", "q1", "t1", "i3.1", "m3.2" ... (stable within the summary)
    QString       text;             // without markers
    StatementKind kind = StatementKind::Statement;
    QVector<SourceSpan> sourceSpans;
    QString       owner;            // for an action item: the owner
    bool          flagged = false;  // "Not said like this?" - flagged by the user
    // Runtime only (not stored): the speaker of the source lines changed since the summary
    // ("X -> Y?"); ownerStaleBecause: the action item's owner is affected too ("X -> Y?").
    QStringList   staleBecause;
    QString       ownerStaleBecause;
};

// The speaker of a source utterance at summary time (for targeted staleness).
struct SourceSpeaker {
    QString key;     // the resolved speaker key (raw label or "participant:N")
    QString name;    // the displayed name, as the LLM saw it
};

// Result of a summary run: the SHORT form (executive summary + lists) and the LONG form
// (memo) are built from the same per-part notes, so they stay consistent.
struct Summary {
    QString execSummary;
    QStringList decisions;
    QStringList openQuestions;           // undecided, still-open matters (at most ~5)
    QVector<ActionItem> actionItems;
    QStringList participants;
    QVector<MemoSection> memo;           // detailed notes of the meeting, in time order
    // Source references (for the quick summary; empty for an old / per-topic summary).
    QVector<SummaryStatement> statements;
    QMap<QString, SourceSpeaker> sourceSpeakers;   // utteranceId -> speaker at the time
    QString renderMarkdown() const;      // impl: the SummaryService module
};

// ---- complex (multi-pass, topic-based) summary ------------------------------
// Pass 1: the TOPICS extracted from the full transcript, an intermediate state the
// user can edit (title + 1-line gist). Pass 2 runs on these (approved) topics.
struct SummaryTopic {
    QString id;        // stable id (QUuid); survives editing / passes
    QString title;
    QString summary;   // 1-2 sentence gist (from pass 1; editable)
};

// Result of pass 2 for one topic: detailed summary + decisions + action items.
struct TopicAnalysis {
    QString topicId;
    QString title;
    QString detail;
    QStringList decisions;
    QStringList openQuestions;           // questions left open in the topic (optional)
    QVector<ActionItem> actionItems;
    // Markdown of the body WITHOUT a title (detail + decisions + action items); the topic card's
    // result view and the topic sections of the complex summary.md are built from it.
    QString renderMarkdown() const;      // impl: the SummaryService module
};

// ---- source lines and staleness (shared by the summary and the editor) ---------
namespace summarysrc {

// One utterance of the transcript (for source resolution).
struct SourceLine {
    QString id;             // "u<startMs>" (transcript.segments.json)
    qint64  startMs = 0;
    qint64  endMs = 0;
    QString speakerKey;     // resolved speaker key
    QString speakerName;    // displayed name (as the LLM saw it)
};

// Lines built from the speech blocks (if there is no transcript.segments.json): id = "u<startMs>",
// key = name = the block's speaker.
QVector<SourceLine> linesFromSegments(const QVector<Utterance>& segments);

} // namespace summarysrc

// Summary staleness: the speaker assignment changed since it was made.
// Targeted staleness (if the summary has statements with source references): which
// statements / action items have source lines whose speaker changed. Without statements (old
// or per-topic summary) targeted == false and only the whole-document flag applies.
struct SummaryStaleInfo {
    bool stale = false;
    int  correctedSpeakers = 0; // "You corrected N speakers since the summary"
    bool targeted = false;              // statement-level data exists (the fields below are valid)
    int  affectedStatements = 0;        // affected statements + decisions ("2 statements")
    int  affectedTodos = 0;             // affected action items ("1 action item")
    int  ownerChanges = 0;              // of which: action items whose owner is affected too
    QStringList affectedStatementIds;   // a SummaryStatement::id-k
    QStringList affectedUtteranceIds;   // for highlighting on the map
};

namespace summarysrc {

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

Q_DECLARE_METATYPE(tanara::ActionItem)
Q_DECLARE_METATYPE(tanara::MemoSection)
Q_DECLARE_METATYPE(tanara::SourceSpan)
Q_DECLARE_METATYPE(tanara::SummaryStatement)
Q_DECLARE_METATYPE(QVector<tanara::SummaryStatement>)
Q_DECLARE_METATYPE(tanara::Summary)
Q_DECLARE_METATYPE(tanara::SummaryTopic)
Q_DECLARE_METATYPE(QVector<tanara::SummaryTopic>)
Q_DECLARE_METATYPE(tanara::TopicAnalysis)
Q_DECLARE_METATYPE(tanara::SummaryStaleInfo)

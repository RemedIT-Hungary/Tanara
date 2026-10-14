#pragma once
//
// SummaryRepository: STRUCTURED access to a meeting's summary.
//
// Only summary.md used to exist on disk. From now on every NEW summary also gets a
// summary.json (the structured Summary + its provenance: when, which provider/model, quick or
// per-topic). For an old meeting (summary.md only) the structure is recovered from the markdown with
// a fault-tolerant parser; the format written by Summary::renderMarkdown and the complex summary reads
// back losslessly.
//
// summary.md stays the human-readable / copied-into-notes form; if someone edits it by hand (newer
// than summary.json), the markdown is the truth and the metadata comes from the json.
#include "tanara/domain/Summary.h"

#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <QMetaType>

namespace tanara::library {

enum class SummaryMode {
    Unknown,   // old summary, the mode cannot be determined for sure
    Quick,     // quick summary (one LLM call)
    Topics,    // per-topic analysis (topics -> analyses -> combining)
};

// Provenance of the summary (right column of the summary view). For an old summary providerId / model
// are empty and createdAt is the summary.md modification time.
struct SummaryMeta {
    QDateTime   createdAt;
    QString     providerId;    // pl. "openai-compat", "tanara-cloud"
    QString     model;         // the model id actually sent
    SummaryMode mode = SummaryMode::Unknown;
    // The meeting note the summary was made with (the Summary tab uses it to gently signal if it changed
    // since). Unknown for an old summary (Known = false).
    QString     contextNote;
    bool        contextNoteKnown = false;
};

struct SummaryDocument {
    bool        exists = false;        // whether the meeting has a summary
    Summary     summary;               // executive summary, decisions, action items, participants
    QVector<TopicAnalysis> topics;     // per-topic mode: the topic sections, in order
    SummaryMeta meta;
    QString     markdown;              // the content of summary.md
    bool        fromMarkdown = false;  // the structure was recovered from the markdown
};

// SummaryRepository: every summary file of a meeting folder (summary.md, summary.json, the
// per-topic mode's summary.topics.json and summary.analyses.json). Stateless: the members are static
// and touch only the given folder, so they are callable from any thread.
class SummaryRepository {
public:
    static QString jsonPath(const QString& meetingFolder);       // <folder>/summary.json
    static QString markdownPath(const QString& meetingFolder);   // <folder>/summary.md

    // Markdown -> structure. Recognizes the quick form (## Executive summary / ## Decisions /
    // ## Open questions / ## Action items / ## Participants / ## Memo + ### title (mm:ss-mm:ss)) and the
    // per-topic form (## Action items (merged) / ## Topics / ### N. title + **Decisions:** / **Open
    // questions:** / **Action items:**), using the Hungarian headings the app writes; headings are matched
    // accent- and case-insensitively. The text of an unknown section is not lost: it goes to the end of the
    // executive summary. In per-topic mode summary.decisions (and openQuestions) is the union of the topics'.
    static SummaryDocument parseMarkdown(const QString& markdown);

    // Loads the meeting's summary (summary.json, otherwise summary.md parsed).
    static SummaryDocument load(const QString& meetingFolder);

    // Saves the structured form + metadata (summary.json). Does NOT write summary.md.
    static bool save(const QString& meetingFolder, const SummaryDocument& doc);
    // Writes summary.md (LF line endings, atomically).
    static bool writeMarkdown(const QString& meetingFolder, const QString& markdown);
    // The copy of a summary in the notes folder (e.g. a Markdown vault): <notesDir>/<fileName>, the
    // folder is created if needed.
    static bool writeNoteCopy(const QString& notesDir, const QString& fileName, const QString& markdown);

    // "Not said like this? / Flag": marks a statement (later training data). It rewrites only the
    // statements of summary.json (the json's mtime stays, so a hand-edited summary.md remains the truth).
    // false: no such statement / write error.
    static bool setStatementFlagged(const QString& meetingFolder, const QString& statementId, bool flagged = true);

    // ---- per-topic mode: the editable topic list (pass 1) and the finished analyses (pass 2) ----
    static QVector<SummaryTopic> readTopics(const QString& meetingFolder);
    static bool writeTopics(const QString& meetingFolder, const QVector<SummaryTopic>& topics);
    static QVector<TopicAnalysis> readAnalyses(const QString& meetingFolder);
    static bool writeAnalyses(const QString& meetingFolder, const QVector<TopicAnalysis>& analyses);
    // Writes / overwrites one finished analysis (by topicId) IMMEDIATELY, so work already paid for is
    // not lost after a cancel / error. Nothing happens if the meeting folder no longer exists.
    static bool upsertAnalysis(const QString& meetingFolder, const TopicAnalysis& a);

    // The complex summary's markdown: a global head (executive summary + open questions + merged action
    // items) + per-topic sections (summary + decisions + action items), in Summary::renderMarkdown's style.
    static QString renderComplexMarkdown(const QString& execSummary, const QVector<ActionItem>& items,
                                         const QVector<TopicAnalysis>& topics);
    // The union of the topics' open questions (in order, without repeats, at most kMaxOpenQuestions).
    static QStringList openQuestionsOf(const QVector<TopicAnalysis>& topics);

    static QJsonObject     toJson(const SummaryDocument& doc);
    static SummaryDocument fromJson(const QJsonObject& o);

    static QString     modeToString(SummaryMode m);              // "quick" | "topics" | "unknown"
    static SummaryMode modeFromString(const QString& s);
};

} // namespace tanara::library
Q_DECLARE_METATYPE(tanara::library::SummaryMode)
Q_DECLARE_METATYPE(tanara::library::SummaryMeta)
Q_DECLARE_METATYPE(tanara::library::SummaryDocument)

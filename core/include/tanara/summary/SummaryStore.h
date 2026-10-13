#pragma once
//
// SummaryStore: STRUCTURED access to a meeting's summary.
//
// Only summary.md used to exist on disk. From now on every NEW summary also gets a
// summary.json (the structured Summary + its provenance: when, which provider/model, quick or
// per-topic). For an old meeting (summary.md only) the structure is recovered from the markdown with
// a fault-tolerant parser; the format written by Summary::renderMarkdown and the complex summary reads
// back losslessly.
//
// summary.md stays the human-readable / copied-into-notes form; if someone edits it by hand (newer
// than summary.json), the markdown is the truth and the metadata comes from the json.
#include "tanara/Types.h"

#include <QJsonObject>
#include <QMetaType>

namespace tanara {

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

namespace summarystore {

QString jsonPath(const QString& meetingFolder);       // <folder>/summary.json
QString markdownPath(const QString& meetingFolder);   // <folder>/summary.md

// Markdown -> structure. Recognizes the quick form (## Executive summary / ## Decisions /
// ## Open questions / ## Action items / ## Participants / ## Memo + ### title (mm:ss-mm:ss)) and the
// per-topic form (## Action items (merged) / ## Topics / ### N. title + **Decisions:** / **Open
// questions:** / **Action items:**), using the Hungarian headings the app writes; headings are matched
// accent- and case-insensitively. The text of an unknown section is not lost: it goes to the end of the
// executive summary. In per-topic mode summary.decisions (and openQuestions) is the union of the topics'.
SummaryDocument parseMarkdown(const QString& markdown);

// Loads the meeting's summary (summary.json, otherwise summary.md parsed).
SummaryDocument load(const QString& meetingFolder);

// Saves the structured form + metadata (summary.json). Does NOT write summary.md.
bool save(const QString& meetingFolder, const SummaryDocument& doc);

// "Not said like this? / Flag": marks a statement (later training data). It rewrites only the
// statements of summary.json (the json's mtime stays, so a hand-edited summary.md remains the truth).
// false: no such statement / write error.
bool setStatementFlagged(const QString& meetingFolder, const QString& statementId, bool flagged = true);

QJsonObject    toJson(const SummaryDocument& doc);
SummaryDocument fromJson(const QJsonObject& o);

QString     modeToString(SummaryMode m);              // "quick" | "topics" | "unknown"
SummaryMode modeFromString(const QString& s);

} // namespace summarystore
} // namespace tanara

Q_DECLARE_METATYPE(tanara::SummaryMode)
Q_DECLARE_METATYPE(tanara::SummaryMeta)
Q_DECLARE_METATYPE(tanara::SummaryDocument)

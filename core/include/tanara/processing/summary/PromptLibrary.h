#pragma once
//
// Tanara: the ONE place for the built-in LLM prompts + file override.
//
// Prompt ids:
//   quick summary (SummaryService): "notes" (per-part notes) | "merge" (combining the
//     part notes) | "single" (a meeting with one part: notes + summary in one call; the old
//     "simple" id is an alias of it),
//   per-topic analysis: "topic" (topic extraction) | "analysis" (per-topic analysis) |
//     "reduce" (final executive summary),
//   tag suggestion after the summary: "tags" (LlmTagSuggester; plain "pick: / new:" lines).
// The built-in prompts are English; the output language comes from the {{LANGUAGE}} variable
// (English language name) or the legacy {{NYELV}} (the setting's text).
//
// Resolution order for a prompt:
//   1. the user override stored in Settings (handled by AppController, not here),
//   2. <dataDir>/prompts/<id>.md: tunable by hand, WITHOUT REBUILDING,
//   3. the built-in default compiled into the code (promptBuiltin).
#include "tanara/domain/Transcript.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::processing {

// The compiled-in default for the given id (unknown id = empty string).
QString promptBuiltin(const QString& id);

// Path of the file override: <dataDir>/prompts/<id>.md (expands "~";
// empty dataDir = the current data folder, see foundation/paths/AppPaths.h).
QString promptFilePath(const QString& id, const QString& dataDir = QString());

// The file override if it exists and is non-empty, otherwise the built-in default.
QString promptDefault(const QString& id, const QString& dataDir = QString());

// Applies the summary's TARGET LANGUAGE to a system prompt:
//  - replaces {{LANGUAGE}} with the English language name (summaryLanguageName), {{NYELV}}
//    with the setting's text and {{SPEAKER_LABEL}} with an example raw-speaker label (promptSpeakerLabel),
//  - for a custom prompt without placeholders and a non-Hungarian target, appends a directive.
// Empty language = "magyar". The Hungarian section headings (decisions / action items) stay Hungarian.
QString applySummaryLanguage(QString prompt, const QString& language);

// The label of a raw speaker key ("speaker:2", domain/SpeakerKeys.h) in the LLM input: in the summary
// language ("Beszélő 2" for a Hungarian summary, "Speaker 2" otherwise), independent of the UI language.
// Anything that is not a raw key (a person's name) is returned unchanged. The built-in prompts explain the
// label through the {{SPEAKER_LABEL}} variable (applySummaryLanguage), rendered by this same function, so
// the explanation and the transcript cannot diverge.
QString promptSpeakerLabel(const QString& rawKey, const QString& language);
// promptSpeakerLabel on every token's speaker (after the names were resolved): the transcript sent to the LLM.
void applyPromptSpeakerLabels(MergedTranscript& transcript, const QString& language);

// English name of the target language (the user's free text, e.g. the Hungarian or German word) for the
// English prompts: maps common languages (e.g. the Hungarian word for Hungarian -> "Hungarian"), returns anything else unchanged.
// Empty = "Hungarian".
QString summaryLanguageName(const QString& language);

// Short language reminder appended to the user message AFTER the transcript / notes
// (the model reads it last; Gemma tends to answer in English with an English prompt).
QString languageReminder(const QString& language);

// Variables substituted by the CODE in the prompts (the Settings legend and the editor
// highlighting use this; list only what is actually replaced).
struct PromptVariable {
    QString token;         // pl. "{{NYELV}}"
    QString description;   // what it is replaced with (in the UI language)
};
QVector<PromptVariable> promptVariables();

// Shape of a prompt's OUTPUT as the processing code expects it (not editable; shown in the
// Settings "Output schema" dialog). id: simple | topic | analysis | reduce.
struct PromptOutputFormat {
    QString kind;          // "json" | "markdown" | "text"
    QString summary;       // egysoros: „execSummary, decisions[], actionItems[], participants[]”
    QString body;          // the full description (schema / sample), shown in a monospace font
};
PromptOutputFormat promptOutputFormat(const QString& id);

} // namespace tanara::processing
#pragma once
//
// The "processing" settings section: transcription language, summary language and prompts,
// speaker models, tag suggestions.
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace tanara::processing {

struct ProcessingSettings {
    QStringList languageHints{QStringLiteral("hu")};   // STT language hints; empty = automatic

    // The summary's TARGET LANGUAGE (free text); it goes into the prompts' {{NYELV}} placeholder
    // (PromptLibrary::applySummaryLanguage). Independent of the UI language.
    QString summaryLanguage{QStringLiteral("magyar")};

    // System prompts of the summary LLM. EMPTY = the file override / built-in default
    // (PromptLibrary) applies.
    //  summaryPrompt - single-step prompt for a meeting with ONE part,
    //  notesPrompt   - per-part notes for a longer meeting,
    //  mergePrompt   - combining the part notes.
    QString summaryPrompt;
    QString notesPrompt;
    QString mergePrompt;
    // The two editable prompts of the complex (multi-pass) summary. EMPTY = the code default.
    QString topicExtractionPrompt;   // pass 1: topic extraction
    QString topicAnalysisPrompt;     // pass 2: per-topic analysis

    // Enabled speaker-embedding models (VoiceModelRegistry ids), alphabetical. A missing model file
    // is skipped at run time, the setting stays. Missing key = ["wespeaker-resnet34-lm"].
    QStringList voiceModels{QStringLiteral("wespeaker-resnet34-lm")};

    // Tag suggestions on/off, and whether the language model suggests after the summary.
    bool tagSuggestions = true;
    bool llmTagSuggestions = true;

    QJsonObject toJson() const;
    static ProcessingSettings fromJson(const QJsonObject& o);
};

} // namespace tanara::processing

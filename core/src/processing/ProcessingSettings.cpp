#include "tanara/processing/ProcessingSettings.h"
#include "tanara/domain/Voice.h"

#include <QJsonArray>

namespace tanara::processing {

namespace {
QStringList toList(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& e : v.toArray()) out << e.toString();
    return out;
}
} // namespace

QJsonObject ProcessingSettings::toJson() const
{
    return QJsonObject{
        {QStringLiteral("languageHints"), QJsonArray::fromStringList(languageHints)},
        {QStringLiteral("summaryLanguage"), summaryLanguage},
        {QStringLiteral("summaryPrompt"), summaryPrompt},
        {QStringLiteral("notesPrompt"), notesPrompt},
        {QStringLiteral("mergePrompt"), mergePrompt},
        {QStringLiteral("topicExtractionPrompt"), topicExtractionPrompt},
        {QStringLiteral("topicAnalysisPrompt"), topicAnalysisPrompt},
        {QStringLiteral("voiceModels"), QJsonArray::fromStringList(voicemodels::normalizeIds(voiceModels))},
        {QStringLiteral("localDiarization"), localDiarization},
        {QStringLiteral("tagSuggestions"), tagSuggestions},
        {QStringLiteral("llmTagSuggestions"), llmTagSuggestions},
    };
}

ProcessingSettings ProcessingSettings::fromJson(const QJsonObject& o)
{
    ProcessingSettings s;
    // languageHints: the EMPTY list is a valid value ("Automatic"); only a missing key gets the default.
    if (o.contains(QStringLiteral("languageHints")))
        s.languageHints = toList(o.value(QStringLiteral("languageHints")));
    s.summaryLanguage = o.value(QStringLiteral("summaryLanguage")).toString(s.summaryLanguage);
    s.summaryPrompt = o.value(QStringLiteral("summaryPrompt")).toString();
    s.notesPrompt   = o.value(QStringLiteral("notesPrompt")).toString();
    s.mergePrompt   = o.value(QStringLiteral("mergePrompt")).toString();
    s.topicExtractionPrompt = o.value(QStringLiteral("topicExtractionPrompt")).toString();
    s.topicAnalysisPrompt   = o.value(QStringLiteral("topicAnalysisPrompt")).toString();
    // Speaker models: a missing key -> the default; an empty list is valid (every model off).
    if (o.contains(QStringLiteral("voiceModels")))
        s.voiceModels = voicemodels::normalizeIds(toList(o.value(QStringLiteral("voiceModels"))));
    s.localDiarization  = o.value(QStringLiteral("localDiarization")).toBool(s.localDiarization);
    s.tagSuggestions    = o.value(QStringLiteral("tagSuggestions")).toBool(s.tagSuggestions);
    s.llmTagSuggestions = o.value(QStringLiteral("llmTagSuggestions")).toBool(s.llmTagSuggestions);
    return s;
}

} // namespace tanara::processing

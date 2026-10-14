#pragma once
//
// SimilarityTagSuggestionProvider: the similarity-based ITagSuggester (library, Origin::Local): a
// wrapper of TagSuggester (the tags of similar meetings; for a draft without a meeting id: by title
// and participants). Main thread (TagService); delivered in the next event-loop turn.
//
// library::registerInto registers it as ("tag-suggestion", "similarity").
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/tags/ITagSuggester.h"

#include <QObject>

namespace tanara::library {

class TagService;
class MeetingProfiles;
class EmbeddingIndex;

class SimilarityTagSuggestionProvider : public QObject, public providers::ITagSuggester {
    Q_OBJECT
public:
    static inline const QString Id = QStringLiteral("similarity");

    // Not owned; they must outlive the provider. index may be null (no embedding level).
    SimilarityTagSuggestionProvider(TagService* tags, MeetingProfiles* profiles, EmbeddingIndex* index = nullptr,
                                    QObject* parent = nullptr);

    QString name() const override { return Id; }
    providers::TagSuggestJob* suggest(const providers::TagSuggestRequest& req) override;

private:
    TagService* m_tags;
    MeetingProfiles* m_profiles;
    EmbeddingIndex* m_index;
};

// The library's capability providers: the similarity tag suggester. The services must outlive the
// registry's providers.
void registerInto(providers::ProviderRegistry& registry, TagService* tags, MeetingProfiles* profiles,
                  EmbeddingIndex* index = nullptr);

} // namespace tanara::library

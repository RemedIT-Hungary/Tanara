#include "tanara/library/tags/SimilarityTagSuggestionProvider.h"
#include "tanara/library/tags/TagSuggester.h"

#include <QCoreApplication>

#include <algorithm>

namespace tanara::library {

SimilarityTagSuggestionProvider::SimilarityTagSuggestionProvider(TagService* tags, MeetingProfiles* profiles,
                                                                 EmbeddingIndex* index, QObject* parent)
    : QObject(parent), m_tags(tags), m_profiles(profiles), m_index(index)
{
}

providers::TagSuggestJob* SimilarityTagSuggestionProvider::suggest(const providers::TagSuggestRequest& req)
{
    return new providers::FunctionJob<QVector<TagSuggestion>>(
        [this, req](QVector<TagSuggestion>* out, Error* error) {
            if (!m_tags || !m_profiles) {
                error->kind = Error::Kind::Unsupported;
                error->code = QStringLiteral("no_tag_service");
                return false;
            }
            const TagSuggester s(m_tags, m_profiles, m_index);
            *out = req.meetingId.isEmpty() ? s.suggestForDraft(req.title, req.participantIds)
                                           : s.suggest(req.meetingId);
            out->erase(std::remove_if(out->begin(), out->end(), [&req](const TagSuggestion& t) {
                           return req.currentTagIds.contains(t.tagId);
                       }), out->end());
            return true;
        },
        this);
}

void registerInto(providers::ProviderRegistry& registry, TagService* tags, MeetingProfiles* profiles,
                  EmbeddingIndex* index)
{
    providers::ProviderDescriptor d;
    d.id = SimilarityTagSuggestionProvider::Id;
    d.displayName = QCoreApplication::translate("BuiltinProviders", "Hasonló meetingek");
    d.origin = providers::Origin::Local;
    d.traits.dataLeavesDevice = false;
    d.traits.metered = false;
    d.traits.worksOffline = true;
    d.traits.inputs = { providers::InputKind::Text };
    d.authMode = providers::AuthMode::None;
    d.networkRequired = false;
    registry.add<providers::ITagSuggester>(
        d, [tags, profiles, index](const providers::ProviderContext&, const providers::ProviderConfig&,
                                   QObject* parent) -> providers::ITagSuggester* {
            return new SimilarityTagSuggestionProvider(tags, profiles, index, parent);
        });
}

} // namespace tanara::library

#pragma once
//
// Tag-suggestion port (CapabilityKind::TagSuggestion): tags for one meeting.
//
//  - library::SimilarityTagSuggestionProvider: similar meetings' tags (TagSuggester; needs meetingId);
//  - processing::LlmTagSuggestionProvider: the language model picks from candidates / invents names
//    (LlmTagSuggester; needs the summary text);
//  - cloud::CloudTagSuggester: the gateway (/v1/tags/suggest; the client sends the candidates).
#include "tanara/domain/Tag.h"
#include "tanara/providers/core/CapabilityJob.h"
#include "tanara/providers/core/ProviderDescriptor.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::providers {

// An existing tag offered to a model, with its one-line profile (typical participants, phrases ...).
struct TagCandidate {
    QString tagId;
    QString name;
    QString profileLine;
};

struct TagSuggestRequest {
    QString meetingId;
    QString title;
    QString summaryText;                   // the summary (LLM / Cloud); may be empty for similarity
    QStringList participants;
    QVector<TagSuggestion> seeds;          // earlier (e.g. similarity) suggestions to refine
    // The existing tags a model may pick; empty = the provider builds them (local) / the seeds (Cloud).
    QVector<TagCandidate> candidates;
    QStringList currentTagIds;             // already on the meeting (never suggested again)
    QString model;
    QString systemPrompt;                  // FINAL form; empty = built-in
    int maxNew = 2;                        // at most this many new names
};

using TagSuggestJob = ResultJob<QVector<TagSuggestion>>;

class ITagSuggester {
public:
    virtual ~ITagSuggester() = default;
    virtual QString name() const = 0;
    virtual TagSuggestJob* suggest(const TagSuggestRequest& req) = 0;
};

template <> struct ProviderTraits<ITagSuggester> {
    static constexpr CapabilityKind kind = CapabilityKind::TagSuggestion;
};

} // namespace tanara::providers

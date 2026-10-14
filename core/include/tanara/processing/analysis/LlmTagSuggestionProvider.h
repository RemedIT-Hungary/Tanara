#pragma once
//
// LlmTagSuggestionProvider: the LLM ITagSuggester (processing) over ANY chat provider: a wrapper of
// LlmTagSuggester (candidates from the tag set + the seeds, the "pick: / new:" reply, the tag-set
// filtering). One LlmTagSuggester per job, so jobs may overlap.
#include "tanara/providers/chat/IChatProvider.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/tags/ITagSuggester.h"

#include <QObject>
#include <QPointer>

namespace tanara::library { class TagService; }

namespace tanara::processing {

class LlmTagSuggestionProvider : public QObject, public providers::ITagSuggester {
    Q_OBJECT
public:
    static inline const QString Id = QStringLiteral("llm");

    // tags, chat: not owned (they must outlive the jobs).
    LlmTagSuggestionProvider(library::TagService* tags, providers::IChatProvider* chat,
                             const providers::ProviderConfig& cfg, QObject* parent = nullptr);

    // Sets the chat provider (the registry factory creates it as this object's child).
    void setChatProvider(providers::IChatProvider* chat) { m_chat = chat; }

    QString name() const override { return Id; }
    // req.systemPrompt empty = the built-in "tags" prompt; req.model empty = the config's model.
    // The candidates are built from the tag set and req.seeds (req.candidates is not used here).
    providers::TagSuggestJob* suggest(const providers::TagSuggestRequest& req) override;

private:
    library::TagService* m_tags;
    providers::IChatProvider* m_chat;
    providers::ProviderConfig m_cfg;
};

} // namespace tanara::processing

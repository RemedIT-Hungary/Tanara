#include "tanara/processing/analysis/LlmTagSuggestionProvider.h"
#include "tanara/processing/analysis/LlmTagSuggester.h"
#include "tanara/processing/summary/PromptLibrary.h"

#include <QPointer>
#include <QTimer>

namespace tanara::processing {

LlmTagSuggestionProvider::LlmTagSuggestionProvider(library::TagService* tags, providers::IChatProvider* chat,
                                                   const providers::ProviderConfig& cfg, QObject* parent)
    : QObject(parent), m_tags(tags), m_chat(chat), m_cfg(cfg)
{
}

providers::TagSuggestJob* LlmTagSuggestionProvider::suggest(const providers::TagSuggestRequest& req)
{
    auto* job = new providers::TagSuggestJob(this);
    if (!m_chat || !m_tags) {
        Error e;
        e.kind = Error::Kind::Unsupported;
        e.code = m_chat ? QStringLiteral("no_tag_service") : QStringLiteral("no_chat_provider");
        job->failLater(e);
        return job;
    }
    auto* runner = new LlmTagSuggester(m_tags, job);
    QPointer<providers::TagSuggestJob> guard(job);
    connect(runner, &LlmTagSuggester::finished, job, [guard](const QString&, const QVector<TagSuggestion>& s) {
        if (guard) guard->completeLater(s);
    });
    connect(runner, &LlmTagSuggester::failed, job, [guard](const QString&, const QString& error) {
        Error e;
        e.kind = Error::Kind::Service;
        e.code = QStringLiteral("tag_suggestion_failed");
        e.message = error;
        if (guard) guard->failLater(e);
    });
    const QString prompt = req.systemPrompt.isEmpty() ? promptBuiltin(QStringLiteral("tags")) : req.systemPrompt;
    const QString model = req.model.isEmpty() ? m_cfg.model : req.model;
    providers::IChatProvider* chat = m_chat;
    // Started in the next turn: LlmTagSuggester may answer synchronously.
    QTimer::singleShot(0, runner, [runner, guard, chat, model, prompt, req]() {
        if (!guard || guard->isCancelled()) return;
        runner->start(chat, model, prompt, req.meetingId, req.summaryText, req.seeds);
    });
    return job;
}

} // namespace tanara::processing

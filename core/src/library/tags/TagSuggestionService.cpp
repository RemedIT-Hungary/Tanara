#include "tanara/library/tags/TagSuggestionService.h"

#include "tanara/foundation/log/Logging.h"
#include "tanara/library/index/EmbeddingIndex.h"
#include "tanara/library/index/EmbeddingPreparer.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/library/tags/MeetingProfiles.h"
#include "tanara/library/tags/TagNames.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/library/tags/TagSuggester.h"

#include <QMetaObject>

#include <algorithm>

namespace tanara::library {

TagSuggestionService::TagSuggestionService(const Deps& deps, QObject* parent)
    : QObject(parent), m_d(deps)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("tanara-tag-suggest"));
    // At the end of a profile-building round (idle) the waiting computations start.
    connect(m_d.profiles, &MeetingProfiles::idle, this, [this]() {
        const QSet<QString> waiting = m_requests;
        m_requests.clear();
        for (const QString& id : waiting) startJob(id);
    });
    // The similarity-based list stays valid until the tags, transcripts and the index change.
    connect(m_d.tags, &TagService::tagsChanged, this, [this]() { invalidate(); });
    if (m_d.embeddings) {
        connect(m_d.embeddings, &EmbeddingIndex::indexChanged, this, [this]() { invalidate(); });
        connect(m_d.embeddings, &EmbeddingIndex::indexReset, this, [this]() { invalidate(); });
    }
    // An applied / rejected suggestion: the published list goes out again filtered (without recomputation).
    connect(m_d.tags, &TagService::meetingTagsChanged, this, [this](const QString& id) {
        invalidate();
        if (m_pending.contains(id)) publish(id, m_pending.value(id));
    });
    // A person's manual tags changed: the "by participants" suggestion goes out again (the published list
    // keeps the similarity-based part).
    connect(m_d.tags, &TagService::personTagsChanged, this, [this]() {
        const QStringList ids = m_pending.keys();
        for (const QString& id : ids) publish(id, m_pending.value(id));
    });
    connect(m_d.tags, &TagService::rejectedChanged, this, [this]() {
        invalidate();
        const QStringList ids = m_pending.keys();
        for (const QString& id : ids) {
            const QVector<TagSuggestion> before = m_pending.value(id);
            publish(id, before);
        }
    });
}

TagSuggestionService::~TagSuggestionService()
{
    shutdown();
}

TagSuggestionService::Config TagSuggestionService::config() const
{
    return m_d.config ? m_d.config() : Config{};
}

void TagSuggestionService::shutdown()
{
    m_shutdown = true;
    abortLlm();
    // A running computation reads the profiles / the index: it must finish before them.
    m_pool.clear();
    m_pool.waitForDone();
}

void TagSuggestionService::invalidate()
{
    m_similarCache.clear();
    ++m_gen;
}

void TagSuggestionService::republish(const QString& meetingId)
{
    if (m_pending.contains(meetingId)) publish(meetingId, m_pending.value(meetingId));
}

void TagSuggestionService::transcriptChanged(const QString& meetingId)
{
    invalidate();
    m_d.profiles->invalidate(meetingId);
    if (m_d.embeddingPreparer) m_d.embeddingPreparer->enqueue(meetingId);   // only if there is a provider
}

void TagSuggestionService::summaryReady(const QString& meetingId)
{
    // Only if a tag set already exists: with an empty set the user is not tagging yet, and a model call
    // after every summary (with new-name ideas) would be intrusive and superfluous.
    const Config c = config();
    if (c.tagSuggestions && c.llmTagSuggestions && !m_d.tags->all().isEmpty())
        requestLlm(meetingId);
}

QVector<TagSuggestion> TagSuggestionService::pending(const QString& meetingId) const
{
    return m_pending.value(meetingId);
}

QVector<TagSuggestion> TagSuggestionService::draft(const QString& title) const
{
    if (!config().tagSuggestions) return {};
    const TagSuggester suggester(m_d.tags, m_d.profiles, m_d.embeddings);
    return suggester.suggestForDraft(title);
}

QString TagSuggestionService::summaryText(const QString& meetingId) const
{
    const Meeting m = m_d.store->load(meetingId);
    if (m.id.isEmpty()) return {};
    const SummaryDocument doc = SummaryRepository::load(m.folder);
    return doc.exists ? doc.markdown : QString();
}

void TagSuggestionService::publish(const QString& meetingId, QVector<TagSuggestion> list)
{
    // The suggestion from the participants' manual tags ("By participants") is always computed fresh: earlier
    // People items drop out, new ones stand first (the row's source tag comes from these). If a tag is also
    // suggested by similarity, one item remains and the reasons add up. It works before transcription too
    // (manual / approved participants).
    list.erase(std::remove_if(list.begin(), list.end(), [](const TagSuggestion& sg) {
        return sg.source == SuggestionSource::People;
    }), list.end());
    if (config().tagSuggestions) {
        QVector<TagSuggestion> people = m_d.tags->suggestMeetingTagsFromParticipants(meetingId);
        for (TagSuggestion& p : people) {
            const auto dup = std::find_if(list.begin(), list.end(), [&](const TagSuggestion& sg) {
                return !sg.isNew && sg.tagId == p.tagId;
            });
            if (dup == list.end()) continue;
            p.reasons += dup->reasons;
            p.similarMeetings = dup->similarMeetings;
            list.erase(dup);
        }
        list = people + list;
    }
    // An already applied and a rejected suggestion are dropped (the LLM's new-name idea too, if a tag with
    // that name has since been put on the meeting).
    const QStringList applied = m_d.tags->tagsOf(meetingId);
    QStringList appliedKeys;
    for (const QString& id : applied) appliedKeys << tagKey(m_d.tags->tag(id).name);
    list.erase(std::remove_if(list.begin(), list.end(), [&](const TagSuggestion& sg) {
        if (sg.isNew) return appliedKeys.contains(tagKey(sg.name)) || m_d.tags->isRejected(meetingId, sg.name);
        return applied.contains(sg.tagId) || !m_d.tags->tag(sg.tagId).isValid()
            || m_d.tags->isRejected(meetingId, sg.tagId);
    }), list.end());
    m_pending.insert(meetingId, list);
    emit suggestionsReady(meetingId, list);
}

// The profiles (rows + transcript phrases) are built on a background thread; at the end of the round
// (idle) the suggestion computation starts.
void TagSuggestionService::schedule(const QString& meetingId)
{
    m_requests.insert(meetingId);
    m_d.profiles->ensureBuilt();
}

// Threading: TagService belongs to the main thread, so a small snapshot of the tag data (TagSnapshot) is
// made here, on the main thread; the background thread uses only that and the thread-safe readers of
// MeetingProfiles / EmbeddingIndex. The result comes back to the main thread via a queued call (finishJob).
// The destructor waits for the running computation.
void TagSuggestionService::startJob(const QString& meetingId)
{
    auto run = m_runs.find(meetingId);
    if (run == m_runs.end() || (!run->publish && !run->llm)) {
        m_runs.remove(meetingId);
        return;
    }
    if (run->running) return;   // the running one's result answers (it restarts on a data change)
    const auto cached = m_similarCache.constFind(meetingId);
    if (cached != m_similarCache.constEnd()) {
        finishJob(meetingId, m_gen, *cached);
        return;
    }
    if (m_shutdown) return;
    run->running = true;
    TagSnapshot snap;
    {
        tanara::PerfScope perfScope("TagSuggestionService::startJob (pillanatkép, fő szál)", 5);
        snap = TagSuggester::snapshot(*m_d.tags, meetingId);
    }
    const quint64 gen = m_gen;
    const MeetingProfiles* profiles = m_d.profiles;
    const EmbeddingIndex* index = m_d.embeddings;
    const std::function<void()> hook = m_d.backgroundHook;
    m_pool.start([this, snap, gen, profiles, index, hook]() {
        if (hook) hook();
        QVector<TagSuggestion> list;
        {
            tanara::PerfScope perfScope("TagSuggester::suggestFrom (háttérszál)", 0);
            list = TagSuggester::suggestFrom(snap, profiles, index);
        }
        const QString meetingId = snap.meetingId;
        QMetaObject::invokeMethod(this, [this, meetingId, gen, list]() {
            finishJob(meetingId, gen, list);
        }, Qt::QueuedConnection);
    });
}

void TagSuggestionService::finishJob(const QString& meetingId, quint64 gen, QVector<TagSuggestion> list)
{
    auto run = m_runs.find(meetingId);
    if (run == m_runs.end()) return;
    run->running = false;
    if (gen != m_gen) {
        // The tags / transcript / index changed meanwhile: the result is stale.
        if (run->publish || run->llm) schedule(meetingId);
        else m_runs.erase(run);
        return;
    }
    const Run done = *run;
    m_runs.erase(run);
    m_similarCache.insert(meetingId, list);
    if (done.publish) publish(meetingId, list);
    if (done.llm) startLlm(meetingId, list);
}

void TagSuggestionService::requestSuggestions(const QString& meetingId)
{
    if (meetingId.isEmpty() || m_shutdown) return;
    emit computing(meetingId);
    if (!config().tagSuggestions) {
        publish(meetingId, {});
        return;
    }
    if (const auto it = m_similarCache.constFind(meetingId); it != m_similarCache.constEnd()) {
        publish(meetingId, *it);
        return;
    }
    m_runs[meetingId].publish = true;
    if (!m_runs.value(meetingId).running) schedule(meetingId);
}

void TagSuggestionService::requestCooccur(const QString& meetingId, const QString& justAddedTagId)
{
    if (!config().tagSuggestions) return;
    const TagSuggester suggester(m_d.tags, m_d.profiles, m_d.embeddings);
    const QVector<TagSuggestion> list = suggester.cooccur(meetingId, justAddedTagId);
    if (list.isEmpty()) return;
    emit computing(meetingId);
    publish(meetingId, list);
}

void TagSuggestionService::requestLlm(const QString& meetingId)
{
    if (m_shutdown || !config().tagSuggestions) return;
    if (summaryText(meetingId).trimmed().isEmpty()) return;
    emit computing(meetingId);
    // The candidate list (similarity-based suggestions) from the cache, or made on a background thread.
    if (m_similarCache.contains(meetingId)) {
        startLlm(meetingId, m_similarCache.value(meetingId));
        return;
    }
    m_runs[meetingId].llm = true;
    if (!m_runs.value(meetingId).running) schedule(meetingId);
}

void TagSuggestionService::abortLlm()
{
    if (m_llmJob) m_llmJob->cancel();
    // Deleting the suggester stops the model call under it.
    if (m_llmProvider) m_llmProvider->deleteLater();
    m_llmProvider = nullptr;
    m_llmJob = nullptr;
    m_llmMeetingId.clear();
}

void TagSuggestionService::startLlm(const QString& meetingId, const QVector<TagSuggestion>& suggested)
{
    const QString text = summaryText(meetingId);
    if (!config().tagSuggestions || text.trimmed().isEmpty() || !m_d.llm) {
        publish(meetingId, m_pending.value(meetingId));
        return;
    }
    // One LLM suggestion runs at a time (the local model works on one thread).
    if (m_llmJob) {
        const QString prev = m_llmMeetingId;
        abortLlm();
        if (!prev.isEmpty() && prev != meetingId) publish(prev, m_pending.value(prev));
    }
    LlmSession session = m_d.llm(meetingId, this);
    if (!session.suggester) {
        publish(meetingId, m_pending.value(meetingId));
        return;
    }
    providers::TagSuggestRequest req;
    req.meetingId = meetingId;
    req.summaryText = text;
    req.seeds = suggested;
    req.model = session.model;
    req.systemPrompt = session.systemPrompt;
    m_llmProvider = dynamic_cast<QObject*>(session.suggester);
    m_llmMeetingId = meetingId;
    providers::TagSuggestJob* job = session.suggester->suggest(req);
    m_llmJob = job;
    const auto done = session.done;
    connect(job, &providers::CapabilityJob::finished, this, [this, job, meetingId, done]() {
        const QVector<TagSuggestion> list = job->result();
        if (m_llmProvider) m_llmProvider->deleteLater();
        m_llmProvider = nullptr;
        m_llmJob = nullptr;
        m_llmMeetingId.clear();
        if (done) done(true);
        // An empty LLM response: the earlier suggestions stay.
        publish(meetingId, list.isEmpty() ? m_pending.value(meetingId) : list);
    });
    connect(job, &providers::CapabilityJob::failed, this, [this, meetingId, done](const tanara::Error& error) {
        if (m_llmProvider) m_llmProvider->deleteLater();
        m_llmProvider = nullptr;
        m_llmJob = nullptr;
        m_llmMeetingId.clear();
        if (done) done(false);
        // A suggestion is not an obstacle: on error there is no popup, only a log entry; the earlier list stays.
        qWarning().noquote() << "[tags] LLM-javaslat sikertelen:" << meetingId << error.message;
        publish(meetingId, m_pending.value(meetingId));
    });
}

bool TagSuggestionService::cancel(const QString& meetingId)
{
    bool any = m_requests.remove(meetingId);
    if (auto run = m_runs.find(meetingId); run != m_runs.end()) {
        any = any || run->publish || run->llm;
        // A computation already on the pool finishes (and fills the cache); nobody is waiting for it.
        run->publish = false;
        run->llm = false;
        if (!run->running) m_runs.erase(run);
    }
    if (m_llmJob && m_llmMeetingId == meetingId) {
        abortLlm();
        any = true;
    }
    // The view leaves its "computing" state: the published list goes out again as it was.
    if (any) publish(meetingId, m_pending.value(meetingId));
    return any;
}

void TagSuggestionService::applyEmbeddingSettings(bool restart)
{
    if (!m_d.embeddingPreparer) return;
    const Config c = config();
    const QString& id = c.embeddingId;
    const QString& model = c.embeddingModel;
    const QString key = id.isEmpty() || model.isEmpty()
        ? QString() : id + QLatin1Char('|') + c.embeddingBaseUrl.trimmed() + QLatin1Char('|') + model;
    if (key == m_embeddingKey && restart) return;   // another setting changed
    const QString oldModel = m_d.embeddingPreparer->state().model;
    const bool hadProvider = !m_embeddingKey.isEmpty();
    m_embeddingKey = key;

    EmbeddingPreparer::ProviderFactory factory;
    if (!key.isEmpty() && m_d.embedder) {
        factory = [this, id](QObject* parent) -> providers::ITextEmbedder* {
            return m_d.embedder(id, parent);
        };
    }
    // A model change: the old embeddings cannot be compared to the new ones, so start over.
    if (restart && hadProvider && !key.isEmpty() && oldModel != model && m_d.embeddings)
        m_d.embeddings->invalidateAll();
    m_d.embeddingPreparer->setProvider(key.isEmpty() ? QString() : id, model, factory);
    if (restart && !key.isEmpty()) m_d.embeddingPreparer->start();
}

} // namespace tanara::library

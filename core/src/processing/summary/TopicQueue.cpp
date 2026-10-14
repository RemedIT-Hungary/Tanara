#include "tanara/processing/summary/TopicQueue.h"

#include "tanara/library/jobs/JobStats.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/processing/summary/ComplexSummaryService.h"
#include "tanara/providers/chat/IChatProvider.h"
#include "tanara/providers/chat/ILocalModelServer.h"
#include "tanara/providers/chat/LlmContext.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QUuid>

#include <algorithm>

namespace tanara::processing {

using library::SummaryRepository;

TopicQueue::TopicQueue(SummarizationService* owner)
    : QObject(nullptr), m_owner(owner)
{
}

TopicQueue::~TopicQueue() = default;

bool TopicQueue::isQueued(const QString& meetingId, const QString& topicId) const
{
    return std::any_of(m_queue.cbegin(), m_queue.cend(),
        [&](const TopicJob& j) { return j.meetingId == meetingId && j.topic.id == topicId; });
}

void TopicQueue::updateCounts(const QString& meetingId)
{
    library::MeetingJobTracker* jobs = m_owner->m_deps.jobs;
    if (!jobs->isRunning(meetingId, JobKind::AnalyzeTopics)) return;
    const QPair<int, int> c = m_counts.value(meetingId);
    jobs->setCounts(meetingId, JobKind::AnalyzeTopics, c.first + c.second, m_batchTotal.value(meetingId));
}

void TopicQueue::clearActive()
{
    m_activeTopicId.clear();
    m_activeCall = {};
}

// ---- pass 1: topic extraction ------------------------------------------------------------------------

void TopicQueue::extractTopics(const QString& meetingId)
{
    SummarizationService* o = m_owner;
    library::MeetingJobTracker* jobs = o->m_deps.jobs;
    if (jobs->isRunning(meetingId, JobKind::ExtractTopics))
        return;   // already running

    Meeting m = o->m_deps.store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    // If a (possibly edited) topic list already exists, we return that; no re-extraction.
    const QVector<SummaryTopic> existing = SummaryRepository::readTopics(m.folder);
    if (!existing.isEmpty()) { emit topicsReady(m.id, existing); return; }

    providers::ReadinessResult res = o->m_deps.llm->readiness(meetingId);
    if (!res.runnable) { emit errorOccurred(res.detail); return; }

    const MergedTranscript merged = o->transcriptFor(m);
    if (merged.tokens.isEmpty()) { emit errorOccurred(tr("Nincs átirat — előbb futtass átírást.")); return; }
    const QString transcriptMd = merged.renderMarkdown();

    const ProcessingSettings s = o->m_deps.llm->processingSettings();
    const QString llmId = o->m_deps.llm->chatProviderId();
    Run run;
    providers::ProviderConfig cfg = o->m_deps.llm->config(run, meetingId, QStringLiteral("topics"));
    const auto sink = std::make_shared<FailureSink>();
    providers::IChatProvider* provider = o->createChat(run, QStringLiteral("complex"), cfg, sink);
    if (!provider) { emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(llmId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new ComplexSummaryService(provider, o);
    svc->setLanguage(s.summaryLanguage);
    emit jobProgress(meetingId, run ? tr("Témák kigyűjtése a Tanara Cloudban…")
                                    : tr("Témák kigyűjtése a helyi modellel…"));

    const QString key = SummarizationService::callKey(meetingId, JobKind::ExtractTopics);
    const QString statsKey = QStringLiteral("topics/%1/%2").arg(llmId, cfg.model);
    const double units = double(transcriptMd.size());
    jobs->begin(meetingId, JobKind::ExtractTopics, tr("Témák javaslása"));
    jobs->setEstimate(meetingId, JobKind::ExtractTopics, o->m_deps.jobStats->estimateSec(statsKey, units));
    o->m_calls.insert(key, SummarizationService::ActiveCall{svc, providerObj, run, {}});
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();

    connect(svc, &ComplexSummaryService::topicsReady, this,
            [this, o, jobs, id = m.id, folder = m.folder, providerObj, svc, run, key, statsKey, units, clock]
            (const QVector<SummaryTopic>& topics) {
        o->m_calls.remove(key);
        if (QDir(folder).exists())   // (the meeting may have been deleted meanwhile)
            SummaryRepository::writeTopics(folder, topics);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        // Cloud: topic gathering is the FIRST part of the complex run; the run (and the X-Tanara-Job-Id) stays
        // open, the topic analyses and the merge use the same one; at the end of the run the total is shown
        // (one run = one id). Here only the balance chip refreshes.
        if (run) {
            m_complexRuns.insert(id, run);
            o->m_deps.llm->refreshAccount(run);
        }
        o->m_deps.jobStats->addSample(statsKey, double(clock->elapsed()) / 1000.0, units);
        jobs->finish(id, JobKind::ExtractTopics);
        emit topicsChanged(id);
        emit topicsReady(id, topics);
    });
    const QString topicPrompt = SummarizationService::resolvedPrompt(s, s.topicExtractionPrompt, "topic");
    const auto ctx = std::make_shared<SummarizationService::ContextState>();
    ctx->need = providers::llmctx::callContextNeed(topicPrompt.size() + transcriptMd.size()
                                        + m.contextNote.size() + 300, cfg.maxTokens);
    connect(svc, &ComplexSummaryService::failed, this,
            [o, jobs, id = m.id, providerObj, svc, run, key, sink, ctx](const QString& e) {
        o->m_calls.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        const JobError je = o->describeFailure(JobKind::ExtractTopics, e, run, sink, ctx);
        jobs->fail(id, JobKind::ExtractTopics, je);
        o->failRun(run, tr("Téma-kinyerés hiba: %1").arg(je.message));
    });

    providers::ILocalModelServer* prep = o->prepareLlm(m.id, JobKind::ExtractTopics, cfg, bool(run), ctx,
        [svc = QPointer<ComplexSummaryService>(svc), transcriptMd, note = m.contextNote.trimmed(),
         topicPrompt, cfg, clock]() {
            clock->restart();   // the model load does not count in the run's speed (estimate)
            if (svc) svc->requestTopics(transcriptMd, note, topicPrompt, cfg.model, cfg.temperature, cfg.maxTokens);
        },
        [this, o, jobs, id = m.id, providerObj = QPointer<QObject>(providerObj),
         svc = QPointer<ComplexSummaryService>(svc), key](const JobError& je) {
            o->m_calls.remove(key);
            if (providerObj) providerObj->deleteLater();
            if (svc) svc->deleteLater();
            jobs->fail(id, JobKind::ExtractTopics, je);
            emit errorOccurred(tr("Téma-kinyerés hiba: %1").arg(je.message));
        });
    if (prep && o->m_calls.contains(key)) o->m_calls[key].preparer = prep;
}

// ---- the topic list -------------------------------------------------------------------------------------

QVector<SummaryTopic> TopicQueue::topics(const QString& meetingId) const
{
    const Meeting m = m_owner->m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return {};
    return SummaryRepository::readTopics(m.folder);
}

QVector<TopicAnalysis> TopicQueue::analyses(const QString& meetingId) const
{
    const Meeting m = m_owner->m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return {};
    return SummaryRepository::readAnalyses(m.folder);
}

QVector<TopicStatus> TopicQueue::statuses(const QString& meetingId) const
{
    QVector<TopicStatus> out;
    const Meeting m = m_owner->m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return out;
    const QVector<SummaryTopic> topics = SummaryRepository::readTopics(m.folder);
    QSet<QString> done;
    for (const TopicAnalysis& a : SummaryRepository::readAnalyses(m.folder))
        done.insert(a.topicId);
    const QHash<QString, JobError> errors = m_owner->m_deps.jobs->topicErrors(meetingId);

    out.reserve(topics.size());
    for (const SummaryTopic& t : topics) {
        TopicStatus st;
        st.topicId = t.id;
        const bool running = m_active && m_activeMeetingId == meetingId && m_activeTopicId == t.id;
        const bool queued = isQueued(meetingId, t.id);
        if (running) {
            st.state = TopicState::Running;
        } else if (queued) {
            st.state = TopicState::Queued;
        } else if (errors.contains(t.id)) {
            // The LAST attempt failed (this shows even if a finished analysis exists from earlier; that stays on
            // disk; a new successful run or a retry clears the error).
            st.state = TopicState::Failed;
            st.error = errors.value(t.id).message;
            st.errorDetail = errors.value(t.id).detail;
            st.fixActionHint = errors.value(t.id).fixActionHint;
        } else if (done.contains(t.id)) {
            st.state = TopicState::Done;
        }
        out.append(st);
    }
    return out;
}

QVector<SummaryTopic> TopicQueue::setTopics(const QString& meetingId, const QVector<SummaryTopic>& topics)
{
    const Meeting m = m_owner->m_deps.store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return {}; }

    QVector<SummaryTopic> saved;
    QSet<QString> ids;
    for (SummaryTopic t : topics) {
        t.title = t.title.trimmed();
        t.summary = t.summary.trimmed();
        if (t.title.isEmpty()) continue;                      // a topic without a title cannot be saved
        if (t.id.isEmpty() || ids.contains(t.id))             // a new (or duplicate-id) topic
            t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ids.insert(t.id);
        saved.append(t);
    }
    // The deleted topics: out of the queue / the running one cancelled, their remembered error cleared.
    for (const SummaryTopic& old : SummaryRepository::readTopics(m.folder)) {
        if (ids.contains(old.id)) continue;
        cancelTopic(meetingId, old.id);
        m_owner->m_deps.jobs->clearTopicError(meetingId, old.id);
    }
    SummaryRepository::writeTopics(m.folder, saved);
    emit topicsChanged(meetingId);
    return saved;
}

// ---- pass 2: the analysis queue -------------------------------------------------------------------------

void TopicQueue::generate(const QString& meetingId, const QVector<SummaryTopic>& topics)
{
    Meeting m = m_owner->m_deps.store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    if (topics.isEmpty()) { emit errorOccurred(tr("Nincs téma a komplex összefoglalóhoz.")); return; }

    // Persisting the (edited) topic list: it stays resumable.
    SummaryRepository::writeTopics(m.folder, topics);
    emit topicsChanged(meetingId);

    // Only the topics NOT YET ANALYZED go into the queue (finished analyses are on disk), so after a
    // cancel/error a restart resumes where it stopped. Rerunning an already finished topic can be requested
    // with the card's own button (analyze).
    const QVector<TopicAnalysis> existing = SummaryRepository::readAnalyses(m.folder);
    QSet<QString> doneIds;
    for (const TopicAnalysis& a : existing) doneIds.insert(a.topicId);

    QVector<SummaryTopic> missing;
    for (const SummaryTopic& t : topics)
        if (!doneIds.contains(t.id)) missing.append(t);

    // Cloud: the complex summary (topic gathering + topic analyses + reduce) is ONE run with one
    // X-Tanara-Job-Id. If the topic-gathering run (or an already running batch) is still open, we continue it;
    // otherwise (e.g. continuing after a cancel / error, where the run has already closed) a new run starts
    // with a new id.
    SummaryLlmPort* llm = m_owner->m_deps.llm;
    if (llm->cloudSelected()) {
        Run run = m_complexRuns.value(meetingId);
        if (!run) {
            run = llm->newRun(meetingId, QStringLiteral("complex"));
            m_complexRuns.insert(meetingId, run);
        }
        llm->setRunKind(run, QStringLiteral("complex"));
    }
    if (missing.isEmpty()) { finalize(meetingId); return; }
    enqueue(meetingId, missing, /*reduceWhenDone*/ true);
}

void TopicQueue::analyze(const QString& meetingId, const SummaryTopic& topic)
{
    Meeting m = m_owner->m_deps.store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }
    if (topic.id.isEmpty() || topic.title.trimmed().isEmpty()) {
        emit errorOccurred(tr("A témához cím kell az elemzéshez."));
        return;
    }

    // Carrying the (edited) topic into topics.json: the reduce's order/content comes from there.
    QVector<SummaryTopic> topics = SummaryRepository::readTopics(m.folder);
    bool found = false;
    for (SummaryTopic& t : topics)
        if (t.id == topic.id) { t = topic; found = true; break; }
    if (!found) topics.append(topic);
    SummaryRepository::writeTopics(m.folder, topics);
    emit topicsChanged(meetingId);

    enqueue(meetingId, { topic }, /*reduceWhenDone*/ false);
}

void TopicQueue::enqueue(const QString& meetingId, const QVector<SummaryTopic>& topics, bool reduceWhenDone)
{
    library::MeetingJobTracker* jobs = m_owner->m_deps.jobs;
    if (reduceWhenDone) m_reduceWhenDone.insert(meetingId);
    if (!m_counts.contains(meetingId)) m_counts.insert(meetingId, {0, 0});

    int added = 0;
    for (const SummaryTopic& t : topics) {
        // Dedup: if the same topic is already running or queued, it does not go in again.
        if (m_active && m_activeMeetingId == meetingId && m_activeTopicId == t.id)
            continue;
        if (isQueued(meetingId, t.id))
            continue;
        m_queue.append({ meetingId, t });
        ++added;
        jobs->clearTopicError(meetingId, t.id);   // a new attempt: the old error loses its validity
        emit topicAnalysisQueued(meetingId, t.id);
        emit topicStatusChanged(meetingId, t.id);
    }
    // Structured task: ONE "Analyzing topics" per meeting (with count progress).
    if (added > 0 || jobs->isRunning(meetingId, JobKind::AnalyzeTopics)) {
        m_batchTotal[meetingId] += added;
        if (!jobs->isRunning(meetingId, JobKind::AnalyzeTopics))
            jobs->begin(meetingId, JobKind::AnalyzeTopics, tr("Témák elemzése"));
        updateCounts(meetingId);
    }
    if (!m_active)
        startNext();
}

void TopicQueue::startNext()
{
    SummarizationService* o = m_owner;
    library::MeetingJobTracker* jobs = o->m_deps.jobs;
    SummaryLlmPort* llm = o->m_deps.llm;
    // Did we reach the end of the queue? We close the batch per meeting: queueFinished + (if requested and
    // nothing failed) auto-reduce. Jobs of several meetings may in principle mix in the queue, so we close a
    // meeting only when no job is waiting for it.
    if (m_queue.isEmpty()) {
        m_active = false;
        m_activeMeetingId.clear();
        m_activeTopicId.clear();
        m_activeCall = {};
        const auto counts = m_counts;
        m_counts.clear();
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
            const QString& id = it.key();
            const int ok = it.value().first, fail = it.value().second;
            // Structured closing: a failed topic -> the summary step's error (the per-topic details are in
            // statuses); otherwise done.
            m_batchTotal.remove(id);
            if (jobs->isRunning(id, JobKind::AnalyzeTopics)) {
                if (fail > 0) {
                    JobError je;
                    je.message = tr("%1 téma elemzése nem sikerült.").arg(fail);
                    jobs->fail(id, JobKind::AnalyzeTopics, je);
                } else {
                    jobs->finish(id, JobKind::AnalyzeTopics);
                }
            }
            emit topicAnalysisQueueFinished(id, ok, fail);
            const bool wantReduce = m_reduceWhenDone.remove(id);
            if (wantReduce && fail == 0) {
                finalize(id);   // the cloud run continues with the reduce
                continue;
            }
            if (wantReduce)
                emit jobProgress(id, tr(
                    "%1 téma elemzése nem sikerült — futtasd újra a kártyáján, majd kérd a végső összegzést.")
                    .arg(fail));
            // A cloud run closing without a reduce (rerunning one topic, or a non-cloud error): the sum of the
            // charges so far goes to the cost feedback.
            const Run run = m_complexRuns.take(id);
            if (run && !llm->runError(run, nullptr))
                llm->finishRun(run);
        }
        return;
    }

    const TopicJob job = m_queue.takeFirst();
    // An early (pre-provider-call) failure: a remembered topic error + the queue goes on.
    auto failEarly = [this, jobs, &job](const QString& msg) {
        m_counts[job.meetingId].second++;
        JobError je;
        je.message = msg;
        jobs->setTopicError(job.meetingId, job.topic.id, je);
        updateCounts(job.meetingId);
        emit topicAnalysisFailed(job.meetingId, job.topic.id, msg);
        emit topicStatusChanged(job.meetingId, job.topic.id);
        startNext();
    };

    Meeting m = o->m_deps.store->load(job.meetingId);
    if (m.id.isEmpty()) { failEarly(tr("Ismeretlen meeting.")); return; }

    const MergedTranscript merged = o->transcriptFor(m);
    if (merged.tokens.isEmpty()) { failEarly(tr("Nincs átirat — előbb futtass átírást.")); return; }

    const ProcessingSettings s = llm->processingSettings();
    const QString llmId = llm->chatProviderId();
    Run run = m_complexRuns.value(m.id);
    if (llm->cloudSelected() && !run) {   // rerunning one topic without a batch
        run = llm->newRun(m.id, QStringLiteral("complex"));
        m_complexRuns.insert(m.id, run);
    }
    if (run) llm->setRunKind(run, QStringLiteral("complex"));   // after topic gathering, the analysis stage
    providers::ProviderConfig cfg = llm->config(run, m.id, QStringLiteral("complex"));
    const auto sink = std::make_shared<FailureSink>();
    providers::IChatProvider* provider = o->createChat(run, QStringLiteral("complex"), cfg, sink);
    if (!provider) { failEarly(tr("Ismeretlen LLM-provider: %1.").arg(llmId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new ComplexSummaryService(provider, o);
    svc->setLanguage(s.summaryLanguage);

    m_active = true;
    m_activeMeetingId = m.id;
    m_activeTopicId = job.topic.id;
    m_activeCall = SummarizationService::ActiveCall{svc, providerObj, run, {}};
    jobs->setMessage(m.id, JobKind::AnalyzeTopics, tr("„%1” téma elemzése…").arg(job.topic.title));
    emit topicAnalysisStarted(m.id, job.topic.id);
    emit topicStatusChanged(m.id, job.topic.id);
    emit jobProgress(m.id, tr("„%1” téma elemzése…").arg(job.topic.title));

    auto cleanup = [providerObj, svc]() {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
    };

    connect(svc, &ComplexSummaryService::topicAnalysisReady, this,
            [this, jobs, meetingId = m.id, folder = m.folder, cleanup](const TopicAnalysis& a) {
        SummaryRepository::upsertAnalysis(folder, a);   // IMMEDIATELY to disk: the work must not be lost
        m_counts[meetingId].first++;
        cleanup();
        // The topic is no longer "running": the state query (statuses) now gives "done".
        clearActive();
        jobs->clearTopicError(meetingId, a.topicId);
        updateCounts(meetingId);
        emit topicAnalysisReady(meetingId, a);
        emit topicStatusChanged(meetingId, a.topicId);
        startNext();
    });
    const QString transcriptMd = merged.renderMarkdown();
    const QString analysisPrompt = SummarizationService::resolvedPrompt(s, s.topicAnalysisPrompt, "analysis");
    const auto ctx = std::make_shared<SummarizationService::ContextState>();
    const auto prepError = std::make_shared<JobError>();   // the preparation's error (if that failed)
    ctx->need = providers::llmctx::callContextNeed(analysisPrompt.size() + transcriptMd.size() + m.contextNote.size()
                                        + job.topic.title.size() + job.topic.summary.size() + 400,
                                        cfg.maxTokens);
    connect(svc, &ComplexSummaryService::failed, this,
            [this, o, jobs, llm, meetingId = m.id, topicId = job.topic.id, cleanup, run, sink, ctx, prepError]
            (const QString& raw) {
        m_counts[meetingId].second++;
        cleanup();
        clearActive();
        const JobError je = prepError->isValid()
            ? *prepError : o->describeFailure(JobKind::AnalyzeTopics, raw, run, sink, ctx);
        const QString e = je.message;
        jobs->setTopicError(meetingId, topicId, je);
        if (run && llm->runError(run, nullptr)) {
            // Cloud error (402 / 403 / 426 / 429 / 503 ...): the meeting's remaining topics would get the same, so we
            // take them out of the queue; the partial-failure dialog shows the amount charged so far and "Continue"
            // (it pays only for the remaining parts).
            const QString cloudMessage = llm->runErrorMessage(run);
            emit topicAnalysisFailed(meetingId, topicId, cloudMessage.isEmpty() ? e : cloudMessage);
            emit topicStatusChanged(meetingId, topicId);
            for (int i = m_queue.size() - 1; i >= 0; --i) {
                if (m_queue.at(i).meetingId != meetingId) continue;
                const QString dropped = m_queue.takeAt(i).topic.id;
                m_counts[meetingId].second++;
                emit topicAnalysisFailed(meetingId, dropped, tr("Megszakítva"));
                emit topicStatusChanged(meetingId, dropped);
            }
            m_reduceWhenDone.remove(meetingId);
            m_complexRuns.remove(meetingId);
            o->failRun(run, e);
        } else {
            emit topicAnalysisFailed(meetingId, topicId, e);   // the other topics go on
            emit topicStatusChanged(meetingId, topicId);
        }
        updateCounts(meetingId);
        startNext();
    });

    providers::ILocalModelServer* prep = o->prepareLlm(m.id, JobKind::AnalyzeTopics, cfg, bool(run), ctx,
        [svc = QPointer<ComplexSummaryService>(svc), transcriptMd, topic = job.topic,
         note = m.contextNote.trimmed(), analysisPrompt, cfg]() {
            if (svc) svc->requestTopicAnalysis(transcriptMd, topic, note, analysisPrompt,
                                               cfg.model, cfg.temperature, cfg.maxTokens);
        },
        [svc = QPointer<ComplexSummaryService>(svc), prepError](const JobError& je) {
            // The same path as for a provider error: the topic's error is remembered, the queue goes on.
            *prepError = je;
            if (svc) emit svc->failed(je.message);
        });
    if (prep) m_activeCall.preparer = prep;
}

// ---- reduce ---------------------------------------------------------------------------------------------

void TopicQueue::finalize(const QString& meetingId)
{
    SummarizationService* o = m_owner;
    library::MeetingJobTracker* jobs = o->m_deps.jobs;
    SummaryLlmPort* llm = o->m_deps.llm;
    if (jobs->isRunning(meetingId, JobKind::Summarize))
        return;   // a merge is already running

    Meeting m = o->m_deps.store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    // The analyses on disk, in the (edited) order of topics.json; orphan analyses (belonging to a deleted
    // topic) are left out.
    const QVector<SummaryTopic> topics = SummaryRepository::readTopics(m.folder);
    const QVector<TopicAnalysis> all = SummaryRepository::readAnalyses(m.folder);
    QVector<TopicAnalysis> ordered;
    for (const SummaryTopic& t : topics)
        for (const TopicAnalysis& a : all)
            if (a.topicId == t.id) { ordered.append(a); break; }
    if (ordered.isEmpty()) {
        emit errorOccurred(tr("Nincs kész téma-elemzés — előbb futtasd a témánkénti elemzést."));
        return;
    }

    const ProcessingSettings s = llm->processingSettings();
    const QString llmId = llm->chatProviderId();
    Run run = m_complexRuns.take(meetingId);   // the batch's run (if any) continues
    providers::ProviderConfig cfg = llm->config(run, meetingId, QStringLiteral("complex"));
    const auto sink = std::make_shared<FailureSink>();
    providers::IChatProvider* provider = o->createChat(run, QStringLiteral("complex"), cfg, sink);
    if (!provider) { emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(llmId)); return; }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new ComplexSummaryService(provider, o);
    svc->setLanguage(s.summaryLanguage);
    svc->setReducePrompt(SummarizationService::resolvedPrompt(s, QString(), "reduce"));
    emit jobProgress(meetingId, tr("Összegzés (vezetői összefoglaló + teendők)…"));

    const QString key = SummarizationService::callKey(meetingId, JobKind::Summarize);
    jobs->begin(meetingId, JobKind::Summarize, tr("Összegzés készítése"));
    o->m_calls.insert(key, SummarizationService::ActiveCall{svc, providerObj, run, {}});

    auto cleanup = [providerObj, svc]() {
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
    };

    const auto ctx = std::make_shared<SummarizationService::ContextState>();
    {
        qint64 chars = SummarizationService::resolvedPrompt(s, QString(), "reduce").size() + m.contextNote.size() + 400;
        for (const TopicAnalysis& a : ordered) chars += a.renderMarkdown().size() + a.title.size() + 40;
        ctx->need = providers::llmctx::callContextNeed(chars, cfg.maxTokens);
    }
    connect(svc, &ComplexSummaryService::failed, this,
            [o, jobs, meetingId, cleanup, run, key, sink, ctx](const QString& e) {
        o->m_calls.remove(key);
        cleanup();
        const JobError je = o->describeFailure(JobKind::Summarize, e, run, sink, ctx);
        jobs->fail(meetingId, JobKind::Summarize, je);
        o->failRun(run, tr("Komplex összefoglaló hiba: %1").arg(je.message));
    });
    connect(svc, &ComplexSummaryService::reduceReady, this,
            [o, jobs, llm, m, ordered, cleanup, run, key, llmId, model = cfg.model]
            (const QString& execSummary, const QVector<ActionItem>&) {
        o->m_calls.remove(key);
        // Action items from CODE (not from the LLM): we collect the per-topic analyses' action items with a
        // normalized-text dedup. Deterministic, model-independent; only the executive summary is left to the LLM
        // reduce (less room for "thinking out loud").
        QVector<ActionItem> mergedItems;
        QSet<QString> seen;
        for (const TopicAnalysis& a : ordered)
            for (const ActionItem& ai : a.actionItems) {
                const QString norm = ai.text.simplified().toLower();
                if (norm.isEmpty() || seen.contains(norm))
                    continue;
                seen.insert(norm);
                mergedItems.append(ai);
            }
        const QString md = SummaryRepository::renderComplexMarkdown(execSummary, mergedItems, ordered);
        Meeting mm = o->m_deps.store->load(m.id);
        if (mm.id.isEmpty()) {   // the meeting was deleted meanwhile
            cleanup();
            jobs->cancelled(m.id, JobKind::Summarize);
            llm->finishRun(run);
            return;
        }
        // Structured form + metadata (summary.json): executive summary, merged action items, the union of the
        // topics' decisions, and the topic analyses themselves, in order.
        library::SummaryDocument doc;
        doc.exists = true;
        doc.markdown = md;
        doc.summary.execSummary = execSummary;
        doc.summary.actionItems = mergedItems;
        for (const TopicAnalysis& a : ordered)
            for (const QString& dec : a.decisions)
                if (!doc.summary.decisions.contains(dec)) doc.summary.decisions << dec;
        doc.summary.openQuestions = SummaryRepository::openQuestionsOf(ordered);
        doc.topics = ordered;
        doc.meta.createdAt = QDateTime::currentDateTime();
        doc.meta.providerId = llmId;
        doc.meta.model = model;
        doc.meta.mode = library::SummaryMode::Topics;
        doc.meta.contextNote = m.contextNote.trimmed();   // at the start of the final merge
        doc.meta.contextNoteKnown = true;
        o->storeSummary(mm, md, doc);
        cleanup();
        jobs->finish(mm.id, JobKind::Summarize);
        emit o->summaryReady(mm.id, SummaryRepository::markdownPath(mm.folder));
        llm->finishRun(run);   // "The summary cost $0.46 (12 parts)."
    });

    providers::ILocalModelServer* prep = o->prepareLlm(meetingId, JobKind::Summarize, cfg, bool(run), ctx,
        [svc = QPointer<ComplexSummaryService>(svc), ordered, note = m.contextNote.trimmed(), cfg]() {
            if (svc) svc->requestReduce(ordered, note, cfg.model, cfg.temperature, cfg.maxTokens);
        },
        [this, o, jobs, meetingId, key, providerObj = QPointer<QObject>(providerObj),
         svc = QPointer<ComplexSummaryService>(svc)](const JobError& je) {
            o->m_calls.remove(key);
            if (providerObj) providerObj->deleteLater();
            if (svc) svc->deleteLater();
            jobs->fail(meetingId, JobKind::Summarize, je);
            emit errorOccurred(tr("Komplex összefoglaló hiba: %1").arg(je.message));
        });
    if (prep && o->m_calls.contains(key)) o->m_calls[key].preparer = prep;
}

// ---- cancelling ---------------------------------------------------------------------------------------

bool TopicQueue::cancelTopic(const QString& meetingId, const QString& topicId)
{
    bool any = false;
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        const TopicJob& j = m_queue.at(i);
        if (j.meetingId != meetingId || j.topic.id != topicId) continue;
        m_queue.removeAt(i);
        if (m_batchTotal.value(meetingId) > 0) m_batchTotal[meetingId]--;
        any = true;
    }
    const bool active = m_active && m_activeMeetingId == meetingId && m_activeTopicId == topicId;
    if (active) {
        m_owner->abortCall(m_activeCall);
        clearActive();
        if (m_batchTotal.value(meetingId) > 0) m_batchTotal[meetingId]--;
        any = true;
    }
    if (!any) return false;
    // After removing a topic the batch is no longer complete, so there is no automatic final summary.
    m_reduceWhenDone.remove(meetingId);
    updateCounts(meetingId);
    emit topicAnalysisFailed(meetingId, topicId, tr("Megszakítva"));
    emit topicStatusChanged(meetingId, topicId);
    if (active)
        startNext();   // the queue goes on (or closes)
    return true;
}

bool TopicQueue::cancelBatch(const QString& meetingId)
{
    library::MeetingJobTracker* jobs = m_owner->m_deps.jobs;
    SummaryLlmPort* llm = m_owner->m_deps.llm;
    bool any = false;
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (m_queue.at(i).meetingId != meetingId) continue;
        const QString dropped = m_queue.takeAt(i).topic.id;
        emit topicAnalysisFailed(meetingId, dropped, tr("Megszakítva"));
        emit topicStatusChanged(meetingId, dropped);
        any = true;
    }
    const bool activeMine = m_active && m_activeMeetingId == meetingId && !m_activeTopicId.isEmpty();
    if (activeMine) {
        const QString tid = m_activeTopicId;
        m_owner->abortCall(m_activeCall);
        clearActive();
        emit topicAnalysisFailed(meetingId, tid, tr("Megszakítva"));
        emit topicStatusChanged(meetingId, tid);
        any = true;
    }
    if (!any && !jobs->isRunning(meetingId, JobKind::AnalyzeTopics)) return false;
    // Closing the batch: no auto-summary, no "N topics failed" message; the analyses already finished stay on
    // disk, a continuation picks up from there.
    const QPair<int, int> counts = m_counts.take(meetingId);
    m_reduceWhenDone.remove(meetingId);
    m_batchTotal.remove(meetingId);
    const Run run = m_complexRuns.take(meetingId);
    if (run && !llm->runError(run, nullptr))
        llm->finishRun(run);
    emit topicAnalysisQueueFinished(meetingId, counts.first, counts.second);
    jobs->cancelled(meetingId, JobKind::AnalyzeTopics);
    if (activeMine)
        startNext();   // other meetings' topics go on
    return true;
}

} // namespace tanara::processing

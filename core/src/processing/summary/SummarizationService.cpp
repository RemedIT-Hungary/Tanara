#include "tanara/processing/summary/SummarizationService.h"

#include "tanara/foundation/text/Text.h"
#include "tanara/library/jobs/JobErrors.h"
#include "tanara/library/jobs/JobStats.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/processing/summary/TopicQueue.h"
#include "tanara/providers/chat/IChatProvider.h"
#include "tanara/providers/core/Routes.h"
#include "tanara/providers/chat/ILocalModelServer.h"
#include "tanara/providers/chat/LlmContext.h"
#include "tanara/providers/core/ProviderRegistry.h"

#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>

namespace tanara::processing {

using library::SummaryRepository;

SummarizationService::SummarizationService(Deps deps, QObject* parent)
    : QObject(parent), m_deps(std::move(deps))
{
    m_topics = std::make_unique<TopicQueue>(this);
}

SummarizationService::~SummarizationService() = default;

QString SummarizationService::callKey(const QString& meetingId, JobKind kind)
{
    return meetingId + QLatin1Char('|') + QString::number(int(kind));
}

QString SummarizationService::serverKey(const providers::ProviderConfig& cfg)
{
    return providers::llmctx::nativeApiRoot(cfg.baseUrl) + QLatin1Char('|') + cfg.model.trimmed();
}

bool SummarizationService::otherLlmBusy() const
{
    return m_calls.size() + (m_topics->busy() ? 1 : 0) > 1;
}

QString SummarizationService::resolvedPrompt(const ProcessingSettings& s, const QString& userOverride, const char* id)
{
    const QString base = userOverride.trimmed().isEmpty()
        ? promptDefault(QLatin1String(id))   // the current data folder
        : userOverride;
    return applySummaryLanguage(base, s.summaryLanguage);
}

SummaryRequest SummarizationService::requestFor(const ProcessingSettings& s, const MergedTranscript& merged,
                                                const Meeting& m, const QString& model, int maxTokens)
{
    constexpr int kNotesMaxTokens = 3000;   // one part note fits well within it
    SummaryRequest req;
    req.transcript = merged;
    req.contextNotes = m.contextNote.trimmed();
    req.singlePrompt = resolvedPrompt(s, s.summaryPrompt, "single");
    req.notesPrompt = resolvedPrompt(s, s.notesPrompt, "notes");
    req.mergePrompt = resolvedPrompt(s, s.mergePrompt, "merge");
    req.language = s.summaryLanguage;
    req.model = model;
    req.maxTokens = maxTokens > 0 ? maxTokens : 8000;
    req.notesMaxTokens = qMin(req.maxTokens, kNotesMaxTokens);
    if (!m.folder.isEmpty())
        req.cachePath = QDir(m.folder).filePath(SummaryService::cacheFileName());
    return req;
}

MergedTranscript SummarizationService::transcriptFor(const Meeting& m) const
{
    MergedTranscript merged = m_deps.cachedTranscript ? m_deps.cachedTranscript(m.id) : MergedTranscript();
    if (merged.tokens.isEmpty())
        merged = library::TranscriptRepository::readTokens(m.folder);
    if (!merged.tokens.isEmpty() && m_deps.resolveSpeakers)
        m_deps.resolveSpeakers(merged, m);   // the LLM should see the real names
    // The unnamed speakers' raw keys -> the label in the summary language (the prompt explains the same label).
    applyPromptSpeakerLabels(merged, m_deps.llm ? m_deps.llm->processingSettings().summaryLanguage : QString());
    return merged;
}

providers::IChatProvider* SummarizationService::createChat(const QString& meetingId, const Run& run,
                                                           const QString& summaryMode,
                                                           const providers::ProviderConfig& cfg,
                                                           const FailureSinkPtr& sink)
{
    providers::ProviderContext pctx = m_deps.llm->context(run, summaryMode);
    captureFailures(pctx, sink);
    return m_deps.registry->create<providers::IChatProvider>(m_deps.llm->chatProviderId(meetingId), pctx, cfg, this);
}

void SummarizationService::abortCall(const ActiveCall& call)
{
    if (call.preparer) {
        call.preparer->cancel();   // the query / load request is dropped (no signal comes)
        call.preparer->deleteLater();
    }
    if (auto* summary = qobject_cast<SummaryService*>(call.svc.data()))
        summary->cancel();   // the running call is cancelled, the next one does not start (the part notes stay)
    if (call.svc) {
        call.svc->disconnect(this);
        call.svc->disconnect(m_topics.get());
        call.svc->deleteLater();
    }
    if (call.provider) call.provider->deleteLater();
}

JobError SummarizationService::describeFailure(JobKind kind, const QString& raw, const Run& run,
                                               const FailureSinkPtr& sink, const ContextPtr& ctx) const
{
    const JobError je = describeFailureImpl(kind, raw, run, sink, ctx);
    // The failure always reaches the log (the UI shows the message, the log says where it went wrong).
    qWarning().noquote().nospace() << "[LLM] a feladat nem sikerült: feladat=" << int(kind)
        << " provider=" << (run ? providers::CloudProviderId : m_deps.llm->chatProviderId(QString()))
        << " url=" << (run ? QStringLiteral("cloud") : m_deps.llm->localConfig().baseUrl)
        << " hiba=" << raw.simplified().left(300)
        << " | " << je.message << (je.detail.isEmpty() ? QString() : QStringLiteral(" | ") + je.detail);
    return je;
}

JobError SummarizationService::describeFailureImpl(JobKind kind, const QString& raw, const Run& run,
                                                   const FailureSinkPtr& sink, const ContextPtr& ctx) const
{
    Error cloudError;
    if (run && m_deps.llm->runError(run, &cloudError))
        return library::describeCloudFailure(kind, cloudError);
    library::ContextFailureHint hint;
    hint.cloud = bool(run);
    if (ctx) {
        hint.lmStudio = ctx->lmStudio && !run;
        if (ctx->need > 0) hint.recommendedContext = providers::llmctx::contextStepFor(ctx->need);
    }
    return library::describeJobFailure(kind, raw, (sink && sink->has) ? &sink->ex : nullptr, &hint);
}

void SummarizationService::failRun(const Run& run, const QString& fallbackMessage)
{
    if (!m_deps.llm->failRun(run))   // refund / structured error report (the app's cloud ledger)
        emit errorOccurred(fallbackMessage);
}

providers::ILocalModelServer* SummarizationService::prepareLlm(const QString& meetingId, JobKind kind,
                                                               const providers::ProviderConfig& cfg, bool cloud,
                                                               const ContextPtr& ctx, std::function<void()> go,
                                                               std::function<void(const JobError&)> onFail)
{
    // Behind Tanara Cloud there is no native API: neither a probe nor a load.
    if (cloud) { go(); return nullptr; }
    // The selected chat provider's local-server handler (LM Studio for "openai-compat"); none = nothing
    // to prepare.
    const providers::ProviderContext pctx = m_deps.llm->context(Run(), QString());
    auto* prep = m_deps.registry->create<providers::ILocalModelServer>(m_deps.llm->chatProviderId(meetingId), pctx, cfg, this);
    if (!prep) { go(); return nullptr; }
    prep->setBusyCheck([this] { return otherLlmBusy(); });
    const int floor = m_contextFloor.take(serverKey(cfg));
    library::MeetingJobTracker* jobs = m_deps.jobs;
    connect(prep, &providers::ILocalModelServer::loadingStarted, prep, [this, jobs, prep, meetingId, kind](int ctxLen) {
        // Visible first stage: "Loading the model" (cancellable like the rest of the task).
        QVector<JobStage> stages = jobs->job(meetingId, kind).stages;
        const QStringList others = prep->serverInfo().otherLoaded;
        QString detail = tr("%1 tokenes kontextus, egy szálon").arg(QStringLiteral("%L1").arg(ctxLen));
        if (!others.isEmpty())
            detail += tr(" · a szerveren más modell is betöltve: %1").arg(others.join(QStringLiteral(", ")));
        stages.prepend({ QStringLiteral("model"), tr("Modell betöltése"), StageState::Running, -1, detail });
        jobs->setStages(meetingId, kind, stages);
        jobs->setMessage(meetingId, kind, tr("Modell betöltése…"));
        emit jobProgress(meetingId, tr("Modell betöltése (%1 tokenes kontextus)…")
                                        .arg(QStringLiteral("%L1").arg(ctxLen)));
    });
    connect(prep, &providers::ILocalModelServer::ready, prep, [jobs, prep, meetingId, kind, ctx, go] {
        ctx->lmStudio = prep->serverInfo().isLmStudio();
        ctx->loadedContext = prep->loadedContext();
        if (jobs->job(meetingId, kind).stage(QStringLiteral("model")))
            jobs->setStage(meetingId, kind, QStringLiteral("model"), StageState::Done);
        prep->deleteLater();
        go();
    });
    connect(prep, &providers::ILocalModelServer::failed, prep, [jobs, prep, meetingId, kind, onFail](JobError e) {
        e.kind = kind;
        if (jobs->job(meetingId, kind).stage(QStringLiteral("model")))
            jobs->setStage(meetingId, kind, QStringLiteral("model"), StageState::Failed);
        prep->deleteLater();
        onFail(e);
    });
    prep->start(ctx->need, floor);
    return prep;
}

void SummarizationService::storeSummary(Meeting& mm, const QString& md, const library::SummaryDocument& doc)
{
    SummaryRepository::writeMarkdown(mm.folder, md);
    // The structured form + the provenance (summary.json); the summary view draws from this.
    SummaryRepository::save(mm.folder, doc);
    // a copy into the notes (vault) folder
    const QString noteName = QStringLiteral("%1 %2.md")
        .arg(mm.startedAt.toString(QStringLiteral("yyyy-MM-dd")), text::slug(mm.title, QStringLiteral("meeting")));
    SummaryRepository::writeNoteCopy(m_deps.notesDir, noteName, md);
    mm.hasSummary = true;
    m_deps.store->saveMeeting(mm);
}

// ---- quick summary ---------------------------------------------------------------------------------

void SummarizationService::summarize(const QString& meetingId)
{
    library::MeetingJobTracker* jobs = m_deps.jobs;
    if (jobs->isRunning(meetingId, JobKind::Summarize))
        return;   // already running

    Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) { emit errorOccurred(tr("Ismeretlen meeting: %1").arg(meetingId)); return; }

    providers::ReadinessResult res = m_deps.llm->readiness(meetingId);
    if (!res.runnable) { emit errorOccurred(res.detail); return; }

    // A content guard: the readiness gates on the meeting.hasTranscript flag, but if tokens.json is
    // missing/empty (deleted by hand, a partial write, a wrongly copied meeting), no summary should start on
    // an empty transcript; this mirrors the original content-based guard.
    const MergedTranscript merged = transcriptFor(m);
    if (merged.tokens.isEmpty()) {
        emit errorOccurred(tr("Nincs átirat — előbb futtass átírást."));
        return;
    }

    const ProcessingSettings s = m_deps.llm->processingSettings();
    const QString llmId = m_deps.llm->chatProviderId(meetingId);
    Run run;   // cloud: ONE run (one job id) for all the note and merge calls
    providers::ProviderConfig cfg = m_deps.llm->config(run, meetingId, QStringLiteral("summary"));
    const auto sink = std::make_shared<FailureSink>();
    providers::IChatProvider* provider = createChat(meetingId, run, QStringLiteral("quick"), cfg, sink);
    if (!provider) {
        emit errorOccurred(tr("Ismeretlen LLM-provider: %1.").arg(llmId));
        return;
    }
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    auto* svc = new SummaryService(provider, this);
    emit jobProgress(meetingId, run ? tr("Összefoglalás a Tanara Cloudban…")
                                    : tr("Összefoglalás a helyi modellel…"));

    SummaryRequest req = requestFor(s, merged, m, cfg.model, cfg.maxTokens);
    req.temperature = cfg.temperature;
    // For the source references: the utterances with their current (resolved) speaker; this becomes the
    // summary's speaker snapshot (the targeted staleness compares against it).
    if (m_deps.sourceLines) req.sourceLines = m_deps.sourceLines(m);
    req.adaptToContext = !run;   // with Tanara Cloud the context is the gateway's business
    const SummaryPlan plan = SummaryService::plan(req);
    const auto ctx = std::make_shared<ContextState>();
    ctx->need = SummaryService::contextNeed(req);

    // Structured task: for a short meeting one stage (one call), for a longer one "Notes per part" (k / n
    // parts) + "Summarizing". The estimate comes only from earlier runs measured with the same model (unit:
    // the transcript's length).
    const QString key = callKey(meetingId, JobKind::Summarize);
    const QString statsKey = QStringLiteral("summary-notes/%1/%2").arg(llmId, cfg.model);
    const double units = double(merged.renderMarkdown().size());
    QVector<JobStage> stages;
    if (plan.singleCall) {
        stages.append({ QStringLiteral("single"), tr("Összefoglalás"), StageState::Waiting, -1, QString() });
    } else {
        // Parts finished in an earlier run are visible at startup (during the model load too).
        QString notesDetail = tr("%n rész", nullptr, plan.parts);
        if (plan.cachedParts > 0) notesDetail += tr(" · %n korábbi futásból", nullptr, plan.cachedParts);
        stages.append({ QStringLiteral("notes"), tr("Jegyzetelés részenként"), StageState::Waiting, -1,
                        notesDetail });
        stages.append({ QStringLiteral("merge"), tr("Összegzés"), StageState::Waiting, -1, QString() });
    }
    jobs->begin(meetingId, JobKind::Summarize, tr("Összefoglaló készítése"), stages);
    if (!plan.singleCall)
        jobs->setCounts(meetingId, JobKind::Summarize, plan.cachedParts, plan.parts);
    jobs->setEstimate(meetingId, JobKind::Summarize, m_deps.jobStats->estimateSec(statsKey, units));
    m_calls.insert(key, ActiveCall{svc, providerObj, run, {}});
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();

    // Context-adapted re-split: the stages switch to per-part notes.
    connect(svc, &SummaryService::partsChanged, this, [this, jobs, meetingId](int parts, int cached) {
        if (!jobs->isRunning(meetingId, JobKind::Summarize)) return;
        QVector<JobStage> stages;
        if (const JobStage* model = jobs->job(meetingId, JobKind::Summarize).stage(QStringLiteral("model")))
            stages.append(*model);
        stages.append({ QStringLiteral("notes"), tr("Jegyzetelés részenként"), StageState::Waiting, -1,
                        tr("%n rész (a modell kontextusához igazítva)", nullptr, parts) });
        stages.append({ QStringLiteral("merge"), tr("Összegzés"), StageState::Waiting, -1, QString() });
        jobs->setStages(meetingId, JobKind::Summarize, stages);
        jobs->setCounts(meetingId, JobKind::Summarize, cached, parts);
    });
    connect(svc, &SummaryService::contextLimitDetected, this, [this, cfg, ctx](int tokens) {
        if (!ctx->lmStudio) m_learnedContext.insert(serverKey(cfg), tokens);
    });
    connect(svc, &SummaryService::progress, this,
            [this, jobs, meetingId, cached = plan.cachedParts](const QString& stage, int done, int total) {
        if (!jobs->isRunning(meetingId, JobKind::Summarize)) return;
        if (stage == QLatin1String("single")) {
            jobs->setStage(meetingId, JobKind::Summarize, stage, StageState::Running);
            return;
        }
        if (stage == QLatin1String("notes")) {
            QString detail = done < total ? tr("%1/%2. rész").arg(done + 1).arg(total)
                                          : tr("%n rész kész", nullptr, total);
            if (cached > 0) detail += tr(" · %n korábbi futásból", nullptr, cached);
            jobs->setStage(meetingId, JobKind::Summarize, stage,
                           done < total ? StageState::Running : StageState::Done, -1, detail);
            jobs->setCounts(meetingId, JobKind::Summarize, done, total);
            if (done < total)
                emit jobProgress(meetingId, tr("Jegyzetelés: %1/%2. rész…").arg(done + 1).arg(total));
            return;
        }
        if (stage == QLatin1String("merge")) {
            jobs->setStage(meetingId, JobKind::Summarize, QStringLiteral("merge"), StageState::Running);
            emit jobProgress(meetingId, tr("Összegzés a részjegyzetekből…"));
        }
    });
    connect(svc, &SummaryService::summaryReady, this,
            [this, jobs, meetingId, providerObj, svc, run, key, statsKey, units, clock, llmId,
             model = cfg.model, note = req.contextNotes](const Summary& sum) {
        m_calls.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        // A FRESH meeting (it may have changed / been deleted during the summarization).
        Meeting mm = m_deps.store->load(meetingId);
        if (mm.id.isEmpty()) {
            jobs->cancelled(meetingId, JobKind::Summarize);
            m_deps.llm->finishRun(run);
            return;
        }
        const QString md = sum.renderMarkdown();
        library::SummaryDocument doc;
        doc.exists = true;
        doc.summary = sum;
        doc.markdown = md;
        doc.meta.createdAt = QDateTime::currentDateTime();
        doc.meta.providerId = llmId;
        doc.meta.model = model;
        doc.meta.mode = library::SummaryMode::Quick;
        doc.meta.contextNote = note;            // the note it was made with
        doc.meta.contextNoteKnown = true;
        storeSummary(mm, md, doc);
        m_deps.jobStats->addSample(statsKey, double(clock->elapsed()) / 1000.0, units);
        jobs->finish(meetingId, JobKind::Summarize);
        emit summaryReady(mm.id, SummaryRepository::markdownPath(mm.folder));
        m_deps.llm->finishRun(run);
    });
    connect(svc, &SummaryService::summaryFailed, this,
            [this, jobs, meetingId, providerObj, svc, run, key, sink, ctx](const QString& e) {
        m_calls.remove(key);
        if (providerObj) providerObj->deleteLater();
        svc->deleteLater();
        const JobError je = describeFailure(JobKind::Summarize, e, run, sink, ctx);
        jobs->fail(meetingId, JobKind::Summarize, je);
        failRun(run, tr("Összefoglaló hiba: %1").arg(je.message));
    });

    // Preparing the model (LM Studio: loading with the needed context), then it starts.
    providers::ILocalModelServer* prep = prepareLlm(meetingId, JobKind::Summarize, cfg, bool(run), ctx,
        [this, svc = QPointer<SummaryService>(svc), req, ctx, cfg, clock]() mutable {
            if (!svc) return;
            clock->restart();   // the model load does not count in the run's speed (estimate)
            req.contextLimit = ctx->loadedContext > 0 ? ctx->loadedContext
                                                      : m_learnedContext.value(serverKey(cfg));
            svc->summarize(req);
        },
        [this, jobs, meetingId, providerObj = QPointer<QObject>(providerObj),
         svc = QPointer<SummaryService>(svc), key](const JobError& je) {
            m_calls.remove(key);
            if (providerObj) providerObj->deleteLater();
            if (svc) svc->deleteLater();
            jobs->fail(meetingId, JobKind::Summarize, je);
            emit errorOccurred(tr("Összefoglaló hiba: %1").arg(je.message));
        });
    if (prep && m_calls.contains(key)) m_calls[key].preparer = prep;
}

// ---- queries, flags, the context request, cancelling -------------------------------------------------

library::SummaryDocument SummarizationService::document(const QString& meetingId) const
{
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty()) return {};
    return SummaryRepository::load(m.folder);
}

bool SummarizationService::flagStatement(const QString& meetingId, const QString& statementId)
{
    const Meeting m = m_deps.store->load(meetingId);
    if (m.id.isEmpty() || m.folder.isEmpty()) return false;
    if (!SummaryRepository::setStatementFlagged(m.folder, statementId, true)) return false;
    emit summaryStatementsChanged(meetingId);
    return true;
}

void SummarizationService::requestLlmContext(int tokens)
{
    if (tokens <= 0 || m_deps.llm->cloudSelected(QString())) return;
    m_contextFloor.insert(serverKey(m_deps.llm->localConfig()), tokens);
}

bool SummarizationService::cancel(const QString& meetingId, JobKind kind)
{
    switch (kind) {
    case JobKind::Summarize:
    case JobKind::ExtractTopics: {
        const auto it = m_calls.find(callKey(meetingId, kind));
        if (it == m_calls.end()) return false;
        const ActiveCall call = *it;
        m_calls.erase(it);
        abortCall(call);
        // Cloud: there is no charge for the cancelled call; the run's charges so far (e.g. topic gathering +
        // analyses) go summed to the cost feedback.
        if (call.cloudRun && !m_deps.llm->runError(call.cloudRun, nullptr))
            m_deps.llm->finishRun(call.cloudRun);
        m_deps.jobs->cancelled(meetingId, kind);
        return true;
    }
    case JobKind::AnalyzeTopics:
        return m_topics->cancelBatch(meetingId);
    default:
        return false;
    }
}

} // namespace tanara::processing

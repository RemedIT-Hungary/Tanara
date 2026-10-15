#include "tanara/app/ProviderRuns.h"

#include "tanara/app/AppController.h"   // keys::LlmApiKey
#include "tanara/app/SettingsManager.h"
#include "tanara/cloud/CloudEstimator.h"
#include "tanara/cloud/CloudMode.h"
#include "tanara/cloud/CloudRunLedger.h"
#include "tanara/cloud/CloudSession.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/foundation/secrets/SecretStore.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/processing/analysis/LlmTagSuggestionProvider.h"
#include "tanara/processing/stt/TranscriptionPort.h"
#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/processing/summary/SummarizationService.h"
#include "tanara/processing/summary/SummaryLlmPort.h"
#include "tanara/processing/summary/SummaryService.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/providers/embedding/ITextEmbedder.h"

#include <QSet>
#include <QUuid>

namespace tanara::app {

using library::SummaryRepository;
using library::TranscriptRepository;

namespace {
constexpr int kCloudMaxTokens = cloud::CloudEstimator::kMaxTokens;
} // namespace

// ---- the summarization's port (processing::SummaryLlmPort) ------------------------------------------------
// The settings, the LLM key and the Tanara Cloud run bookkeeping. A cloud run travels as an opaque handle.
struct ProviderRuns::SummaryAdapter final : processing::SummaryLlmPort {
    struct CloudRunHandle final : RunHandle { CloudRunPtr run; };

    explicit SummaryAdapter(ProviderRuns* owner) : q(owner) {}

    static CloudRunPtr unwrap(const Run& run)
    {
        const auto* h = static_cast<const CloudRunHandle*>(run.get());
        return h ? h->run : CloudRunPtr();
    }
    static Run wrap(const CloudRunPtr& run)
    {
        if (!run) return {};
        auto h = std::make_shared<CloudRunHandle>();
        h->run = run;
        return h;
    }

    providers::ReadinessResult readiness(const QString& meetingId) const override
    {
        return q->canRun(providers::WorkflowStep::Summarize, meetingId);
    }
    processing::ProcessingSettings processingSettings() const override { return q->m_deps.settings->settings().processing; }
    QString chatProviderId() const override { return q->m_deps.settings->settings().providers.llm.selectedId; }
    providers::ProviderConfig localConfig() const override { return q->m_deps.settings->settings().providers.llm.selected(); }
    bool cloudSelected() const override { return q->usesCloud(providers::WorkflowStep::Summarize); }

    Run newRun(const QString& meetingId, const QString& kind) override { return wrap(q->newCloudRun(meetingId, kind)); }
    void setRunKind(const Run& run, const QString& kind) override
    {
        if (const CloudRunPtr r = unwrap(run)) r->kind = kind;
    }
    providers::ProviderConfig config(Run& run, const QString& meetingId, const QString& kind) override
    {
        CloudRunPtr r = unwrap(run);
        const providers::ProviderConfig cfg = q->llmConfigFor(r, meetingId, kind);
        if (!run && r) run = wrap(r);
        return cfg;
    }
    providers::ProviderContext context(const Run& run, const QString& summaryMode) const override
    {
        return q->providerContext(unwrap(run), summaryMode);
    }
    void finishRun(const Run& run) override { q->finishCloudRun(unwrap(run)); }
    bool failRun(const Run& run) override { return q->m_deps.ledger->fail(unwrap(run)); }
    bool runError(const Run& run, Error* out) const override
    {
        const CloudRunPtr r = unwrap(run);
        if (!r || !r->lastError.isError()) return false;
        if (out) *out = cloud::toError(r->lastError);
        return true;
    }
    QString runErrorMessage(const Run& run) const override
    {
        const CloudRunPtr r = unwrap(run);
        return r ? r->lastError.message : QString();
    }
    void refreshAccount(const Run& run) override
    {
        const CloudRunPtr r = unwrap(run);
        if (r && (r->balance.isValid() || r->calls > 0)) q->m_deps.cloud->refreshAccount();
    }

    ProviderRuns* q;
};

// ---- the transcription's port (processing::TranscriptionPort) ---------------------------------------------
// The selected STT provider and its key slot (from the descriptor), or a Tanara Cloud run with the gateway
// config.
struct ProviderRuns::SttAdapter final : processing::TranscriptionPort {
    struct CloudRunHandle final : RunHandle { CloudRunPtr run; };

    explicit SttAdapter(ProviderRuns* owner) : q(owner) {}

    static CloudRunPtr unwrap(const Run& run)
    {
        const auto* h = static_cast<const CloudRunHandle*>(run.get());
        return h ? h->run : CloudRunPtr();
    }

    Setup setup(const QString& meetingId) override
    {
        const AppSettings s = q->m_deps.settings->settings();
        Setup out;
        out.providerId = s.providers.stt.selectedId;
        out.config = s.providers.stt.selected();
        out.languageHints = s.processing.languageHints;
        if (q->usesCloud(providers::WorkflowStep::Transcribe)) {
            // The same provider with the gateway config; the run (job id, charge, write-back, error) is filled
            // from the response hook.
            const CloudRunPtr run = q->newCloudRun(meetingId, QStringLiteral("transcribe"));
            if (run) {
                auto h = std::make_shared<CloudRunHandle>();
                h->run = run;
                out.run = h;
            }
            out.config = q->cloudConfig(providers::WorkflowStep::Transcribe);
            const std::optional<cloud::CloudModel> cm = findModel(q->m_deps.cloud->models(), out.config.model);
            out.diarization = !run || !cm.has_value() || cm->diarization;
        } else {
            // The API key's slot is provider-dependent: the descriptor's secretKey tells it (soniox.apiKey /
            // stt.whisper.apiKey / ...); we do not hardcode it to Soniox.
            const providers::ProviderDescriptor desc =
                q->m_deps.registry->descriptor(providers::CapabilityKind::Transcription, out.providerId);
            for (const providers::ConfigField& f : desc.fields)
                if (f.isSecret && !f.secretKey.isEmpty()) {
                    out.config.apiKey = q->m_deps.secrets->get(f.secretKey);
                    break;
                }
        }
        return out;
    }
    providers::ProviderContext context(const Run& run) const override { return q->providerContext(unwrap(run), QString()); }
    void finishRun(const Run& run) override { q->finishCloudRun(unwrap(run)); }
    bool failRun(const Run& run) override { return q->m_deps.ledger->fail(unwrap(run)); }
    bool runError(const Run& run, Error* out) const override
    {
        const CloudRunPtr r = unwrap(run);
        if (!r || !r->lastError.isError()) return false;
        if (out) *out = cloud::toError(r->lastError);
        return true;
    }
    void cancelledRun(const Run& run) override
    {
        const CloudRunPtr r = unwrap(run);
        if (!r) return;
        if (!r->transcriptionId.isEmpty()) q->m_deps.cloud->removePendingTranscription(r->transcriptionId);
        if (r->calls > 0 || r->balance.isValid()) q->m_deps.cloud->refreshAccount();
    }

    ProviderRuns* q;
};

ProviderRuns::ProviderRuns(Deps deps)
    : m_deps(std::move(deps))
    , m_summary(std::make_unique<SummaryAdapter>(this))
    , m_stt(std::make_unique<SttAdapter>(this))
{
}

ProviderRuns::~ProviderRuns() = default;

processing::SummaryLlmPort& ProviderRuns::summaryPort() const { return *m_summary; }
processing::TranscriptionPort& ProviderRuns::transcriptionPort() const { return *m_stt; }

// ---- readiness and the cloud selection ----------------------------------------------------------------------

providers::ReadinessResult ProviderRuns::canRun(providers::WorkflowStep step, const QString& meetingId) const
{
    const Meeting meeting = m_deps.store->load(meetingId);
    SecretStore* secrets = m_deps.secrets;
    providers::ReadinessModel model(m_deps.settings->settings().providers,
                                    [secrets](const QString& k) { return !secrets->get(k).isEmpty(); },
                                    m_deps.registry);
    providers::ReadinessResult r = model.check(step, meeting);
    if (!r.runnable || !usesCloud(step))
        return r;

    // Tanara Cloud-specific obstacles (exactly one CTA in the task strip).
    const cloud::CloudSession::Blocker blocker = m_deps.cloud->blocker();
    if (blocker == cloud::CloudSession::Blocker::ClientTooOld) {
        providers::ReadinessResult c;
        c.blockerKind   = providers::BlockerKind::Cloud;
        c.detail        = tr("A Tanara Cloudhoz frissítés kell (legalább %1).").arg(m_deps.cloud->minClient());
        c.fixActionHint = QStringLiteral("cloud:update");
        c.providerId    = cloud::ProviderId;
        return c;
    }
    if (blocker == cloud::CloudSession::Blocker::BalanceEmpty) {
        providers::ReadinessResult c;
        c.blockerKind   = providers::BlockerKind::Cloud;
        c.detail        = tr("töltsd fel az egyenleged");
        c.fixActionHint = QStringLiteral("cloud:topup");
        c.providerId    = cloud::ProviderId;
        return c;
    }
    return r;
}

bool ProviderRuns::usesCloud(providers::WorkflowStep step) const
{
    return m_deps.mode->selects(step, m_deps.settings->settings().providers);
}

QString ProviderRuns::cloudModelFor(providers::WorkflowStep step) const
{
    return m_deps.cloud->modelFor(step == providers::WorkflowStep::Transcribe ? QStringLiteral("stt") : QStringLiteral("llm"),
                                  m_deps.settings->settings().cloud);
}

cloud::EstimateRequest ProviderRuns::estimateRequest(const QString& meetingId, const QString& task,
                                                     const QString& summaryMode) const
{
    const Meeting m = m_deps.store->load(meetingId);
    const AppSettings s = m_deps.settings->settings();
    cloud::EstimateInputs in;
    in.task = task;
    in.summaryMode = summaryMode;
    in.durationMs = m.durationMs;
    for (const Track& t : m.tracks) if (t.active) ++in.tracks;
    in.language = s.processing.languageHints.value(0);
    if (task != QLatin1String("summarize"))
        in.sttModel = cloudModelFor(providers::WorkflowStep::Transcribe);
    if (task != QLatin1String("transcribe")) {
        in.llmModel = cloudModelFor(providers::WorkflowStep::Summarize);
        // The actual input: system prompt + context + transcript (markdown).
        MergedTranscript merged = m_deps.cachedTranscript ? m_deps.cachedTranscript(meetingId) : MergedTranscript();
        if (merged.tokens.isEmpty() && !m.folder.isEmpty())
            merged = TranscriptRepository::readTokens(m.folder);
        if (!merged.tokens.isEmpty()) {
            editing::speakeredit::applyResolvedSpeakers(merged, m, m_deps.personNames ? m_deps.personNames() : PersonNames());
            in.hasTranscript = true;
            in.transcriptChars = int(merged.renderMarkdown().size() + m.contextNote.size());
            in.contextNoteChars = int(m.contextNote.size());
            if (summaryMode == QLatin1String("complex")) {
                in.topicPromptChars = int(processing::SummarizationService::resolvedPrompt(
                    s.processing, s.processing.topicExtractionPrompt, "topic").size());
                in.analysisPromptChars = int(processing::SummarizationService::resolvedPrompt(
                    s.processing, s.processing.topicAnalysisPrompt, "analysis").size());
                const QVector<SummaryTopic> topics = SummaryRepository::readTopics(m.folder);
                in.topicsKnown = !topics.isEmpty();
                if (in.topicsKnown) {
                    QSet<QString> done;
                    for (const TopicAnalysis& a : SummaryRepository::readAnalyses(m.folder))
                        done.insert(a.topicId);
                    for (const SummaryTopic& t : topics) if (!done.contains(t.id)) ++in.missingAnalyses;
                }
            } else {
                const processing::SummaryRequest req =
                    processing::SummarizationService::requestFor(s.processing, merged, m, in.llmModel, kCloudMaxTokens);
                const processing::SummaryPlan plan = processing::SummaryService::plan(req);
                in.singleCall = plan.singleCall;
                in.parts = plan.parts;
                in.cachedParts = plan.cachedParts;
                in.partChars = plan.partChars;
                in.singlePromptChars = int(req.singlePrompt.size());
                in.notesPromptChars = int(req.notesPrompt.size());
                in.mergePromptChars = int(req.mergePrompt.size());
                in.notesMaxTokens = req.notesMaxTokens;
            }
        }
    }
    return cloud::CloudEstimator::plan(in);
}

// ---- the tag suggester's LLM run and the text embedders ---------------------------------------------------

library::TagSuggestionService::LlmSession ProviderRuns::tagLlmSession(const QString& meetingId, QObject* parent)
{
    const AppSettings s = m_deps.settings->settings();
    CloudRunPtr run;
    providers::ProviderConfig cfg = llmConfigFor(run, meetingId, QStringLiteral("tags"));
    cfg.type = s.providers.llm.selectedId;
    library::TagSuggestionService::LlmSession session;
    session.suggester = m_deps.registry->create<providers::ITagSuggester>(
        processing::LlmTagSuggestionProvider::Id, providerContext(run, QStringLiteral("quick")), cfg, parent);
    if (!session.suggester) return session;
    session.model = cfg.model;
    session.systemPrompt = processing::promptDefault(QStringLiteral("tags"), m_deps.metaDir);
    session.done = [this, run](bool ok) {
        if (ok) finishCloudRun(run);
    };
    return session;
}

providers::ITextEmbedder* ProviderRuns::makeEmbedder(const QString& id, QObject* parent)
{
    const AppSettings cur = m_deps.settings->settings();
    providers::ProviderConfig cfg = cur.providers.embedding.configs.value(id);
    cfg.type = id;
    providers::ProviderContext pctx;
    pctx.http = m_deps.http;
    if (id == providers::embeddingproviders::CloudId) {
        // The same sign-in and header set as on the LLM path (/v1/embeddings), outside any run.
        cfg.baseUrl = m_deps.cloud->apiBase();
        pctx = m_deps.ledger->contextFor(nullptr, QString(), m_deps.http);
    } else {
        cfg.apiKey = m_deps.secrets->get(providers::embeddingproviders::ApiKeySecret);
    }
    return m_deps.registry->create<providers::ITextEmbedder>(id, pctx, cfg, parent);
}

// ---- cloud runs ---------------------------------------------------------------------------------------------

ProviderRuns::CloudRunPtr ProviderRuns::newCloudRun(const QString& meetingId, const QString& kind) const
{
    return m_deps.ledger->newRun(meetingId, kind);
}

providers::ProviderConfig ProviderRuns::cloudConfig(providers::WorkflowStep step) const
{
    return m_deps.ledger->configFor(cloudModelFor(step), kCloudMaxTokens);
}

providers::ProviderContext ProviderRuns::providerContext(const CloudRunPtr& run, const QString& summaryMode) const
{
    if (run) return m_deps.ledger->contextFor(run, summaryMode, m_deps.http);
    providers::ProviderContext ctx;   // own key / address: the config's apiKey, no hooks
    ctx.http = m_deps.http;
    // A fresh correlation id per call, so own-key requests of one run group together (e.g. Soniox's
    // client_reference_id in the user's usage log). The meeting id is added by the caller that knows it.
    ctx.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return ctx;
}

providers::ProviderConfig ProviderRuns::llmConfigFor(CloudRunPtr& run, const QString& meetingId,
                                                     const QString& kind) const
{
    if (!usesCloud(providers::WorkflowStep::Summarize)) {
        providers::ProviderConfig cfg = m_deps.settings->settings().providers.llm.selected();
        cfg.apiKey = m_deps.secrets->get(keys::LlmApiKey);   // LM Studio: may be empty
        return cfg;
    }
    if (!run) run = newCloudRun(meetingId, kind);
    return cloudConfig(providers::WorkflowStep::Summarize);
}

void ProviderRuns::finishCloudRun(const CloudRunPtr& run)
{
    m_deps.ledger->finish(run);   // the ledger's charged signal (the facade forwards it as cloudCharged)
}

} // namespace tanara::app

#include "tanara/processing/summary/LlmSummarizer.h"
#include "tanara/processing/summary/ComplexSummaryService.h"
#include "tanara/processing/summary/SummaryService.h"

#include <QTimer>

namespace tanara::processing {

namespace {

Error serviceError(const QString& message)
{
    Error e;
    e.kind = Error::Kind::Service;
    e.code = QStringLiteral("summary_failed");
    e.message = message;
    return e;
}

class LlmSummaryJob final : public providers::SummarizationJob {
public:
    LlmSummaryJob(providers::IChatProvider* chat, providers::SummarizationRequest req, QObject* parent)
        : providers::SummarizationJob(parent), m_chat(chat), m_req(std::move(req))
    {
        m_result.mode = m_req.mode;
        m_result.model = m_req.model;
        QTimer::singleShot(0, this, [this]() { start(); });
    }

private:
    void onCancel() override
    {
        if (m_quick) m_quick->cancel();
        delete m_complex;   // drops its running call (the provider's job is its child chain)
        m_complex = nullptr;
    }

    void start()
    {
        if (isCancelled()) return;
        if (m_req.mode == providers::SummarizationMode::Quick) startQuick();
        else startComplex();
    }

    void startQuick()
    {
        m_quick = new SummaryService(m_chat, this);
        SummaryRequest r;
        r.transcript = m_req.transcript;
        r.contextNotes = m_req.contextNote;
        r.glossary = m_req.glossary;
        r.notesPrompt = m_req.prompts.notes;
        r.mergePrompt = m_req.prompts.merge;
        r.singlePrompt = m_req.prompts.single;
        r.language = m_req.language;
        r.model = m_req.model;
        r.temperature = m_req.temperature;
        r.maxTokens = m_req.maxTokens;
        r.sourceLines = m_req.lines;
        connect(m_quick, &SummaryService::progress, this, [this](const QString&, int done, int total) {
            emit progress(done, total);
        });
        connect(m_quick, &SummaryService::summaryReady, this, [this](const Summary& s) {
            m_result.summary = s;
            m_result.markdown = s.renderMarkdown();
            complete(m_result);
        });
        connect(m_quick, &SummaryService::summaryFailed, this, [this](const QString& e) { fail(serviceError(e)); });
        m_quick->summarize(r);
    }

    void startComplex()
    {
        m_complex = new ComplexSummaryService(m_chat, this);
        m_complex->setLanguage(m_req.language);
        m_complex->setReducePrompt(m_req.prompts.reduce);
        m_md = m_req.transcript.renderMarkdown();
        connect(m_complex, &ComplexSummaryService::failed, this, [this](const QString& e) { fail(serviceError(e)); });
        connect(m_complex, &ComplexSummaryService::topicsReady, this, [this](const QVector<SummaryTopic>& topics) {
            m_result.topics = topics;
            if (topics.isEmpty()) { complete(m_result); return; }
            nextAnalysis();
        });
        connect(m_complex, &ComplexSummaryService::topicAnalysisReady, this, [this](const TopicAnalysis& a) {
            m_result.analyses.append(a);
            emit progress(m_result.analyses.size(), m_result.topics.size());
            nextAnalysis();
        });
        connect(m_complex, &ComplexSummaryService::reduceReady, this,
                [this](const QString& exec, const QVector<ActionItem>& items) {
            m_result.summary.execSummary = exec;
            m_result.summary.actionItems = items;
            for (const TopicAnalysis& a : m_result.analyses) {
                m_result.summary.decisions += a.decisions;
                m_result.summary.openQuestions += a.openQuestions;
            }
            complete(m_result);
        });
        m_complex->requestTopics(m_md, m_req.contextNote, m_req.prompts.topics, m_req.model, m_req.temperature);
    }

    void nextAnalysis()
    {
        const int i = int(m_result.analyses.size());
        if (i < m_result.topics.size()) {
            m_complex->requestTopicAnalysis(m_md, m_result.topics.at(i), m_req.contextNote, m_req.prompts.analysis,
                                            m_req.model, m_req.temperature);
            return;
        }
        m_complex->requestReduce(m_result.analyses, m_req.contextNote, m_req.model, m_req.temperature);
    }

    providers::IChatProvider* m_chat;
    providers::SummarizationRequest m_req;
    providers::SummarizationResult m_result;
    QPointer<SummaryService> m_quick;
    ComplexSummaryService* m_complex = nullptr;
    QString m_md;
};

} // namespace

LlmSummarizer::LlmSummarizer(providers::IChatProvider* chat, const providers::ProviderConfig& cfg, QObject* parent)
    : QObject(parent), m_chat(chat), m_cfg(cfg)
{
}

providers::SummarizationJob* LlmSummarizer::summarize(const providers::SummarizationRequest& req)
{
    if (!m_chat) {
        auto* job = new providers::SummarizationJob(this);
        Error e;
        e.kind = Error::Kind::Unsupported;
        e.code = QStringLiteral("no_chat_provider");
        job->failLater(e);
        return job;
    }
    providers::SummarizationRequest r = req;
    if (r.model.isEmpty()) r.model = m_cfg.model;
    return new LlmSummaryJob(m_chat, std::move(r), this);
}

} // namespace tanara::processing

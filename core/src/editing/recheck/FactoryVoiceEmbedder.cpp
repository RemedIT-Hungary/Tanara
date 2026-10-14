#include "tanara/editing/recheck/FactoryVoiceEmbedder.h"

#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>

#include <atomic>

namespace tanara::editing {

// The embedder state shared by the jobs (one job runs at a time in practice; the mutex keeps it safe anyway).
struct FactoryVoiceEmbedder::Shared {
    QMutex mutex;
    providers::UtteranceEmbedderFactory factory;
    std::unique_ptr<providers::IUtteranceEmbedder> embedder;
    QString openPath;
    bool    opened = false;
    QString openError;
    std::atomic<bool> stop{false};      // stopJobs(): the running jobs end early, with their partial result
};

namespace {

struct WorkState {
    std::atomic<bool> cancelled{false};
    QVector<providers::VoiceVector> out;
    Error error;
};

class FactoryVoiceJob final : public providers::VoiceEmbeddingJob {
public:
    FactoryVoiceJob(std::shared_ptr<FactoryVoiceEmbedder::Shared> shared, providers::VoiceEmbeddingRequest req,
                    QStringList models, QObject* parent)
        : providers::VoiceEmbeddingJob(parent), m_state(std::make_shared<WorkState>())
    {
        auto state = m_state;
        m_thread = QThread::create([state, shared = std::move(shared), req = std::move(req),
                                    models = std::move(models)]() {
            QMutexLocker lock(&shared->mutex);
            for (int i = 0; i < req.clips.size(); ++i) {
                const providers::AudioClipRef& clip = req.clips.at(i);
                // The audio is opened before the stop check: one decode per run, as the embedder promises.
                if (!shared->embedder || shared->openPath != clip.path) {
                    shared->embedder = shared->factory ? shared->factory() : nullptr;
                    shared->openPath = clip.path;
                    shared->opened = shared->embedder && shared->embedder->open(clip.path);
                    shared->openError = shared->embedder ? shared->embedder->lastError() : QString();
                }
                if (!shared->opened) {
                    state->error.kind = Error::Kind::Service;
                    state->error.code = QStringLiteral("open_failed");
                    state->error.message = shared->openError;
                    return;
                }
                if (state->cancelled || shared->stop) break;
                const EmbeddingSet set = shared->embedder->embedAll(clip.startMs, clip.endMs, models);
                for (auto it = set.cbegin(); it != set.cend(); ++it) {
                    providers::VoiceVector v;
                    v.vector = it.value();
                    v.dim = int(v.vector.size());
                    v.modelId = it.key();
                    v.input = i;
                    state->out.append(v);
                }
            }
        });
        QPointer<FactoryVoiceJob> self(this);
        connect(m_thread, &QThread::finished, this, [self]() {
            if (!self) return;
            if (self->m_state->error.isError()) self->fail(self->m_state->error);
            else self->complete(self->m_state->out);
        });
        m_thread->start();
    }

    ~FactoryVoiceJob() override
    {
        m_state->cancelled = true;
        if (m_thread) {
            m_thread->disconnect(this);
            m_thread->wait();
            delete m_thread;
        }
    }

private:
    void onCancel() override { m_state->cancelled = true; }

    std::shared_ptr<WorkState> m_state;
    QThread* m_thread = nullptr;
};

} // namespace

FactoryVoiceEmbedder::FactoryVoiceEmbedder(providers::UtteranceEmbedderFactory factory, QStringList defaultModels,
                                           QObject* parent)
    : QObject(parent), m_shared(std::make_shared<Shared>()), m_defaultModels(std::move(defaultModels))
{
    m_shared->factory = std::move(factory);
}

FactoryVoiceEmbedder::~FactoryVoiceEmbedder()
{
    // The jobs are children: they are destroyed (and their workers reaped) by ~QObject, after this body.
}

void FactoryVoiceEmbedder::stopJobs() { m_shared->stop = true; }

QString FactoryVoiceEmbedder::name() const { return QStringLiteral("utterance-factory"); }

QStringList FactoryVoiceEmbedder::defaultModelIds() const
{
    return m_defaultModels.isEmpty() ? QStringList{voicemodels::defaultModelId()} : m_defaultModels;
}

providers::VoiceEmbeddingJob* FactoryVoiceEmbedder::embed(const providers::VoiceEmbeddingRequest& req)
{
    if (!req.pcm.isEmpty()) {
        auto* job = new providers::VoiceEmbeddingJob(this);
        job->failLater(providers::inputError(QStringLiteral("clips_only"),
                                             QStringLiteral("only audio clips can be embedded here")));
        return job;
    }
    if (req.clips.isEmpty()) {
        auto* job = new providers::VoiceEmbeddingJob(this);
        job->completeLater({});
        return job;
    }
    const QStringList models = req.modelIds.isEmpty() ? defaultModelIds() : req.modelIds;
    return new FactoryVoiceJob(m_shared, req, models, this);
}

} // namespace tanara::editing

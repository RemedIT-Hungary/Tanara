#include "tanara/processing/voiceid/OnnxVoiceEmbedder.h"
#include "tanara/processing/voiceid/VoiceEmbedder.h"

#include <QPointer>
#include <QThread>

#include <atomic>
#include <memory>

namespace tanara::processing {

namespace {

// The worker's shared state: the cancel flag and the result, read on the job's thread after the
// worker thread finished.
struct WorkState {
    std::atomic<bool> cancelled{false};
    QVector<providers::VoiceVector> out;
    Error error;
};

class OnnxVoiceJob final : public providers::VoiceEmbeddingJob {
public:
    OnnxVoiceJob(VoiceEmbedderSet set, media::MediaToolPtr media, providers::VoiceEmbeddingRequest req,
                 QStringList models, QObject* parent)
        : providers::VoiceEmbeddingJob(parent), m_state(std::make_shared<WorkState>())
    {
        auto state = m_state;
        m_thread = QThread::create([state, set = std::move(set), media = std::move(media),
                                    req = std::move(req), models = std::move(models)]() mutable {
            if (!set.ensureLoaded()) {
                state->error.kind = Error::Kind::Unsupported;
                state->error.code = QStringLiteral("no_voice_model");
                state->error.message = set.lastError();
                return;
            }
            const int n = req.inputCount();
            for (int i = 0; i < n && !state->cancelled; ++i) {
                QVector<float> pcm;
                if (!req.clips.isEmpty()) {
                    const providers::AudioClipRef& c = req.clips.at(i);
                    if (media) pcm = VoiceEmbedder::decodePcm16kMono(*media, c.path, c.startMs, c.endMs);
                } else {
                    pcm = req.pcm.at(i);
                }
                const EmbeddingSet set1 = pcm.isEmpty() ? EmbeddingSet() : set.embedPcmWith(pcm, models);
                for (const QString& id : models) {
                    providers::VoiceVector v;
                    v.vector = set1.value(id);
                    v.dim = int(v.vector.size());
                    v.modelId = id;
                    v.input = i;
                    state->out.append(v);
                }
            }
        });
        QPointer<OnnxVoiceJob> self(this);
        connect(m_thread, &QThread::finished, this, [self]() {
            if (!self) return;
            if (self->m_state->error.isError()) self->fail(self->m_state->error);
            else self->complete(self->m_state->out);
        });
        m_thread->start();
    }

    ~OnnxVoiceJob() override
    {
        // The worker reads nothing of the job; stop it after the slice in progress and reap it.
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

OnnxVoiceEmbedder::OnnxVoiceEmbedder(VoiceEmbedderSet set, media::MediaToolPtr media, QObject* parent)
    : QObject(parent), m_set(std::move(set)), m_media(std::move(media))
{
}

QStringList OnnxVoiceEmbedder::defaultModelIds() const { return m_set.modelIds(); }

providers::VoiceEmbeddingJob* OnnxVoiceEmbedder::embed(const providers::VoiceEmbeddingRequest& req)
{
    if (req.isEmpty()) {
        auto* job = new providers::VoiceEmbeddingJob(this);
        job->completeLater({});
        return job;
    }
    if (m_set.isEmpty()) {
        auto* job = new providers::VoiceEmbeddingJob(this);
        Error e;
        e.kind = Error::Kind::Unsupported;
        e.code = QStringLiteral("no_voice_model");
        job->failLater(e);
        return job;
    }
    if (!req.clips.isEmpty() && !m_media) {
        auto* job = new providers::VoiceEmbeddingJob(this);
        Error e;
        e.kind = Error::Kind::Unsupported;
        e.code = QStringLiteral("no_media_tool");
        job->failLater(e);
        return job;
    }
    const QStringList models = req.modelIds.isEmpty() ? m_set.modelIds() : req.modelIds;
    return new OnnxVoiceJob(m_set.freshCopy(), m_media, req, models, this);
}

} // namespace tanara::processing

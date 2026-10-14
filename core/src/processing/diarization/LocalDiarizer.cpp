#include "tanara/processing/diarization/LocalDiarizer.h"

#include "tanara/foundation/log/Logging.h"

#include <QHash>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace tanara::processing {

using namespace providers;

namespace {

struct WorkState {
    std::atomic<bool> cancelled{false};
    media::CancelToken decodeCancel;
    DiarizeResult out;
    Error error;
};

Error makeError(Error::Kind kind, const QString& code, const QString& message)
{
    Error e;
    e.kind = kind;
    e.code = code;
    e.message = message;
    return e;
}

QVector<float> decodeTrack(const media::MediaTool& media, const QString& path, const media::CancelToken& cancel,
                           QString* error)
{
    media::DecodeSpec spec;
    spec.path = path;
    spec.format = media::PcmFormat{16000, 1, media::SampleFormat::F32};
    QVector<float> pcm;
    const media::MediaResult r = media.decodePcm(spec, [&pcm](const char* data, qsizetype bytes) {
        const qsizetype n = bytes / qsizetype(sizeof(float));
        const qsizetype at = pcm.size();
        pcm.resize(at + n);
        std::memcpy(pcm.data() + at, data, size_t(n) * sizeof(float));
    }, cancel);
    // Lenient like the voice code: any output counts (a non-zero exit after a full decode is not fatal).
    if (!r.started || r.cancelled || (pcm.isEmpty() && !r.ok())) {
        if (error) *error = r.started ? r.errorText() : QStringLiteral("the media tool could not be started");
        return {};
    }
    return pcm;
}

class LocalDiarizeJob final : public DiarizeJob {
public:
    LocalDiarizeJob(LocalDiarizer::SegmenterLoader loader, media::MediaToolPtr media, DiarizeRequest req,
                    SegmenterOptions opts, QObject* parent)
        : DiarizeJob(parent), m_state(std::make_shared<WorkState>())
    {
        auto state = m_state;
        LocalDiarizeJob* job = this;   // alive until the worker is reaped (see the destructor)
        m_thread = QThread::create([state, job, loader = std::move(loader), media = std::move(media),
                                    req = std::move(req), opts]() {
            QString err;
            std::unique_ptr<PyannoteSegmenter> seg = loader ? loader(req.segmentationModelId, opts, &err) : nullptr;
            if (!seg) {
                state->error = makeError(Error::Kind::Unsupported, QStringLiteral("no_segmentation_model"), err);
                return;
            }
            // Decode every track first, so the progress can count the windows of all of them.
            QVector<int> idx;
            QVector<QVector<float>> pcms;
            for (int i = 0; i < req.tracks.size() && !state->cancelled; ++i) {
                const DiarizeTrack& t = req.tracks[i];
                if (!t.hasInput()) continue;
                QVector<float> pcm = t.pcm;
                if (!t.path.isEmpty()) {
                    if (!media) {
                        state->error = makeError(Error::Kind::Unsupported, QStringLiteral("no_media_tool"), {});
                        return;
                    }
                    pcm = decodeTrack(*media, t.path, state->decodeCancel, &err);
                    if (pcm.isEmpty() && !state->cancelled) {
                        state->error = makeError(Error::Kind::Input, QStringLiteral("decode_failed"),
                                                 QStringLiteral("%1: %2").arg(t.path, err));
                        return;
                    }
                }
                idx.append(i);
                pcms.append(std::move(pcm));
            }
            int total = 0;
            for (const QVector<float>& p : pcms) total += seg->windowCount(p.size());
            int doneBefore = 0;
            for (int k = 0; k < idx.size() && !state->cancelled; ++k) {
                const DiarizeTrack& t = req.tracks[idx[k]];
                const auto r = seg->segment(pcms[k], [job, doneBefore, total](int done, int) {
                    const qint64 d = doneBefore + done, n = total;
                    QMetaObject::invokeMethod(job, [job, d, n]() { if (!job->isCancelled()) emit job->progress(d, n); },
                                              Qt::QueuedConnection);
                }, &state->cancelled, &err);
                if (!r) {
                    if (state->cancelled) return;
                    state->error = makeError(Error::Kind::Service, QStringLiteral("segmentation_failed"), err);
                    return;
                }
                LocalDiarizer::appendTrackResult(t, *r, opts, &state->out);
                doneBefore += seg->windowCount(pcms[k].size());
                pcms[k] = {};   // free the PCM early
            }
            std::sort(state->out.turns.begin(), state->out.turns.end(), [](const SpeakerTurn& a, const SpeakerTurn& b) {
                if (a.startMs != b.startMs) return a.startMs < b.startMs;
                if (a.trackId != b.trackId) return a.trackId < b.trackId;
                return a.speakerIndex < b.speakerIndex;
            });
            std::sort(state->out.speakers.begin(), state->out.speakers.end(),
                      [](const DiarizedSpeaker& a, const DiarizedSpeaker& b) {
                          return a.trackId != b.trackId ? a.trackId < b.trackId : a.index < b.index;
                      });
        });
        QPointer<LocalDiarizeJob> self(this);
        connect(m_thread, &QThread::finished, this, [self]() {
            if (!self || self->isCancelled()) return;
            if (self->m_state->error.isError()) self->fail(self->m_state->error);
            else self->complete(self->m_state->out);
        });
        m_thread->start();
    }

    ~LocalDiarizeJob() override
    {
        stop();
        if (m_thread) {
            m_thread->disconnect(this);
            m_thread->wait();
            delete m_thread;
        }
    }

private:
    void stop()
    {
        m_state->cancelled = true;
        m_state->decodeCancel.cancel();
    }
    void onCancel() override { stop(); }

    std::shared_ptr<WorkState> m_state;
    QThread* m_thread = nullptr;
};

} // namespace

LocalDiarizer::LocalDiarizer(SegmenterLoader loader, media::MediaToolPtr media, QObject* parent)
    : QObject(parent), m_loader(std::move(loader)), m_media(std::move(media))
{
}

LocalDiarizer::SegmenterLoader LocalDiarizer::catalogLoader(const QString& modelsDir, const QString& appDir,
                                                            OnnxSegmentationOptions ort)
{
    return [modelsDir, appDir, ort](const QString& modelId, const SegmenterOptions& opts,
                                    QString* error) -> std::unique_ptr<PyannoteSegmenter> {
        const QString id = modelId.isEmpty() ? VoiceModelRegistry::defaultSegmentationModelId() : modelId;
        const std::optional<VoiceModelSpec> spec = VoiceModelRegistry::spec(id);
        if (!spec || spec->kind != VoiceModelKind::Segmentation) {
            if (error) *error = QStringLiteral("Unknown segmentation model: %1").arg(id);
            return nullptr;
        }
        return PyannoteSegmenter::open(*spec, modelsDir, appDir, opts, ort, error);
    };
}

bool LocalDiarizer::hasUsableModel(const QString& modelsDir, const QString& appDir)
{
    for (const VoiceModelSpec& s : VoiceModelRegistry::segmentation())
        if (PyannoteSegmenter::isUsable(s, modelsDir, appDir)) return true;
    return false;
}

SegmenterOptions LocalDiarizer::optionsFor(const DiarizeRequest& req, SegmenterOptions base)
{
    if (req.minDurationOn >= 0) base.minDurationOn = req.minDurationOn;
    if (req.minDurationOff >= 0) base.minDurationOff = req.minDurationOff;
    return base;
}

void LocalDiarizer::appendTrackResult(const DiarizeTrack& track, const SegmentationResult& seg,
                                      const SegmenterOptions& opts, DiarizeResult* out)
{
    auto ms = [&](double sec) { return track.startOffsetMs + qint64(std::llround(sec * 1000.0)); };
    if (!track.knownPerson.isNull()) {
        // One speaker: the union of all speech, post-processed again (the merge can close small gaps).
        QVector<segmentation::Span> spans;
        for (const segmentation::Turn& t : seg.turns) spans.append({t.start, t.end});
        std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        QVector<segmentation::Span> merged;
        for (const segmentation::Span& s : spans) {
            if (!merged.isEmpty() && s.start <= merged.last().end) merged.last().end = std::max(merged.last().end, s.end);
            else merged.append(s);
        }
        merged = segmentation::applyMinDuration(merged, opts.minDurationOn, opts.minDurationOff);
        DiarizedSpeaker sp;
        sp.trackId = track.trackId;
        sp.index = 0;
        sp.person = track.knownPerson;
        sp.centroid.trackId = track.trackId;
        for (const segmentation::Span& s : merged) {
            SpeakerTurn t;
            t.trackId = track.trackId;
            t.startMs = ms(s.start);
            t.endMs = ms(s.end);
            t.speakerIndex = 0;
            sp.totalMs += t.durationMs();
            out->turns.append(t);
        }
        if (!merged.isEmpty()) out->speakers.append(sp);
        return;
    }
    // Stitched ids -> dense indices by first appearance (seg.turns is sorted by start).
    QHash<int, int> index;
    QVector<DiarizedSpeaker> speakers;
    for (const segmentation::Turn& st : seg.turns) {
        if (!index.contains(st.speaker)) {
            index.insert(st.speaker, int(speakers.size()));
            DiarizedSpeaker sp;
            sp.trackId = track.trackId;
            sp.index = int(speakers.size());
            sp.centroid.trackId = track.trackId;   // D4 fills the vectors
            speakers.append(sp);
        }
        SpeakerTurn t;
        t.trackId = track.trackId;
        t.startMs = ms(st.start);
        t.endMs = ms(st.end);
        t.speakerIndex = index.value(st.speaker);
        t.overlap = st.overlap;
        speakers[t.speakerIndex].totalMs += t.durationMs();
        out->turns.append(t);
    }
    out->speakers += speakers;
}

DiarizeJob* LocalDiarizer::diarize(const DiarizeRequest& req)
{
    if (req.isEmpty()) {
        auto* job = new DiarizeJob(this);
        job->failLater(inputError(QStringLiteral("no_input"), QStringLiteral("no track has input")));
        return job;
    }
    return new LocalDiarizeJob(m_loader, m_media, req, optionsFor(req, m_base), this);
}

} // namespace tanara::processing

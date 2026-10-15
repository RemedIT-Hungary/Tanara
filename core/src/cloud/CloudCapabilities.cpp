// The Tanara Cloud capability adapters (cloud/CloudCapabilities.h) and their registration.
#include "tanara/cloud/CloudCapabilities.h"
#include "tanara/cloud/CloudSession.h"
#include "tanara/providers/embedding/OpenAiCompatibleEmbeddingProvider.h"

#include <QCoreApplication>
#include <QFile>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

namespace tanara::cloud {

using namespace providers;

namespace {

const QString kVoiceModel = QStringLiteral("tanara/voice-embedding");

Error unsupported(const QString& code, const QString& message)
{
    Error e;
    e.kind = Error::Kind::Unsupported;
    e.code = code;
    e.message = message;
    return e;
}

QJsonArray toJson(const QVector<float>& v)
{
    QJsonArray a;
    for (float x : v) a.append(double(x));
    return a;
}

QVector<float> floatsFrom(const QJsonValue& v)
{
    QVector<float> out;
    const QJsonArray a = v.toArray();
    out.reserve(a.size());
    for (const QJsonValue& x : a) out.append(float(x.toDouble()));
    return out;
}

QStringList stringsFrom(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& x : v.toArray()) out << x.toString();
    return out;
}

QJsonArray toJson(const QStringList& l)
{
    QJsonArray a;
    for (const QString& s : l) a.append(s);
    return a;
}

QVector<ActionItem> actionItemsFrom(const QJsonValue& v)
{
    QVector<ActionItem> out;
    for (const QJsonValue& x : v.toArray()) {
        const QJsonObject o = x.toObject();
        out.append(ActionItem{ o.value(QStringLiteral("text")).toString(),
                               o.value(QStringLiteral("owner")).toString(),
                               o.value(QStringLiteral("due")).toString() });
    }
    return out;
}

// Wires a job to the context's cancel token: cancelling the token cancels the job.
void followCancel(const ProviderContext& ctx, CapabilityJob* job)
{
    QPointer<CapabilityJob> guard(job);
    ctx.cancel.onCancel([guard]() { if (guard) guard->cancel(); });
}

} // namespace

QString capabilityProviderId() { return ProviderId; }

QStringList gatewayKindsFor(CapabilityKind capability)
{
    switch (capability) {
    case CapabilityKind::Transcription:             return { QStringLiteral("stt") };
    case CapabilityKind::Chat:
    case CapabilityKind::Summarization:
    case CapabilityKind::TagSuggestion:             return { QStringLiteral("llm") };
    case CapabilityKind::TextEmbedding:             return { QStringLiteral("text_embedding") };
    case CapabilityKind::VoiceEmbedding:
    case CapabilityKind::ParticipantIdentification: return { QStringLiteral("voice_embedding") };
    case CapabilityKind::ModelCatalog:
    case CapabilityKind::Diarization:               // no gateway diarizer yet
    case CapabilityKind::LocalModelServer:          return {};
    }
    return {};
}

// ---- text embedding -------------------------------------------------------------------------

CloudTextEmbedder::CloudTextEmbedder(const ProviderContext& ctx, const ProviderConfig& cfg,
                                     CloudSession* session, QObject* parent)
    : QObject(parent)
{
    const detail::GatewayClient gw(ctx, cfg, session);
    ProviderContext c = ctx;
    if (!c.http) c.http = gw.http();
    QPointer<CloudSession> s(session);
    if (!c.auth.isSet() && cfg.apiKey.isEmpty())
        c.auth = AuthSource::bearer([s]() { return s ? s->apiKey() : QString(); });
    if (c.headers.isEmpty() && session) c.headers = session->requestHeaders(c.jobId);
    if (!c.observe && session)
        c.observe = [s](const HttpExchange& ex) { if (s) s->observeExchange(ex); };
    ProviderConfig f = cfg;
    f.baseUrl = gw.apiBase();
    if (f.model.isEmpty()) f.model = QStringLiteral("default");   // the gateway's default model
    m_inner = new OpenAiCompatibleEmbeddingProvider(c, f, this);
}

QString CloudTextEmbedder::name() const { return embeddingproviders::CloudId; }

EmbeddingJob* CloudTextEmbedder::embed(const EmbeddingRequest& req) { return m_inner->embed(req); }

// ---- voice embedding ------------------------------------------------------------------------

QByteArray CloudVoiceEmbedder::encodeOpus(const media::MediaTool& media, const QVector<float>& mono16k)
{
    if (mono16k.isEmpty()) return {};
    QTemporaryDir dir;
    if (!dir.isValid()) return {};
    const QString out = dir.filePath(QStringLiteral("clip.ogg"));
    media::EncodeSpec spec;
    spec.input = media::PcmFormat{ 16000, 1, media::SampleFormat::S16 };
    spec.outputPath = out;
    spec.bitrateKbps = kOpusKbps;
    std::unique_ptr<media::EncoderHandle> enc = media.startEncoder(spec);
    if (!enc) return {};
    QByteArray pcm;
    pcm.resize(mono16k.size() * qsizetype(sizeof(qint16)));
    auto* dst = reinterpret_cast<qint16*>(pcm.data());
    for (qsizetype i = 0; i < mono16k.size(); ++i)
        dst[i] = qint16(std::clamp(std::lround(double(mono16k.at(i)) * 32767.0), -32768L, 32767L));
    enc->write(pcm.constData(), pcm.size());
    if (!enc->finish()) return {};
    QFile f(out);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

namespace {

class CloudVoiceJob final : public VoiceEmbeddingJob {
public:
    CloudVoiceJob(const detail::GatewayClient& gw, QVector<QByteArray> clips, QStringList models, QObject* parent)
        : VoiceEmbeddingJob(parent), m_gw(gw), m_clips(std::move(clips)), m_models(std::move(models))
    {
        followCancel(gw.context(), this);
        QTimer::singleShot(0, this, [this]() { next(); });
    }

private:
    void onCancel() override { detail::abortReply(m_reply); m_reply = nullptr; }

    void next()
    {
        if (isCancelled() || isDone()) return;
        const int total = int(m_clips.size() * m_models.size());
        if (m_at >= total) { complete(m_out); return; }
        const int input = m_at / int(m_models.size());
        const QString model = m_models.at(m_at % m_models.size());
        auto* mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart file;
        file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/ogg"));
        file.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"file\"; filename=\"clip.ogg\""));
        file.setBody(m_clips.at(input));
        QHttpPart modelPart;
        modelPart.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"model\""));
        modelPart.setBody(model.toUtf8());
        mp->append(file);
        mp->append(modelPart);
        m_reply = m_gw.postMultipart(QStringLiteral("/voice/embeddings"), mp, this,
                                     [this, input, model](const HttpExchange& ex) { onReply(ex, input, model); });
        if (!m_reply) fail(unsupported(QStringLiteral("no_http_client"), QStringLiteral("no HTTP client")));
    }

    void onReply(const HttpExchange& ex, int input, const QString& model)
    {
        m_reply = nullptr;
        if (!detail::GatewayClient::ok(ex)) { fail(detail::GatewayClient::errorOf(ex)); return; }
        const QJsonObject o = QJsonDocument::fromJson(ex.body).object();
        VoiceVector v;
        v.vector = floatsFrom(o.value(QStringLiteral("embedding")));
        v.dim = o.value(QStringLiteral("dim")).toInt(int(v.vector.size()));
        v.modelId = o.value(QStringLiteral("model")).toString(model);
        v.input = input;
        m_out.append(v);
        ++m_at;
        emit progress(m_at, m_clips.size() * m_models.size());
        next();
    }

    detail::GatewayClient m_gw;
    QVector<QByteArray> m_clips;
    QStringList m_models;
    QVector<VoiceVector> m_out;
    int m_at = 0;
    QPointer<QNetworkReply> m_reply;
};

} // namespace

CloudVoiceEmbedder::CloudVoiceEmbedder(const ProviderContext& ctx, const ProviderConfig& cfg,
                                       CloudSession* session, media::MediaToolPtr media, QObject* parent)
    : QObject(parent), m_gw(ctx, cfg, session), m_media(std::move(media))
{
}

QString CloudVoiceEmbedder::name() const { return ProviderId; }

QStringList CloudVoiceEmbedder::defaultModelIds() const
{
    return { m_gw.config().model.isEmpty() ? kVoiceModel : m_gw.config().model };
}

VoiceEmbeddingJob* CloudVoiceEmbedder::embed(const VoiceEmbeddingRequest& req)
{
    if (req.isEmpty()) {
        auto* job = new VoiceEmbeddingJob(this);
        job->completeLater({});
        return job;
    }
    const QStringList models = req.modelIds.isEmpty() ? defaultModelIds() : req.modelIds;
    if (!m_media) {
        auto* job = new VoiceEmbeddingJob(this);
        job->failLater(unsupported(QStringLiteral("no_media_tool"), QStringLiteral("no media tool")));
        return job;
    }
    // Every input becomes one Opus clip: a clip is decoded (only its span) and re-encoded; a PCM slice
    // is encoded. The raw recording never leaves the device beyond the requested spans.
    QVector<QByteArray> clips;
    const int n = req.inputCount();
    for (int i = 0; i < n; ++i) {
        QVector<float> pcm = req.clips.isEmpty()
            ? req.pcm.at(i)
            : m_media->decodeMonoF32(req.clips.at(i).path, req.clips.at(i).startMs, req.clips.at(i).endMs, 16000);
        const QByteArray opus = encodeOpus(*m_media, pcm);
        if (opus.isEmpty()) {
            auto* job = new VoiceEmbeddingJob(this);
            job->failLater(inputError(QStringLiteral("clip_unreadable"),
                                      QStringLiteral("clip %1 could not be decoded / encoded").arg(i)));
            return job;
        }
        clips.append(opus);
    }
    return new CloudVoiceJob(m_gw, std::move(clips), models, this);
}

// ---- participant identification ------------------------------------------------------------

CloudParticipantIdentifier::CloudParticipantIdentifier(const ProviderContext& ctx, const ProviderConfig& cfg,
                                                       CloudSession* session, QObject* parent)
    : QObject(parent), m_gw(ctx, cfg, session)
{
}

QString CloudParticipantIdentifier::name() const { return ProviderId; }

QByteArray CloudParticipantIdentifier::requestBody(const IdentifyRequest& req, const QString& model)
{
    QJsonObject body;
    if (!model.isEmpty()) body.insert(QStringLiteral("model"), model);
    if (req.threshold >= 0.0) body.insert(QStringLiteral("threshold"), req.threshold);
    QJsonArray probes;
    for (int i = 0; i < req.probes.size(); ++i) {
        QJsonArray vecs;
        for (const VoiceVector& v : req.probes.at(i).vectors)
            vecs.append(QJsonObject{ { QStringLiteral("model"), v.modelId },
                                     { QStringLiteral("embedding"), toJson(v.vector) } });
        probes.append(QJsonObject{ { QStringLiteral("id"), QStringLiteral("p%1").arg(i) },
                                   { QStringLiteral("vectors"), vecs } });
    }
    QJsonArray cands;
    for (int i = 0; i < req.candidates.size(); ++i) {
        QJsonArray prints;
        for (const Voiceprint& p : req.candidates.at(i).prints)
            prints.append(QJsonObject{ { QStringLiteral("model"), p.model },
                                       { QStringLiteral("embedding"), toJson(p.embedding) } });
        cands.append(QJsonObject{ { QStringLiteral("ref"), QStringLiteral("c%1").arg(i) },
                                  { QStringLiteral("prints"), prints } });
    }
    body.insert(QStringLiteral("probes"), probes);
    body.insert(QStringLiteral("candidates"), cands);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

IdentifyJob* CloudParticipantIdentifier::identify(const IdentifyRequest& req)
{
    auto* job = new IdentifyJob(this);
    followCancel(m_gw.context(), job);
    if (req.probes.isEmpty()) { job->completeLater({}); return job; }
    QPointer<IdentifyJob> guard(job);
    QNetworkReply* reply = m_gw.postJson(QStringLiteral("/voice/identify"),
                                         requestBody(req, m_gw.config().model), job,
                                         [guard, req](const HttpExchange& ex) {
        if (!detail::GatewayClient::ok(ex)) { guard->fail(detail::GatewayClient::errorOf(ex)); return; }
        QVector<ParticipantGuess> out;
        for (const IdentifyProbe& p : req.probes) {
            ParticipantGuess g;
            g.trackId = p.trackId;
            g.deviceName = p.deviceName;
            g.windows = p.windows;
            g.sampleRef = p.sampleRef;
            out.append(g);
        }
        const QJsonArray results = QJsonDocument::fromJson(ex.body).object().value(QStringLiteral("results")).toArray();
        for (const QJsonValue& rv : results) {
            const QJsonObject r = rv.toObject();
            const int pi = r.value(QStringLiteral("probe")).toString().mid(1).toInt();
            if (pi < 0 || pi >= out.size()) continue;
            out[pi].score = r.value(QStringLiteral("score")).toDouble(-1.0);
            const QString ref = r.value(QStringLiteral("match")).toString();
            const int ci = ref.startsWith(QLatin1Char('c')) ? ref.mid(1).toInt() : -1;
            if (!ref.isEmpty() && ci >= 0 && ci < req.candidates.size()) {   // the ref maps back on the device only
                out[pi].personId = req.candidates.at(ci).personId;
                out[pi].displayName = req.candidates.at(ci).name;
            }
        }
        guard->complete(out);
    });
    connect(job, &QObject::destroyed, this, [r = QPointer<QNetworkReply>(reply)]() { detail::abortReply(r); });
    if (!reply) job->failLater(unsupported(QStringLiteral("no_http_client"), QStringLiteral("no HTTP client")));
    return job;
}

// ---- server-side summary pipeline ----------------------------------------------------------

CloudPipelineSummarizer::CloudPipelineSummarizer(const ProviderContext& ctx, const ProviderConfig& cfg,
                                                 CloudSession* session, QObject* parent)
    : QObject(parent), m_gw(ctx, cfg, session)
{
    m_pollMs = cfg.extra.value(QStringLiteral("pollIntervalMs"), kPollIntervalMs).toInt();
    if (m_pollMs <= 0) m_pollMs = kPollIntervalMs;
}

QString CloudPipelineSummarizer::name() const { return ProviderId; }

QByteArray CloudPipelineSummarizer::requestBody(const SummarizationRequest& req, const QString& model)
{
    QJsonObject body;
    body.insert(QStringLiteral("mode"), summarizationModeName(req.mode));
    if (!model.isEmpty()) body.insert(QStringLiteral("model"), model);
    if (!req.language.isEmpty()) body.insert(QStringLiteral("language"), req.language);
    if (!req.contextNote.isEmpty()) body.insert(QStringLiteral("context_note"), req.contextNote);
    if (!req.glossary.isEmpty()) body.insert(QStringLiteral("glossary"), toJson(req.glossary));
    QJsonObject prompts;
    const auto put = [&prompts](const char* k, const QString& v) {
        if (!v.isEmpty()) prompts.insert(QLatin1String(k), v);
    };
    put("single", req.prompts.single);
    put("notes", req.prompts.notes);
    put("merge", req.prompts.merge);
    put("topics", req.prompts.topics);
    put("analysis", req.prompts.analysis);
    put("reduce", req.prompts.reduce);
    if (!prompts.isEmpty()) body.insert(QStringLiteral("prompts"), prompts);
    QJsonArray lines;
    for (const Utterance& u : req.transcript.segments())
        lines.append(QJsonObject{ { QStringLiteral("id"), QStringLiteral("u%1").arg(u.startMs) },
                                  { QStringLiteral("start_ms"), double(u.startMs) },
                                  { QStringLiteral("end_ms"), double(u.endMs) },
                                  { QStringLiteral("speaker"), u.speaker },
                                  { QStringLiteral("text"), u.text } });
    body.insert(QStringLiteral("lines"), lines);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

SummarizationResult CloudPipelineSummarizer::resultFromJson(const QJsonObject& o, SummarizationMode mode)
{
    SummarizationResult r;
    r.mode = mode;
    const QJsonObject s = o.value(QStringLiteral("summary")).toObject();
    r.summary.execSummary   = s.value(QStringLiteral("exec_summary")).toString();
    r.summary.decisions     = stringsFrom(s.value(QStringLiteral("decisions")));
    r.summary.openQuestions = stringsFrom(s.value(QStringLiteral("open_questions")));
    r.summary.actionItems   = actionItemsFrom(s.value(QStringLiteral("action_items")));
    r.summary.participants  = stringsFrom(s.value(QStringLiteral("participants")));
    for (const QJsonValue& v : o.value(QStringLiteral("topics")).toArray()) {
        const QJsonObject t = v.toObject();
        r.topics.append(SummaryTopic{ t.value(QStringLiteral("id")).toString(),
                                      t.value(QStringLiteral("title")).toString(),
                                      t.value(QStringLiteral("summary")).toString() });
    }
    for (const QJsonValue& v : o.value(QStringLiteral("analyses")).toArray()) {
        const QJsonObject a = v.toObject();
        TopicAnalysis t;
        t.topicId       = a.value(QStringLiteral("topic_id")).toString();
        t.title         = a.value(QStringLiteral("title")).toString();
        t.detail        = a.value(QStringLiteral("detail")).toString();
        t.decisions     = stringsFrom(a.value(QStringLiteral("decisions")));
        t.openQuestions = stringsFrom(a.value(QStringLiteral("open_questions")));
        t.actionItems   = actionItemsFrom(a.value(QStringLiteral("action_items")));
        r.analyses.append(t);
    }
    r.markdown = o.value(QStringLiteral("markdown")).toString();
    r.model = o.value(QStringLiteral("model")).toString();
    return r;
}

namespace {

class CloudSummaryJob final : public SummarizationJob {
public:
    CloudSummaryJob(const detail::GatewayClient& gw, SummarizationMode mode, int pollMs, QObject* parent)
        : SummarizationJob(parent), m_gw(gw), m_mode(mode)
    {
        followCancel(gw.context(), this);
        m_timer.setSingleShot(true);
        m_timer.setInterval(pollMs);
        connect(&m_timer, &QTimer::timeout, this, [this]() { poll(); });
    }

    void start(const QByteArray& body)
    {
        m_reply = m_gw.postJson(QStringLiteral("/summaries"), body, this, [this](const HttpExchange& ex) {
            m_reply = nullptr;
            if (!detail::GatewayClient::ok(ex)) { fail(detail::GatewayClient::errorOf(ex)); return; }
            const QJsonObject o = QJsonDocument::fromJson(ex.body).object();
            m_id = o.value(QStringLiteral("id")).toString();
            if (m_id.isEmpty()) {
                Error e;
                e.kind = Error::Kind::Service;
                e.httpStatus = ex.status;
                e.code = QStringLiteral("bad_response");
                fail(e);
                return;
            }
            if (!handleStatus(o)) m_timer.start();
        });
        if (!m_reply) failLater(unsupported(QStringLiteral("no_http_client"), QStringLiteral("no HTTP client")));
    }

private:
    void onCancel() override { m_timer.stop(); detail::abortReply(m_reply); m_reply = nullptr; }

    void poll()
    {
        if (isCancelled() || isDone()) return;
        m_reply = m_gw.getJson(QStringLiteral("/summaries/") + m_id, this, [this](const HttpExchange& ex) {
            m_reply = nullptr;
            if (!detail::GatewayClient::ok(ex)) { fail(detail::GatewayClient::errorOf(ex)); return; }
            if (!handleStatus(QJsonDocument::fromJson(ex.body).object())) m_timer.start();
        });
    }

    // true = the job ended (completed / error).
    bool handleStatus(const QJsonObject& o)
    {
        const QString status = o.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("completed")) {
            complete(CloudPipelineSummarizer::resultFromJson(o.value(QStringLiteral("result")).toObject(), m_mode));
            return true;
        }
        if (status == QLatin1String("error")) {
            const QJsonObject err = o.value(QStringLiteral("error")).toObject();
            Error e;
            e.kind = Error::Kind::Service;
            e.code = err.value(QStringLiteral("code")).toString(QStringLiteral("summary_failed"));
            e.message = err.value(QStringLiteral("message")).toString();
            fail(e);
            return true;
        }
        emit progress(++m_polls, 0);
        return false;
    }

    detail::GatewayClient m_gw;
    SummarizationMode m_mode;
    QTimer m_timer;
    QString m_id;
    int m_polls = 0;
    QPointer<QNetworkReply> m_reply;
};

} // namespace

SummarizationJob* CloudPipelineSummarizer::summarize(const SummarizationRequest& req)
{
    const QString model = req.model.isEmpty() ? m_gw.config().model : req.model;
    auto* job = new CloudSummaryJob(m_gw, req.mode, m_pollMs, this);
    job->start(requestBody(req, model));
    return job;
}

// ---- tag suggestion -------------------------------------------------------------------------

CloudTagSuggester::CloudTagSuggester(const ProviderContext& ctx, const ProviderConfig& cfg,
                                     CloudSession* session, QObject* parent)
    : QObject(parent), m_gw(ctx, cfg, session)
{
}

QString CloudTagSuggester::name() const { return ProviderId; }

TagSuggestJob* CloudTagSuggester::suggest(const TagSuggestRequest& req)
{
    auto* job = new TagSuggestJob(this);
    followCancel(m_gw.context(), job);
    // The candidates: given, or the seeds' existing tags. Sent as refs (t0, t1 ...).
    QVector<TagCandidate> cands = req.candidates;
    if (cands.isEmpty())
        for (const TagSuggestion& s : req.seeds)
            if (!s.isNew && !s.tagId.isEmpty()) cands.append(TagCandidate{ s.tagId, s.name, QString() });
    QJsonObject body;
    const QString model = req.model.isEmpty() ? m_gw.config().model : req.model;
    if (!model.isEmpty()) body.insert(QStringLiteral("model"), model);
    if (!req.title.isEmpty()) body.insert(QStringLiteral("title"), req.title);
    body.insert(QStringLiteral("summary"), req.summaryText);
    if (!req.participants.isEmpty()) body.insert(QStringLiteral("participants"), toJson(req.participants));
    if (!req.systemPrompt.isEmpty()) body.insert(QStringLiteral("prompt"), req.systemPrompt);
    QJsonArray ca;
    for (int i = 0; i < cands.size(); ++i) {
        QJsonObject c{ { QStringLiteral("ref"), QStringLiteral("t%1").arg(i) },
                       { QStringLiteral("name"), cands.at(i).name } };
        if (!cands.at(i).profileLine.isEmpty()) c.insert(QStringLiteral("profile"), cands.at(i).profileLine);
        ca.append(c);
    }
    body.insert(QStringLiteral("candidates"), ca);
    body.insert(QStringLiteral("max_new"), req.maxNew);

    QPointer<TagSuggestJob> guard(job);
    const QStringList current = req.currentTagIds;
    QNetworkReply* reply = m_gw.postJson(QStringLiteral("/tags/suggest"),
                                         QJsonDocument(body).toJson(QJsonDocument::Compact), job,
                                         [guard, cands, current, maxNew = req.maxNew](const HttpExchange& ex) {
        if (!detail::GatewayClient::ok(ex)) { guard->fail(detail::GatewayClient::errorOf(ex)); return; }
        QVector<TagSuggestion> out;
        int fresh = 0;
        for (const QJsonValue& v : QJsonDocument::fromJson(ex.body).object().value(QStringLiteral("suggestions")).toArray()) {
            const QJsonObject o = v.toObject();
            TagSuggestion s;
            s.source = SuggestionSource::Llm;
            s.score = o.value(QStringLiteral("score")).toDouble();
            const QString ref = o.value(QStringLiteral("ref")).toString();
            const int ci = ref.startsWith(QLatin1Char('t')) ? ref.mid(1).toInt() : -1;
            if (!ref.isEmpty() && ci >= 0 && ci < cands.size()) {
                s.tagId = cands.at(ci).tagId;
                s.name = cands.at(ci).name;
                if (current.contains(s.tagId)) continue;
            } else {
                s.name = o.value(QStringLiteral("name")).toString().trimmed();
                s.isNew = true;
                if (s.name.isEmpty() || fresh >= maxNew) continue;
                ++fresh;
            }
            out.append(s);
        }
        guard->complete(out);
    });
    connect(job, &QObject::destroyed, this, [r = QPointer<QNetworkReply>(reply)]() { detail::abortReply(r); });
    if (!reply) job->failLater(unsupported(QStringLiteral("no_http_client"), QStringLiteral("no HTTP client")));
    return job;
}

// ---- model catalog ----------------------------------------------------------------------------

CloudModelCatalog::CloudModelCatalog(const ProviderContext& ctx, const ProviderConfig& cfg,
                                     CloudSession* session, QObject* parent)
    : QObject(parent), m_gw(ctx, cfg, session)
{
}

QString CloudModelCatalog::name() const { return ProviderId; }

namespace {

QVector<ModelInfo> modelInfos(const QByteArray& body, CapabilityKind capability, bool filter)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QStringList kinds = gatewayKindsFor(capability);
    QHash<QString, int> dims;
    for (const QJsonValue& v : root.value(QStringLiteral("data")).toArray()) {
        const QJsonObject o = v.toObject();
        dims.insert(o.value(QStringLiteral("id")).toString(),
                    o.value(QStringLiteral("tanara")).toObject().value(QStringLiteral("dim")).toInt());
    }
    QVector<ModelInfo> out;
    for (const CloudModel& m : modelsFromJson(root)) {
        if (filter && !kinds.contains(m.kind)) continue;
        ModelInfo i;
        i.id = m.id;
        i.displayName = m.displayName;
        i.capability = capability;
        i.origin = Origin::Cloud;
        i.available = true;
        i.tier = m.tier;
        i.recommended = m.isVirtual && m.tier == QLatin1String("accurate");
        i.dim = dims.value(m.id);
        out.append(i);
    }
    return out;
}

} // namespace

ModelListJob* CloudModelCatalog::list(CapabilityKind capability)
{
    auto* job = new ModelListJob(this);
    followCancel(m_gw.context(), job);
    if (gatewayKindsFor(capability).isEmpty()) { job->completeLater({}); return job; }
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("capability"), capabilityName(capability));
    QPointer<ModelListJob> guard(job);
    QNetworkReply* reply = m_gw.getJson(QStringLiteral("/models?") + q.toString(QUrl::FullyEncoded), job,
                                        [guard, capability](const HttpExchange& ex) {
        if (!detail::GatewayClient::ok(ex)) { guard->fail(detail::GatewayClient::errorOf(ex)); return; }
        guard->complete(modelInfos(ex.body, capability, /*filter*/ true));
    });
    connect(job, &QObject::destroyed, this, [r = QPointer<QNetworkReply>(reply)]() { detail::abortReply(r); });
    if (!reply) job->failLater(unsupported(QStringLiteral("no_http_client"), QStringLiteral("no HTTP client")));
    return job;
}

ModelReadyJob* CloudModelCatalog::ensureAvailable(const QString& modelId, ProgressSink)
{
    auto* job = new ModelReadyJob(this);
    followCancel(m_gw.context(), job);
    QPointer<ModelReadyJob> guard(job);
    QNetworkReply* reply = m_gw.getJson(QStringLiteral("/models"), job, [guard, modelId](const HttpExchange& ex) {
        if (!detail::GatewayClient::ok(ex)) { guard->fail(detail::GatewayClient::errorOf(ex)); return; }
        for (const ModelInfo& m : modelInfos(ex.body, CapabilityKind::ModelCatalog, /*filter*/ false))
            if (m.id == modelId) {
                ModelLocation loc;
                loc.modelId = modelId;
                loc.remoteId = modelId;   // nothing to download: the gateway runs it
                guard->complete(loc);
                return;
            }
        guard->fail(inputError(QStringLiteral("unknown_model"), modelId));
    });
    connect(job, &QObject::destroyed, this, [r = QPointer<QNetworkReply>(reply)]() { detail::abortReply(r); });
    if (!reply) job->failLater(unsupported(QStringLiteral("no_http_client"), QStringLiteral("no HTTP client")));
    return job;
}

// ---- registration -------------------------------------------------------------------------------

namespace {

ProviderDescriptor cloudDescriptor(InputKind input)
{
    ProviderDescriptor d;
    d.id              = ProviderId;
    d.displayName     = QCoreApplication::translate("BuiltinProviders", "Tanara Cloud");
    d.origin          = Origin::Cloud;
    d.kind            = ProviderKind::Cloud;
    d.traits.dataLeavesDevice = true;
    d.traits.metered  = true;
    d.traits.worksOffline = false;
    d.traits.inputs   = { input };
    d.catalogRef      = ProviderId;
    d.authMode        = AuthMode::Login;
    d.loginSecretKey  = ApiKeySecret;
    d.networkRequired = true;
    return d;
}

} // namespace

void registerInto(ProviderRegistry& registry, CloudSession& session, CloudCapabilityDeps deps)
{
    QPointer<CloudSession> s(&session);
    registerCloudProviders(registry);   // STT + chat (the Soniox- and OpenAI-shaped gateway paths)

    // The text embedder: the built-in "tanara-hosted-embedding" descriptor gets the session-aware factory.
    ProviderDescriptor text = registry.descriptor(CapabilityKind::TextEmbedding, embeddingproviders::CloudId);
    if (text.id.isEmpty()) {
        text = cloudDescriptor(InputKind::Text);
        text.id = embeddingproviders::CloudId;
    }
    registry.add<ITextEmbedder>(text, [s](const ProviderContext& ctx, const ProviderConfig& cfg, QObject* p) -> ITextEmbedder* {
        return new CloudTextEmbedder(ctx, cfg, s, p);
    });

    ProviderDescriptor voice = cloudDescriptor(InputKind::AudioClip);
    voice.traits.inputs = { InputKind::AudioClip, InputKind::Pcm };
    const auto media = deps.media;
    registry.add<IVoiceEmbedder>(voice, [s, media](const ProviderContext& ctx, const ProviderConfig& cfg, QObject* p) -> IVoiceEmbedder* {
        return new CloudVoiceEmbedder(ctx, cfg, s, media ? media() : media::MediaToolPtr(), p);
    });
    // Identification sends vectors only (no audio, no names).
    registry.add<IParticipantIdentifier>(cloudDescriptor(InputKind::Text),
        [s](const ProviderContext& ctx, const ProviderConfig& cfg, QObject* p) -> IParticipantIdentifier* {
            return new CloudParticipantIdentifier(ctx, cfg, s, p);
        });
    registry.add<ISummarizer>(cloudDescriptor(InputKind::Text),
        [s](const ProviderContext& ctx, const ProviderConfig& cfg, QObject* p) -> ISummarizer* {
            return new CloudPipelineSummarizer(ctx, cfg, s, p);
        });
    registry.add<ITagSuggester>(cloudDescriptor(InputKind::Text),
        [s](const ProviderContext& ctx, const ProviderConfig& cfg, QObject* p) -> ITagSuggester* {
            return new CloudTagSuggester(ctx, cfg, s, p);
        });
    ProviderDescriptor catalog = cloudDescriptor(InputKind::Text);
    catalog.traits.metered = false;   // listing the catalog costs nothing
    catalog.traits.inputs.clear();
    registry.add<IModelCatalog>(catalog,
        [s](const ProviderContext& ctx, const ProviderConfig& cfg, QObject* p) -> IModelCatalog* {
            return new CloudModelCatalog(ctx, cfg, s, p);
        });
}

} // namespace tanara::cloud

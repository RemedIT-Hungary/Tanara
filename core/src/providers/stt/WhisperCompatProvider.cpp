#include "tanara/providers/stt/WhisperCompatProvider.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/foundation/log/Logging.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace tanara::providers {

namespace {

// Transcribing a long recording on a local machine can take minutes: a generous limit.
constexpr int kTransferTimeoutMs = 30 * 60 * 1000;

QString defaultBaseUrl(const ProviderConfig& cfg)
{
    QString base = cfg.baseUrl.isEmpty() ? QStringLiteral("http://localhost:8000/v1")
                                         : cfg.baseUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base;
}

QString defaultModel(const ProviderConfig& cfg)
{
    return cfg.model.isEmpty() ? QStringLiteral("whisper-1") : cfg.model;
}

// Whisper's "prompt" field biases decoding toward the given words: from the context envelope the
// terms + the free description go in (cut short; whisper looks at only the last ~224 tokens anyway).
QString promptFromContext(const SttRequest& req)
{
    QStringList parts;
    if (!req.contextTerms.isEmpty())
        parts << req.contextTerms.join(QStringLiteral(", "));
    if (!req.context.trimmed().isEmpty())
        parts << req.context.trimmed();
    QString p = parts.join(QStringLiteral(". "));
    if (p.size() > 800)
        p.truncate(800);
    return p;
}

QHttpPart textPart(const QString& name, const QString& value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"").arg(name));
    part.setBody(value.toUtf8());
    return part;
}

} // namespace

// ===================== WhisperCompatProvider =====================

WhisperCompatProvider::WhisperCompatProvider(ProviderConfig cfg, QObject* parent)
    : WhisperCompatProvider(ProviderContext{}, std::move(cfg), parent)
{
}

WhisperCompatProvider::WhisperCompatProvider(ProviderContext ctx, ProviderConfig cfg, QObject* parent)
    : QObject(parent), m_ctx(std::move(ctx)), m_cfg(std::move(cfg))
{
    m_cfg.baseUrl = defaultBaseUrl(m_cfg);
    m_cfg.model   = defaultModel(m_cfg);
}

SttJob* WhisperCompatProvider::transcribe(const SttRequest& req)
{
    auto* job = new WhisperCompatJob(m_ctx, m_cfg, req);
    m_ctx.cancel.onCancel([j = QPointer<WhisperCompatJob>(job)]() { if (j) j->cancel(); });
    // Asynchronous start, so the caller connects the finished/failed signals first.
    QTimer::singleShot(0, job, &WhisperCompatJob::start);
    return job;
}

// ===================== WhisperCompatJob =====================

WhisperCompatJob::WhisperCompatJob(ProviderContext ctx, ProviderConfig cfg, SttRequest req, QObject* parent)
    : SttJob(parent), m_ctx(std::move(ctx)), m_cfg(std::move(cfg)), m_req(std::move(req))
{
    m_http = m_ctx.http ? m_ctx.http.data() : new net::HttpClient(this);
}

WhisperCompatJob::~WhisperCompatJob()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void WhisperCompatJob::setState(SttJobState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void WhisperCompatJob::fail(const QString& error, const Error& classified)
{
    if (m_finished)
        return;
    m_finished = true;
    m_error = classified;
    if (!m_error.isError()) {
        m_error.kind = Error::Kind::Service;
        m_error.message = error;
    }
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    setState(SttJobState::Failed);
    emit failed(error);
}

void WhisperCompatJob::cancel()
{
    if (m_finished)
        return;
    fail(tr("Megszakítva."), Error::cancelled());
}

void WhisperCompatJob::start()
{
    auto* file = new QFile(m_req.audioFilePath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        fail(tr("Nem olvasható a hangfájl: %1").arg(m_req.audioFilePath));
        return;
    }

    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    const QString fileName = QFileInfo(*file).fileName();
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(fileName));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                       QMimeDatabase().mimeTypeForFile(m_req.audioFilePath).name());
    filePart.setBodyDevice(file);
    file->setParent(multi);   // bound to the multipart's lifetime
    multi->append(filePart);

    multi->append(textPart(QStringLiteral("model"), m_cfg.model));
    multi->append(textPart(QStringLiteral("response_format"), QStringLiteral("verbose_json")));
    multi->append(textPart(QStringLiteral("timestamp_granularities[]"), QStringLiteral("word")));
    multi->append(textPart(QStringLiteral("timestamp_granularities[]"), QStringLiteral("segment")));

    // Language: the config's "language" field (ISO-639-1, e.g. "en"); empty = the server detects.
    const QString lang = m_cfg.extra.value(QStringLiteral("language")).toString().trimmed();
    if (!lang.isEmpty())
        multi->append(textPart(QStringLiteral("language"), lang));

    const QString prompt = promptFromContext(m_req);
    if (!prompt.isEmpty())
        multi->append(textPart(QStringLiteral("prompt"), prompt));

    QNetworkRequest request{QUrl(m_cfg.baseUrl + QStringLiteral("/audio/transcriptions"))};
    const QString bearer = m_ctx.bearer(m_cfg);
    if (!bearer.trimmed().isEmpty())
        request.setRawHeader("Authorization",
                             QByteArrayLiteral("Bearer ") + bearer.toUtf8());
    m_ctx.applyHeaders(request);
    request.setTransferTimeout(kTransferTimeoutMs);

    setState(SttJobState::Uploading);
    emit progress(0, tr("Hang feltöltése…"));

    m_reply = m_http->post(request, multi);
    multi->setParent(m_reply);

    connect(m_reply, &QNetworkReply::uploadProgress, this,
            [this](qint64 sent, qint64 total) {
                if (total > 0) {
                    emit progress(int(sent * 60 / total), tr("Hang feltöltése…"));
                    emit uploadProgress(sent, total);
                }
                if (total > 0 && sent == total) {
                    setState(SttJobState::Processing);
                    emit progress(60, tr("Átírás folyamatban…"));
                }
            });
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        if (m_finished)
            return;
        const QByteArray body = reply->readAll();
        // Not reported to ProviderContext::observe (as before P9): the job's own error text stays the
        // remembered error's message; the classification goes into error().
        if (reply->error() != QNetworkReply::NoError) {
            const Error classified = net::errorFromExchange(net::makeHttpExchange(reply, body));
            const int http =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qCWarning(lcStt).noquote()
                << "[whisper] HTTP" << http << reply->errorString()
                << "body:" << QString::fromUtf8(body.left(500));
            fail(http > 0 ? tr("A szerver hibát adott (HTTP %1).").arg(http)
                          : tr("Hálózati hiba: %1").arg(reply->errorString()),
                 classified);
            return;
        }
        parseResponse(body);
    });
}

void WhisperCompatJob::parseResponse(const QByteArray& body)
{
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        fail(tr("Érvénytelen válasz a szervertől (nem JSON)."));
        return;
    }
    const QJsonObject obj = doc.object();

    TrackTranscript result;
    result.trackId      = m_req.trackId;
    result.speakerLabel = m_req.speakerLabel;
    result.language     = obj.value(QStringLiteral("language")).toString();

    // No diarization: every token is speaker "1"; the caller translates it to "Speaker 1", and the name
    // comes from voice ID / manual rename.
    const QString speaker = QStringLiteral("1");

    auto wordToken = [&](const QJsonObject& w) {
        TranscriptToken tok;
        tok.text    = w.value(QStringLiteral("word")).toString();
        tok.startMs = qint64(w.value(QStringLiteral("start")).toDouble() * 1000.0);
        tok.endMs   = qint64(w.value(QStringLiteral("end")).toDouble() * 1000.0);
        // faster-whisper: no confidence; whisper.cpp: "probability" (0..1).
        tok.confidence = w.value(QStringLiteral("probability")).toDouble(1.0);
        tok.speaker    = speaker;
        tok.trackId    = m_req.trackId;
        return tok;
    };

    // 1) OpenAI / faster-whisper shape: top-level "words".
    const QJsonArray words = obj.value(QStringLiteral("words")).toArray();
    for (const QJsonValue& v : words) {
        const TranscriptToken tok = wordToken(v.toObject());
        if (!tok.text.trimmed().isEmpty())
            result.tokens.append(tok);
    }

    const QJsonArray segments = obj.value(QStringLiteral("segments")).toArray();

    // 2) whisper.cpp shape: the words are WITHIN the segments ("segments[].words").
    if (result.tokens.isEmpty()) {
        for (const QJsonValue& sv : segments)
            for (const QJsonValue& wv : sv.toObject().value(QStringLiteral("words")).toArray()) {
                const TranscriptToken tok = wordToken(wv.toObject());
                if (!tok.text.trimmed().isEmpty())
                    result.tokens.append(tok);
            }
    }

    // 3) Fallback: without word-level timestamps one token per segment; the clickable transcript keeps
    // working at segment resolution.
    if (result.tokens.isEmpty()) {
        for (const QJsonValue& v : segments) {
            const QJsonObject s = v.toObject();
            TranscriptToken tok;
            tok.text       = s.value(QStringLiteral("text")).toString().trimmed();
            tok.startMs    = qint64(s.value(QStringLiteral("start")).toDouble() * 1000.0);
            tok.endMs      = qint64(s.value(QStringLiteral("end")).toDouble() * 1000.0);
            tok.confidence = 1.0;
            tok.speaker    = speaker;
            tok.trackId    = m_req.trackId;
            if (!tok.text.isEmpty())
                result.tokens.append(tok);
        }
    }

    if (result.tokens.isEmpty()) {
        fail(tr("A válaszban nincs időbélyeges átirat (words/segments)."));
        return;
    }

    m_finished = true;
    emit progress(100, tr("Kész"));
    setState(SttJobState::Completed);
    emit finished(result);
}

} // namespace tanara::providers
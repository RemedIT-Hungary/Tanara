#include "tanara/stt/WhisperCompatProvider.h"
#include "tanara/Logging.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace tanara {

namespace {

// Hosszú felvétel átírása lokális gépen percekig tarthat — bőséges keret.
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

// A whisper "prompt" mezője a dekódolást torzítja a megadott szavak felé — a
// context-envelope-ból a terms + a szabad leírás megy bele (rövidre vágva; a
// whisper amúgy is csak az utolsó ~224 tokent nézi).
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
    : QObject(parent), m_cfg(std::move(cfg))
{
    m_cfg.baseUrl = defaultBaseUrl(m_cfg);
    m_cfg.model   = defaultModel(m_cfg);
}

SttJob* WhisperCompatProvider::transcribe(const SttRequest& req)
{
    auto* job = new WhisperCompatJob(m_cfg, req);
    // Aszinkron indítás, hogy a hívó előbb rákösse a finished/failed jeleket.
    QTimer::singleShot(0, job, &WhisperCompatJob::start);
    return job;
}

// ===================== WhisperCompatJob =====================

WhisperCompatJob::WhisperCompatJob(ProviderConfig cfg, SttRequest req, QObject* parent)
    : SttJob(parent), m_cfg(std::move(cfg)), m_req(std::move(req))
{
    m_nam = new QNetworkAccessManager(this);
}

WhisperCompatJob::~WhisperCompatJob()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void WhisperCompatJob::setState(JobState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void WhisperCompatJob::fail(const QString& error)
{
    if (m_finished)
        return;
    m_finished = true;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    setState(JobState::Failed);
    emit failed(error);
}

void WhisperCompatJob::cancel()
{
    if (m_finished)
        return;
    fail(tr("Megszakítva."));
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
    file->setParent(multi);   // a multipart élettartamához kötve
    multi->append(filePart);

    multi->append(textPart(QStringLiteral("model"), m_cfg.model));
    multi->append(textPart(QStringLiteral("response_format"), QStringLiteral("verbose_json")));
    multi->append(textPart(QStringLiteral("timestamp_granularities[]"), QStringLiteral("word")));
    multi->append(textPart(QStringLiteral("timestamp_granularities[]"), QStringLiteral("segment")));

    // Nyelv: a config "language" mezője (ISO-639-1, pl. "en"); üres → a szerver detektál.
    const QString lang = m_cfg.extra.value(QStringLiteral("language")).toString().trimmed();
    if (!lang.isEmpty())
        multi->append(textPart(QStringLiteral("language"), lang));

    const QString prompt = promptFromContext(m_req);
    if (!prompt.isEmpty())
        multi->append(textPart(QStringLiteral("prompt"), prompt));

    QNetworkRequest request{QUrl(m_cfg.baseUrl + QStringLiteral("/audio/transcriptions"))};
    if (!m_cfg.apiKey.trimmed().isEmpty())
        request.setRawHeader("Authorization",
                             QByteArrayLiteral("Bearer ") + m_cfg.apiKey.toUtf8());
    request.setTransferTimeout(kTransferTimeoutMs);

    setState(JobState::Uploading);
    emit progress(0, tr("Hang feltöltése…"));

    m_reply = m_nam->post(request, multi);
    multi->setParent(m_reply);

    connect(m_reply, &QNetworkReply::uploadProgress, this,
            [this](qint64 sent, qint64 total) {
                if (total > 0) {
                    emit progress(int(sent * 60 / total), tr("Hang feltöltése…"));
                    emit uploadProgress(sent, total);
                }
                if (total > 0 && sent == total) {
                    setState(JobState::Processing);
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
        if (reply->error() != QNetworkReply::NoError) {
            const int http =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qCWarning(lcStt).noquote()
                << "[whisper] HTTP" << http << reply->errorString()
                << "body:" << QString::fromUtf8(body.left(500));
            fail(http > 0 ? tr("A szerver hibát adott (HTTP %1).").arg(http)
                          : tr("Hálózati hiba: %1").arg(reply->errorString()));
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

    // Nincs diarizáció: minden token az "1" beszélő → a hívó „Beszélő 1"-re fordítja,
    // a nevet a voice-ID / kézi átnevezés adja.
    const QString speaker = QStringLiteral("1");

    auto wordToken = [&](const QJsonObject& w) {
        TranscriptToken tok;
        tok.text    = w.value(QStringLiteral("word")).toString();
        tok.startMs = qint64(w.value(QStringLiteral("start")).toDouble() * 1000.0);
        tok.endMs   = qint64(w.value(QStringLiteral("end")).toDouble() * 1000.0);
        // faster-whisper: nincs confidence; whisper.cpp: "probability" (0..1).
        tok.confidence = w.value(QStringLiteral("probability")).toDouble(1.0);
        tok.speaker    = speaker;
        tok.trackId    = m_req.trackId;
        return tok;
    };

    // 1) OpenAI / faster-whisper alak: top-level "words".
    const QJsonArray words = obj.value(QStringLiteral("words")).toArray();
    for (const QJsonValue& v : words) {
        const TranscriptToken tok = wordToken(v.toObject());
        if (!tok.text.trimmed().isEmpty())
            result.tokens.append(tok);
    }

    const QJsonArray segments = obj.value(QStringLiteral("segments")).toArray();

    // 2) whisper.cpp alak: a szavak a szegmenseken BELÜL vannak ("segments[].words").
    if (result.tokens.isEmpty()) {
        for (const QJsonValue& sv : segments)
            for (const QJsonValue& wv : sv.toObject().value(QStringLiteral("words")).toArray()) {
                const TranscriptToken tok = wordToken(wv.toObject());
                if (!tok.text.trimmed().isEmpty())
                    result.tokens.append(tok);
            }
    }

    // 3) Fallback: szó-szintű időbélyeg nélkül szegmensenként egy token — a kattintható
    // átirat szegmens-felbontással működik tovább.
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
    setState(JobState::Completed);
    emit finished(result);
}

} // namespace tanara

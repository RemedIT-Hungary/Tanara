#include "tanara/providers/stt/SonioxProvider.h"
#include "tanara/cloud/CloudTypes.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QMimeDatabase>

namespace tanara::providers {

namespace {
constexpr int kPollIntervalMs = 2000;
// Token timing: Soniox gives the moment the token was EMITTED (~60 ms wide, toward the end of the
// word; measured: "R" 127410-127470, "ok" 127530-127590), not the start of the sound. The tokens stay
// untouched; the result gets an emissionTiming flag, and the merger pads the finished lines
// (sonioxtiming::padUtterances; the constants and the measurement: tanara/stt/SonioxTiming.h).

QString defaultModel(const ProviderConfig& cfg) {
    return cfg.model.isEmpty() ? QStringLiteral("stt-async-v5") : cfg.model;
}

QString defaultBaseUrl(const ProviderConfig& cfg) {
    QString base = cfg.baseUrl.isEmpty() ? QStringLiteral("https://api.soniox.com/v1")
                                         : cfg.baseUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base;
}
} // namespace

// ===================== SonioxProvider =====================

SonioxProvider::SonioxProvider(ProviderConfig cfg, QObject* parent)
    : QObject(parent), m_cfg(std::move(cfg)) {
    m_cfg.baseUrl = defaultBaseUrl(m_cfg);
    m_cfg.model   = defaultModel(m_cfg);
}

SttJob* SonioxProvider::transcribe(const SttRequest& req) {
    auto* job = new SonioxJob(m_cfg, req);
    // Asynchronous start, so the caller connects the finished/failed signals first.
    QTimer::singleShot(0, job, &SonioxJob::start);
    return job;
}

// ===================== SonioxJob =====================

SonioxJob::SonioxJob(ProviderConfig cfg, SttRequest req, QObject* parent)
    : SttJob(parent), m_cfg(std::move(cfg)), m_req(std::move(req)) {
    m_nam = new QNetworkAccessManager(this);
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &SonioxJob::onPollTick);
}

SonioxJob::~SonioxJob() {
    abortInFlight();
}

QNetworkRequest SonioxJob::makeRequest(const QString& path) const {
    QNetworkRequest r{QUrl(m_cfg.baseUrl + path)};
    r.setRawHeader("Authorization",
                   QByteArrayLiteral("Bearer ") + m_cfg.apiKey.toUtf8());
    // Gateway mode (Tanara Cloud): additive X-Tanara-* / Accept-Language headers.
    for (const auto& h : m_cfg.extraHeaders)
        r.setRawHeader(h.first, h.second);
    return r;
}

void SonioxJob::report(QNetworkReply* reply, const QByteArray& body) const {
    if (m_cfg.onExchange)
        m_cfg.onExchange(cloud::makeHttpExchange(reply, body));
}

void SonioxJob::setState(SttJobState state) {
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void SonioxJob::fail(const QString& error) {
    if (m_finished)
        return;
    m_finished = true;
    abortInFlight();
    // We clean up after a failed/cancelled job too: the already uploaded file / created transcription
    // must not stay at Soniox until the 30-day eviction (storage quota + data).
    cleanup();
    setState(SttJobState::Failed);
    emit failed(error);
}

void SonioxJob::abortInFlight() {
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void SonioxJob::cancel() {
    if (m_finished)
        return;
    fail(QStringLiteral("cancelled"));
}

namespace {
// Elapsed time in "m:ss" form for the UI progress.
QString formatElapsed(qint64 ms) {
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

void SonioxJob::start() {
    if (m_finished)
        return;
    m_clock.start();   // the elapsed time measures the job's full length (upload -> poll -> download)
    uploadFile();
}

// --- 1) POST /files (multipart) -> file_id ---
void SonioxJob::uploadFile() {
    setState(SttJobState::Uploading);
    emit progress(0, tr("Fájl feltöltése…"));

    auto* file = new QFile(m_req.audioFilePath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        fail(tr("Nem nyitható meg az audiofájl: %1").arg(m_req.audioFilePath));
        return;
    }

    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    QFileInfo fi(m_req.audioFilePath);
    const QString mime = QMimeDatabase().mimeTypeForFile(fi).name();
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, mime);
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                           .arg(fi.fileName()));
    file->setParent(multi);     // the multipart owns the file
    filePart.setBodyDevice(file);
    multi->append(filePart);

    QNetworkRequest req = makeRequest(QStringLiteral("/files"));
    QNetworkReply* reply = m_nam->post(req, multi);
    multi->setParent(reply);    // the reply owns the multipart
    m_reply = reply;
    // Real upload progress for the structured process signal (the "Upload" stage).
    connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (total > 0) emit uploadProgress(sent, total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_reply != reply) {          // cancelled / replaced
            reply->deleteLater();
            return;
        }
        m_reply = nullptr;
        const QByteArray body = reply->readAll();
        const auto err = reply->error();
        report(reply, body);
        reply->deleteLater();
        if (m_finished)
            return;
        if (err != QNetworkReply::NoError) {
            fail(tr("Feltöltés hiba: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        m_fileId = obj.value(QStringLiteral("id")).toString();
        if (m_fileId.isEmpty()) {
            fail(tr("Nincs file id a válaszban: %1")
                     .arg(QString::fromUtf8(body)));
            return;
        }
        createTranscription();
    });
}

// --- 2) POST /transcriptions -> transcription id ---
void SonioxJob::createTranscription() {
    if (m_finished)
        return;
    setState(SttJobState::Queued);
    emit progress(20, tr("Átírás indítása…"));

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), m_cfg.model);
    payload.insert(QStringLiteral("file_id"), m_fileId);
    QJsonArray hints;
    for (const QString& h : m_req.languageHints)
        hints.append(h);
    payload.insert(QStringLiteral("language_hints"), hints);
    payload.insert(QStringLiteral("enable_speaker_diarization"), m_req.diarization);
    // Context envelope -> Soniox stt-async-v5's structured "context" object (general/text/terms). With it
    // the STT decides better in doubtful parts (names, jargon, topic). We send only the filled sections;
    // never an empty object. Limit: ~8000 tokens (~10000 characters) for the whole object (Soniox errors beyond it).
    {
        QJsonObject context;
        if (!m_req.contextGeneral.isEmpty()) {
            QJsonArray general;
            for (auto it = m_req.contextGeneral.constBegin();
                 it != m_req.contextGeneral.constEnd(); ++it) {
                if (it.value().trimmed().isEmpty()) continue;
                QJsonObject kv;
                kv.insert(QStringLiteral("key"), it.key());
                kv.insert(QStringLiteral("value"), it.value().trimmed());
                general.append(kv);
            }
            if (!general.isEmpty()) context.insert(QStringLiteral("general"), general);
        }
        if (!m_req.context.trimmed().isEmpty())
            context.insert(QStringLiteral("text"), m_req.context.trimmed());
        if (!m_req.contextTerms.isEmpty()) {
            QJsonArray terms;
            for (const QString& t : m_req.contextTerms) {
                const QString tt = t.trimmed();
                if (!tt.isEmpty()) terms.append(tt);
            }
            if (!terms.isEmpty()) context.insert(QStringLiteral("terms"), terms);
        }
        if (!context.isEmpty())
            payload.insert(QStringLiteral("context"), context);
    }

    QNetworkRequest req = makeRequest(QStringLiteral("/transcriptions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply* reply =
        m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_reply != reply) {
            reply->deleteLater();
            return;
        }
        m_reply = nullptr;
        const QByteArray body = reply->readAll();
        const auto err = reply->error();
        report(reply, body);
        reply->deleteLater();
        if (m_finished)
            return;
        if (err != QNetworkReply::NoError) {
            fail(tr("Átírás létrehozási hiba: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        m_transcriptionId = obj.value(QStringLiteral("id")).toString();
        if (m_transcriptionId.isEmpty()) {
            fail(tr("Nincs transcription id a válaszban: %1")
                     .arg(QString::fromUtf8(body)));
            return;
        }
        setState(SttJobState::Processing);
        emit progress(40, tr("Feldolgozás…"));
        m_pollTimer->start();
    });
}

// --- 3) poll GET /transcriptions/<id> ---
void SonioxJob::onPollTick() {
    pollStatus();
}

void SonioxJob::pollStatus() {
    if (m_finished)
        return;
    if (m_reply)        // the previous poll is still running, we wait for it
        return;

    QNetworkRequest req =
        makeRequest(QStringLiteral("/transcriptions/") + m_transcriptionId);
    QNetworkReply* reply = m_nam->get(req);
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_reply != reply) {
            reply->deleteLater();
            return;
        }
        m_reply = nullptr;
        const QByteArray body = reply->readAll();
        const auto err = reply->error();
        report(reply, body);
        reply->deleteLater();
        if (m_finished)
            return;
        if (err != QNetworkReply::NoError) {
            fail(tr("Státusz lekérdezési hiba: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        const QString status = obj.value(QStringLiteral("status")).toString();

        if (status == QLatin1String("completed")) {
            m_pollTimer->stop();
            emit progress(80, tr("Átirat letöltése…"));
            fetchTranscript();
        } else if (status == QLatin1String("error")) {
            m_pollTimer->stop();
            QString msg = obj.value(QStringLiteral("error_message")).toString();
            if (msg.isEmpty())
                msg = tr("ismeretlen Soniox hiba");
            fail(msg);
        } else {
            // queued / processing / running etc.: we stay in the poll. Every successful poll is a "heartbeat",
            // so the elapsed time + the counter visibly move, the UI does not look frozen, and it shows the connection is alive.
            ++m_pollCount;
            setState(SttJobState::Processing);
            emit progress(60, tr("Feldolgozás (%1)… %2 · %3. életjel")
                                  .arg(status, formatElapsed(m_clock.elapsed()))
                                  .arg(m_pollCount));
        }
    });
}

// --- 4) GET /transcriptions/<id>/transcript -> tokens ---
void SonioxJob::fetchTranscript() {
    if (m_finished)
        return;

    QNetworkRequest req = makeRequest(QStringLiteral("/transcriptions/") +
                                      m_transcriptionId +
                                      QStringLiteral("/transcript"));
    QNetworkReply* reply = m_nam->get(req);
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_reply != reply) {
            reply->deleteLater();
            return;
        }
        m_reply = nullptr;
        const QByteArray body = reply->readAll();
        const auto err = reply->error();
        report(reply, body);
        reply->deleteLater();
        if (m_finished)
            return;
        if (err != QNetworkReply::NoError) {
            fail(tr("Átirat letöltési hiba: %1").arg(reply->errorString()));
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        const QJsonArray tokens = obj.value(QStringLiteral("tokens")).toArray();

        TrackTranscript tt;
        tt.trackId = m_req.trackId;
        tt.speakerLabel = m_req.speakerLabel;
        tt.language = m_req.languageHints.value(0);
        tt.tokens.reserve(tokens.size());

        for (const QJsonValue& v : tokens) {
            const QJsonObject t = v.toObject();
            TranscriptToken tok;
            tok.text = t.value(QStringLiteral("text")).toString();
            tok.startMs = static_cast<qint64>(
                t.value(QStringLiteral("start_ms")).toDouble());
            tok.endMs = static_cast<qint64>(
                t.value(QStringLiteral("end_ms")).toDouble());
            tok.confidence = t.value(QStringLiteral("confidence")).toDouble();
            tok.trackId = m_req.trackId;
            if (m_req.diarization) {
                // diarization on: we use Soniox's speaker field
                const QJsonValue sp = t.value(QStringLiteral("speaker"));
                tok.speaker = sp.isString() ? sp.toString()
                                            : QString::number(sp.toInt());
            } else {
                // per track = one known speaker
                tok.speaker = m_req.speakerLabel;
            }
            tt.tokens.append(tok);
        }
        // Emission moment, not an audio range: the merger does the line padding.
        tt.emissionTiming = true;

        // Best-effort cleanup in the background, then the done signal.
        cleanup();

        m_finished = true;
        setState(SttJobState::Completed);
        emit progress(100, tr("Kész"));
        emit finished(tt);
    });
}

// --- 5) best-effort cleanup: DELETE file + transcription ---
void SonioxJob::cleanup() {
    auto fireDelete = [this](const QString& path) {
        if (path.isEmpty())
            return;
        QNetworkReply* r = m_nam->deleteResource(makeRequest(path));
        // we ignore the error and only clean up (the gateway hook gets the response)
        connect(r, &QNetworkReply::finished, r, [r, hook = m_cfg.onExchange]() {
            if (hook) hook(cloud::makeHttpExchange(r, r->readAll()));
            r->deleteLater();
        });
    };
    if (!m_transcriptionId.isEmpty())
        fireDelete(QStringLiteral("/transcriptions/") + m_transcriptionId);
    if (!m_fileId.isEmpty())
        fireDelete(QStringLiteral("/files/") + m_fileId);
}

} // namespace tanara::providers
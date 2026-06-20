#include "tanara/stt/SonioxProvider.h"

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

namespace tanara {

namespace {
constexpr int kPollIntervalMs = 2000;

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
    // Aszinkron indítás, hogy a hívó előbb rákösse a finished/failed jeleket.
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
    return r;
}

void SonioxJob::setState(JobState state) {
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
    setState(JobState::Failed);
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

void SonioxJob::start() {
    if (m_finished)
        return;
    uploadFile();
}

// --- 1) POST /files (multipart) -> file_id ---
void SonioxJob::uploadFile() {
    setState(JobState::Uploading);
    emit progress(0, QStringLiteral("Fájl feltöltése…"));

    auto* file = new QFile(m_req.audioFilePath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        fail(QStringLiteral("Nem nyitható meg az audiofájl: %1").arg(m_req.audioFilePath));
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
    file->setParent(multi);     // a multipart birtokolja a fájlt
    filePart.setBodyDevice(file);
    multi->append(filePart);

    QNetworkRequest req = makeRequest(QStringLiteral("/files"));
    QNetworkReply* reply = m_nam->post(req, multi);
    multi->setParent(reply);    // a reply birtokolja a multipartot
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_reply != reply) {          // megszakítva / lecserélve
            reply->deleteLater();
            return;
        }
        m_reply = nullptr;
        const QByteArray body = reply->readAll();
        const auto err = reply->error();
        reply->deleteLater();
        if (m_finished)
            return;
        if (err != QNetworkReply::NoError) {
            fail(QStringLiteral("Feltöltés hiba: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        m_fileId = obj.value(QStringLiteral("id")).toString();
        if (m_fileId.isEmpty()) {
            fail(QStringLiteral("Nincs file id a válaszban: %1")
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
    setState(JobState::Queued);
    emit progress(20, QStringLiteral("Átírás indítása…"));

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), m_cfg.model);
    payload.insert(QStringLiteral("file_id"), m_fileId);
    QJsonArray hints;
    for (const QString& h : m_req.languageHints)
        hints.append(h);
    payload.insert(QStringLiteral("language_hints"), hints);
    payload.insert(QStringLiteral("enable_speaker_diarization"), m_req.diarization);
    // Context-envelope: szabad szöveg (meeting-cím + a felhasználó pár szavas leírása,
    // később naptár-bejegyzés). A Soniox ezzel jobban dönt a kétes/félreérthető
    // részeknél (nevek, szakszavak, téma). Üresen nem küldjük.
    if (!m_req.context.trimmed().isEmpty())
        payload.insert(QStringLiteral("context"), m_req.context.trimmed());

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
        reply->deleteLater();
        if (m_finished)
            return;
        if (err != QNetworkReply::NoError) {
            fail(QStringLiteral("Átírás létrehozási hiba: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        m_transcriptionId = obj.value(QStringLiteral("id")).toString();
        if (m_transcriptionId.isEmpty()) {
            fail(QStringLiteral("Nincs transcription id a válaszban: %1")
                     .arg(QString::fromUtf8(body)));
            return;
        }
        setState(JobState::Processing);
        emit progress(40, QStringLiteral("Feldolgozás…"));
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
    if (m_reply)        // előző poll még fut, várjuk meg
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
        reply->deleteLater();
        if (m_finished)
            return;
        if (err != QNetworkReply::NoError) {
            fail(QStringLiteral("Státusz lekérdezési hiba: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        const QString status = obj.value(QStringLiteral("status")).toString();

        if (status == QLatin1String("completed")) {
            m_pollTimer->stop();
            emit progress(80, QStringLiteral("Átirat letöltése…"));
            fetchTranscript();
        } else if (status == QLatin1String("error")) {
            m_pollTimer->stop();
            QString msg = obj.value(QStringLiteral("error_message")).toString();
            if (msg.isEmpty())
                msg = QStringLiteral("ismeretlen Soniox hiba");
            fail(msg);
        } else {
            // queued / processing / running stb. — maradunk a poll-ban
            setState(JobState::Processing);
            emit progress(60, QStringLiteral("Feldolgozás (%1)…").arg(status));
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
        reply->deleteLater();
        if (m_finished)
            return;
        if (err != QNetworkReply::NoError) {
            fail(QStringLiteral("Átirat letöltési hiba: %1").arg(reply->errorString()));
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
                // diarizáció be: a Soniox speaker mezőjét használjuk
                const QJsonValue sp = t.value(QStringLiteral("speaker"));
                tok.speaker = sp.isString() ? sp.toString()
                                            : QString::number(sp.toInt());
            } else {
                // per-sáv = egy ismert beszélő
                tok.speaker = m_req.speakerLabel;
            }
            tt.tokens.append(tok);
        }

        // Best-effort takarítás a háttérben, majd kész jelzés.
        cleanup();

        m_finished = true;
        setState(JobState::Completed);
        emit progress(100, QStringLiteral("Kész"));
        emit finished(tt);
    });
}

// --- 5) best-effort cleanup: DELETE file + transcription ---
void SonioxJob::cleanup() {
    auto fireDelete = [this](const QString& path) {
        if (path.isEmpty())
            return;
        QNetworkReply* r = m_nam->deleteResource(makeRequest(path));
        // hibát figyelmen kívül hagyjuk, csak takarítunk
        connect(r, &QNetworkReply::finished, r, &QNetworkReply::deleteLater);
    };
    if (!m_transcriptionId.isEmpty())
        fireDelete(QStringLiteral("/transcriptions/") + m_transcriptionId);
    if (!m_fileId.isEmpty())
        fireDelete(QStringLiteral("/files/") + m_fileId);
}

} // namespace tanara

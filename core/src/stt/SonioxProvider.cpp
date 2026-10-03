#include "tanara/stt/SonioxProvider.h"
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
    // Gateway-mód (Tanara Cloud): additív X-Tanara-* / Accept-Language fejlécek.
    for (const auto& h : m_cfg.extraHeaders)
        r.setRawHeader(h.first, h.second);
    return r;
}

void SonioxJob::report(QNetworkReply* reply, const QByteArray& body) const {
    if (m_cfg.onExchange)
        m_cfg.onExchange(makeHttpExchange(reply, body));
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
    // Hibás/megszakított job után is takarítunk: a már feltöltött fájl / létrehozott
    // transcription ne maradjon a Sonioxnál a 30 napos evictionig (tárolási kvóta + adat).
    cleanup();
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

namespace {
// Eltelt idő „m:ss" formában a UI-progresshez.
QString formatElapsed(qint64 ms) {
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

void SonioxJob::start() {
    if (m_finished)
        return;
    m_clock.start();   // az eltelt idő a job teljes hosszát méri (feltöltés → poll → letöltés)
    uploadFile();
}

// --- 1) POST /files (multipart) -> file_id ---
void SonioxJob::uploadFile() {
    setState(JobState::Uploading);
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
    file->setParent(multi);     // a multipart birtokolja a fájlt
    filePart.setBodyDevice(file);
    multi->append(filePart);

    QNetworkRequest req = makeRequest(QStringLiteral("/files"));
    QNetworkReply* reply = m_nam->post(req, multi);
    multi->setParent(reply);    // a reply birtokolja a multipartot
    m_reply = reply;
    // Valós feltöltés-haladás a strukturált folyamat-jelzéshez (M04 „Feltöltés” szakasz).
    connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (total > 0) emit uploadProgress(sent, total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_reply != reply) {          // megszakítva / lecserélve
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
    setState(JobState::Queued);
    emit progress(20, tr("Átírás indítása…"));

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), m_cfg.model);
    payload.insert(QStringLiteral("file_id"), m_fileId);
    QJsonArray hints;
    for (const QString& h : m_req.languageHints)
        hints.append(h);
    payload.insert(QStringLiteral("language_hints"), hints);
    payload.insert(QStringLiteral("enable_speaker_diarization"), m_req.diarization);
    // Context-envelope → a Soniox stt-async-v5 strukturált „context" objektuma
    // (general/text/terms). Ezzel jobban dönt a kétes részeknél (nevek, szakszavak,
    // téma). Csak a kitöltött szekciókat küldjük; üres objektumot nem.
    // Limit: ~8000 token (~10000 karakter) az egész objektumra (a Soniox hibát ad túl).
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
        setState(JobState::Processing);
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
            // queued / processing / running stb. — maradunk a poll-ban. Minden sikeres
            // poll egy „életjel" → az eltelt idő + a számláló láthatóan mozog, így a UI
            // nem tűnik befagyottnak, és látszik, hogy a kapcsolat él.
            ++m_pollCount;
            setState(JobState::Processing);
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
        // hibát figyelmen kívül hagyjuk, csak takarítunk (a gateway-hook megkapja a választ)
        connect(r, &QNetworkReply::finished, r, [r, hook = m_cfg.onExchange]() {
            if (hook) hook(makeHttpExchange(r, r->readAll()));
            r->deleteLater();
        });
    };
    if (!m_transcriptionId.isEmpty())
        fireDelete(QStringLiteral("/transcriptions/") + m_transcriptionId);
    if (!m_fileId.isEmpty())
        fireDelete(QStringLiteral("/files/") + m_fileId);
}

} // namespace tanara

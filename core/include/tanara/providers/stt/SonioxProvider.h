#pragma once
//
// Soniox async STT provider (https://soniox.com).
// An asynchronous flow over the shared HTTP client (ProviderContext::http; a private one if none):
//   files (upload) -> transcriptions (create) -> poll -> transcript (fetch) -> cleanup.
// Per-track use: if diarization=false, every token gets the track's fixed speaker.
#include "tanara/providers/stt/ISttProvider.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderContext.h"
#include "tanara/domain/Job.h"

#include <QObject>
#include <QString>
#include <QPointer>
#include <QElapsedTimer>
#include <QNetworkRequest>

class QNetworkReply;
class QTimer;

namespace tanara::providers {

// A concrete Soniox transcription job. SonioxProvider::transcribe() returns one.
class SonioxJob : public SttJob {
    Q_OBJECT
public:
    SonioxJob(ProviderContext ctx, ProviderConfig cfg, SttRequest req, QObject* parent = nullptr);
    ~SonioxJob() override;

    void start();              // the provider calls it after handing the job back
    void cancel() override;

private slots:
    void onPollTick();

private:
    // --- flow steps ---
    void uploadFile();
    void createTranscription();
    void pollStatus();
    void fetchTranscript();
    void cleanup();            // best-effort: delete the file + transcription

    // --- helpers ---
    QNetworkRequest makeRequest(const QString& path) const;
    QString clientReference() const;   // client_reference_id of /files and /transcriptions (empty: not sent)
    void report(QNetworkReply* reply, const QByteArray& body);   // ProviderContext::observe + last failure
    void fail(const QString& error);
    void setState(SttJobState state);
    void abortInFlight();

    ProviderContext m_ctx;
    ProviderConfig m_cfg;
    SttRequest     m_req;

    net::HttpClient* m_http = nullptr;     // m_ctx.http, or an own one (child)
    Error          m_lastFailure;          // the last failed (non-DELETE) HTTP exchange
    QPointer<QNetworkReply> m_reply;       // the request currently running
    QTimer*        m_pollTimer = nullptr;

    QString  m_fileId;
    QString  m_transcriptionId;
    SttJobState m_state = SttJobState::Idle;
    bool     m_finished = false;           // so we do not emit again after finished/failed

    QElapsedTimer m_clock;                 // total elapsed time since the job started (UI progress)
    int      m_pollCount = 0;              // number of successful poll heartbeats (the connection is alive)
};

class SonioxProvider : public QObject, public ISttProvider {
    Q_OBJECT
public:
    explicit SonioxProvider(ProviderConfig cfg, QObject* parent = nullptr);
    SonioxProvider(ProviderContext ctx, ProviderConfig cfg, QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("soniox"); }
    bool supportsLive() const override { return false; }

    SttJob* transcribe(const SttRequest& req) override;

private:
    ProviderContext m_ctx;
    ProviderConfig m_cfg;
};

} // namespace tanara::providers
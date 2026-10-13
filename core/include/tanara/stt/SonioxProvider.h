#pragma once
//
// Soniox async STT provider (https://soniox.com).
// An asynchronous, QNetworkAccessManager-based flow:
//   files (upload) -> transcriptions (create) -> poll -> transcript (fetch) -> cleanup.
// Per-track use: if diarization=false, every token gets the track's fixed speaker.
#include "tanara/stt/ISttProvider.h"
#include "tanara/Types.h"

#include <QObject>
#include <QString>
#include <QPointer>
#include <QElapsedTimer>
#include <QNetworkRequest>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace tanara {

// A concrete Soniox transcription job. SonioxProvider::transcribe() returns one.
class SonioxJob : public SttJob {
    Q_OBJECT
public:
    SonioxJob(ProviderConfig cfg, SttRequest req, QObject* parent = nullptr);
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
    void report(QNetworkReply* reply, const QByteArray& body) const;   // gateway-hook (onExchange)
    void fail(const QString& error);
    void setState(JobState state);
    void abortInFlight();

    ProviderConfig m_cfg;
    SttRequest     m_req;

    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;       // the request currently running
    QTimer*        m_pollTimer = nullptr;

    QString  m_fileId;
    QString  m_transcriptionId;
    JobState m_state = JobState::Idle;
    bool     m_finished = false;           // so we do not emit again after finished/failed

    QElapsedTimer m_clock;                 // total elapsed time since the job started (UI progress)
    int      m_pollCount = 0;              // number of successful poll heartbeats (the connection is alive)
};

class SonioxProvider : public QObject, public ISttProvider {
    Q_OBJECT
public:
    explicit SonioxProvider(ProviderConfig cfg, QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("soniox"); }
    bool supportsLive() const override { return false; }

    SttJob* transcribe(const SttRequest& req) override;

private:
    ProviderConfig m_cfg;
};

} // namespace tanara

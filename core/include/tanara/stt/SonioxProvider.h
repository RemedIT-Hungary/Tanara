#pragma once
//
// Soniox async STT provider (https://soniox.com).
// Aszinkron, QNetworkAccessManager-alapú folyamat:
//   files (upload) -> transcriptions (create) -> poll -> transcript (fetch) -> cleanup.
// Per-sáv használat: ha diarization=false, minden token a sáv fix beszélőjét kapja.
//
#include "tanara/stt/ISttProvider.h"
#include "tanara/Types.h"

#include <QObject>
#include <QString>
#include <QPointer>
#include <QNetworkRequest>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace tanara {

// Egy konkrét Soniox átírási feladat. A SonioxProvider::transcribe() ad ilyet.
class SonioxJob : public SttJob {
    Q_OBJECT
public:
    SonioxJob(ProviderConfig cfg, SttRequest req, QObject* parent = nullptr);
    ~SonioxJob() override;

    void start();              // a provider hívja meg a visszaadás után
    void cancel() override;

private slots:
    void onPollTick();

private:
    // --- folyamat-lépések ---
    void uploadFile();
    void createTranscription();
    void pollStatus();
    void fetchTranscript();
    void cleanup();            // best-effort: file + transcription törlése

    // --- segédek ---
    QNetworkRequest makeRequest(const QString& path) const;
    void fail(const QString& error);
    void setState(JobState state);
    void abortInFlight();

    ProviderConfig m_cfg;
    SttRequest     m_req;

    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;       // épp futó kérés
    QTimer*        m_pollTimer = nullptr;

    QString  m_fileId;
    QString  m_transcriptionId;
    JobState m_state = JobState::Idle;
    bool     m_finished = false;           // finished/failed után ne emittáljunk újra
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

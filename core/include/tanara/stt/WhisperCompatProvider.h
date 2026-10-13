#pragma once
//
// OpenAI-compatible Whisper STT provider (POST {baseUrl}/audio/transcriptions).
// Primarily meant for LOCAL whisper servers (faster-whisper-server, speaches); api.openai.com
// works too with an API key, but there the file limit is 25 MB.
//
// A single-pass flow: multipart upload -> verbose_json response (words/segments).
// NO diarization: every token gets speaker "1" ("Speaker 1"); the language is detected by the server if
// the config's "language" field is empty.
#include "tanara/stt/ISttProvider.h"
#include "tanara/Types.h"

#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace tanara {

class WhisperCompatJob : public SttJob {
    Q_OBJECT
public:
    WhisperCompatJob(ProviderConfig cfg, SttRequest req, QObject* parent = nullptr);
    ~WhisperCompatJob() override;

    void start();              // the provider calls it after handing the job back
    void cancel() override;

private:
    void parseResponse(const QByteArray& body);
    void fail(const QString& error);
    void setState(SttJobState state);

    ProviderConfig m_cfg;
    SttRequest     m_req;

    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    SttJobState m_state = SttJobState::Idle;
    bool     m_finished = false;
};

class WhisperCompatProvider : public QObject, public ISttProvider {
    Q_OBJECT
public:
    explicit WhisperCompatProvider(ProviderConfig cfg, QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("whisper-compat"); }
    bool supportsLive() const override { return false; }

    SttJob* transcribe(const SttRequest& req) override;

private:
    ProviderConfig m_cfg;
};

} // namespace tanara

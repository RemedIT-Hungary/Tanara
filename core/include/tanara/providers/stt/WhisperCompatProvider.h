#pragma once
//
// OpenAI-compatible Whisper STT provider (POST {baseUrl}/audio/transcriptions).
// Primarily meant for LOCAL whisper servers (faster-whisper-server, speaches); api.openai.com
// works too with an API key, but there the file limit is 25 MB.
//
// A single-pass flow: multipart upload -> verbose_json response (words/segments).
// NO diarization: every token gets speaker "1" ("Speaker 1"); the language is detected by the server if
// the config's "language" field is empty.
#include "tanara/providers/stt/ISttProvider.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderContext.h"
#include "tanara/domain/Types.h"

#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkReply;

namespace tanara::providers {

class WhisperCompatJob : public SttJob {
    Q_OBJECT
public:
    WhisperCompatJob(ProviderContext ctx, ProviderConfig cfg, SttRequest req, QObject* parent = nullptr);
    ~WhisperCompatJob() override;

    void start();              // the provider calls it after handing the job back
    void cancel() override;

private:
    void parseResponse(const QByteArray& body);
    void fail(const QString& error, const Error& classified = Error{});
    void setState(SttJobState state);

    ProviderContext m_ctx;
    ProviderConfig m_cfg;
    SttRequest     m_req;

    net::HttpClient* m_http = nullptr;     // m_ctx.http, or an own one (child)
    QPointer<QNetworkReply> m_reply;
    SttJobState m_state = SttJobState::Idle;
    bool     m_finished = false;
};

class WhisperCompatProvider : public QObject, public ISttProvider {
    Q_OBJECT
public:
    explicit WhisperCompatProvider(ProviderConfig cfg, QObject* parent = nullptr);
    WhisperCompatProvider(ProviderContext ctx, ProviderConfig cfg, QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("whisper-compat"); }
    bool supportsLive() const override { return false; }

    SttJob* transcribe(const SttRequest& req) override;

private:
    ProviderContext m_ctx;
    ProviderConfig m_cfg;
};

} // namespace tanara::providers
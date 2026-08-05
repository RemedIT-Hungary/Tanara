#pragma once
//
// OpenAI-kompatibilis Whisper STT provider (POST {baseUrl}/audio/transcriptions).
// Elsődleges cél a LOKÁLIS whisper-szerverek (faster-whisper-server, speaches) —
// az api.openai.com is működik API-kulccsal, de ott 25 MB a fájllimit.
//
// Egykörös folyamat: multipart upload → verbose_json válasz (words/segments).
// NINCS diarizáció: minden token az "1" beszélőt kapja („Beszélő 1"); a nyelvet a
// szerver detektálja, ha a config "language" mezője üres.
//
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

    void start();              // a provider hívja meg a visszaadás után
    void cancel() override;

private:
    void parseResponse(const QByteArray& body);
    void fail(const QString& error);
    void setState(JobState state);

    ProviderConfig m_cfg;
    SttRequest     m_req;

    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    JobState m_state = JobState::Idle;
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

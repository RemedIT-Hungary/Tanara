#pragma once
//
// Embedding-model plugin contract, for the "related topic" level of tag suggestions.
// One OpenAI-compatible implementation covers the LM Studio / Ollama / OpenAI and Tanara Cloud
// gateway (/v1/embeddings) endpoints.
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

struct EmbeddingRequest {
    QStringList texts;
    QString     model;   // empty = the model in the provider's config
};

class EmbeddingJob : public QObject {
    Q_OBJECT
public:
    explicit EmbeddingJob(QObject* parent = nullptr) : QObject(parent) {}
    ~EmbeddingJob() override = default;
    // Cancels the running request; neither finished nor failed follows.
    virtual void cancel() = 0;

    // On a failed HTTP response (filled before failed).
    int errorStatus() const { return m_errorStatus; }

signals:
    // The vectors in the order of the request's texts (one text -> one vector).
    void finished(QVector<QVector<float>> vectors);
    void failed(QString error);

protected:
    int m_errorStatus = 0;
};

class IEmbeddingProvider {
public:
    virtual ~IEmbeddingProvider() = default;
    virtual QString name() const = 0;
    // The job is the provider's child; the caller may delete it after the signals (deleteLater).
    virtual EmbeddingJob* embed(const EmbeddingRequest& req) = 0;
};

} // namespace tanara

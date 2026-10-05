#pragma once
//
// Beágyazó modell (embedding) plugin-szerződés — a címkejavaslatok „rokon téma” szintjéhez.
// Egy OpenAI-kompatibilis implementáció lefedi az LM Studio / Ollama / OpenAI és a Tanara
// Cloud gateway (/v1/embeddings) végpontjait.
//
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

struct EmbeddingRequest {
    QStringList texts;
    QString     model;   // üres → a provider configjában lévő modell
};

class EmbeddingJob : public QObject {
    Q_OBJECT
public:
    explicit EmbeddingJob(QObject* parent = nullptr) : QObject(parent) {}
    ~EmbeddingJob() override = default;
    // A futó kérés megszakítása; utána sem finished, sem failed nem jön.
    virtual void cancel() = 0;

    // Sikertelen HTTP-válasz esetén (a failed előtt kitöltve).
    int errorStatus() const { return m_errorStatus; }

signals:
    // A vektorok a kérés szövegeinek sorrendjében (egy szöveg → egy vektor).
    void finished(QVector<QVector<float>> vectors);
    void failed(QString error);

protected:
    int m_errorStatus = 0;
};

class IEmbeddingProvider {
public:
    virtual ~IEmbeddingProvider() = default;
    virtual QString name() const = 0;
    // A job a provider gyereke; a hívó a jelek után törölheti (deleteLater).
    virtual EmbeddingJob* embed(const EmbeddingRequest& req) = 0;
};

} // namespace tanara

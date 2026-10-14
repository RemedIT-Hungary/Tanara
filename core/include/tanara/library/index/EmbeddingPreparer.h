#pragma once
//
// EmbeddingPreparer: "PREPARING THE LIBRARY": embedding every meeting that has a transcript but no
// valid index, one meeting at a time (one request sequence runs).
//
//  - start(): collects the work and embeds it in turn (stateChanged with progress and an
//    estimated remaining time);
//  - cancel(): the running request is cancelled, meetings already done stay (Idle);
//  - resume(): same as start(): it continues where it left off (finished indexes stay valid);
//  - error: the Error state with a message; the queue stops (Continue = resume()).
//  - enqueue(meetingId): background embedding of a meeting transcribed later (if there is a provider).
// lastRun and the model go into <metadataDir>/embedding-state.json.

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <functional>

#include "tanara/providers/embedding/IEmbeddingProvider.h"   // the text-embedding port (old name, TODO(P15))
namespace tanara::providers { class EmbeddingJob; }

namespace tanara::library {

class MeetingRepository;
class EmbeddingIndex;

struct EmbeddingState {
    enum Status { Idle, Running, Error, Done };
    Status    status = Idle;
    QString   provider;      // provider id ("" = no embedding)
    QString   model;
    int       prepared = 0;  // this many meetings have a valid index
    int       total = 0;     // this many meetings have a transcript
    QString   error;         // in the Error state
    QDateTime lastRun;       // end of the last preparation that ran without error
    int       etaSec = -1;   // estimated remaining time (Running); -1 = cannot be estimated yet
};

class EmbeddingPreparer : public QObject {
    Q_OBJECT
public:
    using ProviderFactory = std::function<providers::IEmbeddingProvider*(QObject* parent)>;

    EmbeddingPreparer(MeetingRepository* store, EmbeddingIndex* index, const QString& stateFile,
                      QObject* parent = nullptr);
    ~EmbeddingPreparer() override;

    // Sets the provider (empty id = no embedding: a running preparation stops, Idle).
    // The factory gives a new provider for every run (the caller fills the key / gateway config).
    void setProvider(const QString& providerId, const QString& model, ProviderFactory factory);

    EmbeddingState state() const;
    bool isConfigured() const;

public slots:
    void start();
    void cancel();
    void resume();
    void enqueue(const QString& meetingId);

signals:
    void stateChanged();
    void meetingPrepared(QString meetingId);

private:
    void refreshCounts();
    void next();
    void fail(const QString& error);
    void stopJob();
    void persist();
    QStringList pending() const;

    MeetingRepository*   m_store = nullptr;
    EmbeddingIndex* m_index = nullptr;
    QString         m_stateFile;
    ProviderFactory m_factory;
    EmbeddingState  m_state;
    QObject*        m_providerObj = nullptr;
    providers::IEmbeddingProvider* m_provider = nullptr;
    QPointer<providers::EmbeddingJob> m_job;
    QString         m_current;
    QStringList     m_skip;          // meetings that cannot be embedded in this run (empty transcript)
    qint64          m_runStartMs = 0;
    int             m_runDone = 0;
};

} // namespace tanara::library
Q_DECLARE_METATYPE(tanara::library::EmbeddingState)

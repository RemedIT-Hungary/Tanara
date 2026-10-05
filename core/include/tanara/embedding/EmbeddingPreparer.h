#pragma once
//
// EmbeddingPreparer — „KÖNYVTÁR ELŐKÉSZÍTÉSE”: minden átirattal bíró, de érvényes index
// nélküli megbeszélés beágyazása, egyszerre egy meeting (egy kérés-sorozat) fut.
//
//  - start(): összegyűjti a teendőket, és sorban beágyazza őket (stateChanged a haladással,
//    becsült hátralévő idővel);
//  - cancel(): a futó kérés megszakad, a már kész meetingek megmaradnak (Idle);
//  - resume(): ugyanaz, mint a start() — onnan folytatja, ahol abbahagyta (a kész indexek
//    érvényesek maradnak);
//  - hiba: Error állapot az üzenettel; a sor megáll (Folytatás = resume()).
//  - enqueue(meetingId): egy később átírt meeting háttér-beágyazása (ha van provider).
// A lastRun és a modell a <metadataDir>/embedding-state.json-ba kerül.
//
#include "tanara/Types.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <functional>

namespace tanara {

class MeetingStore;
class EmbeddingIndex;
class IEmbeddingProvider;
class EmbeddingJob;

struct EmbeddingState {
    enum Status { Idle, Running, Error, Done };
    Status    status = Idle;
    QString   provider;      // provider id ("" = nincs beágyazás)
    QString   model;
    int       prepared = 0;  // ennyi meeting indexe érvényes
    int       total = 0;     // ennyi átirattal bíró meeting van
    QString   error;         // Error állapotban
    QDateTime lastRun;       // az utolsó hibátlanul végigfutott előkészítés vége
    int       etaSec = -1;   // becsült hátralévő idő (Running); -1 = még nem becsülhető
};

class EmbeddingPreparer : public QObject {
    Q_OBJECT
public:
    using ProviderFactory = std::function<IEmbeddingProvider*(QObject* parent)>;

    EmbeddingPreparer(MeetingStore* store, EmbeddingIndex* index, const QString& stateFile,
                      QObject* parent = nullptr);
    ~EmbeddingPreparer() override;

    // A provider beállítása (üres id → nincs beágyazás: a futó előkészítés leáll, Idle).
    // A factory minden futáshoz új providert ad (a hívó tölti a kulcsot / gateway-configot).
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

    MeetingStore*   m_store = nullptr;
    EmbeddingIndex* m_index = nullptr;
    QString         m_stateFile;
    ProviderFactory m_factory;
    EmbeddingState  m_state;
    QObject*        m_providerObj = nullptr;
    IEmbeddingProvider* m_provider = nullptr;
    QPointer<EmbeddingJob> m_job;
    QString         m_current;
    QStringList     m_skip;          // ebben a futásban beágyazhatatlan (üres átirat) meetingek
    qint64          m_runStartMs = 0;
    int             m_runDone = 0;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::EmbeddingState)

#include "tanara/embedding/EmbeddingPreparer.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/embedding/EmbeddingIndex.h"
#include "tanara/embedding/IEmbeddingProvider.h"
#include "tanara/store/MeetingStore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <memory>

namespace tanara {

EmbeddingPreparer::EmbeddingPreparer(MeetingStore* store, EmbeddingIndex* index,
                                     const QString& stateFile, QObject* parent)
    : QObject(parent), m_store(store), m_index(index), m_stateFile(stateFile)
{
    QFile f(stateFile);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_state.lastRun = QDateTime::fromString(o.value(QStringLiteral("lastRun")).toString(), Qt::ISODateWithMs);
        m_state.model = o.value(QStringLiteral("model")).toString();
        m_state.provider = o.value(QStringLiteral("provider")).toString();
    }
}

EmbeddingPreparer::~EmbeddingPreparer()
{
    stopJob();
}

void EmbeddingPreparer::setProvider(const QString& providerId, const QString& model, ProviderFactory factory)
{
    const bool changed = providerId != m_state.provider || model != m_state.model;
    stopJob();
    m_factory = std::move(factory);
    if (changed) m_state.lastRun = QDateTime();   // más modell: a korábbi „naprakész” nem érvényes
    m_state.provider = providerId;
    m_state.model = model;
    m_state.status = EmbeddingState::Idle;
    m_state.error.clear();
    m_state.etaSec = -1;
    if (m_index) m_index->setModel(providerId.isEmpty() ? QString() : model);
    if (changed) persist();
    refreshCounts();
    emit stateChanged();
}

EmbeddingState EmbeddingPreparer::state() const { return m_state; }

bool EmbeddingPreparer::isConfigured() const
{
    return !m_state.provider.isEmpty() && !m_state.model.isEmpty() && bool(m_factory);
}

QStringList EmbeddingPreparer::pending() const
{
    QStringList out;
    if (!m_store || !m_index) return out;
    for (const Meeting& m : m_store->loadAll())   // legújabb elöl: a friss megbeszélések előbb
        if (m.hasTranscript && !m_skip.contains(m.id) && !m_index->has(m.id)) out << m.id;
    return out;
}

void EmbeddingPreparer::refreshCounts()
{
    int total = 0, prepared = 0;
    if (m_store && m_index && isConfigured()) {
        for (const Meeting& m : m_store->loadAll()) {
            if (!m.hasTranscript) continue;
            ++total;
            if (m_index->has(m.id)) ++prepared;
        }
    }
    m_state.total = total;
    m_state.prepared = prepared;
}

void EmbeddingPreparer::start()
{
    if (m_state.status == EmbeddingState::Running) return;
    if (!isConfigured()) {
        m_state.status = EmbeddingState::Idle;
        refreshCounts();
        emit stateChanged();
        return;
    }
    m_skip.clear();
    m_state.error.clear();
    m_state.status = EmbeddingState::Running;
    m_state.etaSec = -1;
    m_runStartMs = QDateTime::currentMSecsSinceEpoch();
    m_runDone = 0;
    refreshCounts();
    emit stateChanged();
    next();
}

void EmbeddingPreparer::resume() { start(); }

void EmbeddingPreparer::cancel()
{
    if (m_state.status != EmbeddingState::Running) return;
    stopJob();
    m_state.status = EmbeddingState::Idle;
    m_state.etaSec = -1;
    refreshCounts();
    emit stateChanged();
}

void EmbeddingPreparer::enqueue(const QString& meetingId)
{
    Q_UNUSED(meetingId)
    if (!isConfigured()) return;
    // A futó sor a következő lépésben úgyis megtalálja; különben csendben (hiba-állapotban is)
    // újraindul — a felhasználó közben dolgozhat.
    if (m_state.status != EmbeddingState::Running) start();
}

void EmbeddingPreparer::stopJob()
{
    if (m_job) {
        m_job->disconnect(this);
        m_job->cancel();
        m_job->deleteLater();
    }
    m_job = nullptr;
    if (m_providerObj) m_providerObj->deleteLater();
    m_providerObj = nullptr;
    m_provider = nullptr;
    m_current.clear();
}

void EmbeddingPreparer::next()
{
    if (m_state.status != EmbeddingState::Running) return;
    const QStringList todo = pending();
    if (todo.isEmpty()) {
        stopJob();
        refreshCounts();
        m_state.status = EmbeddingState::Done;
        m_state.etaSec = 0;
        m_state.lastRun = QDateTime::currentDateTime();
        persist();
        emit stateChanged();
        return;
    }
    m_current = todo.first();
    const Meeting m = m_store->load(m_current);
    QVector<EmbeddingChunk> chunks = EmbeddingIndex::chunkTranscript(speakeredit::loadTranscriptLines(m.folder));
    if (chunks.isEmpty()) {
        m_skip << m_current;   // nincs szöveg: ebben a futásban kimarad
        next();
        return;
    }
    if (!m_provider) {
        m_provider = m_factory ? m_factory(this) : nullptr;
        m_providerObj = dynamic_cast<QObject*>(m_provider);
        if (!m_provider) { fail(tr("A beágyazó szolgáltató nem hozható létre.")); return; }
    }
    EmbeddingRequest req;
    req.model = m_state.model;
    for (const EmbeddingChunk& c : std::as_const(chunks)) req.texts << c.text;
    EmbeddingJob* job = m_provider->embed(req);
    m_job = job;
    const QString id = m_current;
    const auto shared = std::make_shared<QVector<EmbeddingChunk>>(std::move(chunks));
    connect(job, &EmbeddingJob::finished, this, [this, job, id, shared](const QVector<QVector<float>>& vectors) {
        job->deleteLater();
        m_job = nullptr;
        if (vectors.size() != shared->size()) { fail(tr("A beágyazási válasz hiányos.")); return; }
        for (int i = 0; i < vectors.size(); ++i) (*shared)[i].vector = vectors.at(i);
        if (!m_index->store(id, m_state.model, *shared)) {
            fail(tr("A beágyazás nem menthető (%1).").arg(EmbeddingIndex::kFileName));
            return;
        }
        ++m_runDone;
        refreshCounts();
        const int left = m_state.total - m_state.prepared;
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_runStartMs;
        m_state.etaSec = m_runDone > 0 ? int((elapsed / m_runDone) * left / 1000) : -1;
        emit meetingPrepared(id);
        emit stateChanged();
        next();
    });
    connect(job, &EmbeddingJob::failed, this, [this, job](const QString& error) {
        job->deleteLater();
        m_job = nullptr;
        fail(error);
    });
}

void EmbeddingPreparer::fail(const QString& error)
{
    stopJob();
    refreshCounts();
    m_state.status = EmbeddingState::Error;
    m_state.error = error;
    m_state.etaSec = -1;
    emit stateChanged();
}

void EmbeddingPreparer::persist()
{
    if (m_stateFile.isEmpty()) return;
    QDir().mkpath(QFileInfo(m_stateFile).absolutePath());
    const QJsonObject o{ { QStringLiteral("provider"), m_state.provider },
                         { QStringLiteral("model"), m_state.model },
                         { QStringLiteral("lastRun"), m_state.lastRun.toString(Qt::ISODateWithMs) } };
    QSaveFile f(m_stateFile);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

} // namespace tanara

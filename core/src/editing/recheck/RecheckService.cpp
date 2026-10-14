#include "tanara/editing/recheck/RecheckService.h"

#include "tanara/editing/recheck/FactoryVoiceEmbedder.h"
#include "tanara/editing/review/SpeakerAnalysis.h"
#include "tanara/editing/session/SpeakerEditSession.h"
#include "tanara/providers/voice/IVoiceEmbedder.h"

#include <QHash>
#include <QLocale>
#include <QPair>
#include <QPointer>
#include <QTimer>

#include <algorithm>

namespace tanara::editing {

using namespace speakeredit;

namespace {

// Lines per embedding job: the progress and the cache grow in steps of this size.
constexpr int kEmbeddingBatch = 16;

} // namespace

struct RecheckService::Private {
    RecheckService* q = nullptr;
    SpeakerEditSession* session = nullptr;     // nullptr after the session's closing()

    providers::IVoiceEmbedder* external = nullptr;
    providers::UtteranceEmbedderFactory factory;
    ModelNameFn modelName;

    // ---- the embedding run ----
    bool running = false;
    bool cancelRequested = false;
    std::unique_ptr<FactoryVoiceEmbedder> ownEmbedder;     // the per-run wrapper of the factory
    providers::IVoiceEmbedder* embedder = nullptr;         // the run's source (external or ownEmbedder)
    QPointer<providers::VoiceEmbeddingJob> job;
    QVector<SpeakerEditSession::EmbeddingTask> tasks;
    int next = 0;                       // the first task not yet submitted
    QString audio;
    int epoch = 0;                      // grows on every stop / reload; an old job's result is dropped
    int done = 0;
    int total = 0;
    QString error;

    bool supported() const { return external != nullptr || bool(factory); }

    // The run's embedder object is released later: we may be inside its job's signal.
    void releaseEmbedder()
    {
        embedder = nullptr;
        if (ownEmbedder) ownEmbedder.release()->deleteLater();
    }

    void dropJob()
    {
        if (!job) return;
        job->disconnect(q);
        job->cancel();
        job->deleteLater();
        job.clear();
    }

    void submitNext()
    {
        // One job per batch: consecutive lines with the same missing models (a request has one model list).
        const QStringList models = tasks.at(next).models;
        int end = next;
        providers::VoiceEmbeddingRequest req;
        req.modelIds = models;
        while (end < tasks.size() && end - next < kEmbeddingBatch && tasks.at(end).models == models) {
            const SpeakerEditSession::EmbeddingTask& t = tasks.at(end);
            req.clips.append(providers::AudioClipRef{audio, t.startMs, t.endMs});
            ++end;
        }
        const int first = next;
        const int count = end - next;
        next = end;
        const int ep = epoch;
        providers::VoiceEmbeddingJob* j = embedder->embed(req);
        job = j;
        QObject::connect(j, &providers::CapabilityJob::finished, q, [this, j, ep, first, count] {
                if (ep != epoch || !running) return;
            onBatch(j, first, count);
        });
        QObject::connect(j, &providers::CapabilityJob::failed, q, [this, ep](const tanara::Error& e) {
            if (ep != epoch || !running) return;
            dropJob();
            finish(false, e.message);
        });
    }

    void onBatch(providers::VoiceEmbeddingJob* j, int first, int count)
    {
        // A run stopped midway (cancelEmbedding with the own embedder) delivers only the lines it got to;
        // the rest stays untried and is computed by the next run.
        int processed = count;
        if (cancelRequested) {
            int last = -1;
            for (const providers::VoiceVector& v : j->result()) last = std::max(last, v.input);
            processed = std::min(count, last + 1);
        }
        QVector<QPair<QString, EmbeddingSet>> batch(processed);
        for (int i = 0; i < processed; ++i) batch[i].first = tasks.at(first + i).utteranceId;
        for (const providers::VoiceVector& v : j->result())
            if (v.input >= 0 && v.input < processed) batch[v.input].second.insert(v.modelId, v.vector);
        job.clear();
        j->deleteLater();
        if (processed > 0) {
            session->addEmbeddings(batch);
            done += processed;
            emit q->embeddingProgress(done, total);
        }
        if (cancelRequested || next >= tasks.size()) {
            finish(!cancelRequested, QString());
            return;
        }
        submitNext();
    }

    // End of a run (completed, cancelled or failed): the cache to disk, the uncertainty refreshed.
    void finish(bool complete, const QString& err)
    {
        if (!running) return;
        running = false;
        cancelRequested = false;
        dropJob();
        releaseEmbedder();
        tasks.clear();
        error = err;
        session->saveEmbeddings();

        const QStringList changed = session->refreshVoiceUncertainty();
        emit q->embeddingRunningChanged(false);
        session->notifyUncertainChanged(changed);
        emit q->embeddingFinished(complete);
        session->refreshReviewGroups();
    }

    // The session reloads / closes: the running job's results would belong to the old state.
    void stop(bool notify)
    {
        ++epoch;
        if (!running) return;
        running = false;
        cancelRequested = false;
        if (job) {
            job->disconnect(q);
            job->cancel();
            delete job.data();      // reaps its worker
        }
        embedder = nullptr;
        ownEmbedder.reset();
        tasks.clear();
        if (session) session->saveEmbeddings();
        if (notify) emit q->embeddingRunningChanged(false);
    }

    PairRecheckAnalysis pairAnalysis(const QString& keyA, const QString& keyB) const
    {
        QHash<QString, int> idx;
        const QVector<AnalysisLine> al = session->analysisLines(&idx, /*groupByPerson*/ false);
        return computePairRecheck(al, idx.value(keyA, -1), idx.value(keyB, -1),
                                  session->priorsFor(al, idx.size()));
    }
};

RecheckService::RecheckService(SpeakerEditSession* session, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->q = this;
    d->session = session;
    if (!session) return;
    connect(session, &SpeakerEditSession::aboutToReload, this, [this](bool newTranscript) {
        d->stop(/*notify*/ true);
        if (newTranscript) d->error.clear();
    });
    connect(session, &SpeakerEditSession::closing, this, [this] {
        d->stop(/*notify*/ false);
        d->session = nullptr;
    });
}

RecheckService::~RecheckService()
{
    d->stop(/*notify*/ false);
}

SpeakerEditSession* RecheckService::session() const { return d->session; }

void RecheckService::setVoiceEmbedder(providers::IVoiceEmbedder* embedder) { d->external = embedder; }
void RecheckService::setEmbedderFactory(providers::UtteranceEmbedderFactory factory) { d->factory = std::move(factory); }
void RecheckService::setModelNameResolver(ModelNameFn fn) { d->modelName = std::move(fn); }

bool RecheckService::embeddingsSupported() const { return d->supported(); }
bool RecheckService::isEmbeddingRunning() const { return d->running; }
QString RecheckService::embeddingError() const { return d->error; }

bool RecheckService::embeddingsComplete() const
{
    return d->session && d->session->hasTranscript() && d->session->pendingEmbeddings().isEmpty();
}

void RecheckService::startEmbedding()
{
    if (d->running) return;
    if (!d->supported() || !d->session || !d->session->hasTranscript()) {
        emit embeddingFinished(false);
        return;
    }
    QVector<SpeakerEditSession::EmbeddingTask> tasks = d->session->pendingEmbeddings();
    if (tasks.isEmpty()) {
        emit embeddingFinished(true);
        return;
    }
    d->tasks = std::move(tasks);
    d->next = 0;
    d->done = 0;
    d->total = int(d->tasks.size());
    d->error.clear();
    d->cancelRequested = false;
    d->audio = d->session->audioPath();
    if (d->external) {
        d->embedder = d->external;
    } else {
        d->ownEmbedder = std::make_unique<FactoryVoiceEmbedder>(d->factory, d->session->voiceModelIds());
        d->embedder = d->ownEmbedder.get();
    }
    d->running = true;
    d->submitNext();    // a job never delivers synchronously, so the signals below come first
    emit embeddingRunningChanged(true);
    emit embeddingProgress(0, d->total);
}

void RecheckService::cancelEmbedding()
{
    if (!d->running || d->cancelRequested) return;
    d->cancelRequested = true;
    if (d->ownEmbedder) {
        // The job in progress stops after its current line and delivers what it has; the run ends then.
        d->ownEmbedder->stopJobs();
        return;
    }
    // A provider's job cannot deliver a partial result: it is dropped (recomputed by the next run) and the
    // run ends in the next turn.
    d->dropJob();
    const int ep = d->epoch;
    QTimer::singleShot(0, this, [this, ep] {
        if (ep == d->epoch) d->finish(false, QString());
    });
}

// ---- recheck ---------------------------------------------------------

bool RecheckService::canRecheck() const
{
    return d->session && d->session->hasTranscript() && !d->running && d->session->hasTrustedCore();
}

QString RecheckService::recheckBlocker() const
{
    const SpeakerEditSession* s = d->session;
    if (!s || !s->hasTranscript())
        return tr("Ennek a megbeszélésnek nincs szerkeszthető átirata.");
    if (!s->hasEmbeddings() && !d->supported())
        return tr("Az újraellenőrzéshez nincs telepítve a hangmodell.");
    if (d->running)
        return tr("A sorok hang-elemzése még fut — a végén újraellenőrizheted a sorokat.");
    if (!s->hasEmbeddings())
        return tr("A sorok hang-elemzése még nem készült el. Nyisd meg az Átirat fület, és várd meg a végét.");
    if (!s->hasTrustedCore())
        return tr("Előbb erősíts meg vagy javíts legalább %1 sort egy beszélőnél („Jó így” vagy "
                  "áthelyezés) — ezek hangjához mérem a többit.").arg(kMinSpeakerLines);
    return {};
}

RecheckService::RecheckResult RecheckService::recheckFromConfirmed()
{
    RecheckResult r;
    if (!canRecheck()) return r;
    SpeakerEditSession* s = d->session;
    const int n = s->utteranceCount();
    QHash<QString, int> idx;
    const QVector<AnalysisLine> al = s->analysisLines(&idx, /*groupByPerson*/ true);
    const RecheckAnalysis a = computeUncertainRechecked(al, idx.size(), s->priorsFor(al, idx.size()));

    // Group index -> speaker key (the speaker carrying the group's most lines; the same person with several
    // keys is one group).
    QVector<QHash<QString, int>> keyFreq(idx.size());
    for (int i = 0; i < n; ++i) ++keyFreq[al[i].speaker][s->assignedKey(i)];
    QVector<QString> groupKey(idx.size());
    for (int g = 0; g < idx.size(); ++g) {
        int best = -1;
        for (auto it = keyFreq[g].constBegin(); it != keyFreq[g].constEnd(); ++it)
            if (it.value() > best) { best = it.value(); groupKey[g] = it.key(); }
    }

    r.ran = true;
    r.flagged = a.flagged();
    r.speakersWithConfirmedCore = a.coreSpeakers();
    r.confirmedLines = a.coreLineTotal();
    for (int g = 0; g < idx.size(); ++g) {
        const ReferenceInfo& info = a.refs.value(g);
        if (info.refLocal() || info.refPrior()) r.references.append(s->referenceOf(groupKey[g], info));
    }
    // For feedback: the stored samples that joined, the models in use and the very similar voices.
    for (const SpeakerReference& ref : std::as_const(r.references)) {
        r.localPrints += ref.localPrints;
        r.priorPrints += ref.priorPrints;
    }
    for (const QString& id : s->voiceModelIds()) {
        const QString name = d->modelName ? d->modelName(id) : QString();
        r.models << (name.isEmpty() ? id : name);
    }
    r.similar = s->similarVoices();

    QVector<SpeakerEditSession::RecheckMark> marks;
    marks.reserve(n);
    for (int i = 0; i < n; ++i) {
        const RecheckVerdict& v = a.lines[i];
        SpeakerEditSession::RecheckMark m;
        m.index = i;
        m.flagged = v.uncertain;
        if (v.uncertain) m.hint = v.otherSpeaker >= 0 ? groupKey.value(v.otherSpeaker) : QString();
        marks.append(m);
    }
    s->applyRecheckMarks(marks, [] { return tr("Beszélők újraellenőrzése"); });
    emit recheckFinished(r.flagged, r.speakersWithConfirmedCore, r.confirmedLines);
    return r;
}

QString RecheckService::referenceSummary(const QVector<SpeakerReference>& refs)
{
    const bool anyPrint = std::any_of(refs.cbegin(), refs.cend(),
                                      [](const SpeakerReference& r) { return r.usesPrints(); });
    if (!anyPrint) return {};
    QStringList parts;
    for (const SpeakerReference& r : refs) {
        if (r.fallback) continue;   // the "few confirmed lines" message speaks about it separately
        QStringList what;
        if (r.lines > 0) what << tr("%1 sor").arg(r.lines);
        if (r.localPrints > 0) what << tr("%1 itteni lenyomat").arg(r.localPrints);
        if (r.priorPrints > 0) what << tr("%1 korábbi lenyomat").arg(r.priorPrints);
        if (what.isEmpty()) continue;
        parts << QStringLiteral("%1 %2").arg(r.name, what.join(QStringLiteral(" + ")));
    }
    if (parts.isEmpty()) return {};
    return tr("Referencia: %1.").arg(parts.join(QStringLiteral(", ")));
}

QString RecheckService::recheckBasisSummary(const RecheckResult& r)
{
    if (!r.ran) return {};
    QStringList parts;
    parts << tr("%n megerősített sor", nullptr, r.confirmedLines);
    if (r.storedPrints() > 0) {
        QString prints = tr("%n tárolt minta", nullptr, r.storedPrints());
        if (r.localPrints > 0 && r.priorPrints > 0)
            prints += QLatin1Char(' ') + tr("(ebből %1 erről a megbeszélésről)").arg(r.localPrints);
        else if (r.localPrints > 0)
            prints += QLatin1Char(' ') + tr("(mind erről a megbeszélésről)");
        else
            prints += QLatin1Char(' ') + tr("(korábbi megbeszélésekből)");
        parts << prints;
    } else {
        parts << tr("tárolt minta nélkül");
    }
    if (!r.models.isEmpty())
        parts << (r.models.size() == 1 ? tr("modell: %1").arg(r.models.first())
                                       : tr("modellek: %1").arg(r.models.join(QStringLiteral(", "))));
    return tr("Alapja: %1.").arg(parts.join(QStringLiteral(" · ")));
}

QString RecheckService::similarVoicesNote(const QVector<SimilarVoices>& similar)
{
    if (similar.isEmpty()) return {};
    const SimilarVoices& v = similar.first();
    const QString value = QLocale(QLocale::Hungarian).toString(v.similarity, 'f', 2);
    QString text = tr("Figyelem: %1 és %2 hangja nagyon hasonló (%3), kettejük sorait érdemes átnézni.")
                       .arg(v.nameA, v.nameB, value);
    if (similar.size() > 1)
        text += QLatin1Char(' ') + tr("(További %n hasonló pár.)", nullptr, int(similar.size()) - 1);
    return text;
}

// ---- pairwise review -----------------------------------------------------

QString RecheckService::pairRecheckBlocker(const QString& speakerKeyA, const QString& speakerKeyB) const
{
    const SpeakerEditSession* s = d->session;
    if (!s || !s->hasTranscript())
        return tr("Ennek a megbeszélésnek nincs szerkeszthető átirata.");
    if (speakerKeyA.isEmpty() || speakerKeyB.isEmpty() || speakerKeyA == speakerKeyB
        || !s->isVisible(speakerKeyA) || !s->isVisible(speakerKeyB))
        return tr("Válassz két különböző beszélőt ebből a megbeszélésből.");
    if (!s->hasEmbeddings() && !d->supported())
        return tr("Az átnézéshez nincs telepítve a hangmodell.");
    if (d->running)
        return tr("A sorok hang-elemzése még fut — a végén átnézheted a két beszélőt.");
    if (!s->hasEmbeddings())
        return tr("A sorok hang-elemzése még nem készült el. Nyisd meg az Átirat fület, és várd meg a végét.");
    const PairRecheckAnalysis a = d->pairAnalysis(speakerKeyA, speakerKeyB);
    if (!a.valid) {
        const QString who = a.refLinesA < kMinSpeakerLines ? s->speakerName(speakerKeyA) : s->speakerName(speakerKeyB);
        return tr("%1 hangjához kevés a minta: legalább %2 elég hosszú, tiszta sora kell.")
            .arg(who).arg(kMinSpeakerLines);
    }
    return {};
}

RecheckService::PairRecheckResult RecheckService::recheckPair(const QString& speakerKeyA,
                                                              const QString& speakerKeyB)
{
    PairRecheckResult r;
    r.blocker = pairRecheckBlocker(speakerKeyA, speakerKeyB);
    if (!r.blocker.isEmpty()) return r;
    SpeakerEditSession* s = d->session;
    QHash<QString, int> idx;
    const QVector<AnalysisLine> al = s->analysisLines(&idx, /*groupByPerson*/ false);
    const int ia = idx.value(speakerKeyA, -1);
    const PairRecheckAnalysis a = computePairRecheck(al, ia, idx.value(speakerKeyB, -1),
                                                     s->priorsFor(al, idx.size()));
    r.ran = true;
    r.flagged = a.flagged();
    r.refLinesA = a.refLinesA;
    r.refLinesB = a.refLinesB;
    r.fallbackA = a.fallbackA;
    r.fallbackB = a.fallbackB;
    r.centroidSimilarity = a.centroidSimilarity;
    r.refA = s->referenceOf(speakerKeyA, a.refA);
    r.refB = s->referenceOf(speakerKeyB, a.refB);

    QVector<SpeakerEditSession::RecheckMark> marks;
    const int n = s->utteranceCount();
    for (int i = 0; i < n; ++i) {
        // Only A's and B's lines: the earlier flags of other speakers are untouched.
        const QString key = s->assignedKey(i);
        if (key != speakerKeyA && key != speakerKeyB) continue;
        const PairVerdict& v = a.lines[i];
        SpeakerEditSession::RecheckMark m;
        m.index = i;
        m.flagged = v.flagged;
        if (v.flagged) m.hint = v.hintedSpeaker == ia ? speakerKeyA : speakerKeyB;
        marks.append(m);
    }
    s->applyRecheckMarks(marks, [s, speakerKeyA, speakerKeyB] {
        return tr("Átnézés: %1 és %2").arg(s->speakerName(speakerKeyA), s->speakerName(speakerKeyB));
    });
    return r;
}

} // namespace tanara::editing

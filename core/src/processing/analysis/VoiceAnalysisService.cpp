#include "tanara/processing/analysis/VoiceAnalysisService.h"

#include "tanara/foundation/log/Logging.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/processing/analysis/LocalParticipantIdentifier.h"
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/processing/voiceid/EmbeddingSet.h"
#include "tanara/processing/voiceid/VoiceEmbedder.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace tanara::processing {

namespace {

using library::MeetingLayout;
using library::TranscriptRepository;

// Which track to take audio from for a raw speaker label:
//  - an exact match with a track's fixed speaker (mic) -> that track;
//  - otherwise (a diarized "Remote N") -> the loopback track;
//  - last fallback: the first track.
const Track* resolveTrackForLabel(const Meeting& m, const MergedTranscript& mt, const QString& rawLabel)
{
    // 1) Primarily by the trackId of the label's tokens (diarized mic/loopback speakers, "Microphone N" /
    //    "Remote N", thus resolve to the RIGHT track).
    QString tid;
    for (const TranscriptToken& t : mt.tokens)
        if (t.speaker == rawLabel && !t.trackId.isEmpty()) { tid = t.trackId; break; }
    if (!tid.isEmpty())
        for (const Track& t : m.tracks)
            if (t.id == tid) return &t;
    // 2) Fallback: an exact track speaker name match (the fixed mic name).
    for (const Track& t : m.tracks)
        if (t.speakerLabel == rawLabel) return &t;
    // 3) Fallback: the first loopback track.
    for (const Track& t : m.tracks)
        if (t.kind == TrackKind::Loopback) return &t;
    return m.tracks.isEmpty() ? nullptr : &m.tracks.first();
}

// A speaker's representative embedding (with every model in use): from their longest utterances it
// collects ~3-12 s of audio from the given track, and computes one vector per model from ONE concatenated
// PCM. Empty = not enough audio / an error. The utterances are in meeting time; offsetMs is the audio
// file's start in the meeting (Track::startOffsetMs for a track file, 0 for the mixdown); the part before
// the file is dropped. refs (optional): the spans used in "fileRel#start-end" form (meeting time), the
// voiceprint's sourceRefs.
EmbeddingSet embeddingForLabel(const media::MediaTool& media, const VoiceEmbedderSet& set,
                               const QString& audioPath, qint64 offsetMs,
                               const MergedTranscript& mt, const QString& rawLabel,
                               const QString& fileRel = QString(), QStringList* refs = nullptr)
{
    QVector<Utterance> utts;
    for (const Utterance& u : mt.segments())
        if (u.speaker == rawLabel) utts.append(u);
    std::sort(utts.begin(), utts.end(), [](const Utterance& a, const Utterance& b) {
        return (a.endMs - a.startMs) > (b.endMs - b.startMs);
    });
    const QString& path = audioPath;
    QVector<float> pcm;
    qint64 accMs = 0;
    for (const Utterance& u : utts) {
        if (accMs >= 3000 || pcm.size() > 16000 * 12) break;
        const tanara::tracktiming::FileRange r = tanara::tracktiming::meetingToFileRange(offsetMs, u.startMs, u.endMs);
        if (!r.valid()) continue;   // the track was not sounding yet at this time
        const QVector<float> part = VoiceEmbedder::decodePcm16kMono(media, path, r.startMs, r.endMs);
        if (part.isEmpty()) continue;
        pcm += part;
        accMs += (r.endMs - r.startMs);
        if (refs)
            *refs << QStringLiteral("%1#%2-%3").arg(fileRel).arg(r.startMs + offsetMs).arg(r.endMs + offsetMs);
    }
    if (pcm.isEmpty()) return {};
    return set.embedPcm(pcm);
}

// Parsing a sample reference ("file#startMs-endMs", meeting time); false if there is no range.
bool parseRangeRef(const QString& ref, QString* file, qint64* startMs, qint64* endMs)
{
    const int hash = ref.lastIndexOf(QLatin1Char('#'));
    if (hash <= 0) return false;
    *file = ref.left(hash);
    const QString range = ref.mid(hash + 1);
    const int dash = range.indexOf(QLatin1Char('-'));
    if (dash <= 0) return false;
    bool ok1 = false, ok2 = false;
    *startMs = range.left(dash).toLongLong(&ok1);
    *endMs = range.mid(dash + 1).toLongLong(&ok2);
    return ok1 && ok2 && *endMs > *startMs;
}

// One voiceprint with ONE model, recomputed from its audio (lazy backfill). It concatenates the PCM of
// the spans (sourceRefs, failing that the sampleRef) and embeds it as a whole, the same way
// enrollSpeaker's voiceprint is made (embeddingForLabel); this differs slightly from the editor's
// weighted per-line average, but is self-consistent per model. The spans are in meeting time: for a track
// file the track's offset is subtracted. missingAudio: the referenced file is missing.
QVector<float> embedPrintSource(const media::MediaTool& media, const VoiceEmbedderSet& set,
                                const QString& modelId, const Meeting& m,
                                const Voiceprint& vp, bool* missingAudio)
{
    *missingAudio = false;
    const QStringList refs = vp.sourceRefs.isEmpty() ? QStringList{vp.sampleRef} : vp.sourceRefs;
    QVector<float> pcm;
    for (const QString& ref : refs) {
        QString file;
        qint64 s = 0, e = 0;
        if (!parseRangeRef(ref, &file, &s, &e)) continue;
        const QString abs = QDir(m.folder).filePath(file);
        if (!QFileInfo::exists(abs)) { *missingAudio = true; continue; }
        qint64 offset = 0;
        for (const Track& t : m.tracks)
            if (t.file == file) { offset = t.startOffsetMs; break; }
        const tanara::tracktiming::FileRange r = tanara::tracktiming::meetingToFileRange(offset, s, e);
        if (!r.valid()) continue;
        pcm += VoiceEmbedder::decodePcm16kMono(media, abs, r.startMs, r.endMs);
    }
    if (pcm.isEmpty()) return {};
    return set.embedPcmWith(pcm, {modelId}).value(modelId);
}

// Voiceprints from one sample: one per model (same sampleRef / source / createdAt / sourceRefs, own id
// and model). The template's embedding, id and model fields do not count.
QVector<Voiceprint> printsFromSet(const EmbeddingSet& set, const Voiceprint& tmpl)
{
    QVector<Voiceprint> out;
    for (auto it = set.cbegin(); it != set.cend(); ++it) {
        if (it.value().isEmpty()) continue;
        Voiceprint vp = tmpl;
        vp.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        vp.model = it.key();
        vp.embedding = it.value();
        vp.dim = it.value().size();
        out.append(vp);
    }
    return out;
}

// The reference of the playable representative segment stored with the voiceprint:
// "track_file#startMs-endMs" (the longest utterance). The times are in MEETING time (as in the
// transcript); for a track file the player converts to file time with the track's offset (PeopleService).
// For playback the People panel resolves the folder from sourceMeetingId.
QString representativeSampleRef(const MergedTranscript& mt, const QString& rawLabel, const QString& fileRel)
{
    qint64 bestS = -1, bestE = -1, bestDur = -1;
    for (const Utterance& u : mt.segments())
        if (u.speaker == rawLabel && (u.endMs - u.startMs) > bestDur) {
            bestDur = u.endMs - u.startMs; bestS = u.startMs; bestE = u.endMs;
        }
    if (bestS < 0)
        return fileRel;
    return QStringLiteral("%1#%2-%3").arg(fileRel).arg(bestS).arg(bestE);
}

// The voice-ID audio source: NOW the mixdown (the transcript is made from it too, so the diarized
// "Speaker N" labels' time windows fit the mix directly). If there is no mixdown (an old, per-track
// meeting), for back-compat we fall back to the label's resolved track.
struct VoiceSource { QString absPath; QString fileRel; QString trackId; QString device;
                     qint64 offsetMs = 0; };   // the file's start in the meeting
VoiceSource resolveVoiceSource(const Meeting& m, const MergedTranscript& mt, const QString& rawLabel)
{
    const QString mixRel = MeetingLayout::mixdownOf(m);
    const QString mixAbs = QDir(m.folder).filePath(mixRel);
    if (QFile::exists(mixAbs))
        return { mixAbs, mixRel, QStringLiteral("mixdown"), QString() };
    if (const Track* t = resolveTrackForLabel(m, mt, rawLabel))
        return { QDir(m.folder).filePath(t->file), t->file, t->id, t->deviceName, t->startOffsetMs };
    return {};
}

// ---- matching through the IParticipantIdentifier port's local rule ----------------------------------

// The voiceprint store's people as the identification candidates (in the store's order).
QVector<providers::IdentifyCandidate> candidatesOf(const library::VoiceprintStore& vp,
                                                   const library::PeopleStore* people)
{
    QVector<providers::IdentifyCandidate> out;
    for (const PersonId& person : vp.people())
        out.append({people ? people->displayName(person) : QString(), vp.printsFor(person), person});
    return out;
}

// One probe from a per-model embedding (the vectors in the normalized model order, as the store matches).
providers::IdentifyProbe probeOf(const EmbeddingSet& set, const QStringList& modelIds)
{
    providers::IdentifyProbe probe;
    for (const QString& id : voicemodels::normalizeIds(modelIds)) {
        const QVector<float> v = set.value(id);
        if (v.isEmpty()) continue;
        probe.vectors.append({v, int(v.size()), id, 0});
    }
    return probe;
}

} // namespace

struct VoiceAnalysisService::Impl {
    VoiceAnalysisService* q = nullptr;
    Deps deps;

    // The speaker models in use (enabled intersect available): the main thread's set, loads lazily.
    VoiceEmbedderSet voiceSet;
    QStringList      voiceModelIds;     // ids of voiceSet (alphabetical)
    PcmEmbedderLoader voiceLoader;      // test seam; empty = the real VoiceEmbedder
    bool             voiceModelsApplied = false;

    // Lazy voiceprint backfill: a 1-thread pool, one run at a time.
    QThreadPool      backfillPool;
    bool             backfillRunning = false;
    bool             backfillAgain = false;
    std::shared_ptr<std::atomic<bool>> backfillCancel = std::make_shared<std::atomic<bool>>(false);
    QSet<QString>    backfillLogged;    // voiceprints already logged because of missing audio
    int              backfillAdded = 0;
    int              backfillDone = 0;
    int              backfillTotal = 0;
    QVector<VoiceprintBackfillSkip> backfillSkipped;   // samples skipped by the latest backfill
    // A voiceprint to backfill: the sibling group's representative + the missing model + the source meeting.
    struct BackfillJob { PersonId person; QString model; Voiceprint print; Meeting meeting; };

    // Per-track activity and pre-transcript voice analysis (background thread; the GUI thread does not wait).
    QThreadPool      analysisPool;
    QHash<QString, MeetingActivity> activityView;   // activity for the speech strip (every track), with a fingerprint
    QSet<QString>    activityViewRunning;           // meetingIds being computed now
    QHash<QString, std::shared_ptr<std::atomic<bool>>> analysisRuns;   // meetingId -> cancel flag
    std::shared_ptr<std::atomic<bool>> analysisShutdown = std::make_shared<std::atomic<bool>>(false);
    std::function<QStringList(const PersonId&)> personTags;  // test seam (a person's tags)

    // A running (background-thread) identification.
    struct IdentifyRun { QThread* thread = nullptr; std::shared_ptr<std::atomic<bool>> cancel; bool asStage = false; };
    QHash<QString, IdentifyRun> identifyRuns;
    bool shutDown = false;

    bool usable() const
    {
        if (voiceSet.isEmpty()) return false;
#ifdef TANARA_HAVE_VOICEID
        return true;
#else
        return bool(voiceLoader);   // the build has no voice ID; only with the test loader
#endif
    }

    // The best match of a voice (the store's rule: no threshold; {null, -1} without a candidate).
    VoiceMatch bestMatch(const EmbeddingSet& set, const QStringList& modelIds) const
    {
        VoiceMatch none;
        if (!deps.voiceprints) return none;
        const providers::IdentifyProbe probe = probeOf(set, modelIds);
        double best = -1.0;
        PersonId who;
        for (const providers::IdentifyCandidate& c : candidatesOf(*deps.voiceprints, deps.people)) {
            const double s = LocalParticipantIdentifier::score(probe, c);
            if (s > best) { best = s; who = c.personId; }
        }
        if (who.isNull()) return none;
        return VoiceMatch{who, best};
    }

    // THE identification rule in ONE place (the synchronous and the asynchronous path): the speaker's
    // embedding against the voiceprint DB through the identifier port; above the threshold, and only for a
    // known person, it is a match.
    bool matchSpeaker(const QString& rawLabel, const EmbeddingSet& embedding, SpeakerVoiceMatch* out) const
    {
        if (!deps.voiceprints || embedding.isEmpty()) return false;
        providers::IdentifyRequest req;
        req.probes = {probeOf(embedding, embedding.keys())};
        req.candidates = candidatesOf(*deps.voiceprints, deps.people);
        req.threshold = kVoiceMatchThreshold;
        const QVector<ParticipantGuess> guesses = LocalParticipantIdentifier::identifyNow(req);
        if (guesses.isEmpty() || guesses.first().personId.isNull()) return false;
        const ParticipantGuess& g = guesses.first();
        if (deps.people && !deps.people->contains(g.personId)) return false;   // a voiceprint of an unknown person
        *out = SpeakerVoiceMatch{rawLabel, g.personId, g.score};
        return true;
    }

    // The matches of the embeddings computed on a background thread (main thread): only the still anonymous
    // labels, above the threshold.
    QVector<SpeakerVoiceMatch> matchEmbeddings(const QString& meetingId,
                                               const QVector<QPair<QString, EmbeddingSet>>& embeddings) const
    {
        QVector<SpeakerVoiceMatch> out;
        if (embeddings.isEmpty() || !deps.voiceprints) return out;
        const Meeting m = deps.store->load(meetingId);
        if (m.id.isEmpty()) return out;
        for (const auto& pair : embeddings) {
            if (pair.second.isEmpty() || m.speakerMap.contains(pair.first)) continue;
            SpeakerVoiceMatch match;
            if (matchSpeaker(pair.first, pair.second, &match)) out.append(match);
        }
        return out;
    }

    void noteBackfillSkip(const PersonId& person, const QString& model, const Voiceprint& vp,
                          VoiceprintBackfillSkip::Reason reason)
    {
        for (const VoiceprintBackfillSkip& s : std::as_const(backfillSkipped))
            if (s.personId == person && s.model == model && s.sampleRef == vp.sampleRef
                && s.sourceMeetingId == vp.sourceMeetingId)
                return;
        const QString name = deps.people ? deps.people->displayName(person) : QString();
        backfillSkipped.append({person, name, model, vp.sampleRef, vp.sourceMeetingId, reason});
    }

    void finishBackfillJob(const BackfillJob& job, const QVector<float>& vec, bool missingAudio);
    void finishBackfill();
    void finishParticipantAnalysis(const QString& meetingId, ClusterSet clusters, const QString& tracksKey);
    void finishTrackActivity(const QString& meetingId, const MeetingActivity& activity)
    {
        activityViewRunning.remove(meetingId);
        // An empty result also goes in with its fingerprint: we do not recompute until the tracks change.
        activityView.insert(meetingId, activity);
        emit q->trackActivityReady(meetingId);
    }
};

VoiceAnalysisService::VoiceAnalysisService(const Deps& deps, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    d->deps = deps;
    d->backfillPool.setMaxThreadCount(1);
    d->backfillPool.setObjectName(QStringLiteral("tanara-voiceprint-backfill"));
    d->analysisPool.setMaxThreadCount(2);
    d->analysisPool.setObjectName(QStringLiteral("tanara-participant-analysis"));
}

VoiceAnalysisService::~VoiceAnalysisService()
{
    shutdown();
}

void VoiceAnalysisService::shutdown()
{
    if (d->shutDown) return;
    d->shutDown = true;
    // The voiceprint backfill: a queued task is skipped, the running one exits at the next voiceprint.
    d->backfillCancel->store(true);
    d->backfillPool.clear();
    d->backfillPool.waitForDone();
    // The track measurement / voice analysis: the queued ones are skipped, the running one exits at the next window.
    d->analysisShutdown->store(true);
    for (const auto& c : std::as_const(d->analysisRuns)) c->store(true);
    d->analysisPool.clear();
    d->analysisPool.waitForDone();
    // Stopping and awaiting the background-thread identifications (the thread no longer hands over a result
    // referring to d: the QPointer is null by then).
    for (auto it = d->identifyRuns.begin(); it != d->identifyRuns.end(); ++it) {
        it->cancel->store(true);
        if (it->thread) {
            it->thread->disconnect(this);
            it->thread->wait();
            delete it->thread;
        }
    }
    d->identifyRuns.clear();
}

// ---- the speaker models in use ---------------------------------------------------------------------

VoiceAnalysisService::ModelChange VoiceAnalysisService::applyModels()
{
    ModelChange change;
    const QStringList enabled = d->deps.enabledModels ? d->deps.enabledModels() : QStringList();
    VoiceEmbedderSet set = VoiceEmbedderSet::fromSettings(enabled, d->deps.modelsDir, d->deps.appDir, d->voiceLoader);
    const QStringList ids = set.modelIds();
    if (d->voiceModelsApplied && ids == d->voiceModelIds) return change;   // another setting changed
    for (const QString& id : ids)
        if (!d->voiceModelIds.contains(id)) change.grew = true;
    change.first = !d->voiceModelsApplied;
    change.changed = true;
    d->voiceModelsApplied = true;
    d->voiceSet = std::move(set);
    d->voiceModelIds = ids;
    for (const QString& id : enabled)
        if (!ids.contains(id))
            qCInfo(lcVoice).noquote() << "Hangmodell bekapcsolva, de nem használható (ismeretlen id vagy"
                                         " hiányzó fájl):" << id;
    qCInfo(lcVoice).noquote() << "Használt hangmodellek:" << (ids.isEmpty() ? QStringLiteral("-") : ids.join(QStringLiteral(", ")));
    return change;
}

void VoiceAnalysisService::setLoader(PcmEmbedderLoader loader)
{
    d->voiceLoader = std::move(loader);
    d->voiceModelsApplied = false;   // the set is rebuilt together with the loader (without backfill)
    d->voiceModelIds.clear();
}

QStringList VoiceAnalysisService::modelIds() const { return d->voiceModelIds; }
bool VoiceAnalysisService::usable() const { return d->usable(); }
VoiceEmbedderSet VoiceAnalysisService::embedderSet() const { return d->voiceSet.freshCopy(); }

UtteranceEmbedderFactory VoiceAnalysisService::utteranceEmbedderFactory() const
{
    if (!d->usable()) return {};
    return voiceUtteranceEmbedderFactory(d->voiceSet, d->deps.media);
}

QStringList VoiceAnalysisService::editorModelIds() const
{
    if (!d->voiceModelIds.isEmpty()) return d->voiceModelIds;
    return d->deps.enabledModels ? d->deps.enabledModels() : QStringList();
}

QString VoiceAnalysisService::defaultModelPath() const
{
    return VoiceModelRegistry::resolveDefaultModelPath(d->deps.modelsDir, d->deps.appDir);
}

// ---- the candidates' context ------------------------------------------------------------------------

participants::CandidateContext VoiceAnalysisService::candidateContext() const
{
    participants::CandidateContext ctx;
    ctx.self = d->deps.self ? d->deps.self() : SelfPerson();
    if (d->deps.people) {
        d->deps.people->refresh();
        ctx.names = d->deps.people->names();
    }
    library::VoiceprintStore* vp = d->deps.voiceprints;
    ctx.rank = [vp](const EmbeddingSet& set) {
        return vp ? vp->rankedMatches(set, set.keys()) : QVector<VoiceMatch>();
    };
    ctx.hasVoiceprint = [vp](const PersonId& person) { return vp && vp->printCount(person) > 0; };
    library::TagService* ts = d->deps.tags;
    // The tag evidence (UX plan rule 4): the TagService snapshot, manual tag and learned association; the
    // configured seam (tests) overrides the manual tags.
    ctx.tagEvidence = [ts, override = d->personTags](const PersonId& person, const QStringList& meetingTags,
                                                     const QString& meetingId) {
        if (!ts) return library::TagEvidence();
        library::PersonTagSnapshot snap = ts->personTagSnapshot();
        if (override) snap.manual.insert(person, override(person));
        return snap.evidence(person, meetingTags, meetingId);
    };
    return ctx;
}

void VoiceAnalysisService::setPersonTagsProvider(std::function<QStringList(const PersonId&)> provider)
{
    d->personTags = std::move(provider);
}

std::function<QStringList(const PersonId&)> VoiceAnalysisService::personTagsProvider() const { return d->personTags; }

// ---- voiceprint backfill ------------------------------------------------------------------------------

bool VoiceAnalysisService::backfillRunning() const { return d->backfillRunning; }

void VoiceAnalysisService::backfill()
{
    library::VoiceprintStore* store = d->deps.voiceprints;
    if (!store) return;
    if (d->backfillRunning) { d->backfillAgain = true; return; }
    if (!d->usable()) { emit backfillFinished(0, 0); return; }
    d->backfillSkipped.clear();   // a new run = a new report

    // The task list is made on the main thread (only the main thread reaches the DB); the background thread
    // only decodes and embeds. We load each meeting once.
    store->refresh();
    QVector<Impl::BackfillJob> jobs;
    QHash<QString, Meeting> meetings;
    for (const PersonId& person : store->people()) {
        for (const QString& model : std::as_const(d->voiceModelIds)) {
            // A sibling-exact list (printsMissingModel works at sampleRef level: it would mix up the same span of two
            // meetings' identically named mixes).
            for (const Voiceprint& vp : store->samplesMissingModel(person, model)) {
                auto it = meetings.constFind(vp.sourceMeetingId);
                if (it == meetings.constEnd())
                    it = meetings.insert(vp.sourceMeetingId,
                                         vp.sourceMeetingId.isEmpty() ? Meeting() : d->deps.store->load(vp.sourceMeetingId));
                if (it->id.isEmpty()) {
                    d->noteBackfillSkip(person, model, vp, VoiceprintBackfillSkip::Reason::MissingMeeting);
                    const QString key = vp.id + QLatin1Char('|') + model;
                    if (!d->backfillLogged.contains(key)) {
                        d->backfillLogged.insert(key);
                        qCInfo(lcVoice).noquote() << "Lenyomat-pótlás: a forrás-megbeszélés nincs meg, kihagyva:"
                                                  << person.toString() << vp.sampleRef << model;
                    }
                    continue;
                }
                jobs.append({person, model, vp, it.value()});
            }
        }
    }
    if (jobs.isEmpty()) { emit backfillFinished(0, int(d->backfillSkipped.size())); return; }

    d->backfillRunning = true;
    d->backfillAdded = 0;
    d->backfillDone = 0;                       // k / N progress
    d->backfillTotal = int(jobs.size());
    emit backfillStarted(d->backfillTotal);
    qCInfo(lcVoice).noquote() << "Lenyomat-pótlás indul:" << jobs.size() << "lenyomat";
    const VoiceEmbedderSet proto = d->voiceSet.freshCopy();
    const auto cancel = d->backfillCancel;
    QPointer<VoiceAnalysisService> self(this);
    const media::MediaToolPtr media = d->deps.media;
    d->backfillPool.start([self, jobs, proto, cancel, media]() {
        const VoiceEmbedderSet emb = proto.freshCopy();   // the thread's own model instances
        for (const Impl::BackfillJob& job : jobs) {
            if (cancel->load()) break;
            bool missing = false;
            const QVector<float> vec = embedPrintSource(*media, emb, job.model, job.meeting, job.print, &missing);
            QMetaObject::invokeMethod(qApp, [self, job, vec, missing]() {
                if (self) self->d->finishBackfillJob(job, vec, missing);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(qApp, [self]() {
            if (self) self->d->finishBackfill();
        }, Qt::QueuedConnection);
    });
}

// One backfilled vector into the DB (main thread). The DB may have changed meanwhile (a deleted / moved
// sample, another process backfilled): it goes in only if the model is still missing from the group.
void VoiceAnalysisService::Impl::finishBackfillJob(const BackfillJob& job, const QVector<float>& vec,
                                                   bool missingAudio)
{
    const QString key = job.print.id + QLatin1Char('|') + job.model;
    ++backfillDone;
    emit q->backfillProgress(backfillDone, backfillTotal);
    if (vec.isEmpty()) {
        noteBackfillSkip(job.person, job.model, job.print,
                         missingAudio ? VoiceprintBackfillSkip::Reason::MissingAudio
                                      : VoiceprintBackfillSkip::Reason::Failed);
        if (!backfillLogged.contains(key)) {
            backfillLogged.insert(key);
            qCInfo(lcVoice).noquote() << (missingAudio ? "Lenyomat-pótlás: hiányzó hang, kihagyva:"
                                                       : "Lenyomat-pótlás: nem számolható, kihagyva:")
                                      << job.person.toString() << job.print.sampleRef << job.model;
        }
        return;
    }
    library::VoiceprintStore* voiceprints = deps.voiceprints;
    voiceprints->refresh();
    const QString sib = library::VoiceprintStore::siblingKey(job.print);
    bool stillMissing = false;
    for (const Voiceprint& p : voiceprints->samplesMissingModel(job.person, job.model))
        if (library::VoiceprintStore::siblingKey(p) == sib) { stillMissing = true; break; }
    if (!stillMissing) return;
    Voiceprint vp = job.print;
    vp.id.clear();   // addPrint generates one
    vp.model = job.model;
    vp.embedding = vec;
    vp.dim = vec.size();
    voiceprints->addPrint(job.person, vp);
    ++backfillAdded;
}

void VoiceAnalysisService::Impl::finishBackfill()
{
    backfillRunning = false;
    const int added = backfillAdded;
    backfillAdded = 0;
    if (added > 0) {
        qCInfo(lcVoice).noquote() << "Lenyomat-pótlás kész:" << added << "új lenyomat";
        emit q->voiceprintsChanged();
    }
    emit q->backfillFinished(added, int(backfillSkipped.size()));
    if (backfillAgain) {
        backfillAgain = false;
        q->backfill();
    }
}

QVector<VoiceprintBackfillSkip> VoiceAnalysisService::backfillSkipped() const
{
    QVector<VoiceprintBackfillSkip> out = d->backfillSkipped;
    std::stable_sort(out.begin(), out.end(), [](const VoiceprintBackfillSkip& a, const VoiceprintBackfillSkip& b) {
        return a.person.localeAwareCompare(b.person) < 0;
    });
    return out;
}

// ---- voiceprints from audio -------------------------------------------------------------------------

bool VoiceAnalysisService::enrollSpeaker(const QString& meetingId, const QString& rawLabel, const PersonId& person)
{
    if (person.isNull()) return false;
    const Meeting m = d->deps.store->load(meetingId);
    if (m.id.isEmpty()) return false;
    // No model: silently skipped (manual labeling works anyway).
    if (!d->usable() || !d->voiceSet.ensureLoaded()) return false;

    const MergedTranscript merged = TranscriptRepository::readTokens(m.folder);
    if (merged.tokens.isEmpty()) return false;
    const VoiceSource src = resolveVoiceSource(m, merged, rawLabel);
    if (src.absPath.isEmpty()) return false;

    QStringList refs;
    const EmbeddingSet set = embeddingForLabel(*d->deps.media, d->voiceSet, src.absPath, src.offsetMs, merged, rawLabel,
                                               src.fileRel, &refs);
    if (set.isEmpty()) return false;

    Voiceprint tmpl;
    tmpl.sourceMeetingId = m.id;
    tmpl.sourceTrack = src.trackId;
    tmpl.device = src.device;
    tmpl.sampleRef = representativeSampleRef(merged, rawLabel, src.fileRel);
    tmpl.sourceRefs = refs;
    tmpl.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const Voiceprint& vp : printsFromSet(set, tmpl)) d->deps.voiceprints->addPrint(person, vp);
    emit voiceprintsChanged();
    return true;
}

PersonId VoiceAnalysisService::enrollFromSample(const PersonChoice& choice, const QString& meetingId,
                                                const QString& trackId, qint64 startMs, qint64 endMs)
{
    if (choice.isAnonymous()) return {};
    const Meeting m = d->deps.store->load(meetingId);
    if (m.id.isEmpty()) return {};
    if (!d->usable() || !d->voiceSet.ensureLoaded()) return {};
    const Track* track = nullptr;
    for (const Track& t : m.tracks) if (t.id == trackId) { track = &t; break; }
    if (!track) return {};
    const QString path = QDir(m.folder).filePath(track->file);
    // startMs/endMs in meeting time (like the sampleRef) -> the track file's time.
    const tanara::tracktiming::FileRange fr = tanara::tracktiming::fileRange(*track, startMs, endMs);
    if (!fr.valid()) return {};
    const EmbeddingSet set =
        d->voiceSet.embedPcm(VoiceEmbedder::decodePcm16kMono(*d->deps.media, path, fr.startMs, fr.endMs));
    if (set.isEmpty()) return {};
    const PersonId person = d->deps.people ? d->deps.people->resolve(choice) : choice.id;
    if (person.isNull()) return {};

    Voiceprint tmpl;
    tmpl.sourceMeetingId = m.id;
    tmpl.sourceTrack = track->id;
    tmpl.device = track->deviceName;
    tmpl.sampleRef = QStringLiteral("%1#%2-%3").arg(track->file).arg(startMs).arg(endMs);
    tmpl.sourceRefs = QStringList{tmpl.sampleRef};
    tmpl.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const Voiceprint& vp : printsFromSet(set, tmpl)) d->deps.voiceprints->addPrint(person, vp);
    emit voiceprintsChanged();
    emit peopleChanged();
    return person;
}

// ---- matching -------------------------------------------------------------------------------------------

VoiceMatch VoiceAnalysisService::testSpeakerMatch(const QString& meetingId, const QString& rawLabel)
{
    VoiceMatch none;   // { "", -1 }
    const Meeting m = d->deps.store->load(meetingId);
    if (m.id.isEmpty()) return none;
    if (!d->usable() || !d->voiceSet.ensureLoaded()) return none;
    const MergedTranscript merged = TranscriptRepository::readTokens(m.folder);
    if (merged.tokens.isEmpty()) return none;
    const VoiceSource src = resolveVoiceSource(m, merged, rawLabel);
    if (src.absPath.isEmpty()) return none;
    const EmbeddingSet e = embeddingForLabel(*d->deps.media, d->voiceSet, src.absPath, src.offsetMs, merged, rawLabel);
    if (e.isEmpty()) return none;
    return d->bestMatch(e, e.keys());
}

QVector<SpeakerVoiceMatch> VoiceAnalysisService::identifyTranscriptSpeakers(
    const QString& meetingId, const std::function<bool(int, int)>& onProgress)
{
    QVector<SpeakerVoiceMatch> out;
    const Meeting m = d->deps.store->load(meetingId);
    if (m.id.isEmpty()) return out;
    if (!d->usable() || !d->voiceSet.ensureLoaded()) return out;

    const MergedTranscript merged = TranscriptRepository::readTokens(m.folder);
    if (merged.tokens.isEmpty()) return out;

    // Distinct raw speaker labels.
    QStringList labels;
    for (const TranscriptToken& t : merged.tokens)
        if (!t.speaker.isEmpty() && !labels.contains(t.speaker))
            labels << t.speaker;

    // We match EVERY unnamed speaker (mic + loopback) against the voiceprint DB. There is no
    // loudness/position-based assumption and no auto-enroll: the NAME goes onto the speaker by fingerprint
    // (or the user's one-time manual rename).
    int progressDone = 0;
    for (const QString& label : labels) {
        if (onProgress && !onProgress(++progressDone, labels.size()))
            break;   // the user cancelled: the matches already found are saved
        if (m.speakerMap.contains(label))
            continue;   // already named (by hand or an earlier match)
        const VoiceSource src = resolveVoiceSource(m, merged, label);
        if (src.absPath.isEmpty()) continue;
        const EmbeddingSet e = embeddingForLabel(*d->deps.media, d->voiceSet, src.absPath, src.offsetMs, merged, label);
        if (e.isEmpty()) continue;
        SpeakerVoiceMatch match;
        if (d->matchSpeaker(label, e, &match)) out.append(match);
    }
    return out;
}

QVector<ParticipantGuess> VoiceAnalysisService::identifyParticipants(
    const QString& meetingId, const std::function<bool(int, int)>& onProgress)
{
    const Meeting m = d->deps.store->load(meetingId);
    if (m.id.isEmpty()) return {};
    if (!d->usable() || !d->voiceSet.ensureLoaded()) return {};
    // Clustering runs on the fused vector (with every loadable model); a window counts only if every model gave a vector for it.
    const QStringList modelIds = d->voiceSet.loadedModelIds();

    constexpr qint64 kWinMs = 3000;          // window length
    constexpr int    kTargetWindows = 50;    // number of windows per track (upper limit)
    constexpr double kMergeThreshold = 0.55; // cosine threshold for cluster merging
    constexpr float  kSilenceRms = 0.004f;   // a window quieter than this = silence (skipped)

    // Every cluster becomes one probe of ONE identification request (the IParticipantIdentifier port's local
    // rule, with the voiceprint store's people as the candidates); one guess per probe, in probe order.
    providers::IdentifyRequest req;
    req.threshold = kVoiceMatchThreshold;
    if (d->deps.voiceprints) req.candidates = candidatesOf(*d->deps.voiceprints, d->deps.people);
    auto guesses = [&req]() { return LocalParticipantIdentifier::identifyNow(req); };

    // Progress: all windows to process (active tracks x windows per track), for the callback.
    int progressTotal = 0, progressDone = 0;
    if (m.durationMs >= kWinMs) {
        const qint64 step0 = std::max<qint64>(kWinMs, m.durationMs / kTargetWindows);
        int perTrack = 0;
        for (qint64 s = 0; s + kWinMs <= m.durationMs; s += step0) ++perTrack;
        int activeTracks = 0;
        for (const Track& t : m.tracks) if (t.active) ++activeTracks;
        progressTotal = perTrack * activeTracks;
    }

    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        const qint64 D = m.durationMs;
        if (D < kWinMs) continue;
        const QString path = QDir(m.folder).filePath(t.file);
        const qint64 step = std::max<qint64>(kWinMs, D / kTargetWindows);

        QVector<QVector<float>> embs;      // fused vectors
        QVector<EmbeddingSet> sets;        // the same per model (for voiceprint matching)
        QVector<qint64> winStart;
        for (qint64 s = 0; s + kWinMs <= D; s += step) {
            if (onProgress && !onProgress(++progressDone, progressTotal))
                return guesses();   // the user cancelled
            // The window is in meeting time; a window outside the file of a track that starts later / ends earlier
            // is empty (skipped).
            const tanara::tracktiming::FileRange fr = tanara::tracktiming::fileRange(t, s, s + kWinMs);
            if (!fr.valid()) continue;
            QVector<float> pcm = VoiceEmbedder::decodePcm16kMono(*d->deps.media, path, fr.startMs, fr.endMs);
            if (pcm.isEmpty()) continue;
            double sum = 0.0;
            for (float x : pcm) sum += static_cast<double>(x) * x;
            const double rms = std::sqrt(sum / static_cast<double>(pcm.size()));
            if (rms < kSilenceRms) continue;          // silence -> skipped
            const EmbeddingSet set = d->voiceSet.embedPcmWith(pcm, modelIds);
            const QVector<float> e = fusion::fuse(set, modelIds);
            if (!e.isEmpty()) { embs.append(e); sets.append(set); winStart.append(s); }
        }
        if (embs.isEmpty()) continue;

        const QVector<int> labels = participants::clusterEmbeddings(embs, kMergeThreshold);
        QMap<int, QVector<int>> groups;
        for (int i = 0; i < labels.size(); ++i) groups[labels[i]].append(i);

        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            const QVector<int>& idxs = it.value();
            // Noise filter: a 1-window cluster is dropped if there is another (overlap/click).
            if (idxs.size() < 2 && groups.size() > 1) continue;

            QVector<float> cent(embs[idxs[0]].size(), 0.0f);
            for (int i : idxs)
                for (int k = 0; k < cent.size(); ++k) cent[k] += embs[i][k];
            for (int k = 0; k < cent.size(); ++k) cent[k] /= idxs.size();
            cent = library::VoiceprintStore::l2normalize(cent);

            // Per-model centroid for matching (voiceprints are stored per model).
            EmbeddingSet centSet;
            for (const QString& id : modelIds) {
                QVector<float> c;
                for (int i : idxs) {
                    const QVector<float>& v = sets[i].value(id);
                    if (c.isEmpty()) c.fill(0.0f, v.size());
                    for (int k = 0; k < c.size() && k < v.size(); ++k) c[k] += v[k];
                }
                centSet.insert(id, library::VoiceprintStore::l2normalize(c));
            }
            // Representative window = the nearest to the centroid (medoid).
            qint64 repStart = winStart[idxs[0]]; double bestSim = -2.0;
            for (int i : idxs) {
                const double s = library::VoiceprintStore::cosineSimilarity(cent, embs[i]);
                if (s > bestSim) { bestSim = s; repStart = winStart[i]; }
            }

            providers::IdentifyProbe probe = probeOf(centSet, modelIds);
            probe.trackId = t.id;
            probe.deviceName = t.deviceName;
            probe.windows = idxs.size();
            probe.sampleRef = QStringLiteral("%1#%2-%3").arg(t.file).arg(repStart).arg(repStart + kWinMs);
            req.probes.append(probe);
        }
    }
    QVector<ParticipantGuess> out = guesses();
    // The most windows first (dominant speakers first).
    std::sort(out.begin(), out.end(),
              [](const ParticipantGuess& a, const ParticipantGuess& b) { return a.windows > b.windows; });
    return out;
}

// ---- asynchronous identification ------------------------------------------------------------------------

bool VoiceAnalysisService::identifyRunning(const QString& meetingId) const
{
    return d->identifyRuns.contains(meetingId);
}

bool VoiceAnalysisService::startIdentify(const QString& meetingId, bool asStage)
{
    if (d->identifyRuns.contains(meetingId) || !d->usable())
        return false;
    const Meeting m = d->deps.store->load(meetingId);
    if (m.id.isEmpty()) return false;
    const MergedTranscript merged = TranscriptRepository::readTokens(m.folder);
    if (merged.tokens.isEmpty()) return false;
    library::MeetingJobTracker* jobs = d->deps.jobs;

    // The still anonymous (unmapped) raw speaker labels and their audio source.
    struct Item { QString label; QString audioPath; qint64 offsetMs = 0; };
    QVector<Item> items;
    QStringList seen;
    for (const TranscriptToken& t : merged.tokens) {
        if (t.speaker.isEmpty() || seen.contains(t.speaker)) continue;
        seen << t.speaker;
        if (m.speakerMap.contains(t.speaker)) continue;
        const VoiceSource src = resolveVoiceSource(m, merged, t.speaker);
        if (!src.absPath.isEmpty()) items.append({t.speaker, src.absPath, src.offsetMs});
    }
    if (items.isEmpty()) {
        jobs->markIdentified(meetingId);   // no anonymous speaker, nothing to identify
        return false;
    }

    const int total = int(items.size());
    const JobKind kind = asStage ? JobKind::Transcribe : JobKind::Identify;
    if (asStage) {
        jobs->setStage(meetingId, kind, QStringLiteral("identify"), StageState::Running, 0,
                       tr("%1 / %2 beszélő").arg(0).arg(total));
    } else {
        jobs->begin(meetingId, kind, tr("Résztvevők azonosítása"));
        jobs->setCounts(meetingId, kind, 0, total);
    }

    // Decoding (ffmpeg) + embedding (ONNX) run on a background thread, with its OWN embedder instance (it
    // does not touch the main thread's embedder). Matching against the voiceprint DB happens on the main
    // thread (only the main thread reaches the DB), and the matches go out on speakersIdentified.
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    auto results = std::make_shared<QVector<QPair<QString, EmbeddingSet>>>();
    const VoiceEmbedderSet proto = d->voiceSet.freshCopy();
    QPointer<VoiceAnalysisService> self(this);
    const media::MediaToolPtr media = d->deps.media;
    QThread* th = QThread::create([self, items, merged, proto, cancel, results, meetingId,
                                   total, kind, asStage, media]() {
        const VoiceEmbedderSet emb = proto.freshCopy();   // the thread's own model instances
        if (!emb.ensureLoaded()) return;
        int done = 0;
        for (const Item& it : items) {
            if (cancel->load()) break;
            const EmbeddingSet e = embeddingForLabel(*media, emb, it.audioPath, it.offsetMs, merged, it.label);
            ++done;
            // The result + real count progress back to the main thread.
            QMetaObject::invokeMethod(qApp, [self, results, label = it.label, e, meetingId,
                                             done, total, kind, asStage]() {
                if (!self) return;
                results->append({label, e});
                if (asStage) {
                    self->d->deps.jobs->setStage(meetingId, kind, QStringLiteral("identify"),
                                                 StageState::Running, done * 100 / total,
                                                 VoiceAnalysisService::tr("%1 / %2 beszélő").arg(done).arg(total));
                } else {
                    self->d->deps.jobs->setCounts(meetingId, kind, done, total);
                }
            }, Qt::QueuedConnection);
        }
    });
    d->identifyRuns.insert(meetingId, Impl::IdentifyRun{th, cancel, asStage});
    const auto stampOf = [this](const QString& id, const QString& folder) {
        return d->deps.transcriptStamp ? d->deps.transcriptStamp(id, folder) : QString();
    };
    const QString stampAtStart = stampOf(meetingId, m.folder);
    connect(th, &QThread::finished, this,
            [this, th, meetingId, cancel, results, asStage, kind, stampAtStart, stampOf, folder = m.folder]() {
        library::MeetingJobTracker* jobs = d->deps.jobs;
        d->identifyRuns.remove(meetingId);
        th->deleteLater();
        // If the transcript changed during the run (a new transcription arrived), the embeddings belong to the
        // OLD transcript's labels: we do not write the names onto the new transcript; it counts as cancelled.
        // (Otherwise: even on cancel we save what was found so far.)
        const bool stale = stampOf(meetingId, folder) != stampAtStart;
        const bool wasCancelled = cancel->load() || stale;
        if (!stale) {
            const QVector<SpeakerVoiceMatch> matches = d->matchEmbeddings(meetingId, *results);
            if (!matches.isEmpty()) emit speakersIdentified(meetingId, matches);
        }
        if (!wasCancelled)
            jobs->markIdentified(meetingId);
        if (asStage) {
            jobs->setStage(meetingId, kind, QStringLiteral("identify"),
                           wasCancelled ? StageState::Skipped : StageState::Done);
            jobs->finish(meetingId, kind);   // the transcript is done; the task closes successfully
        } else if (wasCancelled) {
            jobs->cancelled(meetingId, kind);
        } else {
            jobs->finish(meetingId, kind);
        }
    });
    th->start();
    return true;
}

bool VoiceAnalysisService::cancel(const QString& meetingId, JobKind kind)
{
    const auto ir = d->identifyRuns.constFind(meetingId);
    if (ir == d->identifyRuns.constEnd()) return false;
    if (kind == JobKind::Transcribe) {
        // The transcript is already done, the last (identification) stage is running, so only that one is skipped.
        if (!ir->asStage) return false;
    } else if (kind == JobKind::Identify) {
        if (ir->asStage) return false;
    } else {
        return false;
    }
    ir->cancel->store(true);   // the thread stops before the next speaker
    d->deps.jobs->setCancelling(meetingId, kind);
    return true;
}

// ---- track activity (the speech strip) --------------------------------------------------------------------

MeetingActivity VoiceAnalysisService::trackActivity(const QString& meetingId)
{
    const Meeting m = d->deps.store->load(meetingId);
    if (m.id.isEmpty() || m.tracks.isEmpty()) return {};
    // The strip shows EVERY track (the excluded one too): the fingerprint treats them all as active.
    Meeting all = m;
    for (Track& t : all.tracks) t.active = true;
    const QString fp = trackactivity::activityFingerprint(all, m.folder);
    if (const auto it = d->activityView.constFind(meetingId);
        it != d->activityView.constEnd() && it->fingerprint == fp)
        return *it;
    MeetingActivity a = MeetingActivity::load(m.folder, fp);   // if every track is included, this one
    if (a.isEmpty()) a = MeetingActivity::load(m.folder, fp, MeetingActivity::viewFilePath(m.folder));
    if (!a.isEmpty()) {
        d->activityView.insert(meetingId, a);
        return a;
    }
    if (!d->activityViewRunning.contains(meetingId)) {
        d->activityViewRunning.insert(meetingId);
        const auto stop = d->analysisShutdown;
        QPointer<VoiceAnalysisService> self(this);
        const QString folder = m.folder;
        d->analysisPool.start([self, all, folder, fp, stop, meetingId, media = d->deps.media]() {
            MeetingActivity act;
            act.fingerprint = fp;
            if (!stop->load()) {
                act = computeMeetingActivity(all, folder, *media);
                act.fingerprint = fp;
                if (!act.isEmpty()) act.save(folder, MeetingActivity::viewFilePath(folder));
            }
            QMetaObject::invokeMethod(qApp, [self, meetingId, act]() {
                if (self) self->d->finishTrackActivity(meetingId, act);
            }, Qt::QueuedConnection);
        });
    }
    MeetingActivity pending;
    pending.fingerprint = fp;
    return pending;
}

bool VoiceAnalysisService::trackActivityComputing(const QString& meetingId) const
{
    return d->activityViewRunning.contains(meetingId);
}

void VoiceAnalysisService::measureActivity(const Meeting& m, std::function<void(const MeetingActivity&)> done)
{
    const QString folder = m.folder;
    const auto stop = d->analysisShutdown;
    QPointer<VoiceAnalysisService> self(this);
    d->analysisPool.start([self, m, folder, stop, done, media = d->deps.media]() {
        if (stop->load()) return;
        const QString fp = trackactivity::activityFingerprint(m, folder);
        MeetingActivity act = MeetingActivity::load(folder, fp);
        if (act.isEmpty()) {
            act = computeMeetingActivity(m, folder, *media);
            if (!act.isEmpty()) act.save(folder);
        }
        QMetaObject::invokeMethod(qApp, [self, act, done]() {
            if (self && done) done(act);
        }, Qt::QueuedConnection);
    });
}

// ---- pre-transcript participant analysis --------------------------------------------------------------------

bool VoiceAnalysisService::analysisRunning(const QString& meetingId) const
{
    return d->analysisRuns.contains(meetingId);
}

void VoiceAnalysisService::cancelAnalysis(const QString& meetingId)
{
    if (const auto it = d->analysisRuns.constFind(meetingId); it != d->analysisRuns.constEnd())
        it.value()->store(true);
}

bool VoiceAnalysisService::maybeAnalyzeParticipants(const QString& meetingId)
{
    if (!d->usable() || d->analysisRuns.contains(meetingId)) return false;
    const Meeting m = d->deps.store->load(meetingId);
    if (m.id.isEmpty() || m.tracks.isEmpty() || m.approval.has_value()) return false;
    // The speech check is running (its end calls again), or the speechless tracks are not filtered out yet
    // (the measurement first; its end calls again).
    if (d->deps.speechCheckPending && d->deps.speechCheckPending(m)) return false;
    if (ParticipantAnalysisFile::load(m.folder).tracksKey == trackactivity::activityFingerprint(m, m.folder))
        return false;   // it already ran on these same tracks
    return analyzeParticipants(meetingId);
}

bool VoiceAnalysisService::analyzeParticipants(const QString& meetingId)
{
    if (d->analysisRuns.contains(meetingId)) return false;
    const Meeting m = d->deps.store->load(meetingId);
    if (m.id.isEmpty() || !d->usable()) return false;
    if (m.approval && m.approval->solo) return false;   // "only me": nothing to identify

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    d->analysisRuns.insert(meetingId, cancel);
    emit participantAnalysisStarted(meetingId);
    const VoiceEmbedderSet proto = d->voiceSet.freshCopy();
    const auto stop = d->analysisShutdown;
    QPointer<VoiceAnalysisService> self(this);
    d->analysisPool.start([self, m, proto, cancel, stop, meetingId, media = d->deps.media]() {
        const QString fp = trackactivity::activityFingerprint(m, m.folder);
        ClusterSet cs;
        if (!stop->load() && !cancel->load()) {
            MeetingActivity act = MeetingActivity::load(m.folder, fp);
            if (act.isEmpty()) {
                act = computeMeetingActivity(m, m.folder, *media);
                if (!act.isEmpty()) act.save(m.folder);
            }
            const VoiceEmbedderSet emb = proto.freshCopy();   // the thread's own model instances
            // Progress: from the window index the track (k / N, only tracks with speech) and the percentage; a signal
            // goes to the main thread only on change.
            QVector<int> ends;   // cumulative window count of the tracks with speech
            int windows = 0;
            for (const TrackActivity& ta : act.tracks) {
                const int n = int(participants::speechWindows(ta, QString()).size());
                if (n == 0) continue;
                windows += n;
                ends.append(windows);
            }
            auto last = std::make_shared<QPair<int, int>>(-1, -1);
            auto report = [self, meetingId, ends, last](int done, int total) {
                if (total <= 0 || ends.isEmpty()) return;
                int track = 1;
                while (track < ends.size() && done > ends.at(track - 1)) ++track;
                const int pct = qBound(0, done * 100 / total, 100);
                if (last->first == track && last->second == pct) return;
                *last = {track, pct};
                const int tracks = int(ends.size());
                QMetaObject::invokeMethod(qApp, [self, meetingId, track, tracks, pct]() {
                    if (self && self->d->analysisRuns.contains(meetingId))
                        emit self->participantAnalysisProgress(meetingId, track, tracks, pct);
                }, Qt::QueuedConnection);
            };
            cs = participants::computeVoiceClusters(
                m, act, emb, participants::filePcmReader(m, media),
                [cancel, stop, report](int done, int total) {
                    report(done, total);
                    return !cancel->load() && !stop->load();
                });
        } else {
            cs.cancelled = true;
        }
        QMetaObject::invokeMethod(qApp, [self, meetingId, cs, fp]() {
            if (self) self->d->finishParticipantAnalysis(meetingId, cs, fp);
        }, Qt::QueuedConnection);
    });
    return true;
}

void VoiceAnalysisService::Impl::finishParticipantAnalysis(const QString& meetingId, ClusterSet clusters,
                                                           const QString& tracksKey)
{
    analysisRuns.remove(meetingId);
    Meeting m = deps.store->load(meetingId);
    if (m.id.isEmpty()) return;
    if (clusters.cancelled || !clusters.error.isEmpty()) {
        if (!clusters.error.isEmpty())
            qCInfo(lcVoice).noquote() << "Résztvevő-elemzés nem futott:" << clusters.error;
        emit q->participantAnalysisFinished(meetingId);
        return;
    }
    if (m.approval && m.approval->solo) {   // it became "only me" meanwhile: the analysis result is not needed
        emit q->participantAnalysisFinished(meetingId);
        return;
    }
    m.participants = participants::buildParticipants(clusters, m, q->candidateContext());

    ParticipantAnalysisFile file;
    file.at = QDateTime::currentDateTime().toString(Qt::ISODate);
    file.modelIds = clusters.modelIds;
    file.tracksKey = tracksKey;
    file.clusters = clusters.clusters;
    if (!file.save(m.folder))
        qCWarning(lcVoice).noquote() << "Résztvevő-elemzés: nem írható:" << ParticipantAnalysisFile::filePath(m.folder);

    // If the transcript already exists: the binding runs now (otherwise when the transcript arrives).
    const QVector<TranscriptLine> lines = deps.transcriptLines ? deps.transcriptLines(m.folder) : QVector<TranscriptLine>();
    if (!lines.isEmpty()) participants::bindRawSpeakers(m.participants, file.clusters, lines);
    deps.store->saveMeeting(m);
    qCInfo(lcVoice).noquote() << "Résztvevő-elemzés kész:" << m.participants.size() << "jelölt,"
                              << clusters.clusters.size() << "klaszter," << clusters.windows << "ablak";
    emit q->participantsChanged(meetingId);
    emit q->participantAnalysisFinished(meetingId);
}

} // namespace tanara::processing

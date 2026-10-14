#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/foundation/log/Logging.h"

#include "tanara/editing/review/CandidateRanker.h"
#include "tanara/editing/review/SideAnalysis.h"
#include "tanara/editing/review/SpeakerAnalysis.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/processing/analysis/TrackActivity.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/processing/voiceid/VoiceModelRegistry.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QPair>
#include <QSet>
#include <QThread>
#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>
#include <QEventLoop>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>

namespace tanara::editing {

using namespace speakeredit;

namespace {

constexpr int kMaxUndoSteps = 200;
// The deviating-sample threshold is the limit of the candidate ranking's voice evidence (see SpeakerAnalysis.h).
static_assert(kSampleDeviationFit == ranking::kVoiceGood,
              "az eltérő-minta küszöb a „Miért ő?” hang-chipjével együtt mozog");
// transcript.md is rewritten this long after the last name change (batched).
constexpr int kMarkdownDelayMs = 1500;

// A change of one Meeting.speakerMap key (undo restores only the AFFECTED keys, so a change arriving
// meanwhile from outside, the old UI or auto-identification, is not overwritten).
struct MapDelta {
    QString raw;
    bool    hadBefore = false;
    QString before;
    bool    hasAfter = false;
    QString after;
};

// A voiceprint side effect (when fixing a "wrong recognition"): undo reverses it.
struct VoiceprintOp {
    bool       added = false;   // true: ADDED to the person; false: REMOVED from them
    QString    person;
    Voiceprint print;
};

// The observable state of a line: the difference before/after an operation yields utterancesChanged.
struct RowState {
    QString speaker;
    bool corrected = false;
    bool confirmed = false;
    bool uncertain = false;
    bool noisy = false;
    bool ignored = false;
    QString likely;
    QString reason;
    bool operator==(const RowState& o) const {
        return speaker == o.speaker && corrected == o.corrected && confirmed == o.confirmed
            && uncertain == o.uncertain && noisy == o.noisy && ignored == o.ignored
            && likely == o.likely && reason == o.reason;
    }
};

struct Command {
    QString text;
    quint64 serial = 0;     // for the shared undo adapter (undoSerials)
    SpeakerOverlay before;
    SpeakerOverlay after;
    QVector<MapDelta> map;
    QVector<VoiceprintOp> voiceprints;
    QStringList peopleAdded;
    QStringList affectedSpeakers;
};

// An operation in progress: the starting state + the side effects accumulating meanwhile.
struct Step {
    SpeakerOverlay before;
    QMap<QString, QString> mapBefore;
    QVector<RowState> rows;
    QStringList names;              // per-line displayed name (whether a new transcript.md is needed)
    SummaryStaleInfo stale;
    QVector<VoiceprintOp> voiceprints;
    QStringList peopleAdded;
    QStringList affectedSpeakers;
};

bool sameEdits(const SpeakerOverlay& a, const SpeakerOverlay& b)
{
    if (a.merged != b.merged || a.removedRaw != b.removedRaw) return false;
    if (a.speakerTracks != b.speakerTracks) return false;
    if (a.participants.size() != b.participants.size()) return false;
    for (int i = 0; i < a.participants.size(); ++i) {
        const OverlayParticipant& x = a.participants[i];
        const OverlayParticipant& y = b.participants[i];
        if (x.key != y.key || x.person != y.person || x.label != y.label) return false;
    }
    if (a.utterances.size() != b.utterances.size()) return false;
    for (auto it = a.utterances.constBegin(); it != a.utterances.constEnd(); ++it) {
        const auto jt = b.utterances.constFind(it.key());
        if (jt == b.utterances.constEnd()) return false;
        if (!(it.value() == jt.value())) return false;
    }
    return true;
}

void appendUnique(QStringList& list, const QString& s)
{
    if (!s.isEmpty() && !list.contains(s)) list << s;
}

// From a voiceprint's sampleRef ("file#startMs-endMs") the start time; -1 if none.
qint64 sampleRefStartMs(const QString& ref)
{
    const int hash = ref.lastIndexOf(QLatin1Char('#'));
    if (hash < 0) return -1;
    const QString range = ref.mid(hash + 1);
    const int dash = range.indexOf(QLatin1Char('-'));
    bool ok = false;
    const qint64 start = (dash < 0 ? range : range.left(dash)).toLongLong(&ok);
    return ok ? start : -1;
}

// The default track-activity source (on a background thread): only the tracks.activity.bin cache. Computing
// a missing one needs the media tool: the owner installs that provider (setActivityProvider, e.g.
// processing::loadOrComputeSideActivity in AppController).
MeetingActivity defaultActivity(const Meeting& m)
{
    if (m.folder.isEmpty()) return {};
    bool mic = false, loop = false;
    for (const Track& t : m.tracks) {
        if (!t.active) continue;
        mic |= t.kind == TrackKind::Mic;
        loop |= t.kind == TrackKind::Loopback;
    }
    if (!mic || !loop) return {};
    return MeetingActivity::load(m.folder, processing::trackactivity::activityFingerprint(m, m.folder));
}

} // namespace

struct SpeakerEditor::Private {
    SpeakerEditor* q = nullptr;
    library::MeetingRepository* store = nullptr;
    library::PeopleStore* people = nullptr;
    library::VoiceprintStore* voiceprints = nullptr;
    QString meetingId;
    QString folder;
    QString audioPath;      // the mixdown (may not exist)
    QString audioRel;
    QString userName;
    bool    hasSummary = false;

    QVector<TranscriptLine> lines;
    QHash<QString, int> indexById;
    QStringList rawOrder;               // raw labels in order of first appearance

    SpeakerOverlay ov;
    QMap<QString, QString> speakerMap;
    QVector<QString> assigned;          // per line the resolved speaker key
    QVector<bool> uncertain;
    QVector<bool> voiceUncertain;       // only the voice judgment (for uncertainReason)
    QVector<bool> overlapNoisy;         // per line: overlaps with another speaker (automatic, not persisted)

    QVector<Command> undoStack;
    QVector<Command> redoStack;
    SpeakerSuggestion suggestion;
    PairRecheckOffer pairOffer;
    QSet<QString> declinedPairs;        // "Not now": we do not offer these pairs again

    // embeddings
    processing::UtteranceEmbedderFactory factory;
    processing::UtteranceEmbeddingCache cache;
    QStringList modelIds{processing::VoiceModelRegistry::defaultModelId()};   // alphabetical
    // Per line the fused vector of the models (only where every model has a non-empty vector); the
    // analysis (SpeakerAnalysis) gets this. Refreshed after every change of the cache.
    QHash<QString, QVector<float>> fused;
    QThread* thread = nullptr;
    std::shared_ptr<std::atomic_bool> cancel;
    int epoch = 0;                      // grows on every (re)load; the old thread's result is dropped
    int embDone = 0;
    int embTotal = 0;
    QString embError;

    // ---- track side, candidates, "To review" groups (background thread) ----
    SpeakerEditor::ActivityProvider activityProvider;
    library::TagEvidenceFn tagEvidence;
    SpeakerEditor::PersonTagsFn personTags;   // manual tags for the quick "+ #Tag"
    QStringList meetingTags;            // Meeting::tagIds
    mutable QHash<QString, library::TagEvidence> tagEvidenceCache;   // case-folded person -> evidence
    std::optional<MeetingActivity> activity;    // in memory after the first computation
    ReviewResult review;                // the latest analysis
    int  reviewEpoch = 0;               // grows on every (re)load; the old result is dropped
    bool reviewRunning = false;
    bool reviewPending = false;         // a newer request came in while running
    bool reviewScheduled = false;
    QString newPersonKey;               // the person just created (SimilarToNewPerson)

    // ---- transparency of sample training ----
    SampleDeviation deviation;          // deviation measure of the last sample-giving decision
    // The lines that counted as sample lines at the session start: id -> their speaker key then.
    QHash<QString, QString> sessionBaseline;
    QVector<SpeakerSampleStats> lastStats;  // the sample meter last signaled

    // Delayed regeneration of transcript.md (see persist()).
    QTimer* markdownTimer = nullptr;
    bool markdownPending = false;
    // Regenerating transcript.md runs on a background thread (rereading the tokens + writing the whole file
    // takes ~120 ms for a 2-hour meeting; on the main thread that stalled the UI after every correction).
    // One run at a time; a request arriving meanwhile restarts at the end.
    bool markdownRunning = false;
    std::atomic<int> markdownInFlight{0};

    // ---- loading -----------------------------------------------------------
    void load()
    {
        QElapsedTimer perf;
        perf.start();
        const Meeting m = store ? store->load(meetingId) : Meeting();
        folder = m.folder;
        hasSummary = m.hasSummary;
        speakerMap = m.speakerMap;
        meetingTags = m.tagIds;
        tagEvidenceCache.clear();
        audioPath = m.folder.isEmpty() ? QString() : mixdownPath(m);
        audioRel = library::MeetingLayout::mixdownOf(m);
        const qint64 tMeeting = perf.elapsed();

        lines = folder.isEmpty() ? QVector<TranscriptLine>() : loadTranscriptLines(folder);
        indexById.clear();
        rawOrder.clear();
        for (int i = 0; i < lines.size(); ++i) {
            indexById.insert(lines[i].id, i);
            if (!rawOrder.contains(lines[i].rawLabel)) rawOrder << lines[i].rawLabel;
        }
        const qint64 tLines = perf.elapsed();
        ov = folder.isEmpty() ? SpeakerOverlay() : loadOverlayFor(folder, lines);
        const qint64 tOverlay = perf.elapsed();
        cache = folder.isEmpty() ? processing::UtteranceEmbeddingCache()
                                 : processing::UtteranceEmbeddingCache::load(folder, ov.transcriptFingerprint);
        rebuildFused();
        const qint64 tCache = perf.elapsed();
        ++epoch;
        recomputeAssigned();
        const qint64 tAssigned = perf.elapsed();
        ++reviewEpoch;
        review = ReviewResult();
        newPersonKey.clear();
        recomputeUncertain();
        scheduleReview();
        qCDebug(lcPerf).noquote()
            << QStringLiteral("SpeakerEditor::load %1 sor: meeting %2 ms, átirat %3 ms, overlay %4 ms, "
                              "embedding-cache %5 ms, feloldás %6 ms, bizonytalanság %7 ms, össz %8 ms")
                   .arg(lines.size()).arg(tMeeting).arg(tLines - tMeeting).arg(tOverlay - tLines)
                   .arg(tCache - tOverlay).arg(tAssigned - tCache).arg(perf.elapsed() - tAssigned)
                   .arg(perf.elapsed());
    }

    // ---- embedding cache -> fused vectors -------------------------------------
    void refreshFused(const QString& id)
    {
        const QVector<float> f = processing::fusion::fuse(cache.setFor(id, modelIds), modelIds);
        if (f.isEmpty()) fused.remove(id);
        else fused.insert(id, f);
    }

    void rebuildFused()
    {
        fused.clear();
        for (const TranscriptLine& l : std::as_const(lines)) refreshFused(l.id);
    }

    // Which models this line has not been tried with yet (empty = done).
    QStringList missingModels(const QString& id) const
    {
        QStringList out;
        for (const QString& m : modelIds)
            if (!cache.has(m, id)) out << m;
        return out;
    }

    // ---- resolution -----------------------------------------------------------
    void recomputeAssigned()
    {
        assigned.resize(lines.size());
        for (int i = 0; i < lines.size(); ++i)
            assigned[i] = resolveSpeakerKey(ov, lines[i]);
        recomputeOverlap();
    }

    // The automatic "talked over each other" flag by the RESOLVED speakers (two overlapping lines of the
    // same speaker are not crosstalk).
    void recomputeOverlap()
    {
        QVector<TimedLine> tl(lines.size());
        QHash<QString, int> ids;
        for (int i = 0; i < lines.size(); ++i) {
            tl[i].startMs = lines[i].startMs;
            tl[i].endMs = lines[i].endMs;
            auto it = ids.find(assigned[i]);
            if (it == ids.end()) it = ids.insert(assigned[i], ids.size());
            tl[i].speaker = it.value();
        }
        overlapNoisy = computeOverlapNoisy(tl);
    }

    bool ignoredAt(int i) const
    {
        const auto it = ov.utterances.constFind(lines[i].id);
        return it != ov.utterances.constEnd() && it->ignored;
    }

    bool noisyAt(int i) const
    {
        const auto it = ov.utterances.constFind(lines[i].id);
        if (it != ov.utterances.constEnd() && it->noisy.has_value()) return *it->noisy;
        return overlapNoisy.value(i);
    }

    // The current key of the recheck's suggestion (the merged label points to the target).
    QString hintKeyAt(int i) const
    {
        const auto it = ov.utterances.constFind(lines[i].id);
        if (it == ov.utterances.constEnd() || it->recheckHint.isEmpty()) return {};
        QString key = it->recheckHint;
        for (int guard = 0; guard < 8 && ov.merged.contains(key); ++guard) key = ov.merged.value(key);
        return key;
    }

    // The line is already at the suggested speaker (or at the same person).
    bool hintResolved(int i, const QString& key) const
    {
        if (key == assigned[i]) return true;
        const QString person = personOf(key);
        return !person.isEmpty() && person.compare(personOf(assigned[i]), Qt::CaseInsensitive) == 0;
    }

    // The recheck's suggestion (the speaker that fits the voice better), if still valid and showable.
    QString likelyAt(int i) const
    {
        const QString key = hintKeyAt(i);
        if (key.isEmpty() || !visible(key) || hintResolved(i, key)) return {};
        return key;
    }

    // Whether the recheck flag still stands (a correction / confirmation clears it; if the line moved to the
    // suggested speaker meanwhile, e.g. by a merge, it ceases by itself).
    bool recheckedAt(int i) const
    {
        const auto it = ov.utterances.constFind(lines[i].id);
        if (it == ov.utterances.constEnd() || !it->rechecked || it->corrected || it->confirmed
            || it->ignored)
            return false;
        const QString key = hintKeyAt(i);
        return key.isEmpty() || !hintResolved(i, key);
    }

    int lineCountOf(const QString& key) const
    {
        return int(std::count(assigned.cbegin(), assigned.cend(), key));
    }

    QVector<int> linesOf(const QString& key) const
    {
        QVector<int> out;
        for (int i = 0; i < assigned.size(); ++i)
            if (assigned[i] == key) out.append(i);
        return out;
    }

    // Whether the speaker is visible in the list (= can be the target of an operation).
    bool visible(const QString& key) const
    {
        if (isParticipantKey(key)) return ov.participant(key) != nullptr;
        if (!rawOrder.contains(key) || ov.merged.contains(key)) return false;
        return !(ov.removedRaw.contains(key) && lineCountOf(key) == 0);
    }

    QStringList visibleKeys() const
    {
        QStringList keys;
        for (const QString& raw : rawOrder)
            if (visible(raw)) keys << raw;
        for (const OverlayParticipant& p : ov.participants) keys << p.key;
        return keys;
    }

    QString personOf(const QString& key) const { return speakerPerson(ov, speakerMap, key); }
    QString displayOf(const QString& key) const { return speakerDisplayName(ov, speakerMap, key); }

    // The meeting's (visible) speaker bound to this person; empty if none.
    QString speakerKeyForPerson(const QString& person, const QString& except = QString()) const
    {
        if (person.isEmpty()) return {};
        for (const QString& key : visibleKeys())
            if (key != except && personOf(key).compare(person, Qt::CaseInsensitive) == 0)
                return key;
        return {};
    }

    // The spelling stored in the name list (on a case-insensitive match), otherwise the given one.
    QString canonicalPerson(const QString& name) const
    {
        const QString n = name.trimmed();
        if (n.isEmpty() || !people) return n;
        for (const QString& known : people->names())
            if (known.compare(n, Qt::CaseInsensitive) == 0) return known;
        return n;
    }

    QVector<int> indicesOf(const QStringList& ids) const
    {
        QVector<int> out;
        QSet<int> seen;
        for (const QString& id : ids) {
            const auto it = indexById.constFind(id);
            if (it == indexById.constEnd() || seen.contains(it.value())) continue;
            seen.insert(it.value());
            out.append(it.value());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    EditorUtterance makeUtterance(int i) const
    {
        EditorUtterance u;
        if (i < 0 || i >= lines.size()) return u;
        const TranscriptLine& l = lines[i];
        u.id = l.id;
        u.index = i;
        u.startMs = l.startMs;
        u.endMs = l.endMs;
        u.text = l.text;
        u.rawLabel = l.rawLabel;
        u.speakerKey = assigned[i];
        u.uncertain = uncertain.value(i);
        const auto it = ov.utterances.constFind(l.id);
        if (it != ov.utterances.constEnd()) {
            u.manuallyCorrected = it->corrected;
            u.confirmed = it->confirmed;
        }
        u.noisy = noisyAt(i);
        u.ignored = ignoredAt(i);
        u.noisyOverlap = overlapNoisy.value(i);
        u.rechecked = recheckedAt(i);
        if (u.rechecked) u.likelySpeakerKey = likelyAt(i);
        u.uncertainReason = reasonAt(i);
        return u;
    }

    QVector<EditorSpeaker> buildSpeakers() const
    {
        QHash<QString, QPair<int, qint64>> stats;   // key -> (lines, speaking time)
        qint64 totalMs = 0;
        for (int i = 0; i < lines.size(); ++i) {
            const qint64 dur = qMax<qint64>(0, lines[i].endMs - lines[i].startMs);
            auto& s = stats[assigned[i]];
            ++s.first;
            s.second += dur;
            totalMs += dur;
        }
        auto fill = [&](EditorSpeaker& s) {
            const auto st = stats.value(s.key);
            s.utteranceCount = st.first;
            s.talkTimeMs = st.second;
            s.talkShare = totalMs > 0 ? double(st.second) / double(totalMs) : 0.0;
            s.personName = personOf(s.key);
            s.displayName = displayOf(s.key);
            s.anonymous = s.personName.isEmpty();
            s.isSelf = !s.anonymous && !userName.isEmpty()
                && s.personName.compare(userName, Qt::CaseInsensitive) == 0;
            s.hasVoiceprint = voiceprints && !s.anonymous
                && voiceprints->printCount(s.personName) > 0;
        };

        QVector<EditorSpeaker> out;
        for (int i = 0; i < rawOrder.size(); ++i) {
            const QString& raw = rawOrder[i];
            if (!visible(raw)) continue;
            EditorSpeaker s;
            s.key = raw;
            s.rawLabel = raw;
            s.colorIndex = i;
            fill(s);
            // The voice-identification score is valid only while the same person stands there.
            const auto id = ov.identified.constFind(raw);
            if (id != ov.identified.constEnd() && !s.anonymous && id->person == s.personName)
                s.voiceConfidence = id->score;
            out.append(s);
        }
        for (const OverlayParticipant& p : ov.participants) {
            EditorSpeaker s;
            s.key = p.key;
            s.added = true;
            s.colorIndex = p.colorIndex;
            fill(s);
            out.append(s);
        }
        return out;
    }

    // ---- uncertainty -----------------------------------------------------
    // groupByPerson: speakers bound to the same PERSON (e.g. two raw labels for the same human) count as
    // one voice: relative to each other they are not "another speaker".
    QVector<AnalysisLine> analysisLines(QHash<QString, int>* keyIndex, bool groupByPerson) const
    {
        QHash<QString, int> idx;
        QHash<QString, QString> groupOf;
        QVector<AnalysisLine> al(lines.size());
        for (int i = 0; i < lines.size(); ++i) {
            const TranscriptLine& l = lines[i];
            QString group = assigned[i];
            if (groupByPerson) {
                auto g = groupOf.find(assigned[i]);
                if (g == groupOf.end()) {
                    const QString person = personOf(assigned[i]);
                    g = groupOf.insert(assigned[i], person.isEmpty()
                            ? assigned[i] : QStringLiteral("person:") + person.toCaseFolded());
                }
                group = g.value();
            }
            auto it = idx.find(group);
            if (it == idx.end()) it = idx.insert(group, idx.size());
            al[i].speaker = it.value();
            al[i].durationMs = l.endMs - l.startMs;
            const auto c = fused.constFind(l.id);
            if (c != fused.constEnd()) al[i].embedding = &c.value();
            const auto o = ov.utterances.constFind(l.id);
            al[i].locked = o != ov.utterances.constEnd() && (o->corrected || o->confirmed);
            al[i].noisy = noisyAt(i);
            al[i].ignored = ignoredAt(i);     // hasEmbedding() is false then: every branch skips it
        }
        if (keyIndex) *keyIndex = idx;
        return al;
    }

    void recomputeUncertain()
    {
        if (fused.isEmpty()) {
            uncertain.fill(false, lines.size());
        } else {
            QHash<QString, int> idx;
            const QVector<AnalysisLine> al = analysisLines(&idx, /*groupByPerson*/ true);
            uncertain = computeUncertain(al, idx.size());
        }
        // The recheck's flags (persistent) stay independent of the voice analysis.
        for (int i = 0; i < lines.size(); ++i)
            if (recheckedAt(i)) uncertain[i] = true;
        voiceUncertain = uncertain;
        // A track contradiction is also a reason for uncertainty.
        for (int i = 0; i < lines.size(); ++i)
            if (sideConflictAt(i)) uncertain[i] = true;
        // An "unidentifiable" line is never uncertain (the user has already decided about it).
        for (int i = 0; i < lines.size(); ++i)
            if (ignoredAt(i)) uncertain[i] = voiceUncertain[i] = false;
    }

    bool lockedAt(int i) const
    {
        const auto it = ov.utterances.constFind(lines[i].id);
        return it != ov.utterances.constEnd() && (it->corrected || it->confirmed);
    }

    // Per the latest analysis the line sounded on a track opposite its CURRENT speaker's side.
    bool sideConflictAt(int i) const
    {
        const auto it = review.sideConflicts.constFind(lines[i].id);
        return it != review.sideConflicts.constEnd() && it.value() == assigned.value(i) && !lockedAt(i)
            && !ignoredAt(i);
    }

    // The person's tag evidence on this meeting (on the main thread; from the cache cleared per analysis).
    library::TagEvidence tagEvidenceOf(const QString& person) const
    {
        if (!tagEvidence || person.isEmpty() || meetingTags.isEmpty()) return {};
        const QString k = person.toCaseFolded();
        auto it = tagEvidenceCache.constFind(k);
        if (it == tagEvidenceCache.constEnd()) it = tagEvidenceCache.insert(k, tagEvidence(person, meetingTags, meetingId));
        return *it;
    }

    // The tags of the speaker's person contradict the meeting (after the strict guards;
    // PersonTagSnapshot::evidence).
    bool tagMismatch(const QString& key) const
    {
        return tagEvidenceOf(personOf(key)).isContradiction();
    }

    QString reasonAt(int i) const
    {
        if (!uncertain.value(i)) return {};
        if (sideConflictAt(i)) return QStringLiteral("side");
        if (voiceUncertain.value(i) && tagMismatch(assigned.value(i))) return QStringLiteral("tag");
        return QStringLiteral("voice");
    }

    // A sample's (sampleRef) voiceprints with the models in use, as a fused vector. Sibling voiceprints
    // (the same sample, another model) belong together by sampleRef + source meeting + createdAt; a sample
    // lacking a voiceprint for any model is dropped.
    struct FusedPrint {
        QVector<float> vec;
        QString sourceMeetingId;
        QString sampleRef;
    };
    QVector<FusedPrint> fusedPrints(const QString& person) const
    {
        struct Group { QString key; EmbeddingSet set; FusedPrint meta; };
        QVector<Group> groups;
        for (const Voiceprint& vp : voiceprints->printsFor(person)) {
            if (!modelIds.contains(vp.model)) continue;
            const QString key = vp.sampleRef + QChar(0x1f) + vp.sourceMeetingId + QChar(0x1f) + vp.createdAt;
            Group* g = nullptr;
            for (Group& cand : groups)
                if (cand.key == key && !cand.set.contains(vp.model)) { g = &cand; break; }
            if (!g) {
                groups.append(Group{key, {}, FusedPrint{{}, vp.sourceMeetingId, vp.sampleRef}});
                g = &groups.last();
            }
            g->set.insert(vp.model, vp.embedding);
        }
        QVector<FusedPrint> out;
        for (Group& g : groups) {
            g.meta.vec = processing::fusion::fuse(g.set, modelIds);
            if (!g.meta.vec.isEmpty()) out.append(g.meta);
        }
        return out;
    }

    // The stored voiceprints for the rechecks' reference, per speaker index (per the indexing of al): the
    // named speaker's person's voiceprints. One made in this meeting is "local" (local evidence); if its
    // sample line (the sampleRef's start time, as in fixVoiceprints) is a locked line of this speaker, that line
    // does not count separately in the reference (replacedLines): the same voice must not count twice.
    QVector<SpeakerPrior> priorsFor(const QVector<AnalysisLine>& al, int speakerCount) const
    {
        QVector<SpeakerPrior> out(speakerCount);
        if (!voiceprints || fused.isEmpty()) return out;
        const int dim = fused.constBegin()->size();
        QVector<QString> personOfIdx(speakerCount);
        for (int i = 0; i < al.size(); ++i) {
            const int s = al[i].speaker;
            if (s >= 0 && s < speakerCount && personOfIdx[s].isEmpty())
                personOfIdx[s] = personOf(assigned[i]);
        }
        for (int s = 0; s < speakerCount; ++s) {
            if (personOfIdx[s].isEmpty()) continue;
            SpeakerPrior& p = out[s];
            for (const FusedPrint& vp : fusedPrints(personOfIdx[s])) {
                if (vp.vec.size() != dim) continue;   // not comparable (another model composition)
                if (vp.sourceMeetingId != meetingId) {
                    p.vectors.append(vp.vec);
                    continue;
                }
                p.localVectors.append(vp.vec);
                const qint64 start = sampleRefStartMs(vp.sampleRef);
                if (start < 0) continue;
                for (int i = 0; i < al.size(); ++i) {
                    if (al[i].speaker != s || !al[i].locked) continue;
                    if (lines[i].startMs <= start && start < lines[i].endMs) {
                        p.replacedLines.append(i);
                        break;
                    }
                }
            }
        }
        return out;
    }

    // ReferenceInfo -> the composition for the UI.
    SpeakerReference referenceOf(const QString& key, const ReferenceInfo& info) const
    {
        SpeakerReference r;
        r.speakerKey = key;
        r.name = displayOf(key);
        r.lines = info.lines;
        r.localPrints = info.localPrints;
        r.priorPrints = info.priorPrints;
        r.priorOnly = info.refPrior();
        r.fallback = info.fallback();
        r.priorCapped = info.priorCapped;
        return r;
    }

    // Embedded lines exist, and at least one speaker has a reliable core (from locked lines or a local voiceprint).
    bool hasCore() const
    {
        if (fused.isEmpty()) return false;
        QHash<QString, int> idx;
        const QVector<AnalysisLine> al = analysisLines(&idx, /*groupByPerson*/ true);
        return hasTrustedCore(al, idx.size(), priorsFor(al, idx.size()));
    }

    int uncertainCount() const
    {
        return int(std::count(uncertain.cbegin(), uncertain.cend(), true));
    }

    // ---- step machinery ------------------------------------------------------
    QVector<RowState> rows() const
    {
        QVector<RowState> out(lines.size());
        for (int i = 0; i < lines.size(); ++i) {
            out[i].speaker = assigned[i];
            out[i].uncertain = uncertain.value(i);
            out[i].noisy = noisyAt(i);
            out[i].ignored = ignoredAt(i);
            out[i].likely = recheckedAt(i) ? likelyAt(i) : QString();
            out[i].reason = reasonAt(i);
            const auto it = ov.utterances.constFind(lines[i].id);
            if (it != ov.utterances.constEnd()) {
                out[i].corrected = it->corrected;
                out[i].confirmed = it->confirmed;
            }
        }
        return out;
    }

    QStringList rowNames() const
    {
        QStringList out;
        out.reserve(lines.size());
        QHash<QString, QString> memo;
        for (const QString& key : assigned) {
            auto it = memo.find(key);
            if (it == memo.end()) it = memo.insert(key, displayOf(key));
            out << it.value();
        }
        return out;
    }

    SummaryStaleInfo staleInfo() const
    {
        SummaryStaleInfo info;
        if (!hasSummary) return info;
        info.correctedSpeakers = ov.changedSinceSummary.size();
        info.stale = info.correctedSpeakers > 0;
        return info;
    }

    Step begin()
    {
        // The meeting's fresh state (the speakerMap may also be written by the old UI / auto-identification).
        if (store) {
            const Meeting m = store->load(meetingId);
            if (!m.id.isEmpty()) {
                speakerMap = m.speakerMap;
                hasSummary = m.hasSummary;
            }
        }
        Step s;
        s.before = ov;
        s.mapBefore = speakerMap;
        s.rows = rows();
        s.names = rowNames();
        s.stale = staleInfo();
        return s;
    }

    // The merged raw labels should show the target person to the old UI/CLI too.
    void syncLegacyMap()
    {
        for (auto it = ov.merged.constBegin(); it != ov.merged.constEnd(); ++it) {
            const QString person = personOf(it.value());
            if (person.isEmpty()) speakerMap.remove(it.key());
            else speakerMap.insert(it.key(), person);
        }
    }

    static QVector<MapDelta> diffMap(const QMap<QString, QString>& before,
                                     const QMap<QString, QString>& after)
    {
        QVector<MapDelta> out;
        QSet<QString> keys;
        for (auto it = before.constBegin(); it != before.constEnd(); ++it) keys.insert(it.key());
        for (auto it = after.constBegin(); it != after.constEnd(); ++it) keys.insert(it.key());
        for (const QString& k : std::as_const(keys)) {
            MapDelta dl;
            dl.raw = k;
            dl.hadBefore = before.contains(k);
            dl.before = before.value(k);
            dl.hasAfter = after.contains(k);
            dl.after = after.value(k);
            if (dl.hadBefore != dl.hasAfter || dl.before != dl.after) out.append(dl);
        }
        return out;
    }

    // Writing out the overlay + speakerMap change, refreshing transcript.md.
    void persist(const QVector<MapDelta>& delta, bool forward, bool namesChanged)
    {
        tanara::PerfScope perfScope("SpeakerEditor::persist", 10);
        if (folder.isEmpty()) return;
        saveOverlay(folder, ov);
        if (!store) return;
        Meeting m = store->load(meetingId);
        if (m.id.isEmpty()) return;
        if (!delta.isEmpty()) {
            for (const MapDelta& dl : delta) {
                const bool has = forward ? dl.hasAfter : dl.hadBefore;
                const QString& val = forward ? dl.after : dl.before;
                if (has) m.speakerMap.insert(dl.raw, val);
                else m.speakerMap.remove(dl.raw);
            }
            store->saveMeeting(m);
        }
        speakerMap = m.speakerMap;
        hasSummary = m.hasSummary;
        // transcript.md (the human-readable export) is refreshed with the resolved names, but DELAYED: rereading
        // the tokens + writing the whole file grows with the meeting's length (30-80 ms), and for per-line
        // corrections it is superfluous at every step. The summary works not from this but from the tokens +
        // the (immediately saved) overlay.
        if (namesChanged) scheduleMarkdown();
    }

    void scheduleMarkdown()
    {
        markdownPending = true;
        if (!markdownTimer) {
            markdownTimer = new QTimer(q);
            markdownTimer->setSingleShot(true);
            markdownTimer->setInterval(kMarkdownDelayMs);
            QObject::connect(markdownTimer, &QTimer::timeout, q, [this] { flushMarkdown(); });
        }
        markdownTimer->start();
    }

    void flushMarkdown()
    {
        tanara::PerfScope perfScope("SpeakerEditor::flushMarkdown (indítás)", 10);
        if (!markdownPending || markdownRunning) return;
        markdownPending = false;
        if (markdownTimer) markdownTimer->stop();
        if (!store) return;
        const Meeting m = store->load(meetingId);
        if (m.id.isEmpty() || !QDir(m.folder).exists()) return;
        // The overlay is already on disk (persist saves it), so the background thread only reads files and
        // writes transcript.md; it does not touch Impl.
        markdownRunning = true;
        ++markdownInFlight;
        QPointer<SpeakerEditor> self(q);
        QThreadPool::globalInstance()->start([this, self, m] {
            {
                tanara::PerfScope bg("SpeakerEditor: transcript.md (háttérszál)", 10);
                regenerateTranscriptMarkdown(m);
            }
            --markdownInFlight;
            QMetaObject::invokeMethod(qApp, [this, self] {
                if (!self) return;
                markdownRunning = false;
                if (markdownPending) flushMarkdown();   // a newer correction arrived meanwhile
            }, Qt::QueuedConnection);
        });
    }

    // On close (destructor): we wait for the running background write so the file is not left half-written.
    void waitMarkdown()
    {
        while (markdownInFlight.load() > 0) QThread::msleep(5);
    }

    void clearSuggestion()
    {
        if (!suggestion.isValid() && !pairOffer.isValid()) return;
        suggestion = SpeakerSuggestion();
        pairOffer = PairRecheckOffer();
        emit q->suggestionChanged();
    }

    static QString pairId(const QString& a, const QString& b)
    {
        return a < b ? a + QChar(0x1f) + b : b + QChar(0x1f) + a;
    }

    // The speaker's locked (confirmed / corrected), clean, embedded lines: the pairwise review's reference is built from these.
    int lockedVoiceLines(const QString& key) const
    {
        int n = 0;
        for (int i = 0; i < lines.size(); ++i) {
            if (assigned[i] != key || noisyAt(i) || ignoredAt(i)) continue;
            if (!fused.contains(lines[i].id)) continue;
            const auto o = ov.utterances.constFind(lines[i].id);
            if (o != ov.utterances.constEnd() && (o->corrected || o->confirmed)) ++n;
        }
        return n;
    }

    // The pairwise review's analysis (the speaker keys directly, without person grouping).
    PairRecheckAnalysis pairAnalysis(const QString& keyA, const QString& keyB) const
    {
        QHash<QString, int> idx;
        const QVector<AnalysisLine> al = analysisLines(&idx, /*groupByPerson*/ false);
        return computePairRecheck(al, idx.value(keyA, -1), idx.value(keyB, -1),
                                  priorsFor(al, idx.size()));
    }

    // The "similar lines" guard fell silent: if the two speakers are named and both have enough confirmed
    // lines, we offer a review between the two.
    void offerPair(const QString& source, const QString& target, double similarity)
    {
        const QString ps = personOf(source), pt = personOf(target);
        if (ps.isEmpty() || pt.isEmpty() || ps.compare(pt, Qt::CaseInsensitive) == 0) return;
        if (declinedPairs.contains(pairId(source, target))) return;
        if (lockedVoiceLines(source) < kMinSpeakerLines || lockedVoiceLines(target) < kMinSpeakerLines)
            return;
        pairOffer.sourceSpeakerKey = source;
        pairOffer.targetSpeakerKey = target;
        pairOffer.centroidSimilarity = similarity;
        emit q->suggestionChanged();
    }

    // What changed relative to the pre-operation state -> fine-grained signals.
    void emitChanges(const Step& s, bool mapChanged, bool peopleTouched, bool printsTouched)
    {
        const int uncertainBefore =
            int(std::count_if(s.rows.cbegin(), s.rows.cend(),
                              [](const RowState& r) { return r.uncertain; }));
        const QVector<RowState> now = rows();
        QStringList changed;
        for (int i = 0; i < now.size() && i < s.rows.size(); ++i)
            if (!(now[i] == s.rows[i])) changed << lines[i].id;
        if (!changed.isEmpty()) emit q->utterancesChanged(changed);
        emit q->speakersChanged();
        const int uc = uncertainCount();
        if (uc != uncertainBefore) emit q->uncertainCountChanged(uc);
        emit q->undoStateChanged();
        const SummaryStaleInfo st = staleInfo();
        if (st.stale != s.stale.stale || st.correctedSpeakers != s.stale.correctedSpeakers)
            emit q->summaryStaleChanged(st.stale, st.correctedSpeakers);
        if (mapChanged) emit q->speakerMapChanged(meetingId);
        if (peopleTouched) emit q->peopleChanged();
        if (printsTouched) emit q->voiceprintsChanged();
        updateSampleStats();
    }

    // ---- transparency of sample training -------------------------------------------
    // A sample line: locked, not noisy, not "unidentifiable", and long enough to embed.
    bool sampleAt(int i) const
    {
        return lockedAt(i) && !noisyAt(i) && !ignoredAt(i)
            && lines[i].endMs - lines[i].startMs >= kMinEmbedMs;
    }

    void snapshotSessionBaseline()
    {
        sessionBaseline.clear();
        for (int i = 0; i < lines.size(); ++i)
            if (sampleAt(i)) sessionBaseline.insert(lines[i].id, assigned[i]);
        lastStats = computeSampleStats();
    }

    QVector<SpeakerSampleStats> computeSampleStats() const
    {
        QVector<SpeakerSampleStats> out;
        QHash<QString, int> at;
        for (const EditorSpeaker& sp : buildSpeakers()) {
            SpeakerSampleStats st;
            st.speakerKey = sp.key;
            st.personName = sp.personName;
            st.displayName = sp.displayName;
            at.insert(sp.key, int(out.size()));
            out.append(st);
        }
        for (int i = 0; i < lines.size(); ++i) {
            if (!sampleAt(i)) continue;
            const auto it = at.constFind(assigned[i]);
            if (it == at.constEnd()) continue;
            SpeakerSampleStats& st = out[it.value()];
            ++st.sampleLines;
            st.sampleMs += lines[i].endMs - lines[i].startMs;
            const auto b = sessionBaseline.constFind(lines[i].id);
            if (b == sessionBaseline.constEnd() || b.value() != assigned[i]) {
                ++st.sessionAdded;
                st.sessionAddedIds << lines[i].id;
            }
        }
        return out;
    }

    void updateSampleStats()
    {
        QVector<SpeakerSampleStats> now = computeSampleStats();
        if (now == lastStats) return;
        lastStats = std::move(now);
        emit q->sampleStatsChanged();
    }

    // Every edit / undo: the earlier deviation flag loses its validity.
    void resetDeviation()
    {
        if (!deviation.isValid()) return;
        deviation = SampleDeviation();
        emit q->sampleDeviationChanged();
    }

    // The sample-giving decision (AFTER the step): of the decided lines, the one farthest from its own
    // speaker's core. The reference per speaker (the same person with several keys together): its locked
    // sample lines, if there are at least kMinSpeakerLines; otherwise all its clean, embedded lines, both
    // WITHOUT the lines just decided (otherwise we would measure against itself).
    void finishDecision(const QVector<int>& decided)
    {
        SampleDeviation worst;
        if (!fused.isEmpty() && !decided.isEmpty()) {
            const QSet<int> dec(decided.cbegin(), decided.cend());
            QHash<QString, QVector<float>> refCache;    // group -> reference (empty = not measurable)
            auto groupOf = [&](const QString& key) {
                const QString person = personOf(key);
                return person.isEmpty() ? key : QStringLiteral("person:") + person.toCaseFolded();
            };
            auto referenceFor = [&](const QString& group) {
                auto it = refCache.constFind(group);
                if (it != refCache.constEnd()) return it.value();
                auto build = [&](bool lockedOnly, QVector<double>& sum) {
                    int n = 0;
                    for (int j = 0; j < lines.size(); ++j) {
                        if (dec.contains(j) || groupOf(assigned[j]) != group) continue;
                        if (noisyAt(j) || ignoredAt(j) || (lockedOnly && !lockedAt(j))) continue;
                        const auto f = fused.constFind(lines[j].id);
                        if (f == fused.constEnd() || f->isEmpty()) continue;
                        if (sum.size() < f->size()) sum.resize(f->size());
                        const double w = double(std::clamp<qint64>(lines[j].endMs - lines[j].startMs,
                                                                   500, kMaxEmbedMs)) / 1000.0;
                        for (int k = 0; k < f->size(); ++k) sum[k] += w * double((*f)[k]);
                        ++n;
                    }
                    return n;
                };
                QVector<double> sum;
                int n = build(/*lockedOnly*/ true, sum);
                if (n < kMinSpeakerLines) { sum.clear(); n = build(false, sum); }
                const QVector<float> ref = n >= kMinSpeakerLines ? ranking::normalized(sum) : QVector<float>();
                refCache.insert(group, ref);
                return ref;
            };
            for (int i : decided) {
                if (!sampleAt(i)) continue;
                const auto f = fused.constFind(lines[i].id);
                if (f == fused.constEnd() || f->isEmpty()) continue;
                const QVector<float> ref = referenceFor(groupOf(assigned[i]));
                if (ref.isEmpty()) continue;
                const double sim = ranking::cosine(ref, *f);
                if (std::isnan(sim)) continue;
                if (!worst.isValid() || sim < worst.similarity) {
                    worst.utteranceId = lines[i].id;
                    worst.speakerKey = assigned[i];
                    worst.similarity = sim;
                }
            }
            worst.deviates = worst.isValid() && worst.similarity < kSampleDeviationFit;
        }
        if (worst == deviation) return;
        deviation = worst;
        emit q->sampleDeviationChanged();
    }

    void commit(Step& s, const QString& text)
    {
        clearSuggestion();
        syncLegacyMap();
        recomputeAssigned();
        if (hasSummary)
            for (const QString& k : std::as_const(s.affectedSpeakers))
                appendUnique(ov.changedSinceSummary, k);

        const QVector<MapDelta> delta = diffMap(s.mapBefore, speakerMap);
        const bool namesChanged = rowNames() != s.names;
        persist(delta, /*forward*/ true, namesChanged);
        recomputeUncertain();

        static std::atomic<quint64> nextSerial{0};
        Command c;
        c.text = text;
        c.serial = ++nextSerial;
        c.before = s.before;
        c.after = ov;
        c.map = delta;
        c.voiceprints = s.voiceprints;
        c.peopleAdded = s.peopleAdded;
        c.affectedSpeakers = s.affectedSpeakers;
        undoStack.append(c);
        if (undoStack.size() > kMaxUndoSteps) undoStack.removeFirst();
        redoStack.clear();

        resetDeviation();
        emitChanges(s, !delta.isEmpty(), !s.peopleAdded.isEmpty(), !s.voiceprints.isEmpty());
        scheduleReview();
    }

    // Restoring an earlier overlay state (undo/redo). Identification is always the current one; the stale
    // flag comes from the snapshot only if no new summary has been made since.
    void restoreOverlay(const SpeakerOverlay& snapshot, const QStringList& affected)
    {
        const SpeakerOverlay cur = ov;
        ov = snapshot;
        ov.identified = cur.identified;
        ov.transcriptFingerprint = cur.transcriptFingerprint;
        if (snapshot.summaryEpoch != cur.summaryEpoch) {
            // A new summary was made meanwhile (or "Fine as is"): relative to it the undo is also a speaker change.
            ov.summaryEpoch = cur.summaryEpoch;
            ov.changedSinceSummary = cur.changedSinceSummary;
            if (hasSummary)
                for (const QString& k : affected) appendUnique(ov.changedSinceSummary, k);
        }
    }

    void apply(const Command& c, bool undoing)
    {
        clearSuggestion();
        Step s = begin();
        restoreOverlay(undoing ? c.before : c.after, c.affectedSpeakers);

        if (voiceprints) {
            if (undoing) {
                for (auto it = c.voiceprints.crbegin(); it != c.voiceprints.crend(); ++it) {
                    if (it->added) voiceprints->removePrint(it->print.id);
                    else voiceprints->addPrint(it->person, it->print);
                }
            } else {
                for (const VoiceprintOp& op : c.voiceprints) {
                    if (op.added) voiceprints->addPrint(op.person, op.print);
                    else voiceprints->removePrint(op.print.id);
                }
            }
        }
        if (people)
            for (const QString& name : c.peopleAdded) {
                if (undoing) people->unlist(name);   // its nicknames / note stay
                else people->add(name);
            }

        recomputeAssigned();
        // The affected keys of the speakerMap in memory too (persist carries them to disk).
        for (const MapDelta& dl : c.map) {
            const bool has = undoing ? dl.hadBefore : dl.hasAfter;
            if (has) speakerMap.insert(dl.raw, undoing ? dl.before : dl.after);
            else speakerMap.remove(dl.raw);
        }
        const bool namesChanged = rowNames() != s.names;
        persist(c.map, /*forward*/ !undoing, namesChanged);
        recomputeUncertain();
        resetDeviation();
        emitChanges(s, !c.map.isEmpty(), !c.peopleAdded.isEmpty(), !c.voiceprints.isEmpty());
        scheduleReview();
    }

    // ---- building blocks of the operations ---------------------------------------------
    QString createParticipant(const QString& person, Step& s)
    {
        OverlayParticipant p;
        p.key = QStringLiteral("participant:%1").arg(ov.nextParticipant);
        // The color follows the order of the recording after the raw labels; it does not change later either.
        p.colorIndex = rawOrder.size() + ov.nextParticipant - 1;
        ++ov.nextParticipant;
        p.person = person;
        if (person.isEmpty())
            p.label = SpeakerEditor::tr("Új beszélő %1").arg(ov.nextAnonymous++);
        ov.participants.append(p);
        rememberPerson(person, s);
        return p.key;
    }

    void rememberPerson(const QString& person, Step& s)
    {
        if (!people || person.isEmpty()) return;
        if (people->names().contains(person, Qt::CaseInsensitive)) return;
        people->add(person);
        s.peopleAdded << person;
    }

    // noisy: "-> Y, not a sample": a "talked over each other" flag in the same step too.
    void moveLines(const QVector<int>& idx, const QString& key, Step& s, bool noisy = false)
    {
        for (int i : idx) {
            OverlayUtterance& u = ov.utterances[lines[i].id];
            u.speaker = key;
            u.corrected = true;     // a manual decision: no longer uncertain
            u.rechecked = false;
            u.recheckHint.clear();
            u.ignored = false;      // we know who said it: identifiable again
            if (noisy) u.noisy = true;
        }
        appendUnique(s.affectedSpeakers, key);
    }

    void setPerson(const QString& key, const QString& person, Step& s)
    {
        if (OverlayParticipant* p = ov.participant(key)) p->person = person;
        else speakerMap.insert(key, person);
        rememberPerson(person, s);
        appendUnique(s.affectedSpeakers, key);
    }

    void mergeInto(const QString& from, const QString& into, Step& s)
    {
        // The lines explicitly set to `from` and the earlier merges pointing to it go to the target.
        for (auto it = ov.utterances.begin(); it != ov.utterances.end(); ++it) {
            if (it->speaker == from) it->speaker = into;
            if (it->recheckHint == from) it->recheckHint = into;
        }
        for (auto it = ov.merged.begin(); it != ov.merged.end(); ++it)
            if (it.value() == from) it.value() = into;
        // The manual track assignment: the target's stays; if it has none, the source's is inherited.
        if (ov.speakerTracks.contains(from)) {
            const QStringList tracks = ov.speakerTracks.take(from);
            if (!ov.speakerTracks.contains(into)) ov.speakerTracks.insert(into, tracks);
        }
        if (isParticipantKey(from)) {
            ov.participants.erase(
                std::remove_if(ov.participants.begin(), ov.participants.end(),
                               [&](const OverlayParticipant& p) { return p.key == from; }),
                ov.participants.end());
        } else {
            ov.merged.insert(from, into);   // every (non-overridden) line of the raw label goes to the target
        }
        appendUnique(s.affectedSpeakers, into);
    }

    // ---- voiceprint -------------------------------------------------------
    // Of the speaker's lines those usable for a voiceprint (longer ones first), up to the target length.
    QVector<int> selectPrintLines(const QVector<int>& speakerLines, qint64* usedMs) const
    {
        QVector<int> cand;
        for (int i : speakerLines) {
            if (ignoredAt(i)) continue;         // "unidentifiable": not a sample
            if (uncertain.value(i)) continue;   // we do not train from a doubtful line
            if (noisyAt(i)) continue;           // talked over each other: not a clean sample
            if (lines[i].endMs - lines[i].startMs >= kPrintMinLineMs) cand.append(i);
        }
        std::sort(cand.begin(), cand.end(), [&](int a, int b) {
            return (lines[a].endMs - lines[a].startMs) > (lines[b].endMs - lines[b].startMs);
        });
        QVector<int> picked;
        qint64 total = 0;
        for (int i : std::as_const(cand)) {
            if (total >= kPrintTargetMs) break;
            total += std::min<qint64>(lines[i].endMs - lines[i].startMs, kMaxEmbedMs);
            picked.append(i);
        }
        if (usedMs) *usedMs = total;
        return picked;
    }

    static void embedWindow(const TranscriptLine& l, qint64* s, qint64* e)
    {
        *s = l.startMs;
        *e = l.endMs;
        if (*e - *s > kMaxEmbedMs) {
            *s = (l.startMs + l.endMs) / 2 - kMaxEmbedMs / 2;
            *e = *s + kMaxEmbedMs;
        }
    }

    // A voiceprint from the given lines (from the cache, or if absent there, computed now): one Voiceprint
    // per model in use (same sampleRef / createdAt). Empty = failed.
    QVector<Voiceprint> buildPrint(const QVector<int>& speakerLines, qint64* usedMs,
                                   int* usedLines, qint64* missingMs)
    {
        qint64 total = 0;
        const QVector<int> picked = selectPrintLines(speakerLines, &total);
        return buildPrintFromPicked(picked, total, kPrintMinTotalMs, usedMs, usedLines, missingMs);
    }

    // A voiceprint from EXPLICITLY given lines (the user chose: "sample from this line"). The filtering is
    // the caller's job; here only the minimum length and the embedding count. Only lines with a vector for
    // every model count.
    QVector<Voiceprint> buildPrintFromPicked(const QVector<int>& picked, qint64 total,
                                             qint64 minTotalMs, qint64* usedMs,
                                             int* usedLines, qint64* missingMs)
    {
        if (usedMs) *usedMs = total;
        if (usedLines) *usedLines = picked.size();
        if (missingMs) *missingMs = std::max<qint64>(0, minTotalMs - total);
        if (total < minTotalMs || modelIds.isEmpty()) return {};

        std::unique_ptr<processing::IUtteranceEmbedder> sync;   // only if something is missing from the cache
        bool syncFailed = false;
        QMap<QString, QVector<double>> sums;        // modelId -> weighted sum
        qint64 embeddedMs = 0, bestDur = -1;
        int best = -1;
        QStringList sourceRefs;   // the windows actually embedded (for lazy backfill)
        for (int i : picked) {
            const TranscriptLine& l = lines[i];
            const QStringList missing = missingModels(l.id);
            if (!missing.isEmpty() && factory && !syncFailed) {
                if (!sync) {
                    sync = factory();
                    if (!sync || !sync->open(audioPath)) { syncFailed = true; sync.reset(); }
                }
                if (sync) {
                    qint64 ws, we;
                    embedWindow(l, &ws, &we);
                    const EmbeddingSet got = sync->embedAll(ws, we, missing);
                    for (auto it = got.cbegin(); it != got.cend(); ++it)
                        if (missing.contains(it.key())) cache.set(it.key(), l.id, it.value());
                    refreshFused(l.id);
                }
            }
            const EmbeddingSet set = cache.setFor(l.id, modelIds);
            if (set.size() != modelIds.size()) continue;   // no vector for every model
            const qint64 dur = std::min<qint64>(l.endMs - l.startMs, kMaxEmbedMs);
            for (auto it = set.cbegin(); it != set.cend(); ++it) {
                QVector<double>& sum = sums[it.key()];
                if (sum.size() < it->size()) sum.resize(it->size());
                for (int k = 0; k < it->size(); ++k) sum[k] += double(dur) * double((*it)[k]);
            }
            embeddedMs += dur;
            {
                qint64 ws, we;
                embedWindow(l, &ws, &we);
                sourceRefs << QStringLiteral("%1#%2-%3").arg(audioRel).arg(ws).arg(we);
            }
            if (l.endMs - l.startMs > bestDur) { bestDur = l.endMs - l.startMs; best = i; }
        }
        if (sync) cache.save(folder);
        if (embeddedMs < minTotalMs || best < 0) return {};

        const QString sampleRef = QStringLiteral("%1#%2-%3").arg(audioRel)
                                      .arg(lines[best].startMs).arg(lines[best].endMs);
        const QString createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
        QVector<Voiceprint> out;
        for (auto it = sums.cbegin(); it != sums.cend(); ++it) {
            Voiceprint vp;
            vp.embedding.resize(it->size());
            for (int k = 0; k < it->size(); ++k) vp.embedding[k] = float((*it)[k] / double(embeddedMs));
            vp.embedding = library::VoiceprintStore::l2normalize(vp.embedding);
            vp.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            vp.dim = vp.embedding.size();
            vp.model = it.key();
            vp.sourceMeetingId = meetingId;
            vp.sourceTrack = QStringLiteral("mixdown");
            vp.sampleRef = sampleRef;
            vp.sourceRefs = sourceRefs;
            vp.createdAt = createdAt;
            out.append(vp);
        }
        if (usedMs) *usedMs = embeddedMs;
        return out;
    }

    // "Wrong recognition": the speaker's samples HERE out of the former person's voiceprints, and (if there
    // is a new person) into theirs. Every step goes into the Step for undo.
    void fixVoiceprints(const QVector<int>& speakerLines, const QString& oldPerson,
                        const QString& newPerson, bool othersHaveOldPerson, Step& s)
    {
        if (!voiceprints || oldPerson.isEmpty()) return;
        QVector<Voiceprint> removed;
        for (const Voiceprint& p : voiceprints->printsFor(oldPerson)) {
            if (p.sourceMeetingId != meetingId) continue;
            const qint64 start = sampleRefStartMs(p.sampleRef);
            bool mine = false;
            if (start >= 0) {
                // Whether the sample is from one of this speaker's lines.
                for (int i : speakerLines)
                    if (lines[i].startMs <= start && start < lines[i].endMs) { mine = true; break; }
            } else {
                // Not attachable to a line: we take it away only if no other speaker of the meeting is this person.
                mine = !othersHaveOldPerson;
            }
            if (mine && voiceprints->removePrint(p.id)) {
                removed.append(p);
                s.voiceprints.append(VoiceprintOp{false, oldPerson, p});
            }
        }
        if (newPerson.isEmpty()) return;
        // A fresh voiceprint for the chosen person from the speaker's lines; if there is not enough material for
        // that (or no model), we move the taken samples to them.
        const QVector<Voiceprint> built = buildPrint(speakerLines, nullptr, nullptr, nullptr);
        if (!built.isEmpty()) {
            for (const Voiceprint& p : built) {
                voiceprints->addPrint(newPerson, p);
                s.voiceprints.append(VoiceprintOp{true, newPerson, p});
            }
        } else {
            for (const Voiceprint& p : std::as_const(removed)) {
                voiceprints->addPrint(newPerson, p);
                s.voiceprints.append(VoiceprintOp{true, newPerson, p});
            }
        }
    }

    // ---- suggestion -----------------------------------------------------------
    void proposeSuggestion(const Step& s, const QVector<int>& moved, const QString& target)
    {
        if (fused.isEmpty() || moved.isEmpty()) return;
        // The most frequent source speaker among the moved lines.
        QHash<QString, int> freq;
        for (int i : moved) ++freq[s.rows[i].speaker];
        QString source;
        int best = 0;
        for (auto it = freq.constBegin(); it != freq.constEnd(); ++it)
            if (it.value() > best && it.key() != target) { best = it.value(); source = it.key(); }
        if (source.isEmpty()) return;

        QHash<QString, int> idx;
        const QVector<AnalysisLine> al = analysisLines(&idx, /*groupByPerson*/ false);
        if (!idx.contains(source) || !idx.contains(target)) return;
        const SuggestOutcome outcome = suggestSimilarDetailed(al, idx.value(source), idx.value(target));
        const QVector<int>& hits = outcome.lines;
        if (hits.isEmpty()) {
            if (outcome.blockedBySimilarity) offerPair(source, target, outcome.centroidSimilarity);
            return;
        }

        SpeakerSuggestion sg;
        sg.sourceSpeakerKey = source;
        sg.targetSpeaker = target;
        sg.anchorUtteranceId = lines[moved.last()].id;
        for (int i : hits) sg.utteranceIds << lines[i].id;
        suggestion = sg;
        emit q->suggestionChanged();
    }

    // ---- embedding thread -----------------------------------------------------
    void stopThread(bool notify)
    {
        if (!thread) return;
        if (cancel) cancel->store(true);
        thread->wait();
        delete thread;
        thread = nullptr;
        ++epoch;    // results queued up are no longer interesting
        if (!folder.isEmpty() && !cache.isEmpty()) cache.save(folder);
        if (notify) emit q->embeddingRunningChanged(false);
    }

    void onBatch(int forEpoch, const QVector<QPair<QString, EmbeddingSet>>& batch)
    {
        if (forEpoch != epoch) return;
        for (const auto& item : batch) {
            for (auto it = item.second.cbegin(); it != item.second.cend(); ++it)
                cache.set(it.key(), item.first, it.value());
            refreshFused(item.first);
        }
        embDone += batch.size();
        emit q->embeddingProgress(embDone, embTotal);
    }

    void onFinished(int forEpoch, bool complete, const QString& error)
    {
        if (forEpoch != epoch || !thread) return;
        thread->wait();
        delete thread;
        thread = nullptr;
        embError = error;
        if (!folder.isEmpty() && !cache.isEmpty()) cache.save(folder);

        const QVector<bool> before = uncertain;
        recomputeUncertain();
        QStringList changed;
        for (int i = 0; i < lines.size(); ++i)
            if (uncertain.value(i) != before.value(i)) changed << lines[i].id;
        emit q->embeddingRunningChanged(false);
        if (!changed.isEmpty()) {
            emit q->utterancesChanged(changed);
            emit q->uncertainCountChanged(uncertainCount());
        }
        emit q->embeddingFinished(complete);
        scheduleReview();
    }

    // ---- background analysis (track side + candidates + "To review" groups) ----------
    void scheduleReview()
    {
        if (lines.isEmpty() || folder.isEmpty() || reviewScheduled) return;
        reviewScheduled = true;
        QTimer::singleShot(0, q, [this] {
            reviewScheduled = false;
            startReview();
        });
    }

    // The snapshot is on the main thread (the stores and the tag seam are not thread-safe).
    ReviewInput makeInput() const
    {
        ReviewInput in;
        if (store) in.meeting = store->load(meetingId);
        in.meeting.speakerMap = speakerMap;
        if (in.meeting.folder.isEmpty()) in.meeting.folder = folder;
        in.lines = lines;
        in.overlay = ov;
        in.embeddings = fused;
        in.userName = userName;
        if (voiceprints && !fused.isEmpty()) {
            const int dim = fused.constBegin()->size();
            for (const QString& person : voiceprints->people()) {
                QVector<QVector<float>> vecs;
                for (const FusedPrint& fp : fusedPrints(person))
                    if (fp.vec.size() == dim) vecs.append(fp.vec);
                if (!vecs.isEmpty()) in.personPrints.insert(person, vecs);
            }
        }
        if (tagEvidence) {
            tagEvidenceCache.clear();
            QHash<QString, library::TagEvidence> tags;
            QStringList persons = in.personPrints.keys();
            for (const QString& key : visibleKeys()) persons << personOf(key);
            for (const QString& p : std::as_const(persons))
                if (!p.isEmpty() && !tags.contains(p.toCaseFolded())) tags.insert(p.toCaseFolded(), tagEvidenceOf(p));
            in.tagEvidence = [tags](const QString& p, const QStringList&, const QString&) {
                return tags.value(p.toCaseFolded());
            };
        }
        if (people) {
            const QHash<QString, QString> learned = people->defaultSides();
            for (auto it = learned.cbegin(); it != learned.cend(); ++it)
                in.sideHints.learnedSides.insert(it.key().toCaseFolded(), sideFromName(it.value()));
        }
        if (!newPersonKey.isEmpty() && visible(newPersonKey)) in.newPersonKey = newPersonKey;
        return in;
    }

    void startReview()
    {
        if (lines.isEmpty() || folder.isEmpty()) return;
        if (reviewRunning) { reviewPending = true; return; }
        reviewRunning = true;
        reviewPending = false;
        emit q->reviewRunningChanged(true);
        const int ep = reviewEpoch;
        const ReviewInput in = makeInput();
        const std::optional<MeetingActivity> cached = activity;
        const SpeakerEditor::ActivityProvider provider = activityProvider;
        QPointer<SpeakerEditor> self(q);
        auto job = [this, self, ep, in, cached, provider] {
            const MeetingActivity act = cached ? *cached : provider ? provider(in.meeting) : defaultActivity(in.meeting);
            ReviewResult result = analyzeReview(in, act);
            QMetaObject::invokeMethod(qApp, [this, self, ep, act, result = std::move(result)]() mutable {
                if (!self) return;
                onReview(ep, act, std::move(result));
            }, Qt::QueuedConnection);
        };
        QThreadPool::globalInstance()->start(job);
    }

    void onReview(int ep, const MeetingActivity& act, ReviewResult result)
    {
        reviewRunning = false;
        if (ep == reviewEpoch) activity = act;
        if (ep != reviewEpoch || reviewPending) {
            // Something changed meanwhile: again on the fresh state.
            emit q->reviewRunningChanged(false);
            reviewPending = false;
            startReview();
            return;
        }
        const QVector<RowState> before = rows();
        const int uncertainBefore = uncertainCount();
        review = std::move(result);
        recomputeUncertain();
        const QVector<RowState> now = rows();
        QStringList changed;
        for (int i = 0; i < now.size() && i < before.size(); ++i)
            if (!(now[i] == before[i])) changed << lines[i].id;
        if (!changed.isEmpty()) emit q->utterancesChanged(changed);
        if (uncertainCount() != uncertainBefore) emit q->uncertainCountChanged(uncertainCount());
        // The learned track sides of people.json (from confirmed lines / manual assignment).
        if (people) {
            bool any = false;
            const QHash<QString, Side> learned = sides::learnedDefaults(review.sides);
            for (auto it = learned.cbegin(); it != learned.cend(); ++it)
                any |= people->setDefaultSide(it.key(), sideName(it.value()));
            if (any) emit q->peopleChanged();
        }
        emit q->reviewRunningChanged(false);
        emit q->reviewGroupsChanged();
    }

    // Moving lines to a target in ONE step: an existing key, a person (their existing speaker or a new
    // participant), or a new anonymous participant.
    bool moveToTarget(const QVector<int>& idx, const QString& key, const QString& person, const QString& label)
    {
        QString target = (!key.isEmpty() && visible(key)) ? key : QString();
        if (target.isEmpty() && !person.isEmpty()) target = speakerKeyForPerson(canonicalPerson(person));
        QVector<int> move;
        for (int i : idx)
            if (target.isEmpty() || assigned[i] != target) move.append(i);
        if (move.isEmpty()) return false;
        Step s = begin();
        if (target.isEmpty()) target = createParticipant(canonicalPerson(person), s);
        moveLines(move, target, s);
        commit(s, SpeakerEditor::tr("%1: %n sor ide: %2", nullptr, move.size()).arg(label, displayOf(target)));
        return true;
    }
};

// =============================================================================

SpeakerEditor::SpeakerEditor(library::MeetingRepository* store, library::PeopleStore* people, library::VoiceprintStore* voiceprints,
                             const QString& meetingId, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->q = this;
    d->store = store;
    d->people = people;
    d->voiceprints = voiceprints;
    d->meetingId = meetingId;
    d->load();
    d->snapshotSessionBaseline();
}

SpeakerEditor::~SpeakerEditor()
{
    if (d->markdownTimer) d->markdownTimer->stop();
    d->waitMarkdown();   // the background write must not run on a half-destroyed object

    d->stopThread(/*notify*/ false);
    // The pending transcript.md write is synchronous here: a background run started now would write the
    // already destroyed Private's counter (markdownInFlight).
    if (d->markdownPending && d->store) {
        d->markdownPending = false;
        const Meeting m = d->store->load(d->meetingId);
        if (!m.id.isEmpty() && QDir(m.folder).exists()) regenerateTranscriptMarkdown(m);
    }
}

void SpeakerEditor::flushPendingWrites()
{
    // Synchronous semantics (close, tests, CLI): we start the background write and also wait for it; and
    // if a newer request came meanwhile, for that too.
    d->flushMarkdown();
    d->waitMarkdown();
    while (d->markdownPending || d->markdownRunning) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        d->waitMarkdown();
    }
}

QString SpeakerEditor::meetingId() const { return d->meetingId; }
bool SpeakerEditor::hasTranscript() const { return !d->lines.isEmpty(); }

void SpeakerEditor::setUserSpeakerName(const QString& name)
{
    if (d->userName == name.trimmed()) return;
    d->userName = name.trimmed();
    emit speakersChanged();
    d->scheduleReview();
}

void SpeakerEditor::setEmbedderFactory(processing::UtteranceEmbedderFactory factory)
{
    d->factory = std::move(factory);
}

void SpeakerEditor::setVoiceModelIds(const QStringList& ids)
{
    const QStringList norm = processing::VoiceModelRegistry::normalizeIds(ids);
    if (norm == d->modelIds) return;
    // The running thread's result goes into the cache per model, so it stays valid after a switch.
    d->modelIds = norm;
    d->rebuildFused();
    d->scheduleReview();
    const QVector<bool> before = d->uncertain;
    d->recomputeUncertain();
    QStringList changed;
    for (int i = 0; i < d->lines.size(); ++i)
        if (d->uncertain.value(i) != before.value(i)) changed << d->lines[i].id;
    if (!changed.isEmpty()) {
        emit utterancesChanged(changed);
        emit uncertainCountChanged(d->uncertainCount());
    }
}

QStringList SpeakerEditor::voiceModelIds() const { return d->modelIds; }

// ---- read model ----------------------------------------------------------

int SpeakerEditor::utteranceCount() const { return d->lines.size(); }

QVector<EditorUtterance> SpeakerEditor::utterances() const
{
    QVector<EditorUtterance> out;
    out.reserve(d->lines.size());
    for (int i = 0; i < d->lines.size(); ++i) out.append(d->makeUtterance(i));
    return out;
}

EditorUtterance SpeakerEditor::utteranceAt(int index) const { return d->makeUtterance(index); }

EditorUtterance SpeakerEditor::utterance(const QString& id) const
{
    return d->makeUtterance(d->indexById.value(id, -1));
}

int SpeakerEditor::indexOf(const QString& id) const { return d->indexById.value(id, -1); }

QVector<EditorSpeaker> SpeakerEditor::speakers() const { return d->buildSpeakers(); }

QStringList SpeakerEditor::personAliases(const QString& personName) const
{
    return d->people && !personName.trimmed().isEmpty() ? d->people->aliases(personName) : QStringList();
}

EditorSpeaker SpeakerEditor::speaker(const QString& key) const
{
    for (const EditorSpeaker& s : d->buildSpeakers())
        if (s.key == key) return s;
    return EditorSpeaker();
}

int SpeakerEditor::uncertainCount() const { return d->uncertainCount(); }

QStringList SpeakerEditor::uncertainUtteranceIds() const
{
    QStringList out;
    for (int i = 0; i < d->lines.size(); ++i)
        if (d->uncertain.value(i)) out << d->lines[i].id;
    return out;
}

// ---- operations --------------------------------------------------------------

bool SpeakerEditor::moveUtterances(const QStringList& utteranceIds, const QString& speakerKey,
                                   bool noisy)
{
    if (!d->visible(speakerKey)) return false;
    QVector<int> idx;
    for (int i : d->indicesOf(utteranceIds))
        if (d->assigned[i] != speakerKey) idx.append(i);
    if (idx.isEmpty()) return false;

    Step s = d->begin();
    d->moveLines(idx, speakerKey, s, noisy);
    d->commit(s, noisy ? tr("%n sor áthelyezése ide: %1 (nem hangminta)", nullptr, idx.size())
                             .arg(d->displayOf(speakerKey))
                       : tr("%n sor áthelyezése ide: %1", nullptr, idx.size())
                             .arg(d->displayOf(speakerKey)));
    // When moving as a non-sample, the line's voice is not reliable: neither a suggestion nor a deviation measurement.
    if (noisy) return true;
    d->finishDecision(idx);
    d->proposeSuggestion(s, idx, speakerKey);
    return true;
}

QString SpeakerEditor::moveUtterancesToPerson(const QStringList& utteranceIds,
                                              const QString& personName, bool noisy)
{
    const QString person = d->canonicalPerson(personName);
    if (person.isEmpty()) return {};
    const QString existing = d->speakerKeyForPerson(person);
    if (!existing.isEmpty()) {
        moveUtterances(utteranceIds, existing, noisy);
        return existing;
    }
    const QVector<int> idx = d->indicesOf(utteranceIds);
    if (idx.isEmpty()) return {};

    Step s = d->begin();
    const QString key = d->createParticipant(person, s);
    d->moveLines(idx, key, s, noisy);
    d->newPersonKey = key;
    d->commit(s, noisy ? tr("%n sor áthelyezése ide: %1 (nem hangminta)", nullptr, idx.size()).arg(person)
                       : tr("%n sor áthelyezése ide: %1", nullptr, idx.size()).arg(person));
    if (noisy) return key;
    d->finishDecision(idx);
    d->proposeSuggestion(s, idx, key);
    return key;
}

QString SpeakerEditor::moveUtterancesToNewParticipant(const QStringList& utteranceIds, bool noisy)
{
    const QVector<int> idx = d->indicesOf(utteranceIds);
    if (idx.isEmpty()) return {};

    Step s = d->begin();
    const QString key = d->createParticipant(QString(), s);
    d->moveLines(idx, key, s, noisy);
    d->newPersonKey = key;
    d->commit(s, noisy ? tr("%n sor áthelyezése ide: %1 (nem hangminta)", nullptr, idx.size()).arg(d->displayOf(key))
                       : tr("%n sor áthelyezése ide: %1", nullptr, idx.size()).arg(d->displayOf(key)));
    if (noisy) return key;
    d->finishDecision(idx);
    d->proposeSuggestion(s, idx, key);
    return key;
}

bool SpeakerEditor::reassignSpeaker(const QString& speakerKey, const QString& personName,
                                    bool fixVoiceprints)
{
    if (!d->visible(speakerKey)) return false;
    const QString person = d->canonicalPerson(personName);
    if (person.isEmpty()) return false;

    Step s = d->begin();
    const QString oldPerson = d->personOf(speakerKey);
    if (oldPerson.compare(person, Qt::CaseInsensitive) == 0) return false;
    const QString oldName = d->displayOf(speakerKey);
    const QVector<int> speakerLines = d->linesOf(speakerKey);
    const bool othersHaveOld = !d->speakerKeyForPerson(oldPerson, speakerKey).isEmpty();

    // If the person is already a speaker of the meeting, there would be two columns for the same human, so a merge.
    const QString existing = d->speakerKeyForPerson(person, speakerKey);
    if (!existing.isEmpty()) d->mergeInto(speakerKey, existing, s);
    else d->setPerson(speakerKey, person, s);

    if (fixVoiceprints)
        d->fixVoiceprints(speakerLines, oldPerson, person, othersHaveOld, s);
    d->commit(s, tr("Beszélő cseréje: %1 → %2").arg(oldName, person));
    return true;
}

bool SpeakerEditor::revertSpeakerToAnonymous(const QString& speakerKey, bool fixVoiceprints)
{
    if (!d->visible(speakerKey)) return false;
    Step s = d->begin();
    const QString oldPerson = d->personOf(speakerKey);
    if (oldPerson.isEmpty()) return false;
    const QVector<int> speakerLines = d->linesOf(speakerKey);
    const bool othersHaveOld = !d->speakerKeyForPerson(oldPerson, speakerKey).isEmpty();

    if (OverlayParticipant* p = d->ov.participant(speakerKey)) {
        p->person.clear();
        if (p->label.isEmpty()) p->label = tr("Új beszélő %1").arg(d->ov.nextAnonymous++);
    } else {
        d->speakerMap.remove(speakerKey);
    }
    s.affectedSpeakers << speakerKey;
    if (fixVoiceprints)
        d->fixVoiceprints(speakerLines, oldPerson, QString(), othersHaveOld, s);
    d->commit(s, tr("Névtelenre állítás: %1").arg(oldPerson));
    return true;
}

bool SpeakerEditor::applyBindings(const QVector<SpeakerBinding>& bindings, const QString& undoText)
{
    Step s = d->begin();
    bool changed = false;
    for (const SpeakerBinding& b : bindings) {
        const QString person = d->canonicalPerson(b.personName);
        if (b.utteranceIds.isEmpty()) {
            const QString key = b.rawLabel;
            if (!d->visible(key)) continue;
            const QString old = d->personOf(key);
            if (person.isEmpty()) {
                if (old.isEmpty()) continue;
                if (OverlayParticipant* p = d->ov.participant(key)) {
                    p->person.clear();
                    if (p->label.isEmpty()) p->label = tr("Új beszélő %1").arg(d->ov.nextAnonymous++);
                } else {
                    d->speakerMap.remove(key);
                }
                appendUnique(s.affectedSpeakers, key);
                changed = true;
                continue;
            }
            if (old.compare(person, Qt::CaseInsensitive) == 0) continue;
            const QString existing = d->speakerKeyForPerson(person, key);
            if (!existing.isEmpty()) d->mergeInto(key, existing, s);
            else d->setPerson(key, person, s);
            changed = true;
            continue;
        }
        const QVector<int> all = d->indicesOf(b.utteranceIds);
        if (all.isEmpty()) continue;
        QString key = d->speakerKeyForPerson(person);
        if (key.isEmpty()) key = d->createParticipant(person, s);
        QVector<int> idx;
        for (int i : all) {
            const auto o = d->ov.utterances.constFind(d->lines[i].id);
            const QString cur = (o != d->ov.utterances.constEnd() && !o->speaker.isEmpty())
                ? o->speaker : d->assigned.value(i);
            if (cur != key) idx.append(i);
        }
        if (idx.isEmpty()) continue;
        d->moveLines(idx, key, s);
        changed = true;
    }
    if (!changed) return false;
    d->commit(s, undoText);
    return true;
}

bool SpeakerEditor::mergeSpeakers(const QString& fromKey, const QString& intoKey)
{
    if (fromKey == intoKey || !d->visible(fromKey) || !d->visible(intoKey)) return false;
    Step s = d->begin();
    const QString fromName = d->displayOf(fromKey);
    d->mergeInto(fromKey, intoKey, s);
    d->commit(s, tr("Összevonás: %1 → %2").arg(fromName, d->displayOf(intoKey)));
    return true;
}

QString SpeakerEditor::addParticipant(const QString& personName)
{
    if (d->folder.isEmpty()) return {};
    const QString person = d->canonicalPerson(personName);
    const QString existing = d->speakerKeyForPerson(person);
    if (!existing.isEmpty()) return existing;

    Step s = d->begin();
    const QString key = d->createParticipant(person, s);
    d->commit(s, tr("Résztvevő hozzáadása: %1").arg(d->displayOf(key)));
    return key;
}

bool SpeakerEditor::removeParticipant(const QString& speakerKey)
{
    if (!d->visible(speakerKey) || d->lineCountOf(speakerKey) > 0) return false;
    Step s = d->begin();
    const QString name = d->displayOf(speakerKey);
    if (isParticipantKey(speakerKey)) {
        d->ov.participants.erase(
            std::remove_if(d->ov.participants.begin(), d->ov.participants.end(),
                           [&](const OverlayParticipant& p) { return p.key == speakerKey; }),
            d->ov.participants.end());
    } else {
        d->ov.removedRaw << speakerKey;
    }
    d->commit(s, tr("Résztvevő eltávolítása: %1").arg(name));
    return true;
}

bool SpeakerEditor::confirmUtterances(const QStringList& utteranceIds, bool asNoisy)
{
    QVector<int> idx;
    for (int i : d->indicesOf(utteranceIds)) {
        const OverlayUtterance u = d->ov.utterances.value(d->lines[i].id);
        if (!u.confirmed || u.ignored || (asNoisy && u.noisy != std::optional<bool>(true))) idx.append(i);
    }
    if (idx.isEmpty()) return false;

    Step s = d->begin();
    for (int i : std::as_const(idx)) {
        OverlayUtterance& u = d->ov.utterances[d->lines[i].id];
        u.confirmed = true;
        u.rechecked = false;
        u.recheckHint.clear();
        u.ignored = false;      // "Fine as is": we know who said it
        if (asNoisy) u.noisy = true;
    }
    d->commit(s, asNoisy ? tr("%n sor megerősítése (nem hangminta)", nullptr, idx.size())
                         : tr("%n sor megerősítése", nullptr, idx.size()));
    if (!asNoisy) d->finishDecision(idx);
    return true;
}

bool SpeakerEditor::setUtterancesNoisy(const QStringList& utteranceIds, bool noisy)
{
    QVector<int> idx;
    for (int i : d->indicesOf(utteranceIds))
        if (d->noisyAt(i) != noisy) idx.append(i);
    if (idx.isEmpty()) return false;

    Step s = d->begin();
    for (int i : std::as_const(idx)) d->ov.utterances[d->lines[i].id].noisy = noisy;
    d->commit(s, noisy ? tr("%n sor: nem hangminta", nullptr, idx.size())
                       : tr("%n sor: mintának használható", nullptr, idx.size()));
    return true;
}

bool SpeakerEditor::setUtterancesIgnored(const QStringList& utteranceIds, bool ignored)
{
    QVector<int> idx;
    for (int i : d->indicesOf(utteranceIds))
        if (d->ignoredAt(i) != ignored) idx.append(i);
    if (idx.isEmpty()) return false;

    Step s = d->begin();
    for (int i : std::as_const(idx)) {
        auto it = d->ov.utterances.find(d->lines[i].id);
        if (it == d->ov.utterances.end()) it = d->ov.utterances.insert(d->lines[i].id, OverlayUtterance());
        it->ignored = ignored;
        if (ignored) {
            // The decision closes the line's doubt: the recheck's flag and suggestion are released.
            it->rechecked = false;
            it->recheckHint.clear();
        }
        if (it->isDefault()) d->ov.utterances.erase(it);
    }
    // The name does not change, so it is not a speaker change (the summary does not become stale).
    d->commit(s, ignored ? tr("%n sor: nem azonosítható", nullptr, idx.size())
                         : tr("%n sor: újra azonosítható", nullptr, idx.size()));
    return true;
}

SampleDeviation SpeakerEditor::lastSampleDeviation() const { return d->deviation; }

QVector<SpeakerSampleStats> SpeakerEditor::sampleStats() const { return d->computeSampleStats(); }

// ---- recheck ---------------------------------------------------------

bool SpeakerEditor::canRecheck() const
{
    return !d->lines.isEmpty() && !d->thread && d->hasCore();
}

QString SpeakerEditor::recheckBlocker() const
{
    if (d->lines.isEmpty())
        return tr("Ennek a megbeszélésnek nincs szerkeszthető átirata.");
    if (d->fused.isEmpty() && !d->factory)
        return tr("Az újraellenőrzéshez nincs telepítve a hangmodell.");
    if (d->thread)
        return tr("A sorok hang-elemzése még fut — a végén újraellenőrizheted a sorokat.");
    if (d->fused.isEmpty())
        return tr("A sorok hang-elemzése még nem készült el. Nyisd meg az Átirat fület, és várd meg a végét.");
    if (!d->hasCore())
        return tr("Előbb erősíts meg vagy javíts legalább %1 sort egy beszélőnél („Jó így” vagy "
                  "áthelyezés) — ezek hangjához mérem a többit.").arg(kMinSpeakerLines);
    return {};
}

SpeakerEditor::RecheckResult SpeakerEditor::recheckFromConfirmed()
{
    RecheckResult r;
    if (!canRecheck()) return r;
    QHash<QString, int> idx;
    const QVector<AnalysisLine> al = d->analysisLines(&idx, /*groupByPerson*/ true);
    const RecheckAnalysis a = computeUncertainRechecked(al, idx.size(), d->priorsFor(al, idx.size()));

    // Group index -> speaker key (the speaker carrying the group's most lines; the same person with several
    // keys is one group).
    QVector<QHash<QString, int>> keyFreq(idx.size());
    for (int i = 0; i < d->lines.size(); ++i) ++keyFreq[al[i].speaker][d->assigned[i]];
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
        if (info.refLocal() || info.refPrior()) r.references.append(d->referenceOf(groupKey[g], info));
    }
    // For feedback: the stored samples that joined, the models in use and the very similar voices.
    for (const SpeakerReference& ref : std::as_const(r.references)) {
        r.localPrints += ref.localPrints;
        r.priorPrints += ref.priorPrints;
    }
    for (const QString& id : std::as_const(d->modelIds)) {
        const auto spec = processing::VoiceModelRegistry::spec(id);
        r.models << (spec && !spec->displayName.isEmpty() ? spec->displayName : id);
    }
    r.similar = similarVoices();

    Step s = d->begin();
    for (int i = 0; i < d->lines.size(); ++i) {
        const QString& id = d->lines[i].id;
        const RecheckVerdict& v = a.lines[i];
        auto it = d->ov.utterances.find(id);
        if (v.uncertain) {
            OverlayUtterance& u = it != d->ov.utterances.end() ? it.value() : d->ov.utterances[id];
            u.rechecked = true;
            u.recheckHint = v.otherSpeaker >= 0 ? groupKey.value(v.otherSpeaker) : QString();
        } else if (it != d->ov.utterances.end() && it->rechecked) {
            // A newer recheck replaces the set: what is no longer doubtful is released.
            it->rechecked = false;
            it->recheckHint.clear();
            if (it->isDefault()) d->ov.utterances.erase(it);
        }
    }
    if (!sameEdits(s.before, d->ov))
        d->commit(s, tr("Beszélők újraellenőrzése"));
    emit recheckFinished(r.flagged, r.speakersWithConfirmedCore, r.confirmedLines);
    return r;
}

QString SpeakerEditor::referenceSummary(const QVector<SpeakerReference>& refs)
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

QString SpeakerEditor::recheckBasisSummary(const RecheckResult& r)
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

QString SpeakerEditor::similarVoicesNote(const QVector<SimilarVoices>& similar)
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

QString SpeakerEditor::pairRecheckBlocker(const QString& speakerKeyA, const QString& speakerKeyB) const
{
    if (d->lines.isEmpty())
        return tr("Ennek a megbeszélésnek nincs szerkeszthető átirata.");
    if (speakerKeyA.isEmpty() || speakerKeyB.isEmpty() || speakerKeyA == speakerKeyB
        || !d->visible(speakerKeyA) || !d->visible(speakerKeyB))
        return tr("Válassz két különböző beszélőt ebből a megbeszélésből.");
    if (d->fused.isEmpty() && !d->factory)
        return tr("Az átnézéshez nincs telepítve a hangmodell.");
    if (d->thread)
        return tr("A sorok hang-elemzése még fut — a végén átnézheted a két beszélőt.");
    if (d->fused.isEmpty())
        return tr("A sorok hang-elemzése még nem készült el. Nyisd meg az Átirat fület, és várd meg a végét.");
    const PairRecheckAnalysis a = d->pairAnalysis(speakerKeyA, speakerKeyB);
    if (!a.valid) {
        const QString who = a.refLinesA < kMinSpeakerLines ? d->displayOf(speakerKeyA) : d->displayOf(speakerKeyB);
        return tr("%1 hangjához kevés a minta: legalább %2 elég hosszú, tiszta sora kell.")
            .arg(who).arg(kMinSpeakerLines);
    }
    return {};
}

SpeakerEditor::PairRecheckResult SpeakerEditor::recheckPair(const QString& speakerKeyA,
                                                            const QString& speakerKeyB)
{
    PairRecheckResult r;
    r.blocker = pairRecheckBlocker(speakerKeyA, speakerKeyB);
    if (!r.blocker.isEmpty()) return r;
    QHash<QString, int> idx;
    const QVector<AnalysisLine> al = d->analysisLines(&idx, /*groupByPerson*/ false);
    const int ia = idx.value(speakerKeyA, -1);
    const PairRecheckAnalysis a = computePairRecheck(al, ia, idx.value(speakerKeyB, -1),
                                                     d->priorsFor(al, idx.size()));
    r.ran = true;
    r.flagged = a.flagged();
    r.refLinesA = a.refLinesA;
    r.refLinesB = a.refLinesB;
    r.fallbackA = a.fallbackA;
    r.fallbackB = a.fallbackB;
    r.centroidSimilarity = a.centroidSimilarity;
    r.refA = d->referenceOf(speakerKeyA, a.refA);
    r.refB = d->referenceOf(speakerKeyB, a.refB);

    Step s = d->begin();
    for (int i = 0; i < d->lines.size(); ++i) {
        // Only A's and B's lines: the earlier flags of other speakers are untouched.
        if (d->assigned[i] != speakerKeyA && d->assigned[i] != speakerKeyB) continue;
        const QString& id = d->lines[i].id;
        const PairVerdict& v = a.lines[i];
        auto it = d->ov.utterances.find(id);
        if (v.flagged) {
            OverlayUtterance& u = it != d->ov.utterances.end() ? it.value() : d->ov.utterances[id];
            u.rechecked = true;
            u.recheckHint = v.hintedSpeaker == ia ? speakerKeyA : speakerKeyB;
        } else if (it != d->ov.utterances.end() && it->rechecked) {
            // This review replaces the earlier flags of the lines on A and B.
            it->rechecked = false;
            it->recheckHint.clear();
            if (it->isDefault()) d->ov.utterances.erase(it);
        }
    }
    if (!sameEdits(s.before, d->ov))
        d->commit(s, tr("Átnézés: %1 és %2").arg(d->displayOf(speakerKeyA), d->displayOf(speakerKeyB)));
    return r;
}

bool SpeakerEditor::hasPairOffer() const { return d->pairOffer.isValid(); }
PairRecheckOffer SpeakerEditor::pairOffer() const { return d->pairOffer; }

void SpeakerEditor::declinePairOffer()
{
    if (!d->pairOffer.isValid()) return;
    d->declinedPairs.insert(Private::pairId(d->pairOffer.sourceSpeakerKey, d->pairOffer.targetSpeakerKey));
    d->pairOffer = PairRecheckOffer();
    emit suggestionChanged();
}

void SpeakerEditor::dismissPairOffer()
{
    if (!d->pairOffer.isValid()) return;
    d->pairOffer = PairRecheckOffer();
    emit suggestionChanged();
}

// ---- suggestion ---------------------------------------------------------------

bool SpeakerEditor::hasSuggestion() const { return d->suggestion.isValid(); }
SpeakerSuggestion SpeakerEditor::suggestion() const { return d->suggestion; }

bool SpeakerEditor::acceptSuggestion()
{
    if (!d->suggestion.isValid()) return false;
    const SpeakerSuggestion sg = d->suggestion;
    if (!d->visible(sg.targetSpeaker)) { d->clearSuggestion(); return false; }
    QVector<int> idx;
    for (int i : d->indicesOf(sg.utteranceIds))
        if (d->assigned[i] != sg.targetSpeaker) idx.append(i);
    if (idx.isEmpty()) { d->clearSuggestion(); return false; }

    Step s = d->begin();
    d->moveLines(idx, sg.targetSpeaker, s);
    d->commit(s, tr("Javaslat elfogadása: %n sor ide: %1", nullptr, idx.size())
                     .arg(d->displayOf(sg.targetSpeaker)));
    return true;
}

void SpeakerEditor::dismissSuggestion() { d->clearSuggestion(); }

// ---- undo / redo ------------------------------------------------------------

bool SpeakerEditor::canUndo() const { return !d->undoStack.isEmpty(); }
bool SpeakerEditor::canRedo() const { return !d->redoStack.isEmpty(); }

QVector<quint64> SpeakerEditor::undoSerials() const
{
    QVector<quint64> out;
    out.reserve(d->undoStack.size());
    for (const Command& c : std::as_const(d->undoStack)) out << c.serial;
    return out;
}
QString SpeakerEditor::undoText() const
{
    return d->undoStack.isEmpty() ? QString() : d->undoStack.last().text;
}
QString SpeakerEditor::redoText() const
{
    return d->redoStack.isEmpty() ? QString() : d->redoStack.last().text;
}

void SpeakerEditor::undo()
{
    if (d->undoStack.isEmpty()) return;
    const Command c = d->undoStack.takeLast();
    d->redoStack.append(c);
    d->apply(c, /*undoing*/ true);
}

void SpeakerEditor::redo()
{
    if (d->redoStack.isEmpty()) return;
    const Command c = d->redoStack.takeLast();
    d->undoStack.append(c);
    d->apply(c, /*undoing*/ false);
}

// ---- embeddings ------------------------------------------------------------

bool SpeakerEditor::embeddingsSupported() const { return bool(d->factory); }
bool SpeakerEditor::isEmbeddingRunning() const { return d->thread != nullptr; }
QString SpeakerEditor::embeddingError() const { return d->embError; }

bool SpeakerEditor::embeddingsComplete() const
{
    for (const TranscriptLine& l : d->lines)
        if (l.endMs - l.startMs >= kMinEmbedMs && !d->missingModels(l.id).isEmpty())
            return false;
    return !d->lines.isEmpty();
}

void SpeakerEditor::startEmbedding()
{
    if (d->thread) return;
    if (!d->factory || d->lines.isEmpty()) {
        emit embeddingFinished(false);
        return;
    }
    // Per line only the still-missing models; decoding the audio happens ONCE on the thread.
    struct Job { QString id; qint64 startMs; qint64 endMs; QStringList models; };
    QVector<Job> jobs;
    for (const TranscriptLine& l : d->lines) {
        if (l.endMs - l.startMs < kMinEmbedMs) continue;
        const QStringList missing = d->missingModels(l.id);
        if (missing.isEmpty()) continue;
        Job j;
        j.id = l.id;
        j.models = missing;
        Private::embedWindow(l, &j.startMs, &j.endMs);
        jobs.append(j);
    }
    if (jobs.isEmpty()) {
        emit embeddingFinished(true);
        return;
    }

    d->embDone = 0;
    d->embTotal = jobs.size();
    d->embError.clear();
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    d->cancel = cancel;
    const int epoch = d->epoch;
    const processing::UtteranceEmbedderFactory factory = d->factory;
    const QString audio = d->audioPath;

    // The thread works with its own embedder instance; it returns the result to the main thread in batches,
    // via a queued call. The destructor waits for it, so `this` lives throughout.
    d->thread = QThread::create([this, jobs, cancel, factory, audio, epoch] {
        using Batch = QVector<QPair<QString, EmbeddingSet>>;
        auto finish = [this, epoch](bool complete, const QString& error) {
            QMetaObject::invokeMethod(this, [this, epoch, complete, error] {
                d->onFinished(epoch, complete, error);
            }, Qt::QueuedConnection);
        };
        std::unique_ptr<processing::IUtteranceEmbedder> emb = factory();
        if (!emb || !emb->open(audio)) {
            finish(false, emb ? emb->lastError() : QString());
            return;
        }
        Batch batch;
        QElapsedTimer timer;
        timer.start();
        bool cancelled = false;
        auto flush = [&] {
            if (batch.isEmpty()) return;
            QMetaObject::invokeMethod(this, [this, epoch, batch] { d->onBatch(epoch, batch); },
                                      Qt::QueuedConnection);
            batch.clear();
            timer.restart();
        };
        for (const Job& j : jobs) {
            if (cancel->load()) { cancelled = true; break; }
            batch.append({j.id, emb->embedAll(j.startMs, j.endMs, j.models)});
            if (batch.size() >= 16 || timer.elapsed() > 300) flush();
        }
        flush();
        finish(!cancelled, QString());
    });
    d->thread->start();
    emit embeddingRunningChanged(true);
    emit embeddingProgress(0, d->embTotal);
}

void SpeakerEditor::cancelEmbedding()
{
    if (d->thread && d->cancel) d->cancel->store(true);
}

// ---- manual voiceprint ------------------------------------------------------

VoiceprintMaterial SpeakerEditor::voiceprintMaterial(const QString& speakerKey) const
{
    VoiceprintMaterial m;
    qint64 used = 0;
    m.usableLines = d->selectPrintLines(d->linesOf(speakerKey), &used).size();
    m.usableMs = used;
    m.missingMs = std::max<qint64>(0, kPrintMinTotalMs - used);
    m.sufficient = m.missingMs == 0;
    return m;
}

VoiceprintResult SpeakerEditor::createVoiceprint(const QString& speakerKey)
{
    VoiceprintResult r;
    if (!d->visible(speakerKey) || !d->voiceprints) {
        r.error = tr("Ismeretlen beszélő.");
        return r;
    }
    const QString person = d->personOf(speakerKey);
    if (person.isEmpty()) {
        r.error = tr("Előbb nevezd el a beszélőt — névtelen beszélőhöz nem készül hanglenyomat.");
        return r;
    }
    const QVector<Voiceprint> prints = d->buildPrint(d->linesOf(speakerKey), &r.usedMs, &r.usedLines, &r.missingMs);
    if (prints.isEmpty()) {
        if (r.missingMs > 0)
            r.error = tr("Nincs elég hanganyag a lenyomathoz: még kb. %1 mp beszéd kellene "
                         "(legalább 3 mp-es sorokból).").arg((r.missingMs + 999) / 1000);
        else
            r.error = tr("A hangmodell vagy a megbeszélés hangja nem érhető el.");
        return r;
    }
    for (const Voiceprint& p : prints) d->voiceprints->addPrint(person, p);
    r.ok = true;
    r.printId = prints.first().id;   // the sibling voiceprints (other models) are deleted together with this
    r.missingMs = 0;
    emit voiceprintsChanged();
    emit speakersChanged();
    return r;
}

VoiceprintResult SpeakerEditor::createVoiceprintFromLines(const QString& speakerKey,
                                                         const QStringList& utteranceIds)
{
    VoiceprintResult r;
    if (!d->visible(speakerKey) || !d->voiceprints) {
        r.error = tr("Ismeretlen beszélő.");
        return r;
    }
    const QString person = d->personOf(speakerKey);
    if (person.isEmpty()) {
        r.error = tr("Előbb nevezd el a beszélőt — névtelen beszélőhöz nem készül hanglenyomat.");
        return r;
    }
    // Only the speaker's own, clean lines of at least 3 s; an uncertain line must first be confirmed or
    // moved (a manual sample cannot teach from a doubtful line either).
    QVector<int> picked;
    qint64 total = 0;
    for (const QString& id : utteranceIds) {
        const int i = d->indexById.value(id, -1);
        if (i < 0 || d->assigned.value(i) != speakerKey) continue;
        if (d->uncertain.value(i)) {
            r.error = tr("Ez a sor bizonytalan: előbb erősítsd meg („Jó így”) vagy helyezd át, utána lehet minta.");
            return r;
        }
        if (d->ignoredAt(i)) {
            r.error = tr("Ez a sor „nem azonosítható” jelölésű, ezért nem használható hangmintának.");
            return r;
        }
        if (d->noisyAt(i)) {
            r.error = tr("Ebben a sorban egymásra beszéltek, ezért nem használható hangmintának.");
            return r;
        }
        const qint64 dur = d->lines[i].endMs - d->lines[i].startMs;
        if (dur < kPrintMinLineMs) {
            r.error = tr("Ez a sor túl rövid mintának (legalább 3 mp beszéd kell).");
            return r;
        }
        picked.append(i);
        total += std::min<qint64>(dur, kMaxEmbedMs);
    }
    if (picked.isEmpty()) {
        r.error = tr("Nincs olyan sor, amelyből minta készülhetne.");
        return r;
    }
    const QVector<Voiceprint> prints = d->buildPrintFromPicked(picked, total, kPrintMinLineMs, &r.usedMs, &r.usedLines, &r.missingMs);
    if (prints.isEmpty()) {
        r.error = tr("A hangmodell vagy a megbeszélés hangja nem érhető el.");
        return r;
    }
    for (const Voiceprint& p : prints) d->voiceprints->addPrint(person, p);
    r.ok = true;
    r.printId = prints.first().id;   // the sibling voiceprints (other models) are deleted together with this
    r.missingMs = 0;
    emit voiceprintsChanged();
    emit speakersChanged();
    return r;
}

bool SpeakerEditor::removeVoiceprint(const QString& printId)
{
    if (!d->voiceprints || printId.isEmpty()) return false;
    // The voiceprint and its siblings (the same sample with other models: the same sampleRef, source and
    // createdAt) are deleted together: one manual creation can be undone as a unit.
    QString owner;
    Voiceprint print;
    if (!d->voiceprints->findPrint(printId, &owner, &print)) return false;
    QStringList ids{printId};
    for (const Voiceprint& p : d->voiceprints->printsFor(owner))
        if (p.id != printId && p.model != print.model && p.sampleRef == print.sampleRef
            && p.sourceMeetingId == print.sourceMeetingId && p.createdAt == print.createdAt)
            ids << p.id;
    bool removed = false;
    for (const QString& id : std::as_const(ids)) removed = d->voiceprints->removePrint(id) || removed;
    if (!removed) return false;
    emit voiceprintsChanged();
    emit speakersChanged();
    return true;
}

// ---- summary staleness / re-transcription ----------------------------------

SummaryStaleInfo SpeakerEditor::summaryStale() const { return d->staleInfo(); }

RetranscribeImpact SpeakerEditor::retranscribeImpact() const
{
    RetranscribeImpact imp;
    for (auto it = d->ov.utterances.constBegin(); it != d->ov.utterances.constEnd(); ++it) {
        if (it->corrected) ++imp.correctedUtterances;
        else if (it->confirmed) ++imp.confirmedUtterances;
    }
    imp.addedParticipants = d->ov.participants.size();
    QSet<QString> named;
    for (const EditorSpeaker& s : d->buildSpeakers())
        if (!s.anonymous) named.insert(s.personName);
    imp.namedSpeakers = named.size();
    return imp;
}

void SpeakerEditor::dismissSummaryStale()
{
    if (d->ov.changedSinceSummary.isEmpty()) return;
    notifySummaryRegenerated();
}

void SpeakerEditor::notifySummaryRegenerated()
{
    const SummaryStaleInfo before = d->staleInfo();
    d->ov.changedSinceSummary.clear();
    ++d->ov.summaryEpoch;
    if (!d->folder.isEmpty()) saveOverlay(d->folder, d->ov);
    if (d->store) d->hasSummary = d->store->load(d->meetingId).hasSummary;
    if (before.stale) emit summaryStaleChanged(false, 0);
}

void SpeakerEditor::refreshFromDisk()
{
    if (d->folder.isEmpty() || !d->store) return;
    const QVector<TranscriptLine> diskLines = loadTranscriptLines(d->folder);
    if (transcriptFingerprint(diskLines) != d->ov.transcriptFingerprint) {
        reloadTranscript();     // the transcript was replaced meanwhile too
        return;
    }
    const SpeakerOverlay disk = loadOverlayFor(d->folder, d->lines);
    if (!sameEdits(disk, d->ov)) {
        // Someone else wrote the overlay (e.g. a global person rename): the undo snapshots are no longer valid.
        d->stopThread(/*notify*/ true);
        d->load();
        d->undoStack.clear();
        d->redoStack.clear();
        d->suggestion = SpeakerSuggestion();
        d->pairOffer = PairRecheckOffer();
        d->resetDeviation();
        emit reloaded();
        emit speakersChanged();
        emit undoStateChanged();
        emit suggestionChanged();
        d->updateSampleStats();
        return;
    }
    Step s;
    s.rows = d->rows();
    s.stale = d->staleInfo();
    const Meeting m = d->store->load(d->meetingId);
    d->speakerMap = m.speakerMap;
    d->hasSummary = m.hasSummary;
    d->ov.identified = disk.identified;
    d->ov.changedSinceSummary = disk.changedSinceSummary;
    d->ov.summaryEpoch = disk.summaryEpoch;
    d->recomputeAssigned();
    d->recomputeUncertain();
    d->emitChanges(s, false, false, false);
    d->scheduleReview();
}

void SpeakerEditor::reloadTranscript()
{
    const SummaryStaleInfo before = d->staleInfo();
    d->stopThread(/*notify*/ true);
    // With the new transcript transcript.md was also made fresh, so the pending regeneration is moot.
    d->markdownPending = false;
    if (d->markdownTimer) d->markdownTimer->stop();
    d->waitMarkdown();
    d->load();
    d->undoStack.clear();
    d->redoStack.clear();
    d->suggestion = SpeakerSuggestion();
    d->pairOffer = PairRecheckOffer();
    d->embError.clear();
    d->snapshotSessionBaseline();   // a new transcript = a new session for the sample meter
    d->resetDeviation();
    emit reloaded();
    emit sampleStatsChanged();
    emit speakersChanged();
    emit uncertainCountChanged(d->uncertainCount());
    emit undoStateChanged();
    emit suggestionChanged();
    const SummaryStaleInfo st = d->staleInfo();
    if (st.stale != before.stale || st.correctedSpeakers != before.correctedSpeakers)
        emit summaryStaleChanged(st.stale, st.correctedSpeakers);
}

// ---- manual track assignment -----------------------------------------------------

QStringList SpeakerEditor::trackAssignment(const QString& speakerKey) const
{
    return d->ov.speakerTracks.value(speakerKey);
}

bool SpeakerEditor::setTrackAssignment(const QString& speakerKey, const QStringList& trackIds)
{
    if (!d->visible(speakerKey)) return false;
    const Meeting m = d->store ? d->store->load(d->meetingId) : Meeting();
    QStringList ids;
    for (const QString& id : trackIds) {
        if (id.isEmpty() || ids.contains(id)) continue;
        const bool known = std::any_of(m.tracks.cbegin(), m.tracks.cend(), [&](const Track& t) { return t.id == id; });
        if (known) ids << id;
    }
    if (ids == d->ov.speakerTracks.value(speakerKey)) return false;
    Step s = d->begin();
    if (ids.isEmpty()) d->ov.speakerTracks.remove(speakerKey);
    else d->ov.speakerTracks.insert(speakerKey, ids);
    d->commit(s, ids.isEmpty() ? tr("Sáv-beosztás törlése: %1").arg(d->displayOf(speakerKey))
                               : tr("Sáv-beosztás: %1").arg(d->displayOf(speakerKey)));
    return true;
}

// ---- evidence, candidates, "To review" -----------------------------------------

void SpeakerEditor::setActivityProvider(ActivityProvider provider)
{
    d->activityProvider = std::move(provider);
    d->activity.reset();
    d->scheduleReview();
}

void SpeakerEditor::setTagEvidenceProvider(library::TagEvidenceFn fn)
{
    d->tagEvidence = std::move(fn);
    d->tagEvidenceCache.clear();
    d->scheduleReview();
}

void SpeakerEditor::setPersonTagsProvider(PersonTagsFn fn)
{
    d->personTags = std::move(fn);
}

QStringList SpeakerEditor::meetingTagIds() const { return d->meetingTags; }

QStringList SpeakerEditor::personTagIds(const QString& personName) const
{
    return d->personTags && !personName.trimmed().isEmpty() ? d->personTags(personName) : QStringList();
}

void SpeakerEditor::notifyPersonTagsChanged() { d->scheduleReview(); }

QVector<ReviewGroup> SpeakerEditor::reviewGroups() const { return d->review.groups; }
bool SpeakerEditor::isReviewRunning() const { return d->reviewRunning; }
SideReport SpeakerEditor::sideReport() const { return d->review.sides; }

void SpeakerEditor::refreshReviewGroups() { d->scheduleReview(); }

bool SpeakerEditor::applyReviewGroup(const QString& groupId)
{
    const auto it = std::find_if(d->review.groups.cbegin(), d->review.groups.cend(),
                                 [&](const ReviewGroup& g) { return g.id == groupId; });
    if (it == d->review.groups.cend()) return false;
    const ReviewGroup g = *it;
    if (g.kind == ReviewKind::ShortLines) return false;
    QVector<int> idx;
    for (int i : d->indicesOf(g.utteranceIds)) {
        // What went elsewhere meanwhile is dropped.
        if (g.kind == ReviewKind::SimilarToNewPerson ? d->assigned[i] == g.proposedSpeakerKey
                                                     : d->assigned[i] != g.currentSpeakerKey)
            continue;
        idx.append(i);
    }
    const QString label = g.kind == ReviewKind::ContaminatedCore ? tr("Szétválasztás") : tr("Átnézendő");
    if (!d->moveToTarget(idx, g.proposedSpeakerKey, g.proposedPersonName, label)) return false;
    d->review.groups.erase(std::remove_if(d->review.groups.begin(), d->review.groups.end(),
                                          [&](const ReviewGroup& x) { return x.id == groupId; }),
                           d->review.groups.end());
    emit reviewGroupsChanged();
    return true;
}

bool SpeakerEditor::splitSpeaker(const QString& speakerKey)
{
    if (!d->visible(speakerKey) || d->fused.isEmpty()) return false;
    const ReviewInput in = d->makeInput();
    const SideReport sr = analyzeSides(in.meeting, in.lines, in.overlay, d->activity.value_or(MeetingActivity{}),
                                       in.userName, in.sideHints);
    const ranking::RankContext ctx = buildRankContext(in, sr);
    for (const ReviewGroup& g : buildReviewGroups(in, sr, ctx)) {
        if (g.kind != ReviewKind::ContaminatedCore || g.currentSpeakerKey != speakerKey) continue;
        return d->moveToTarget(d->indicesOf(g.utteranceIds), g.proposedSpeakerKey, g.proposedPersonName,
                               tr("Szétválasztás"));
    }
    return false;
}

QVector<Candidate> SpeakerEditor::lineCandidates(const QString& utteranceId) const
{
    const int i = d->indexById.value(utteranceId, -1);
    if (i < 0) return {};
    const QVector<float> emb = d->fused.value(utteranceId);
    Side side = Side::Unknown;
    if (i < d->review.sides.lines.size() && d->review.sides.lines[i].utteranceId == utteranceId)
        side = d->review.sides.lines[i].side;
    // An "unidentifiable" line's voice is not part of its speaker's line sum (ReviewGroups).
    return ranking::rankForLine(d->review.context, emb, side, d->assigned[i],
                                !emb.isEmpty() && !d->noisyAt(i) && !d->ignoredAt(i));
}

QVector<Candidate> SpeakerEditor::speakerCandidates(const QString& speakerKey) const
{
    return ranking::rankForSpeaker(d->review.context, speakerKey);
}

QVector<Evidence> SpeakerEditor::whyNot(const QString& utteranceId) const
{
    const int i = d->indexById.value(utteranceId, -1);
    if (i < 0) return {};
    for (const Candidate& c : lineCandidates(utteranceId))
        if (c.speakerKey == d->assigned[i]) return ranking::whyNot(c);
    return {};
}

QVector<Evidence> SpeakerEditor::speakerEvidence(const QString& speakerKey) const
{
    return ranking::selfEvidence(d->review.context, speakerKey);
}

QVector<SpeakerEditor::SimilarVoices> SpeakerEditor::similarVoices() const
{
    QVector<SimilarVoices> out;
    QSet<QString> seen;
    const ranking::RankContext& ctx = d->review.context;
    for (const ranking::SpeakerProfile& p : ctx.speakers) {
        if (p.key.isEmpty() || !d->visible(p.key)) continue;
        for (const Evidence& e : ranking::similarityWarnings(ctx, p.key)) {
            const QString other = e.fixTarget.mid(5);   // "pair:<key>"
            if (other.isEmpty() || !d->visible(other)) continue;
            const QString pair = p.key < other ? p.key + QLatin1Char('|') + other : other + QLatin1Char('|') + p.key;
            if (seen.contains(pair)) continue;
            seen.insert(pair);
            SimilarVoices v;
            v.keyA = p.key;
            v.nameA = d->displayOf(p.key);
            v.keyB = other;
            v.nameB = d->displayOf(other);
            v.similarity = e.value;
            out.append(v);
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const SimilarVoices& a, const SimilarVoices& b) { return a.similarity > b.similarity; });
    return out;
}

} // namespace tanara::editing
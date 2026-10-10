#include "tanara/edit/SpeakerEditor.h"
#include "tanara/Logging.h"

#include "tanara/edit/SpeakerAnalysis.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
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
#include <optional>

namespace tanara {

using namespace speakeredit;

namespace {

constexpr int kMaxUndoSteps = 200;
// A transcript.md ennyivel az utolsó névváltozás után íródik újra (kötegelve).
constexpr int kMarkdownDelayMs = 1500;

// A Meeting.speakerMap egy kulcsának változása (az undo csak az ÉRINTETT kulcsokat állítja
// vissza, így a közben kívülről — régi UI, auto-azonosítás — érkező változást nem írja felül).
struct MapDelta {
    QString raw;
    bool    hadBefore = false;
    QString before;
    bool    hasAfter = false;
    QString after;
};

// Hanglenyomat-mellékhatás (a „téves felismerés" javításánál) — az undo visszacsinálja.
struct VoiceprintOp {
    bool       added = false;   // true: a személyhez ADTUK; false: ELVETTÜK tőle
    QString    person;
    Voiceprint print;
};

// Egy sor megfigyelhető állapota — a művelet előtti/utáni különbségből lesz az utterancesChanged.
struct RowState {
    QString speaker;
    bool corrected = false;
    bool confirmed = false;
    bool uncertain = false;
    bool noisy = false;
    QString likely;
    bool operator==(const RowState& o) const {
        return speaker == o.speaker && corrected == o.corrected && confirmed == o.confirmed
            && uncertain == o.uncertain && noisy == o.noisy && likely == o.likely;
    }
};

struct Command {
    QString text;
    SpeakerOverlay before;
    SpeakerOverlay after;
    QVector<MapDelta> map;
    QVector<VoiceprintOp> voiceprints;
    QStringList peopleAdded;
    QStringList affectedSpeakers;
};

// Egy folyamatban lévő művelet: a kiinduló állapot + a közben gyűlő mellékhatások.
struct Step {
    SpeakerOverlay before;
    QMap<QString, QString> mapBefore;
    QVector<RowState> rows;
    QStringList names;              // soronkénti megjelenített név (kell-e új transcript.md)
    SummaryStaleInfo stale;
    QVector<VoiceprintOp> voiceprints;
    QStringList peopleAdded;
    QStringList affectedSpeakers;
};

bool sameEdits(const SpeakerOverlay& a, const SpeakerOverlay& b)
{
    if (a.merged != b.merged || a.removedRaw != b.removedRaw) return false;
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

// A lenyomat sampleRef-jéből ("fájl#startMs-endMs") a kezdőidő; -1, ha nincs.
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

} // namespace

struct SpeakerEditor::Private {
    SpeakerEditor* q = nullptr;
    MeetingStore* store = nullptr;
    PeopleStore* people = nullptr;
    VoiceprintStore* voiceprints = nullptr;
    QString meetingId;
    QString folder;
    QString audioPath;      // a mixdown (lehet, hogy nem létezik)
    QString audioRel;
    QString userName;
    bool    hasSummary = false;

    QVector<TranscriptLine> lines;
    QHash<QString, int> indexById;
    QStringList rawOrder;               // nyers címkék az első megjelenés sorrendjében

    SpeakerOverlay ov;
    QMap<QString, QString> speakerMap;
    QVector<QString> assigned;          // soronként a feloldott beszélő-kulcs
    QVector<bool> uncertain;
    QVector<bool> overlapNoisy;         // soronként: más beszélővel átfed (automatikus, nem perzisztál)

    QVector<Command> undoStack;
    QVector<Command> redoStack;
    SpeakerSuggestion suggestion;
    PairRecheckOffer pairOffer;
    QSet<QString> declinedPairs;        // „Most nem": ezeket a párokat nem ajánljuk újra

    // embeddingek
    UtteranceEmbedderFactory factory;
    UtteranceEmbeddingCache cache;
    QStringList modelIds{VoiceModelRegistry::defaultModelId()};   // ábécérendben
    // Soronként a modellek fúziós vektora (csak ahol minden modellre van nem üres vektor) —
    // az elemzés (SpeakerAnalysis) ezt kapja. A cache minden változása után frissül.
    QHash<QString, QVector<float>> fused;
    QThread* thread = nullptr;
    std::shared_ptr<std::atomic_bool> cancel;
    int epoch = 0;                      // nő minden (újra)töltésnél — a régi szál eredményét eldobjuk
    int embDone = 0;
    int embTotal = 0;
    QString embError;

    // A transcript.md késleltetett újragenerálása (lásd persist()).
    QTimer* markdownTimer = nullptr;
    bool markdownPending = false;
    // A transcript.md újragenerálása háttérszálon fut (a tokenek újraolvasása + a teljes fájl
    // kiírása egy 2 órás megbeszélésnél ~120 ms — a fő szálon ez minden javítás után
    // megakasztotta a felületet). Egyszerre egy futás; közben érkező kérés a végén újraindul.
    bool markdownRunning = false;
    std::atomic<int> markdownInFlight{0};

    // ---- betöltés -----------------------------------------------------------
    void load()
    {
        QElapsedTimer perf;
        perf.start();
        const Meeting m = store ? store->load(meetingId) : Meeting();
        folder = m.folder;
        hasSummary = m.hasSummary;
        speakerMap = m.speakerMap;
        audioPath = m.folder.isEmpty() ? QString() : mixdownPath(m);
        audioRel = m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3") : m.mixdownFile;
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
        cache = folder.isEmpty() ? UtteranceEmbeddingCache()
                                 : UtteranceEmbeddingCache::load(folder, ov.transcriptFingerprint);
        rebuildFused();
        const qint64 tCache = perf.elapsed();
        ++epoch;
        recomputeAssigned();
        const qint64 tAssigned = perf.elapsed();
        recomputeUncertain();
        qCDebug(lcPerf).noquote()
            << QStringLiteral("SpeakerEditor::load %1 sor: meeting %2 ms, átirat %3 ms, overlay %4 ms, "
                              "embedding-cache %5 ms, feloldás %6 ms, bizonytalanság %7 ms, össz %8 ms")
                   .arg(lines.size()).arg(tMeeting).arg(tLines - tMeeting).arg(tOverlay - tLines)
                   .arg(tCache - tOverlay).arg(tAssigned - tCache).arg(perf.elapsed() - tAssigned)
                   .arg(perf.elapsed());
    }

    // ---- embedding-cache → fúziós vektorok -------------------------------------
    void refreshFused(const QString& id)
    {
        const QVector<float> f = fusion::fuse(cache.setFor(id, modelIds), modelIds);
        if (f.isEmpty()) fused.remove(id);
        else fused.insert(id, f);
    }

    void rebuildFused()
    {
        fused.clear();
        for (const TranscriptLine& l : std::as_const(lines)) refreshFused(l.id);
    }

    // Mely modellekkel nem próbáltuk még ezt a sort (üres = kész).
    QStringList missingModels(const QString& id) const
    {
        QStringList out;
        for (const QString& m : modelIds)
            if (!cache.has(m, id)) out << m;
        return out;
    }

    // ---- feloldás -----------------------------------------------------------
    void recomputeAssigned()
    {
        assigned.resize(lines.size());
        for (int i = 0; i < lines.size(); ++i)
            assigned[i] = resolveSpeakerKey(ov, lines[i]);
        recomputeOverlap();
    }

    // Az „egymásra beszéltek" automatikus jelzése a FELOLDOTT beszélők szerint (ugyanannak a
    // beszélőnek két, egymásba lógó sora nem áthallás).
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

    bool noisyAt(int i) const
    {
        const auto it = ov.utterances.constFind(lines[i].id);
        if (it != ov.utterances.constEnd() && it->noisy.has_value()) return *it->noisy;
        return overlapNoisy.value(i);
    }

    // Az újraellenőrzés javaslatának mostani kulcsa (az összevont címke a célra mutat).
    QString hintKeyAt(int i) const
    {
        const auto it = ov.utterances.constFind(lines[i].id);
        if (it == ov.utterances.constEnd() || it->recheckHint.isEmpty()) return {};
        QString key = it->recheckHint;
        for (int guard = 0; guard < 8 && ov.merged.contains(key); ++guard) key = ov.merged.value(key);
        return key;
    }

    // A sor már a javasolt beszélőnél (vagy ugyanannál a személynél) van.
    bool hintResolved(int i, const QString& key) const
    {
        if (key == assigned[i]) return true;
        const QString person = personOf(key);
        return !person.isEmpty() && person.compare(personOf(assigned[i]), Qt::CaseInsensitive) == 0;
    }

    // Az újraellenőrzés javaslata (a hangra jobban illő beszélő), ha még érvényes és mutatható.
    QString likelyAt(int i) const
    {
        const QString key = hintKeyAt(i);
        if (key.isEmpty() || !visible(key) || hintResolved(i, key)) return {};
        return key;
    }

    // Az újraellenőrzés jelzése még áll-e (javítás / megerősítés törli; ha a sor közben a
    // javasolt beszélőhöz került — pl. összevonással —, magától megszűnik).
    bool recheckedAt(int i) const
    {
        const auto it = ov.utterances.constFind(lines[i].id);
        if (it == ov.utterances.constEnd() || !it->rechecked || it->corrected || it->confirmed)
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

    // A beszélő látszik-e a listában (= célpontja lehet-e egy műveletnek).
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

    // A meeting (látható) beszélője, aki ehhez a személyhez van kötve; üres, ha nincs.
    QString speakerKeyForPerson(const QString& person, const QString& except = QString()) const
    {
        if (person.isEmpty()) return {};
        for (const QString& key : visibleKeys())
            if (key != except && personOf(key).compare(person, Qt::CaseInsensitive) == 0)
                return key;
        return {};
    }

    // A névlistában tárolt írásmód (kisbetű-független egyezésnél), különben a megadott.
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
        u.noisyOverlap = overlapNoisy.value(i);
        u.rechecked = recheckedAt(i);
        if (u.rechecked) u.likelySpeakerKey = likelyAt(i);
        return u;
    }

    QVector<EditorSpeaker> buildSpeakers() const
    {
        QHash<QString, QPair<int, qint64>> stats;   // kulcs → (sorok, beszédidő)
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
            // A hang-azonosítás pontszáma csak addig érvényes, amíg ugyanaz a személy áll ott.
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

    // ---- bizonytalanság -----------------------------------------------------
    // groupByPerson: az azonos SZEMÉLYHEZ kötött beszélők (pl. két nyers címke ugyanarra az
    // emberre) egy hangnak számítanak — egymáshoz képest nem „másik beszélő".
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
        // Az újraellenőrzés jelzései (perzisztensek) a hang-elemzéstől függetlenül megmaradnak.
        for (int i = 0; i < lines.size(); ++i)
            if (recheckedAt(i)) uncertain[i] = true;
    }

    // Egy minta (sampleRef) lenyomatai a használt modellekkel, fúziós vektorként. A testvér-
    // lenyomatok (ugyanaz a minta, más modell) a sampleRef + forrás-meeting + createdAt szerint
    // tartoznak össze; amelyik mintához nincs minden modellre lenyomat, kimarad.
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
            g.meta.vec = fusion::fuse(g.set, modelIds);
            if (!g.meta.vec.isEmpty()) out.append(g.meta);
        }
        return out;
    }

    // Az újraellenőrzések referenciájához a tárolt lenyomatok, beszélő-indexenként (az al
    // indexelése szerint): az elnevezett beszélő személyének lenyomatai. Ami ebben a
    // megbeszélésben készült, az „itteni" (helyi bizonyíték); ha a mintasora (a sampleRef
    // kezdőideje, mint a fixVoiceprints-nél) e beszélő zárolt sora, az a sor a referenciában nem
    // számít külön (replacedLines) — ugyanaz a hang ne számítson kétszer.
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
                if (vp.vec.size() != dim) continue;   // nem összemérhető (más modell-összetétel)
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

    // A ReferenceInfo → a felületnek szóló összetétel.
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

    // Embeddelt sorok vannak, és legalább egy beszélőnél van megbízható mag (zárolt sorokból
    // vagy itteni lenyomatból).
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

    // ---- lépés-gépezet ------------------------------------------------------
    QVector<RowState> rows() const
    {
        QVector<RowState> out(lines.size());
        for (int i = 0; i < lines.size(); ++i) {
            out[i].speaker = assigned[i];
            out[i].uncertain = uncertain.value(i);
            out[i].noisy = noisyAt(i);
            out[i].likely = recheckedAt(i) ? likelyAt(i) : QString();
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
        // A meeting friss állapota (a speakerMap-et a régi UI / auto-azonosítás is írhatja).
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

    // Az összevont nyers címkék a régi UI/CLI felé is a cél személyét mutassák.
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

    // Overlay + speakerMap-változás kiírása, transcript.md frissítése.
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
        // A transcript.md (az ember által olvasható export) a feloldott nevekkel frissül — de
        // KÉSLELTETVE: a tokenek újraolvasása + a teljes fájl kiírása a meeting hosszával nő
        // (30–80 ms), és soronkénti javításnál minden lépésben fölösleges. Az összefoglaló nem
        // ebből, hanem a tokenekből + az (azonnal mentett) overlay-ből dolgozik.
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
        // Az overlay már a lemezen van (persist menti), ezért a háttérszál csak fájlokat olvas
        // és a transcript.md-t írja; az Impl-hez nem nyúl.
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
                if (markdownPending) flushMarkdown();   // közben újabb javítás érkezett
            }, Qt::QueuedConnection);
        });
    }

    // Lezáráskor (destruktor): a futó háttér-írást bevárjuk, hogy a fájl ne maradjon félben.
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

    // A beszélő zárolt (megerősített / javított), tiszta, embeddelt sorai — a páronkénti
    // átnézés referenciája ezekből épül.
    int lockedVoiceLines(const QString& key) const
    {
        int n = 0;
        for (int i = 0; i < lines.size(); ++i) {
            if (assigned[i] != key || noisyAt(i)) continue;
            if (!fused.contains(lines[i].id)) continue;
            const auto o = ov.utterances.constFind(lines[i].id);
            if (o != ov.utterances.constEnd() && (o->corrected || o->confirmed)) ++n;
        }
        return n;
    }

    // A páronkénti átnézés elemzése (a beszélő-kulcsok közvetlenül, személy-csoportosítás nélkül).
    PairRecheckAnalysis pairAnalysis(const QString& keyA, const QString& keyB) const
    {
        QHash<QString, int> idx;
        const QVector<AnalysisLine> al = analysisLines(&idx, /*groupByPerson*/ false);
        return computePairRecheck(al, idx.value(keyA, -1), idx.value(keyB, -1),
                                  priorsFor(al, idx.size()));
    }

    // A „hasonló sorok" őre elhallgatott: ha a két beszélő elnevezett, és mindkettőnek van
    // elég megerősített sora, felajánljuk a kettejük közötti átnézést.
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

    // A művelet előtti állapothoz képest mi változott → finom jelek.
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

        Command c;
        c.text = text;
        c.before = s.before;
        c.after = ov;
        c.map = delta;
        c.voiceprints = s.voiceprints;
        c.peopleAdded = s.peopleAdded;
        c.affectedSpeakers = s.affectedSpeakers;
        undoStack.append(c);
        if (undoStack.size() > kMaxUndoSteps) undoStack.removeFirst();
        redoStack.clear();

        emitChanges(s, !delta.isEmpty(), !s.peopleAdded.isEmpty(), !s.voiceprints.isEmpty());
    }

    // Egy korábbi overlay-állapot visszatöltése (undo/redo). Az azonosítás mindig a mostani;
    // az elavult-jelző csak akkor jön a pillanatképből, ha azóta nem készült új összefoglaló.
    void restoreOverlay(const SpeakerOverlay& snapshot, const QStringList& affected)
    {
        const SpeakerOverlay cur = ov;
        ov = snapshot;
        ov.identified = cur.identified;
        ov.transcriptFingerprint = cur.transcriptFingerprint;
        if (snapshot.summaryEpoch != cur.summaryEpoch) {
            // Közben új összefoglaló készült (vagy „Rendben így"): ahhoz képest a visszavonás
            // is beszélő-változás.
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
                if (undoing) people->unlist(name);   // a becenevei / megjegyzése megmaradnak
                else people->add(name);
            }

        recomputeAssigned();
        // A speakerMap érintett kulcsai a memóriában is (a persist a lemezre vezeti át).
        for (const MapDelta& dl : c.map) {
            const bool has = undoing ? dl.hadBefore : dl.hasAfter;
            if (has) speakerMap.insert(dl.raw, undoing ? dl.before : dl.after);
            else speakerMap.remove(dl.raw);
        }
        const bool namesChanged = rowNames() != s.names;
        persist(c.map, /*forward*/ !undoing, namesChanged);
        recomputeUncertain();
        emitChanges(s, !c.map.isEmpty(), !c.peopleAdded.isEmpty(), !c.voiceprints.isEmpty());
    }

    // ---- a műveletek építőkövei ---------------------------------------------
    QString createParticipant(const QString& person, Step& s)
    {
        OverlayParticipant p;
        p.key = QStringLiteral("participant:%1").arg(ov.nextParticipant);
        // A szín a felvétel sorrendjét követi a nyers címkék után; később sem változik.
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

    void moveLines(const QVector<int>& idx, const QString& key, Step& s)
    {
        for (int i : idx) {
            OverlayUtterance& u = ov.utterances[lines[i].id];
            u.speaker = key;
            u.corrected = true;     // kézi döntés → többé nem bizonytalan
            u.rechecked = false;
            u.recheckHint.clear();
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
        // A kifejezetten `from`-ra állított sorok és a rá mutató korábbi összevonások a célra.
        for (auto it = ov.utterances.begin(); it != ov.utterances.end(); ++it) {
            if (it->speaker == from) it->speaker = into;
            if (it->recheckHint == from) it->recheckHint = into;
        }
        for (auto it = ov.merged.begin(); it != ov.merged.end(); ++it)
            if (it.value() == from) it.value() = into;
        if (isParticipantKey(from)) {
            ov.participants.erase(
                std::remove_if(ov.participants.begin(), ov.participants.end(),
                               [&](const OverlayParticipant& p) { return p.key == from; }),
                ov.participants.end());
        } else {
            ov.merged.insert(from, into);   // a nyers címke minden (nem felülírt) sora a célhoz megy
        }
        appendUnique(s.affectedSpeakers, into);
    }

    // ---- hanglenyomat -------------------------------------------------------
    // A beszélő sorai közül a lenyomathoz használhatók (hosszabbak elöl), a célhosszig.
    QVector<int> selectPrintLines(const QVector<int>& speakerLines, qint64* usedMs) const
    {
        QVector<int> cand;
        for (int i : speakerLines) {
            if (uncertain.value(i)) continue;   // kétes sorból nem tanítunk
            if (noisyAt(i)) continue;           // egymásra beszéltek: nem tiszta minta
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

    // Lenyomat a megadott sorokból (cache-ből, vagy ha ott nincs, most kiszámolva): minden
    // használt modellel egy-egy Voiceprint (azonos sampleRef / createdAt). Üres = nem sikerült.
    QVector<Voiceprint> buildPrint(const QVector<int>& speakerLines, qint64* usedMs,
                                   int* usedLines, qint64* missingMs)
    {
        qint64 total = 0;
        const QVector<int> picked = selectPrintLines(speakerLines, &total);
        return buildPrintFromPicked(picked, total, kPrintMinTotalMs, usedMs, usedLines, missingMs);
    }

    // Lenyomat KIFEJEZETTEN megadott sorokból (a felhasználó választotta: „minta ebből a
    // sorból”). A szűrést a hívó végzi; itt csak a minimum-hossz és a beágyazás számít. Csak
    // azok a sorok számítanak, amelyekhez minden modellel van vektor.
    QVector<Voiceprint> buildPrintFromPicked(const QVector<int>& picked, qint64 total,
                                             qint64 minTotalMs, qint64* usedMs,
                                             int* usedLines, qint64* missingMs)
    {
        if (usedMs) *usedMs = total;
        if (usedLines) *usedLines = picked.size();
        if (missingMs) *missingMs = std::max<qint64>(0, minTotalMs - total);
        if (total < minTotalMs || modelIds.isEmpty()) return {};

        std::unique_ptr<IUtteranceEmbedder> sync;   // csak ha a cache-ből hiányzik valami
        bool syncFailed = false;
        QMap<QString, QVector<double>> sums;        // modelId → súlyozott összeg
        qint64 embeddedMs = 0, bestDur = -1;
        int best = -1;
        QStringList sourceRefs;   // a ténylegesen beágyazott ablakok (a lusta pótláshoz)
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
            if (set.size() != modelIds.size()) continue;   // nincs minden modellre vektor
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
            vp.embedding = VoiceprintStore::l2normalize(vp.embedding);
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

    // „Téves felismerés": a beszélő ITTENI mintái ki a korábbi személy lenyomatából, és
    // (ha van új személy) be az övébe. Minden lépés a Step-be kerül az undo-hoz.
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
                // A minta ennek a beszélőnek valamelyik sorából való-e.
                for (int i : speakerLines)
                    if (lines[i].startMs <= start && start < lines[i].endMs) { mine = true; break; }
            } else {
                // Nem köthető sorhoz: csak akkor vesszük el, ha a meetingen más nem ez a személy.
                mine = !othersHaveOldPerson;
            }
            if (mine && voiceprints->removePrint(p.id)) {
                removed.append(p);
                s.voiceprints.append(VoiceprintOp{false, oldPerson, p});
            }
        }
        if (newPerson.isEmpty()) return;
        // A választott személyhez friss lenyomat a beszélő soraiból; ha ehhez nincs elég
        // anyag (vagy nincs modell), az elvett mintákat tesszük át hozzá.
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

    // ---- javaslat -----------------------------------------------------------
    void proposeSuggestion(const Step& s, const QVector<int>& moved, const QString& target)
    {
        if (fused.isEmpty() || moved.isEmpty()) return;
        // A leggyakoribb forrás-beszélő az áthelyezett sorok közt.
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

    // ---- embedding-szál -----------------------------------------------------
    void stopThread(bool notify)
    {
        if (!thread) return;
        if (cancel) cancel->store(true);
        thread->wait();
        delete thread;
        thread = nullptr;
        ++epoch;    // a sorban álló eredmények már nem érdekesek
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
    }
};

// =============================================================================

SpeakerEditor::SpeakerEditor(MeetingStore* store, PeopleStore* people, VoiceprintStore* voiceprints,
                             const QString& meetingId, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->q = this;
    d->store = store;
    d->people = people;
    d->voiceprints = voiceprints;
    d->meetingId = meetingId;
    d->load();
}

SpeakerEditor::~SpeakerEditor()
{
    if (d->markdownTimer) d->markdownTimer->stop();
    d->waitMarkdown();   // a háttér-írás ne a félig lebontott objektumra fusson

    d->stopThread(/*notify*/ false);
    d->flushMarkdown();
}

void SpeakerEditor::flushPendingWrites()
{
    // Szinkron szemantika (lezárás, tesztek, CLI): elindítjuk és be is várjuk a háttér-írást;
    // ha közben újabb kérés jött, azt is.
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
}

void SpeakerEditor::setEmbedderFactory(UtteranceEmbedderFactory factory)
{
    d->factory = std::move(factory);
}

void SpeakerEditor::setVoiceModelIds(const QStringList& ids)
{
    const QStringList norm = VoiceModelRegistry::normalizeIds(ids);
    if (norm == d->modelIds) return;
    // A futó szál eredménye modellenként a cache-be kerül, így a váltás után is érvényes.
    d->modelIds = norm;
    d->rebuildFused();
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

// ---- olvasó-modell ----------------------------------------------------------

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

// ---- műveletek --------------------------------------------------------------

bool SpeakerEditor::moveUtterances(const QStringList& utteranceIds, const QString& speakerKey)
{
    if (!d->visible(speakerKey)) return false;
    QVector<int> idx;
    for (int i : d->indicesOf(utteranceIds))
        if (d->assigned[i] != speakerKey) idx.append(i);
    if (idx.isEmpty()) return false;

    Step s = d->begin();
    d->moveLines(idx, speakerKey, s);
    d->commit(s, tr("%n sor áthelyezése ide: %1", nullptr, idx.size())
                     .arg(d->displayOf(speakerKey)));
    d->proposeSuggestion(s, idx, speakerKey);
    return true;
}

QString SpeakerEditor::moveUtterancesToPerson(const QStringList& utteranceIds,
                                              const QString& personName)
{
    const QString person = d->canonicalPerson(personName);
    if (person.isEmpty()) return {};
    const QString existing = d->speakerKeyForPerson(person);
    if (!existing.isEmpty()) {
        moveUtterances(utteranceIds, existing);
        return existing;
    }
    const QVector<int> idx = d->indicesOf(utteranceIds);
    if (idx.isEmpty()) return {};

    Step s = d->begin();
    const QString key = d->createParticipant(person, s);
    d->moveLines(idx, key, s);
    d->commit(s, tr("%n sor áthelyezése ide: %1", nullptr, idx.size()).arg(person));
    d->proposeSuggestion(s, idx, key);
    return key;
}

QString SpeakerEditor::moveUtterancesToNewParticipant(const QStringList& utteranceIds)
{
    const QVector<int> idx = d->indicesOf(utteranceIds);
    if (idx.isEmpty()) return {};

    Step s = d->begin();
    const QString key = d->createParticipant(QString(), s);
    d->moveLines(idx, key, s);
    d->commit(s, tr("%n sor áthelyezése ide: %1", nullptr, idx.size()).arg(d->displayOf(key)));
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

    // Ha a személy már a meeting beszélője, két oszlop lenne ugyanarra az emberre → összevonás.
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
        if (!u.confirmed || (asNoisy && u.noisy != std::optional<bool>(true))) idx.append(i);
    }
    if (idx.isEmpty()) return false;

    Step s = d->begin();
    for (int i : std::as_const(idx)) {
        OverlayUtterance& u = d->ov.utterances[d->lines[i].id];
        u.confirmed = true;
        u.rechecked = false;
        u.recheckHint.clear();
        if (asNoisy) u.noisy = true;
    }
    d->commit(s, asNoisy ? tr("%n sor megerősítése (nem hangminta)", nullptr, idx.size())
                         : tr("%n sor megerősítése", nullptr, idx.size()));
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

// ---- újraellenőrzés ---------------------------------------------------------

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

    // Csoport-index → beszélő-kulcs (a csoport legtöbb sorát vivő beszélő; azonos személy
    // több kulccsal egy csoport).
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
            // Egy újabb újraellenőrzés lecseréli a halmazt: ami már nem kétes, elengedjük.
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
        if (r.fallback) continue;   // a „kevés megerősített sor" üzenet külön szól róla
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

// ---- páronkénti átnézés -----------------------------------------------------

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
        // Csak A és B sorai: a többi beszélő korábbi jelzései érintetlenek.
        if (d->assigned[i] != speakerKeyA && d->assigned[i] != speakerKeyB) continue;
        const QString& id = d->lines[i].id;
        const PairVerdict& v = a.lines[i];
        auto it = d->ov.utterances.find(id);
        if (v.flagged) {
            OverlayUtterance& u = it != d->ov.utterances.end() ? it.value() : d->ov.utterances[id];
            u.rechecked = true;
            u.recheckHint = v.hintedSpeaker == ia ? speakerKeyA : speakerKeyB;
        } else if (it != d->ov.utterances.end() && it->rechecked) {
            // Az A-n és B-n lévő sorok korábbi jelzését ez az átnézés váltja fel.
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

// ---- javaslat ---------------------------------------------------------------

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

// ---- embeddingek ------------------------------------------------------------

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
    // Soronként csak a még hiányzó modellek; a hang dekódolása a szálon EGYSZER történik.
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
    const UtteranceEmbedderFactory factory = d->factory;
    const QString audio = d->audioPath;

    // A szál a saját embedder-példányával dolgozik; az eredményt adagokban, queued hívással
    // adja vissza a fő szálnak. A destruktor megvárja, ezért a `this` végig él.
    d->thread = QThread::create([this, jobs, cancel, factory, audio, epoch] {
        using Batch = QVector<QPair<QString, EmbeddingSet>>;
        auto finish = [this, epoch](bool complete, const QString& error) {
            QMetaObject::invokeMethod(this, [this, epoch, complete, error] {
                d->onFinished(epoch, complete, error);
            }, Qt::QueuedConnection);
        };
        std::unique_ptr<IUtteranceEmbedder> emb = factory();
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

// ---- kézi hanglenyomat ------------------------------------------------------

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
    r.printId = prints.first().id;   // a testvér-lenyomatok (más modellek) ezzel együtt törlődnek
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
    // Csak a beszélő saját, tiszta, legalább 3 mp-es sorai; a bizonytalan sort előbb meg kell
    // erősíteni vagy áthelyezni (a kézi minta sem taníthat kétes sorból).
    QVector<int> picked;
    qint64 total = 0;
    for (const QString& id : utteranceIds) {
        const int i = d->indexById.value(id, -1);
        if (i < 0 || d->assigned.value(i) != speakerKey) continue;
        if (d->uncertain.value(i)) {
            r.error = tr("Ez a sor bizonytalan: előbb erősítsd meg („Jó így”) vagy helyezd át, utána lehet minta.");
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
    r.printId = prints.first().id;   // a testvér-lenyomatok (más modellek) ezzel együtt törlődnek
    r.missingMs = 0;
    emit voiceprintsChanged();
    emit speakersChanged();
    return r;
}

bool SpeakerEditor::removeVoiceprint(const QString& printId)
{
    if (!d->voiceprints || printId.isEmpty()) return false;
    // A lenyomat és a testvérei (ugyanaz a minta más modellekkel: azonos sampleRef, forrás és
    // createdAt) együtt törlődnek — egy kézi készítés egy egységként vonható vissza.
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

// ---- összefoglaló-elavultság / újra-átírás ----------------------------------

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
        reloadTranscript();     // közben az átirat is kicserélődött
        return;
    }
    const SpeakerOverlay disk = loadOverlayFor(d->folder, d->lines);
    if (!sameEdits(disk, d->ov)) {
        // Valaki más írta az overlay-t (pl. globális személy-átnevezés): az undo-pillanatképek
        // már nem érvényesek.
        d->stopThread(/*notify*/ true);
        d->load();
        d->undoStack.clear();
        d->redoStack.clear();
        d->suggestion = SpeakerSuggestion();
        d->pairOffer = PairRecheckOffer();
        emit reloaded();
        emit speakersChanged();
        emit undoStateChanged();
        emit suggestionChanged();
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
}

void SpeakerEditor::reloadTranscript()
{
    const SummaryStaleInfo before = d->staleInfo();
    d->stopThread(/*notify*/ true);
    // Az új átirattal a transcript.md is frissen készült: a függő újragenerálás tárgytalan.
    d->markdownPending = false;
    if (d->markdownTimer) d->markdownTimer->stop();
    d->waitMarkdown();
    d->load();
    d->undoStack.clear();
    d->redoStack.clear();
    d->suggestion = SpeakerSuggestion();
    d->pairOffer = PairRecheckOffer();
    d->embError.clear();
    emit reloaded();
    emit speakersChanged();
    emit uncertainCountChanged(d->uncertainCount());
    emit undoStateChanged();
    emit suggestionChanged();
    const SummaryStaleInfo st = d->staleInfo();
    if (st.stale != before.stale || st.correctedSpeakers != before.correctedSpeakers)
        emit summaryStaleChanged(st.stale, st.correctedSpeakers);
}

} // namespace tanara

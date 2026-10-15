#include "tanara/processing/analysis/ParticipantAnalysis.h"
#include "tanara/domain/SpeakerText.h"
#include "tanara/library/MeetingLayout.h"

#include <QCoreApplication>

#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/domain/EvidenceKeys.h"
#include "tanara/domain/Meeting.h"
#include "tanara/editing/review/CandidateRanker.h"
#include "tanara/editing/review/SideAnalysis.h"
#include "tanara/editing/session/TrackSpeech.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/processing/voiceid/VoiceEmbedder.h"
#include "tanara/processing/voiceid/VoiceEmbedderSet.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace tanara::processing {

// ---- persisted analysis ------------------------------------------------------------------

QString ParticipantAnalysisFile::filePath(const QString& meetingFolder)
{
    return library::MeetingLayout::path(meetingFolder, library::MeetingLayout::participantsAnalysisJson());
}

ParticipantAnalysisFile ParticipantAnalysisFile::load(const QString& meetingFolder)
{
    ParticipantAnalysisFile a;
    QFile f(filePath(meetingFolder));
    if (!f.open(QIODevice::ReadOnly)) return a;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    a.at = o.value(QStringLiteral("at")).toString();
    a.tracksKey = o.value(QStringLiteral("tracksKey")).toString();
    for (const QJsonValue& v : o.value(QStringLiteral("modelIds")).toArray()) a.modelIds << v.toString();
    for (const QJsonValue& v : o.value(QStringLiteral("clusters")).toArray()) {
        const QJsonObject co = v.toObject();
        VoiceCluster c;
        c.trackId = co.value(QStringLiteral("trackId")).toString();
        c.side = co.value(QStringLiteral("side")).toString();
        c.sampleRef = co.value(QStringLiteral("sampleRef")).toString();
        c.participantId = co.value(QStringLiteral("participantId")).toString();
        c.matchPersonId = PersonId::fromString(co.value(QStringLiteral("matchPersonId")).toString());
        c.matchScore = co.value(QStringLiteral("matchScore")).toDouble(-1.0);
        for (const QJsonValue& w : co.value(QStringLiteral("windows")).toArray()) {
            const QJsonArray p = w.toArray();
            if (p.size() == 2) c.windows.append({qint64(p.at(0).toDouble()), qint64(p.at(1).toDouble())});
        }
        a.clusters.append(c);
    }
    return a;
}

bool ParticipantAnalysisFile::save(const QString& meetingFolder) const
{
    QJsonObject o;
    o[QStringLiteral("at")] = at;
    o[QStringLiteral("tracksKey")] = tracksKey;
    o[QStringLiteral("modelIds")] = QJsonArray::fromStringList(modelIds);
    QJsonArray cs;
    for (const VoiceCluster& c : clusters) {
        QJsonObject co;
        co[QStringLiteral("trackId")] = c.trackId;
        co[QStringLiteral("side")] = c.side;
        co[QStringLiteral("sampleRef")] = c.sampleRef;
        co[QStringLiteral("participantId")] = c.participantId;
        if (!c.matchPersonId.isNull()) co[QStringLiteral("matchPersonId")] = c.matchPersonId.toString();
        co[QStringLiteral("matchScore")] = c.matchScore;
        QJsonArray ws;
        for (const auto& w : c.windows) ws.append(QJsonArray{double(w.first), double(w.second)});
        co[QStringLiteral("windows")] = ws;
        cs.append(co);
    }
    o[QStringLiteral("clusters")] = cs;
    QSaveFile f(filePath(meetingFolder));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

namespace participants {

namespace {

qint64 overlapMs(qint64 a0, qint64 a1, qint64 b0, qint64 b1)
{
    return std::max<qint64>(0, std::min(a1, b1) - std::max(a0, b0));
}


bool hasPolarity(const Participant& p, Polarity pol, EvidenceKind kind, bool anyKind = false)
{
    for (const Evidence& e : p.evidence)
        if (e.polarity == pol && (anyKind || e.kind == kind)) return true;
    return false;
}

} // namespace

QString sideOf(TrackKind kind)
{
    switch (kind) {
    case TrackKind::Mic:      return kSideMic;
    case TrackKind::Loopback: return kSideLoopback;
    case TrackKind::Other:    return QString();
    }
    return QString();
}

QString sideLabel(const QString& side)
{
    if (side == kSideMic) return QCoreApplication::translate("ParticipantAnalysis", "mikrofon");
    if (side == kSideLoopback) return QCoreApplication::translate("ParticipantAnalysis", "hívás");
    return QString();
}

QVector<SpeechWindow> speechWindows(const TrackActivity& t, const QString& side)
{
    QVector<SpeechWindow> out;
    if (t.frameMs <= 0) return out;
    const int gapFrames = int(kMaxGapMs / t.frameMs);
    const int n = t.dbAboveFloor.size();
    int i = 0;
    while (i < n) {
        if (!editing::trackspeech::isSpeechFrame(t, i)) { ++i; continue; }
        // One span: speech frames, with pauses of at most gapFrames.
        const int start = i;
        int last = i, gap = 0;
        for (++i; i < n; ++i) {
            if (editing::trackspeech::isSpeechFrame(t, i)) { last = i; gap = 0; }
            else if (++gap > gapFrames) break;
        }
        const qint64 s = t.originMs + qint64(start) * t.frameMs;
        const qint64 e = t.originMs + qint64(last + 1) * t.frameMs;
        // Split into windows of at most kMaxWindowMs; a remainder shorter than kMinWindowMs is dropped.
        for (qint64 w = s; e - w >= kMinWindowMs; w += kMaxWindowMs)
            out.append({t.trackId, side, w, std::min(e, w + kMaxWindowMs)});
        i = last + 1;
    }
    if (out.size() > kMaxWindowsPerTrack) {
        QVector<SpeechWindow> thin;
        for (int k = 0; k < kMaxWindowsPerTrack; ++k)
            thin.append(out.at(int(qint64(k) * out.size() / kMaxWindowsPerTrack)));
        out = thin;
    }
    return out;
}

QVector<int> clusterEmbeddings(const QVector<QVector<float>>& embs, double mergeThreshold)
{
    const int n = embs.size();
    QVector<int> label(n);
    for (int i = 0; i < n; ++i) label[i] = i;
    if (n <= 1) return label;

    QVector<QVector<float>> cent = embs;   // L2-normalized embeddings
    QVector<int> sz(n, 1);
    QVector<bool> alive(n, true);
    for (;;) {
        double best = -2.0; int a = -1, b = -1;
        for (int i = 0; i < n; ++i) if (alive[i])
            for (int j = i + 1; j < n; ++j) if (alive[j]) {
                const double s = library::VoiceprintStore::cosineSimilarity(cent[i], cent[j]);
                if (s > best) { best = s; a = i; b = j; }
            }
        if (a < 0 || best < mergeThreshold) break;
        const int na = sz[a], nb = sz[b];
        QVector<float> mrg(cent[a].size());
        for (int k = 0; k < mrg.size(); ++k)
            mrg[k] = (cent[a][k] * na + cent[b][k] * nb) / (na + nb);
        cent[a] = library::VoiceprintStore::l2normalize(mrg);
        sz[a] = na + nb; alive[b] = false;
        for (int i = 0; i < n; ++i) if (label[i] == b) label[i] = a;
    }
    return label;
}

PcmReader filePcmReader(const Meeting& m, media::MediaToolPtr media)
{
    // Per logical track the spans (in ascending offset order).
    QHash<QString, QVector<Track>> parts;
    const QVector<library::tracknames::SegmentInfo> seg = library::tracknames::segments(m.tracks);
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        parts[m.tracks.at(lead).id].append(m.tracks.at(i));
    }
    for (auto it = parts.begin(); it != parts.end(); ++it)
        std::sort(it->begin(), it->end(),
                  [](const Track& a, const Track& b) { return a.startOffsetMs < b.startOffsetMs; });
    const QString folder = m.folder;
    return [parts, folder, media](const QString& trackId, qint64 startMs, qint64 endMs) -> QVector<float> {
        const QVector<Track> list = parts.value(trackId);
        const Track* use = nullptr;   // the span sounding at the start (the latest started)
        for (const Track& t : list)
            if (t.startOffsetMs <= startMs || !use) use = &t;
        if (!use || use->file.isEmpty()) return {};
        const tanara::tracktiming::FileRange fr = tanara::tracktiming::fileRange(*use, startMs, endMs);
        if (!fr.valid()) return {};
        if (!media) return {};
        return VoiceEmbedder::decodePcm16kMono(*media, QDir(folder).filePath(use->file), fr.startMs, fr.endMs);
    };
}

ClusterSet computeVoiceClusters(const Meeting& m, const MeetingActivity& activity,
                                const VoiceEmbedderSet& embedders, const PcmReader& pcm,
                                const std::function<bool(int, int)>& progress)
{
    ClusterSet out;
    if (!embedders.ensureLoaded()) {
        out.error = QStringLiteral("no voice model");
        return out;
    }
    out.modelIds = embedders.loadedModelIds();

    // The logical tracks' file names for the sample (sampleRef).
    QHash<QString, QString> fileOf;
    for (const Track& t : m.tracks)
        if (!fileOf.contains(t.id)) fileOf.insert(t.id, t.file);

    QVector<QVector<SpeechWindow>> perTrack;
    int total = 0;
    for (const TrackActivity& ta : activity.tracks) {
        perTrack.append(speechWindows(ta, sideOf(ta.kind)));
        total += perTrack.last().size();
    }

    int done = 0;
    for (const QVector<SpeechWindow>& wins : std::as_const(perTrack)) {
        QVector<QVector<float>> embs;
        QVector<EmbeddingSet> sets;
        QVector<SpeechWindow> used;
        for (const SpeechWindow& w : wins) {
            if (progress && !progress(++done, total)) { out.cancelled = true; return out; }
            const QVector<float> audio = pcm ? pcm(w.trackId, w.startMs, w.endMs) : QVector<float>();
            if (audio.isEmpty()) continue;
            const EmbeddingSet set = embedders.embedPcmWith(audio, out.modelIds);
            const QVector<float> fused = fusion::fuse(set, out.modelIds);
            if (fused.isEmpty()) continue;
            embs.append(fused);
            sets.append(set);
            used.append(w);
        }
        if (embs.isEmpty()) continue;
        out.windows += embs.size();

        const QVector<int> labels = clusterEmbeddings(embs, kClusterThreshold);
        QMap<int, QVector<int>> groups;
        for (int i = 0; i < labels.size(); ++i) groups[labels[i]].append(i);
        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            const QVector<int>& idxs = it.value();
            // Noise filter: a single-window cluster is dropped if there is another.
            if (idxs.size() < 2 && groups.size() > 1) continue;

            VoiceCluster c;
            c.trackId = used.at(idxs.first()).trackId;
            c.side = used.at(idxs.first()).side;
            for (int i : idxs) c.windows.append({used.at(i).startMs, used.at(i).endMs});
            for (const QString& id : std::as_const(out.modelIds)) {
                QVector<float> acc;
                for (int i : idxs) {
                    const QVector<float>& v = sets.at(i).value(id);
                    if (acc.isEmpty()) acc.fill(0.0f, v.size());
                    for (int k = 0; k < acc.size() && k < v.size(); ++k) acc[k] += v[k];
                }
                c.centroid.insert(id, library::VoiceprintStore::l2normalize(acc));
            }
            // Medoid: the window nearest to the fused centroid.
            QVector<float> fc(embs.at(idxs.first()).size(), 0.0f);
            for (int i : idxs)
                for (int k = 0; k < fc.size(); ++k) fc[k] += embs.at(i).at(k);
            fc = library::VoiceprintStore::l2normalize(fc);
            int rep = idxs.first();
            double best = -2.0;
            for (int i : idxs) {
                const double s = library::VoiceprintStore::cosineSimilarity(fc, embs.at(i));
                if (s > best) { best = s; rep = i; }
            }
            c.sampleRef = QStringLiteral("%1#%2-%3")
                              .arg(fileOf.value(c.trackId)).arg(used.at(rep).startMs).arg(used.at(rep).endMs);
            out.clusters.append(c);
        }
    }
    return out;
}

QVector<Evidence> participantEvidence(const Participant& p, bool heard, double score,
                                      const Meeting& m, const CandidateContext& ctx)
{
    // Persisted evidence: a key + params only; the text is rendered at display time (domain/EvidenceText.h).
    auto make = [](EvidenceKind kind, Polarity pol, double value, const QLatin1String& key,
                   const QString& fix, const QVariantMap& params = {}) {
        Evidence e;
        e.kind = kind;
        e.polarity = pol;
        e.value = value;
        e.key = key;
        e.fixTarget = fix;
        e.params = params;
        return e;
    };
    const QString samples = QStringLiteral("samples");
    const QString tracks = QStringLiteral("tracks");
    QVector<Evidence> ev;
    const bool named = !p.personId.isNull();
    if (p.source == ParticipantSource::Manual)
        ev.append(make(EvidenceKind::Manual, Polarity::Support, 1.0, evidencekeys::kManualAdded, {}));

    // Voice.
    if (heard && named) {
        ev.append(make(EvidenceKind::Voice, Polarity::Support, score, evidencekeys::kVoiceScore, samples));
    } else if (heard) {
        ev.append(make(EvidenceKind::Voice, Polarity::Neutral, score, evidencekeys::kVoiceUnknown, samples));
    } else if (named) {
        const bool prints = ctx.hasVoiceprint && ctx.hasVoiceprint(p.personId);
        if (prints)
            ev.append(make(EvidenceKind::Voice, Polarity::Contradict, score, evidencekeys::kVoiceNotHeard, samples));
        else
            ev.append(make(EvidenceKind::Voice, Polarity::Neutral, 0.0, evidencekeys::kVoiceNoVoiceprint, samples));
    }

    // Track side: the manual track binding (Participant::tracks) is the truth, if present.
    QStringList manualSides;   // mic first
    for (const QString& side : {kSideMic, kSideLoopback})
        for (const Track& t : m.tracks)
            if (p.tracks.contains(t.id) && sideOf(t.kind) == side && !manualSides.contains(side))
                manualSides << side;
    if (!manualSides.isEmpty()) {
        ev.append(make(EvidenceKind::Side, Polarity::Support, 1.0, evidencekeys::kManualTracks, tracks,
                       {{evidencekeys::kParamSides, manualSides}}));
    }
    // Otherwise the heard side: the user's own voice is expected on the microphone.
    else if (heard && !p.sides.isEmpty()) {
        const QVariantMap sideParams{{evidencekeys::kParamSides, p.sides}};
        const bool self = ctx.self.is(p.personId);
        const bool onMic = p.sides.contains(kSideMic);
        if (self && onMic)
            ev.append(make(EvidenceKind::Side, Polarity::Support, 1.0, evidencekeys::kSideHeard, tracks, sideParams));
        else if (self)
            ev.append(make(EvidenceKind::Side, Polarity::Contradict, 0.0, evidencekeys::kSideCallOnly, tracks));
        else
            ev.append(make(EvidenceKind::Side, Polarity::Neutral, 0.0, evidencekeys::kSideHeard, tracks, sideParams));
    }

    // Tag (UX plan rule 4): a manual tag OR a learned association supports; a contradiction only informs
    // here (neutral): the voice decides the "Who was there?" group and the default checking.
    if (named && ctx.tagEvidence) {
        Evidence e;
        if (editing::ranking::tagEvidenceOf(ctx.tagEvidence(p.personId, m.tagIds, m.id),
                                            personName(ctx.names, p.personId), &e)) {
            if (e.polarity == Polarity::Contradict) e.polarity = Polarity::Neutral;
            ev.append(e);
        }
    }
    return ev;
}

QVector<Participant> buildParticipants(ClusterSet& clusters, const Meeting& m,
                                       const CandidateContext& ctx)
{
    // The survivors: the manual participants (without the previous analysis's evidence).
    QVector<Participant> out;
    QSet<PersonId> manualNames;
    QSet<PersonId> approvedNames;
    QSet<QString> usedIds;
    QHash<PersonId, QStringList> manualTracks;   // person -> manual track binding (a re-analysis keeps it too)
    for (const Participant& p : m.participants) {
        if (p.approved && !p.personId.isNull()) approvedNames.insert(p.personId);
        if (!p.tracks.isEmpty() && !p.personId.isNull()) manualTracks.insert(p.personId, p.tracks);
        if (p.source != ParticipantSource::Manual) continue;
        Participant k = p;
        k.sides.clear();
        k.rawSpeakerIds.clear();
        k.talkShare = 0.0;
        out.append(k);
        usedIds.insert(k.id);
        if (!k.personId.isNull()) manualNames.insert(k.personId);
    }

    // Matching per cluster (a manual participant has an advantage).
    qint64 totalMs = 0;
    for (VoiceCluster& c : clusters.clusters) {
        totalMs += c.speechMs();
        c.matchPersonId = PersonId();
        c.matchScore = -1.0;
        c.participantId.clear();
        const QVector<VoiceMatch> ranked = ctx.rank ? ctx.rank(c.centroid) : QVector<VoiceMatch>();
        PersonId pick;
        double pickScore = -1.0, bestEff = -2.0;
        for (const VoiceMatch& vm : ranked) {
            if (vm.personId.isNull()) continue;
            // Manual track binding ("Which track do they speak on?"): a cluster speaking on another track cannot be
            // them; one speaking on the bound track gets an advantage (like a manual participant).
            const auto bound = manualTracks.constFind(vm.personId);
            const bool hasBinding = bound != manualTracks.constEnd();
            if (hasBinding && !c.trackId.isEmpty() && !bound->contains(c.trackId)) continue;
            const double eff = vm.score + (manualNames.contains(vm.personId) ? kManualBoost : 0.0)
                             + (hasBinding ? kManualBoost : 0.0);
            if (eff > bestEff) { bestEff = eff; pick = vm.personId; pickScore = vm.score; }
        }
        if (bestEff >= kMatchThreshold) { c.matchPersonId = pick; c.matchScore = pickScore; }
        else if (!ranked.isEmpty()) c.matchScore = ranked.first().score;   // without a name, informational
    }

    auto newId = [&usedIds]() {
        for (int n = 1;; ++n) {
            const QString id = QStringLiteral("v%1").arg(n);
            if (!usedIds.contains(id)) { usedIds.insert(id); return id; }
        }
    };

    // Clusters -> participants: the same name is one participant; an anonymous cluster stands separate.
    QHash<PersonId, int> byName;   // person -> index in out
    for (int i = 0; i < out.size(); ++i)
        if (!out.at(i).personId.isNull()) byName.insert(out.at(i).personId, i);
    QHash<int, double> bestScore;   // participant index -> best voice score
    QHash<int, qint64> speech;
    for (VoiceCluster& c : clusters.clusters) {
        int idx = -1;
        if (!c.matchPersonId.isNull()) idx = byName.value(c.matchPersonId, -1);
        if (idx < 0) {
            Participant p;
            p.id = newId();
            p.personId = c.matchPersonId;
            p.source = ParticipantSource::Voice;
            p.approved = !p.personId.isNull() && approvedNames.contains(p.personId);
            if (!p.personId.isNull()) p.tracks = manualTracks.value(p.personId);
            out.append(p);
            idx = out.size() - 1;
            if (!p.personId.isNull()) byName.insert(p.personId, idx);
        }
        Participant& p = out[idx];
        c.participantId = p.id;
        if (!c.side.isEmpty() && !p.sides.contains(c.side)) p.sides << c.side;
        bestScore[idx] = std::max(bestScore.value(idx, -1.0), c.matchScore);
        speech[idx] += c.speechMs();
    }

    for (int i = 0; i < out.size(); ++i) {
        Participant& p = out[i];
        std::sort(p.sides.begin(), p.sides.end());
        const bool heard = speech.contains(i);
        p.talkShare = (heard && totalMs > 0) ? double(speech.value(i)) / double(totalMs) : 0.0;
        p.evidence = participantEvidence(p, heard, bestScore.value(i, -1.0), m, ctx);
    }

    // The participant's listenable sample (its largest cluster that has one) is copied onto the record;
    // the meeting-local "same voice" merges of the previous analysis no longer apply (new clusters).
    for (Participant& p : out) {
        p.mergedFrom.clear();
        p.sampleTrackId.clear();
        p.sampleRef.clear();
        qint64 bestMs = -1;
        for (const VoiceCluster& c : std::as_const(clusters.clusters)) {
            if (c.participantId != p.id || c.sampleRef.isEmpty() || c.trackId.isEmpty() || c.speechMs() <= bestMs)
                continue;
            bestMs = c.speechMs();
            p.sampleTrackId = c.trackId;
            p.sampleRef = c.sampleRef;
        }
    }

    // Order: group (Certain, Doubtful, Invited), within that by speech share; manual, unheard participants
    // at the end of their group.
    std::stable_sort(out.begin(), out.end(), [](const Participant& a, const Participant& b) {
        const int ga = int(participantGroup(a)), gb = int(participantGroup(b));
        if (ga != gb) return ga < gb;
        return a.talkShare > b.talkShare;
    });
    return out;
}

ParticipantGroup participantGroup(const Participant& p)
{
    const bool voice = hasPolarity(p, Polarity::Support, EvidenceKind::Voice);
    if (p.source == ParticipantSource::Calendar && !voice) return ParticipantGroup::InvitedNotHeard;
    if (hasPolarity(p, Polarity::Contradict, EvidenceKind::Voice, /*anyKind*/ true))
        return ParticipantGroup::Doubt;
    bool other = false;
    for (const Evidence& e : p.evidence)
        if (e.polarity == Polarity::Support && e.kind != EvidenceKind::Voice) other = true;
    return (voice && other) ? ParticipantGroup::Sure : ParticipantGroup::Doubt;
}

bool defaultChecked(const Participant& p)
{
    return !p.personId.isNull()
        && !hasPolarity(p, Polarity::Contradict, EvidenceKind::Voice, /*anyKind*/ true);
}

QString rawLabelOf(const QString& id)
{
    const int at = id.lastIndexOf(QLatin1Char('@'));
    return at < 0 ? id : id.left(at);
}

QString rawSideOf(const QString& id)
{
    const int at = id.lastIndexOf(QLatin1Char('@'));
    return at < 0 ? QString() : id.mid(at + 1);
}

QString rawId(const QString& rawLabel, const QString& side)
{
    return side.isEmpty() ? rawLabel : rawLabel + QLatin1Char('@') + side;
}

QString rawDisplay(const QString& id)
{
    const QString side = rawSideOf(id);
    const QString label = speakerLabelText(rawLabelOf(id));   // a raw key in the UI language
    return side.isEmpty() ? label : QStringLiteral("%1 · %2").arg(label, sideLabel(side));
}

bool bindRawSpeakers(QVector<Participant>& ps, const QVector<VoiceCluster>& clusters,
                     const QVector<TranscriptLine>& lines)
{
    QHash<QString, int> idxOf;
    for (int i = 0; i < ps.size(); ++i) idxOf.insert(ps.at(i).id, i);

    // Per raw speaker: (participant, side) -> overlap ms.
    QStringList rawOrder;
    QHash<QString, QHash<int, QHash<QString, qint64>>> ov;
    QHash<QString, qint64> rawDur;
    qint64 allDur = 0;
    for (const TranscriptLine& l : lines) {
        if (l.rawLabel.isEmpty()) continue;
        if (!rawOrder.contains(l.rawLabel)) rawOrder << l.rawLabel;
        rawDur[l.rawLabel] += l.endMs - l.startMs;
        allDur += l.endMs - l.startMs;
        for (const VoiceCluster& c : clusters) {
            const int pi = idxOf.value(c.participantId, -1);
            if (pi < 0) continue;
            qint64 sum = 0;
            for (const auto& w : c.windows) sum += overlapMs(l.startMs, l.endMs, w.first, w.second);
            if (sum > 0) ov[l.rawLabel][pi][c.side] += sum;
        }
    }

    QVector<QStringList> raw(ps.size());
    QVector<double> talk(ps.size(), 0.0);
    for (const QString& r : std::as_const(rawOrder)) {
        const auto& per = ov.value(r);
        qint64 total = 0;
        QHash<QString, qint64> sideTotal;
        QHash<QString, QPair<int, qint64>> bestBySide;   // side -> (participant, overlap)
        QHash<int, qint64> perPart;
        for (auto it = per.cbegin(); it != per.cend(); ++it) {
            for (auto jt = it->cbegin(); jt != it->cend(); ++jt) {
                total += jt.value();
                sideTotal[jt.key()] += jt.value();
                perPart[it.key()] += jt.value();
                const auto cur = bestBySide.value(jt.key(), {-1, 0});
                if (jt.value() > cur.second || (jt.value() == cur.second && it.key() < cur.first))
                    bestBySide.insert(jt.key(), {it.key(), jt.value()});
            }
        }
        if (total <= 0) continue;
        const double share = allDur > 0 ? double(rawDur.value(r)) / double(allDur) : 0.0;
        const auto mic = bestBySide.value(kSideMic, {-1, 0});
        const auto loop = bestBySide.value(kSideLoopback, {-1, 0});
        const qint64 micT = sideTotal.value(kSideMic), loopT = sideTotal.value(kSideLoopback);
        const bool split = mic.first >= 0 && loop.first >= 0 && mic.first != loop.first
            && micT >= kSplitMinShare * total && loopT >= kSplitMinShare * total;
        if (split) {
            raw[mic.first] << rawId(r, kSideMic);
            raw[loop.first] << rawId(r, kSideLoopback);
            const double both = double(micT + loopT);
            talk[mic.first] += share * micT / both;
            talk[loop.first] += share * loopT / both;
        } else {
            int best = -1; qint64 bestV = -1;
            for (auto it = perPart.cbegin(); it != perPart.cend(); ++it)
                if (it.value() > bestV || (it.value() == bestV && it.key() < best)) { best = it.key(); bestV = it.value(); }
            raw[best] << r;
            talk[best] += share;
        }
    }

    bool changed = false;
    for (int i = 0; i < ps.size(); ++i) {
        if (ps[i].rawSpeakerIds != raw[i]) { ps[i].rawSpeakerIds = raw[i]; changed = true; }
        // The speech share from the transcript (if there is a binding); otherwise the analysis's stays.
        if (!raw[i].isEmpty() && !qFuzzyCompare(1.0 + ps[i].talkShare, 1.0 + talk[i])) {
            ps[i].talkShare = talk[i];
            changed = true;
        }
    }
    return changed;
}

QString lineSide(const TranscriptLine& line, const QVector<VoiceCluster>& clusters,
                 const MeetingActivity* activity)
{
    qint64 mic = 0, loop = 0;
    for (const VoiceCluster& c : clusters)
        for (const auto& w : c.windows) {
            const qint64 o = overlapMs(line.startMs, line.endMs, w.first, w.second);
            if (c.side == kSideMic) mic += o;
            else if (c.side == kSideLoopback) loop += o;
        }
    if (mic != loop) return mic > loop ? kSideMic : kSideLoopback;
    if (!activity) return QString();
    // Track energy: the power above the noise floor per side.
    double pm = 0.0, pl = 0.0;
    for (const TrackActivity& t : activity->tracks) {
        const auto db = windowEnergyDb(t, line.startMs, line.endMs);
        if (!db) continue;
        const double p = std::pow(10.0, double(*db) / 10.0) - 1.0;
        if (t.kind == TrackKind::Mic) pm += p;
        else if (t.kind == TrackKind::Loopback) pl += p;
    }
    if (pm == pl) return QString();
    return pm > pl ? kSideMic : kSideLoopback;
}

QVector<SpeakerBinding> approvalBindings(const QVector<Participant>& all, const QStringList& acceptedIds,
                                         const QVector<TranscriptLine>& lines,
                                         const QVector<VoiceCluster>& clusters,
                                         const MeetingActivity* activity, const Meeting* meeting)
{
    // Track veto: if every checked, named participant has a manual track, the line's unambiguous side
    // (the speech-level-normalized classification) excludes whoever has none of their tracks on that side.
    const bool veto = meeting && activity && editing::sides::allNamedParticipantsBound(*meeting, all, acceptedIds);
    QVector<editing::Side> sideOfLine;
    QHash<QString, int> lineIdx;
    QHash<QString, editing::Side> manualOf;   // participant id -> side of their manual tracks
    if (veto) {
        sideOfLine = editing::sides::classifyLineSides(*activity, lines);
        for (int i = 0; i < lines.size(); ++i) lineIdx.insert(lines[i].id, i);
        for (const Participant& p : all)
            if (acceptedIds.contains(p.id) && !p.personId.isNull())
                manualOf.insert(p.id, editing::sides::sideOfTracks(*meeting, p.tracks));
    }
    // The excluded line's best non-excluded owner: the checked, named participant whose clusters cover the
    // most of the line (none -> empty: the line stays anonymous).
    auto alternativeFor = [&](const Participant& self, const TranscriptLine& l, editing::Side ls) -> PersonId {
        QHash<QString, qint64> ov;
        for (const VoiceCluster& c : clusters) {
            if (c.participantId == self.id || !manualOf.contains(c.participantId)) continue;
            if (editing::sides::vetoes(true, manualOf.value(c.participantId), ls)) continue;
            for (const auto& w : c.windows) ov[c.participantId] += overlapMs(l.startMs, l.endMs, w.first, w.second);
        }
        PersonId best;
        qint64 bestV = 0;
        for (const Participant& q : all)
            if (ov.value(q.id) > bestV) { bestV = ov.value(q.id); best = q.personId; }
        return best;
    };

    QVector<SpeakerBinding> out;
    QSet<QString> acceptedRaw;   // raw labels bound to the checked ones
    for (const Participant& p : all) {
        if (!acceptedIds.contains(p.id) || p.personId.isNull()) continue;
        for (const QString& id : p.rawSpeakerIds) {
            const QString label = rawLabelOf(id), side = rawSideOf(id);
            acceptedRaw.insert(label);
            QStringList mine;
            if (side.isEmpty()) {
                if (veto)
                    for (const TranscriptLine& l : lines)
                        if (l.rawLabel == label) mine << l.id;
            } else {
                // A two-sided raw speaker: only that side's lines. An undecidable line goes to the side on which the raw
                // speaker spoke more (the "main" side).
                qint64 micMs = 0, loopMs = 0;
                QStringList undecided;
                for (const TranscriptLine& l : lines) {
                    if (l.rawLabel != label) continue;
                    const QString s = lineSide(l, clusters, activity);
                    if (s == kSideMic) micMs += l.endMs - l.startMs;
                    else if (s == kSideLoopback) loopMs += l.endMs - l.startMs;
                    if (s == side) mine << l.id;
                    else if (s.isEmpty()) undecided << l.id;
                }
                const QString major = micMs >= loopMs ? kSideMic : kSideLoopback;
                if (side == major) mine << undecided;
            }
            // Excluded lines split off: to the best non-excluded participant of the other side, or become anonymous.
            QStringList kept;
            QMap<PersonId, QStringList> moved;  // person (null = anonymous) -> lines
            for (const QString& uid : std::as_const(mine)) {
                const int li = lineIdx.value(uid, -1);
                const editing::Side ls = li >= 0 ? sideOfLine.value(li, editing::Side::Unknown) : editing::Side::Unknown;
                if (!veto || !editing::sides::vetoes(true, manualOf.value(p.id), ls)) { kept << uid; continue; }
                moved[alternativeFor(p, lines[li], ls)] << uid;
            }
            if (side.isEmpty() && moved.isEmpty()) { out.append({label, p.personId, {}}); continue; }
            // The whole raw speaker becomes anonymous (an earlier binding must not carry the excluded lines
            // either), the remaining lines go to the person.
            if (side.isEmpty()) out.append({label, PersonId(), {}});
            if (!kept.isEmpty()) out.append({label, p.personId, kept});
            for (auto it = moved.cbegin(); it != moved.cend(); ++it)
                if (!it.key().isNull()) out.append({label, it.key(), it.value()});
        }
    }
    // (Whole) raw speakers bound only to unchecked participants go anonymous.
    for (const Participant& p : all) {
        if (acceptedIds.contains(p.id)) continue;
        for (const QString& id : p.rawSpeakerIds) {
            const QString label = rawLabelOf(id);
            if (!rawSideOf(id).isEmpty() || acceptedRaw.contains(label)) continue;
            out.append({label, PersonId(), {}});
            acceptedRaw.insert(label);
        }
    }
    return out;
}

} // namespace participants
} // namespace tanara::processing
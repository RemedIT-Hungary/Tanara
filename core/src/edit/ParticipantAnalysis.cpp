#include "tanara/edit/ParticipantAnalysis.h"

#include "tanara/audio/TrackCatalog.h"
#include "tanara/audio/TrackTiming.h"
#include "tanara/edit/TrackSpeech.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceEmbedder.h"
#include "tanara/voiceid/VoiceEmbedderSet.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace tanara {

qint64 VoiceCluster::speechMs() const
{
    qint64 sum = 0;
    for (const auto& w : windows) sum += w.second - w.first;
    return sum;
}

// ---- perzisztált elemzés ------------------------------------------------------------------

QString ParticipantAnalysisFile::filePath(const QString& meetingFolder)
{
    return QDir(meetingFolder).filePath(QStringLiteral("participants.analysis.json"));
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
        c.matchName = co.value(QStringLiteral("matchName")).toString();
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
        co[QStringLiteral("matchName")] = c.matchName;
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

bool isSelf(const QString& name, const QString& self)
{
    return !name.isEmpty() && !self.isEmpty() && name.compare(self, Qt::CaseInsensitive) == 0;
}

bool hasPolarity(const Participant& p, Polarity pol, EvidenceKind kind, bool anyKind = false)
{
    for (const Evidence& e : p.evidence)
        if (e.polarity == pol && (anyKind || e.kind == kind)) return true;
    return false;
}

QString percent(double v) { return QString::number(qRound(std::clamp(v, 0.0, 1.0) * 100.0)); }

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
    if (side == kSideMic) return QStringLiteral("mikrofon");
    if (side == kSideLoopback) return QStringLiteral("hívás");
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
        if (!trackspeech::isSpeechFrame(t, i)) { ++i; continue; }
        // Egy szakasz: beszéd-keretek, legfeljebb gapFrames szünettel.
        const int start = i;
        int last = i, gap = 0;
        for (++i; i < n; ++i) {
            if (trackspeech::isSpeechFrame(t, i)) { last = i; gap = 0; }
            else if (++gap > gapFrames) break;
        }
        const qint64 s = t.originMs + qint64(start) * t.frameMs;
        const qint64 e = t.originMs + qint64(last + 1) * t.frameMs;
        // Darabolás legfeljebb kMaxWindowMs-es ablakokra; a kMinWindowMs-nél rövidebb maradék kimarad.
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

    QVector<QVector<float>> cent = embs;   // L2-normalizált embeddingek
    QVector<int> sz(n, 1);
    QVector<bool> alive(n, true);
    for (;;) {
        double best = -2.0; int a = -1, b = -1;
        for (int i = 0; i < n; ++i) if (alive[i])
            for (int j = i + 1; j < n; ++j) if (alive[j]) {
                const double s = VoiceprintStore::cosineSimilarity(cent[i], cent[j]);
                if (s > best) { best = s; a = i; b = j; }
            }
        if (a < 0 || best < mergeThreshold) break;
        const int na = sz[a], nb = sz[b];
        QVector<float> mrg(cent[a].size());
        for (int k = 0; k < mrg.size(); ++k)
            mrg[k] = (cent[a][k] * na + cent[b][k] * nb) / (na + nb);
        cent[a] = VoiceprintStore::l2normalize(mrg);
        sz[a] = na + nb; alive[b] = false;
        for (int i = 0; i < n; ++i) if (label[i] == b) label[i] = a;
    }
    return label;
}

PcmReader filePcmReader(const Meeting& m)
{
    // Logikai sávonként a szakaszok (eltolás szerint növekvő sorrendben).
    QHash<QString, QVector<Track>> parts;
    const QVector<tracknames::SegmentInfo> seg = tracknames::segments(m.tracks);
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        parts[m.tracks.at(lead).id].append(m.tracks.at(i));
    }
    for (auto it = parts.begin(); it != parts.end(); ++it)
        std::sort(it->begin(), it->end(),
                  [](const Track& a, const Track& b) { return a.startOffsetMs < b.startOffsetMs; });
    const QString folder = m.folder;
    return [parts, folder](const QString& trackId, qint64 startMs, qint64 endMs) -> QVector<float> {
        const QVector<Track> list = parts.value(trackId);
        const Track* use = nullptr;   // a kezdéskor szóló (legkésőbb indult) szakasz
        for (const Track& t : list)
            if (t.startOffsetMs <= startMs || !use) use = &t;
        if (!use || use->file.isEmpty()) return {};
        const tracktiming::FileRange fr = tracktiming::fileRange(*use, startMs, endMs);
        if (!fr.valid()) return {};
        return VoiceEmbedder::decodePcm16kMono(QDir(folder).filePath(use->file), fr.startMs, fr.endMs);
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

    // A logikai sávok fájlneve a mintához (sampleRef).
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
            // Zaj-szűrés: az egyablakos klaszter kimarad, ha van más is.
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
                c.centroid.insert(id, VoiceprintStore::l2normalize(acc));
            }
            // Medoid: a fúziós centroidhoz legközelebbi ablak.
            QVector<float> fc(embs.at(idxs.first()).size(), 0.0f);
            for (int i : idxs)
                for (int k = 0; k < fc.size(); ++k) fc[k] += embs.at(i).at(k);
            fc = VoiceprintStore::l2normalize(fc);
            int rep = idxs.first();
            double best = -2.0;
            for (int i : idxs) {
                const double s = VoiceprintStore::cosineSimilarity(fc, embs.at(i));
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
    QVector<Evidence> ev;
    const bool named = !p.personName.isEmpty();
    if (p.source == ParticipantSource::Manual)
        ev.append({EvidenceKind::Manual, Polarity::Support, 1.0, QStringLiteral("kézzel felvéve"), {}, {}});

    // Hang.
    if (heard && named) {
        ev.append({EvidenceKind::Voice, Polarity::Support, score,
                   QStringLiteral("hang %1%").arg(percent(score)), {}, QStringLiteral("samples")});
    } else if (heard) {
        ev.append({EvidenceKind::Voice, Polarity::Neutral, score, QStringLiteral("ismeretlen hang"),
                   {}, QStringLiteral("samples")});
    } else if (named) {
        const bool prints = ctx.hasVoiceprint && ctx.hasVoiceprint(p.personName);
        if (prints)
            ev.append({EvidenceKind::Voice, Polarity::Contradict, score, QStringLiteral("nem hallottuk"),
                       {}, QStringLiteral("samples")});
        else
            ev.append({EvidenceKind::Voice, Polarity::Neutral, 0.0, QStringLiteral("nincs lenyomata"),
                       {}, QStringLiteral("samples")});
    }

    // Sáv-oldal: a saját hang a mikrofonon várható.
    if (heard && !p.sides.isEmpty()) {
        QStringList labels;
        for (const QString& s : p.sides) labels << sideLabel(s);
        const QString text = labels.join(QStringLiteral(" és "));
        const bool self = isSelf(p.personName, ctx.selfName);
        const bool onMic = p.sides.contains(kSideMic);
        if (self && onMic)
            ev.append({EvidenceKind::Side, Polarity::Support, 1.0, text, {}, QStringLiteral("tracks")});
        else if (self)
            ev.append({EvidenceKind::Side, Polarity::Contradict, 0.0,
                       QStringLiteral("csak a hívás hangján"), {}, QStringLiteral("tracks")});
        else
            ev.append({EvidenceKind::Side, Polarity::Neutral, 0.0, text, {}, QStringLiteral("tracks")});
    }

    // Címke: a megbeszélés és a személy közös címkéi.
    if (named && ctx.personTags) {
        QStringList common;
        for (const QString& id : ctx.personTags(p.personName))
            if (m.tagIds.contains(id)) common << (ctx.tagName ? ctx.tagName(id) : id);
        if (!common.isEmpty())
            ev.append({EvidenceKind::Tag, Polarity::Support, double(common.size()),
                       QStringLiteral("címke: %1").arg(common.first()), common.join(QStringLiteral(", ")),
                       QStringLiteral("tags")});
    }
    return ev;
}

QVector<Participant> buildParticipants(ClusterSet& clusters, const Meeting& m,
                                       const CandidateContext& ctx)
{
    // A megmaradók: a kézi résztvevők (az előző elemzés bizonyítékai nélkül).
    QVector<Participant> out;
    QSet<QString> manualNames;
    QSet<QString> approvedNames;
    QSet<QString> usedIds;
    for (const Participant& p : m.participants) {
        if (p.approved && !p.personName.isEmpty()) approvedNames.insert(p.personName.toLower());
        if (p.source != ParticipantSource::Manual) continue;
        Participant k = p;
        k.sides.clear();
        k.rawSpeakerIds.clear();
        k.talkShare = 0.0;
        out.append(k);
        usedIds.insert(k.id);
        if (!k.personName.isEmpty()) manualNames.insert(k.personName.toLower());
    }

    // Párosítás klaszterenként (kézi résztvevő előnnyel).
    qint64 totalMs = 0;
    for (VoiceCluster& c : clusters.clusters) {
        totalMs += c.speechMs();
        c.matchName.clear();
        c.matchScore = -1.0;
        c.participantId.clear();
        const QVector<VoiceMatch> ranked = ctx.rank ? ctx.rank(c.centroid) : QVector<VoiceMatch>();
        QString pick;
        double pickScore = -1.0, bestEff = -2.0;
        for (const VoiceMatch& vm : ranked) {
            if (vm.name.isEmpty()) continue;
            const double eff = vm.score + (manualNames.contains(vm.name.toLower()) ? kManualBoost : 0.0);
            if (eff > bestEff) { bestEff = eff; pick = vm.name; pickScore = vm.score; }
        }
        if (bestEff >= kMatchThreshold) { c.matchName = pick; c.matchScore = pickScore; }
        else if (!ranked.isEmpty()) c.matchScore = ranked.first().score;   // név nélkül, tájékoztató
    }

    auto newId = [&usedIds]() {
        for (int n = 1;; ++n) {
            const QString id = QStringLiteral("v%1").arg(n);
            if (!usedIds.contains(id)) { usedIds.insert(id); return id; }
        }
    };

    // Klaszterek → résztvevők: azonos név egy résztvevő; névtelen klaszter külön.
    QHash<QString, int> byName;   // kisbetűs név → index az out-ban
    for (int i = 0; i < out.size(); ++i)
        if (!out.at(i).personName.isEmpty()) byName.insert(out.at(i).personName.toLower(), i);
    QHash<int, double> bestScore;   // résztvevő-index → legjobb hang-pontszám
    QHash<int, qint64> speech;
    for (VoiceCluster& c : clusters.clusters) {
        int idx = -1;
        if (!c.matchName.isEmpty()) idx = byName.value(c.matchName.toLower(), -1);
        if (idx < 0) {
            Participant p;
            p.id = newId();
            p.personName = c.matchName;
            p.source = ParticipantSource::Voice;
            p.approved = !p.personName.isEmpty() && approvedNames.contains(p.personName.toLower());
            out.append(p);
            idx = out.size() - 1;
            if (!p.personName.isEmpty()) byName.insert(p.personName.toLower(), idx);
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

    // Sorrend: csoport (Biztos, Kétséges, Meghívott), azon belül beszédarány; a kézi, nem hallott
    // résztvevők a csoportjuk végén.
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
    return !p.personName.isEmpty()
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
    return side.isEmpty() ? id : QStringLiteral("%1 · %2").arg(rawLabelOf(id), sideLabel(side));
}

bool bindRawSpeakers(QVector<Participant>& ps, const QVector<VoiceCluster>& clusters,
                     const QVector<TranscriptLine>& lines)
{
    QHash<QString, int> idxOf;
    for (int i = 0; i < ps.size(); ++i) idxOf.insert(ps.at(i).id, i);

    // Nyers beszélőnként: (résztvevő, oldal) → átfedés ms.
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
        QHash<QString, QPair<int, qint64>> bestBySide;   // oldal → (résztvevő, átfedés)
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
        // A beszédarány az átiratból (ha van kötés); különben marad az elemzésé.
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
    // Sáv-energia: a zajpadló feletti teljesítmény oldalanként.
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
                                         const MeetingActivity* activity)
{
    QVector<SpeakerBinding> out;
    QSet<QString> acceptedRaw;   // a bejelöltekhez kötött nyers címkék
    for (const Participant& p : all) {
        if (!acceptedIds.contains(p.id) || p.personName.isEmpty()) continue;
        for (const QString& id : p.rawSpeakerIds) {
            const QString label = rawLabelOf(id), side = rawSideOf(id);
            acceptedRaw.insert(label);
            if (side.isEmpty()) { out.append({label, p.personName, {}}); continue; }
            // Kétoldalú nyers beszélő: csak az adott oldal sorai. Az eldönthetetlen sor ahhoz az
            // oldalhoz kerül, amelyiken a nyers beszélő többet beszélt (ez a „fő" oldal).
            qint64 micMs = 0, loopMs = 0;
            QStringList mine, undecided;
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
            if (!mine.isEmpty()) out.append({label, p.personName, mine});
        }
    }
    // A csak ki nem jelölt résztvevőkhöz kötött (egész) nyers beszélők névtelenre.
    for (const Participant& p : all) {
        if (acceptedIds.contains(p.id)) continue;
        for (const QString& id : p.rawSpeakerIds) {
            const QString label = rawLabelOf(id);
            if (!rawSideOf(id).isEmpty() || acceptedRaw.contains(label)) continue;
            out.append({label, QString(), {}});
            acceptedRaw.insert(label);
        }
    }
    return out;
}

} // namespace participants
} // namespace tanara

#include "tanara/voiceid/VoiceEval.h"

#include "tanara/store/VoiceprintStore.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace tanara {

namespace {

using Vec = QVector<float>;

Vec meanOf(const QVector<const Vec*>& vs, const Vec* skip = nullptr)
{
    Vec sum;
    int n = 0;
    for (const Vec* v : vs) {
        if (v == skip) continue;
        if (sum.isEmpty()) sum.fill(0.0f, v->size());
        if (v->size() != sum.size()) continue;
        for (int k = 0; k < sum.size(); ++k) sum[k] += (*v)[k];
        ++n;
    }
    if (n == 0) return {};
    return VoiceprintStore::l2normalize(sum);
}

double cosine(const Vec& a, const Vec& b) { return VoiceprintStore::cosineSimilarity(a, b); }

// 2-közép cosine-nal: kezdőpontok a centroidtól legtávolabbi sor és a tőle legtávolabbi.
VoiceEvalSplit twoMeans(const QVector<const Vec*>& vs)
{
    VoiceEvalSplit out;
    out.lines = vs.size();
    if (vs.size() < kVoiceEvalMinSplitLines) return out;
    const Vec all = meanOf(vs);
    int a = 0;
    for (int i = 1; i < vs.size(); ++i)
        if (cosine(*vs[i], all) < cosine(*vs[a], all)) a = i;
    int b = a == 0 ? 1 : 0;
    for (int i = 0; i < vs.size(); ++i)
        if (i != a && cosine(*vs[i], *vs[a]) < cosine(*vs[b], *vs[a])) b = i;
    Vec ca = *vs[a], cb = *vs[b];
    QVector<int> assign(vs.size(), -1);
    for (int iter = 0; iter < 20; ++iter) {
        bool changed = false;
        QVector<const Vec*> ga, gb;
        for (int i = 0; i < vs.size(); ++i) {
            const int g = cosine(*vs[i], ca) >= cosine(*vs[i], cb) ? 0 : 1;
            if (g != assign[i]) { assign[i] = g; changed = true; }
            (g == 0 ? ga : gb).append(vs[i]);
        }
        if (ga.isEmpty() || gb.isEmpty()) break;
        ca = meanOf(ga);
        cb = meanOf(gb);
        if (!changed) break;
    }
    int na = 0;
    for (int g : assign) if (g == 0) ++na;
    const int nb = vs.size() - na;
    out.sizeA = std::max(na, nb);
    out.sizeB = std::min(na, nb);
    if (na > 0 && nb > 0) out.centroidCosine = cosine(ca, cb);
    return out;
}

VoiceEvalSpace evaluateSpace(const QString& id, const QVector<Vec>& vecs,
                             const QVector<VoiceEvalLine>& lines, int speakerCount, qint64 minMs)
{
    VoiceEvalSpace sp;
    sp.id = id;
    sp.linesTotal = lines.size();
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].durationMs >= minMs) ++sp.linesEligible;
        if (!vecs[i].isEmpty()) ++sp.linesCovered;
    }

    // (2) magok: zárolt, nem zajos, vektoros sorok.
    QVector<QVector<const Vec*>> coreMembers(speakerCount);
    QVector<QVector<const Vec*>> speakerLines(speakerCount);
    for (int i = 0; i < lines.size(); ++i) {
        const int s = lines[i].speaker;
        if (s < 0 || s >= speakerCount || vecs[i].isEmpty() || lines[i].noisy) continue;
        speakerLines[s].append(&vecs[i]);
        if (lines[i].locked) coreMembers[s].append(&vecs[i]);
    }
    QVector<Vec> cores(speakerCount);
    sp.coreLines.fill(0, speakerCount);
    for (int s = 0; s < speakerCount; ++s)
        if (coreMembers[s].size() >= kVoiceEvalMinCoreLines) {
            cores[s] = meanOf(coreMembers[s]);
            sp.coreLines[s] = coreMembers[s].size();
        }
    sp.coreCosine.fill(QVector<double>(speakerCount, qQNaN()), speakerCount);
    for (int a = 0; a < speakerCount; ++a)
        for (int b = 0; b < speakerCount; ++b)
            if (!cores[a].isEmpty() && !cores[b].isEmpty()) sp.coreCosine[a][b] = cosine(cores[a], cores[b]);

    // (3) 2-közép beszélőnként.
    for (int s = 0; s < speakerCount; ++s) sp.splits.append(twoMeans(speakerLines[s]));

    // (4) közelebb egy másik maghoz. A saját mag tagjánál a mag önmaga nélkül számít.
    for (int i = 0; i < lines.size(); ++i) {
        const int s = lines[i].speaker;
        if (s < 0 || s >= speakerCount || vecs[i].isEmpty() || cores[s].isEmpty()) continue;
        Vec own = cores[s];
        if (coreMembers[s].contains(&vecs[i])) {
            if (coreMembers[s].size() - 1 < kVoiceEvalMinCoreLines) continue;
            own = meanOf(coreMembers[s], &vecs[i]);
        }
        ++sp.closerChecked;
        const double ownFit = cosine(vecs[i], own);
        for (int o = 0; o < speakerCount; ++o) {
            if (o == s || cores[o].isEmpty()) continue;
            if (cosine(vecs[i], cores[o]) - ownFit >= kVoiceEvalMargin) { ++sp.closerToOther; break; }
        }
    }
    return sp;
}

QJsonValue num(double v) { return std::isnan(v) ? QJsonValue() : QJsonValue(std::round(v * 10000.0) / 10000.0); }

} // namespace

VoiceEvalReport evaluateVoices(const QVector<VoiceEvalLine>& lines,
                               const QVector<VoiceEvalSpeaker>& speakers,
                               const QStringList& modelIds, qint64 minMs)
{
    VoiceEvalReport r;
    r.models = modelIds;
    r.minMs = minMs;
    r.speakers = speakers;
    const int n = speakers.size();
    for (const QString& id : modelIds) {
        QVector<Vec> vecs(lines.size());
        for (int i = 0; i < lines.size(); ++i) vecs[i] = lines[i].vectors.value(id);
        r.spaces.append(evaluateSpace(id, vecs, lines, n, minMs));
    }
    if (modelIds.size() > 1) {
        QVector<Vec> vecs(lines.size());
        for (int i = 0; i < lines.size(); ++i) vecs[i] = fusion::fuse(lines[i].vectors, modelIds);
        r.spaces.append(evaluateSpace(QStringLiteral("fused"), vecs, lines, n, minMs));
    }
    return r;
}

QJsonObject voiceEvalToJson(const VoiceEvalReport& r)
{
    QJsonObject root;
    root[QStringLiteral("models")] = QJsonArray::fromStringList(r.models);
    root[QStringLiteral("minMs")] = double(r.minMs);
    QJsonArray speakers;
    for (const VoiceEvalSpeaker& s : r.speakers)
        speakers.append(QJsonObject{{QStringLiteral("key"), s.key}, {QStringLiteral("name"), s.name}});
    root[QStringLiteral("speakers")] = speakers;
    QJsonArray spaces;
    for (const VoiceEvalSpace& sp : r.spaces) {
        QJsonObject o;
        o[QStringLiteral("id")] = sp.id;
        o[QStringLiteral("linesTotal")] = sp.linesTotal;
        o[QStringLiteral("linesEligible")] = sp.linesEligible;
        o[QStringLiteral("linesCovered")] = sp.linesCovered;
        QJsonArray coreLines;
        for (int c : sp.coreLines) coreLines.append(c);
        o[QStringLiteral("coreLines")] = coreLines;
        QJsonArray matrix;
        for (const QVector<double>& row : sp.coreCosine) {
            QJsonArray jr;
            for (double v : row) jr.append(num(v));
            matrix.append(jr);
        }
        o[QStringLiteral("coreCosine")] = matrix;
        QJsonArray splits;
        for (const VoiceEvalSplit& s : sp.splits)
            splits.append(QJsonObject{{QStringLiteral("lines"), s.lines},
                                      {QStringLiteral("sizeA"), s.sizeA},
                                      {QStringLiteral("sizeB"), s.sizeB},
                                      {QStringLiteral("centroidCosine"), num(s.centroidCosine)}});
        o[QStringLiteral("splits")] = splits;
        o[QStringLiteral("closerChecked")] = sp.closerChecked;
        o[QStringLiteral("closerToOther")] = sp.closerToOther;
        spaces.append(o);
    }
    root[QStringLiteral("spaces")] = spaces;
    return root;
}

} // namespace tanara

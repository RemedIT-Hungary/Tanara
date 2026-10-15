#include "tanara/processing/diarization/DiarizationMerge.h"

#include <QSet>

#include <algorithm>
#include <limits>

namespace tanara::processing::diarizationmerge {

using providers::DiarizeResult;
using providers::SpeakerTurn;

QString rawLabel(int n) { return QStringLiteral("Beszélő %1").arg(n); }

QString speakerKey(const QString& trackId, int index) { return QStringLiteral("%1#%2").arg(trackId).arg(index); }

bool needsSpeakers(const MergedTranscript& t)
{
    if (t.tokens.isEmpty()) return false;
    QSet<QString> labels;
    for (const TranscriptToken& tok : t.tokens) {
        labels.insert(tok.speaker);
        if (labels.size() > 1) return false;
    }
    return true;
}

namespace {

struct IndexedTurn {
    qint64 start = 0;
    qint64 end = 0;
    int speaker = 0;   // index into the speaker keys
};

// Overlap length of [a0, a1) and [b0, b1) (0 if disjoint).
qint64 overlap(qint64 a0, qint64 a1, qint64 b0, qint64 b1) { return std::max<qint64>(0, std::min(a1, b1) - std::max(a0, b0)); }

// Distance between [a0, a1) and [b0, b1) (0 if they touch / overlap).
qint64 gap(qint64 a0, qint64 a1, qint64 b0, qint64 b1)
{
    if (b1 <= a0) return a0 - b1;
    if (a1 <= b0) return b0 - a1;
    return 0;
}

} // namespace

MergeResult apply(MergedTranscript& t, const DiarizeResult& d, const Options& opts)
{
    MergeResult res;
    if (t.tokens.isEmpty()) { res.reason = QStringLiteral("no tokens"); return res; }
    if (!needsSpeakers(t)) { res.reason = QStringLiteral("the transcript already has speakers"); return res; }

    // The speakers (track, index) and their turns.
    QStringList keys;
    QHash<QString, int> keyIndex;
    QVector<IndexedTurn> turns;
    for (const SpeakerTurn& st : d.turns) {
        if (st.endMs <= st.startMs) continue;
        const QString key = speakerKey(st.trackId, st.speakerIndex);
        auto it = keyIndex.find(key);
        if (it == keyIndex.end()) {
            it = keyIndex.insert(key, int(keys.size()));
            keys << key;
        }
        turns.append({st.startMs, st.endMs, it.value()});
    }
    if (turns.isEmpty()) { res.reason = QStringLiteral("the diarization found no speech"); return res; }
    std::sort(turns.begin(), turns.end(), [](const IndexedTurn& a, const IndexedTurn& b) { return a.start < b.start; });
    qint64 maxLen = 0;
    for (const IndexedTurn& x : std::as_const(turns)) maxLen = std::max(maxLen, x.end - x.start);
    const int S = int(keys.size());

    // Rule 2: per token the overlap per speaker (the longest wins), else the nearest turn within snapMs.
    const int N = int(t.tokens.size());
    QVector<int> label(N, -1);
    QVector<QVector<qint64>> weight(N);   // overlap per speaker (for the block majority)
    auto nearest = [&](qint64 a, qint64 b, qint64 limit) {
        int best = -1;
        qint64 bestGap = limit;
        for (const IndexedTurn& x : std::as_const(turns)) {
            const qint64 g = gap(a, b, x.start, x.end);
            if (g < bestGap || (best < 0 && g <= bestGap)) { bestGap = g; best = x.speaker; }
        }
        return best;
    };
    for (int i = 0; i < N; ++i) {
        const TranscriptToken& tok = t.tokens[i];
        const qint64 a = tok.startMs, b = std::max(tok.endMs, tok.startMs + 1);
        QVector<qint64> w(S, 0);
        // Candidates: turns starting before b and not before a - maxLen.
        auto lo = std::lower_bound(turns.cbegin(), turns.cend(), a - maxLen,
                                   [](const IndexedTurn& x, qint64 v) { return x.start < v; });
        for (auto it = lo; it != turns.cend() && it->start < b; ++it) w[it->speaker] += overlap(a, b, it->start, it->end);
        int best = -1;
        qint64 bestW = 0;
        for (int s = 0; s < S; ++s)
            if (w[s] > bestW) { bestW = w[s]; best = s; }
        if (best < 0) best = nearest(a, b, opts.snapMs);
        label[i] = best;
        weight[i] = std::move(w);
    }

    // Rules 3 + 4 per block.
    int blockStart = 0;
    while (blockStart < N) {
        int blockEnd = blockStart + 1;
        while (blockEnd < N && t.tokens[blockEnd].startMs - t.tokens[blockEnd - 1].endMs <= opts.blockGapMs) ++blockEnd;

        // Undecided tokens: the block's majority (by overlap); a block with no decided token: the nearest turn.
        QVector<qint64> sum(S, 0);
        bool anyDecided = false;
        for (int i = blockStart; i < blockEnd; ++i) {
            if (label[i] >= 0) anyDecided = true;
            for (int s = 0; s < S; ++s) sum[s] += weight[i][s];
        }
        int major = -1;
        qint64 majorW = 0;
        for (int s = 0; s < S; ++s)
            if (sum[s] > majorW) { majorW = sum[s]; major = s; }
        if (major < 0) {
            for (int i = blockStart; i < blockEnd && major < 0; ++i) major = label[i];
            if (major < 0 || !anyDecided)
                major = nearest(t.tokens[blockStart].startMs, t.tokens[blockEnd - 1].endMs,
                                std::numeric_limits<qint64>::max());
        }
        for (int i = blockStart; i < blockEnd; ++i)
            if (label[i] < 0) label[i] = major;

        // Islands: a short run joins its neighbour, until every run is a clear one (or one run is left).
        for (;;) {
            struct Run { int from, to; };
            QVector<Run> runs;
            for (int i = blockStart; i < blockEnd; ++i) {
                if (runs.isEmpty() || label[i] != label[runs.last().from]) runs.append({i, i + 1});
                else runs.last().to = i + 1;
            }
            if (runs.size() <= 1) break;
            int island = -1;
            for (int r = 0; r < runs.size() && island < 0; ++r) {
                const int count = runs[r].to - runs[r].from;
                const qint64 dur = t.tokens[runs[r].to - 1].endMs - t.tokens[runs[r].from].startMs;
                if (count < opts.minRunTokens || dur < opts.minRunMs) island = r;
            }
            if (island < 0) break;
            const int into = island > 0 ? label[runs[island - 1].from] : label[runs[island + 1].from];
            for (int i = runs[island].from; i < runs[island].to; ++i) label[i] = into;
        }
        blockStart = blockEnd;
    }

    // Rule 5: "Beszélő N" in the order of the first token.
    QHash<int, QString> names;
    for (int i = 0; i < N; ++i) {
        const int s = label[i];
        if (s < 0) continue;   // (cannot happen: there are turns, so the nearest one exists)
        if (!names.contains(s)) {
            names.insert(s, rawLabel(int(names.size()) + 1));
            res.labels.insert(keys[s], names.value(s));
        }
        TranscriptToken& tok = t.tokens[i];
        const QString& name = names.value(s);
        if (tok.speaker != name) {
            tok.speaker = name;
            ++res.changedTokens;
        }
    }
    t.speakerSource = kSourceLocalDiarization;
    t.speakerModels.clear();
    if (!d.segmentationModelId.isEmpty()) t.speakerModels << d.segmentationModelId;
    t.speakerModels << d.embeddingModelIds;
    res.applied = true;
    return res;
}

} // namespace tanara::processing::diarizationmerge

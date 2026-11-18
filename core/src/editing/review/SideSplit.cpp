#include "tanara/editing/review/SideSplit.h"

#include <QHash>

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace tanara::editing::sides {

namespace {
struct Run {
    Side side = Side::Unknown;
    qint64 startMs = 0;
    qint64 endMs = 0;
};

bool countsAsSide(Side s) { return s == Side::Local || s == Side::Remote; }

} // namespace

int SplitResult::parts() const
{
    int n = 0;
    for (const LineSplit& l : lines) n += int(l.cuts.size()) + 1;
    return n;
}

qint64 SplitResult::affectedMs() const
{
    qint64 ms = 0;
    for (const LineSplit& l : lines) ms += l.endMs - l.startMs;
    return ms;
}

SplitResult splitLinesBySide(const MergedTranscript& transcript, const MeetingActivity& activity)
{
    SplitResult r;
    r.active = bothSidesActive(activity);
    if (!r.active || transcript.tokens.isEmpty()) return r;

    MergedTranscript mt = transcript;   // implicitly shared: only the cut list detaches
    mt.sideCuts.clear();
    const QVector<TranscriptToken>& tok = mt.tokens;
    const bool emission = mt.emissionTiming;
    const QMap<QString, float> speech = speechLevels(activity);

    // A start time shared by several tokens is not a safe cut (the grouping would break the other one too).
    QHash<qint64, int> startCount;
    for (const TranscriptToken& t : tok) ++startCount[t.startMs];

    // Emission-timed (Soniox) subword tokens: a leading space starts a word. Real word spans: every token.
    auto wordStart = [&](const QVector<int>& g, int p) {
        if (p == 0 || !emission) return true;
        const QString& text = tok.at(g.at(p)).text;
        return !text.isEmpty() && text.at(0).isSpace();
    };
    auto partSpanStart = [&](const QVector<int>& g, int p) {
        return std::max<qint64>(0, tok.at(g.at(p)).startMs - (emission ? kSplitLeadMs : 0));
    };

    for (const QVector<int>& g : mt.lineTokenGroups()) {
        const int n = int(g.size());
        if (n < 2 * kSplitMinRunWords) continue;
        const qint64 s = partSpanStart(g, 0);
        const qint64 e = tok.at(g.last()).endMs;
        if (e - s < 2 * kSplitMinRunMs) continue;

        // 1) The sides of the sliding windows -> runs of the same side.
        QVector<Run> runs;
        for (qint64 ws = s; ws < e; ws += kSplitStepMs) {
            const qint64 mid = ws + kSplitStepMs / 2;
            const Side sd = classifyWindow(activity, speech, std::max(s, mid - kSplitWindowMs / 2),
                                           std::min(e, mid + kSplitWindowMs / 2));
            const qint64 we = std::min(e, ws + kSplitStepMs);
            if (!runs.isEmpty() && runs.last().side == sd) runs.last().endMs = we;
            else runs.append({sd, ws, we});
        }

        // 2) The runs that count. Mixed and Unknown windows are neutral: the Local / Remote runs around them
        //    join when they are of the same side (a talker's pauses). Then the weakest run that is too short
        //    or holds too few words is dropped (absorbed: its neighbours join if they are of the same side),
        //    until every run left counts.
        auto wordsIn = [&](qint64 a, qint64 b) {
            int words = 0;
            for (int p = 0; p < n; ++p)
                if (wordStart(g, p) && tok.at(g.at(p)).startMs >= a && tok.at(g.at(p)).startMs < b) ++words;
            return words;
        };
        QVector<Run> counted;
        for (const Run& run : std::as_const(runs)) {
            if (!countsAsSide(run.side)) continue;
            if (!counted.isEmpty() && counted.last().side == run.side) counted.last().endMs = run.endMs;
            else counted.append(run);
        }
        for (;;) {
            int weakest = -1;
            for (int k = 0; k < counted.size(); ++k) {
                const Run& c = counted.at(k);
                if (c.endMs - c.startMs >= kSplitMinRunMs && wordsIn(c.startMs, c.endMs) >= kSplitMinRunWords) continue;
                if (weakest < 0 || c.endMs - c.startMs < counted.at(weakest).endMs - counted.at(weakest).startMs)
                    weakest = k;
            }
            if (weakest < 0) break;
            counted.removeAt(weakest);
            if (weakest > 0 && weakest < counted.size() && counted.at(weakest - 1).side == counted.at(weakest).side) {
                counted[weakest - 1].endMs = counted.at(weakest).endMs;
                counted.removeAt(weakest);
            }
        }
        if (counted.size() < 2) continue;

        // 3) A cut between every two counted runs: at the word boundary nearest to the middle of the change.
        auto wordsBetween = [&](int from, int to) {   // positions [from, to)
            int words = 0;
            for (int p = from; p < to; ++p)
                if (wordStart(g, p)) ++words;
            return words;
        };
        QVector<int> cutPos;
        int lastPos = 0;
        for (int k = 1; k < counted.size(); ++k) {
            const Run& a = counted.at(k - 1);
            const Run& b = counted.at(k);
            const qint64 target = (a.endMs + b.startMs) / 2;
            int best = -1;
            qint64 bestDist = std::numeric_limits<qint64>::max();
            for (int p = lastPos + 1; p < n; ++p) {
                const TranscriptToken& prev = tok.at(g.at(p - 1));
                const TranscriptToken& cur = tok.at(g.at(p));
                if (!wordStart(g, p) || prev.startMs >= cur.startMs || startCount.value(cur.startMs) != 1) continue;
                // Emission timing: the next word's sound begins right after the previous word's stamp.
                const qint64 boundary = emission ? prev.endMs : (prev.endMs + cur.startMs) / 2;
                if (boundary < a.startMs || boundary > b.endMs) continue;
                if (wordsBetween(lastPos, p) < kSplitMinRunWords || wordsBetween(p, n) < kSplitMinRunWords) continue;
                const qint64 dist = std::llabs(boundary - target);
                if (dist < bestDist) { bestDist = dist; best = p; }
            }
            if (best < 0) continue;
            cutPos.append(best);
            lastPos = best;
        }
        if (cutPos.isEmpty()) continue;

        LineSplit ls;
        ls.startMs = tok.at(g.first()).startMs;
        ls.endMs = e;
        ls.rawLabel = tok.at(g.first()).speaker;
        int from = 0;
        for (int i = 0; i <= cutPos.size(); ++i) {
            const int to = i < cutPos.size() ? cutPos.at(i) : n;
            const qint64 ps = partSpanStart(g, from);
            const qint64 pe = tok.at(g.at(to - 1)).endMs;
            ls.partSides.append(classifyWindow(activity, speech, ps, std::max(pe, ps + 1)));
            if (i < cutPos.size()) ls.cuts.append(tok.at(g.at(to)).startMs);
            from = to;
        }
        r.cuts << ls.cuts;
        r.lines.append(ls);
    }
    std::sort(r.cuts.begin(), r.cuts.end());
    r.cuts.erase(std::unique(r.cuts.begin(), r.cuts.end()), r.cuts.end());
    return r;
}

} // namespace tanara::editing::sides

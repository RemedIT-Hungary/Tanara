#include "tanara/stt/SonioxTiming.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace tanara::sonioxtiming {

void padUtterances(QVector<Utterance>& u)
{
    const int n = int(u.size());
    if (n == 0) return;

    // 1) starts: up to the latest end of the strictly earlier-starting lines (same-start lines as a group).
    std::vector<qint64> starts(size_t(n), 0);
    qint64 prefixEnd = std::numeric_limits<qint64>::min();
    for (int i = 0; i < n;) {
        int j = i;
        while (j < n && u[j].startMs == u[i].startMs) ++j;
        for (int k = i; k < j; ++k) {
            const qint64 s = u[k].startMs;
            qint64 ns = std::max(prefixEnd, s - kLeadMs);
            ns = std::min(ns, s);
            starts[size_t(k)] = std::max<qint64>(0, ns);
        }
        for (int k = i; k < j; ++k) prefixEnd = std::max(prefixEnd, u[k].endMs);
        i = j;
    }
    // 2) ends: up to the earliest new start of the strictly later-starting lines.
    qint64 suffixStart = std::numeric_limits<qint64>::max();
    for (int j = n; j > 0;) {
        int i = j - 1;
        while (i > 0 && u[i - 1].startMs == u[j - 1].startMs) --i;
        for (int k = i; k < j; ++k) {
            const qint64 e = u[k].endMs;
            u[k].endMs = std::max(e, std::min(suffixStart, e + kTailMs));
        }
        for (int k = i; k < j; ++k) suffixStart = std::min(suffixStart, starts[size_t(k)]);
        j = i;
    }
    for (int k = 0; k < n; ++k) u[k].startMs = starts[size_t(k)];
}

qint64 medianTokenWidthMs(const QVector<TranscriptToken>& tokens)
{
    if (tokens.isEmpty()) return -1;
    std::vector<qint64> w;
    w.reserve(size_t(tokens.size()));
    for (const TranscriptToken& t : tokens) w.push_back(std::max<qint64>(0, t.endMs - t.startMs));
    const auto mid = w.begin() + std::ptrdiff_t(w.size() / 2);
    std::nth_element(w.begin(), mid, w.end());
    return *mid;
}

bool looksLikeEmissionTiming(const QVector<TranscriptToken>& tokens)
{
    const qint64 m = medianTokenWidthMs(tokens);
    return m >= 0 && m <= kNarrowMedianMs;
}

} // namespace tanara::sonioxtiming

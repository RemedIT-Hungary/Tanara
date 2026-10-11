#include "tanara/stt/SonioxTiming.h"

#include <algorithm>
#include <vector>

namespace tanara::sonioxtiming {

void correctTrackTokens(QVector<TranscriptToken>& tokens)
{
    if (tokens.isEmpty()) return;
    std::stable_sort(tokens.begin(), tokens.end(),
                     [](const TranscriptToken& a, const TranscriptToken& b) { return a.startMs < b.startMs; });

    // 1) kezdetek: előre kLeadMs-mel, de az előző token (eredeti) végénél nem korábbra.
    const int n = int(tokens.size());
    std::vector<qint64> starts(size_t(n), 0);
    for (int i = 0; i < n; ++i) {
        const qint64 s = tokens[i].startMs;
        qint64 ns = s - kLeadMs;
        if (i > 0) ns = std::max(ns, tokens[i - 1].endMs);
        ns = std::min(ns, s);        // átfedő bemenetnél sem kerül későbbre
        starts[size_t(i)] = std::max<qint64>(0, ns);
    }
    // 2) végek: hátra kTailMs-mel, de a következő token új kezdeténél nem későbbre.
    for (int i = 0; i < n; ++i) {
        qint64 ne = tokens[i].endMs + kTailMs;
        if (i + 1 < n) ne = std::min(ne, starts[size_t(i + 1)]);
        tokens[i].startMs = starts[size_t(i)];
        tokens[i].endMs = std::max(ne, tokens[i].startMs);
    }
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

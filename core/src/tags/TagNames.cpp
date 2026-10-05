#include "tanara/tags/TagNames.h"
#include "tanara/library/TextFold.h"

#include <QVector>

#include <algorithm>

namespace tanara {

QString normalizeTagName(const QString& name)
{
    QString s = textfold::normalize(name).simplified();
    int i = 0;
    while (i < s.size() && (s.at(i) == QLatin1Char('#') || s.at(i).isSpace())) ++i;
    return s.mid(i);
}

QString tagKey(const QString& name)
{
    const QString folded = textfold::fold(textfold::normalize(name));
    QString out;
    out.reserve(folded.size());
    for (const QChar c : folded)
        if (c.isLetterOrNumber()) out.append(c);
    return out;
}

int damerauLevenshtein(const QString& a, const QString& b, int maxDistance)
{
    const int n = int(a.size()), m = int(b.size());
    if (maxDistance >= 0 && std::abs(n - m) > maxDistance) return maxDistance + 1;
    if (n == 0) return m;
    if (m == 0) return n;
    // Három sor elég (az átrendezés a kettővel korábbi sort nézi).
    QVector<int> prev2(m + 1), prev(m + 1), cur(m + 1);
    for (int j = 0; j <= m; ++j) prev[j] = j;
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        int rowMin = cur[0];
        for (int j = 1; j <= m; ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            int v = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
            if (i > 1 && j > 1 && a.at(i - 1) == b.at(j - 2) && a.at(i - 2) == b.at(j - 1))
                v = std::min(v, prev2[j - 2] + 1);
            cur[j] = v;
            rowMin = std::min(rowMin, v);
        }
        if (maxDistance >= 0 && rowMin > maxDistance) return maxDistance + 1;
        std::swap(prev2, prev);
        std::swap(prev, cur);
    }
    return prev[m];
}

bool nearDuplicate(const QString& a, const QString& b)
{
    const QString ka = tagKey(a), kb = tagKey(b);
    if (ka.isEmpty() || kb.isEmpty()) return false;
    if (ka == kb) return true;
    const int len = int(std::min(ka.size(), kb.size()));
    const int limit = len >= 10 ? 2 : (len >= 5 ? 1 : 0);
    if (limit == 0) return false;
    return damerauLevenshtein(ka, kb, limit) <= limit;
}

} // namespace tanara

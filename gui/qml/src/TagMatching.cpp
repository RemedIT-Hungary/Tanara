#include "TagMatching.h"

#include "tanara/library/TextFold.h"

#include <QSet>

#include <algorithm>

namespace tanara_qml::tagmatch {

QString normalizeName(const QString& name)
{
    return tanara::textfold::normalize(name).simplified();
}

QString foldName(const QString& name)
{
    return tanara::textfold::fold(tanara::textfold::normalize(name));
}

QString key(const QString& name)
{
    const QString folded = foldName(name);
    QString out;
    out.reserve(folded.size());
    for (const QChar c : folded)
        if (c.isLetterOrNumber()) out += c;
    return out;
}

int editDistance(const QString& a, const QString& b)
{
    const int n = int(a.size()), m = int(b.size());
    QVector<QVector<int>> d(n + 1, QVector<int>(m + 1, 0));
    for (int i = 0; i <= n; ++i) d[i][0] = i;
    for (int j = 0; j <= m; ++j) d[0][j] = j;
    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a.at(i - 1) == b.at(j - 2) && a.at(i - 2) == b.at(j - 1))
                d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
        }
    }
    return d[n][m];
}

bool nearDuplicate(const QString& a, const QString& b)
{
    const QString ka = key(a), kb = key(b);
    if (ka.isEmpty() || kb.isEmpty()) return false;
    if (ka == kb) return true;
    const int len = int(std::min(ka.size(), kb.size()));
    if (len < 5) return false;
    const int allowed = len >= 10 ? 2 : 1;
    if (std::abs(int(ka.size()) - int(kb.size())) > allowed) return false;
    return editDistance(ka, kb) <= allowed;
}

namespace {

struct Hit {
    const TagItem* tag;
    int group;                  // 0: a név eleje · 1: szókezdet · 2: bárhol
    int pos;
};

// A beírt szöveg első előfordulása szókezdeten; -1, ha csak szó belsejében van meg.
int wordStartHit(const QString& folded, const QString& needle)
{
    for (int at = folded.indexOf(needle); at >= 0; at = folded.indexOf(needle, at + 1))
        if (at == 0 || !folded.at(at - 1).isLetterOrNumber()) return at;
    return -1;
}

} // namespace

QVector<InputRow> inputRows(const QVector<TagItem>& all, const QVector<TagItem>& recent,
                            const QString& typed, int limit, const QStringList& excludeIds)
{
    QVector<InputRow> rows;
    const QSet<QString> excluded(excludeIds.cbegin(), excludeIds.cend());
    const QString name = normalizeName(typed);

    if (name.isEmpty()) {
        for (const TagItem& t : recent) {
            if (excluded.contains(t.id)) continue;
            if (rows.size() >= limit) break;
            rows.push_back({QStringLiteral("recent"), t.id, t.name, t.meetingCount, -1, 0});
        }
        return rows;
    }

    const QString needle = foldName(name);
    bool exact = false;
    bool nearMode = false;
    QVector<Hit> hits;
    QVector<const TagItem*> near;
    for (const TagItem& t : all) {
        const QString folded = foldName(t.name);
        if (folded == needle) exact = true;
        if (excluded.contains(t.id)) continue;
        const int at = folded.indexOf(needle);
        if (at >= 0) {
            const int word = at == 0 ? 0 : wordStartHit(folded, needle);
            hits.push_back({&t, at == 0 ? 0 : word > 0 ? 1 : 2, at == 0 ? 0 : word > 0 ? word : at});
        }
        if (nearDuplicate(name, t.name) && folded != needle) near.push_back(&t);
    }
    // Pontos egyezésnél nincs „hasonló már van”: a meglévő a találat.
    nearMode = !exact && !near.isEmpty();

    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.tag->meetingCount != b.tag->meetingCount) return a.tag->meetingCount > b.tag->meetingCount;
        return QString::localeAwareCompare(a.tag->name, b.tag->name) < 0;
    });
    std::stable_sort(near.begin(), near.end(), [](const TagItem* a, const TagItem* b) {
        return a->meetingCount > b->meetingCount;
    });

    auto matchRow = [&](const TagItem* t, const QString& kind) {
        InputRow r{kind, t->id, t->name, t->meetingCount, -1, 0};
        const QString nfc = tanara::textfold::normalize(t->name);
        for (const Hit& h : std::as_const(hits)) {
            if (h.tag != t) continue;
            // A kiemelés csak akkor pontos, ha a név NFC-alakja megegyezik a tárolttal.
            if (nfc == t->name) { r.matchStart = h.pos; r.matchLen = int(needle.size()); }
            break;
        }
        return r;
    };

    QSet<QString> listed;
    if (nearMode) {
        for (const TagItem* t : std::as_const(near)) {
            if (rows.size() >= limit) break;
            rows.push_back(matchRow(t, QStringLiteral("nearDuplicate")));
            listed.insert(t->id);
        }
    }
    for (const Hit& h : std::as_const(hits)) {
        if (rows.size() >= limit) break;
        if (listed.contains(h.tag->id)) continue;
        rows.push_back(matchRow(h.tag, QStringLiteral("match")));
        listed.insert(h.tag->id);
    }
    if (nearMode)
        rows.push_back({QStringLiteral("forceNew"), QString(), name, 0, -1, 0});
    else if (!exact)
        rows.push_back({QStringLiteral("new"), QString(), name, 0, -1, 0});
    return rows;
}

} // namespace tanara_qml::tagmatch

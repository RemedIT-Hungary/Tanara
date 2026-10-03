#include "tanara/library/TextFold.h"

namespace tanara {
namespace textfold {

QString normalize(const QString& s)
{
    return s.normalized(QString::NormalizationForm_C);
}

QString fold(const QString& nfc)
{
    QString out = nfc;
    QChar* d = out.data();
    for (int i = 0; i < out.size(); ++i) {
        QChar c = d[i];
        if (c.unicode() < 0x80) {                 // ASCII gyorsút
            d[i] = c.toLower();
            continue;
        }
        if (c.isSurrogate())
            continue;                             // BMP-n kívüli karakter: érintetlen (1:1 hossz)
        // Kanonikus felbontás alap-karaktere (ő → o, Ű → U, ǘ → ü → u). A kompatibilitási
        // felbontásokat (ﬁ, ½ …) nem bontjuk — azok hosszt váltanának.
        for (int guard = 0; guard < 4; ++guard) {
            if (c.decompositionTag() != QChar::Canonical) break;
            const QString dec = c.decomposition();
            if (dec.isEmpty() || dec.at(0).isSurrogate()) break;
            c = dec.at(0);
        }
        d[i] = c.toLower();
    }
    return out;
}

QString foldQuery(const QString& query)
{
    return fold(normalize(query)).simplified();
}

Range find(const QString& foldedText, const QString& foldedQuery)
{
    if (foldedQuery.isEmpty() || foldedText.isEmpty())
        return {};
    const int at = foldedText.indexOf(foldedQuery);
    if (at >= 0)
        return { at, int(foldedQuery.size()) };
    const QStringList words = foldedQuery.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() < 2)
        return {};
    Range first;
    for (int i = 0; i < words.size(); ++i) {
        const int w = foldedText.indexOf(words.at(i));
        if (w < 0) return {};
        if (i == 0) first = { w, int(words.at(i).size()) };
    }
    return first;
}

QString snippet(const QString& text, const Range& match, Range* matchInSnippet,
                int before, int after)
{
    if (!match.isValid() || match.start >= text.size()) {
        if (matchInSnippet) *matchInSnippet = {};
        return {};
    }
    int from = qMax(0, match.start - before);
    int to   = qMin(int(text.size()), match.start + match.length + after);
    // Szóhatárra igazítás: ne vágjunk szót ketté (de legfeljebb 16 karaktert engedünk).
    if (from > 0 && !text.at(from - 1).isSpace()) {
        int f = from;
        while (f < match.start && f - from < 16 && !text.at(f).isSpace()) ++f;
        if (f < match.start && text.at(f).isSpace()) from = f + 1;
    }
    if (to < text.size() && !text.at(to).isSpace()) {
        int t = to;
        while (t > match.start + match.length && to - t < 16 && !text.at(t - 1).isSpace()) --t;
        if (t > match.start + match.length && text.at(t - 1).isSpace()) to = t - 1;
    }
    QString body = text.mid(from, to - from);
    // Sortörés / tab → szóköz, a hossz megtartásával (a találat pozíciója nem csúszik el).
    for (QChar& c : body)
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r') || c == QLatin1Char('\t'))
            c = QLatin1Char(' ');
    const QString ell = QString(QChar(0x2026));
    QString out;
    int shift = -from;
    if (from > 0) { out += ell; shift += ell.size(); }
    out += body;
    if (to < text.size()) out += ell;
    if (matchInSnippet)
        *matchInSnippet = { match.start + shift, match.length };
    return out;
}

} // namespace textfold
} // namespace tanara

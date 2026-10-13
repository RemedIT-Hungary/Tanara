#include "tanara/foundation/text/Text.h"

#include <QRegularExpression>

namespace tanara {
namespace text {

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
        if (c.unicode() < 0x80) {                 // ASCII fast path
            d[i] = c.toLower();
            continue;
        }
        if (c.isSurrogate())
            continue;                             // a character outside the BMP: untouched (1:1 length)
        // The base character of the canonical decomposition (o-double-acute -> o, U-double-acute -> U,
        // u-diaeresis-acute -> u-diaeresis -> u). We do not break down compatibility decompositions (ligature fi,
        // the half sign ...) since those would change the length.
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
    // Aligning to a word boundary: we do not cut a word in two (but allow at most 16 characters).
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
    // Line break / tab -> space, keeping the length (the match position does not shift).
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

QString foldForSearch(const QString& text)
{
    // Canonical decomposition (o-double-acute -> o + a combining mark), then dropping the combining marks.
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        const QChar::Category cat = c.category();
        if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining
            || cat == QChar::Mark_Enclosing)
            continue;
        out += c;
    }
    return out.toCaseFolded();
}

QString asciiSlug(const QString& in, int maxLength, const QString& fallback)
{
    QString s;
    for (const QChar c : in.normalized(QString::NormalizationForm_KD))
        if (c.category() != QChar::Mark_NonSpacing) s.append(c);
    static const QRegularExpression nonWord(QStringLiteral("[^A-Za-z0-9]+"));
    s.replace(nonWord, QStringLiteral("-"));
    static const QRegularExpression edges(QStringLiteral("^-+|-+$"));
    s.replace(edges, QString());
    s = s.toLower();
    if (maxLength >= 0) s = s.left(maxLength);
    return s.isEmpty() ? fallback : s;
}

QString slug(const QString& in, const QString& fallback)
{
    QString out;
    for (QChar c : in) out += (c.isLetterOrNumber() ? c : QChar('-'));
    while (out.contains(QStringLiteral("--"))) out.replace(QStringLiteral("--"), QStringLiteral("-"));
    return out.isEmpty() ? fallback : out;
}

QString normalizeName(const QString& name)
{
    return normalize(name).simplified();
}

} // namespace text
} // namespace tanara

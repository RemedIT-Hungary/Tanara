#include "tanara/summary/SummarySources.h"
#include "tanara/library/TextFold.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <limits>

namespace tanara {
namespace summarysrc {

namespace {

// Forrás-jelölő: `[t=…]`, `[u…]`, ill. a csupasz `[12:30]` / `[12:30-13:10]` (a modell néha
// így írja). A belseje legfeljebb 80 karakter, zárójel nélkül.
const QRegularExpression& markerRe()
{
    static const QRegularExpression re(QStringLiteral(
        "\\[\\s*((?:t\\s*=|u\\d)[^\\[\\]\\n]{0,80}"
        "|\\d{1,3}:\\d{2}(?::\\d{2})?(?:\\s*[-–—]\\s*\\d{1,3}:\\d{2}(?::\\d{2})?)?"
        "(?:\\s*[,;]\\s*\\d{1,3}:\\d{2}(?::\\d{2})?(?:\\s*[-–—]\\s*\\d{1,3}:\\d{2}(?::\\d{2})?)?)*)\\s*\\]"));
    return re;
}

// Szigorú időértelmezés: mm:ss / mmm:ss / h:mm:ss, a másodperc (és óránál a perc) < 60.
qint64 strictTime(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d{1,3}):(\\d{2})(?::(\\d{2}))?\\s*$"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch()) return -1;
    if (!m.captured(3).isEmpty()) {
        const qint64 h = m.captured(1).toLongLong(), mi = m.captured(2).toLongLong(),
                     se = m.captured(3).toLongLong();
        if (mi >= 60 || se >= 60) return -1;
        return (h * 3600 + mi * 60 + se) * 1000;
    }
    const qint64 mi = m.captured(1).toLongLong(), se = m.captured(2).toLongLong();
    if (se >= 60) return -1;
    return (mi * 60 + se) * 1000;
}

// Egy jelölő belseje → hivatkozások (az értelmezhetetlen elemek kimaradnak).
QVector<SourceRef> parseMarkerBody(const QString& body)
{
    QVector<SourceRef> out;
    static const QRegularExpression sep(QStringLiteral("[,;]|\\s+(?=t\\s*=|u\\d)"));
    static const QRegularExpression range(QStringLiteral("^(.+?)\\s*[-–—]\\s*(\\d.*)$"));
    static const QRegularExpression uid(QStringLiteral("^u\\d+(?:-\\d+)?$"));
    for (QString item : body.split(sep, Qt::SkipEmptyParts)) {
        item = item.trimmed();
        if (item.isEmpty()) continue;
        if (uid.match(item).hasMatch()) {
            SourceRef r;
            r.utteranceId = item;
            out.append(r);
            continue;
        }
        if (item.startsWith(QLatin1Char('t'))) {
            item = item.mid(1).trimmed();
            if (!item.startsWith(QLatin1Char('='))) continue;
            item = item.mid(1).trimmed();
        }
        SourceRef r;
        const QRegularExpressionMatch rm = range.match(item);
        if (rm.hasMatch()) {
            r.startMs = strictTime(rm.captured(1));
            r.endMs = strictTime(rm.captured(2));
            if (r.startMs < 0 || r.endMs < 0) continue;
            if (r.endMs < r.startMs) std::swap(r.startMs, r.endMs);
        } else {
            r.startMs = strictTime(item);
            if (r.startMs < 0) continue;
            r.endMs = r.startMs;
        }
        out.append(r);
    }
    return out;
}

// A jelölők levétele után maradt szóköz-hibák javítása soronként (a sortörések maradnak).
QString tidy(const QString& s)
{
    static const QRegularExpression spaces(QStringLiteral("[ \\t]{2,}"));
    static const QRegularExpression beforePunct(QStringLiteral("[ \\t]+([.,;:!?…])"));
    QStringList lines = s.split(QLatin1Char('\n'));
    for (QString& l : lines) {
        l.replace(spaces, QStringLiteral(" "));
        l.replace(beforePunct, QStringLiteral("\\1"));
        l = l.trimmed();
    }
    return lines.join(QLatin1Char('\n')).trimmed();
}

bool isSentenceEnd(QChar c)
{
    return c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?') || c == QChar(0x2026);
}

bool isCloser(QChar c)
{
    return isSentenceEnd(c) || c == QLatin1Char('"') || c == QChar(0x201D) || c == QLatin1Char(')')
        || c == QChar(0x00BB) || c == QLatin1Char('\'');
}

// A pont előtti szó rövidítés-e (ilyenkor a pont nem mondatvég).
bool endsWithAbbreviation(const QString& sentence)
{
    static const QSet<QString> abbr{
        QStringLiteral("dr"), QStringLiteral("pl"), QStringLiteral("kb"), QStringLiteral("ill"),
        QStringLiteral("ún"), QStringLiteral("vö"), QStringLiteral("mr"), QStringLiteral("ms"),
        QStringLiteral("mrs"), QStringLiteral("id"), QStringLiteral("ifj"), QStringLiteral("e.g"),
        QStringLiteral("i.e"), QStringLiteral("vs"), QStringLiteral("st"), QStringLiteral("sz")};
    QString t = sentence;
    if (!t.endsWith(QLatin1Char('.'))) return false;
    t.chop(1);
    int i = int(t.size());
    while (i > 0 && !t.at(i - 1).isSpace() && t.at(i - 1) != QLatin1Char('(')) --i;
    const QString word = t.mid(i).toLower();
    return abbr.contains(word);
}

void appendSpan(QVector<SourceSpan>& spans, QSet<QString>& seen, const SourceLine& l)
{
    if (seen.contains(l.id)) return;
    seen.insert(l.id);
    SourceSpan sp;
    sp.startMs = l.startMs;
    sp.endMs = l.endMs;
    sp.utteranceIds << l.id;
    spans.append(sp);
}

QVector<SourceSpan> sortedSpans(QVector<SourceSpan> spans)
{
    std::stable_sort(spans.begin(), spans.end(),
                     [](const SourceSpan& a, const SourceSpan& b) { return a.startMs < b.startMs; });
    return spans;
}

} // namespace

QVector<SourceLine> linesFromSegments(const QVector<Utterance>& segments)
{
    QVector<SourceLine> out;
    QHash<QString, int> seen;
    out.reserve(segments.size());
    for (const Utterance& u : segments) {
        SourceLine l;
        const QString base = QStringLiteral("u%1").arg(u.startMs);
        const int n = ++seen[base];
        l.id = n == 1 ? base : QStringLiteral("%1-%2").arg(base).arg(n);
        l.startMs = u.startMs;
        l.endMs = u.endMs;
        l.speakerKey = u.speaker;
        l.speakerName = u.speaker;
        out.append(l);
    }
    return out;
}

MarkedText extractMarkers(const QString& text)
{
    MarkedText out;
    QString rest;
    rest.reserve(text.size());
    int pos = 0;
    QRegularExpressionMatchIterator it = markerRe().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        rest += text.mid(pos, m.capturedStart() - pos);
        // A jelölő előtti szóköz is megy (a sor elején álló jelölő után maradt szóközt a tidy viszi).
        while (!rest.isEmpty() && (rest.back() == QLatin1Char(' ') || rest.back() == QLatin1Char('\t')))
            rest.chop(1);
        if (!rest.isEmpty() && !rest.back().isSpace() && m.capturedEnd() < text.size()
            && !text.at(m.capturedEnd()).isSpace() && !isCloser(text.at(m.capturedEnd()))
            && text.at(m.capturedEnd()) != QLatin1Char(',') && text.at(m.capturedEnd()) != QLatin1Char(';'))
            rest += QLatin1Char(' ');   // „szó[t=1:00]szó” → ne tapadjon össze
        out.refs += parseMarkerBody(m.captured(1));
        pos = m.capturedEnd();
    }
    rest += text.mid(pos);
    out.text = tidy(rest);
    return out;
}

QString stripMarkers(const QString& text)
{
    return extractMarkers(text).text;
}

QVector<MarkedText> splitSentences(const QString& text)
{
    QVector<MarkedText> out;
    QString cur;            // a mondat nyers szövege (jelölőkkel)
    auto flush = [&]() {
        const MarkedText mt = extractMarkers(cur);
        cur.clear();
        if (mt.text.isEmpty()) {
            // Önálló jelölő (pl. a bekezdés végén, mondat nélkül): az előző mondaté.
            if (!out.isEmpty()) out.last().refs += mt.refs;
            return;
        }
        out.append(mt);
    };

    const int n = int(text.size());
    int i = 0;
    while (i < n) {
        // Jelölő: egyben másoljuk (a belső pont / kettőspont nem mondatvég).
        const QRegularExpressionMatch mm = markerRe().match(text, i, QRegularExpression::NormalMatch,
                                                            QRegularExpression::AnchorAtOffsetMatchOption);
        if (mm.hasMatch()) {
            cur += mm.captured();
            i = int(mm.capturedEnd());
            continue;
        }
        const QChar c = text.at(i);
        // Bekezdés-határ: üres sor.
        if (c == QLatin1Char('\n')) {
            int j = i + 1;
            while (j < n && (text.at(j) == QLatin1Char(' ') || text.at(j) == QLatin1Char('\t'))) ++j;
            if (j < n && text.at(j) == QLatin1Char('\n')) {
                flush();
                i = j + 1;
                continue;
            }
        }
        cur += c;
        ++i;
        if (!isSentenceEnd(c)) continue;
        while (i < n && isCloser(text.at(i))) cur += text.at(i++);
        if (endsWithAbbreviation(cur)) continue;
        // Előrenézés: szóközök és a mondat utáni jelölők.
        int j = i;
        QString tail;
        bool sawSpace = false;
        while (j < n) {
            if (text.at(j).isSpace()) { sawSpace = true; tail += text.at(j++); continue; }
            const QRegularExpressionMatch tm = markerRe().match(text, j, QRegularExpression::NormalMatch,
                                                                QRegularExpression::AnchorAtOffsetMatchOption);
            if (tm.hasMatch()) { tail += tm.captured(); j = int(tm.capturedEnd()); continue; }
            break;
        }
        const bool boundary = j >= n || (sawSpace && (text.at(j).isUpper() || text.at(j).isDigit()
                                                     || text.at(j) == QLatin1Char('"')
                                                     || text.at(j) == QChar(0x201E)));
        if (!boundary) continue;
        cur += tail;
        i = j;
        flush();
    }
    flush();
    return out;
}

QVector<SourceSpan> resolveRefs(const QVector<SourceRef>& refs, const QVector<SourceLine>& lines,
                                qint64 toleranceMs)
{
    QVector<SourceSpan> spans;
    QSet<QString> seen;
    for (const SourceRef& r : refs) {
        if (!r.utteranceId.isEmpty()) {
            for (const SourceLine& l : lines)
                if (l.id == r.utteranceId) { appendSpan(spans, seen, l); break; }
            continue;
        }
        if (r.startMs < 0) continue;
        // A jelölt másodperc: [start, end + 999] (a jelölő másodperc-pontosságú).
        const qint64 from = r.startMs;
        const qint64 to = qMax(r.endMs, r.startMs) + 999;
        if (r.endMs > r.startMs) {
            // Tartomány: minden átfedő megszólalás.
            bool any = false;
            for (const SourceLine& l : lines)
                if (l.startMs <= to && l.endMs >= from) { appendSpan(spans, seen, l); any = true; }
            if (any) continue;
        }
        // Egy időpont: a tartalmazó megszólalás (átfedésnél a legközelebbi kezdetű), különben
        // a legközelebbi a tűrésen belül.
        const SourceLine* best = nullptr;
        qint64 bestDist = std::numeric_limits<qint64>::max();
        bool bestContains = false;
        for (const SourceLine& l : lines) {
            const bool contains = l.startMs <= to && l.endMs >= from;
            qint64 dist;
            if (contains) dist = qAbs(l.startMs - from);
            else dist = l.endMs < from ? from - l.endMs : l.startMs - to;
            if (!contains && dist > toleranceMs) continue;
            if ((contains && !bestContains) || (contains == bestContains && dist < bestDist)) {
                best = &l;
                bestDist = dist;
                bestContains = contains;
            }
        }
        if (best) appendSpan(spans, seen, *best);
    }
    return sortedSpans(spans);
}

void fillMemoSpeakers(QVector<MemoSection>& memo, const QVector<SourceLine>& lines)
{
    for (MemoSection& m : memo) {
        m.speakers.clear();
        if (m.startMs < 0) continue;
        const qint64 end = m.endMs >= m.startMs ? m.endMs : m.startMs;
        for (const SourceLine& l : lines) {
            if (l.startMs > end || l.endMs < m.startMs) continue;
            // A határon épp csak érintkező / átlógó sor (a keret vége után kezdődik, ill. a keret
            // előtt kezdődik és alig ér bele) nem számít — a szomszéd szakaszé.
            if (end > m.startMs && l.startMs >= end) continue;
            if (l.startMs < m.startMs && l.endMs - m.startMs < 1000 && end > m.startMs) continue;
            const QString name = l.speakerName.trimmed();
            if (!name.isEmpty() && !m.speakers.contains(name)) m.speakers << name;
        }
    }
}

void attachSources(Summary& s, const QVector<SourceLine>& lines)
{
    s.statements.clear();
    s.sourceSpeakers.clear();
    auto add = [&](StatementKind kind, const QString& prefix, int n, const QString& text,
                   const QVector<SourceRef>& refs, const QString& owner) {
        SummaryStatement st;
        st.id = prefix + QString::number(n);
        st.kind = kind;
        st.text = text;
        st.owner = owner;
        st.sourceSpans = resolveRefs(refs, lines);
        for (const SourceSpan& sp : std::as_const(st.sourceSpans))
            for (const QString& id : sp.utteranceIds)
                for (const SourceLine& l : lines)
                    if (l.id == id) { s.sourceSpeakers.insert(id, SourceSpeaker{l.speakerKey, l.speakerName}); break; }
        s.statements.append(st);
    };

    // Vezetői összefoglaló: mondatonként egy állítás; a tárolt szöveg jelölő nélkül.
    int k = 0;
    for (const MarkedText& mt : splitSentences(s.execSummary))
        add(StatementKind::Statement, QStringLiteral("s"), ++k, mt.text, mt.refs, QString());
    s.execSummary = stripMarkers(s.execSummary);

    k = 0;
    QStringList decisions;
    for (const QString& d : std::as_const(s.decisions)) {
        const MarkedText mt = extractMarkers(d);
        if (mt.text.isEmpty()) continue;
        decisions << mt.text;
        add(StatementKind::Decision, QStringLiteral("d"), ++k, mt.text, mt.refs, QString());
    }
    s.decisions = decisions;

    k = 0;
    QStringList open;
    for (const QString& q : std::as_const(s.openQuestions)) {
        const MarkedText mt = extractMarkers(q);
        if (mt.text.isEmpty()) continue;
        open << mt.text;
        add(StatementKind::OpenQuestion, QStringLiteral("q"), ++k, mt.text, mt.refs, QString());
    }
    s.openQuestions = open;

    k = 0;
    QVector<ActionItem> items;
    for (ActionItem ai : std::as_const(s.actionItems)) {
        MarkedText mt = extractMarkers(ai.text);
        const MarkedText mo = extractMarkers(ai.owner);
        const MarkedText md = extractMarkers(ai.due);
        if (mt.text.isEmpty()) continue;
        mt.refs += mo.refs;
        mt.refs += md.refs;
        ai.text = mt.text;
        ai.owner = mo.text;
        ai.due = md.text;
        items.append(ai);
        add(StatementKind::Todo, QStringLiteral("t"), ++k, ai.text, mt.refs, ai.owner);
    }
    s.actionItems = items;

    // Memó: szakaszonként a bevezető mondatai ("i<n>.<k>") és a pontok ("m<n>.<k>", a points
    // indexe szerint — a jelölő nélkül üres pont előbb kimarad, a pointGroups vele együtt).
    for (int n = 0; n < s.memo.size(); ++n) {
        MemoSection& m = s.memo[n];
        const QString sec = QString::number(n + 1) + QLatin1Char('.');
        k = 0;
        for (const MarkedText& mt : splitSentences(m.intro))
            add(StatementKind::MemoIntro, QStringLiteral("i") + sec, ++k, mt.text, mt.refs, QString());
        m.intro = stripMarkers(m.intro);
        QStringList points, groups;
        QVector<MarkedText> marked;
        for (int i = 0; i < m.points.size(); ++i) {
            const MarkedText mt = extractMarkers(m.points.at(i));
            if (mt.text.isEmpty()) continue;
            points << mt.text;
            groups << m.groupOf(i);
            marked << mt;
        }
        m.points = points;
        m.pointGroups = std::all_of(groups.cbegin(), groups.cend(), [](const QString& g) { return g.isEmpty(); })
                            ? QStringList() : groups;
        for (int i = 0; i < marked.size(); ++i)
            add(StatementKind::MemoPoint, QStringLiteral("m") + sec, i + 1, marked.at(i).text, marked.at(i).refs, QString());
    }
    fillMemoSpeakers(s.memo, lines);
}

QString memoGroupKey(const QString& heading)
{
    QString t = heading.trimmed();
    static const QRegularExpression deco(QStringLiteral("^[#*_\\s]+|[#*_:\\s]+$"));
    t.remove(deco);
    if (t.isEmpty()) return QString();
    const QString f = textfold::foldQuery(t).trimmed();
    static const QStringList args{QStringLiteral("arguments"), QStringLiteral("argument"),
                                  QStringLiteral("arguments raised"), QStringLiteral("pros and cons"),
                                  QStringLiteral("elhangzott ervek"), QStringLiteral("ervek")};
    static const QStringList open{QStringLiteral("unresolved"), QStringLiteral("open question"),
                                  QStringLiteral("open questions"), QStringLiteral("open"),
                                  QStringLiteral("nyitott kerdes"), QStringLiteral("nyitott kerdesek"),
                                  QStringLiteral("nyitott")};
    if (args.contains(f)) return QStringLiteral("arguments");
    if (open.contains(f)) return QStringLiteral("open");
    return t;
}

QString memoGroupLabel(const QString& key)
{
    if (key == QLatin1String("arguments")) return QStringLiteral("Elhangzott érvek");
    if (key == QLatin1String("open"))      return QStringLiteral("Nyitott kérdés");
    return key;
}

QVector<QPair<QString, QVector<int>>> memoGroups(const MemoSection& m)
{
    QVector<QPair<QString, QVector<int>>> out;
    out.append({QString(), {}});
    for (int i = 0; i < m.points.size(); ++i) {
        const QString g = m.groupOf(i);
        auto it = std::find_if(out.begin(), out.end(), [&g](const auto& p) { return p.first == g; });
        if (it == out.end()) { out.append({g, {i}}); continue; }
        it->second.append(i);
    }
    if (out.first().second.isEmpty()) out.removeFirst();
    return out;
}

QString kindToString(StatementKind k)
{
    switch (k) {
    case StatementKind::Decision:     return QStringLiteral("decision");
    case StatementKind::Todo:         return QStringLiteral("todo");
    case StatementKind::OpenQuestion: return QStringLiteral("openQuestion");
    case StatementKind::MemoIntro:    return QStringLiteral("intro");
    case StatementKind::MemoPoint:    return QStringLiteral("memoPoint");
    case StatementKind::Statement: break;
    }
    return QStringLiteral("statement");
}

StatementKind kindFromString(const QString& s)
{
    if (s == QLatin1String("decision"))     return StatementKind::Decision;
    if (s == QLatin1String("todo"))         return StatementKind::Todo;
    if (s == QLatin1String("openQuestion")) return StatementKind::OpenQuestion;
    if (s == QLatin1String("intro"))        return StatementKind::MemoIntro;
    if (s == QLatin1String("memoPoint"))    return StatementKind::MemoPoint;
    return StatementKind::Statement;
}

void applyTargetedStaleness(QVector<SummaryStatement>& statements,
                            const QMap<QString, SourceSpeaker>& then,
                            const QHash<QString, SourceSpeaker>& now,
                            const QStringList& changedKeys, SummaryStaleInfo& info)
{
    // Célzott csak akkor, ha legalább egy állításnak van forrása (különben az egész-dokumentum
    // jelzés a fallback).
    info.targeted = std::any_of(statements.cbegin(), statements.cend(),
                                [](const SummaryStatement& st) { return !st.sourceSpans.isEmpty(); });
    info.affectedStatements = info.affectedTodos = info.ownerChanges = 0;
    info.affectedStatementIds.clear();
    info.affectedUtteranceIds.clear();
    const QSet<QString> changed(changedKeys.cbegin(), changedKeys.cend());

    for (SummaryStatement& st : statements) {
        st.staleBecause.clear();
        st.ownerStaleBecause.clear();
        if (changed.isEmpty()) continue;
        QVector<QPair<QString, QString>> moves;     // (akkor, most) nevek
        for (const SourceSpan& sp : std::as_const(st.sourceSpans)) {
            for (const QString& id : sp.utteranceIds) {
                const auto t = then.constFind(id);
                const auto n = now.constFind(id);
                if (t == then.constEnd() || n == now.constEnd()) continue;   // újragenerált átirat
                if (t->name == n->name) continue;
                if (!changed.contains(t->key) && !changed.contains(n->key)) continue;
                const QString reason = QStringLiteral("%1 → %2?").arg(t->name, n->name);
                if (!st.staleBecause.contains(reason)) st.staleBecause << reason;
                moves.append({t->name, n->name});
                if (!info.affectedUtteranceIds.contains(id)) info.affectedUtteranceIds << id;
            }
        }
        if (st.staleBecause.isEmpty()) continue;
        info.affectedStatementIds << st.id;
        if (st.kind == StatementKind::Todo) {
            ++info.affectedTodos;
            const QString owner = textfold::foldQuery(st.owner).trimmed();
            if (!owner.isEmpty()) {
                for (const auto& mv : std::as_const(moves)) {
                    if (textfold::foldQuery(mv.first).trimmed() == owner) {
                        st.ownerStaleBecause = QStringLiteral("%1 → %2?").arg(st.owner, mv.second);
                        ++info.ownerChanges;
                        break;
                    }
                }
            }
        } else {
            ++info.affectedStatements;
        }
    }
}

} // namespace summarysrc
} // namespace tanara

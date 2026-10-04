#include "tanara/summary/SummaryPipeline.h"
#include "tanara/TranscriptMerger.h"
#include "tanara/library/TextFold.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace tanara {
namespace summarypipe {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("SummaryPipeline", s); }

// Időpont: m:ss, mm:ss, mmm:ss vagy h:mm:ss.
const QString kTime = QStringLiteral("\\d{1,3}:\\d{2}(?::\\d{2})?");

// Időkeret egy címben: [12:30-18:05], (12:30–18:05), 12:30 - 18:05 …
const QRegularExpression& rangeRe()
{
    static const QRegularExpression re(
        QStringLiteral("[\\[(]?\\s*(%1)\\s*(?:-|–|—|to)+\\s*(%1)\\s*[\\])]?").arg(kTime));
    return re;
}

// Egyetlen, zárójelezett időbélyeg egy sor elején: "[12:30]" / "(12:30) –". (Zárójel nélkül
// nem vesszük le: a „14:00-kor kezdünk” szövegből nem vághatunk.)
const QRegularExpression& leadingTimeRe()
{
    static const QRegularExpression re(
        QStringLiteral("^\\s*[\\[(]\\s*%1\\s*[\\])]\\s*[-–—:]?\\s*").arg(kTime));
    return re;
}

const QRegularExpression& bulletRe()
{
    static const QRegularExpression re(QStringLiteral("^\\s*(?:[-*•–]|\\d+[.)])\\s+(.*)$"));
    return re;
}

enum class Sec { None, Topics, Decisions, Open, Actions, Summary };

// Egy sor szakasz-fejléc-e (a díszítések — #, **, kettőspont — nélkül, ékezet-függetlenül).
Sec headingOf(const QString& line)
{
    QString t = line.trimmed();
    if (t.isEmpty() || t.size() > 40) return Sec::None;
    while (!t.isEmpty() && (t.front() == QLatin1Char('#') || t.front() == QLatin1Char('*')
                            || t.front() == QLatin1Char('_') || t.front().isSpace()))
        t.remove(0, 1);
    while (!t.isEmpty() && (t.back() == QLatin1Char('*') || t.back() == QLatin1Char(':')
                            || t.back() == QLatin1Char('_') || t.back().isSpace()))
        t.chop(1);
    const QString f = textfold::foldQuery(t);
    static const QStringList topics{QStringLiteral("topics"), QStringLiteral("topic"),
                                    QStringLiteral("temak"), QStringLiteral("tema"),
                                    QStringLiteral("notes"), QStringLiteral("jegyzetek"),
                                    QStringLiteral("memo")};
    static const QStringList decisions{QStringLiteral("decisions"), QStringLiteral("dontesek")};
    static const QStringList open{QStringLiteral("open"), QStringLiteral("open questions"),
                                  QStringLiteral("open items"), QStringLiteral("nyitott"),
                                  QStringLiteral("nyitott kerdesek")};
    static const QStringList actions{QStringLiteral("actions"), QStringLiteral("action items"),
                                     QStringLiteral("teendok"), QStringLiteral("feladatok")};
    static const QStringList summary{QStringLiteral("summary"), QStringLiteral("osszefoglalo"),
                                     QStringLiteral("osszegzes"), QStringLiteral("summary json")};
    if (topics.contains(f))    return Sec::Topics;
    if (decisions.contains(f)) return Sec::Decisions;
    if (open.contains(f))      return Sec::Open;
    if (actions.contains(f))   return Sec::Actions;
    if (summary.contains(f))   return Sec::Summary;
    return Sec::None;
}

// A „nincs ilyen” jelölések (- none, - nincs, - n/a, - (none), - –).
bool isNoneItem(const QString& text)
{
    QString f = textfold::foldQuery(text);
    f.remove(QRegularExpression(QStringLiteral("[()\\[\\].\\-–—*]")));
    f = f.trimmed();
    return f.isEmpty() || f == QLatin1String("none") || f == QLatin1String("nincs")
        || f == QLatin1String("n/a") || f == QLatin1String("na") || f == QLatin1String("nothing")
        || f == QLatin1String("nincs ilyen") || f == QLatin1String("semmi");
}

// Félkövér / dőlt jelölés levétele egy cím köré.
QString unwrapEmphasis(QString s)
{
    s = s.trimmed();
    while (s.size() >= 2 && (s.startsWith(QLatin1String("**")) || s.startsWith(QLatin1String("__"))))
        s = s.mid(2).trimmed();
    while (s.endsWith(QLatin1String("**")) || s.endsWith(QLatin1String("__")))
        s.chop(2);
    if (s.endsWith(QLatin1Char(':'))) s.chop(1);
    return s.trimmed();
}

// Egy tárgy-cím sor → MemoSection (cím + opcionális időkeret).
MemoSection topicFromTitle(QString title)
{
    MemoSection m;
    title = unwrapEmphasis(title);
    const QRegularExpressionMatch rm = rangeRe().match(title);
    if (rm.hasMatch()) {
        m.startMs = parseTimestamp(rm.captured(1));
        m.endMs = parseTimestamp(rm.captured(2));
        title.remove(rm.capturedStart(), rm.capturedLength());
    } else {
        const QRegularExpressionMatch tm = leadingTimeRe().match(title);
        if (tm.hasMatch()) {
            static const QRegularExpression t(kTime);
            m.startMs = parseTimestamp(t.match(tm.captured()).captured());
            title = title.mid(tm.capturedLength());
        }
    }
    title = unwrapEmphasis(title);
    static const QRegularExpression numPrefix(QStringLiteral("^\\s*\\d+[.)]\\s+"));
    title.remove(numPrefix);
    // A maradék elválasztók (": ", " – ") levétele a cím széléről.
    static const QRegularExpression edges(QStringLiteral("^[\\s:–—-]+|[\\s:–—-]+$"));
    title.remove(edges);
    m.title = title;
    return m;
}

// JSON-töredék sor (a jegyzetbe keveredett JSON nem lesz memó-pont). A „[12:30] …” nem az.
bool looksLikeJson(const QString& t)
{
    if (t.startsWith(QLatin1Char('{')) || t.startsWith(QLatin1Char('}'))
        || t.startsWith(QLatin1Char(']')) || t.startsWith(QLatin1Char('"')))
        return true;
    if (t.startsWith(QLatin1Char('['))) {
        const QString rest = t.mid(1).trimmed();
        return rest.isEmpty() || rest.startsWith(QLatin1Char('"')) || rest.startsWith(QLatin1Char('{'));
    }
    return false;
}

// A hiányzó időket kitölti, a kereteket a rész határai közé szorítja.
void fillTimes(QVector<MemoSection>& topics, qint64 partStart, qint64 partEnd)
{
    for (int i = 0; i < topics.size(); ++i) {
        MemoSection& t = topics[i];
        if (t.startMs < 0)
            t.startMs = (i == 0) ? partStart : qMax(partStart, topics[i - 1].endMs);
        if (t.endMs < 0) {
            qint64 next = -1;
            for (int j = i + 1; j < topics.size() && next < 0; ++j) next = topics[j].startMs;
            t.endMs = next >= 0 ? next : partEnd;
        }
        t.startMs = std::clamp(t.startMs, partStart, qMax(partStart, partEnd));
        t.endMs = std::clamp(t.endMs, partStart, qMax(partStart, partEnd));
        if (t.endMs < t.startMs) t.endMs = t.startMs;
    }
}

PartNotes parseNotesImpl(const QString& raw, const TranscriptPart& part, bool fallbackWithoutHeadings)
{
    PartNotes n;
    n.index = part.index;
    n.startMs = part.startMs;
    n.endMs = part.endMs;

    Sec sec = Sec::None;
    bool sawHeading = false;
    QStringList loose;                  // fejléc nélküli szöveg (fallbackhez)
    QStringList* list = nullptr;        // a futó lista-szakasz (döntés / nyitott / teendő)

    auto currentTopic = [&n]() -> MemoSection& {
        if (n.topics.isEmpty()) n.topics.append(MemoSection{});
        return n.topics.last();
    };

    for (const QString& rawLine : raw.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1String("```"))) continue;   // kódkerítés
        const Sec h = headingOf(line);
        if (h != Sec::None) {
            sawHeading = true;
            sec = h;
            list = h == Sec::Decisions ? &n.decisions
                 : h == Sec::Open      ? &n.open
                 : h == Sec::Actions   ? &n.actions : nullptr;
            if (h == Sec::Summary) break;                       // single: innen a JSON jön
            continue;
        }
        if (sec == Sec::None) { loose << line; continue; }
        if (sec == Sec::Summary) continue;

        const QRegularExpressionMatch bm = bulletRe().match(line);
        if (sec == Sec::Topics) {
            const bool hashTitle = line.startsWith(QLatin1Char('#'));
            const bool boldTitle = !bm.hasMatch() && line.startsWith(QLatin1String("**"))
                                   && unwrapEmphasis(line).size() < 160;
            const bool rangeTitle = !bm.hasMatch() && rangeRe().match(line).hasMatch()
                                    && rangeRe().match(line).capturedStart() <= 1;
            if (hashTitle || boldTitle || rangeTitle) {
                QString t = line;
                while (t.startsWith(QLatin1Char('#'))) t.remove(0, 1);
                MemoSection m = topicFromTitle(t);
                n.topics.append(m);
                continue;
            }
            if (looksLikeJson(line)) continue;
            QString point = bm.hasMatch() ? bm.captured(1).trimmed() : line;
            if (point.isEmpty() || isNoneItem(point)) continue;
            MemoSection& cur = currentTopic();
            if (!bm.hasMatch() && !cur.points.isEmpty())
                cur.points.last() += QLatin1Char(' ') + point;      // tördelt sor folytatása
            else
                cur.points << point;
            continue;
        }
        if (list) {
            QString item = bm.hasMatch() ? bm.captured(1).trimmed() : line;
            if (item.isEmpty() || isNoneItem(item) || looksLikeJson(item)) continue;
            if (!bm.hasMatch() && !list->isEmpty())
                list->last() += QLatin1Char(' ') + item;
            else
                *list << item;
        }
    }

    if (!sawHeading && fallbackWithoutHeadings) {
        // Nincs felismerhető fejléc: a sorok egyetlen tárgy pontjai (a részt nem dobjuk el).
        MemoSection m;
        for (const QString& l : std::as_const(loose)) {
            if (looksLikeJson(l)) continue;
            const QRegularExpressionMatch bm = bulletRe().match(l);
            const QString p = bm.hasMatch() ? bm.captured(1).trimmed() : l;
            if (!p.isEmpty() && !isNoneItem(p)) m.points << p;
        }
        if (!m.points.isEmpty()) n.topics.append(m);
    }

    // Üres (pont és cím nélküli) tárgyak ki.
    n.topics.erase(std::remove_if(n.topics.begin(), n.topics.end(),
                                  [](const MemoSection& m) { return m.title.isEmpty() && m.points.isEmpty(); }),
                   n.topics.end());
    fillTimes(n.topics, part.startMs, part.endMs);
    return n;
}

QString foldedTitle(const QString& s) { return textfold::foldQuery(s).trimmed(); }

QSet<QString> titleWords(const QString& folded)
{
    QSet<QString> out;
    static const QRegularExpression split(QStringLiteral("[^\\p{L}\\p{N}]+"));
    for (const QString& w : folded.split(split, Qt::SkipEmptyParts))
        if (w.size() >= 3 || w.front().isDigit()) out.insert(w);   // a sorszám is megkülönböztet
    return out;
}

// Ugyanarról a tárgyról szól-e a két cím (a részhatáron folytatódó tárgy összevonásához).
bool sameSubject(const QString& a, const QString& b)
{
    const QString fa = foldedTitle(a), fb = foldedTitle(b);
    if (fa.isEmpty() || fb.isEmpty()) return false;
    if (fa == fb) return true;
    const QString& shorter = fa.size() <= fb.size() ? fa : fb;
    const QString& longer  = fa.size() <= fb.size() ? fb : fa;
    if (shorter.size() >= 8 && longer.contains(shorter)) return true;
    const QSet<QString> wa = titleWords(fa), wb = titleWords(fb);
    if (wa.isEmpty() || wb.isEmpty()) return false;
    const int inter = int(QSet<QString>(wa).intersect(wb).size());
    const int uni = int(QSet<QString>(wa).unite(wb).size());
    return uni > 0 && double(inter) / uni >= 0.6;
}

// Egy lista-elem elejéről a modell által másolt időbélyeg levétele.
QString stripLeadingTime(QString s)
{
    const QRegularExpressionMatch m = leadingTimeRe().match(s);
    if (m.hasMatch() && m.capturedLength() < s.size()) s = s.mid(m.capturedLength());
    return s.trimmed();
}

QString valueToText(const QJsonValue& v)
{
    if (v.isString()) return v.toString().trimmed();
    if (v.isDouble()) return QString::number(v.toDouble());
    if (v.isArray()) {
        QStringList parts;
        for (const QJsonValue& x : v.toArray()) {
            const QString t = valueToText(x);
            if (!t.isEmpty()) parts << t;
        }
        return parts.join(QLatin1Char(' '));
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (const char* k : {"text", "decision", "question", "item", "description", "title", "summary"})
            if (o.contains(QLatin1String(k))) return valueToText(o.value(QLatin1String(k)));
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            if (it.value().isString()) return it.value().toString().trimmed();
    }
    return QString();
}

QJsonValue firstOf(const QJsonObject& o, std::initializer_list<const char*> keys, bool* found)
{
    for (const char* k : keys) {
        const QString key = QLatin1String(k);
        if (o.contains(key)) { if (found) *found = true; return o.value(key); }
    }
    return QJsonValue();
}

QStringList textList(const QJsonValue& v)
{
    QStringList out;
    auto add = [&out](QString t) {
        t = stripLeadingTime(t);
        if (t.isEmpty() || isNoneItem(t)) return;
        for (const QString& e : std::as_const(out))
            if (e.compare(t, Qt::CaseInsensitive) == 0) return;
        out << t;
    };
    if (v.isArray()) {
        for (const QJsonValue& x : v.toArray()) add(valueToText(x));
    } else if (v.isString()) {
        for (const QString& line : v.toString().split(QLatin1Char('\n'))) {
            const QRegularExpressionMatch bm = bulletRe().match(line);
            add(bm.hasMatch() ? bm.captured(1) : line);
        }
    }
    return out;
}

QVector<ActionItem> actionList(const QJsonValue& v)
{
    QVector<ActionItem> out;
    QJsonArray arr = v.isArray() ? v.toArray() : QJsonArray();
    if (v.isString())
        for (const QString& line : v.toString().split(QLatin1Char('\n'))) arr.append(line);
    for (const QJsonValue& x : std::as_const(arr)) {
        ActionItem ai;
        if (x.isObject()) {
            const QJsonObject o = x.toObject();
            ai.text  = valueToText(firstOf(o, {"text", "task", "action", "title", "description", "item"}, nullptr));
            ai.owner = valueToText(firstOf(o, {"owner", "assignee", "responsible", "who", "person"}, nullptr));
            ai.due   = valueToText(firstOf(o, {"due", "deadline", "dueDate", "when"}, nullptr));
        } else {
            ai.text = valueToText(x);
            const QRegularExpressionMatch bm = bulletRe().match(ai.text);
            if (bm.hasMatch()) ai.text = bm.captured(1);
        }
        ai.text = stripLeadingTime(ai.text);
        if (ai.owner == QLatin1String("?") || isNoneItem(ai.owner)) ai.owner.clear();
        if (isNoneItem(ai.due) || ai.due == QLatin1String("?")) ai.due.clear();
        if (ai.text.isEmpty() || isNoneItem(ai.text)) continue;
        bool dup = false;
        for (const ActionItem& e : std::as_const(out))
            if (e.text.compare(ai.text, Qt::CaseInsensitive) == 0) { dup = true; break; }
        if (!dup) out.append(ai);
    }
    return out;
}

// A JSON-szöveg egy pozíciójának környezete a hibaüzenethez.
QString snippetAround(const QByteArray& json, int offset)
{
    const int from = qMax(0, offset - 40);
    QString s = QString::fromUtf8(json.mid(from, 80)).simplified();
    return s;
}

} // namespace

// ---- darabolás ------------------------------------------------------------------------

QVector<TranscriptPart> splitTranscript(const QVector<Utterance>& segments, qint64 partMs)
{
    if (partMs <= 0) partMs = kDefaultPartMs;
    QVector<QVector<Utterance>> groups;
    QVector<Utterance> cur;
    qint64 curStart = 0;
    for (const Utterance& u : segments) {
        if (!cur.isEmpty() && u.startMs - curStart >= partMs) {
            groups.append(cur);
            cur.clear();
        }
        if (cur.isEmpty()) curStart = u.startMs;
        cur.append(u);
    }
    if (!cur.isEmpty()) groups.append(cur);

    // A túl rövid utolsó rész (fél célhossz alatt, vagy alig van benne szöveg) az előzőhöz olvad.
    if (groups.size() > 1) {
        const QVector<Utterance>& last = groups.last();
        qint64 end = 0;
        int chars = 0;
        for (const Utterance& u : last) { end = qMax(end, u.endMs); chars += int(u.text.size()); }
        if (end - last.first().startMs < partMs / 2 || chars < 1500) {
            groups[groups.size() - 2] += last;
            groups.removeLast();
        }
    }

    QVector<TranscriptPart> parts;
    for (int i = 0; i < groups.size(); ++i) {
        TranscriptPart p;
        p.index = i;
        p.startMs = groups[i].first().startMs;
        for (const Utterance& u : groups[i]) p.endMs = qMax(p.endMs, u.endMs);
        p.markdown = renderUtterancesMarkdown(groups[i]);
        parts.append(p);
    }
    return parts;
}

QVector<TranscriptPart> splitTranscriptByChars(const QVector<Utterance>& segments, int maxChars)
{
    QVector<TranscriptPart> parts;
    if (segments.isEmpty() || maxChars <= 0) return parts;
    QVector<QVector<Utterance>> groups;
    QVector<Utterance> cur;
    qint64 curLen = 0;
    for (const Utterance& u : segments) {
        // A csoport hossza additív: bekezdésenként a saját sora + "\n\n" elválasztó.
        const qint64 len = renderUtterancesMarkdown({u}).size();
        if (len > maxChars) return {};
        const qint64 next = cur.isEmpty() ? len : curLen + 2 + len;
        if (!cur.isEmpty() && next > maxChars) {
            groups.append(cur);
            cur.clear();
            curLen = len;
        } else {
            curLen = next;
        }
        cur.append(u);
    }
    if (!cur.isEmpty()) groups.append(cur);
    for (int i = 0; i < groups.size(); ++i) {
        TranscriptPart p;
        p.index = i;
        p.startMs = groups[i].first().startMs;
        for (const Utterance& u : groups[i]) p.endMs = qMax(p.endMs, u.endMs);
        p.markdown = renderUtterancesMarkdown(groups[i]);
        parts.append(p);
    }
    return parts;
}

qint64 parseTimestamp(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d{1,3}):(\\d{2})(?::(\\d{2}))?\\s*$"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch()) return -1;
    if (!m.captured(3).isEmpty())
        return (m.captured(1).toLongLong() * 3600 + m.captured(2).toLongLong() * 60
                + m.captured(3).toLongLong()) * 1000;
    return (m.captured(1).toLongLong() * 60 + m.captured(2).toLongLong()) * 1000;
}

QString formatTimestamp(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 sec = ms / 1000;
    return QStringLiteral("%1:%2").arg(sec / 60, 2, 10, QLatin1Char('0'))
                                  .arg(sec % 60, 2, 10, QLatin1Char('0'));
}

// ---- jegyzet --------------------------------------------------------------------------

PartNotes parseNotes(const QString& raw, const TranscriptPart& part)
{
    return parseNotesImpl(raw, part, /*fallbackWithoutHeadings*/ true);
}

QVector<MemoSection> assembleMemo(const QVector<PartNotes>& parts)
{
    QVector<MemoSection> out;
    for (const PartNotes& p : parts) {
        for (const MemoSection& t : p.topics) {
            if (t.points.isEmpty() && t.title.isEmpty()) continue;
            // Összevonás csak észszerű hosszig: egy 20 percnél hosszabb szakasz már nem olvasható
            // memó-egység (a modell néha egy egész részt egyetlen, az előzővel azonos című blokkba
            // tesz) — ilyenkor azonos címmel, külön időkerettel marad.
            const bool fits = out.isEmpty() || t.endMs < 0 || out.last().startMs < 0
                              || t.endMs - out.last().startMs <= kMaxMemoJoinMs;
            const bool join = !out.isEmpty() && fits
                              && (t.title.isEmpty() || sameSubject(out.last().title, t.title));
            if (!join) {
                MemoSection m = t;
                if (m.title.isEmpty()) m.title = tr("Jegyzet");
                out.append(m);
                continue;
            }
            MemoSection& last = out.last();
            for (const QString& pt : t.points) {
                const QString f = foldedTitle(pt);
                bool dup = false;
                for (const QString& e : std::as_const(last.points))
                    if (foldedTitle(e) == f) { dup = true; break; }
                if (!dup) last.points << pt;
            }
            if (t.startMs >= 0 && (last.startMs < 0 || t.startMs < last.startMs)) last.startMs = t.startMs;
            if (t.endMs > last.endMs) last.endMs = t.endMs;
        }
    }
    return out;
}

QString renderNotesForMerge(const QVector<PartNotes>& parts, const QStringList& speakers)
{
    QString out;
    if (!speakers.isEmpty())
        out += QStringLiteral("Speakers: ") + speakers.join(QStringLiteral(", ")) + QStringLiteral("\n\n");
    auto list = [&out](const char* head, const QStringList& items) {
        out += QLatin1String(head) + QLatin1Char('\n');
        if (items.isEmpty()) out += QStringLiteral("- none\n");
        for (const QString& i : items) out += QStringLiteral("- ") + i + QLatin1Char('\n');
    };
    for (const PartNotes& p : parts) {
        out += QStringLiteral("=== PART %1 of %2 [%3-%4] ===\n")
                   .arg(p.index + 1).arg(parts.size())
                   .arg(formatTimestamp(p.startMs), formatTimestamp(p.endMs));
        out += QStringLiteral("TOPICS\n");
        for (const MemoSection& t : p.topics) {
            out += QStringLiteral("### [%1-%2] %3\n")
                       .arg(formatTimestamp(t.startMs), formatTimestamp(t.endMs), t.title);
            for (const QString& pt : t.points) out += QStringLiteral("- ") + pt + QLatin1Char('\n');
        }
        list("DECISIONS", p.decisions);
        list("OPEN", p.open);
        list("ACTIONS", p.actions);
        out += QLatin1Char('\n');
    }
    return out.trimmed() + QLatin1Char('\n');
}

// ---- JSON -----------------------------------------------------------------------------

QByteArray extractJsonObject(const QString& raw)
{
    const QString s = raw;
    const int start = s.indexOf(QLatin1Char('{'));
    if (start < 0) return QByteArray();
    // A hozzá tartozó záró '}' (sztring-tudatosan); ha nincs (csonka), a szöveg végéig.
    int depth = 0;
    bool inStr = false;
    int end = -1;
    for (int i = start; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (inStr) {
            if (c == QLatin1Char('\\')) { ++i; continue; }
            if (c == QLatin1Char('"')) inStr = false;
            continue;
        }
        if (c == QLatin1Char('"')) inStr = true;
        else if (c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char('}')) { if (--depth == 0) { end = i; break; } }
    }
    QString body = end >= 0 ? s.mid(start, end - start + 1) : s.mid(start);
    // Csonka válasz végén maradt záró kódkerítés.
    if (end < 0) {
        const int fence = body.lastIndexOf(QLatin1String("```"));
        if (fence > 0) body = body.left(fence);
    }
    return body.toUtf8();
}

QByteArray repairJson(const QByteArray& json)
{
    const QString s = QString::fromUtf8(json);
    QString out;
    out.reserve(s.size() + 16);
    QVector<QChar> stack;
    bool inStr = false;
    bool smartOpen = false;   // a sztringet „okos” idézőjel nyitotta (akkor az is zárhatja)

    auto nextNonSpace = [&s](int from) {
        int j = from;
        while (j < s.size() && s.at(j).isSpace()) ++j;
        return j;
    };
    auto lastSignificant = [&out]() -> QChar {
        for (int k = out.size() - 1; k >= 0; --k)
            if (!out.at(k).isSpace()) return out.at(k);
        return QChar();
    };
    auto dropTrailingComma = [&out]() {
        int k = out.size() - 1;
        while (k >= 0 && out.at(k).isSpace()) --k;
        if (k >= 0 && out.at(k) == QLatin1Char(',')) out.remove(k, 1);
    };
    // Egy érték / kulcs kezdete előtt hiányzó vessző pótlása (pl. "a" "b" egy tömbben).
    auto commaIfNeeded = [&]() {
        if (stack.isEmpty()) return;
        const QChar p = lastSignificant();
        if (p == QLatin1Char('"') || p == QLatin1Char('}') || p == QLatin1Char(']') || p.isDigit())
            out += QLatin1Char(',');
    };

    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (inStr) {
            if (c == QLatin1Char('\\')) {
                out += c;
                if (i + 1 < s.size()) out += s.at(++i);
                continue;
            }
            const bool smart = smartOpen && (c == QChar(0x201C) || c == QChar(0x201D));
            if (c == QLatin1Char('"') || smart) {
                // Záró idézőjel-e? Utána (szóköz után) vessző, ':', '}' , ']' vagy a vég jön.
                const int j = nextNonSpace(i + 1);
                bool closes = j >= s.size();
                if (!closes) {
                    const QChar n = s.at(j);
                    if (n == QLatin1Char(':') || n == QLatin1Char('}') || n == QLatin1Char(']')) closes = true;
                    else if (n == QLatin1Char(',')) {
                        const int k = nextNonSpace(j + 1);
                        static const QString starts = QStringLiteral("\"{[]}") + QChar(0x201C)
                                                      + QChar(0x201D) + QChar(0x201E);
                        closes = k >= s.size() || starts.contains(s.at(k)) || s.at(k).isDigit();
                    } else if (n == QLatin1Char('"') && s.at(j - 1).isSpace()
                               && (s.indexOf(QLatin1Char('\n'), i) >= 0 && s.indexOf(QLatin1Char('\n'), i) < j)) {
                        closes = true;   // sor végén lezárt sztring, a következő sorban új elem (hiányzó vessző)
                    }
                }
                if (closes) { inStr = false; out += QLatin1Char('"'); }
                else out += smart ? QString(c) : QStringLiteral("\\\"");   // escape-eletlen belső idézőjel
                continue;
            }
            if (c == QLatin1Char('\n')) { out += QStringLiteral("\\n"); continue; }
            if (c == QLatin1Char('\r')) continue;
            if (c == QLatin1Char('\t')) { out += QStringLiteral("\\t"); continue; }
            if (c.unicode() < 0x20) continue;
            out += c;
            continue;
        }
        // Sztringen kívül.
        if (c == QLatin1Char('"') || c == QChar(0x201C) || c == QChar(0x201D) || c == QChar(0x201E)) {
            commaIfNeeded();
            inStr = true;
            smartOpen = c != QLatin1Char('"');
            out += QLatin1Char('"');
        } else if (c == QLatin1Char('{') || c == QLatin1Char('[')) {
            commaIfNeeded();
            stack.append(c);
            out += c;
        } else if (c == QLatin1Char('}') || c == QLatin1Char(']')) {
            dropTrailingComma();
            const QChar want = c == QLatin1Char('}') ? QLatin1Char('{') : QLatin1Char('[');
            // Egy kimaradt zárójel: a belső nyitottat előbb lezárjuk.
            while (!stack.isEmpty() && stack.last() != want) {
                out += stack.last() == QLatin1Char('{') ? QLatin1Char('}') : QLatin1Char(']');
                stack.removeLast();
            }
            if (!stack.isEmpty()) { stack.removeLast(); out += c; }
            if (stack.isEmpty()) break;                              // az objektum vége — a maradék próza
        } else {
            out += c;
        }
    }
    // Csonka vég: nyitott sztring / tömb / objektum lezárása.
    if (inStr) out += QLatin1Char('"');
    {
        // Egy félbemaradt „"kulcs":” pár eldobása.
        int k = out.size() - 1;
        while (k >= 0 && out.at(k).isSpace()) --k;
        if (k >= 0 && out.at(k) == QLatin1Char(':')) {
            const int q = out.lastIndexOf(QLatin1Char('"'), k - 1);
            const int q0 = q > 0 ? out.lastIndexOf(QLatin1Char('"'), q - 1) : -1;
            if (q0 >= 0) out.truncate(q0);
        }
    }
    dropTrailingComma();
    while (!stack.isEmpty()) {
        out += stack.last() == QLatin1Char('{') ? QLatin1Char('}') : QLatin1Char(']');
        stack.removeLast();
    }
    return out.toUtf8();
}

MergeResult parseMergeJson(const QString& raw)
{
    MergeResult r;
    const QByteArray json = extractJsonObject(raw);
    if (json.isEmpty()) {
        r.error = raw.trimmed().isEmpty()
            ? tr("a modell válasza üres")
            : tr("a válaszban nincs JSON-objektum (eleje: „%1”)").arg(raw.simplified().left(80));
        return r;
    }
    QJsonParseError perr{};
    QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        const QJsonParseError first = perr;
        const QByteArray fixed = repairJson(json);
        doc = QJsonDocument::fromJson(fixed, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            r.error = tr("a válasz nem érvényes JSON (%1 a(z) %2. karakternél: „%3”)")
                          .arg(first.errorString()).arg(first.offset)
                          .arg(snippetAround(json, first.offset));
            return r;
        }
    }
    const QJsonObject o = doc.object();
    bool any = false;
    r.execSummary = valueToText(firstOf(o, {"execSummary", "executiveSummary", "exec_summary",
                                            "executive_summary", "summary"}, &any));
    r.decisions = textList(firstOf(o, {"decisions", "decision"}, &any));
    r.openQuestions = textList(firstOf(o, {"openQuestions", "open_questions", "openItems",
                                           "open_items", "open"}, &any));
    r.actionItems = actionList(firstOf(o, {"actionItems", "action_items", "actions", "tasks"}, &any));
    if (!any) {
        r.error = tr("a JSON-ban nincs egyik várt mező sem (execSummary, decisions, openQuestions, actionItems)");
        return r;
    }
    if (r.openQuestions.size() > kMaxOpenQuestions)
        r.openQuestions = r.openQuestions.mid(0, kMaxOpenQuestions);
    r.ok = true;
    return r;
}

SingleResult parseSingle(const QString& raw, const TranscriptPart& part)
{
    SingleResult r;
    // A jegyzet és a JSON határa: a SUMMARY fejléc, különben az első sor eleji '{' (vagy kerítés).
    const QStringList lines = raw.split(QLatin1Char('\n'));
    int cut = -1;            // karakter-pozíció a raw-ban
    int pos = 0;
    for (const QString& l : lines) {
        const QString t = l.trimmed();
        if (headingOf(t) == Sec::Summary || t.startsWith(QLatin1Char('{'))
            || t.startsWith(QLatin1String("```json"))) {
            cut = pos;
            break;
        }
        pos += int(l.size()) + 1;
    }
    if (cut < 0) cut = raw.indexOf(QLatin1Char('{'));
    const QString notesText = cut >= 0 ? raw.left(cut) : raw;
    const QString jsonText = cut >= 0 ? raw.mid(cut) : QString();
    r.notes = parseNotesImpl(notesText, part, /*fallbackWithoutHeadings*/ false);
    r.merge = parseMergeJson(jsonText);
    return r;
}

QStringList speakersOf(const QVector<Utterance>& segments)
{
    QStringList out;
    for (const Utterance& u : segments)
        if (!u.speaker.trimmed().isEmpty() && !out.contains(u.speaker)) out << u.speaker;
    return out;
}

Summary buildSummary(const MergeResult& merge, const QVector<PartNotes>& parts,
                     const QStringList& participants)
{
    Summary s;
    s.execSummary = merge.execSummary;
    s.decisions = merge.decisions;
    s.openQuestions = merge.openQuestions.mid(0, kMaxOpenQuestions);
    s.actionItems = merge.actionItems;
    s.participants = participants;
    s.memo = assembleMemo(parts);
    return s;
}

} // namespace summarypipe
} // namespace tanara

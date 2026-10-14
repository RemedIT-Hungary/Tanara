#include "tanara/library/summary/SummaryFormat.h"
#include "tanara/foundation/text/Text.h"

#include <QRegularExpression>
#include <QStringList>

namespace tanara::library::summaryformat {

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

QString memoGroupKey(const QString& heading)
{
    QString t = heading.trimmed();
    static const QRegularExpression deco(QStringLiteral("^[#*_\\s]+|[#*_:\\s]+$"));
    t.remove(deco);
    if (t.isEmpty()) return QString();
    const QString f = text::foldQuery(t).trimmed();
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

} // namespace tanara::library::summaryformat

#include "tanara/foundation/time/TimeFormat.h"

namespace tanara {
namespace timefmt {

namespace {
QString two(qint64 v) { return QStringLiteral("%1").arg(v, 2, 10, QLatin1Char('0')); }
} // namespace

QString timestamp(qint64 ms)
{
    const qint64 totalSec = ms / 1000;
    return QStringLiteral("%1:%2").arg(two(totalSec / 60), two(totalSec % 60));
}

QString clock(qint64 ms, bool padMinutes)
{
    const qint64 total = qMax<qint64>(0, ms) / 1000;
    const qint64 h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(two(m), two(s));
    return QStringLiteral("%1:%2").arg(m, padMinutes ? 2 : 1, 10, QLatin1Char('0')).arg(two(s));
}

QString clockMs(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2.%3").arg(two(s / 60), two(s % 60)).arg((ms % 1000) / 100);
}

QString hms(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2:%3").arg(s / 3600).arg(two((s / 60) % 60), two(s % 60));
}

QString duration(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("–");
    const qint64 total = (ms + 500) / 1000;
    const qint64 h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(two(m), two(s));
    return QStringLiteral("%1:%2").arg(two(m), two(s));
}

} // namespace timefmt
} // namespace tanara

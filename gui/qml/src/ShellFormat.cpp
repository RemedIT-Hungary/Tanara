#include "ShellFormat.h"

#include "tanara/Localization.h"

#include <QCoreApplication>

namespace tanara_qml::fmt {

QLocale uiLocale()
{
    return tanara::activeUiLanguage() == QLatin1String("en") ? QLocale(QLocale::English)
                                                             : QLocale(QLocale::Hungarian);
}

QString shortDate(const QDateTime& dt)
{
    if (!dt.isValid())
        return {};
    const QLocale loc = uiLocale();
    return loc.toString(dt.date(), loc.language() == QLocale::Hungarian ? QStringLiteral("MMM d.")
                                                                         : QStringLiteral("MMM d"));
}

QString longDate(const QDateTime& dt)
{
    if (!dt.isValid())
        return {};
    const QLocale loc = uiLocale();
    return loc.toString(dt.date(), loc.language() == QLocale::Hungarian
                                       ? QStringLiteral("yyyy. MMM d.")
                                       : QStringLiteral("MMM d, yyyy"));
}

QString shortDuration(qint64 ms)
{
    const qint64 totalMin = ms / 60000;
    if (ms > 0 && totalMin == 0)
        return QCoreApplication::translate("ShellFormat", "<1 p");
    const qint64 h = totalMin / 60, m = totalMin % 60;
    if (h > 0)
        return QCoreApplication::translate("ShellFormat", "%1 ó %2 p").arg(h).arg(m);
    return QCoreApplication::translate("ShellFormat", "%1 p").arg(m);
}

QString clock(qint64 ms, bool padMinutes)
{
    const qint64 total = qMax<qint64>(0, ms) / 1000;
    const qint64 h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m, padMinutes ? 2 : 1, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

QString stepStateKey(tanara::StepState state)
{
    switch (state) {
    case tanara::StepState::Done:    return QStringLiteral("done");
    case tanara::StepState::Running: return QStringLiteral("running");
    case tanara::StepState::Failed:  return QStringLiteral("error");
    case tanara::StepState::None:    break;
    }
    return QStringLiteral("missing");
}

} // namespace tanara_qml::fmt

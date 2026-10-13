#include "ShellFormat.h"
#include "tanara/foundation/time/TimeFormat.h"

#include "tanara/foundation/i18n/Localization.h"

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
    return tanara::timefmt::clock(ms, padMinutes);
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

#include "tanara/watcher/WatcherSettings.h"

#include <QJsonArray>

namespace tanara::watcher {

QJsonObject WatcherSettings::toJson() const
{
    return QJsonObject{
        {QStringLiteral("detectorEnabled"), detectorEnabled},
        {QStringLiteral("detectorIntervalSec"), detectorIntervalSec},
        {QStringLiteral("autostart"), autostart},
        {QStringLiteral("askStopOnCallEnd"), askStopOnCallEnd},
        {QStringLiteral("silenceAskMinutes"), silenceAskMinutes},
        {QStringLiteral("detectorId"), detectorId},
        {QStringLiteral("knownCallApps"), QJsonArray::fromStringList(knownCallApps)},
    };
}

WatcherSettings WatcherSettings::fromJson(const QJsonObject& o)
{
    WatcherSettings s;
    s.detectorEnabled     = o.value(QStringLiteral("detectorEnabled")).toBool(s.detectorEnabled);
    s.detectorIntervalSec = o.value(QStringLiteral("detectorIntervalSec")).toInt(s.detectorIntervalSec);
    s.autostart           = o.value(QStringLiteral("autostart")).toBool(s.autostart);
    s.askStopOnCallEnd    = o.value(QStringLiteral("askStopOnCallEnd")).toBool(s.askStopOnCallEnd);
    s.silenceAskMinutes   = o.value(QStringLiteral("silenceAskMinutes")).toInt(s.silenceAskMinutes);
    s.detectorId          = o.value(QStringLiteral("detectorId")).toString(s.detectorId);
    if (o.contains(QStringLiteral("knownCallApps"))) {
        s.knownCallApps.clear();
        for (const QJsonValue& v : o.value(QStringLiteral("knownCallApps")).toArray())
            s.knownCallApps << v.toString();
    }
    return s;
}

} // namespace tanara::watcher

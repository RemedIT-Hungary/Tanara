#include "tanara/recording/RecordingSettings.h"

namespace tanara::recording {

QJsonObject RecordingSettings::toJson() const
{
    QJsonObject names;
    for (auto it = deviceNames.constBegin(); it != deviceNames.constEnd(); ++it)
        names[it.key()] = it.value();
    return QJsonObject{
        {QStringLiteral("userSpeakerName"), userSpeakerName},
        {QStringLiteral("autoRecordAllDevices"), autoRecordAllDevices},
        {QStringLiteral("audioQuality"), audioQuality},
        {QStringLiteral("mixdownMode"), mixdownMode},
        {QStringLiteral("deviceNames"), names},
    };
}

RecordingSettings RecordingSettings::fromJson(const QJsonObject& o)
{
    RecordingSettings s;
    s.userSpeakerName = o.value(QStringLiteral("userSpeakerName")).toString();
    s.autoRecordAllDevices = o.value(QStringLiteral("autoRecordAllDevices")).toBool(s.autoRecordAllDevices);
    s.audioQuality = o.value(QStringLiteral("audioQuality")).toString(s.audioQuality);
    s.mixdownMode = o.value(QStringLiteral("mixdownMode")).toString(s.mixdownMode);
    // An empty name is not an override (dropped).
    const QJsonObject names = o.value(QStringLiteral("deviceNames")).toObject();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        const QString name = it.value().toString().simplified();
        if (!it.key().isEmpty() && !name.isEmpty())
            s.deviceNames.insert(it.key(), name);
    }
    return s;
}

} // namespace tanara::recording

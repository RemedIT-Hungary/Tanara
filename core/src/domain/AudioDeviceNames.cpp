#include "tanara/domain/AudioDeviceNames.h"

#include <QRegularExpression>

namespace tanara::audionames {

QString stripMonitorPrefix(const QString& deviceName)
{
    static const QRegularExpression prefix(
        QStringLiteral("^(monitor of|monitor:|loopback of|loopback:)\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    QString n = deviceName.trimmed();
    n.remove(prefix);
    return n;
}

bool looksLikeSystemAudio(const QString& deviceName)
{
    const QString n = deviceName.toLower();
    return n.contains(QStringLiteral("monitor")) || n.contains(QStringLiteral("loopback"));
}

bool looksLikeLineIn(const QString& deviceName)
{
    const QString n = deviceName.toLower();
    return n.contains(QStringLiteral("line-in")) || n.contains(QStringLiteral("line in"))
        || n.contains(QStringLiteral("vonalbemenet")) || n.contains(QStringLiteral("aux"));
}

TrackKind classifyCaptureName(const QString& deviceName)
{
    if (looksLikeSystemAudio(deviceName))
        return TrackKind::Loopback;
    // Line-in / AUX: typically nothing is plugged in (it would record silence), so it is not a
    // microphone. It can still be checked by hand in the recorder's "Other" group.
    if (looksLikeLineIn(deviceName))
        return TrackKind::Other;
    return TrackKind::Mic;
}

} // namespace tanara::audionames

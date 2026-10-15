#include "tanara/domain/SpeakerKeys.h"

namespace tanara::speakerkeys {

namespace {
// The legacy Hungarian raw label (data written before the neutral keys).
const QString kLegacyLabel = QStringLiteral("Beszélő");
}

QString rawSpeakerKey(int n)
{
    return kRawSpeakerPrefix + QString::number(n);
}

QString rawSpeakerKeyForId(const QString& providerId)
{
    const QString id = providerId.trimmed();
    if (id.isEmpty()) return rawSpeakerKey(1);
    bool ok = false;
    const int n = id.toInt(&ok);
    return ok ? rawSpeakerKey(n) : kRawSpeakerPrefix + id;
}

bool isRawSpeakerKey(const QString& label)
{
    return label.size() > kRawSpeakerPrefix.size() && label.startsWith(kRawSpeakerPrefix);
}

QString rawSpeakerId(const QString& label)
{
    return isRawSpeakerKey(label) ? label.mid(kRawSpeakerPrefix.size()) : QString();
}

std::optional<int> rawSpeakerIndex(const QString& label)
{
    if (!isRawSpeakerKey(label)) return std::nullopt;
    bool ok = false;
    const int n = rawSpeakerId(label).toInt(&ok);
    return ok ? std::optional<int>(n) : std::nullopt;
}

QString normalizeSpeakerLabel(const QString& label)
{
    if (!label.startsWith(kLegacyLabel)) return label;
    // A raw participant id may carry a side: "<label>@<side>".
    const int at = label.lastIndexOf(QLatin1Char('@'));
    const QString head = at < 0 ? label : label.left(at);
    const QString tail = at < 0 ? QString() : label.mid(at);
    if (head == kLegacyLabel) return rawSpeakerKey(1) + tail;
    if (head.size() < kLegacyLabel.size() + 2 || head.at(kLegacyLabel.size()) != QLatin1Char(' ')) return label;
    const QString id = head.mid(kLegacyLabel.size() + 1);
    bool ok = false;
    const int n = id.toInt(&ok);
    if (!ok || n < 0) return label;   // e.g. a person really named "Beszélő Béla"
    return rawSpeakerKey(n) + tail;
}

QStringList normalizeSpeakerLabels(const QStringList& labels)
{
    QStringList out;
    out.reserve(labels.size());
    for (const QString& l : labels) out << normalizeSpeakerLabel(l);
    return out;
}

QString fixedSpeakerLabel(const QString& label, bool hungarian)
{
    if (!isRawSpeakerKey(label)) return label;
    return (hungarian ? kLegacyLabel : QStringLiteral("Speaker")) + QLatin1Char(' ') + rawSpeakerId(label);
}

QString legacySpeakerLabel(const QString& label)
{
    return fixedSpeakerLabel(label, true);
}

} // namespace tanara::speakerkeys

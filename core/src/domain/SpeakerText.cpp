#include "tanara/domain/SpeakerText.h"

#include "tanara/domain/SpeakerKeys.h"

#include <QCoreApplication>

namespace tanara {

QString speakerLabelText(const QString& label)
{
    const int at = label.lastIndexOf(QLatin1Char('@'));
    const QString head = at < 0 ? label : label.left(at);
    if (!speakerkeys::isRawSpeakerKey(head)) return label;
    const QString text = QCoreApplication::translate("SpeakerText", "Beszélő %1").arg(speakerkeys::rawSpeakerId(head));
    return at < 0 ? text : text + label.mid(at);
}

} // namespace tanara

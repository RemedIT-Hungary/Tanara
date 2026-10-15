#include "tanara/providers/stt/SonioxReference.h"

#include <QStringList>

namespace tanara::providers {

namespace {
const QString kPrefix = QStringLiteral("tanara");
const QString kVersion = QStringLiteral("1");

// A part must not contain the separator; we replace it rather than reject the whole reference.
QString sanitize(QString part)
{
    part.replace(QLatin1Char(':'), QLatin1Char('_'));
    return part;
}
} // namespace

QString buildClientReference(const QString& meetingId, const QString& jobId, const QString& trackId)
{
    if (meetingId.isEmpty() && jobId.isEmpty() && trackId.isEmpty())
        return {};
    const QString ref = QStringList{kPrefix, kVersion, sanitize(meetingId), sanitize(jobId), sanitize(trackId)}
                            .join(QLatin1Char(':'));
    return ref.left(kClientReferenceMaxLength);
}

ClientReference parseClientReference(const QString& reference)
{
    ClientReference out;
    const QStringList parts = reference.split(QLatin1Char(':'));
    if (parts.size() != 5 || parts[0] != kPrefix || parts[1] != kVersion)
        return out;
    out.meetingId = parts[2];
    out.jobId = parts[3];
    out.trackId = parts[4];
    out.valid = true;
    return out;
}

} // namespace tanara::providers

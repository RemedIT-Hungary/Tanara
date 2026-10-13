#include "tanara/domain/Voice.h"

#include <algorithm>

namespace tanara {
namespace voicemodels {

QStringList normalizeIds(const QStringList& ids)
{
    QStringList out;
    for (const QString& id : ids) {
        const QString t = id.trimmed();
        if (!t.isEmpty() && !out.contains(t)) out << t;
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace voicemodels
} // namespace tanara

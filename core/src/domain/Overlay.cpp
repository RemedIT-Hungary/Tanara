#include "tanara/domain/Overlay.h"

namespace tanara {

const OverlayParticipant* SpeakerOverlay::participant(const QString& key) const
{
    for (const OverlayParticipant& p : participants)
        if (p.key == key) return &p;
    return nullptr;
}

OverlayParticipant* SpeakerOverlay::participant(const QString& key)
{
    for (OverlayParticipant& p : participants)
        if (p.key == key) return &p;
    return nullptr;
}

} // namespace tanara

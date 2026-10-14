#pragma once
//
// Transitional COLLECTOR header (cleanup phase 2, P4). The shared value types live in the domain
// module (tanara/domain/*.h); this header only includes them, plus opusBitrateKbps.
// providers::ProviderConfig lives in providers/core/ProviderConfig.h, AppSettings in app/AppSettings.h (P6).
// RULE: core code includes the precise domain headers. gui / cli / tests may still use it as a collector.
#include "tanara/domain/Activity.h"
#include "tanara/domain/Audio.h"
#include "tanara/domain/Job.h"
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Overlay.h"
#include "tanara/domain/Participant.h"
#include "tanara/domain/Summary.h"
#include "tanara/domain/Tag.h"
#include "tanara/domain/Transcript.h"
#include "tanara/domain/Voice.h"
#include "tanara/foundation/Version.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

namespace tanara {

// Recording quality step -> per-track Opus bitrate (kbps). Unknown = 64 ("best").
// The lowest step (24 kbps) is the "STT floor": we do not go below it, so the accuracy of
// transcription working from the per-track .ogg does not degrade.
inline int opusBitrateKbps(const QString& quality) {
    if (quality == QStringLiteral("low"))    return 24;
    if (quality == QStringLiteral("medium")) return 32;
    if (quality == QStringLiteral("high"))   return 48;
    return 64;   // "best" / unknown
}

} // namespace tanara


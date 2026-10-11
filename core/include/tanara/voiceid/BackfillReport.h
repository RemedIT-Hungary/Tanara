#pragma once
//
// A lusta lenyomat-pótlás (AppController::backfillVoiceprints) kimaradt mintái — a Beállítások ›
// Beszélők „részben” állapotához: KINEK a mintái maradtak ki és miért. Sima érték-típus.
//
#include <QString>
#include <QMetaType>

namespace tanara {

struct VoiceprintBackfillSkip {
    enum class Reason {
        MissingMeeting,   // a minta forrás-megbeszélése nincs meg (törölve / másik gépen)
        MissingAudio,     // a megbeszélés megvan, de a minta hangja nem olvasható
        Failed,           // a hang megvan, de a modell nem adott vektort (pl. túl rövid)
    };
    QString person;
    QString model;            // a hiányzó modell id-ja
    QString sampleRef;        // a minta helye ("<folder>/track.ogg#start-end")
    QString sourceMeetingId;  // üres is lehet
    Reason  reason = Reason::MissingAudio;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::VoiceprintBackfillSkip)

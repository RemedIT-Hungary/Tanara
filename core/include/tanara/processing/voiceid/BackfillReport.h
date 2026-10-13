#pragma once
//
// The skipped samples of the lazy voiceprint backfill (AppController::backfillVoiceprints), for the
// Settings > Speakers "partial" state: WHOSE samples were skipped and why. A plain value type.
#include <QString>
#include <QMetaType>

namespace tanara::processing {

struct VoiceprintBackfillSkip {
    enum class Reason {
        MissingMeeting,   // the sample's source meeting is gone (deleted / on another machine)
        MissingAudio,     // the meeting exists, but the sample's audio is not readable
        Failed,           // the audio exists, but the model gave no vector (e.g. too short)
    };
    QString person;
    QString model;            // id of the missing model
    QString sampleRef;        // the sample's location ("<folder>/track.ogg#start-end")
    QString sourceMeetingId;  // may be empty
    Reason  reason = Reason::MissingAudio;
};

} // namespace tanara::processing
Q_DECLARE_METATYPE(tanara::processing::VoiceprintBackfillSkip)

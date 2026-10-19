#pragma once
//
// The "recording" settings section.
#include <QJsonObject>
#include <QMap>
#include <QString>

namespace tanara::recording {

struct RecordingSettings {
    // Fixed name of the own microphone track (e.g. "Adam"). Default: the OS account's display name.
    QString userSpeakerName;
    // true = record every device (silent ones are dropped afterwards).
    bool autoRecordAllDevices = true;
    // Recording audio quality (per-track Opus bitrate, see opusBitrateKbps). The raw per-track .ogg
    // files are also used by the STT, so even the lowest step is "STT-safe" (24 kbps).
    // Values: "best"|"high"|"medium"|"low".
    QString audioQuality{QStringLiteral("best")};
    // Timing of the mixdown after recording stops. "auto" = in the background, immediately;
    // "manual" = only on manual start. The mixdown is needed ONLY for listening (the STT works from
    // the per-track .ogg files), so stop() never waits for it.
    QString mixdownMode{QStringLiteral("auto")};
    // Keep the machine out of idle sleep while recording (platform::IPowerInhibitor; the display
    // may still turn off). Off: the recording service never acquires the inhibitor.
    bool keepAwake = true;
    // User-facing names of audio devices: raw OS device name -> friendly name. Resolution happens in
    // ONE place: tanara::library::devicenames (library/tracks/TrackCatalog.h); the SettingsManager
    // loads it on every load / save.
    QMap<QString, QString> deviceNames;

    QJsonObject toJson() const;
    static RecordingSettings fromJson(const QJsonObject& o);
};

} // namespace tanara::recording

#pragma once
//
// Domain: audio devices, track kinds and the recording quality steps (plain value types).
#include <QMetaType>
#include <QString>

namespace tanara {

enum class TrackKind { Mic, Loopback, Other };
enum class RecordingState { Idle, Recording, Stopping, Encoding };

struct AudioDeviceInfo {
    QString id;
    QString name;
    TrackKind kind = TrackKind::Mic;
    bool isDefault = false;
    int sampleRate = 48000;
    int channels = 1;
};

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

Q_DECLARE_METATYPE(tanara::AudioDeviceInfo)

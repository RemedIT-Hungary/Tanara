#pragma once
//
// Domain: audio devices and track kinds (plain value types).
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

} // namespace tanara

Q_DECLARE_METATYPE(tanara::AudioDeviceInfo)

#pragma once
//
// recording::ISystemAudioSource: where the SYSTEM AUDIO ("what the others say": the outputs' sound)
// comes from on this platform, and which devices play that role. A port; the implementations are in
// core/src/recording/platform/<os>/:
//   Linux:   PulseAudio / PipeWire expose every output's "Monitor of ..." source as a capture device;
//            the role comes from the name (domain/AudioDeviceNames.h).
//   Windows: system audio is NOT a capture device; every playback (output) device becomes a Loopback
//            device ("loopback:<i>:<name>"), and AudioEngine opens it in WASAPI loopback mode.
//   macOS:   a Core Audio process tap in a private aggregate device ("Loopback: System audio"), which
//            miniaudio lists among the capture devices (docs/macos.md section 2).
//   Otherwise: the capture devices' names only.
//
// It only classifies and selects. The enumeration and the capture itself stay in DeviceManager /
// AudioEngine (miniaudio), so this header has no miniaudio dependency.
#include "tanara/domain/Audio.h"

#include <QString>
#include <QVector>

#include <memory>

namespace tanara::recording {

// An output (playback) device as the audio backend enumerated it.
struct PlaybackEndpoint {
    QString name;
    bool    isDefault = false;
    int     nativeChannels = 0;   // 0 = unknown
};

class ISystemAudioSource {
public:
    virtual ~ISystemAudioSource() = default;

    // Stable id of the mechanism: "pulse-monitor", "wasapi-loopback", "coreaudio-tap", "none".
    virtual QString id() const = 0;
    // The role of an enumerated CAPTURE device: Loopback (system audio), Other (line-in / AUX), Mic.
    virtual TrackKind classifyCapture(const QString& deviceName) const = 0;
    // The system-audio devices derived from the PLAYBACK devices (Windows: one Loopback device per
    // output, in enumeration order). Empty where system audio already appears among the captures.
    virtual QVector<AudioDeviceInfo> loopbackDevices(const QVector<PlaybackEndpoint>& playback) const = 0;
    // Whether a device of this kind is opened from the PLAYBACK list in loopback mode (Windows
    // Loopback) instead of as a capture device (microphones, Linux monitors).
    virtual bool opensFromPlayback(TrackKind kind) const = 0;
    // Whether a device of this kind legitimately delivers NO samples while nothing sounds (Windows
    // WASAPI loopback), instead of delivering silence. Such a track is not watched for stalls
    // (tracktiming::kStallWarnMs): "no data" is normal there.
    virtual bool deliversNothingWhenIdle(TrackKind /*kind*/) const { return false; }
};

// This platform's implementation (core/src/recording/platform/<os>/).
std::unique_ptr<ISystemAudioSource> createSystemAudioSource();
// The process-wide instance of it (created on first use; on macOS that creates the tap device, so call
// it before enumerating the devices).
const ISystemAudioSource& systemAudioSource();

} // namespace tanara::recording

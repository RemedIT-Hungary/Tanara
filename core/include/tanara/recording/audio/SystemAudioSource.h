#pragma once
//
// recording::ISystemAudioSource: where the SYSTEM AUDIO ("what the others say": the outputs' sound)
// comes from on this platform, and which devices play that role. A port; the implementations are in
// core/src/recording/platform/<os>/:
//   Linux:   PulseAudio / PipeWire expose every output's "Monitor of ..." source as a capture device;
//            the role comes from the name (domain/AudioDeviceNames.h).
//   Windows: system audio is NOT a capture device; every playback (output) device becomes a Loopback
//            device ("loopback:<i>:<name>"), and AudioEngine opens it in WASAPI loopback mode.
//   Otherwise (macOS until docs/macos.md is implemented): the capture devices' names only.
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

    // Stable id of the mechanism: "pulse-monitor", "wasapi-loopback", "none".
    virtual QString id() const = 0;
    // The role of an enumerated CAPTURE device: Loopback (system audio), Other (line-in / AUX), Mic.
    virtual TrackKind classifyCapture(const QString& deviceName) const = 0;
    // The system-audio devices derived from the PLAYBACK devices (Windows: one Loopback device per
    // output, in enumeration order). Empty where system audio already appears among the captures.
    virtual QVector<AudioDeviceInfo> loopbackDevices(const QVector<PlaybackEndpoint>& playback) const = 0;
    // Whether a device of this kind is opened from the PLAYBACK list in loopback mode (Windows
    // Loopback) instead of as a capture device (microphones, Linux monitors).
    virtual bool opensFromPlayback(TrackKind kind) const = 0;
};

// This platform's implementation (core/src/recording/platform/<os>/).
std::unique_ptr<ISystemAudioSource> createSystemAudioSource();
// The process-wide instance of it (stateless; created on first use).
const ISystemAudioSource& systemAudioSource();

} // namespace tanara::recording

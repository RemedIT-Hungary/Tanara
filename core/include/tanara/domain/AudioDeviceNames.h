#pragma once
//
// Domain: the naming rules of audio devices, in ONE place (pure string functions, no I/O).
//
// PulseAudio / PipeWire expose every output's sound as a capture source named "Monitor of <output>"
// (some layers: "Monitor: ...", "Loopback of ...", "Loopback: ..."). The recorder's system-audio
// adapter (recording::ISystemAudioSource) classifies live devices with these rules, the playback
// routing maps a monitor back to its output, and the track catalog shortens stored device names.
// The rules are platform-neutral on purpose: a meeting recorded on Linux keeps its device names,
// and they must read the same wherever the archive is opened.
#include "tanara/domain/Audio.h"

#include <QString>

namespace tanara::audionames {

// "Monitor of X" / "Monitor: X" / "Loopback of X" / "Loopback: X" -> "X"; other names unchanged.
// The result is trimmed.
QString stripMonitorPrefix(const QString& deviceName);

// Whether a CAPTURE device's name marks it as an output's monitor / loopback (system audio).
bool looksLikeSystemAudio(const QString& deviceName);

// Whether a capture device's name marks it as a line-in / AUX input (a real capture, but not a
// microphone: usually nothing is plugged in).
bool looksLikeLineIn(const QString& deviceName);

// The name-based role of a capture device: Loopback (system audio), Other (line-in / AUX) or Mic.
TrackKind classifyCaptureName(const QString& deviceName);

} // namespace tanara::audionames

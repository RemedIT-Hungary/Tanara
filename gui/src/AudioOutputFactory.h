#pragma once
// Tanara: a QAudioOutput that FOLLOWS the system default output.
//
// A plain QAudioOutput binds to the default device at creation: after switching
// headphones/speakers, playback sticks to the old device. Connected to the
// QMediaDevices::audioOutputsChanged signal, setDevice() redirects even a live stream. The
// mechanism is cross-platform (Linux: PipeWire/Pulse event, Windows: WASAPI device notification).
#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QObject>

namespace tanara_gui {

// A QAudioOutput on the system default output, following default changes. The parent owns it.
inline QAudioOutput* makeFollowDefaultAudioOutput(QObject* parent)
{
    auto* out = new QAudioOutput(parent);
    auto* devices = new QMediaDevices(out);
    QObject::connect(devices, &QMediaDevices::audioOutputsChanged, out, [out]() {
        const QAudioDevice def = QMediaDevices::defaultAudioOutput();
        // Guard: the signal also fires on hotplug, not only on a default change; a needless
        // setDevice must not stutter running playback.
        if (!def.isNull() && out->device() != def)
            out->setDevice(def);
    });
    return out;
}

} // namespace tanara_gui

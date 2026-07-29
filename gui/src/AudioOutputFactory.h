#pragma once
//
// Tanara — QAudioOutput, ami KÖVETI a rendszer default kimenetét.
//
// A sima QAudioOutput a létrehozáskori default eszközre rögzül: fülhallgató/hangszóró
// váltásnál a lejátszás a régi eszközön ragad. A QMediaDevices::audioOutputsChanged
// signalra kötve a setDevice() élő stream közben is átirányít — a mechanizmus
// cross-platform (Linux: PipeWire/Pulse esemény, Windows: WASAPI device-notification).
//
#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QObject>

namespace tanara_gui {

// QAudioOutput a rendszer default kimenetére, default-követéssel. A parent birtokolja.
inline QAudioOutput* makeFollowDefaultAudioOutput(QObject* parent)
{
    auto* out = new QAudioOutput(parent);
    auto* devices = new QMediaDevices(out);
    QObject::connect(devices, &QMediaDevices::audioOutputsChanged, out, [out]() {
        const QAudioDevice def = QMediaDevices::defaultAudioOutput();
        // Guard: a signal hotplugnál is jön, nemcsak default-váltásnál — felesleges
        // setDevice ne okozzon csuklást a futó lejátszásban.
        if (!def.isNull() && out->device() != def)
            out->setDevice(def);
    });
    return out;
}

} // namespace tanara_gui

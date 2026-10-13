#pragma once
//
// Meeting-detector descriptor (headless). It follows the ProviderDescriptor pattern, but the detector
// has no user-editable config schema, only an id + platform metadata the watcher/UI can choose from.
// Does NOT link Qt Widgets.
#include <QString>

namespace tanara {

struct DetectorDescriptor {
    QString id;               // stable id: "linux-capture", "windows-wasapi", "macos-coreaudio"
    QString displayName;      // human-readable name
    QString platform;         // "linux" | "windows" | "macos"
    bool    derivesAppName = false;   // whether it can extract the call app/window name (not just a bool signal)
};

} // namespace tanara

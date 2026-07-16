#pragma once
//
// Meeting-detektor leíró (headless). A ProviderDescriptor mintáját követi, de a
// detektornak nincs felhasználó-szerkeszthető konfig-sémája — csak azonosító +
// platform-metaadat, amiből a figyelő/UI választhat. NEM linkel Qt Widgetset.
//
#include <QString>

namespace tanara {

struct DetectorDescriptor {
    QString id;               // stabil azonosító: "linux-capture", "windows-wasapi", "macos-coreaudio"
    QString displayName;      // ember-olvasható név
    QString platform;         // "linux" | "windows" | "macos"
    bool    derivesAppName = false;   // ki tudja-e nyerni a hívás-app/ablak nevét (nem csak bool jelet)
};

} // namespace tanara

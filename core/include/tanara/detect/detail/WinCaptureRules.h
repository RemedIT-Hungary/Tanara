#pragma once
//
// WinCaptureRules — a Windows-os meeting-detektor (WindowsCaptureDetector) TISZTA szabályai:
// processz-képnév normalizálása, ön-kizárás, hívás-app illesztés (app-id + szép név), a
// mikrofon-hozzájárulás registry-kulcsnevének értelmezése és a FILETIME „épp használja"
// logika. Nincs COM, nincs registry-hívás, nincs Windows-fejléc → minden platformon fordul
// és unit-tesztelhető (a COM/registry rész a WindowsCaptureDetector.cpp-ben, Q_OS_WIN alatt).
//
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace tanara::detail::win {

// Teljes út vagy fájlnév → kisbetűs alapnév ".exe" nélkül. Mindkét elválasztót kezeli
// ('\\' és '/'), pl. "C:\\Program Files\\Zoom\\bin\\Zoom.exe" → "zoom".
QString imageBaseName(const QString& pathOrName);

// Ön-kizárás: a saját binárisunk és testvérei (selfBinary = "tanara" → "tanara",
// "tanara-cli", "tanara-watcher"). Csak pontos egyezés vagy "<self>-" előtag számít, így
// pl. egy "tanarak" nevű idegen app NEM esik ki.
bool isSelfImage(const QString& baseLower, const QString& selfBinary);

struct CallAppMatch {
    bool    matched = false;
    QString appId;     // normalizált kulcs (a Linux-detektorral azonos, ahol van: "zoom", "teams"…)
    QString appName;   // ember-olvasható név a meeting-címhez ("Zoom", "Microsoft Teams"…)
};

// Egy (kisbetűs, .exe nélküli) képnév hívás-app-e a knownApps lista szerint. Egy lista-elem
// illeszkedik, ha a képnév tartalmazza, VAGY a Windows-alias tábla szerint az elemhez
// tartozik (pl. "teams" → "ms-teams"/"msteams", "webex" → "ciscocollabhost"/"webexmta"/
// "atmgr", "meet" → a böngészők: chrome/msedge/firefox/brave/opera/vivaldi — a Google Meet
// böngészőben fut, ahogy Linuxon is a böngésző WebRTC-streamje jelez).
CallAppMatch matchCallApp(const QString& baseLower, const QStringList& knownApps);

// A ConsentStore\microphone alatti kulcsnév → egy illeszthető (kisbetűs) név.
//  - csomagolt app (family name): "MSTeams_8wekyb3d8bbwe" → "msteams",
//    "5319275A.WhatsAppDesktop_cv1g1gvanyjgm" → "5319275a.whatsappdesktop"
//  - NonPackaged: "C:#Program Files#Zoom#bin#Zoom.exe" → "zoom" (nonPackaged = true)
QString consentKeyToName(const QString& keyName, bool nonPackaged);

// LastUsedTimeStart / LastUsedTimeStop (FILETIME, 100 ns) → épp használja-e a mikrofont:
// van kezdés, és nincs vége (Stop == 0) vagy a kezdés későbbi a legutóbbi végnél.
bool consentInUse(quint64 lastUsedStart, quint64 lastUsedStop);

} // namespace tanara::detail::win

#pragma once
//
// WinCaptureRules: the PURE rules of the Windows meeting detector (WindowsCaptureDetector):
// normalizing the process image name, self-exclusion, call-app matching (app id + pretty name),
// interpreting the registry key name of the microphone consent, and the FILETIME "currently using"
// logic. No COM, no registry call, no Windows header, so it compiles on every platform and is
// unit-testable (the COM/registry part is in WindowsCaptureDetector.cpp, under Q_OS_WIN).
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace tanara::detail::win {

// Full path or file name -> lowercase base name without ".exe". Handles both separators
// ('\\' and '/'), e.g. "C:\\Program Files\\Zoom\\bin\\Zoom.exe" -> "zoom".
QString imageBaseName(const QString& pathOrName);

// Self-exclusion: our own binary and its siblings (selfBinary = "tanara" -> "tanara",
// "tanara-cli", "tanara-watcher"). Only an exact match or a "<self>-" prefix counts, so e.g. a
// foreign app named "tanarak" is NOT excluded.
bool isSelfImage(const QString& baseLower, const QString& selfBinary);

struct CallAppMatch {
    bool    matched = false;
    QString appId;     // normalized key (the same as the Linux detector's where there is one: "zoom", "teams"...)
    QString appName;   // human-readable name for the meeting title ("Zoom", "Microsoft Teams"...)
};

// Whether a (lowercase, no .exe) image name is a call app per the knownApps list. A list item
// matches if the image name contains it, OR per the Windows alias table it belongs to the item
// (e.g. "teams" -> "ms-teams"/"msteams", "webex" -> "ciscocollabhost"/"webexmta"/"atmgr", "meet" -> the
// browsers: chrome/msedge/firefox/brave/opera/vivaldi; Google Meet runs in a browser, as on Linux
// the browser's WebRTC stream signals it).
CallAppMatch matchCallApp(const QString& baseLower, const QStringList& knownApps);

// The key name under ConsentStore\microphone -> a matchable (lowercase) name.
//  - packaged app (family name): "MSTeams_8wekyb3d8bbwe" -> "msteams",
//    "5319275A.WhatsAppDesktop_cv1g1gvanyjgm" -> "5319275a.whatsappdesktop"
//  - NonPackaged: "C:#Program Files#Zoom#bin#Zoom.exe" -> "zoom" (nonPackaged = true)
QString consentKeyToName(const QString& keyName, bool nonPackaged);

// LastUsedTimeStart / LastUsedTimeStop (FILETIME, 100 ns) -> whether the microphone is in use now:
// there is a start, and no end (Stop == 0) or the start is later than the last end.
bool consentInUse(quint64 lastUsedStart, quint64 lastUsedStop);

} // namespace tanara::detail::win

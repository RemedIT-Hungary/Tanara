; Tanara: Windows installer (Inno Setup 6).
;
; Per-user install, no admin rights needed:
;   {localappdata}\Programs\Tanara  (e.g. C:\Users\<name>\AppData\Local\Programs\Tanara)
;
; Build (see packaging/windows/README.md):
;   iscc /DDistDir=D:\Maszek\Tanara\dist /DBuildDir=D:\Maszek\Tanara\build packaging\windows\tanara.iss
;
;   DistDir  - the self-contained folder assembled with windeployqt (tanara.exe, tanara-cli.exe,
;              tanara-watcher.exe, Qt + QML plugins, MinGW runtime, onnxruntime.dll,
;              ffmpeg.exe + ffprobe.exe (LGPL-only build), models\campplus_sv_zh_en_16k.onnx,
;              OLVASSEL.md, LICENSE, THIRD_PARTY_NOTICES.md, licenses\; see README.md).
;   BuildDir - the CMake build folder: tanara-version.iss comes from there (the version from
;              project(VERSION) in CMakeLists.txt). /DAppVersion=x.y.z overrides it.
;
; The uninstaller does NOT delete user data (%USERPROFILE%\.tanara, %USERPROFILE%\Tanara);
; the recordings and transcripts belong to the user.

#ifndef DistDir
  #error "Add meg a DistDir-t: iscc /DDistDir=<dist mappa> ..."
#endif

#ifndef AppVersion
  #ifndef BuildDir
    #error "Add meg a BuildDir-t (a tanara-version.iss miatt) vagy az AppVersion-t."
  #endif
  #include AddBackslash(BuildDir) + "tanara-version.iss"
#endif

#ifndef OutputDir
  #define OutputDir AddBackslash(SourcePath) + "Output"
#endif

#define AppName "Tanara"
#define AppPublisher "RemedIT Hungary Kft."
#define AppExe "tanara.exe"
#define WatcherExe "tanara-watcher.exe"

[Setup]
; The AppId identifies the installation on update / uninstall; DO NOT change it.
AppId={{9F52DCF5-068F-45BA-9596-DDA2224D616E}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Tanara-{#AppVersion}-win64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; The Tanara (MIT) license is on the first page; third-party licenses are in {app}\licenses
; and in THIRD_PARTY_NOTICES.md (copied from DistDir).
LicenseFile={#DistDir}\LICENSE
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} {#AppVersion}
; Closes the running tanara.exe / tanara-watcher.exe before an update and before uninstall.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "hu"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
hu.WatcherAutostart=A tálca-figyelő (Tanara Watcher) induljon el bejelentkezéskor
en.WatcherAutostart=Start the tray watcher (Tanara Watcher) at sign-in
hu.WatcherName=Tanara tálca-figyelő
en.WatcherName=Tanara tray watcher
hu.ReadmeName=Tanara — olvass el
en.ReadmeName=Tanara — read me
hu.NoticesName=Harmadik féltől származó komponensek (licencek)
en.NoticesName=Third-party notices (licenses)

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "watcherautostart"; Description: "{cm:WatcherAutostart}"; Flags: unchecked

[Files]
; The whole folder assembled with windeployqt (with subfolders: platforms, qml, models, licenses ...).
; It includes LICENSE, THIRD_PARTY_NOTICES.md and licenses\; do not build a package without them.
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:WatcherName}"; Filename: "{app}\{#WatcherExe}"
Name: "{group}\{cm:ReadmeName}"; Filename: "{app}\OLVASSEL.md"
Name: "{group}\{cm:NoticesName}"; Filename: "{app}\THIRD_PARTY_NOTICES.md"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon
; Start at login: a shortcut in the user's Startup folder (the app's own autostart switch
; does not handle an entry on Windows yet).
Name: "{userstartup}\{cm:WatcherName}"; Filename: "{app}\{#WatcherExe}"; Tasks: watcherautostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

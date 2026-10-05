; Tanara — Windows-telepítő (Inno Setup 6).
;
; Felhasználói (per-user) telepítés, rendszergazdai jog nélkül:
;   {localappdata}\Programs\Tanara  (pl. C:\Users\<név>\AppData\Local\Programs\Tanara)
;
; Fordítás (lásd packaging/windows/README.md):
;   iscc /DDistDir=D:\Maszek\Tanara\dist /DBuildDir=D:\Maszek\Tanara\build packaging\windows\tanara.iss
;
;   DistDir  — a windeployqt-vel összerakott, önálló mappa (tanara.exe, tanara-cli.exe,
;              tanara-watcher.exe, Qt + QML pluginek, MinGW runtime, onnxruntime.dll,
;              ffmpeg.exe, models\campplus_sv_zh_en_16k.onnx, OLVASSEL.md).
;   BuildDir — a CMake build-mappa: innen jön a tanara-version.iss (a verzió a
;              CMakeLists.txt project(VERSION)-jéből). /DAppVersion=x.y.z felülírja.
;
; A felhasználói adatokat (%USERPROFILE%\.tanara, %USERPROFILE%\Tanara) az eltávolító
; NEM törli — a felvételek és az átiratok a felhasználóé.

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
; Az AppId azonosítja a telepítést frissítéskor / eltávolításkor — NE változtasd meg.
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
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} {#AppVersion}
; A futó tanara.exe / tanara-watcher.exe bezárása frissítés és eltávolítás előtt.
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

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "watcherautostart"; Description: "{cm:WatcherAutostart}"; Flags: unchecked

[Files]
; A teljes, windeployqt-vel összerakott mappa (almappákkal: platforms, qml, models …).
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:WatcherName}"; Filename: "{app}\{#WatcherExe}"
Name: "{group}\{cm:ReadmeName}"; Filename: "{app}\OLVASSEL.md"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon
; Indítás bejelentkezéskor: parancsikon a felhasználó Startup mappájában (az app saját
; autostart-kapcsolója Windowson még nem kezel bejegyzést).
Name: "{userstartup}\{cm:WatcherName}"; Filename: "{app}\{#WatcherExe}"; Tasks: watcherautostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

# Windows packaging

This folder holds the files for the Windows tester package (zip) and the installer.

| File | Purpose |
|---|---|
| `OLVASSEL.md` | Hungarian guide for testers. It goes into the package root. |
| `tanara.iss` | Inno Setup 6 script. Per-user install, no admin rights. |
| `tanara-version.iss.in` | CMake writes `build/tanara-version.iss` from this file on Windows. |

## Prerequisites

- Qt 6.11 (mingw_64), the Qt-bundled MinGW 13.1, Ninja, and CMake. See the main
  `README.md`, section "Build on Windows (MinGW)".
- ONNX Runtime win-x64 1.20.1, unpacked (for example `C:\tools\onnxruntime-win-x64-1.20.1`).
- A static `ffmpeg.exe` (for example the gyan.dev "essentials" build in `C:\ffmpeg\bin`).
- The speaker model `campplus_sv_zh_en_16k.onnx` (see the main `README.md`).
- Inno Setup 6 for the installer: `winget install JRSoftware.InnoSetup`.

## Steps

```powershell
$qt  = 'C:\Qt\6.11.1\mingw_64'
$ort = 'C:\tools\onnxruntime-win-x64-1.20.1'
$env:PATH = "$qt\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;$env:PATH"

# 1) Build and test
cmake -S . -B build -G Ninja "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release `
      -DTANARA_BUILD_VOICEID=ON "-DONNXRUNTIME_ROOT_DIR=$ort"
cmake --build build -j 10
$env:QT_QPA_PLATFORM = 'offscreen'; $env:TANARA_HOME = "$env:TEMP\tanara-ctest"
ctest --test-dir build --output-on-failure
Remove-Item Env:QT_QPA_PLATFORM, Env:TANARA_HOME

# 2) Stand-alone folder
New-Item -ItemType Directory dist, dist\models -Force | Out-Null
Copy-Item build\gui\tanara.exe, build\cli\tanara-cli.exe, build\watcher\tanara-watcher.exe dist
& "$qt\bin\windeployqt.exe" --release --compiler-runtime --qmldir gui\qml dist\tanara.exe dist\tanara-cli.exe dist\tanara-watcher.exe
Copy-Item "$ort\lib\onnxruntime.dll" dist        # never rely on System32\onnxruntime.dll (old version)
Copy-Item C:\ffmpeg\bin\ffmpeg.exe dist          # the app finds it next to tanara.exe
Copy-Item "$env:USERPROFILE\.tanara\models\campplus_sv_zh_en_16k.onnx" dist\models
Copy-Item packaging\windows\OLVASSEL.md dist

# 3a) Tester zip
Compress-Archive dist\* Tanara-<version>-win64.zip

# 3b) Installer
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" /DDistDir=$PWD\dist /DBuildDir=$PWD\build `
  /DOutputDir=$PWD packaging\windows\tanara.iss
```

The installer goes to `{localappdata}\Programs\Tanara`. It adds Start-menu shortcuts
for the app, the tray watcher, and `OLVASSEL.md`. It has an optional task that
starts the watcher at sign-in. The uninstaller does not remove user data
(`%USERPROFILE%\.tanara`, `%USERPROFILE%\Tanara`).

## Check the package

Run these on a clean `PATH` (no Qt, MinGW, or ffmpeg), with an empty data folder:

```powershell
$env:PATH = 'C:\Windows\System32;C:\Windows'
$env:TANARA_HOME = "$env:TEMP\tanara-pkgtest"
dist\tanara-cli.exe --version
dist\tanara-cli.exe devices
dist\tanara.exe --qml-shot shot.png --qml-page Main --size 1280x820
```

# Windows packaging

This folder holds the files for the Windows tester package (zip) and the installer.

| File | Purpose |
|---|---|
| `OLVASSEL.md` | Hungarian guide for testers. It goes into the package root. |
| `tanara.iss` | Inno Setup 6 script. Per-user install, no admin rights. |
| `tanara-version.iss.in` | CMake writes `build/tanara-version.iss` from this file on Windows. |

License texts for everything in the package are in `packaging/licenses/`; the component list
is in `THIRD_PARTY_NOTICES.md`. Both go into the package (see step 2).

## Prerequisites

- Qt 6.11 (mingw_64), the Qt-bundled MinGW 13.1, Ninja, and CMake. See the main
  `README.md`, section "Build on Windows (MinGW)".
- ONNX Runtime win-x64 1.20.1, unpacked (for example `C:\tools\onnxruntime-win-x64-1.20.1`).
- The pinned **LGPL-only FFmpeg build** (see "FFmpeg build" below), unpacked to `C:\ffmpeg-lgpl`.
  Do NOT use a GPL build (gyan.dev, or BtbN `gpl`): the package would then have to be
  distributed under GPL terms for FFmpeg.
- The Microsoft Visual C++ runtime DLLs from an official redistributable source (see step 2).
- The speaker model `campplus_sv_zh_en_16k.onnx` (see the main `README.md`).
- Inno Setup 6 for the installer: `winget install JRSoftware.InnoSetup`.

## FFmpeg build

Tanara runs `ffmpeg.exe` and `ffprobe.exe` as separate programs (`QProcess`); it does not link
FFmpeg. The Windows packages ship this exact build:

| Item | Value |
|---|---|
| Source of the build | BtbN/FFmpeg-Builds, release `autobuild-2026-09-30-13-08` (a month-end build; BtbN keeps those for two years) |
| File | `ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0.zip` (win64, LGPL, **static**) |
| URL | https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-30-13-08/ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0.zip |
| SHA256 | `6b264b9e6019103f601d98c292bd332fd87acf1c5e941ddff4fb71760fe63432` (also in the release's `checksums.sha256`) |
| FFmpeg version | n9.0.2 + 17 commits on `release/9.0`, commit `2a571b606854520cf89804d8030c8b328e621689` (2026-09-29) |
| License of the build | LGPL-3.0-or-later. BtbN configures it with `--enable-version3` and no `--enable-gpl` / `--enable-nonfree`; its `LICENSE.txt` is `COPYING.LGPLv3`. |
| Source | FFmpeg: https://github.com/FFmpeg/FFmpeg/tree/2a571b606854520cf89804d8030c8b328e621689 (also https://git.ffmpeg.org/ffmpeg.git). Build recipe and pinned library commits: https://github.com/BtbN/FFmpeg-Builds (state of 2026-09-29, commit `6c9aec5fc9a72ec3abedd1fa84db141fa18cf52b`, `scripts.d/`). |

What the LGPL variant contains and lacks (read from the BtbN scripts, `variants/defaults-lgpl.sh`
and `scripts.d/*`, at the commit above; the zip itself was not downloaded or run for this note):

- Included and used by Tanara: **libopus** (`-c:a libopus`, recordings and tracks) and
  **libmp3lame** (`-c:a libmp3lame`, mixdown MP3). Both are enabled unconditionally in the
  BtbN scripts. The `loudnorm`, `acompressor`, `alimiter` and `amix` filters are plain
  LGPL FFmpeg filters. The decoders for wav, mp3, ogg (vorbis, opus), flac and aac/m4a are
  FFmpeg's native decoders. The build contains further LGPL-compatible libraries (see `scripts.d/` in BtbN's repository).
- Excluded in the `lgpl` variant: libx264, libx265, rubberband, and the other GPL-only libraries.
  nonfree libraries (fdk-aac) are only in the `nonfree` variant. Tanara needs none of these.
- Static build: `ffmpeg.exe` and `ffprobe.exe` are self-contained (no extra DLLs). A static build also avoids
  any name clash with the `avcodec-61.dll` family that Qt Multimedia ships. The shared variant
  (`...-win64-lgpl-shared-9.0.zip`) is smaller in total, but needs about ten DLLs next to the exes. The
  copy step below is simpler with the static build; a shared build would also make relinking easier
  (swap the DLLs). To switch, copy `bin\*.dll` too and update this note and `THIRD_PARTY_NOTICES.md`.

To verify an unpacked build: `ffmpeg.exe -version` must show `--enable-version3` and must NOT contain
`--enable-gpl` or `--enable-nonfree`; `ffmpeg.exe -encoders | findstr "libopus libmp3lame"` must list both.

Download and unpack (PowerShell):

```powershell
$u = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-30-13-08/ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0.zip'
Invoke-WebRequest $u -OutFile ffmpeg-lgpl.zip
if ((Get-FileHash ffmpeg-lgpl.zip -Algorithm SHA256).Hash -ne '6b264b9e6019103f601d98c292bd332fd87acf1c5e941ddff4fb71760fe63432') { throw 'FFmpeg checksum mismatch' }
Expand-Archive ffmpeg-lgpl.zip C:\ffmpeg-lgpl -Force      # -> C:\ffmpeg-lgpl\ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0\{bin,doc,presets,LICENSE.txt}
```

## Steps

```powershell
$qt  = 'C:\Qt\6.11.1\mingw_64'
$ort = 'C:\tools\onnxruntime-win-x64-1.20.1'
$ff  = 'C:\ffmpeg-lgpl\ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0'
$crt = 'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\<version>\x64\Microsoft.VC143.CRT'   # adjust
$env:PATH = "$qt\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;$env:PATH"

# 1) Build and test
cmake -S . -B build -G Ninja "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release `
      -DTANARA_BUILD_VOICEID=ON "-DONNXRUNTIME_ROOT_DIR=$ort"
cmake --build build -j 10
$env:QT_QPA_PLATFORM = 'offscreen'; $env:TANARA_HOME = "$env:TEMP\tanara-ctest"
ctest --test-dir build --output-on-failure
Remove-Item Env:QT_QPA_PLATFORM, Env:TANARA_HOME

# 2) Stand-alone folder
New-Item -ItemType Directory dist, dist\models, dist\licenses -Force | Out-Null
Copy-Item build\gui\tanara.exe, build\cli\tanara-cli.exe, build\watcher\tanara-watcher.exe dist
& "$qt\bin\windeployqt.exe" --release --compiler-runtime --qmldir gui\qml `
  --exclude-plugins qsqlibase,qsqlpsql,qsqlmimer,qsqloci,qsqlodbc `
  --dir dist dist\tanara.exe dist\tanara-cli.exe dist\tanara-watcher.exe
Copy-Item "$ort\lib\onnxruntime.dll" dist        # never rely on System32\onnxruntime.dll (old version)
# onnxruntime.dll is built with MSVC: ship the VC++ runtime app-locally (a clean PC may not have it).
# Take the DLLs from the official Visual C++ redistributable (VS / Build Tools "Redist" folder, or the
# files extracted from Microsoft's vc_redist.x64.exe), NOT from C:\Windows\System32.
Copy-Item "$crt\msvcp140.dll", "$crt\vcruntime140.dll", "$crt\vcruntime140_1.dll" dist
# the app runs both ffmpeg and ffprobe (QProcess); Windows finds them next to the exe.
# LGPL static build: the two exes are all that is needed (no DLLs).
Copy-Item "$ff\bin\ffmpeg.exe", "$ff\bin\ffprobe.exe" dist
# windeployqt deploys only qwindows; --qml-shot (diagnostics, offscreen) needs qoffscreen
Copy-Item "$qt\plugins\platforms\qoffscreen.dll" dist\platforms
Copy-Item "$env:USERPROFILE\.tanara\models\campplus_sv_zh_en_16k.onnx" dist\models
Copy-Item packaging\windows\OLVASSEL.md dist

# License material (required: LGPL/Apache/MIT/OFL/BSD terms need the texts and notices to travel with the binaries)
Copy-Item LICENSE, THIRD_PARTY_NOTICES.md dist
Copy-Item packaging\licenses\* dist\licenses
Copy-Item "$ff\LICENSE.txt"           dist\licenses\FFmpeg-LICENSE.txt
Copy-Item "$ort\LICENSE"              dist\licenses\onnxruntime-LICENSE.txt
Copy-Item "$ort\ThirdPartyNotices.txt" dist\licenses\onnxruntime-ThirdPartyNotices.txt
```

Then check `ffmpeg.exe -version` (see above) and that `dist` contains `LICENSE`,
`THIRD_PARTY_NOTICES.md` and `licenses\` before you build the zip or the installer.

```powershell
# 3a) Tester zip (flat: the files, including LICENSE, THIRD_PARTY_NOTICES.md and licenses\, are at the root of the zip)
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory("$PWD\dist", "$PWD\Tanara-<version>-win64.zip", 'Optimal', $false)

# 3b) Installer
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" /DDistDir=$PWD\dist /DBuildDir=$PWD\build `
  /DOutputDir=$PWD packaging\windows\tanara.iss
```

The installer goes to `{localappdata}\Programs\Tanara`. It shows the Tanara license (`LICENSE`) on
the first wizard page and copies the whole `dist` folder, including the license files, into the
install folder. It adds Start-menu shortcuts for the app, the tray watcher, and `OLVASSEL.md`. It has an
optional task that starts the watcher at sign-in. The uninstaller does not remove user data
(`%USERPROFILE%\.tanara`, `%USERPROFILE%\Tanara`).

## Check the package

Run these on a clean `PATH` (no Qt, MinGW, or ffmpeg), with an empty data folder:

```powershell
$env:PATH = 'C:\Windows\System32;C:\Windows'
$env:TANARA_HOME = "$env:TEMP\tanara-pkgtest"
dist\tanara-cli.exe --version
dist\tanara-cli.exe devices
dist\tanara-cli.exe import some.wav --title Test      # runs the bundled ffmpeg + ffprobe
dist\tanara.exe --qml-shot shot.png --qml-page Main --size 1280x820
```

To check that no DLL outside `dist` and `System32` is needed, list the imports of
every binary with `C:\Qt\Tools\mingw1310_64\bin\objdump.exe -p <file> | findstr "DLL Name"`.

Also check that a recording ends up as Opus and that a mixdown MP3 is produced (these use
`libopus` and `libmp3lame` of the LGPL FFmpeg build).

## Shipped files and where their licenses are

See `THIRD_PARTY_NOTICES.md`, section "Shipped only in Windows packages". Summary of what
`windeployqt` and the steps above put into the package: the Qt DLLs and plugins (LGPL-3.0), Qt's
own FFmpeg DLLs `avcodec-*`, `avformat-*`, `avutil-*`, `swresample-*`, `swscale-*` (LGPL), the
MinGW runtime `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`, `D3Dcompiler_47.dll`,
`opengl32sw.dll`, `onnxruntime.dll`, the VC++ runtime DLLs, `ffmpeg.exe`, `ffprobe.exe`, and the CAM++ model.
Check the actual `dist` contents against that list after a Qt upgrade and update the notices if a file is added.

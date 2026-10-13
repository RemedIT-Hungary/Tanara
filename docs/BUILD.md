# Building Tanara

Tanara is a C++20 / Qt 6 (Quick) project built with CMake and Ninja. This page covers a
developer build on Fedora and on Windows (MinGW), the CMake options, how to run the tests,
and how to run the CI steps on your own machine. For the Windows release package (zip and
installer) see [`packaging/windows/README.md`](../packaging/windows/README.md).

## Requirements

- **C++20** compiler, **CMake >= 3.21**, **Ninja**
- **Qt 6** (Core, Network, Sql, Widgets, Multimedia, MultimediaWidgets, Qml, Quick,
  QuickControls2, Svg, LinguistTools, Test; DBus on Linux). The Windows packages use
  Qt 6.11.1 under the LGPL-3.0.
- **ONNX Runtime** (dev package) for the speaker-embedding model. Optional: see
  `TANARA_BUILD_VOICEID`.
- **KISS FFT** (float build), used by the bundled kaldi-native-fbank. It is vendored under
  `third_party/kissfft` on every platform (vendored since P0).
- **FFmpeg** CLI (`ffmpeg` and `ffprobe`, with libopus and libmp3lame). Tanara runs it as an
  external program; it does not link FFmpeg. Without it, recording and import do not work, and
  the tests that need it skip.
- **Node.js** (20 or newer), only for the tests that start the cloud mock gateway.
- A **Soniox** API key (transcription) and an OpenAI-compatible LLM endpoint (summaries) are
  optional run-time settings. Builds and tests do not need them.

## Linux (Fedora)

```bash
sudo dnf install gcc-c++ cmake ninja-build ccache git \
    qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel \
    qt6-qtmultimedia-devel qt6-qtsvg-devel qt6-qttools-devel qt6-linguist \
    onnxruntime-devel \
    ffmpeg-free nodejs24 python3
```

`ffmpeg-free` is enough: it includes libopus and libmp3lame. `tools/ci/linux-deps.sh` installs the same list (it is what CI uses).

Build and run:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -j4 --output-on-failure

./build/gui/tanara               # GUI
./build/cli/tanara-cli           # CLI
./build/watcher/tanara-watcher   # tray watcher
```

## Windows (MinGW from Qt)

The build uses the MinGW 13.1 toolchain that comes with Qt (the `tools_mingw1310` tool of the
Qt installer or aqtinstall). You do not need an MSVC kit.

Prerequisites:

- Qt 6.11.1 `mingw_64`, with the Multimedia and Shader Tools add-on modules.
- Qt MinGW 13.1 (`C:\Qt\Tools\mingw1310_64`), Ninja and CMake on `PATH`.
- ONNX Runtime win-x64 1.20.1, unpacked (voice-ID builds only).
- `ffmpeg.exe` and `ffprobe.exe` on `PATH`. For a build that you give to other people, use the
  pinned LGPL-only static build that is described in `packaging/windows/README.md`.
  Do not ship a GPL build.
- Node.js and Python 3 for the full test run (optional; those tests skip without them).

Fast bring-up without speaker recognition:

```powershell
$qt = 'C:\Qt\6.11.1\mingw_64'     # adjust to your install
$env:PATH = "$qt\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;$env:PATH"

cmake -S . -B build -G Ninja "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release `
      -DTANARA_BUILD_VOICEID=OFF
cmake --build build -j 4
```

Full build with speaker recognition. Unpack an ONNX Runtime win-x64 release
(<https://github.com/microsoft/onnxruntime/releases>, for example
`onnxruntime-win-x64-1.20.1`). `cmake/OnnxRuntimeWin.cmake` wires it in through
`ONNXRUNTIME_ROOT_DIR`; KISS FFT is built from the vendored sources, so no package is needed.

```powershell
cmake -S . -B build -G Ninja "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release `
      -DTANARA_BUILD_VOICEID=ON "-DONNXRUNTIME_ROOT_DIR=C:\tools\onnxruntime-win-x64-1.20.1"
cmake --build build -j 4

$env:QT_QPA_PLATFORM = 'offscreen'; $env:TANARA_HOME = "$env:TEMP\tanara-ctest"
ctest --test-dir build -j 4 --output-on-failure
Remove-Item Env:QT_QPA_PLATFORM, Env:TANARA_HOME
```

The tests find `onnxruntime.dll` through a post-build copy step. At run time the app needs
`onnxruntime.dll` next to `tanara.exe`.

Run from a build tree: `build\gui\tanara.exe`, `build\cli\tanara-cli.exe`,
`build\watcher\tanara-watcher.exe`, with `%QT%\bin` and the MinGW `bin` folder on `PATH`.

### Standalone folder

A self-contained folder that runs without Qt on `PATH` (for your own use):

```powershell
mkdir dist; copy build\gui\tanara.exe dist; copy build\cli\tanara-cli.exe dist
& "$qt\bin\windeployqt.exe" --release --compiler-runtime --qmldir gui\qml --dir dist dist\tanara.exe dist\tanara-cli.exe
copy C:\tools\onnxruntime-win-x64-1.20.1\lib\onnxruntime.dll dist   # voice-ID builds only
copy C:\path\to\ffmpeg.exe, C:\path\to\ffprobe.exe dist
```

For a package that you give to others (licenses, VC++ runtime, LGPL FFmpeg, installer), follow
`packaging/windows/README.md`.

## CMake options

| Option | Default | Effect |
|---|---|---|
| `TANARA_BUILD_GUI` | ON | Build the Qt Quick GUI (and the UI tests). OFF: core and CLI only, faster iteration, no display libraries needed at run time. |
| `TANARA_BUILD_WATCHER` | ON | Build the tray meeting watcher. |
| `TANARA_BUILD_VOICEID` | ON | ONNX-based speaker recognition. OFF: ONNX Runtime and KISS FFT are not needed; the app records, transcribes and summarizes, but skips speaker labels. |
| `TANARA_BUILD_CLOUD` | ON | Tanara Cloud client (sign-in, cloud providers, estimate, balance). OFF: nothing cloud-related is registered. |
| `TANARA_CLOUD_TEASER` | ON | The "coming soon" waitlist panel and the hint next to Settings. |
| `ONNXRUNTIME_ROOT_DIR` | unset | Windows only: path of the unpacked ONNX Runtime release. |

Headless build (no GUI, no watcher; this is what the `linux-headless` CI job builds):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DTANARA_BUILD_GUI=OFF -DTANARA_BUILD_WATCHER=OFF
```

## Tests

```bash
export QT_QPA_PLATFORM=offscreen
export TANARA_HOME="$(mktemp -d)"      # sandbox: never test against real data
ctest --test-dir build -j4 --output-on-failure
```

- Every file in `tests/unit/` is its own test executable. Every file in `tests/ui/` is a UI
  test of the QML view models; it runs on the `offscreen` platform with the software renderer,
  so no display is needed (the CMake file sets this for those tests).
- `TANARA_HOME` moves all app data (settings, people, voiceprints, logs) away from `~/.tanara`.
  Always set it when you run tests or experiments, so that your real meetings stay untouched.
- Tests never use the network. The cloud tests start `tests/mock-gateway/mock-gateway.mjs`
  (Node, no packages) on a local port.
- Some tests skip (`QSKIP`) when a prerequisite is missing: no `ffmpeg`/`ffprobe` (or no
  libopus / libmp3lame in it), no speaker model under `~/.tanara/models`, no `node`, or no
  usable sandbox. CTest counts a skipped test as passed, so check the skip list:
  `python3 tools/ci/qskip-summary.py build/Testing/Temporary/LastTest.log`.
- Translations: `cmake --build build --target update_translations` refreshes
  `i18n/tanara_en.ts`. Hungarian is the source language.

## Running the CI steps locally

CI (`.github/workflows/ci.yml`) runs three jobs on every push: `linux` (Fedora 44 container,
full build and tests), `linux-headless` (no GUI, no watcher) and `windows` (MinGW 13.1 from
aqtinstall). The steps are plain scripts in `tools/ci/`, so you can run them yourself.

Linux, in the same container image that CI uses:

```bash
podman run --rm -v "$PWD":/src:Z -w /src fedora:44 bash -c \
  'bash tools/ci/linux-deps.sh && bash tools/ci/build-test.sh /src/build'
# headless variant:
podman run --rm -v "$PWD":/src:Z -w /src fedora:44 bash -c \
  'bash tools/ci/linux-deps.sh && bash tools/ci/build-test.sh /src/build-headless -DTANARA_BUILD_GUI=OFF -DTANARA_BUILD_WATCHER=OFF'
```

`build-test.sh [build-dir] [extra cmake args...]` configures with Ninja and ccache (if present),
builds and runs ctest with `JOBS` (default 4) jobs, uses a throw-away `TANARA_HOME`, and prints
the QSKIP summary. On your own machine you can skip the container and run it directly.

Windows (PowerShell, from the repository root):

```powershell
powershell -File tools\ci\windows-deps.ps1 -Dest C:\ci-deps          # ONNX Runtime + LGPL FFmpeg, SHA256-checked
$env:ONNXRUNTIME_ROOT_DIR = 'C:\ci-deps\onnxruntime-win-x64-1.20.1'
$env:PATH = 'C:\ci-deps\ffmpeg-lgpl\ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0\bin;' + $env:PATH
powershell -File tools\ci\build-test.ps1 -QtDir C:\Qt\6.11.1\mingw_64 -MingwBin C:\Qt\Tools\mingw1310_64\bin
```

`windows-deps.ps1 -WhatIf` only prints what it would download. Versions and checksums of the
downloads live in that script and must match `packaging/windows/README.md`.

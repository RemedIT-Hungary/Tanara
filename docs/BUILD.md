# Building Tanara

Tanara is a C++20 / Qt 6 (Quick) project built with CMake and Ninja. This page covers a
developer build on Fedora, on Windows (MinGW) and on macOS (Homebrew), the CMake options, how to run the tests,
and how to run the CI steps on your own machine. For the release packages (Windows installer,
AppImage, RPM, Flatpak experiment) see [Packaging](#packaging) below.

## Requirements

- **C++20** compiler, **CMake >= 3.24** (`cmake_minimum_required` says 3.21, but with 3.23 the
  Windows configure fails at generate time: "Target Qt6::QmlIntegration not found" in `tests/`;
  the CMake that ships with Qt, `C:\Qt\Tools\CMake_64`, is fine), **Ninja**
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
    onnxruntime-devel qtkeychain-qt6-devel \
    ffmpeg-free nodejs24 python3
```

`qtkeychain-qt6-devel` (QtKeychain, BSD-3-Clause) stores the API keys in the system keychain
(Secret Service / KWallet). It is optional: without it the build keeps the keys in a file that only
the user can read (`TANARA_USE_KEYCHAIN=OFF` turns it off on purpose).

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
- Qt MinGW 13.1 (`C:\Qt\Tools\mingw1310_64`), Ninja and CMake on `PATH`. Put Qt's own
  `C:\Qt\Tools\CMake_64\bin` and `C:\Qt\Tools\Ninja` first: a standalone CMake older than 3.24
  (for example 3.23 from the Kitware installer) does not configure this project.
- ONNX Runtime win-x64 1.20.1, unpacked (voice-ID builds only).
- `ffmpeg.exe` and `ffprobe.exe` on `PATH`. For a build that you give to other people, use the
  pinned LGPL-only static build that is described in `packaging/windows/README.md`.
  Do not ship a GPL build.
- Node.js and Python 3 for the full test run (optional; those tests skip without them).

Fast bring-up without speaker recognition:

```powershell
$qt = 'C:\Qt\6.11.1\mingw_64'     # adjust to your install
$env:PATH = "$qt\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;$env:PATH"

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

## macOS (Homebrew)

A developer build: the GUI, the CLI and the watcher build, pass the tests and run from the build
tree, or as an ad-hoc signed app bundle (below). There is no Developer ID signing and no
system-audio capture or meeting detection yet (the platform stubs are used); the port status and plan are in [macos.md](macos.md). Tested on
Apple silicon (macOS 27) with the Xcode Command Line Tools, Homebrew's Qt 6.12, ONNX Runtime 1.31
and QtKeychain 0.17.

```bash
xcode-select --install                     # once: AppleClang and the SDK
brew bundle --file=tools/ci/Brewfile       # cmake, ninja, ccache, qt, qtkeychain, onnxruntime, ffmpeg, node, python
tools/ci/build-test.sh build-mac -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"

./build-mac/gui/tanara
./build-mac/cli/tanara-cli
./build-mac/watcher/tanara-watcher
```

Homebrew's ONNX Runtime is found through `find_package`, so voice-ID needs no extra option.
Homebrew's `ffmpeg` is a GPL build: fine for development, never put it in a package. Unsigned
builds started from a terminal get the microphone permission under the terminal's name.

Run the tests on their own (the script already does this, with a throw-away `TANARA_HOME`):

```bash
QT_QPA_PLATFORM=offscreen TANARA_HOME="$(mktemp -d)" ctest --test-dir build-mac -j8 --output-on-failure
```

### macOS app bundle

`TANARA_MACOS_BUNDLE=ON` builds the GUI as `tanara.app` with `tanara-cli` and `tanara-watcher` next
to it in `Contents/MacOS`. The `tanara_bundle` target runs `macdeployqt` (Qt frameworks, QML imports,
Homebrew dylibs) and signs the bundle ad hoc. The `macos-xcode` preset generates an Xcode project
for this in `build-xcode/`.

From the command line (`cmake --build` runs `xcodebuild`):

```bash
cmake --preset macos-xcode
cmake --build --preset macos-xcode-bundle      # -> build-xcode/gui/Release/tanara.app
open build-xcode/gui/Release/tanara.app
```

Or open `build-xcode/tanara.xcodeproj` in Xcode, pick the `tanara_bundle` scheme, set
Product > Scheme > Edit Scheme > Run > Build Configuration to Release, and build. Use the `tanara` scheme
for day-to-day debugging; it builds the bundle without the Qt deployment step.

You can also call `xcodebuild` directly:

```bash
xcodebuild -project build-xcode/tanara.xcodeproj -scheme tanara_bundle -configuration Release build
```

The bundle contains no FFmpeg (Homebrew's is GPL, see above). Started from Finder, the app does not
see Homebrew's `PATH`, so set the FFmpeg and FFprobe paths in Settings (`/opt/homebrew/bin/ffmpeg`,
`/opt/homebrew/bin/ffprobe`), or put an LGPL build into `Contents/Resources/bin`, where Tanara
looks first. Ad-hoc signed apps run only on the machine that built them, unless the user
removes the quarantine attribute; distribution needs Developer ID signing and notarization
([macos.md](macos.md) section 7).

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
| `TANARA_USE_KEYCHAIN` | ON | Store the API keys in the OS keychain through QtKeychain. Linux: the distro package (`qtkeychain-qt6-devel`) if found. Windows: if no installed QtKeychain is found, CMake `FetchContent` builds the pinned release 0.16.0 (needs git + network at configure time). Not found / OFF: file only. Sandboxes (`TANARA_HOME`) and tests always use the file. |

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

## Packaging

Release packages are built from a tag `v<version>` (for example `v0.5.11`). The tag must equal
the CMake version: `project(VERSION)` in `CMakeLists.txt` plus `TANARA_VERSION_SUFFIX`
(`tools/ci/package-version.sh <tag>` checks it; the release workflow fails at once if they differ).
There is no `.deb` package, and nothing is code-signed yet (decision D13; signing is phase 3).

| Package | Built by | Where it runs |
|---|---|---|
| Windows installer (`Tanara-<v>-win64-setup.exe`, per-user, no admin rights) and zip | `tools/ci/package-windows.ps1` | GitHub Actions `windows-2022`, or any Windows machine with the build prerequisites |
| AppImage (`Tanara-<v>-x86_64.AppImage`) | `tools/ci/package-appimage.sh` | GitHub Actions `ubuntu-22.04`, or an `ubuntu:22.04` container |
| RPM (`tanara`) | Fedora COPR, `.copr/Makefile` + `packaging/rpm/tanara.spec` | COPR (the project and the webhook are set up by the maintainer) |
| Flatpak (experiment only) | `flatpak-builder`, `packaging/flatpak/eu.remedit.Tanara.yml` | local; not in the release workflow, see `packaging/flatpak/TESTING.md` |

### Release workflow

`.github/workflows/release.yml` runs on a pushed `v*` tag: `check-version`, then `windows-installer`
and `appimage` in parallel, then `release`, which attaches the files and their `.sha256` files to a
**draft** GitHub release. The maintainer reads the draft and publishes it. To try it without a real
release, push a throw-away tag whose CMake version matches (or a tag in a fork) and delete the draft
and the tag afterwards.

Experimental builds for testers: a version with a letter suffix (`TANARA_VERSION_SUFFIX "a"`,
tag `v0.5.305a`) is published as a **pre-release** with a note for testers; GitHub never shows it as
"Latest". The installer and the AppImage are release assets, not files in the repository, so when
the feedback round is over the whole thing goes away with
`gh release delete v0.5.305a --cleanup-tag --yes` (the release and the tag) and nothing stays in the
history. Remove the suffix (`""`) for a real release.

The release jobs do not run the tests again: CI (`ci.yml`) already ran them on the same commit.

### Windows installer

```powershell
powershell -File tools\ci\windows-deps.ps1 -Dest C:\ci-deps      # ONNX Runtime + LGPL FFmpeg (SHA256-checked)
powershell -File tools\ci\package-windows.ps1 -DepsDir C:\ci-deps `
    -QtDir C:\Qt\6.11.1\mingw_64 -MingwBin C:\Qt\Tools\mingw1310_64\bin
```

The script builds (unless `-SkipBuild`), assembles `build-release\dist` (windeployqt with the QML
imports, `qoffscreen`, `onnxruntime.dll`, the VC++ runtime DLLs from the Visual Studio `Redist`
folder, the LGPL FFmpeg, the CAM++ model downloaded with a SHA256 check, the license files), runs
a smoke test on a clean `PATH` (`tanara-cli --version`, `paths show`, `tanara --qml-shot`), writes
the zip and runs Inno Setup (`ISCC`; on CI it is installed with Chocolatey when it is missing, on a
developer machine the script stops with a message). The result is in `build-release\`. These are the scripted steps of `packaging/windows/README.md` (that file explains
why each step is there). Qt 6.11 needs the `qttasktree` add-on module with aqtinstall, and on
Windows an aqtinstall newer than the 3.3.0 PyPI release: Qt 6.11 moved the Windows packages into
per-arch folders on download.qt.io, which 3.3.0 cannot resolve ("Failed to download checksum for
the file 'Updates.xml'"). The workflows pass `aqtsource` with the fixed commit to
`install-qt-action` (miurahr/aqtinstall#1000); locally, `pip install git+https://github.com/miurahr/aqtinstall.git@<that commit>`.

Switches for a machine without Visual Studio and Inno Setup:

- `-SkipInstaller`: build, `dist`, smoke test and zip run, then the script exits 0 without an
  installer (no ISCC lookup, no Chocolatey).
- The VC++ runtime DLLs (`msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`) are needed only by
  `onnxruntime.dll`; Tanara's own MinGW binaries do not need the MSVC CRT. The script takes them from
  `-CrtDir`, else from a Visual Studio / Build Tools `Redist` folder, else (only with `-DownloadCrt`)
  from Microsoft's `vc_redist.x64.exe`, which it downloads into `-DepsDir` and unpacks without
  installing it (`/layout`, then `msiexec /a`). That URL is a moving target, so the download is NOT
  SHA256-pinned; the script checks the Microsoft Authenticode signature of the exe and of each DLL.
  CI does not use `-DownloadCrt`.

```powershell
powershell -File tools\ci\package-windows.ps1 -DepsDir C:\ci-deps -QtDir ... -MingwBin ... -SkipInstaller -DownloadCrt
```

### AppImage

```bash
# on ubuntu:22.04 (a container is fine); Qt 6.11.1 linux_gcc_64 from aqtinstall, with the modules
#   qtmultimedia qtshadertools qttasktree
tools/ci/package-appimage.sh --install-deps
QT_ROOT_DIR=/path/to/6.11.1/gcc_64 tools/ci/package-appimage.sh [v0.5.11]
# -> build-appimage/out/Tanara-<version>-x86_64.AppImage (+ .sha256)
```

The script downloads pinned, SHA256-checked tools and libraries (CMake 3.31, linuxdeploy and its Qt
plugin, appimagetool, the BtbN LGPL FFmpeg, ONNX Runtime 1.20.1, QtKeychain 0.16.0, the CAM++ model),
builds Tanara, lets `linuxdeploy` collect the libraries, the QML imports (`gui/qml`) and the Qt
plugins, and squashes the result. Ubuntu 22.04 (glibc 2.35) is the build image on purpose: an
AppImage runs on distributions with the same or a newer glibc. `ffmpeg` and `ffprobe` are in
`usr/bin`; `MediaToolLocator` finds them through `$APPDIR` (the `bundle` source in the log).

One AppImage contains three programs. `packaging/linux/AppRun` starts one of them:

```bash
./Tanara-0.5.11-x86_64.AppImage                      # the GUI
./Tanara-0.5.11-x86_64.AppImage --tanara-cli paths show
./Tanara-0.5.11-x86_64.AppImage --tanara-watcher
ln -s Tanara-0.5.11-x86_64.AppImage tanara-cli       # started under this name: the CLI
```

Known limits: the watcher's "start at sign-in" entry writes the path of the running program, which
inside an AppImage is a temporary mount; use the CLI/watcher through the AppImage file itself
(for example an autostart entry with `Exec=/path/to/Tanara.AppImage --tanara-watcher`) until the
app writes `$APPIMAGE` there.

### RPM (Fedora COPR)

`packaging/rpm/tanara.spec` builds with `%cmake`, runs `ctest` offscreen in `%check`, and installs
the three programs, the desktop file, the AppStream file and the icon. `.copr/Makefile`
(`make -f .copr/Makefile srpm outdir=... spec=...`) makes the source tarball with `git archive` and
writes the version from `CMakeLists.txt` into the spec. Local check:

```bash
make -f .copr/Makefile srpm outdir=/tmp/tanara-srpm
rpmbuild --rebuild /tmp/tanara-srpm/tanara-*.src.rpm      # full build and tests (takes a while)
```

The CAM++ model is not in the RPM (Fedora packages are built without network access); users run
`tanara-cli voice-models fetch campplus`.

### Flatpak (experiment)

`packaging/flatpak/eu.remedit.Tanara.yml` and the checklist `packaging/flatpak/TESTING.md`. Not
part of the release workflow until the checklist has passed.

### Bundled components and licenses

The Windows and AppImage packages carry `LICENSE`, `THIRD_PARTY_NOTICES.md` and a `licenses`
folder (`licenses\` next to `tanara.exe`; `usr/share/licenses/tanara/` in the AppImage). When you
change the bundled components (versions, new library), update `THIRD_PARTY_NOTICES.md`,
`packaging/licenses/` and the pinned URLs and checksums in the scripts, `tools/ci/windows-deps.ps1`
and the Flatpak manifest together. The FFmpeg build must stay LGPL-only (the scripts check
`--enable-gpl` / `--enable-nonfree`). Check the license of a model before it goes into a package.

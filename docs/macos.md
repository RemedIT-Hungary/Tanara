# macOS port plan

Status: **plan only.** Tanara does not build or run on macOS yet, and there is no Mac to test on
(decision D4 of the cleanup). This document records what a macOS port needs, where each piece plugs
in, and the order of work for a first Mac build. Nothing here is implemented unless it says "done".

## 1. Where the platform code lives

Platform code sits in `*/platform/<os>/` folders. `core/CMakeLists.txt` compiles only the current
platform's folders:

| Platform | Folders compiled |
|---|---|
| Linux | `linux`, `posix` |
| Windows | `windows` |
| Anything else (today: macOS) | `posix`, `unsupported` |

The common code has no `#if Q_OS_*` (the platform rule of `tools/check_modules.py`). A macOS port
adds a `macos` folder next to the existing ones and changes the CMake rule to
`APPLE -> macos posix`. Every `unsupported/` file then gets a `macos/` sibling, one by one; a port
that is not ready on macOS can keep a copy of the `unsupported` stub inside `macos/`.

The ports and their state:

| Port | Declared in | Linux | Windows | macOS today (`unsupported`) | macOS plan |
|---|---|---|---|---|---|
| `recording::ISystemAudioSource` | `recording/audio/SystemAudioSource.h` | monitor sources | WASAPI loopback | names only, no system audio | Core Audio process tap (section 2) |
| `watcher::IMeetingDetector` (built-ins) | `watcher/detect/DetectorRegistry.h` | `linux-capture` (pw-dump) | `windows-wasapi` | none registered | `macos-coreaudio` (section 3) |
| `watcher::IAutostart` | `watcher/detect/Autostart.h` | XDG `.desktop` | HKCU Run | `entryPath()` empty | `SMAppService` (section 4) |
| `watcher::IPlaybackRouting` | `watcher/PlaybackRouting.h` | pw-dump | not supported | not supported | Core Audio process objects (section 5) |
| `watcher::RunningApps` | `watcher/RunningApps.h` | `/proc` | Toolhelp32 | not supported | `libproc` (section 6) |
| `platform::IPowerInhibitor` | `foundation/platform/PowerInhibitor.h` | logind / desktop portal | `SetThreadExecutionState` | **written, untested**: `IOPMAssertionCreateWithName(PreventUserIdleSystemSleep)` under `__APPLE__` in `unsupported/UnsupportedPowerInhibitor.cpp` (IOKit linked in `core/CMakeLists.txt`) | move it to `macos/` and verify with `pmset -g assertions` while recording |
| `foundation::platform` | `foundation/platform/Platform.h` | `linux` + `posix` | `windows` | `posix` + `unsupported` (works) | move `osId()` and `caseSensitiveFileSystem()` to `macos/` |
| `media::MediaToolLocator` | `media/MediaToolLocator.h` | PATH | next to the exe | **done**: `Contents/Resources/bin`, `Contents/MacOS` | — |
| `paths::AppPaths` | `foundation/paths/AppPaths.h` | XDG | `%APPDATA%` | **done**: `~/Library/Application Support/Tanara`, `~/Library/Logs/Tanara` | — |
| `SecretStore` | `foundation/secrets/SecretStore.h` | QtKeychain | QtKeychain | **done** (QtKeychain has a Keychain backend) | — |

Already POSIX-generic and expected to work unchanged: user display name (`getpwuid`, gecos),
process liveness for the lock files (`kill(pid, 0)`), the per-user local-socket name of the
recorder singleton (`-<uid>` suffix), microphone capture through miniaudio's Core Audio backend
(not tested).

## 2. System audio capture

miniaudio's Core Audio backend records input devices only; macOS has no "monitor of the output"
device and no WASAPI-style loopback. There are three ways to get the remote participants' audio:

| Option | macOS | Permission | Per app | Notes |
|---|---|---|---|---|
| **Core Audio process tap** (`AudioHardwareCreateProcessTap`, `CATapDescription`) | 14.2+ | "System audio recording" (TCC), text from `NSAudioCaptureUsageDescription` | yes (include / exclude process lists) | Recommended. Fits the call-app model: tap only the call app, or everything except Tanara. |
| **ScreenCaptureKit** audio (`SCStream`, `capturesAudio`) | 13+ | Screen recording ("Screen & System Audio Recording" in 15) | per app via the content filter | Needs the screen-recording permission even for audio only; users read that as "it records my screen". |
| **Virtual device** (BlackHole and similar) | any | microphone only | no | The user installs a driver and builds a multi-output device by hand. Works today without code: the device shows up as an input and the name rules (`domain/AudioDeviceNames.h`) classify a name with "loopback" as system audio. Support only as a documented fallback. |

Plan with the process tap:

1. Create a `CATapDescription` (stereo mixdown; `excludedProcesses` = Tanara's own process
   objects, or `processes` = the detected call app), then `AudioHardwareCreateProcessTap`.
2. Create an aggregate device (`AudioHardwareCreateAggregateDevice`) whose tap list holds the tap.
3. The aggregate device is an input device. Two ways to record it:
   - let miniaudio enumerate and open it like any capture device (`ISystemAudioSource::loopbackDevices`
     returns it as a `Loopback` device; `opensFromPlayback` stays false). To verify first: a
     *private* aggregate device is visible only to the creating process, which is the recorder
     itself, so miniaudio should list it; if not, create it non-private with a Tanara-specific UID
     and destroy it on exit.
   - or record it with an `AudioDeviceIOProcID` in a small macOS-only capture class that feeds the
     same `RingBuffer` as the miniaudio callback. Only if miniaudio cannot open it.
4. The port then needs a lifecycle the Linux and Windows adapters do not: create the tap before
   enumeration and destroy tap + aggregate device when the recording ends. Add
   `prepare()` / `release()` (no-ops on Linux and Windows) to `ISystemAudioSource`, called by
   `DeviceManager::refresh()` and the recording session.

The `AudioEngine` mechanics (miniaudio, ring buffers, track files) do not change.

Info.plist keys:

- `NSMicrophoneUsageDescription`: "Tanara records your microphone during the meetings you choose to record."
- `NSAudioCaptureUsageDescription` (process tap): "Tanara records the sound of the call app so the
  other participants' voices are in the recording."
- Only with ScreenCaptureKit: the screen-recording permission has no usage string; macOS shows its
  own prompt and the user must enable Tanara in System Settings, then restart the app. The onboarding
  must explain this.

## 3. Meeting detection: `MacCaptureDetector`

Id `macos-coreaudio`, registered in `core/src/watcher/platform/macos/BuiltinDetectors.cpp`.

- `kAudioHardwarePropertyProcessObjectList` (14.2+) lists the Core Audio process objects.
- For each: `kAudioProcessPropertyIsRunningInput` = the process is recording from an input device
  now; `kAudioProcessPropertyPID` and `kAudioProcessPropertyBundleID` give the app.
- Match the bundle id / executable name against the known call apps (the same rule set as
  `detail/WinCaptureRules.h`: normalize, self-exclusion of `tanara*`, app-id matching). Browsers
  (Meet, Teams web) are matched by the browser's bundle id, as on the other platforms.
- No permission is needed to read these properties. Poll synchronously like the other detectors;
  a property listener (`AudioObjectAddPropertyListenerBlock`) can come later.
- Before 14.2 the only signal is `kAudioDevicePropertyDeviceIsRunningSomewhere` on the input
  device: a boolean without the app. Not worth a separate detector.

## 4. Autostart

| Option | macOS | Notes |
|---|---|---|
| **`SMAppService`** (`.agent(plistName:)` or `.loginItem(identifier:)`) | 13+ | The supported API. The plist / helper lives inside the signed bundle (`Contents/Library/LaunchAgents/eu.remedit.Tanara.watcher.plist`), macOS lists it in System Settings > Login Items, and the user can switch it off there. Needs a signed app. |
| LaunchAgent plist in `~/Library/LaunchAgents` | any | Works unsigned (developer builds). macOS 13+ shows a "background item added" notice. |

Plan: `macos/SMAppServiceAutostart.mm` implements `IAutostart` with `SMAppService.agent`
(`register`, `unregister`, `status == .enabled`); `entryPath()` returns the plist name for display.
Fall back to a `~/Library/LaunchAgents` plist only when the app is not signed. The sandbox guard
(`TANARA_HOME`, test mode) stays in `autostart::applyWatcher()` and needs no change.

## 5. Playback routing

`IPlaybackRouting` answers "which app plays to which output". On macOS 14.2+ the same process
objects give it: `kAudioProcessPropertyIsRunningOutput` and `kAudioProcessPropertyDevices` (the
output devices a process uses); the device name comes from `kAudioObjectPropertyName`. The
system default output is `kAudioHardwarePropertyDefaultOutputDevice`. `deviceKeys` (the hot-plug
signal) = the sorted UIDs of `kAudioHardwarePropertyDevices`. `queryAsync` can run the query on a
worker thread and deliver the snapshot with a queued call; there is no external process as with
pw-dump.

## 6. Running apps

`RunningApps::processes()` on macOS: `proc_listpids(PROC_ALL_PIDS, ...)` + `proc_pidpath()` from
`<libproc.h>` (plain C++, no AppKit, so it also works in `tanara-cli`), name = the lowercase file
name of the executable path. `NSWorkspace.runningApplications` gives nicer names and bundle ids but
needs Objective-C++ and AppKit, and lists only GUI apps; use it only if the Settings list needs
the display names.

## 7. Bundle, signing and notarization

- Bundle id: `eu.remedit.Tanara` (decision 6 of the cleanup). `tanara`, `tanara-watcher` and
  `tanara-cli` go into `Contents/MacOS`.
- `macdeployqt Tanara.app -qmldir=gui/qml` copies Qt and the QML imports into `Contents/Frameworks`
  and `Contents/Resources/qml`.
- `Info.plist`: `CFBundleIdentifier`, `CFBundleShortVersionString` = `TANARA_VERSION`,
  `LSMinimumSystemVersion` (section 10), `NSMicrophoneUsageDescription`,
  `NSAudioCaptureUsageDescription`, `LSUIElement` = false for `tanara` (the watcher is a menu-bar
  agent: its own plist or `LSUIElement` = true if it becomes a helper app).
- ffmpeg / ffprobe: static arm64 + x86_64 (or universal) binaries in `Contents/Resources/bin`.
  `MediaToolLocator` already looks there (done). They must be signed with the same identity.
  Check the ffmpeg build's license (LGPL build without GPL parts, as on Windows).
- Signing: `codesign --options runtime --timestamp` on every Mach-O (frameworks, dylibs, ffmpeg,
  the three executables), then the bundle. Entitlements for the hardened runtime:
  `com.apple.security.device.audio-input`. No App Sandbox (Tanara reads and writes the user's
  documents folder and runs ffmpeg).
- Notarization: `xcrun notarytool submit Tanara.dmg --wait`, then `xcrun stapler staple`.
  Distribute as a DMG.

## 8. ONNX Runtime and other dependencies

- ONNX Runtime: the official `onnxruntime-osx-universal2-<ver>.tgz` release. Add a CMake branch like
  `cmake/OnnxRuntimeWin.cmake` (imported target from `ONNXRUNTIME_ROOT_DIR`), copy
  `libonnxruntime.<ver>.dylib` into `Contents/Frameworks`, and set its install name to
  `@rpath/...` (`install_name_tool`), the executables' rpath to `@executable_path/../Frameworks`.
  Homebrew's `onnxruntime` works for a local developer build but is not universal.
- kissfft: vendored and built from `third_party/kissfft` on every platform (done).
- kaldi-native-fbank, miniaudio, miniz: vendored, build unchanged.
- QtKeychain: Homebrew `qtkeychain` or the same pinned FetchContent as on Windows (extend the
  `WIN32` condition in `core/CMakeLists.txt` to `APPLE`).

## 9. Keyboard differences (`TranscriptKeys`)

- `gui/qml/TranscriptKeys.qml` reads the digit row, comma / period and `[` `]` from
  `event.nativeScanCode` on Linux and Windows. On macOS QML gets no native scan code (0), so today
  Shift+digit works only where `event.key` is already a digit (not on a Hungarian layout, where
  Shift+6 types "/"). Fix: a small C++ event filter that exposes `QKeyEvent::nativeVirtualKey()`
  (macOS virtual key codes `kVK_ANSI_1` ... `kVK_ANSI_9` = 18, 19, 20, 21, 23, 22, 26, 28, 25;
  comma 43, period 47, brackets 33 / 30) and a `macos` table in `TranscriptKeys`.
- Qt swaps Control and Command on macOS: `Qt.ControlModifier` is the Command key. The shortcuts
  written with Ctrl (Ctrl+click in the library, Ctrl+Enter, Ctrl+Z) become Command, which is what
  Mac users expect. Check the help texts and the onboarding, which name "Ctrl".
- Option (Alt) types characters on macOS (Option+digit); do not use Alt+letter shortcuts.

## 10. Minimum macOS version

Recommendation: **macOS 14.2 (Sonoma)**.

- The process tap (14.2+) gives per-app system audio with a dedicated, understandable permission,
  and the same process objects give the meeting detector (`kAudioProcessPropertyIsRunningInput`)
  and the playback routing. One API family for three ports.
- With macOS 13 the system audio needs ScreenCaptureKit (screen-recording permission) and there is
  no per-app microphone signal for the detector, so automatic meeting detection would not work.
  That is two extra implementations for a shrinking user base (13 is out of security support).
- Qt does not force 14.2; it is a product-driven limit set by the audio APIs.

## 11. Steps for a first Mac build

1. CMake: `APPLE -> macos posix` platform folders; copy every `unsupported/` file into `macos/`
   so the build links (stubs first). `osId()` returns "macos", `caseSensitiveFileSystem()` false.
2. Dependencies: Qt 6.11 (official installer, universal), QtKeychain, ONNX Runtime universal2
   (section 8), or `-DTANARA_BUILD_VOICEID=OFF` for the very first build.
3. Build and run `ctest` headless (`QT_QPA_PLATFORM=offscreen`). Expected failures: tests that
   assert Linux or Windows behavior without an `#if`; fix them as they show up.
4. Run the app with microphone-only recording (miniaudio Core Audio backend); check the
   microphone permission prompt (unsigned builds attribute it to the terminal).
5. `RunningApps` with `libproc` (section 6): small and testable, the Settings list fills.
6. `MacCaptureDetector` + `IPlaybackRouting` on the process objects (sections 3 and 5).
7. Process-tap `ISystemAudioSource` with the `prepare()` / `release()` lifecycle (section 2).
8. `SMAppService` autostart (section 4) once the app is signed.
9. `TranscriptKeys` virtual-key table (section 9).
10. Bundle: `macdeployqt`, Info.plist, ffmpeg in `Contents/Resources/bin`, codesign, notarize, DMG
    (section 7). Then an optional compile-only macOS CI job (decision 7: later).

## 12. Open questions

- Is a private aggregate device visible to miniaudio's enumeration in the same process
  (section 2, step 3)? Decides between "miniaudio opens it" and a separate Core Audio capture class.
- Tap everything except Tanara, or only the detected call app? Per-app is cleaner (no
  notification sounds) but needs the call app known before the recording starts.
- Developer signing identity and notarization account (Apple Developer Program membership).
- ffmpeg for macOS: build it ourselves (universal, LGPL) or take a trusted static build.

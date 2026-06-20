# Tanara

**Tanara** is a local-first meeting recorder, transcriber and summarizer for the
desktop. It records every audio device as a **separate track**, transcribes them
with good Hungarian accuracy, recognizes who is speaking (by voice), and produces
a structured summary — all stored as plain files next to the audio.

> Built as a self-hosted alternative to cloud meeting assistants that don't run on
> Linux, don't capture your own microphone reliably, and send everything to the cloud.

- **Privacy-first:** recording, diarization, speaker recognition and summarization
  run locally. Only the (optional) speech-to-text call goes to a cloud API.
- **Multi-track capture:** each device (your mic + system/loopback audio) is a
  separate Opus track, plus a mixed `mixdown.mp3`. Your own voice is never lost.
- **Speaker recognition:** an on-device voice-embedding model auto-labels recurring
  speakers across meetings; you can listen to and correct labels.
- **Open formats:** transcript and summary are Markdown files stored beside the audio.

Status: **working on Linux and Windows.** On Windows, system audio is captured via
WASAPI loopback (playback devices appear as "loopback" capture sources), the voice-ID
stack (KISS FFT + ONNX Runtime) is validated, and a standalone build is produced with
`windeployqt`. macOS is not yet targeted.

---

## How it works

```
record (miniaudio, per device)  →  track_*.ogg + mixdown.mp3
   → transcribe (Soniox, per track, Hungarian)  →  transcript.md / .tokens.json / .segments.json
   → speaker ID (CAM++ ONNX embedding + cosine)  →  auto-labels recurring voices
   → summarize (local LLM, OpenAI-compatible)     →  summary.md
```

- **Architecture:** a UI-independent core library (`tanara_core`, no Qt Widgets)
  with a Qt Widgets GUI (`tanara`) and a headless CLI (`tanara-cli`) on top. The
  linker boundary enforces the UI⟂backend split (a QML front-end can reuse the same core).
- Per-meeting folder layout (under your recordings dir):
  `meeting.json`, `track_*.ogg`, `mixdown.mp3`, `transcript.md`,
  `transcript.tokens.json`, `transcript.segments.json`, `summary.md`.
- App data lives in `~/.tanara/`: `settings.json`, `index.db` (rebuildable cache),
  `people.json`, `voiceprints.json`, `secrets.json`, plus `models/`.

## Requirements

- **C++20**, **CMake ≥ 3.21**, **Ninja**
- **Qt 6** (Core, Network, Sql, Widgets, Multimedia, Test)
- **ONNX Runtime** (dev package) — for the speaker-embedding model
- **KISS FFT** (float build) — used by the bundled kaldi-native-fbank
- **FFmpeg** CLI — for audio encode/decode (called as an external program)
- A **Soniox** API key (for transcription) and an **OpenAI-compatible LLM endpoint**
  (e.g. LM Studio / Ollama) for summaries — both optional, configured in-app.

### Install dependencies (Fedora)

```bash
sudo dnf install cmake ninja-build gcc-c++ \
    qt6-qtbase-devel qt6-qtmultimedia-devel \
    onnxruntime-devel kiss-fft-devel ffmpeg
```

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build        # unit tests
./build/gui/tanara            # GUI
./build/cli/tanara-cli        # CLI
```

Build without the GUI (faster core/CLI iteration): `-DTANARA_BUILD_GUI=OFF`.
Build without speaker recognition (no ONNX/KISS FFT needed): `-DTANARA_BUILD_VOICEID=OFF`
— the app still records, transcribes and summarizes; it just skips voice fingerprinting.

### Build on Windows (MinGW)

Uses the MinGW toolchain bundled with Qt (no MSVC kit required). Prerequisites:
Qt 6 (mingw_64), the Qt-bundled MinGW + Ninja + CMake, and **FFmpeg** on `PATH`
(only `ffmpeg.exe` is needed at runtime; a static build works).

```powershell
# adjust the Qt path to your install
$qt = 'C:\Qt\6.11.1\mingw_64'
$env:PATH = "$qt\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;$env:PATH"

# 1) core + CLI + GUI without voice-ID (fast bring-up)
cmake -S . -B build -G Ninja "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release `
      -DTANARA_BUILD_VOICEID=OFF
cmake --build build

# 2) full build with voice-ID — point at an unpacked ONNX Runtime win-x64 release:
#    https://github.com/microsoft/onnxruntime/releases  (e.g. onnxruntime-win-x64-1.20.1)
#    KISS FFT is vendored under third_party/kissfft (no system package needed).
cmake -S . -B build -G Ninja "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release `
      -DTANARA_BUILD_VOICEID=ON "-DONNXRUNTIME_ROOT_DIR=C:\path\to\onnxruntime-win-x64-1.20.1"
cmake --build build
ctest --test-dir build
```

**Standalone package** (self-contained folder users can run without Qt on `PATH`):

```powershell
mkdir dist; copy build\gui\tanara.exe dist; copy build\cli\tanara-cli.exe dist
& "$qt\bin\windeployqt.exe" --release --compiler-runtime --no-translations --dir dist dist\tanara.exe
copy C:\path\to\onnxruntime-win-x64-1.20.1\lib\onnxruntime.dll dist   # only for voice-ID builds
copy C:\path\to\ffmpeg.exe dist                                       # so recording is self-contained
```

The speaker model goes in `%USERPROFILE%\.tanara\models\` (same file as on Linux; see below).

## Speaker-recognition model

The voice-ID feature needs a speaker-embedding model (not bundled, ~27 MB,
Apache-2.0). Download it once into `~/.tanara/models/`:

```bash
mkdir -p ~/.tanara/models
curl -L -o ~/.tanara/models/campplus_sv_zh_en_16k.onnx \
  "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx"
```

Speaker embeddings are language-independent (they model the voice, not the words),
so this model works fine for Hungarian. If the model is missing, Tanara still works —
it just skips automatic speaker labeling.

## Configuration

Open **Settings** in the GUI to set:
- recordings / notes / metadata folders, your own speaker name;
- automatic recording (record all devices, drop the silent ones afterwards);
- Soniox API key + base URL;
- LLM endpoint, model, temperature and max tokens.

## Usage

**GUI:** start a recording (compact floating controller available), then Transcribe
and Summarize the selected meeting. Rename speakers in the transcript — that also
*teaches* the voice model. The **Tracks** tab lets you restore or permanently delete
auto-dropped silent tracks. The **People** dialog manages names and voiceprints
(listen, merge, delete).

**CLI** (`tanara-cli`):

```
devices                         list capture devices
record [--title T --seconds N --device IDX]
list                            list meetings
transcribe <meetingId>
summarize  <meetingId>
rename <meetingId> <rawLabel> <name>     # maps + enrolls a voiceprint
identify <meetingId>            # auto-label speakers from the voiceprint DB
voiceprints                     # list enrolled people / prints
```

## Logging / debugging

Both the GUI and the CLI use a single cross-platform logger (Qt logging framework).
Messages go to **stderr** and to a rotating **file log**:

```
~/.tanara/logs/tanara.log         all messages (filtered by level)
~/.tanara/logs/tanara-error.log   warnings + errors only — always written
```

`warning`/`error` are recorded regardless of level (so problems are captured even
without debug). The level only controls `info`/`debug` verbosity.

Flags (GUI and CLI):

```
--debug                 verbose; alias for --log-level=debug
--log-level <lvl>       error | warning | info (default) | debug
--log-dir <dir>         override the log directory
--no-log-file           stderr only, no file
--log-rules <rules>     pass-through QLoggingCategory filter rules
```

Environment: `TANARA_LOG_LEVEL=debug` (the flag overrides it).

In **debug** mode the app dumps startup diagnostics: resolved paths, loaded
settings, selected STT/LLM providers (**API keys are never logged**, only their
presence) and the audio capture devices it sees.

On Linux, when launched from a `.desktop` entry, stderr is captured by the systemd
journal — `journalctl --user -t tanara`. On Windows the file log is authoritative
(the GUI has no console) and messages are also mirrored to `OutputDebugString`.

## Privacy

Audio, transcripts, summaries, the people list and voiceprints all stay on your
machine. The only network calls are the (optional) Soniox transcription request and
your own LLM endpoint. FFmpeg runs locally; the speaker-embedding model runs on-device.

## License

Tanara is released under the **MIT License** (see [`LICENSE`](LICENSE)).
Third-party components keep their own licenses — see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

A RemedIT Hungary Kft. project.

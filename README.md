# Tanara

**Tanara** is a local-first meeting recorder, transcriber, and summarizer for the
desktop. It records each audio device on a **separate track**. It transcribes the
audio with good Hungarian accuracy. It recognizes speakers by voice and writes a
structured summary. All output is plain files next to the audio.

> Tanara is a self-hosted alternative to cloud meeting assistants. Those
> assistants do not run on Linux, do not capture your own microphone reliably,
> and send all data to the cloud.

- **Privacy:** recording, diarization, speaker recognition, and summarization run
  locally. Only the optional speech-to-text call goes to a cloud API.
- **Multi-track capture:** each device (your microphone and the system loopback
  audio) becomes a separate Opus track, plus a mixed `mixdown.mp3`. The recording
  always keeps your own voice.
- **Speaker recognition:** an on-device voice-embedding model labels recurring
  speakers across meetings. You can listen to the samples and correct the labels.
- **Meeting watcher:** a tray app detects an active call and offers to record it
  with one click. Detection also runs locally.
- **Open formats:** the transcript and the summary are Markdown files beside the audio.
- **Bilingual UI:** Hungarian and English. The default follows the system locale.

Status: **Tanara works on Linux and Windows.** On Windows, Tanara captures system
audio with WASAPI loopback (playback devices appear as "loopback" capture
sources). The speaker-recognition stack (KISS FFT + ONNX Runtime) is validated on
Windows, and `windeployqt` produces a standalone build. Call detection currently
works on Linux only (PipeWire). Tanara does not target macOS yet.

---

## How it works

```
watch (tray app, PipeWire call detector)  →  notification  →  tanara --record
record (miniaudio, per device)  →  track_*.ogg + mixdown.mp3
   → transcribe (Soniox, per track, Hungarian)        →  transcript.md / .tokens.json / .segments.json
   → speaker recognition (CAM++ ONNX embedding + cosine)  →  labels recurring voices
   → summarize (local LLM, OpenAI-compatible)         →  summary.md
```

The build produces four targets:

| Target | What it is |
|---|---|
| `tanara_core` | UI-independent core library (no Qt Widgets) — audio, stores, STT, LLM, speaker recognition, call detection |
| `tanara` | Qt Widgets GUI — the main analyzer window, plus a floating-recorder mode (`tanara --record`) |
| `tanara-cli` | headless CLI on top of the same core |
| `tanara-watcher` | lightweight tray app — watches for active calls and starts `tanara --record` |

- **Architecture:** the GUI, the CLI, and the watcher all sit on `tanara_core`.
  The linker boundary enforces the split between UI and backend. A QML front-end
  can reuse the same core.
- Each meeting gets one folder under your recordings directory. The folder
  contains `meeting.json`, `track_*.ogg`, `mixdown.mp3`, `transcript.md`,
  `transcript.tokens.json`, `transcript.segments.json`, and `summary.md`.
- App data lives in `~/.tanara/`: `settings.json`, `index.db` (a cache that
  Tanara can rebuild), `people.json`, `voiceprints.json`, `secrets.json`, and
  `models/`.
- Set `TANARA_HOME=<dir>` to use a different app-data directory. Tanara then
  reads and writes nothing under `~/.tanara`, and a new `settings.json` puts
  recordings and notes under `<dir>` too. Use it to test on a copy of your data.

## Requirements

- **C++20**, **CMake ≥ 3.21**, **Ninja**
- **Qt 6** (Core, Network, Sql, Widgets, Multimedia, Test)
- **ONNX Runtime** (dev package) — for the speaker-embedding model
- **KISS FFT** (float build) — used by the bundled kaldi-native-fbank
- **FFmpeg** CLI — Tanara calls it as an external program to encode and decode audio
- A **Soniox** API key for transcription, and an **OpenAI-compatible LLM
  endpoint** (for example LM Studio or Ollama) for summaries. Both are optional.
  You configure them in the app.

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
./build/watcher/tanara-watcher  # tray watcher
```

Build options:

- `-DTANARA_BUILD_GUI=OFF` — no GUI (faster core/CLI iteration).
- `-DTANARA_BUILD_WATCHER=OFF` — no tray watcher.
- `-DTANARA_BUILD_VOICEID=OFF` — no speaker recognition, so ONNX Runtime and
  KISS FFT are not needed. The app still records, transcribes, and summarizes.
  It only skips speaker recognition.
- `-DTANARA_BUILD_CLOUD=OFF` — no Tanara Cloud client (sign-in, cloud
  providers, estimate, balance). Nothing cloud-related is registered.
- `-DTANARA_CLOUD_TEASER=OFF` — no "coming soon" waitlist panel and no hint
  line next to "⚙ Settings…".

### Build on Windows (MinGW)

The build uses the MinGW toolchain that comes with Qt. You do not need an MSVC
kit. Prerequisites: Qt 6 (mingw_64), the Qt-bundled MinGW, Ninja, CMake, and
**FFmpeg** on `PATH`. At runtime only `ffmpeg.exe` is needed, and a static build
works.

```powershell
# adjust the Qt path to your install
$qt = 'C:\Qt\6.11.1\mingw_64'
$env:PATH = "$qt\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;$env:PATH"

# 1) core + CLI + GUI without speaker recognition (fast bring-up)
cmake -S . -B build -G Ninja "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release `
      -DTANARA_BUILD_VOICEID=OFF
cmake --build build

# 2) full build with speaker recognition — point at an unpacked ONNX Runtime win-x64 release:
#    https://github.com/microsoft/onnxruntime/releases  (e.g. onnxruntime-win-x64-1.20.1)
#    KISS FFT is vendored under third_party/kissfft (no system package needed).
cmake -S . -B build -G Ninja "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release `
      -DTANARA_BUILD_VOICEID=ON "-DONNXRUNTIME_ROOT_DIR=C:\path\to\onnxruntime-win-x64-1.20.1"
cmake --build build
ctest --test-dir build
```

**Standalone package** — a self-contained folder that users can run without Qt on `PATH`:

```powershell
mkdir dist; copy build\gui\tanara.exe dist; copy build\cli\tanara-cli.exe dist
& "$qt\bin\windeployqt.exe" --release --compiler-runtime --no-translations --dir dist dist\tanara.exe
copy C:\path\to\onnxruntime-win-x64-1.20.1\lib\onnxruntime.dll dist   # only for voice-ID builds
copy C:\path\to\ffmpeg.exe dist                                       # so recording is self-contained
```

Put the speaker-embedding model in `%USERPROFILE%\.tanara\models\`. It is the
same file as on Linux (see below).

## Speaker-embedding model

Speaker recognition needs a speaker-embedding model. The model is not bundled
(~27 MB, Apache-2.0). Download it once into `~/.tanara/models/`:

```bash
mkdir -p ~/.tanara/models
curl -L -o ~/.tanara/models/campplus_sv_zh_en_16k.onnx \
  "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx"
```

Speaker embeddings are language-independent. They model the voice, not the
words, so this model works for Hungarian. If the model is missing, Tanara still
works. It only skips the automatic speaker labels.

## Configuration

Open **Settings** in the GUI to set:

- the UI language (system / Magyar / English) — a change applies at the next start
- the folders for recordings, notes, and metadata, plus your own speaker name
- automatic recording (record all devices, then drop the silent tracks)
- the Soniox API key and base URL
- the LLM endpoint, model, temperature, and max tokens
- the summary language — free text, independent from the UI language

The language of the transcript follows the meeting audio. The summary language
is a setting: type any target language, and the LLM writes the summary in it.

### Prompt tuning (without a rebuild)

The built-in LLM prompts have a file override. Put your version in
`~/.tanara/prompts/<id>.md`, where `<id>` is `simple`, `topic`, `analysis`, or
`reduce`. The app reads the file at use time, so a change needs no rebuild and
no restart. The `{{NYELV}}` placeholder in a prompt receives the summary
language. A prompt saved in Settings has priority over the file.

### Translations (for contributors)

The source language of the code is Hungarian. The English translation lives in
`i18n/tanara_en.ts`. After you change UI strings, run
`cmake --build build --target update_translations` to refresh the file, then
fill the new entries (Qt Linguist or a text editor). The build compiles the
`.ts` file and embeds it into each executable.

## Usage

### GUI (`tanara`)

Start a recording (a compact floating controller is available). Then select the
meeting and use Transcribe and Summarize. When you rename a speaker in the
transcript, Tanara also enrolls that voiceprint. On the **Tracks** tab you can
restore or permanently delete the dropped silent tracks. The **People** dialog
manages names and voiceprints (listen, merge, delete).

The Summary tab has two modes:

- **Quick summary** — one LLM pass over the transcript.
- **Complex summary** — the LLM first extracts topics, then analyzes each topic
  in its own job, then merges the results. Tanara persists the analysis, so you
  can stop it and continue later.

### Recorder mode (`tanara --record`)

`tanara --record` opens only the floating recorder, without the main window, and
starts the recording at once. The watcher uses this mode, but you can also start
it yourself. Options:

```
--no-start          open the recorder, do not start the recording
--title <T>         meeting title
--context <C>       meeting context note
--device <IDX>      record only this device (repeatable)
```

A lock file (`~/.tanara/recording.lock`) makes sure that only one recording runs
at a time. Tanara detects and removes a stale lock (dead process) automatically.

### Meeting watcher (`tanara-watcher`)

The watcher is a tray app that contains no audio code. At a set interval it asks
a call detector if a call is active. When a call starts, it shows a notification
and offers two actions:

- **Start recording now** — launches `tanara --record` with the detected title
  and context.
- **Open recorder** — launches `tanara --record --no-start`, so you can check
  the devices first.

The watcher offers each call only once (it re-arms when the call ends). On
Linux, the detector reads the PipeWire graph with `pw-dump`. An app that
captures the microphone counts as a call when its name matches the known
call-app list (Zoom, Teams, Webex, Slack, Discord, Meet, and more). You can edit
the list, the poll interval, and autostart on the **Watcher** tab in Settings.

### CLI (`tanara-cli`)

```
devices                         list capture devices
record [--title T --seconds N --device IDX]
list                            list meetings
reindex                         rebuild the index from the meeting folders on disk
transcribe <meetingId>
summarize  <meetingId>
rename <meetingId> <rawLabel> <name>     # maps + enrolls a voiceprint
identify <meetingId>            # auto-label speakers from the voiceprint DB
participants <meetingId>        # local speaker guesses, before transcription
voiceprints                     # list enrolled people / prints
detect [--watch] [--interval N] # run the call detector (the watcher engine)
embed-probe <model> <audio> <startMs> <endMs>   # dump one voice embedding (diagnostics)
cloud status|login|logout|use|tier|lang|models|estimate|accept-terms|pending|waitlist
```

### Tanara Cloud (optional, paid service)

Tanara Cloud is an optional hosted service for transcription and summaries
without your own API keys. Your own keys (BYO) stay free and work as before.

- Before the launch, Settings → Tanara Cloud shows a "coming soon" panel. You
  can join the waitlist there. The app sends data only when you click the
  button. There are no pop-ups and no telemetry.
- After the launch (`"cloudEnabled": true` in `settings.json`, or
  `TANARA_CLOUD=live`), the same place shows the sign-in (device code in the
  browser), the balance, and the default tier (Fast / Accurate). Before each
  cloud job, the app shows the cost estimate from the gateway. The app never
  computes prices.
- `TANARA_CLOUD_URL=<url>` sets the gateway address. `TANARA_CLOUD=off` turns
  everything off at run time.
- The contract is `docs/cloud-gateway-api.yaml` (a copy, do not edit). The mock
  gateway for development and tests is `tests/mock-gateway/mock-gateway.mjs`:

```bash
node tests/mock-gateway/mock-gateway.mjs --port 8300 --auto-approve --llm off
# error states: --maintenance --rate-limited --suspended admin --terms-required
#               --min-client 9.9.9 --refund --fail-chat-after 2 --balance 0.01 …
# at run time:  curl -X POST localhost:8300/__mock/config -d '{"maintenance":true}'
```

## Logging / debugging

The GUI and the CLI use one cross-platform logger (the Qt logging framework).
Messages go to **stderr** and to a rotating **file log**:

```
~/.tanara/logs/tanara.log         all messages (filtered by level)
~/.tanara/logs/tanara-error.log   warnings + errors only — always written
```

Tanara always writes `warning` and `error` messages, at each log level. The log
level only controls the `info` and `debug` verbosity.

Flags (GUI and CLI):

```
--debug                 verbose; alias for --log-level=debug
--log-level <lvl>       error | warning | info (default) | debug
--log-dir <dir>         override the log directory
--no-log-file           stderr only, no file
--log-rules <rules>     pass-through QLoggingCategory filter rules
```

Environment: `TANARA_LOG_LEVEL=debug` (the flag overrides it).

In **debug** mode the app writes startup diagnostics: the resolved paths, the
loaded settings, the selected STT and LLM providers, and the audio capture
devices it sees. The app never logs API keys, only their presence.

On Linux, when you start Tanara from a `.desktop` entry, the systemd journal
captures stderr. Read it with `journalctl --user -t tanara`. On Windows, use the
file log, because the GUI has no console. Tanara also mirrors the messages to
`OutputDebugString`.

## Privacy

The audio, the transcripts, the summaries, the people list, and the voiceprints
stay on your machine. The only network calls are the optional Soniox
transcription request and your own LLM endpoint. FFmpeg runs locally, and the
speaker-embedding model runs on-device. In Tanara Cloud mode, the audio file
(transcription) and the transcript text (summary) pass through the Tanara Cloud
gateway to the provider. The gateway does not store them.

## License

Tanara uses the **MIT License** (see [`LICENSE`](LICENSE)). Third-party
components keep their own licenses — see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

A RemedIT Hungary Kft. project.

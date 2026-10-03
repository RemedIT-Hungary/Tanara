# Slice contract — main window (wave 2)

Three slices are built in parallel in separate worktrees and merged afterwards. This file
is the interface between them. Do not change it unilaterally; if something here cannot
work, implement the closest thing and say so in your report.

## File ownership

| Slice | Owns (QML) | Owns (C++ in `gui/qml/src/`, tests in `tests/ui/`) |
|---|---|---|
| **Shell** | `Main.qml`, `LibrarySidebar.qml`, `MeetingHeader.qml`, `TaskStrip.qml`, `PlayerBar.qml`, `EmptyLibraryView.qml`, `NoSelectionView.qml`, new files prefixed `Shell*` / `Library*` | `PlayerController`, `Shell*`, `Library*`; plus `gui/src/` (bridge, `main.cpp`) |
| **Editor** | `TranscriptTab.qml`, new files prefixed `Transcript*` / `Speaker*` / `Person*` | `Transcript*`, `Speaker*`, `Person*` |
| **Views** | `PreTranscriptView.qml`, `SummaryTab.qml`, `TracksTab.qml`, new files prefixed `PreTranscript*` / `Job*` / `Summary*` / `Topic*` / `Track*` / `Waveform*` | same prefixes |

Nobody edits: `core/` (ask for a core change in your report instead, unless it is a small
additive fix you need and you say exactly what you changed), the `T*` controls and
`Theme.qml` (same rule), `i18n/tanara_en.ts`, any CMake file, another slice's files.
New shared control needed? Create it under your own prefix.

## Inputs every content component receives

`TranscriptTab`, `SummaryTab`, `TracksTab` and `PreTranscriptView` are instantiated by
`Main.qml` with these properties. Declare them exactly like this; all must tolerate
empty / null values, because `--qml-shot` renders a component standalone.

```qml
property string meetingId: ""      // selected meeting; "" = none
property var player: null          // PlayerController (below) or null
property var shell: null           // ShellActions (below) or null
```

A component standing alone with `App.demo === true` (or `meetingId === ""` in a
screenshot) shows built-in **fictional** sample content from its own view-model, so it
can be rendered with `--qml-shot`. Never ship real names or real transcript text as
fixtures.

`PreTranscriptView` is shown instead of the tabs whenever the meeting has no transcript:
it covers M03 (stepper), M04 (transcription running) and M05's error card (failed).

## `PlayerController` (Shell implements; C++, `QML_ELEMENT`)

One instance, owned by `Main.qml`, plays the selected meeting's mixdown.

| Member | Meaning |
|---|---|
| `string meetingId` (rw) | setting it loads that meeting's mixdown and stops playback |
| `bool available` (r) | a playable audio file exists |
| `bool playing` (r) | |
| `int positionMs`, `int durationMs` (r) | `positionMs` updates at least 10×/s while playing |
| `real rate` (rw) | 1.0, 1.25, 1.5, 2.0 … |
| `real volume` (rw) | 0…1 |
| `play()`, `pause()`, `toggle()` | |
| `seek(int ms)` | does not change the playing state |
| `playRange(int startMs, int endMs)` | plays one utterance, then pauses |
| `playFile(string absolutePath)` | previews another file (a single track); `previewPath` (r) is that path while it plays, `""` otherwise; `stopPreview()` returns to the mixdown |

## `ShellActions` (Shell implements; exposed as the `shell` property)

Everything that needs a Widgets dialog, a cloud estimate / login / error flow, or a
change of what the window shows goes through here. Content components never call
`AppController::transcribeMeeting` / `summarizeMeeting` / `extractMeetingTopics` /
`generateComplexSummary` / `retranscribeMeeting` directly: the shell wraps them with the
readiness check and the Tanara Cloud estimate confirmation that the old `MainWindow` does.

| Invokable | Meaning |
|---|---|
| `openSettings(string page)` | page: `""`, `"providers"`, `"watcher"`, `"cloud"`, `"summary"` |
| `openPeople()` | People management dialog |
| `openRecorder()` | floating recorder |
| `startTranscription(string meetingId)` | readiness + cloud estimate, then transcribe |
| `retranscribe(string meetingId)` | opens the M10 confirmation dialog (Shell owns it) |
| `startQuickSummary(string meetingId)` | |
| `startTopicExtraction(string meetingId)` | topic-based summary, round 1 |
| `startTopicAnalysis(string meetingId)` | analyse missing topics + reduce (topics are already persisted via `AppController::setMeetingTopics`) |
| `analyzeTopic(string meetingId, string topicId)` | (re)run one topic |
| `identifyParticipants(string meetingId)` | async identification with the task strip |
| `cancelJob(string meetingId, int jobKind)` | `tanara::JobKind` value |
| `revealInFolder(string meetingId)` | open the meeting folder |
| `pickAudioFile()` → string | native file dialog; `""` if cancelled |
| `pickAudioFiles()` → list<string> | native multi-file dialog (audio and video files, for import); `[]` if cancelled |
| `openImport(list files = [])` | "Import audio file…": without files the native picker opens first (cancelling it does nothing); with files (e.g. dropped on the window) the import dialog opens right away; while an import is running, its progress dialog is shown instead |
| `confirm(string title, string text, string confirmLabel, bool danger)` → bool | modal confirmation in the M10 style |
| `showMeeting(string meetingId)`, `showTab(int index)` | navigation (0 transcript, 1 summary, 2 tracks) |
| `seekTo(string meetingId, int ms)` | select meeting if needed, seek the player, show the transcript at that time |
| `toast(string text)` | short non-modal notice |

Signal `transcriptPositionRequested(int ms)`: the Editor scrolls to the utterance at `ms`.

## Core API to build on (all reachable from `AppContext::instance()->controller()`)

- Speaker editing: `AppController::speakerEditor(meetingId)` → `tanara::SpeakerEditor`
  (`core/include/tanara/edit/*.h`), `peopleDirectory()`, `summaryStale()`,
  `retranscribeImpact()`, `dismissSummaryStale()`.
- Jobs and state: `jobs()` → `MeetingJobTracker`, `processingState(id)`, `cancelJob`,
  `identifyMeetingAsync` (`core/include/tanara/jobs/*.h`).
- Library: `library()` → `MeetingLibrary` (`core/include/tanara/library/*.h`).
- Tracks and waveforms: `tracks()`, `waveforms()`, `requestWaveforms(id)`
  (`core/include/tanara/audio/TrackCatalog.h`, `WaveformService.h`).
- Summary: `summaryDocument(id)`, `meetingTopics(id)`, `setMeetingTopics`, `topicStatuses(id)`,
  `topicAnalyses(id)` (`core/include/tanara/summary/SummaryStore.h`).
- Readiness: `canRun(step, meetingId)` (`provider/ReadinessModel.h`).

## Known limits to design around

- Transcription has **no percentage** from the provider: only mixdown and upload report
  one. Show an indeterminate bar for the transcribe stage; show an ETA only when
  `JobProgress::estimatedTotalSec > 0`.
- `ActionItem` has no done/checked state: render the M07 checkboxes as non-interactive
  bullets unless you add persistence yourself and report it.
- Utterances shorter than 1.5 s are never "uncertain" and never suggested.
- Screenshot mode uses the software renderer: no `MultiEffect` / `layer.effect` /
  `ShaderEffect`. Use `TShadow`, `TIcon`, plain items, or `QQuickPaintedItem`.
- `Theme.textOnAccent` / `Theme.textOnSpeaker` replace the spec's `onAccent` / `onSpeaker`.
  For the transcript line height use `TLabel.cssLineHeight`.

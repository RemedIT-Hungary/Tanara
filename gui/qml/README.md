# Tanara QML module (`import Tanara`)

The Qt Quick front end of Tanara: design system (theme, icons, `T*` controls), the
application shell (`Main.qml`) and the C++ view-models that feed it. The spec is
`design/handoff/README.md`; rendered targets are in `design/handoff/renders/`.

The process is still a `QApplication`: Settings, People and the cloud dialogs remain Qt
Widgets and open next to the QML window. The floating recorder is QML in both the main
window and `tanara --record` (only `--classic` keeps the old Widgets recorder).

## Layout

| Path | Content | Picked up by |
|---|---|---|
| `gui/qml/*.qml` | QML types, **flat**; file name = type name | glob |
| `gui/qml/src/*.h`, `*.cpp` | C++ types of the module (view-models, `App`, image provider) | glob |
| `gui/qml/icons/*.svg` | Lucide icons (ISC) → `:/qt/qml/Tanara/icons/` | glob |
| `gui/qml/fonts/*.ttf` | IBM Plex Sans / Mono (OFL) → `:/qt/qml/Tanara/fonts/` | glob |
| `tests/ui/*.cpp` | Qt Test, one executable per file, links `tanara_qml` | glob |
| `gui/src/` | Widgets UI + `main.cpp` (may include module headers) | glob |

**Adding a file never needs a CMake edit.** The globs use `CONFIGURE_DEPENDS`, so the
next `cmake --build build` re-configures by itself. Targets: `tanara_qml` (static
library with the C++ and the compiled QML), `tanara_qmlplugin` (linked into `tanara`).

A QML file whose first lines contain `pragma Singleton` is registered as a singleton
automatically (re-run cmake if you add the pragma to an existing file).

## Run

```bash
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure

build/gui/tanara                      # new QML window (uses the real AppController / user data)
build/gui/tanara --meeting <id>       # same, with that meeting selected; handed over to a running main window
build/gui/tanara --classic            # the old Widgets MainWindow, unchanged
build/gui/tanara --record …           # the QML floating recorder (see "Recorder"); add --classic for the old Widgets one
build/gui/tanara --gallery            # control gallery, interactive — no AppController
build/gui/tanara --demo               # Main.qml with App.demo = true — no AppController
build/gui/tanara --theme dark         # light | dark | system (default); env: TANARA_THEME
```

`--gallery`, `--demo` and `--qml-shot` never construct an `AppController` and never open a
log file, so they do not touch `~/.tanara` or `~/Tanara`.

## Screenshots without a window

```bash
build/gui/tanara --qml-shot out.png --qml-page Gallery --theme dark --size 1280x2900
build/gui/tanara --qml-shot out.png --qml-page Main --theme light --size 1280x820 \
    --qml-prop 'shellState="empty"'
build/gui/tanara --qml-shot out.png --qml-page SummaryTab --size 1004x640 --scale 2
build/gui/tanara --qml-shot out.png --qml-page Gallery --qml-prop 'overlay="dialog"'

gui/qml/shoot.sh                      # gallery + all Main states, both themes → build/shots/
gui/qml/shoot.sh TranscriptTab:1004x640 'Main:1280x820:shellState="noSelection"'
```

| Switch | Meaning |
|---|---|
| `--qml-shot <png>` | render and exit (forces `QT_QPA_PLATFORM=offscreen` + the software renderer) |
| `--qml-page <Type>` | any QML type of the module; default `Main`. Non-window roots are wrapped in an `ApplicationWindow` with `Theme.bg` |
| `--size WxH` | logical size, default `1280x820` |
| `--scale <f>` | device pixel ratio (1.5, 2 …) — output is `W·f × H·f` pixels |
| `--theme light\|dark` | theme |
| `--qml-prop name=value` | initial property, repeatable; value is JSON (`true`, `3`, `"text"`) or plain text |
| `--delay <ms>` | wait before grabbing (default 300) — raise it for long transitions |

The log line reports the number of QML warnings; treat anything above 0 as a bug.
`shoot.sh` takes `Type[:WxH[:name=value,…]]` specs and always renders both themes.

The software renderer is used on purpose: the module uses **no shader effects**, so the
PNG matches the live window. Keep it that way — no `MultiEffect`, `layer.effect`,
`ShaderEffect` or `OpacityMask`. Shadows come from `TShadow`, icon tinting from `TIcon`,
dashed outlines from `TDashedRect`, stripes from `Shape` / `Canvas`.

Compare against the design: `design/handoff/renders/main-{light,dark}--m01…m10-*.png`,
`editor--3a-*.png`, `visual-language--nyomat-*.png` (regenerate with
`node design/handoff/renders/render.mjs`; needs `google-chrome` and network).

## Conventions

- **Names.** Design-system controls are prefixed `T` (`TButton`) so they never collide
  with Qt Quick Controls. Screens and view parts have plain names (`SummaryTab`). C++
  view-models end in `ViewModel` / `Model`. One type per file.
- **Imports.** Files of the module see each other, `Theme` and `App` without importing
  `Tanara`. Controls are built on `QtQuick.Templates as T`; do not import
  `QtQuick.Controls` for controls (only `Main.qml` does, for `ApplicationWindow`).
- **Text.** Use `TLabel` (or a control), not raw `Text`, so the family and colour are
  right. User-visible strings are `qsTr("…")` with **Hungarian source text**; English goes
  to `i18n/tanara_en.ts` (`cmake --build build --target update_translations`, then fill in
  the new `<translation>` entries). `Gallery.qml` sample text is intentionally not translated.
- **Line height.** `TLabel.cssLineHeight: 1.55` gives CSS-like line height (a multiple of
  the pixel size). Qt's own `lineHeight` multiplies the font's natural spacing and comes
  out too loose.
- **Theme.** Every colour, size and radius comes from `Theme` (`Theme.accent`,
  `Theme.space4`, `Theme.radiusControl`, `Theme.speakerInk(i)` …). No hex literals in
  screens. Never write `Theme.dark`; set `App.themeMode` (`"system" | "light" | "dark"`).
  Two tokens are renamed versus the spec: `onAccent` → `Theme.textOnAccent`,
  `onSpeaker` → `Theme.textOnSpeaker` (a QML property called `on…` is parsed as a signal
  handler and silently evaluates to black).
- **Icons.** `TIcon { name: "search"; size: 15; color: Theme.textMuted }`. Names are the
  Lucide file names in `gui/qml/icons/`. To add one: `gui/qml/fetch-assets.sh <name>` and
  commit the SVG. `TIcon` renders at `size × devicePixelRatio`, so it stays crisp at 125 % /
  150 %. An unknown name logs a warning and renders nothing.
- **Popups** use `popupType: Popup.Item` (already set in `TMenu`, `TPopover`, `TDialog`,
  `TToolTip`) so they appear in screenshots and inside the window overlay.
- **States in the gallery.** Controls expose `stateHovered` / `stateFocused` (default bound
  to the real `hovered` / `visualFocus`) so `Gallery.qml` can show them statically; set
  `down: true` for pressed. Add every new reusable control to `Gallery.qml`.
- **Keyboard.** All controls take focus by Tab, show a 3 px ring on keyboard focus and
  activate with Space / Return. Give every `TIconButton` and `TStatusIcon` a `toolTipText`
  (it is also the accessible name).

## Controls

| Type | Main properties |
|---|---|
| `TButton` | `text`, `variant`: `primary` \| `secondary` (default) \| `ghost` \| `danger` (solid) \| `dangerSoft` \| `dangerGhost` \| `record`; `size`: `normal` (34) \| `small` (28); `iconName`, `trailingIconName`, `muted`, `horizontalAlignment`, `radius`, `toolTipText` |
| `TIconButton` | `iconName`, `iconSize`, `variant`: `outline` \| `flat` \| `solid`; `size`; `checkable`/`checked`; `radius`, `toolTipText` |
| `TTextField` | `text`, `placeholderText`, `hasError`, `mono` |
| `TSearchField` | `text`, `placeholderText`, `hint` (e.g. `"Ctrl+F"`); Esc clears |
| `TTextArea` | `text`, `placeholderText`, `hasError` |
| `TSwitch`, `TCheckBox` | `text`, `checked` |
| `TPill` | `text`, `tone`: `warn` \| `success` \| `danger` \| `accent` \| `neutral` |
| `TChip` | `text`, `speakerIndex` (person chip), `removable` + `removed()`, `dashed` ("add"), `checkable`/`checked` (filter), `tone`, `iconName` |
| `TAvatar` | `name` (→ monogram) or `monogram`, `speakerIndex`, `variant`: `soft` \| `solid`, `size` |
| `TStatusIcon` | `kind`: `transcript` \| `summary` \| `identified`; `state`: `done` \| `running` \| `error` \| `stale` \| `missing`; `toolTipText` |
| `TTabBar` + `TTabButton` | `currentIndex`; per tab `text`, `pillText`, `pillTone` |
| `TMenu` + `TMenuItem` + `TMenuSeparator` | item: `text`, `iconName`, `shortcutText`, `danger`, `checked`, `onTriggered` |
| `TPopover` | anchored panel; set `width`, `x`/`y`; content as children |
| `TDialog` | modal with scrim (M10 pattern): `title`, children (column, 14 px gap), `actions: [TButton…]`; `accept()` / `reject()` |
| `TToolTip` | `text`, `visible` (controls create their own from `toolTipText`) |
| `TProgressBar` | `value`, `thickness` (4 \| 6), `indeterminate`, `trackColor` |
| `TBanner` | `tone`: `warn` \| `accent` \| `danger`; `title`, `text`, `iconName`; children = action buttons |
| `TCard` | `tone`: `default` \| `raised` \| `accent` \| `danger`; `padding`, `radius` |
| `TSurface` | raised panel with border + shadow (`elevated`), the popup background |
| `TLabel`, `TSectionLabel` | `text`, `mono`, `muted`, `cssLineHeight` |
| `TIcon`, `TSpinner` | `name`, `size`, `color`, `strokeWidth` |
| `TScrollBar` | `T.ScrollBar.vertical: TScrollBar {}` |
| `TSplitHandle` | `SplitView { handle: TSplitHandle {} }` |
| `TDivider`, `TDashedRect`, `TShadow`, `TFocusRing` | building blocks |
| `TPlaceholder` | dashed box for not-yet-built regions |

## Shell (`Main.qml`)

`ApplicationWindow` 1280×820 (minimum 960×600): menu row (Fájl / Nézet, native window
decorations kept) · 276 px sidebar · content · 52 px player. `Main.qml` owns the view-models
`ShellActions` (the `shell` of `CONTRACT.md`: navigation + gated actions), `PlayerController`,
`ShellMeetingModel` (header, stale flag, task strip) and `ShellUiState` (window size, selection,
theme, player volume in `<TANARA_HOME or ~/.tanara>/ui-state.json`). What shows is computed:

- library empty → `EmptyLibraryView` · nothing selected → `NoSelectionView` · meeting without
  transcript → `MeetingHeader` + `PreTranscriptView` + `PlayerBar` · otherwise `MeetingHeader` +
  tabs (`TranscriptTab`, `SummaryTab`, `TracksTab`) + `PlayerBar`. `showTab(2)` before a
  transcript shows `TracksTab` with a back button instead of `PreTranscriptView`.
- Content components get `meetingId` / `player` / `shell` as plain property bindings in `Main.qml`.

Keyboard: `Ctrl+F` library search · `Ctrl+Shift+F` search in the transcript · `Ctrl+1/2/3` tabs ·
`F2` rename · `Space` play / pause (never while a text field or a keyboard-focused control has
focus). With the focus in the transcript: arrows, `Enter` (play from the line), `1`–`9` (move the
selection), `B` / `Shift+B` (next / previous uncertain line), `Ctrl+C` (copy the selected lines),
`Ctrl+A`, `Ctrl+L` (rail), `Ctrl+Z` / `Ctrl+Shift+Z`. Right click on a line: copy / play menu.
An inline editor that hides itself must give the focus back (`releaseHiddenFocus()` pattern):
an invisible `TextInput` keeps the active focus and would swallow every key.

Overrides for `--demo` / `--qml-shot` (leave unset with a real controller):
`shellState` (`"empty" | "noSelection" | "preTranscript" | "meeting"`, `""` = computed),
`taskRunning` (sample task strip), `demoSearch` (sidebar search text), `demoOverlay`
(`retranscribe | delete | close | stop | confirm | toast | toastError | cloudToast | filters |
rename | tracks`).

Widgets side (`gui/src/`): `QmlShellBridge` implements `tanara_qml::ShellBridge` (Settings,
People, file picker, all Tanara Cloud dialogs and chrome); `ShellRecorderHost` is the only
place that knows the recorder (see "Recorder in the main window" below); `MediaPlayerBackend`
is the Qt Multimedia engine behind `PlayerController` (the QML module itself does not link
Multimedia).

`tanara --meeting <id>` selects that meeting at startup. If a QML main window is already
running, the second process hands the request over through a local socket and exits
(`gui/src/AnalyzerSingleton.h`, name scoped by `TANARA_HOME` like the recorder's); the running
window selects the meeting and comes to the front (`ShellBridge::showMeetingRequested` →
`ShellActions.showMeeting` + `activateWindow`). `--classic` ignores the argument.

QA without touching the desktop: `TANARA_HOME=<sandbox>/home QT_QPA_PLATFORM=offscreen
build/gui/tanara --shell-script script.qml` loads `script.qml` next to the real window with
`window` (`window.shell`, `.player`, `.library`, `.meetingModel`) and `hook` (`grab(path)`,
`log(text)`, `quit(code)`, `resize(w, h)`, `widgets()` / `clickButton()` / `fillLineEdit()` /
`closeWidget()` for the Widgets dialogs, `clipboardText()`); see `gui/src/ShellQaHook.h`. In this
mode the app rebuilds the meeting index from disk. It listens on the recorder / analyzer sockets
only when `TANARA_HOME` is set (the socket names are then scoped to that folder, so a real instance
is never disturbed) — a second `tanara --record …` / `tanara --meeting <id>` with the same
`TANARA_HOME` drives the window under test. The script reaches the recorder window through
`App.bridge.recorderWindow()` (`import Tanara`; `.visible`, `.sheetOpen`, `.vm.state` …).

Build trap: after adding a C++ file to `gui/qml/src/`, AUTOMOC may not re-run (link errors
about `staticMetaObject` / vtable): delete `build/gui/qml/tanara_qml_autogen/timestamp`.

## C++ view-models

1. Add `gui/qml/src/FooViewModel.h/.cpp` in namespace `tanara_qml`, a `QObject` with
   `QML_ELEMENT` (or `QML_SINGLETON`) and `#include <QtQml/qqmlregistration.h>`. It is
   then usable in QML as `FooViewModel { id: vm }` — no registration call, no CMake edit.
2. Reach the core through the `App` singleton:

   ```cpp
   #include "AppContext.h"
   #include "tanara/AppController.h"

   tanara::AppController* c = tanara_qml::AppContext::instance()->controller();
   if (!c) { /* --demo, --gallery, --qml-shot, tests: no controller */ }
   ```

   `controller()` is **null** in demo / gallery / screenshot mode and in tests. A view-model
   must work without it; when `AppContext::instance()->demo()` is true it should serve
   built-in **fictional** sample data, so its screen can be rendered with `--qml-shot`
   and `--demo` without touching user data. Prefer taking the controller through a
   settable property / constructor argument so tests can inject one.
3. From QML: `App.controller` (as `QObject`), `App.demo`, `App.dark`, `App.themeMode`,
   `App.bridge`.
4. Test it in `tests/ui/test_foo_view_model.cpp` (`QTEST_MAIN`, see
   `tests/ui/test_app_context.cpp`). Tests run with `QT_QPA_PLATFORM=offscreen`. Never
   point a test at `~/.tanara`; use `QTemporaryDir`.

## Widgets dialogs from QML

`App.bridge` is a `QObject*` slot for the object that opens the Widgets dialogs
(Settings, People, recorder, cloud). The class belongs in `gui/src/` (it needs the Widgets
classes; that directory is globbed too) and is installed in `gui/src/main.cpp`:
`tanara_qml::AppContext::instance()->setBridge(bridge);`. QML then calls its
`Q_INVOKABLE`s: `App.bridge.openSettings()`. `tests/ui/test_qml_smoke.cpp` verifies that a
`QDialog` can be shown next to the QML window.

## Recorder (`Recorder*.qml`, `VuMeter.qml`, `src/Recorder*`)

Spec: `design/handoff-recorder/README.md` (states R01–R11).

| Piece | Role |
|---|---|
| `RecorderView.qml` | the whole recorder as an **item** (title bar, title field, start / status + stop, source lines, device list, R06 box, R07 sheet, R09, R10); 380 px wide, height follows content |
| `RecorderPill.qml` | pill mode (R05) |
| `RecorderWindow.qml` | frameless always-on-top `Window` hosting the two; created by the host |
| `RecorderPreview.qml` | screenshot wrapper with fictional devices: `--qml-page RecorderPreview --size 420x640 --qml-prop 'demoState="R04"'` (`R01`…`R10`) |
| `VuMeter.qml`, `RecorderSwitch.qml`, `RecorderButton.qml` | 14-segment meter with peak hold, 30×18 switch with lock, the recorder's buttons |
| `RecorderViewModel` | state, title, device model (`devices`: name / rawName / group / selected / locked / appName / level / peak / status…), `start()`, `stop()`, `toggleDevice(row)`; without a controller it serves fictional data (`demoState`) |
| `RecorderWindowHost` | C++ host: shows the window, executes `--record` requests, remembers position, snaps the pill, hide-to-tray, `recording.lock` |

### Recorder in the main window

`gui/src/ShellRecorderHost` wraps one `RecorderWindowHost` for the QML main window (the host
creates **its own QML engine**, as in the standalone process; the theme still follows because
`App` is one process-wide object shared by both engines — `tests/ui/test_recorder_view_model.cpp`
`hostWithOwnEngineFollowsTheme`). Who does what:

| Event | Handled by |
|---|---|
| "Új felvétel", Ctrl+N, "Felvétel folyamatban" | `ShellActions.openRecorder` → `ShellRecorderHost::open` (a hidden recorder left in "done" is reset to idle) |
| forwarded `tanara --record …` (title / app / context / devices / start / stop) | `RecorderSingleton` → `ShellRecorderHost` → `RecorderWindowHost::request` |
| `recording.lock`, context note, rename while recording | the recorder (`setManageLock(true)`, `RecorderViewModel`) |
| automatic mixdown after the recording | `AppController` — stays ON in the main application; only the standalone `--record` process turns it off |
| new meeting appears and is selected, "Felvétel kész" toast | `ShellActions` on `AppController::recordingFinished` (the host's `recordingFinished` is not handled a second time) |
| "Megnyitás az elemzőben" (R09) | `openMeetingRequested` → `QmlShellBridge::showMeeting` |
| "Rögzítés beállításai" (R10) | `settingsRequested` → Settings on the `recording` page |
| "Vége a megbeszélésnek?" | only the recorder's box (R06); the bridge never emits `stopPromptRequested`, so the shell dialog does not open as well |
| closing the **recorder** while recording | the recorder's sheet (R07) |
| closing the **main window** while recording | the shell dialog (background / stop and quit / cancel) |
| recorder hidden ("Háttérbe") or closed while the main window is hidden | the main window comes back (`showWindowRequested`) |
| level monitoring | runs while the recorder is visible or a recording runs; released when it is closed, or when a recording ends with the recorder hidden |

### Using the recorder host directly

```cpp
#include "RecorderWindowHost.h"

auto* recorder = new tanara_qml::RecorderWindowHost(&controller, &engine /* or nullptr */, parent);
recorder->show();                                   // "Felvétel" button
recorder->request({.title = t, .appName = app, .context = ctx, .deviceIndexes = {}, .start = true});
// forwarded `tanara --record …` (RecorderSingleton::requestReceived → parseRecorderArgs → request)
connect(recorder, &RecorderWindowHost::openMeetingRequested, …);   // "Megnyitás az elemzőben"
connect(recorder, &RecorderWindowHost::settingsRequested, …);      // "Rögzítés beállításai"
connect(recorder, &RecorderWindowHost::hiddenToTray, …);           // window hidden, recording goes on
connect(recorder, &RecorderWindowHost::notificationRequested, …);  // R06 while hidden / pill
connect(recorder, &RecorderWindowHost::closed, …);                 // closed while not recording
```

`request({.stop = true})` stops a running recording without showing the window (tray menu).
The host never stops or closes a recording by itself: closing while recording opens the R07
sheet. `setHideToTrayEnabled(false)` makes "Háttérbe" minimise instead (no tray to come back
from). Window state lives in `<metadata dir>/recorder.ini` (never in the shared `QSettings`).

Platform notes: always-on-top, remembered position and pill edge-snapping need a platform
where the client may position its window (X11, Windows). On Wayland the compositor decides;
the pin button is disabled with an explanation. `TANARA_RECORDER_X11=1 tanara --record` runs
the standalone recorder through XWayland where all three work.

## Known gaps

- Custom title bar is not implemented (native decorations are kept, as the spec allows);
  the centred "Tanara" caption is therefore omitted.
- `TDialog` / `TMenu` are in-window popups (`Popup.Item`); they cannot extend beyond the
  window.
- Windows packaging (`windeployqt --qmldir gui/qml`) has not been updated or tested.

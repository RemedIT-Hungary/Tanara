# Tanara QML module (`import Tanara`)

The Qt Quick front end of Tanara: design system (theme, icons, `T*` controls), the
application shell (`Main.qml`) and the C++ view-models that feed it. The spec is
`design/handoff/README.md`; rendered targets are in `design/handoff/renders/`.

The process is still a `QApplication`: the Tanara Cloud dialogs (sign-in, estimate
confirmation, errors, top-up, terms, mode choice, Expert model picker), the native file / folder
pickers and the tray icon are Qt Widgets and open next to the QML windows. Everything else is
QML: the main window, Settings (`SettingsWindow.qml`, see "Settings window"), People
(`PeopleWindow.qml`, see "People window") and the floating recorder, both in the main window
and in `tanara --record`. The old Widgets main window (`tanara --classic`) is gone.

## Layout

| Path | Content | Picked up by |
|---|---|---|
| `gui/qml/*.qml` | QML types, **flat**; file name = type name | glob |
| `gui/qml/src/*.h`, `*.cpp` | C++ types of the module (view-models, `App`, image provider) | glob |
| `gui/qml/icons/*.svg` | Lucide icons (ISC) → `:/qt/qml/Tanara/icons/` | glob |
| `gui/qml/fonts/*.ttf` | IBM Plex Sans / Mono (OFL) → `:/qt/qml/Tanara/fonts/` | glob |
| `tests/ui/*.cpp` | Qt Test, one executable per file, links `tanara_qml` | glob |
| `gui/src/` | `main.cpp`, the bridge to the Widgets world and the remaining Widgets dialogs (`cloud/`); may include module headers | glob |

**Build gotcha.** A new `QObject` header under `core/include/` in an *existing* build directory is not
picked up by the core's AUTOMOC cache: delete the core autogen cache
(`build/core/tanara_core_autogen` and `build/core/CMakeFiles/tanara_core_autogen.dir`) and re-run
`cmake -S . -B build`, otherwise the link fails with undefined `vtable` / `staticMetaObject` symbols.
A fresh build directory is always fine.

**Adding a file never needs a CMake edit.** The globs use `CONFIGURE_DEPENDS`, so the
next `cmake --build build` re-configures by itself. Targets: `tanara_qml` (static
library with the C++ and the compiled QML), `tanara_qmlplugin` (linked into `tanara`).

A QML file whose first lines contain `pragma Singleton` is registered as a singleton
automatically (re-run cmake if you add the pragma to an existing file).

## Run

```bash
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure

build/gui/tanara                      # the main window (uses the real AppController / user data)
build/gui/tanara --meeting <id>       # same, with that meeting selected; handed over to a running main window
build/gui/tanara --record …           # the floating recorder (see "Recorder")
build/gui/tanara --settings [page]    # only the Settings window (general | recording | watcher | services | speakers | summary; the old names providers / cloud / voiceModels still map);
                                      # handed over to a running main window, otherwise a stand-alone process
build/gui/tanara --gallery            # control gallery, interactive — no AppController
build/gui/tanara --demo               # Main.qml with App.demo = true — no AppController
build/gui/tanara --theme dark         # light | dark | system (default); env: TANARA_THEME
```

`--gallery`, `--demo` and `--qml-shot` never construct an `AppController` and never open a
log file, so they do not touch `~/.tanara` or `~/Tanara`.

## Screenshots without a window

```bash
build/gui/tanara --qml-shot out.png --qml-page Gallery --theme dark --size 1280x6400
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
`SHOT_DELAY=<ms>` passes `--delay` to every shot; without it `shoot.sh` uses the default, except for
`TranscriptTab` (2500 ms: its demo meeting loads asynchronously). The v3 recipes are in "Demo states
(screenshots)" under "The v3 meeting view".

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
| `EvidenceChip` | `kind`: `voice` \| `side` \| `calendar` \| `tag` \| `lines` \| `similarity` \| `manual`; `polarity`: `support` \| `neutral` \| `contradict`; `side` (`mic` \| `loopback`), `quiet`, `text` |
| `EvidenceReason` | `evidence` (an `evidenceMap()` object: `kind`, `polarity`, `label`, `sentence`, `fixLabel`), `fixEnabled`; `fixClicked(evidence)` |
| `CandidateRow` | `personName`, `colorIndex` (< 0 = outside the meeting), `subText`, `evidence`, `keyHint`, `dimmed`, `highlighted`, `strongEvidence`; `clicked()` |
| `TimeChip` | `text`, `active`, `warn`, `iconName`, `large`, `toolTipText`; `stateHovered` / `stateFocused` |
| `TranscriptMarker` | `kind`: `confirmed` \| `short` \| `uncertain` \| `corrected` \| `newPerson` \| `noisy`; `reason` (`side` \| `voice` \| `tag`) for `uncertain` |
| `TagRowOverview` | `model` (a `MeetingTagsModel`), `maxVisibleSuggestions`; `tagClicked(id)` — tags, inline input and suggestions in one field |
| `StatTiles` | `stats`: `[{ label, value, sub }]`, 2 columns |
| `PipelineSteps` | `steps`: `[{ key, title, sub, state, parallel, progress }]`, `state`: `done` \| `running` \| `attention` \| `waiting`; `progress` 0–100, `-2` = indeterminate, `-1` = none |
| `MeetingToolsRow` | the 46 px per-tab tool-row slot under the header (children go in; empty = hidden) |
| `MapDock`, `SourceCard`, `MemoToc` | see "The v3 meeting view" |

## Shell (`Main.qml`)

`ApplicationWindow` 1280×820 (minimum 960×600): menu row (Fájl / Megbeszélés / Nézet, native window
decorations kept) · 276 px sidebar · content · 52 px player. `Main.qml` owns the view-models
`ShellActions` (the `shell` of `CONTRACT.md`: navigation + gated actions), `PlayerController`,
`ShellMeetingModel` (header, stale flag, task strip) and `ShellUiState` (window size, selection,
theme, player volume in `<TANARA_HOME or ~/.tanara>/ui-state.json`). What shows is computed:

- library empty → `EmptyLibraryView` · nothing selected → `NoSelectionView` · meeting without
  transcript → `MeetingHeader` + the Áttekintés page showing `PreTranscriptView` (until the
  transcription starts; then the Áttekintés itself) + `PlayerBar` · otherwise `MeetingHeader` +
  four tabs (0 Áttekintés `OverviewTab`, 1 Átirat `TranscriptTab`, 2 Vezetői összefoglaló and
  3 Memó `SummaryTab`) + the map dock or `PlayerBar`. Details: "The v3 meeting view" below.
- Content components get `meetingId` / `player` / `shell` as plain property bindings in `Main.qml`.

Keyboard: `Ctrl+F` library search · `Ctrl+Shift+F` search in the transcript · `Ctrl+1/2/3/4` tabs
(Áttekintés / Átirat / Vezetői összefoglaló / Memó) · `Ctrl+E` Olvasás / Javítás · `F2` rename · `Space` play / pause (never while a text field or a keyboard-focused control has
focus). With the focus in the transcript: arrows, `Enter` (play from the line), `1`–`9` (move the
selection), `B` / `Shift+B` (next / previous uncertain line), `Ctrl+C` (copy the selected lines),
`Ctrl+A`, `Ctrl+L` (rail), `Ctrl+E` (Olvasás / Javítás), `Ctrl+Z` / `Ctrl+Shift+Z`. Right click on a line: copy / play menu.
An inline editor that hides itself must give the focus back (`releaseHiddenFocus()` pattern):
an invisible `TextInput` keeps the active focus and would swallow every key.

Overrides for `--demo` / `--qml-shot` (leave unset with a real controller; `demoState` below):
`shellState` (`"empty" | "noSelection" | "preTranscript" | "meeting"`, `""` = computed),
`taskRunning` (sample task strip), `demoSearch` (sidebar search text), `demoOverlay`
(`retranscribe | delete | close | stop | confirm | toast | toastError | cloudToast | filters |
rename | tracks | import | importSplit | importProbing | importError | importEmpty | importFailed |
importProgress | importStrip | drop`; also `tagToast`, `selection`; `tracks` opens the
"Sávok részletei" dialog on the Áttekintés), `demoTags` (header tag-model states), and `demoState`
(the v3 states: `overviewProcessing | overviewDone | overviewEmpty | overviewNobody | readingMap |
tabTooltip | summarySources | memoSections`).

### The v3 meeting view (design: `design/handoff-v3/`)

Spec and decision log: `design/handoff-v3/README.md` (V1–V6 shell, E1–E4 editor, S1–S3 summary).
What the shell is made of, top to bottom:

| Part | File | What it does |
|---|---|---|
| Menu row | `Main.qml` | **Fájl · Megbeszélés · Nézet** (Alt+F / Alt+M / Alt+N). `Megbeszélés` is `MeetingMenu` |
| Header, **one 56 px line** | `MeetingHeader.qml` | title 19/600 + pencil (F2 / double click renames in place) · separator · four tabs · `…` button (the same `MeetingMenu`). No meta line, no tag row, no "identify" button any more |
| Tool row, 46 px | `MeetingToolsRow.qml` | a slot under the header whose content changes per tab: Átirat → `TranscriptToolbar` (lives in the editor); Vezetői összefoglaló / Memó → `SummaryToolsRow`; Áttekintés → none (the row hides itself when empty) |
| Content | `Main.qml` `StackLayout` | one page per tab (see below) |
| Bottom | `MapDock.qml` or `PlayerBar.qml` | tabs Átirat / Vezetői összefoglaló / Memó **with a transcript**: the map dock; Áttekintés (and no lane data): the plain player bar. `Main.mapDockShown` |

**Tabs and indices** (`ShellActions.currentTab`, `Main.currentTab`, `Main.showTab(i)`, `MeetingHeader.currentTab`):

| Index | Tab | Shortcut | Content |
|---|---|---|---|
| 0 | Áttekintés | `Ctrl+1` | `OverviewTab` (before the transcript is started: `PreTranscriptView` in the same page) |
| 1 | Átirat | `Ctrl+2` | `TranscriptTab` |
| 2 | Vezetői összefoglaló | `Ctrl+3` | `SummaryTab` with `section = "exec"` |
| 3 | Memó | `Ctrl+4` | the **same** `SummaryTab` with `section = "memo"` (`Main.syncSummarySection()`); the `StackLayout` index is `min(currentTab, 2)` |

A tab that is not available yet is dimmed: Átirat needs a transcript (or a running transcription,
the tab then shows `62%`), the two summary tabs need a summary (`MeetingHeader.tabAvailable`).
Hover or click opens a panel that says what is missing and offers the action (`demoState="tabTooltip"`).
A running step shows as a pill on its tab; a stale summary as a warn pill on the summary tab.
The old **Sávok** tab is gone: its full view is the "Sávok részletei" dialog of the Áttekintés
(`OverviewTab.openTracks()`, `objectName: "tracksDialog"`, it hosts `TracksTab`).

`MeetingMenu` (`Megbeszélés`): Átnevezés (F2) · Ki volt ott? · Résztvevők azonosítása (hang alapján) ·
Újraellenőrzés… · Újra-átírás… · Exportálás archívumba… · Megnyitás mappában · — · Törlés….
The "identify" entry is the old voice-identification entry point; the Ki volt ott? dialog is the
new one (see below). `Nézet` lists the four tabs, then **Javítás / Olvasás** (Ctrl+E) and
**Beszélő-oszlopok** (Ctrl+L).

#### Áttekintés (`OverviewTab`, `src/OverviewViewModel`)

Two columns. **Left**: `MIRŐL SZÓL` (`SourceCard`: the manual description, calendar is a placeholder) ·
the tag field `TagRowOverview` (tags + suggestions with source label and "Miért?") · `KIK VOLTAK OTT`
(`ParticipantList` with source badges and speech share; empty: "+ Résztvevő" / "Csak én beszéltem";
everybody unknown: the raw speakers + "Elnevezem őket") · the approval banner `approvalBanner`
("A hanglenyomat-elemzés kész: N jelölt", `Átnézem` opens the dialog, `Később` hides it for the session).
**Right** (340 px): `FELDOLGOZÁS` (`PipelineSteps`) and `SÁVOK` (`TrackList`); when all is done:
`ADATOK` (`StatTiles`, 2×3) and the steps / tracks collapsed to one row each (`stepsCollapsed`, `tracksCollapsed`).

#### The map dock (`MapDock`)

One 13 px row per speaker (name 104 px in the speaker's ink · 8 px lane · speech share), an accent
play line, the visible part of the list as a faint band, the 44 px control row below. A click on the
lane seeks; a click on the name opens the speaker menu (`speakerClicked`); the fingerprint mark at
the end of the name column opens the voiceprint panel (`voiceprintClicked`). Inputs: `lanes`
(`TranscriptEditorViewModel.overview`), `viewportStart` / `viewportSize` (only on the Átirat tab),
`collapsedCount` / `lanesExpanded` ("Egyéb (N)"), `timelineMs`, and the two extra-row lists
**`sourceMarks`** (row "Forrás": accent = the active statement, accentLine = the others; the speaker
lanes dim to 45 %) and **`sectionBands`** (row "Szakaszok", memo sections as bands, the active one
accent). Items are `{ startMs, endMs, active }`; `Main` takes them from `SummaryTab.sourceMarks` /
`.sectionBands` on tabs 2 / 3. `SummaryMapRows` is the same two rows as a stand-alone component.

#### "Ki volt ott?" (`ParticipantsDialog`, `src/ParticipantsViewModel`)

An 820 px dialog that approves the voice analysis' participant candidates. Groups (an empty one is
hidden): Biztos · Kétséges · Meghívott, de nem hallottuk · added in the dialog; a name field
(`PersonPicker`) with tag-based suggestions; footer: the info about unbound raw speakers, `Kihagyás`
(`skip()`), `Tovább · N résztvevő` (`approve()`). While the analysis runs it shows a spinner; Esc
closes it without a decision ("later"). `Main.qml` hosts it (`participantsDialog`); it opens itself
on `ShellActions.participantsDialogRequested`.

`ShellActions` entry points: `openParticipants(meetingId)` (banner, tool row, Megbeszélés menu, editor),
`maybeOfferParticipants(meetingId)` (called by `Main.showTab(1)`: opens the dialog once per session when
an approval is pending; returns `true` if it opened), `participantsGuess(meetingId)` (the pre-transcript
voice guess), `identifyParticipants(meetingId)` and `recheckSpeakers(meetingId)` (the menu entries).

#### Átirat: Olvasás and Javítás (`TranscriptTab`)

`TranscriptTab.fixMode` (Ctrl+E, the segment in `TranscriptToolbar`, `Nézet › Javítás`): **Olvasás** is
the plain list; **Javítás** switches the speaker rail on (`onFixModeChanged` sets
`editorVm.railVisible`) and shows the per-line markers and the "Átnézendő N" chip. There is no
permanent undo button: `Ctrl+Z` and the `TranscriptChangeBar` at the bottom (Visszavonás).
`B` / `Shift+B` step over the lines to review (over the groups in the Átnézendő view).

| Type | Role |
|---|---|
| `LinePopover` (E1, 390 px, `objectName: "linePopover"`) | "Kinek a sora ez?": scope (this line / selection / the whole speaker), JAVASOLT candidates (`CandidateRow` + `EvidenceChip`s, keys 1–3), "Miért nem X?" (`EvidenceReason` rows, each with the place to fix it), "Új személy ebből a sorból…". `TranscriptTab.openLinePopover(row, anchor)` / `openLinePopoverFor(utteranceId, anchor)` |
| `SpeakerWhyPopover` (E3, 430 px, `"speakerWhyPopover"`) | "Miért ő?" for a whole speaker: reasons, which track he speaks on (the manual side assignment, the one truth), "Nem ő? valójában…", new person, merge, pair review, "Ő nem volt ott". `openSpeakerWhyAt(key, x, y, focusTracks)` |
| `ReviewGroupsView` (E2, `"reviewView"`) + `ReviewGroupCard` + `ContaminatedCoreBanner` | the "Átnézendő" view: doubtful lines grouped by reason, one decision (and one undo step) per group: "Mind a N → X", "Egyenként", "Kihagyom"; the contaminated-core strip on top ("Szétválasztás", "Miért?") |
| `TranscriptMarker` | per-line mark in Javítás mode: `confirmed`, `short`, `uncertain` (`reason`: side / voice / tag), `corrected`, `newPerson`, `noisy` |
| `EvidenceChip` | 20 px chip: `kind` voice / side (`side`: mic / loopback) / calendar / tag / lines / similarity / manual; `polarity` support / neutral / contradict (dashed outline, muted text, × / ⚠ icon); `quiet` |
| `EvidenceReason` | one reason row (`evidence` = `TranscriptEditorViewModel.evidenceMap()`), `fixClicked(evidence)` |
| `CandidateRow` | a person candidate with avatar, sub line, evidence chips and the 1–3 key hint; `dimmed` for the other-track and current speaker |
| `TranscriptChangeBar` | after a reassignment; after a **new person** (E4) three lines: what happened, "Még N sor hangja hasonlít…" + "Mind a N hozzá", voiceprint readiness |

#### Vezetői összefoglaló and Memó (`SummaryTab`, `src/SummaryViewModel`)

`SummaryTab.section` (`"exec"` | `"memo"`, set by the shell) picks the form; `sourcesLayout` is true for a
summary with statements (`vm.hasStatements`), an old summary keeps `SummaryDocView`. In the shell
`embedToolsRow`, `embedMapRows` and `showSectionSwitch` are `false`: the tool row and the dock own them.

| Type | Role |
|---|---|
| `SummaryExecView` (S1/S3) | the sentences as one flowing paragraph, a `TimeChip` after each; decisions and to-dos with an owner chip; the right column (250 px): participants with speech share, note, remaining actions. Stale summary (S3): a banner counts the affected statements, the affected sentence is underlined (`warnLine`), the affected source chip is `warn`. `popover` is the `SourcePopover` |
| `SourcePopover` (S1, 440 px) | "Honnan jön ez?": the quotes of one statement with "Meghallgatom", "Ugrás az átiratba", "Nem így hangzott el? · Jelzem". `openFor(statementId, anchor)` |
| `SummaryMemoView` (S2) + `MemoToc` | memo sections: table of contents (236 px, `picked(index)`) and the sections in time order with a range `TimeChip` (`large`, `iconName: "play"`); the active section follows the scrolling |
| `SummaryToolsRow` | generation meta · **Források** switch · Másolás · Újragenerálás |
| `SummaryMapRows` | the "Forrás" / "Szakaszok" rows of the dock as a component |
| `TimeChip` | monospace time chip: default accentSoft, `active` (selected statement), `warn` (the speaker changed in its source), `iconName`, `large` |

#### Demo states (screenshots)

`--qml-prop` values are JSON, so strings carry their quotes. `shoot.sh` renders both themes. The
**TranscriptTab** states build their fictional meeting asynchronously: use `--delay 2500` (`shoot.sh`
does that by itself for `TranscriptTab`; override with `SHOT_DELAY=<ms>`). Names in the samples:
Kovács Lilla, Fehér Gábor, Varga Árpád, #Nordvik.

| Screen | Spec (`gui/qml/shoot.sh …`) | `demoState` values |
|---|---|---|
| Whole shell | `'Main:1280x820:demoState="overviewDone"'` | `overviewProcessing` (V1) · `overviewDone` (V3) · `overviewEmpty` (V5) · `overviewNobody` (V6) · `readingMap` (V4, Átirat + dock) · `tabTooltip` (V4 panel) · `summarySources` (S1, tab 2) · `memoSections` (S2, tab 3) |
| Tracks dialog | `'Main:1280x820:demoOverlay="tracks"'` | (`demoOverlay`, not `demoState`) |
| Áttekintés alone | `OverviewTab:1100x720:demoState="overviewDone"` | `overviewProcessing` · `overviewDone` · `overviewEmpty` · `overviewNobody` |
| Map dock alone | `MapDock:1100x200` | (invented lanes) |
| Header alone | `MeetingHeader:1100x420:demoState="tabTooltip"` | `tabTooltip` |
| Ki volt ott? | `ParticipantsDialog:1000x760:demoState="doubts"` | `allSure` · `doubts` · `invitedNotHeard` · `addPerson` · `running` · `noTranscriptYet` |
| Átirat (Javítás) | `TranscriptTab:1100x760:demoState="fixLinePopover"` | `markers` · `fixLinePopover` (E1) · `reviewGroups` · `reviewExpanded` (E2) · `contaminatedCore` · `speakerWhy` (E3) · `newPersonSimilar` (E4) — plus the older ones listed in the header of `TranscriptTab.qml` |
| Összefoglaló | `SummaryTab:1100x720:demoState="sourcesOn"` | `sourcesOn` · `sourcesOff` · `sourcePopover` · `memoSections` · `staleTargeted` · `noSources` — plus the older `stale`, `done`, `memo`, `running`, `empty*`, `topics` … |

A `Main` demo state needs `App.demo` (set by `--qml-shot`): the shell then picks the sample meeting
`demo-partner`, and `Main.demoState` forwards the value to the content components.


### Tags in the shell

Spec: `design/handoff-tags/README.md` (C03/C04, C07), contract `CONTRACT-TAGS.md`. The view-models
reach the core through `TagBackend` → `TagControllerBackend` (`createControllerTagBackend`).

- **Áttekintés** (v3: the header has no tag row any more; `OverviewTab` → `TagRowOverview`, the
  "Címkék" field of the left column): the model is `ShellMeetingModel.tags` (a `MeetingTagsModel` bound to the selected meeting). Suggestions are
  requested when a transcribed meeting is shown and when its transcript arrives; co-occurring
  ones after a manual add; LLM ones arrive by themselves after a summary. A chip click →
  `shell.filterByTag(id)` → `tagFilterRequested` → `Main.filterLibraryByTag` (sets `tags` on the
  library model when it has that property). "Miért?" links → `shell.showMeeting`.
- **Before transcript** the Áttekintés page shows `PreTranscriptView`; its step 1 of `PreTranscriptView` shows "Címkék"
  (`TagField` on `PreTranscriptViewModel.tags`) and the suggestion line (`tagSuggestions`,
  `tagSuggestionReason`: the meeting's normal suggestions, else `draftTagSuggestions(title)`). The
  note cards show the source meeting's tags (`MeetingNoteModel.suggestions[].tags`, mini chips).
- **Undo**: every tag step toasts through `shell.toast(text, "tags")` (ShellToast shows
  "Visszavonás" → `shell.undoFromToast` → `undoRequested("tags")`). `Ctrl+Z` undoes the last tag
  step unless the focus is in the transcript editor or a text field (`Main.tagUndoActive`;
  `TranscriptTab.undoAllowed`). All tag models share the one `TagService` undo stack.
- **Tags window**: `shell.openTags(tagId)` → `ShellBridge::openTagsAt` → `TagsWindowHost`
  ("Megnyitás a könyvtárban szűrőként" → `ShellBridge::tagFilterRequested`; meeting links →
  `showMeetingRequested`). QA: `App.bridge.tagsWindow()`.
- Screenshots: `Main` with `demoTags` (`none | few | many | computing | similar | cooccur | llm |
  why`) and `demoOverlay="tagToast"`; `MeetingHeader:1004x120:demoState="cooccur"` (T03),
  `"llm"` (T06), `MeetingHeader:1004x520:demoState="why"` (T02, panel open);
  `PreTranscriptView:1004x640:demoState="note"` (T07).

### Audio file import

"Fájl → Hangfájl importálása…" (`Ctrl+I`), the icon button next to "Új felvétel", the empty-library
screen and dropping files on the window all end in `ShellActions.openImport(files)` →
`ShellImportDialog` (a 560 px `TDialog`) fed by `ShellImportModel`: files with their probed data,
per-file "split channels" switch, title / date, optional "my microphone" track, the resulting
track count in words; then progress with cancel. "Háttérben folytatom" closes the dialog while
the import goes on — `ShellImportStrip` in the sidebar shows it and reopens the dialog. On
success `ShellActions` selects the new meeting (pre-transcript view); transcription never starts
by itself. The work is `tanara::AudioImporter` behind `AppController::importAudio`
(`core/include/tanara/import/AudioImporter.h`); the job runs as `JobKind::Import` under the id of
the meeting-to-be. QA scripts reach it through `window.importModel` / `window.importDialog`
(`addFiles([...])`, `setSplit(row, on)`, `ownTrack`, `start()`, `cancel()`) — the native file
picker is only opened by `openImport()` without arguments, so pass the paths.

Tags (C07, T09): a "Címkék" `TagField` under Cím / Mikor készült plus a suggestion line
("Javasolt: [+ chip] hasonló cím: „…”"). `ShellImportModel.addTag(name)` creates the tag in the
set right away (`TagService::create`); `suggestions` come from
`AppController::draftTagSuggestions(title)`, recomputed 300 ms after the title changes;
`acceptSuggestion(i)` / `dismissSuggestion(i)` (the meeting does not exist yet, so a dismissal
only lasts for this dialog). When `importFinished` arrives the chosen ids go onto the new
meeting (`TagService::setTags`).

### Meeting archive (export / import)

"…" menu in `MeetingHeader` → "Exportálás archívumba…" calls `ShellActions.exportArchive(id)`:
`ShellBridge::pickSaveFile` (proposed `<folder>.tanara.zip` in the last used folder or `~/Tanara`),
then `AppController::exportMeetingArchive` on a worker thread. The progress is a
`JobKind::Export` task in the `TaskStrip` (cancellable); `archiveFinished` gives a toast with
"Megnyitás mappában" (`ShellToast.revealPath` → `ShellActions.revealFile`). "Fájl → Megbeszélés
importálása archívumból…", the empty-library row and a dropped `*.zip` call
`ShellActions.importArchive(pathOrUrl)` (`ShellBridge::pickArchiveFile` when empty) →
`AppController::importMeetingArchive` → on success the new meeting is selected; errors come as
a danger toast. Main.qml splits dropped URLs with `ShellActions.isArchiveFile`: archives go to
the archive import, everything else to `openImport`. Format and checks: `core/include/tanara/store/MeetingArchive.h`.

Widgets side (`gui/src/`): `QmlShellBridge` implements `tanara_qml::ShellBridge` (file
pickers, all Tanara Cloud dialogs and chrome; it owns the `PeopleWindowHost` and the `SettingsWindowHost` and re-evaluates
readiness, the recorder's device policy and the cloud chrome when Settings saves); `ShellRecorderHost` is the only
place that knows the recorder (see "Recorder in the main window" below); `MediaPlayerBackend`
is the Qt Multimedia engine behind `PlayerController` (the QML module itself does not link
Multimedia).

`tanara --meeting <id>` selects that meeting at startup. If a QML main window is already
running, the second process hands the request over through a local socket and exits
(`gui/src/AnalyzerSingleton.h`, name scoped by `TANARA_HOME` like the recorder's); the running
window selects the meeting and comes to the front (`ShellBridge::showMeetingRequested` →
`ShellActions.showMeeting` + `activateWindow`).

QA without touching the desktop: `TANARA_HOME=<sandbox>/home QT_QPA_PLATFORM=offscreen
build/gui/tanara --shell-script script.qml` loads `script.qml` next to the real window with
`window` (`window.shell`, `.player`, `.library`, `.meetingModel`) and `hook` (`grab(path)`, `grabWindow(win, path)`,
`log(text)`, `quit(code)`, `resize(w, h)`, `widgets()` / `clickButton()` / `fillLineEdit()` /
`closeWidget()` for the Widgets dialogs, `clipboardText()`); see `gui/src/ShellQaHook.h`. In this
mode the app rebuilds the meeting index from disk. It listens on the recorder / analyzer sockets
only when `TANARA_HOME` is set (the socket names are then scoped to that folder, so a real instance
is never disturbed) — a second `tanara --record …` / `tanara --meeting <id>` with the same
`TANARA_HOME` drives the window under test. The script reaches the recorder window through
`App.bridge.recorderWindow()` (`import Tanara`; `.visible`, `.sheetOpen`, `.vm.state` …) and the
Settings window through `App.bridge.settingsWindow()` (`.visible`, `.vm.page`, `.vm.save()` …)
and the People window through `App.bridge.peopleWindow()` (`.visible`, `.vm`, `.mergeDialog`,
`.deleteDialog`, `.toastItem` …; grab any of them with `hook.grabWindow(win, path)` — they live
in their own engines).

Build trap: after adding a C++ file to `gui/qml/src/`, AUTOMOC may not re-run (link errors
about `staticMetaObject` / vtable): delete `build/gui/qml/tanara_qml_autogen/timestamp`.

### Tracks (`TracksTab.qml`, `TrackRow.qml`, `src/TrackListModel`)

Since v3 this is no longer a tab: the Áttekintés shows the tracks in its right column (`TrackList`),
and the full view below lives in the "Sávok részletei" dialog (`OverviewTab.openTracks()`).

One row per track file: play (preview), source icon, friendly name (rename in place), a second
line, the waveform, the file length, and the action (dropped → „Visszaállítás”, missing file →
„Megkeresés…”). Below: the mixdown row and the permanent delete of the dropped tracks.

- **Timeline.** A track file holds only what was captured: a device switched on later starts
  at `Track::startOffsetMs`. `waveStart` / `waveSpan` (0..1, `TrackListModel::waveExtent`)
  place the waveform on the meeting timeline: the gap before the start and after the end shows
  a thin baseline. `durationText` is the file length (until the peaks are known: from the start
  to the end of the meeting).
- **Segments.** The files of one device that started at different offsets (switched off and on
  during the recording: `track_<slug>.ogg`, `track_<slug>-2.ogg` …) are segments of one logical
  track (`tracknames::segments`): same name and role, adjacent rows ordered by offset. The
  second line reads `N. szakasz · kezdete: m:ss · <device> · <file>`. Rename applies to all
  segments; drop / restore / locate stay per file.
- **Preview** (`PlayerController::playFile`) plays the file on its own, from the file's start
  (file time, not meeting time).
- Demo (`--qml-page TracksTab --qml-prop 'demoState="idle"'`): the „Hívás hangja” row starts at
  2:30.

## Transcript editor: the scope of a speaker correction

Agreed with the owner; it deliberately departs from the designer's spec, where a click on a
line's speaker name meant the whole speaker.

- **A name click on a line is line-scoped.** `TranscriptTab.openLinePopover(row, anchor)` opens
  `SpeakerPopover` with a scope selector: "Csak ez a sor" (default) · "Kijelölt N sor" (default
  when the row is part of a multi-selection) · "<Név> minden sora (N)". The same panel opens from
  "Más mondta…" (uncertain filter, row context menu). Title and footer always state the scope
  and the count.
- **Whole-speaker operations** start at speaker-level places: the rail header avatar and the
  overview name (`openSpeakerPopover(key, anchor)`), with "Meghallgatás", merge list, the
  voiceprint checkbox and the voiceprint section (see "Voiceprints" below).
- **After every reassignment** `TranscriptChangeBar` appears below the list (view-model:
  `changeActive`, `changeText`, `changeRestCount` …): Visszavonás · "Hasonló N sor is" +
  "Megmutatom" (the similarity suggestion; there is no inline box any more) · "<Forrás> mind a N
  sora". It goes away on the next edit, on dismissal, or after ~12 s — it does not expire while
  a similarity suggestion is waiting for an answer.
- **A merge of two speakers of the meeting asks first**, with numbers (`mergeDialog`,
  `requestMerge()`); a line or a selection never asks.
- In the uncertain filter a corrected line stays in place ("javítva") until the filter is
  applied again.

### Voiceprints in the transcript editor

A voiceprint is made **only on an explicit user action**, never automatically, and only for a
named speaker with enough material (the core decides: `SpeakerEditor::voiceprintMaterial`). The
content is one component, `VoiceprintPanel.qml` (state in plain words, usable lines / seconds,
what is missing and what helps, the create button, and "Visszavonás" for the print just made).
When voice analysis is unavailable it says why once (`voiceprintMaterial().reason`: `model` /
`audio`) and shows no button. Three entry points:

- **The fingerprint mark on every overview row** (`TranscriptToolbar`, `overviewVoiceprint`):
  filled green = the person has a voiceprint, muted outline = named but none yet, faded and not
  clickable for anonymous speakers. It sits inside the name column, so the lane tracks do not
  move. A click opens `voiceprintPopover` (`TranscriptTab.openVoiceprintPopover(key, anchor)`).
  The "Egyéb (N)" row has no mark; a click on its name expands the collapsed speakers into their
  own rows ("Keveset beszélők összecsukása" folds them back). The rail-header dot carries the
  same state (green dot / hollow ring / none). View-model: `voiceprint` (`has` | `none` |
  `anonymous`) in `overview`, `lanes` and `speakers`.
- **`SpeakerPopover` in whole-speaker scope**, from a speaker-level place or from a line once
  "<Név> minden sora (N)" is chosen (compact layout of the same panel). In a low window that
  tallest variant shortens its lists (`tight`) so the block and the footer stay visible.
- **An offer in `TranscriptChangeBar`** after a whole speaker was given a person (rename,
  reassign, merge into a named person, "mind a N sora") when that person has no voiceprint and
  the meeting has enough material: `changeVoiceprintOffer` → "Hanglenyomat készítése"
  (`createVoiceprintFromChange()`). Never after a line / selection move or a similarity
  suggestion, and not when the material is insufficient.

Undo: a voiceprint is not part of the editor's undo stack. The print just made can be taken
back where it was made — "Visszavonás" in the panel (`removeVoiceprint(printId)`), or on the
bar, where after creation `undoChange()` removes exactly that print
(`SpeakerEditor::removeVoiceprint`) and leaves the rename in place (the rename is still undone
with Ctrl+Z; `changeUndoable` turns false once the print is removed).

Re-check from confirmed lines (`SpeakerEditor::recheckFromConfirmed`, view-model
`recheckSpeakers()` / `canRecheck` / `recheckBlocker`): per speaker the voice centroid is built
only from the confirmed / corrected lines (when there are ≥ 3), and every other line is judged
against these. Doubtful lines stay "bizonytalan" (persisted in the overlay as `rechecked`) until
they are corrected or confirmed; a row with a suggestion shows "<Név> mondta". Reachable from
"Résztvevők azonosítása" when everyone is named (confirm dialog, `ShellActions::recheckSpeakers`),
the header "…" menu ("Beszélők újraellenőrzése…"), the toolbar "Bizonytalan 0" button and the
empty uncertain filter.

Stored voiceprints in the re-check references (`SpeakerPrior` in `SpeakerAnalysis.h`; only
`recheckFromConfirmed` and `recheckPair` — the automatic uncertainty and the similarity
suggestion do not use them). For a named speaker, `SpeakerEditor` passes the person's prints from
`VoiceprintStore` (prints of another model dimension are skipped). Weights, in ms-equivalent: a
confirmed line weighs its duration (capped at `kMaxEmbedMs` = 10 s); a print made **in this
meeting** (`sourceMeetingId`) is local evidence of this recording's acoustics: `kLocalPrintMs`
= 10 s each, not capped, and one such print alone is a local core (it also enables the re-check);
a print from **another meeting** weighs `kPriorPrintMs` = 4 s, and all of them together at most
`kPriorMaxShare` = 1/3 of the local weight, so the meeting's own evidence always dominates. Three
reference cases per speaker (`ReferenceInfo::kind`): `Local` (confirmed lines and/or this-meeting
prints, plus the capped earlier prints), `Prior` (no local core: the earlier prints alone), and
`Fallback` (neither: all clean lines, as before). When a this-meeting print's sample line
(`sampleRef` start time) is itself a confirmed line of the speaker, that line is left out of the
reference and only the print counts. The result carries the composition
(`RecheckResult::references`, `PairRecheckResult::refA/refB`); when any print was used, the toast
appends `SpeakerEditor::referenceSummary`, e.g. "Referencia: Dompa 3 sor + 1 itteni lenyomat + 2
korábbi lenyomat, Gábor 5 sor." (view-model: `referenceSummary` in the result maps).

Pairwise review between two speakers ("Átnézés A és B között"; `SpeakerEditor::recheckPair`,
analysis `computePairRecheck`, view-model `recheckPair(a, b)`): only the lines of A and B, only
their two voices. Each reference is built from the speaker's confirmed / corrected clean lines
(≥ 3) plus the stored voiceprints (see above), else from the earlier prints alone, else from
all of its clean lines (`fallbackA/B`, said in the result toast). Every
unlocked, clean line ≥ 1.5 s on A or B is flagged when the other reference fits better by
`kPairMargin` 0.05 (0.10 under 3 s). There is no "not two people" centroid guard here — the user
said they are two people — but a reference similarity ≥ 0.60 is reported ("a két hang nagyon
hasonló (0,8x), az eredmény bizonytalan"). Same persistence as the re-check (`rechecked` /
`recheckHint`); a run replaces the marks on A's and B's lines only, and is one undo step. Entry
points: (1) after a manual line / selection move between two named speakers, when the similarity
suggestion is silent **because of the centroid guard**
(`suggestSimilarDetailed().blockedBySimilarity`) and both have ≥ 3 confirmed / corrected embedded clean lines (the moved
line counts), `TranscriptChangeBar` shows a second row: `pairOfferActive` / `pairOfferText` with
"Átnézés" (`acceptPairOffer()`) and "Most nem" (`declinePairOffer()`: not offered again for that
pair in this editor session); the bar does not expire while the offer waits. (2)
`SpeakerPopover` in whole-speaker scope of a named speaker: "Átnézés másik beszélővel…" → pick
one of `pairCandidates(key)`. The result is a toast (`notice`); flagged lines switch the
uncertain filter on.

"Egymásra beszéltek" (noisy line): automatic when another speaker overlaps the line's embedding
window by ≥ 1000 ms or ≥ 30 %, or manual ("Jó így, de nem minta" on an uncertain row;
"Mintának használható" on hover clears it). Noisy lines are left out of the voice centroids and
the voiceprint material, and the re-check never flags them. The row shows a muted pill.

Demo states for screenshots: `linePopover`, `selectionPopover`, `lineToSpeakerPopover`,
`speakerPopover`, `changeLine`, `suggestion`, `suggestionShown`, `changeSelection`,
`changeSpeaker`, `changeFilter`, `mergeConfirm`, `voiceprintHas`, `voiceprintNone`,
`voiceprintDone`, `voiceprintShort`, `changeVoiceprint`, `changeVoiceprintDone`, `recheck`
(after a re-check), `recheckReady` (nothing uncertain: the toolbar offers the re-check),
`noisy` (a line marked "egymásra beszéltek"), `changePairOffer` (the pairwise-review offer on
the bar; the offer itself is staged, the demo voices are too distinct), `speakerPopoverPair`
(the whole-speaker panel with the pair picker open), e.g.
`gui/qml/shoot.sh 'TranscriptTab:1004x640:demoState="changeLine"'`,
`'TranscriptTab:1004x640:demoVariant="many",demoState="voiceprintNone"'` (the 11-speaker
overview; `demoVariant="novoice"` shows the "no voice model" wording).

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
(Tanara Cloud, native file pickers) and the QML recorder, Settings and People windows. The class belongs in `gui/src/` (it needs the Widgets
classes; that directory is globbed too) and is installed in `gui/src/main.cpp`:
`tanara_qml::AppContext::instance()->setBridge(bridge);`. QML then calls its
`Q_INVOKABLE`s: `App.bridge.openSettings()`. `tests/ui/test_qml_smoke.cpp` verifies that a
`QDialog` can be shown next to the QML window.

## Settings window (`Settings*.qml`, `src/Settings*`)

Spec: `design/handoff-settings/README.md` (B01–B07). A separate, resizable, **non-modal** window
(900 × 680) with six pages (Általános · Rögzítés · Hívásfigyelő · Szolgáltatások · **Beszélők** · Összefoglaló); native decorations are kept, so the spec's own 36 px title bar is
not drawn.

| Piece | Role |
|---|---|
| `SettingsWindow.qml` | the window: navigation, scrolling content, footer (dirty indicator, Mégse / Mentés), the unsaved-changes / reset / logout / output-format dialogs |
| `SettingsGeneralPage` · `SettingsRecordingPage` · `SettingsWatcherPage` · `SettingsServicesPage` (+ `SettingsProviderCard`, `SettingsProviderFields`, `SettingsEmbeddingCard`, `SettingsCloudPanel`, `SettingsWaitlistPanel`) · `SettingsSummaryPage` | the pages B01–B07 (+ C09) |
| `SettingsSpeakersPage` (E01, B08) + `SettingsVoiceModelCard` | the **Beszélők** page: the "Te" section (own name and voiceprint, moved here from Általános), "Hangmodellek" (one card per speaker model, `vm.voiceModels` = `VoiceModelListModel`; enabling is part of the draft, downloading is immediate), the voiceprint backfill status. Card `state_`: `missing` \| `downloading` (`progress`, `progressText`, Mégse) \| `downloaded` \| `bundled` \| `error` (`errorText`, Újra) \| `offline`; `enabledModel` / `canEnable`, `recommended`; licence, source link, size and dimension are shown before the download. With no model at all the page offers "CAM++ letöltése (N MB)" |
| `VoiceModelOnboardingRow` | the same downloader as one row in the First steps window (Szolgáltatások step; hidden when the default model exists); `demoState: "B08none"` |
| `SettingsSegmented`, `SettingsRadio`, `SettingsSwitchRow`, `SettingsStepper`, `SettingsCombo`, `SettingsTextField`, `SettingsNavItem`, `SettingsStatusPill` | the window's controls (the device rows reuse the recorder's `RecorderSwitch`, `VuMeter`, `RecorderGroupHeader`, `RecorderDefaultPill`) |
| `SettingsViewModel` | the draft: a copy of `AppSettings` + secrets + default sources + theme. Pages write the draft; `save()` applies **only the difference** onto the core's current settings (so nothing this window does not show, or that changed meanwhile, is lost), `discard()` drops it. `dirty` / `changeCount` / `footerText`, `errors`, `servicesWarn`, `openPage(page, focusField)` |
| `SettingsDeviceModel` (`vm.devices`) | B02 device rows: switch = default source, rename, live level (`AppController::retainLevelMonitoring`, only while the page is visible) |
| `SettingsProviderModel` (`vm.stt`, `vm.llm`) | a provider card rendered from the provider registry (`ProviderDescriptor.fields`); "Kapcsolat tesztelése" and "Lekérés" through `tanara::ConnectionTester` with the *draft* address and key |
| `SettingsCloudModel` (`vm.cloud`) | Tanara Cloud account panel (everything from `CloudAccount`) and the waitlist offer |
| `SettingsEmbeddingModel` (`vm.embedding`) | C09 "Beágyazás" card: mode Nincs (alap) / Helyi végpont / Tanara Cloud in the draft (`embeddingProviderId` / `embeddingConfigs`), the local endpoint's fields and test via `card` (a `SettingsProviderModel` of kind `embedding`), KÖNYVTÁR ELŐKÉSZÍTÉSE from `EmbeddingPreparer` (running / error / done, Megszakítás / Folytatás / Újraelőkészítés), the model-change warning before saving (`restartPending`), the CÍMKEJAVASLATOK switches and the rejected-suggestion reset (immediate, confirmed) |
| `SettingsPromptHighlighter` | `QSyntaxHighlighter` on the prompt editor's document (`{{VÁLTOZÓ}}`) |
| `SettingsWindowHost` | C++ host with its own engine: `open(page, focusField)`, `saved()`, `themeModeSaved()`, `returnRequested()`, `closed()` |
| `SettingsDialogs` | what the window needs from the desktop / Widgets (folder picker, open folder / URL, People, cloud login / top-up / Expert model / terms); implemented by `gui/src/SettingsWidgetsDialogs`, faked in tests |

Where it opens: the main window (`ShellActions.openSettings(page, focusField)` →
`QmlShellBridge` → host), the stand-alone recorder (`tanara --record`, R10 "Rögzítés
beállításai" → the same window on the `recording` page, in the recorder's process) and
`tanara --settings [page]` (the tray watcher's "Beállítások…"; forwarded to a running main window,
otherwise a process of its own).

Behaviour worth knowing:

- **Apply on save.** The theme previews live and is restored by Mégse / Elvetés. Closing with
  unsaved changes asks (Mentés / Elvetés / Mégse); Mentés keeps the window open.
- **Deep link (B04).** `openSettings("providers", "stt" | "llm")` shows the info banner and
  highlights the card; after a save that makes the step runnable the window closes and the main
  window comes forward (`returnRequested`).
- **Collapsed roles (C09).** A configured role (Átírás, Összefoglaló) is a one-line card
  (title · provider · status pill); the header click expands it (`SettingsProviderModel.expanded`).
  The deep-link card and a card whose test failed are always expanded.
- **Embedding model change (C09).** Changing the embedding model shows a warning before saving,
  the footer says "Modellváltás · mentéskor újraindul az előkészítés" and the button "Mentés és
  újraindítás"; the core restarts the preparation itself on `settingsChanged`.
- **Device names.** A rename is stored in `AppSettings::deviceNames` (raw OS name → name) and
  resolved in one place, `tanara::devicenames` (`core/include/tanara/audio/TrackCatalog.h`); the
  recorder, the Tracks tab and the watcher's notification follow on `settingsChanged`.
- **Keys** stay in the `KeyStore` (`<metadata dir>/secrets.json`, mode 600); the OS keychain of the
  spec is not implemented.
- **Autostart** (`~/.config/autostart/tanara-watcher.desktop`) is written on save through
  `tanara::autostart` — never under `TANARA_HOME` or in Qt test mode.

Screenshots (fictional data, no controller):

```bash
build/gui/tanara --qml-shot out.png --qml-page SettingsWindow --size 900x780 --qml-prop 'demoState="B04"'
```

`demoState`: `B01` … `B07` · `B07notes` (the per-part notes prompt) · `advanced` (LLM card with "Haladó" open, incl. the reasoning switch) · `dirty` · `unsaved` · `schema` · `teaser` · `cloudOut` · `addApp` ·
`logout` · `reset` · C09: `B04embedding` (local, idle) · `B04embeddingRunning` (T14) · `B04embeddingError` ·
`B04embeddingDone` · `B04embeddingCloud` (T16) · `B04embeddingNone` · `B04modelChange` (T15) · `rejectedReset` ·
E01 (Beszélők page): `B08` (models on) · `B08download` (WeSpeaker downloading, 46 %) · `B08none` (nothing downloaded) · `B08error` · `B08offline` · `B08backfill` (voiceprint backfill running) · `B08partial` (backfill finished with failures) · `B08bundled` (CAM++ shipped with the app).
Example: `gui/qml/shoot.sh 'SettingsWindow:900x880:demoState="B08"'`.

## People window (`People*.qml`, `src/People*`)

Spec: `design/handoff-people/README.md` (P01–P06). A separate, resizable, **non-modal** window
(960 × 660); native decorations are kept, so the spec's 36 px title bar is not drawn. The list
pane's width is draggable (names are not elided in normal widths).

| Piece | Role |
|---|---|
| `PeopleWindow.qml` | the window: list pane, detail pane, shortcuts, undo toast, the delete / new-person dialogs |
| `PeopleListPane` + `PeopleListRow` | search (names and aliases), "Új személy", count, sort menu, list with section headers, the P06 hint and the no-result link |
| `PersonTagsSection` (U5) | the "Címkék" section of the detail pane, above "Becenevek": an editable person → `TagField` + the suggestion line ("Ezeken szokott ott lenni: [+ #Nordvik 11/14] … · Mind", at most 3 shown, rest behind "+N"; click accepts, × rejects); own person → one muted line; a nameless sample holder → hidden. Mode: `vm.tagsMode` = `none` \| `self` \| `edit` |
| `PeopleDetailPane` + `PeopleSampleRow` | header with inline rename, aliases, note (autosave on blur), voiceprint samples with play and the "…" menu, the no-voiceprint box (P03), meetings |
| `PeopleMergeDialog`, `PeopleSampleTargetDialog`, `PeopleUndoToast` | merge (P04), "new person from this sample" / "move to another person", the inverted toast with "Visszavonás" (~8 s) |
| `PeopleViewModel` (+ `PeopleListModel`) | list (filter, sort, sections, match parts), the selected person's detail, operations, toast texts; fictional people without a controller (`demoState`) |
| `PeopleWindowHost` | C++ host with its own engine: `open(person)`, `closed()` |

Core behind it (`core/include/tanara/people/`, `store/PeopleStore.h`):

- **Storage.** `people.json` holds one record per person (`{"version": 2, "people": [{"name",
  "aliases", "note"}], "unlisted": [...]}`, `PeopleStore`; keyed by name like `voiceprints.json`,
  locked reload-merge-save, unknown fields preserved). `unlisted` keeps aliases / notes of a name
  that is not on the list (a voiceprint-only person, or a name changed from outside); the record
  returns to the list when the name does. The old shape (a plain name list plus the sibling
  `people-details.json`) is migrated once, automatically, on load.
- **`PeopleService`** (`AppController::peopleService()`): persons, samples with a friendly source
  (`TrackCatalog` / `devicenames`), rename (old name becomes an alias; through the global
  `renamePerson` / `setUserSpeakerName`), aliases, note, sample delete / move / new person from a
  sample, voiceprint from the meetings with manually assigned lines (the per-meeting
  `SpeakerEditor::createVoiceprint`), merge, delete, undo.
- **`PeopleStats`** (`AppController::peopleStats()`): meeting count, talk time and last-seen per
  person, computed on a worker thread and cached per meeting (key: mtime + size of
  `meeting.json`, `transcript.segments.json`, `transcript.speakers.json`); the list shows names and
  sample counts at once and fills the rest in (`vm.statsReady`).
- **Undoable** (toast + `Ctrl+Z`): sample delete, sample move / new person from a sample, alias
  removal, rename. **Not undoable** (the dialogs say so): merge, delete.
- **Stale summaries.** Merge, delete and sample move mark the summaries of the affected meetings
  stale (`speakeredit::markSummaryStale`) and emit the usual `AppController` signals, so the
  library, an open transcript editor and Settings follow without a restart.
- **"Keep samples as an anonymous person"** (delete dialog): the samples move to a new person
  named "Névtelen N", who appears in the list and can be renamed or merged later.
- Aliases are also found by the transcript editor's person pickers (`filterPeople`,
  `PersonInfo::aliases` / `matchedAlias`).

Keyboard: `Ctrl+F` search · `F2` rename · `Del` delete (asks) · `Space` plays the selected sample
· `Ctrl+Z` undo (inside a text field the field's own undo applies) · `Ctrl+W` closes.

Where it opens: `ShellActions.openPeople(person = "")` (sidebar footer, Fájl menu) →
`QmlShellBridge::openPeopleAt` → host; the "Személyek kezelése →" link in Settings › Általános
(`SettingsDialogs::openPeopleAt`, with the user's own person selected; in the stand-alone
recorder / `tanara --settings` processes `SettingsWidgetsDialogs` owns a host of its own).

Samples are played through a backend from the same factory as the main player
(`PlayerController::createBackend`); without a factory (tests, screenshots) it is the silent one.

Screenshots (fictional data, no controller):

```bash
build/gui/tanara --qml-shot out.png --qml-page PeopleWindow --size 960x1100 --qml-prop 'demoState="P04"'
```

`demoState`: `P01` … `P06` · `newPerson` · `sampleNew` · `sampleMove` · `noResult` · `sort` ·
`allSamples` · `deleteKeep` · `creating` · `toastPlain` · U5/E01: `tagsSection` · `tagsSuggested` · `tagsSelf` · `samplesModels` (samples labelled by model) · `backfill`.

The **Címkék window** has the mirror view (U5): `TagPeopleList` ("SZEMÉLYEK" + count + "+ Személy"; row = avatar, name, "11 / 14", a "# címkéje" state or a dashed "+ Ráteszem" chip with × to reject; `personClicked` opens the Személyek window; `PersonPicker` has a tag mode, `tagId`, for "+ Személy"). `TagsWindow` `demoState`: `T10` … `T13` · `rename` · `people` · `peopleEmpty` · `peopleAdd`.

## First steps window (`OnboardingWindow.qml`, `src/Onboarding*`)

Screen list item K14 (`design/kepernyolista.md`). There is no designer package: it follows the
Settings window and the empty-library screen (`SettingsNavItem` rail, `SettingsTextField`,
`SettingsCombo`, `SettingsSegmented`, `SettingsSwitchRow`, the numbered step chips of
`EmptyLibraryView`). A separate, **non-modal** window (720 × 640) with native decorations.

**Wizard, not one long page.** Four of the six steps are real decisions, and each needs its
own "Kihagyom" (skip). Per step that button clearly applies to what is on screen; on one long
page it would not. "Tovább" saves the step at once, so a wizard left half-way keeps what was
accepted. Nothing is mandatory: "Később" and the window's × close it at any time.

| Step | Content | Saved on "Tovább" |
|---|---|---|
| `welcome` | what Tanara does (local-first, own keys), "everything can be changed later" | — |
| `you` | own name (prefilled from `AppSettings::userSpeakerName`, i.e. the OS account on a fresh install), UI language (applies at next start), theme (live preview) | name via `AppController::setUserSpeakerName`; `uiLanguage`; theme via `themeModeSaved` (the main window stores it) |
| `folders` | recordings and notes folders, "alapértelmezett" pill when unchanged, Tallózás… (`SettingsDialogs::pickFolder`), reset | `audioDir`, `notesDir` |
| `providers` | own-key explanation, readiness lines from `ReadinessModel` ("Átírás: nincs kulcs", "Összefoglaló: LM Studio · gemma-4-12b"), "Beállítás most" → Settings › Szolgáltatások, "Tanara Cloud: hamarosan" (not a choice; in live cloud mode with Cloud chosen the own-key copy is replaced) | — |
| `watcher` | "Induljon el a figyelő bejelentkezéskor" + platform note | `watcherAutostart` + `tanara::autostart::applyWatcher` |
| `done` | summary of accepted / skipped steps; "Kezdjük" closes | `onboardingDone` |

| Piece | Role |
|---|---|
| `OnboardingWindow.qml` | rail, step pages, footer (Később · Vissza · Kihagyom · Tovább / Kezdjük) |
| `OnboardingViewModel` | per-step draft over `AppSettings`; `next()` applies **only the step's difference** onto the core's current settings, `skip()` drops it, `discardPending()` on close, `markDone()`; `sttStatus` / `llmStatus`, `providerLabel()` |
| `OnboardingWindowHost` | C++ host with its own engine: `open()` (manual, always), `openIfNeeded()` (first run: only while `AppSettings::onboardingDone` is false, once per process), centred over the transient parent; closing the window in any way sets `onboardingDone` |

Where it opens: `QmlShellBridge::windowShown()` → next event-loop turn → after the cloud
startup checks (K-01 mode choice in live cloud mode stays as it was) → `openIfNeeded()`. Never in
`--shell-script` QA mode. Manually: File › "Első lépések…" (`ShellActions.openOnboarding()` →
`ShellBridge::openOnboarding()`) and the link at the bottom of Settings › Általános
(`SettingsDialogs::openOnboarding()`; hidden where no main window runs: `tanara --record`,
`tanara --settings`).

Screenshots (fictional data, no controller):

```bash
gui/qml/shoot.sh 'OnboardingWindow:720x640:demoState="you"'
```

`demoState`: `welcome` · `you` · `folders` · `providers` · `watcher` · `done`.

## Recorder (`Recorder*.qml`, `VuMeter.qml`, `src/Recorder*`)

Spec: `design/handoff-recorder/README.md` (states R01–R11).

| Piece | Role |
|---|---|
| `RecorderView.qml` | the whole recorder as an **item** (title bar, title field, start / status + stop, source lines, device list, R06 box, R07 sheet, R09, R10); 380 px wide, height follows content |
| `RecorderPill.qml` | pill mode (R05) |
| `RecorderWindow.qml` | frameless always-on-top `Window` hosting the two; created by the host |
| `RecorderPreview.qml` | screenshot wrapper with fictional devices: `--qml-page RecorderPreview --size 420x640 --qml-prop 'demoState="R04"'` (`R01`…`R10`, `R03typing` = T08c) |
| `VuMeter.qml`, `RecorderSwitch.qml`, `RecorderButton.qml` | 14-segment meter with peak hold, 30×18 switch with lock (unplugged device), the recorder's buttons |
| `RecorderViewModel` | state, title, device model (`devices`: name / rawName / group / selected / locked / appName / level / peak / status…), `start()`, `stop()`, `toggleDevice(row)` (during a recording: on → the device's track starts now, a new segment if it ran before; off → its file ends, `AppController::stopRecordingDevice`; the last open track stays on; `locked` = unplugged device); tags (C06): `tags`, `addTag(name)`, `removeTag(id)`, `openTagInput()` (Ctrl+T) — handed to the core with `AppController::setRecordingTags` (the meeting only exists when the recording ends); without a controller it serves fictional data (`demoState`) |
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

- Settings: closing the **main** window while Settings has unsaved changes drops them without a
  question; two stand-alone `tanara --settings` processes are not prevented.
- The software renderer does not clip `Shape` items (`TDashedRect`, so also a *disabled*
  `TButton`) to a clipping `Flickable`: scrolled under the Settings footer they would paint over
  it in offscreen QA shots. Hide such an item instead of disabling it there (see the locked
  "Tallózás…" button). The hardware renderer clips correctly.
- `--qml-shot` / `--demo` / `--gallery` create no `AppController`, but the process still installs
  the translator first, which reads `settings.json` for the UI language (and creates the file on
  a machine that has none). Set `TANARA_HOME` to a scratch folder to keep them fully isolated.

- People: creating a voiceprint from meetings decodes each meeting's audio on the UI thread (one
  meeting per step, a few seconds each, with a progress line); meeting rows are not links; the
  call-app name ("Hívás hangja · Teams") is not stored with a track, so it is not shown.
- Custom title bar is not implemented (native decorations are kept, as the spec allows);
  the centred "Tanara" caption is therefore omitted.
- `TDialog` / `TMenu` are in-window popups (`Popup.Item`); they cannot extend beyond the
  window.
- Windows packaging (`windeployqt --qmldir gui/qml`) has not been updated or tested.

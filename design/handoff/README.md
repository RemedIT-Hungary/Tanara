# Handoff: Tanara — main window & transcript editor (Qt Quick / QML)

## Overview
Tanara is a local-first desktop app (Linux + Windows) that records online meetings per audio source, transcribes them, separates/recognises speakers by voice, and produces summaries. This package covers the **redesign of the main window** (library + meeting view: Transcript / Summary / Tracks) and the new **transcript editor** (speaker rail for per-line reassignment, whole-speaker rename/merge). Recorder window, tray, People, Settings and First run are **not** in this package yet.

Target stack: **Qt 6, Qt Quick / QML, Qt Quick Controls 2 with a custom style** (no web view). Default window 1280×820, resizable.

## About the design files
The `design/*.dc.html` files are **design references built in HTML** — static mockups of the intended look and states, not code to port. Open them in a browser (keep `support.js` next to them; internet needed for fonts/icons). Recreate the UI natively in QML using the tokens in `qml/theme/Theme.qml`. Where HTML uses flexbox, use `RowLayout`/`ColumnLayout`; long lists (library, transcript) must be `ListView`s with delegates.

## Fidelity
**High-fidelity.** Colours, type, spacing, radii and copy are final for the main window and transcript editor. Interaction details below are the spec; motion is minimal (120–200 ms, OutCubic).

## Design files (in `design/`)
- `Tanara Visual Language.dc.html` — visual language. **Direction 1b "Nyomat / Print" is the chosen one**; 1a is a rejected alternative, ignore it.
- `Tanara Transcript Editor.dc.html` — **section 3 / option 3a is the approved editor**. Sections 2a/2b are earlier explorations (2b is the basis of 3a; ignore 2a). Has a `lang` hu/en tweak.
- `Tanara Main Window.dc.html` — screens M01–M10 (all main-window states). Has a `theme` tweak (mixed/light/dark).
- `design-brief.md` — the original product brief (Hungarian), incl. principles and terminology.
- `fonts/` — IBM Plex woff2 used by the HTML files (for QML, bundle the TTF/OTF of the same families).

## Reference screenshots (`screenshots/`)
Use these to compare the QML implementation visually (1280×820 unless noted):
- `M01`–`M10` — main window states (names match the sections below).
- `E01_editor_rail_on_name_menu` — editor, rail on, whole-speaker popover open.
- `E02_editor_rail_hidden_dark` — reading mode, rail hidden, dark theme.
- `E03_editor_two_speakers_multiselect` — 2 speakers, 3 lines selected, bottom move bar.
- `E04_editor_uncertain_filter` — "Bizonytalan" filter on, dark.
- `E05_editor_11_speakers_picker` — 11 speakers, collapsed "+5" lane, drag preview, person picker (editor area only).
- `V01`/`V02` — visual language sheet (Print direction), light and dark.

## Design tokens
All tokens are in `qml/theme/Theme.qml` (singleton, `Theme.dark` switches light/dark) and `tokens.json`. Hex values were converted from the OKLCH originals.

| Token | Light | Dark | Use |
|---|---|---|---|
| bg | #f6f5f3 | #121416 | window / content background |
| surface | #fbfbf9 | #191b1d | sidebar, title bar, player, cards, sticky headers |
| raised | #ffffff | #232529 | inputs, secondary buttons, popovers, dialogs, selected row |
| sunken | #ecebe8 | #0d0e11 | search field, progress track, overview track |
| border | #dbdbd8 | #313336 | 1px dividers, card borders |
| borderStrong | #b5b4b1 | #505357 | secondary button border, dashed targets, checkbox |
| text | #191b1d | #ecebe8 | primary text |
| textMuted | #585b5f | #acaba7 | secondary text, meta, timestamps (≥4.5:1) |
| accent | #2f62ac | #7eb1f3 | primary button, active tab underline, focus, progress |
| onAccent | #fcfcfc | #09121f | text on accent |
| accentSoft / accentLine | #e1ecfc / #bacfef | #1e2e47 / #304d78 | selected library item, playing row, suggestion box, focus ring (3px) |
| warnSoft / warnLine / warnInk | #fcedcd / #eac992 / #784900 | #3e2d10 / #6c5019 / #f3d086 | "elavult"/"bizonytalan" pills, missing-setting & stale banners |
| successSoft / successInk | #daf3e1 / #115531 | #193825 / #a7e1ba | done status icons, "javítva" pill |
| danger / dangerSoft / dangerLine / dangerInk | #ba3535 / #ffece9 / #f9bdb7 / #9b1e22 | #da534f / #3e1e1c / #843c38 / #febab4 | destructive button, error card |
| rec | #d42f34 | #e9504d | record button only (white text + white dot) |
| scrim | #5913161b | #80000000 | dialog backdrop |

**Speaker palette** — 11 hues in fixed order `[30, 230, 145, 330, 75, 270, 185, 0, 110, 300, 55]`, assigned by order of first appearance within a meeting. Each has `line` (lane block, avatar ring, chip dot), `soft` (avatar/chip fill), `ink` (name label on bg). Use `Theme.speakerLine(i)` / `speakerSoft(i)` / `speakerInk(i)`.

**Type** — IBM Plex Sans + IBM Plex Mono (SIL OFL; bundle the TTFs via `FontLoader`). Title 20/600; heading 16/600; body & transcript 14/400 (transcript line-height 1.55); small 13; caption 12 (section labels 12/600 uppercase, letter-spacing 0.06em); micro 11 (pills 11/600). Timestamps and durations always mono 11–12.

**Spacing** 4/8/12/16/24/32/48. **Radius** lane 3, control 6, popup 8, dialog 10, pills fully round. **Heights** controls 34 (toolbar 28), title bar 36, player 52, sidebar 276 wide. **Shadow** popovers/dialogs: 0 12 32 + 0 1 3 in `shadowColor` (use `MultiEffect` shadow).

## Assets
- Icons: **Lucide** (ISC licence), stroke 1.75, 15–16 px in UI. Ship as SVGs and tint via `ColorOverlay`/`MultiEffect colorization`, or use the Lucide icon font. Used: `file-text, sparkles, user-check` (status), `search, users, settings, fingerprint, ellipsis, undo-2, redo-2, plus, panel-left, chevron-down, check, wand-sparkles, play, pause, volume-2, mic, monitor-speaker, speaker, audio-lines, triangle-alert, lock, rotate-ccw, list-tree, list-filter, copy, folder-open, grip-vertical, trash-2, inbox, radar, arrow-right, minus, square, x, chevrons-left-right, pencil`.
- No raster images. Replace all emoji icons of the current app.

## Window structure (all screens)
1. **Title bar** 36px, `surface`, bottom border. App mark (14px accent square, radius 3), menu "Fájl", "Nézet" (13px), centred "Tanara" muted, window buttons 36×28. On Windows/Linux you may keep native decorations instead; then put the menu bar in the same row style.
2. **Sidebar** 276px, `surface`, right border, padding 14/12, gap 10:
   - "Új felvétel" button — full width, 34px, `rec` bg, white 14/600, 10px white dot.
   - Search field 32px, `sunken`, border, search icon, placeholder "Keresés", hint "Ctrl+F" mono 11.
   - Library `ListView`, grouped by date section headers ("Ma", "Tegnap", "Ezen a héten", "Korábban": 11/600 uppercase muted). Item: padding 9/10, radius 5; title 14/600 single-line elided; meta 12 muted (e.g. "okt. 2. · 30 p", **no wrap**) left, three 20×20 status icons right (gap 5). Selected item bg `accentSoft`.
   - Status icon states (icons file-text / sparkles / user-check = transcript / summary / identified): done = successSoft bg + successInk icon; running = accentSoft bg + accent border/icon; error = dangerSoft + dangerLine + dangerInk; stale = warnSoft + warnLine + warnInk; missing = transparent + borderStrong border + muted icon. Provide tooltips.
   - Footer (top border): "Személyek" and "Beállítások" ghost buttons (only place they appear — removed from the toolbar).
3. **Meeting header** (padding 16/24/0): title 20/600, meta 13 muted ("2026. okt. 1. · 1:16:04 · 6 beszélő"); right: secondary button "Résztvevők azonosítása" (fingerprint icon) + 32px "…" icon button → menu: Átnevezés, Megnyitás mappában, Újra-átírás…, ─, Törlés… (danger ink). Speakers are **not** listed in the header any more.
4. **Task strip** (optional, under header): accentSoft box with accentLine border, icon, "Résztvevők azonosítása…", "3 / 5 beszélő", 4px progress, "Megszakítás". Used for any cancellable background task on this meeting.
5. **Tabs** "Átirat", "Összefoglaló", "Sávok": 14px, active 600 + 2px accent underline; inactive muted. Summary tab shows a warn pill "elavult" when stale.
6. **Content** (per tab, below).
7. **Player** 52px, `surface`, top border: 32px round play/pause (text bg, bg icon), time mono "00:17 / 30:34", 4px seek track with accent fill and 12px knob, speed "1×" mono box, volume icon.

## Screens — main window (`Tanara Main Window.dc.html`)
- **M01 Empty library** — sidebar shows inbox icon, "Még nincs megbeszélés", "A felvételek itt jelennek meg, legújabb felül." Content: max-width 520 centred; heading 26/600 "Vegyük fel az első megbeszélést"; body 15 muted; a 3-step flow (Felvétel → Átirat → Összefoglaló, numbered chips with arrow icons); buttons "Felvétel indítása" (rec) and "Hívásfigyelő beállítása" (secondary → Settings › Figyelő); note about the tray watcher. No header/tabs/player.
- **M02 No selection** — centred 560px: "Válassz egy megbeszélést", "Ezek várnak rád:", list card of actionable items (icon tile 28px + title 14/500 + sub 12 muted + secondary button): pending transcription ("Megnyitás"), stale summary ("Frissítés"), failed transcription ("Megnézem"). Hide the card if nothing pending.
- **M03 Before transcript (missing setting)** — replaces tabs when no transcript exists. Vertical stepper (28px numbered circles joined by 2px border line), max-width 760:
  1. "Miről szólt a megbeszélés?" — helper text + multi-line field (context for STT). Optional.
  2. "Előkészítés" (label "opcionális") — two switches: "Résztvevők azonosítása hang alapján" (+ explanation that it is skippable; transcript works with anonymous speakers), "Lekeverés készítése".
  3. "Átírás" — if no STT provider: warn banner "Nincs beállítva átíró szolgáltató" / "Add meg a saját kulcsodat, vagy jelentkezz be a Tanara Cloudba." + button "Szolgáltató beállítása →" (opens Settings › Külső szolgáltatások). "Átírás indítása" shown disabled **with readable label** (sunken bg, muted text, lock icon) + reason text. When configured: primary enabled.
- **M04 Transcription running** — card (surface, radius 8): "Átírás folyamatban" + ETA; stage list Feltöltés (done ✓ + "52 perc, 3 sáv"), Átírás (running, 6px bar + %), Beszélők szétválasztása (vár), Résztvevők azonosítása (vár); footer "Közben nyugodtan dolgozz tovább; szólunk, ha kész." + "Megszakítás". Library item shows running status. App stays fully usable.
- **M05 Search + filter, failed transcription** — sidebar search focused (accent border + 3px accentSoft ring), filter chips below (removable: "Nincs összefoglaló", person chip with speaker colour, dashed "+ Szűrő"), result count, items show a transcript snippet with the match highlighted (warnSoft). Content: error card (dangerSoft/dangerLine) — "Az átírás nem sikerült", human explanation, mono details line ("HTTP 401 · invalid_api_key · …"), "Újrapróbálás" (primary) + "Kulcs módosítása". Always state that recording and tracks are untouched.
- **M06 Summary — none yet** — "Még nincs összefoglaló" + context; two choice cards side by side: **Gyors összefoglaló** (recommended, accent 1.5px border, primary "Összefoglaló készítése", "kb. 1 perc") and **Témánkénti elemzés** (secondary "Témák javaslása", "2 lépés, témánként 1–2 perc"). Provider line underneath.
- **M07 Summary — done, stale** — warn banner "Az összefoglaló óta 3 beszélőt javítottál…" with "Frissítés" / "Rendben így". Two columns (1fr / 250): left = "Vezetői összefoglaló" (15/1.6), "Döntések" (timestamp links in accent mono → seek player & transcript), "Teendők" (checkbox, text, owner chip in speaker colours, due date mono); right = "Résztvevők" (avatar + name + talk-time %), generation meta, actions "Témánkénti elemzés", "Másolás", "Megnyitás mappában". Stale = speakers changed after summary creation.
- **M08 Topic analysis** — header "Témák" + counts + "Téma hozzáadása" + primary "Hiányzók elemzése". Topic cards (surface, radius 8, drag handle for reorder, title 15/600, description 13 muted, state pill): kész (result text), fut (progress), hibás (dangerLine border, message + "Újra"), vár. Each topic runs/retries independently.
- **M09 Tracks** — list card; row: 32px play button, source icon, **friendly name** 14/600 ("Saját mikrofon", "Hívás hangja", "Rendszerhang") + raw device id below in mono 11 muted (elided), waveform (2px bars in speaker/track colour), duration, action. States: dropped/silent (75% opacity, flat waveform, pill "eldobott · csendes", "Visszaállítás"); missing file (danger pill "hiányzik a fájl", "Megkeresés…"). Mixdown row with % progress + "Megszakítás". Footer: "Eldobott sávok végleges törlése" (danger ghost, confirm dialog). Friendly names: map PipeWire/PulseAudio/WASAPI device roles (input → "Saját mikrofon", monitor of call app → "Hívás hangja", other monitor → "Rendszerhang"), let user rename.
- **M10 Confirmation — re-transcribe** — scrim + 440px dialog (radius 10): "Újra-átírod a megbeszélést?", consequence with concrete numbers ("…23 kézi javítás elvész…; a hanglenyomatokba tanított javítások megmaradnak"), checkbox "A mostani átirat maradjon meg másolatként" (default on), "Mégse" / danger "Újra-átírás". Use the same pattern for delete, close-while-recording and "Vége a meetingnek?".

## Transcript editor (`Tanara Transcript Editor.dc.html`, option 3a)
Lives in the **Átirat** tab.

**Toolbar** (padding 12/24/10, bottom border): label "ÁTTEKINTÉS"; right: "Sávok" toggle (panel-left icon, Ctrl+L; on = accentSoft bg + accent border/text), "Bizonytalan N" filter chip (striped swatch; on = accent style), "Visszavonás Ctrl+Z", redo, search.

**Overview (swim-lanes)** — one 12px row per visible speaker: name (110px, ink colour, 11/600), 8px track (`sunken`) with that speaker's segments over the meeting timeline, talk-time % (mono). Rare speakers folded into "Egyéb (N)". A translucent `text` @12% band shows the current viewport. Click → seek + scroll.

**Transcript list** (`ListView`, one delegate per utterance):
- Speaker turn header only when the speaker changes: **name** 13/600 in speaker `ink` + timestamp mono 11 + optional pill ("bizonytalan" warn / "javítva" success). Consecutive utterances of the same speaker stack as paragraphs (6px gap); new turn adds 14px top padding. Text 14/1.55, padding left 24 / right 20. Long monologues (200–300 words) are normal.
- Row states: playing = `accentSoft` row bg + accent timestamp; selected = `raised` bg + 1.5px accent inset border; drag source = block at 30% opacity + floating accent chip "→ Name".

**Speaker rail** (left of the text, toggled by "Sávok"; **hidden by default**, remembered per meeting):
- Sticky header row (`surface`): per visible speaker a 24px column with 20px solid avatar (`line` colour, monogram 8/700 `onSpeaker`) and a 4px dot below if the person has a voiceprint; "+N" collapsed group (20px, bordered); "+" add column (dashed circle; accent when its popover is open). Count label "412 megszólalás · 4 beszélő".
- Each row repeats the columns; the utterance's block fills its speaker's column for the full row height (inset 3px top/bottom, 4px sides, radius 3). Uncertain = soft fill + 135° stripes in `line` (3px/3px). Selected = 2px `text` outline; other columns show 1px dashed `borderStrong` drop targets. Drag target = soft fill + 2px dashed outline in target colour. Lines suggested by the similarity engine = 2px accent outline.
- Interactions: **click another column** → move that line; **drag** the block; **Shift/Ctrl+click** multi-select, then a dark bottom bar appears: "3 sor kijelölve · Áthelyezés:" speaker chips with number keys (1, 2…), "Új résztvevő…", "Mégse". Keys 1–9 move selection to the Nth speaker.
- "+N" column expands collapsed speakers. Collapse rule: speakers with < ~3% talk time beyond the 6 most active.
- "+" column → **person picker** popover (300px): search field (type-ahead), known people (avatar, name, "12 megbeszélés" or "nincs hanglenyomat", fingerprint icon), "+ Új személy: „…”", footer hint "A beszélő névtelenül is maradhat; később is elnevezheted."

**Click on a speaker name** → whole-speaker popover (340px, anchored under the name; name gets raised bg + accent ring + chevron):
- "Kovács Lilla valójában…" + "41 megszólalás · hang alapján felismerve (82%)".
- Search "Név keresése vagy új személy".
- "MÁSIK SZEMÉLY": known persons (reassign all lines of this speaker), "Névtelen beszélő" (revert to anonymous).
- "ÖSSZEVONÁS EBBEN A MEGBESZÉLÉSBEN": other speakers of the meeting (merge).
- Checkbox (default on): "A sorok kerüljenek ki Kovács Lilla hanglenyomatából (téves felismerés)" — removes these samples from the wrong voiceprint and adds them to the chosen person.
- Footer "Mind a 41 sor átkerül · visszavonható: Ctrl+Z".

**Suggestion** — after a manual correction, an inline box appears right under the corrected line (accentSoft, accentLine border, wand icon): "Még 14 sor hasonlít erre a hangra. Átrakjam őket Fehér Ádámhoz?" + "Átrakom" (primary), "Megmutatom" (highlights matches on rail + overview), "Nem".

**Uncertain filter** — shows only uncertain lines; confident runs collapse into a divider "··· 14 biztos sor elrejtve"; each uncertain line gets "Jó így" (mark confident) and "Meghallgatom" (play that line).

**Undo** — every reassignment, merge, rename and suggestion acceptance is one undo step (Ctrl+Z / Ctrl+Shift+Z), including voiceprint side effects.

## State (suggested model)
- `Meeting { id, title, startedAt, duration, tracks[], transcriptState: none|running|failed|done, summaryState: none|running|failed|done|stale, identifyState: none|running|done, error? }`
- `Speaker { id, meetingId, personId?, label ("Távoli 1"), colorIndex, voiceConfidence }`, `Person { id, name, hasVoiceprint, meetingCount }`
- `Utterance { id, start, end, speakerId, text, uncertain: bool, manuallyCorrected: bool }`
- Editor UI: `railVisible` (per meeting), `uncertainOnly`, `selection: Set<utteranceId>`, `collapsedSpeakers`, `playingUtteranceId`, `undoStack`.
- Summary becomes `stale` when any speaker assignment changes after its creation.
- Background jobs (transcribe, summarise, per-topic analysis, mixdown %, identify) are cancellable and reflected in: library status icons, task strip, tab content.

## Copy & i18n
UI language Hungarian and English; Hungarian strings are ~20–30% longer — no fixed-width labels; elide only titles/device names. The Transcript Editor file contains both languages (tweak `lang`); the Main Window file is Hungarian only. All sample names and content are fictional.

## Not in this package
Recorder window, tray states, People management, Settings, First run, Tanara Cloud (login, credits, cost estimate, quality tier).

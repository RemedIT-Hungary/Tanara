# Handoff: Tanara — recorder window & tray (Qt Quick / QML)

## Overview
The recorder is a small, always-on-top floating window, separate from the main window (brief principle 4). It records each selected audio source to its own track. It must be compact, show where sound is actually coming from (live level meters on every device), allow renaming the meeting, toggling which devices get a track, and must collapse, shrink to a pill, or run hidden in the tray. **The recorder does no processing:** on stop the tracks are already on disk, so no "saving/encoding" screen is shown.

Target: Qt 6, Qt Quick / QML, custom-styled Controls. Tokens: `qml/theme/Theme.qml` (same singleton as the main window package; recorder tokens are at the end: `rec*`, `vu*`, `successLine`).

## About the design files
`design/Tanara Recorder.dc.html` is an **HTML design reference** (open in a browser with `support.js` next to it). Recreate it natively; do not embed HTML. Screenshots of every state are in `screenshots/`. Tweaks in the HTML: `theme` (mixed/light/dark) and `showRawNames`.

## Fidelity
High-fidelity: colours, sizes, copy and states are final.

## Window
- Width **380 px** fixed; height follows content (collapsed ≈ 230, expanded ≈ 520). Frameless with custom title bar, radius 10, 1px `borderStrong`, drop shadow. Draggable by the title bar; remember position per screen.
- **Title bar** 30px, `surface`, bottom border: 8px status dot (idle `borderStrong`; recording `rec` with 3px `recLine` ring; done `accent`), "Tanara felvevő" 12px muted, then 26×24 icon buttons (14px icons, muted; active = accent on accentSoft):
  1. `pin` — Mindig felül (toggle, default on)
  2. `chevrons-down` / `chevrons-up` — Kinyitás / Összecsukás
  3. `minimize-2` — Pirula méret
  4. `arrow-down-to-line` — Háttérbe (tálcára): hides window, keeps running, tray shows state
  5. `x` — Bezárás. **During recording it never stops recording**: it opens the close sheet (R07).
- Body padding 12, vertical gap 10.

## Components
- **Meeting title field** 32px, radius 6, `surface` bg + `border`; 14/600, elided; pencil icon right. Placeholder/auto name from the call watcher (e.g. "Teams-hívás · okt. 3. 14:02") in muted colour, with caption 11px "Automatikus név · kattints az átnevezéshez". Editing: `raised` bg, accent border + 3px accentSoft ring. Editable in every state, including during recording.
- **Start button** (idle) 42px full width, radius 7, `rec` bg, white 15/600 "Felvétel indítása", 12px white dot, hint "Ctrl+R" mono 11 @80%.
- **Recording status** (replaces start button): left box flex 1, 42px, `recSoft` bg + `recLine` border: 12px `rec` dot with 4px `recLine` ring (pulse 1.2 s), "FELVÉTEL" 12/700 letter-spacing .08em `recInk`, elapsed time right-aligned mono 22/500 (HH:MM:SS). Right: **Leállítás** button 42px, `text` bg / `bg` fg (inverted, neutral — not red), 11px square icon. The red area is a status indicator, not a button.
- **Source line** (collapsed view) 26px: 14px device icon muted, name 13px elided, optional pill, VU meter.
- **Device row** (expanded view): toggle switch 30×18 (on: accent; off: borderStrong outline), name 13px (600 when on, muted 400 when off) + second line: app currently playing to that output in accent 11/600 (e.g. "▸ Microsoft Teams"), "alapért." pill (default device), raw OS device name mono 11 muted elided (tooltip = full). Right: VU meter + 10/600 status text below.
- **Groups**: "MIKROFONOK · amit mondasz", "HANGKIMENETEK · amit hallasz" (output monitors/loopback), "EGYÉB BEMENETEK" (line-in/AUX). 11/600 uppercase muted + 11 muted sub.
- **VU meter**: 14 segments, 5×10 px, gap 2, radius 1. Lit if index < level: 0–8 `vuLow`, 9–11 `vuMid`, 12–13 `vuHigh`; unlit `vuTrack`. Peak hold: 1px inset outline on the peak segment, decay ~1.5 s. Update ≥ 20 fps from RMS/peak of the stream. **Unselected devices are metered too** (lit segments in `vuOff`).
- **Signal hints** (the key feature):
  - Unselected device with signal → row bg `accentSoft`, status "jel van · nincs rögzítve" in accent.
  - Selected device silent → "nincs jel" muted (idle) or, during recording after 3 min, pill/status "3 perce nincs jel" in `warnInk` on `warnSoft`.
- **Footer** (collapsed): 30px, top border, "3 / 6 forrás rögzül" muted + "Források ▾" → expands.
- **Expanded header**: hint text ("Beszélj vagy játssz le hangot: a mozgó jelző mutatja, melyik eszközön jön hang.") + "Összecsukás ▴".

## States (see screenshots)
- **R01 Idle · collapsed** — auto title, start button, selected sources with live meters, footer.
- **R02 Idle · expanded, editing title** — all devices with toggles; one unselected output with signal highlighted.
- **R03 Recording · collapsed** — status + Leállítás; per-track meters; silent track warning.
- **R04 Recording · expanded** — recorded tracks' toggles **locked** (lock icon in knob); other devices can be switched on → a new track starts from that moment (note text under the list).
- **R05 Pill** — ~230×40, radius 20, `surface`, always on top, snaps to screen edges: rec dot, time mono 15/500, one 4px vertical mini-meter per track (vuLow / vuOff when silent), divider, 32px round inverted stop button, `maximize-2` expand.
- **R06 "Vége a megbeszélésnek?"** — in-window warn box (warnSoft/warnLine): title, "A hívás sávján 2 perce csend van. Magamtól nem állítom le.", buttons "Folytatom" (secondary) and "Leállítom" (inverted). Triggered by watcher setting "Csend után kérdezzen" (default 3 min). If the recorder is a pill or hidden, it expands and also posts a system notification. **Never auto-stops.**
- **R07 Close while recording** — bottom sheet over a scrim inside the window: "A felvétel még fut" + explanation; primary "Fusson a háttérben" (hide to tray), "Leállítás és bezárás" (danger outline), "Mégse".
- **R09 Done** (right after stop) — successSoft box: "Elmentve", duration · tracks; "Megnyitás az elemzőben" (primary, opens main window with this meeting selected), "Új felvétel" (same sources).
- **R10 No audio device** — empty state with `mic-off`, explanation (PipeWire/PulseAudio hint on Linux), "Újrakeresés", "Rögzítés beállításai".
- **R11 Tray** — icon states: Figyel (muted ring + dot), Hívás észlelve (accent ring + dot), Felvétel fut (filled `rec` + white dot). Tooltip shows app / elapsed time. Menu while recording: header "Felvétel · 00:12:47", Felvevő megjelenítése (Ctrl+Shift+R), Felvétel leállítása, Elemző megnyitása, ─, Kilépés… (asks if recording). Call-detected notification (420 px): "Hívást észleltem: Microsoft Teams", "Elindítsam a felvételt? A Teams most a Sennheiser headsetre szól.", buttons "Felvétel indítása" (rec), "Felvevő megnyitása", "Nem most".

## Behaviour
- Device friendly names: derive from OS description (strip "Monitor of", bus/profile suffixes); show raw name as the second line. Renaming devices happens in Settings › Rögzítés (not in the recorder).
- Which app plays to which output: PipeWire/PulseAudio sink-input → sink mapping (Linux), WASAPI audio sessions per endpoint (Windows).
- Selection persists as the default for next time; Settings holds the defaults.
- Hot-plug: devices appear/disappear live; a recorded device that disappears mid-recording shows a danger status "leválasztva" and its track is closed safely.
- Keyboard: Ctrl+R start, Ctrl+. stop (with no confirmation — the stop button is explicit), Ctrl+Shift+R global show/hide.
- Safety (brief principle 5): nothing closes or stops recording without explicit user action.

## Files
- `design/Tanara Recorder.dc.html`, `design/support.js`
- `qml/theme/Theme.qml`, `qml/theme/qmldir`
- `screenshots/` — one PNG per state

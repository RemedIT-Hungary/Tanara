# Handoff: Tanara v3, meeting view (Overview, Transcript editor, Executive summary, Memo)

Qt 6 · Qt Quick / QML · existing `Theme` singleton and `T*` controls · light + dark · Hungarian UI copy (English allowed in code).

This package supersedes the main-window parts of `design_handoff_tanara_main_window` (header, tabs, Tracks tab, transcript toolbar/rail placement, summary tab). It answers the briefs in `briefs/` (E03 editor rework + the layout friction the user reported) and adds the flow changes agreed during design.

## Files
- `design/Tanara Meeting v3.dc.html`: header, **Overview** tab (running, done, empty, nobody recognised), **"Ki volt ott?"** dialog, Transcript reading mode with the map dock. Screens V1–V6.
- `design/Tanara Editor v3.dc.html`: Transcript **Javítás** mode: line popover with evidence, Átnézendő groups, whole-speaker "Miért ő?" menu, new person plus similar lines. Screens E1–E4.
- `design/Tanara Summary v3.dc.html`: **Vezetői összefoglaló** and **Memó** with source references; targeted staleness. Screens S1–S3.
- `design/history/…Analyzer v2…`: all intermediate rounds (1: modes vs panel, 2: header wireframes, 3: Tracks → Overview wireframes, 4: Overview + approval wireframes). Read only for rationale.
- `screenshots/`: one PNG per final screen; `screenshots/history/` key wireframes.
- `qml/theme/Theme.qml`: unchanged tokens (no new colours in v3).
- `briefs/`: the developer's brief and UI inventory (`leltar.md`) this design responds to.

Open the `.dc.html` files in a browser (keep `support.js` + `fonts/` next to them). They are HTML **references**: recreate them in QML; don't embed HTML. Each has a `theme` tweak (mixed / light / dark).

## Decision log (what changed and why)
1. **Header = one row.** Title (✎ / F2 to rename, no dropdown), vertical divider, tabs, `…` (meeting menu). Removed from the header: "Résztvevők azonosítása", undo/redo, tag row, meta line. *Why:* 7 stacked layers and 17 controls before the first text line; mis-clicks changed state without explanation.
2. **Tabs follow the pipeline:** `Áttekintés → Átirat → Vezetői összefoglaló → Memó`. **The Sávok tab is removed.** Running step shows on its tab (`Átirat 62%` pill, accentSoft/accent). Unavailable tabs are muted (`borderStrong` text); hover/click opens a tooltip popover that says what's missing and offers the action (V4).
3. **Tab-specific tools live in a 46 px row under the header** and change per tab; the header never moves.
4. **Undo has no permanent button.** Every edit posts the dark change bar ("3 sor átkerült ide: X · Visszavonás · Ctrl+Z"). One shared undo stack for tags and speakers.
5. **New menu "Megbeszélés"** mirrors `…`: Átnevezés, Ki volt ott?, Újraellenőrzés, Újra-átírás, Exportálás, Megnyitás mappában, Törlés. Nézet: tabs, Olvasás/Javítás (Ctrl+E), Beszélő-oszlopok (Ctrl+L).
6. **Overview (new first tab)**, two columns: left = *what & who* (sources, tags, participants), right 340 px = *technical* (pipeline steps, tracks, later stats). New integrations (Google Calendar, Outlook…) are new **cards** in "Miről szól", never new header rows. Finished sections collapse to one-line rows.
7. **Tracks before mixdown:** tracks are listenable and toggleable on Overview from the start. **Speech detection** per track: no speech → excluded from mixdown automatically, row dimmed with "nincs beszéd · kimaradt", actions `Beemelem` / (details) `Törlés`.
8. **Participants before the transcript:** can be added manually during processing (and in the recorder); optional. If given, identification is constrained/boosted.
9. **Voiceprint analysis runs in parallel with STT** (Soniox), so candidates exist when the transcript returns.
10. **"Ki volt ott?" approval** after voiceprint analysis (V2): Biztos / Kétséges / Meghívott, de nem hallottuk. Contradicted candidates are **unchecked by default**. Each row shows its raw-speaker mapping. "Tovább" = accept as is. This list is the **basis for binding raw STT speakers to people**; editable later (Overview "Módosítás", editor "Ki volt ott?").
11. **Tags ↔ people (bidirectional suggestions):** same tag set on meetings and people. Whichever field the user fills, suggestions come from the other (tag → people "11 / 14", people → tags "mindkettőjükön"). Never auto-applied.
12. **Transcript editor = modes:** Olvasás (clean list) / Javítás (rail, markers, Átnézendő). Speaker map moved into the **player dock** and acts as the scrubber in both modes.
13. **Evidence everywhere + fix location everywhere:** every suggestion shows why (voice %, track side, calendar, tag, existing line count); every reason shows where to fix it (Sáv-beosztás, Minták, Kettejük átnézése…). Contradicting evidence shown dashed.
14. **Track-side conflict = a reason of "bizonytalan"** (pill label + icon: hang / sáv / címke), not a separate marker.
15. **"Bizonytalan" → "Átnézendő"**: filter with group headers, one decision per group, one undo step per group.
16. **Summary/Memo source references**: every statement links to transcript time(s); hover shows quotes; map dock shows a "Forrás" row; Memo shows section bands. **Targeted staleness**: only statements/owners whose source speakers changed are marked.

## Screens

### Header (all screens)
56 px, border-bottom. Title 19/600 + pencil 15 muted · 1 px divider (17 px vertical inset) · tabs (14 px, 0 11 px padding; active 600 + 2 px accent underline; muted; disabled `borderStrong`) · flex · `…` 34 px secondary icon button.

### V1 Overview, processing (light)
- **MIRŐL SZÓL** + "+ Forrás" (accent). Calendar card: surface, border, radius 8, 32 px accentSoft icon tile (calendar), title 14/600 + source name 12 muted, meta line 12.5 muted, agenda 13, right "Leválasztás".
- **Tag field**: raised, borderStrong, radius 7, chips (24 px tag chip spec from tags package), "Címke…" placeholder, divider, suggestion label 12 muted, dashed accent suggestion chip + reason 11.5 muted + "Miért?".
- **KIK VOLTAK OTT** + hint (sources) + "+ Résztvevő". List: surface, border, radius 8; row 8/12 px: 26 px avatar (speaker soft + 1.5 px line ring, monogram 9/700), name 13.5/600 (150 px), source badges (20 px, radius 4, sunken bg + 11 px icon; negative = dashed borderStrong, muted), optional talk bar or "+ Hozzáadás" + ×. Declined invitee 55% opacity. Suggested person: dashed avatar, muted name, sub "javasolt", accentSoft badge.
- **Review banner** (when analysis done & not approved): accentSoft/accentLine, fingerprint icon, title 14/600, sub, primary "Átnézem", ghost "Később".
- **Right column** (surface, border-left, 340, padding 18): FELDOLGOZÁS steps: 22 px circle (done = successSoft + check; attention = warnSoft "!" and row raised + warnLine border; running = 2 px accent ring + 4 px progress; waiting = dashed borderStrong). "‖ párhuzamosan" mono chip on parallel steps. **SÁVOK** list (raised, radius 8): 26 px round play, source icon + name 13/600, sub 11.5 (speech % · who), switch 30×18 = "in mixdown"; excluded row 60% + warnInk sub + "Beemelem".
- Player 52 px (mixdown).

### V2 "Ki volt ott?" (dark)
Scrim + 820 px dialog, radius 10. Title 18/600 + mono meta (models · age), helper 13.5 muted. Groups (label 11.5/600 + hint + count) each in a bordered list; row: 16 px checkbox (accent when checked), avatar, name (130), evidence badges, mapping chip "→ Beszélő 1 · hívás" (accentSoft/accent when checked, sunken/muted when not). Unchecked rows 70%. Add field (230 px sunken) + tag-based person suggestion. Footer: info line (unbound raw speakers stay anonymous), ghost "Kihagyás", primary "Tovább · N résztvevő".
Opens from the banner, from the editor's "Ki volt ott?" button, or automatically when entering Átirat with an unapproved analysis.

### V3 Overview, done (dark)
Calendar card single line; tags without suggestions; participants with talk-share bar (speaker line colour) + "Módosítás"; right column: ADATOK 2×3 stat tiles (raised, 11/600 label, 19 px mono value, 11.5 sub), then collapsed rows "Minden lépés kész ▸", "Sávok ▸".

### V5 / V6 Overview, empty (light / dark)
Every empty part = dashed `borderStrong` card with one sentence of why + action. No calendar: "Leírás megadása", "Naptár összekapcsolása". No participants: "+ Résztvevő", "Csak én beszéltem" (single-speaker shortcut, skips identification). Nobody recognised (V6): raw speakers as coloured chips with share + primary "Elnevezem őket" (opens Ki volt ott).

### V4 Transcript, reading (light)
Tools row: segmented Olvasás/Javítás (Ctrl+E) · status · flex · "N átnézendő sor" (quiet) · search. List 14.5/1.6, max 880 px. **Map dock** (surface, border-top): one 13 px row per speaker (name 104 px ink, 8 px lane, share %), accent playhead line, 7% text-colour band = visible list range; controls row 44 px (play, time, hint, speed, volume). Click = seek; name = speaker menu.

### E1 Line fix (light), Javítás mode
Rail header row (24 px columns, avatar + side icon). Row markers: check (confirmed), "rövid" (ear icon, outline), "bizonytalan · sáv|hang|címke" (warn), "javítva", "egymásra beszéltek". **Popover 390 px**: title, "Most: X · ts", listen; scope radios (line / all of X); search; JAVASOLT candidates (avatar, name, "N sor itt", evidence chips: good = accentSoft/accent; neutral = sunken; bad = dashed muted; key hint 1–3); other-side candidate at 60%; **"MIÉRT NEM X?"** surface block with reasons + fix links (Sáv-beosztás, Minták); "Új személy ebből a sorból…"; footer "1 sor kerül át · Ctrl+Z · a gép tanul belőle".

### E2 Átnézendő (dark)
Chip "Átnézendő 84 ×" active (accentSoft/accent). Contaminated-core banner (warnSoft) with "Szétválasztás" and "Miért?". Group card: 28 px icon tile, title 14/600 + mono count, evidence chips, buttons (primary "Mind a N → X", "Egyenként", ghost "Kihagyom"). Collapsed group rows: mono "··· 69 sor ebben a csoportban · Kinyitás". Info group (short lines) neutral with "Megmutatom". Per-row inline actions when expanded: Meghallgatom · "X mondta (1)" primary · Jó így · Más….

### E3 Whole speaker "Miért ő?" (light)
Opens from the name in a row, rail avatar or map. 430 px popover: **MIÉRT Ő?** reasons with green/warn icons and fix links (Minták, Címkéi, Módosítás, Kettejük átnézése); **MELYIK SÁVON BESZÉL?** chips (single source of truth, same setting as Overview); **NEM Ő? VALÓJÁBAN…** candidates with evidence; links: Új személy…, Összevonás…, **Ő nem volt ott** (lines become anonymous; also unchecks in Ki volt ott); checkbox "remove from voiceprint (false recognition)"; warn footer "applies to all N lines".

### E4 New person (dark)
New rail column (avatar with accent ring), marker "új személy". Similar lines get 2 px accent cell outline and accent ticks on the map. Change bar (inverted) with 3 lines: result + Visszavonás; "Még 14 sor hangja hasonlít… (hang 76%, mind a hívás hangján)" + "Mind a 14 hozzá"; voiceprint readiness ("még kb. 9 mp kell").

### S1 Executive summary (light)
Tools row: generation meta · "Források" toggle (accentSoft when on) · Másolás · Újragenerálás. Each sentence followed by mono time chip(s) (accentSoft/accent; active = accent/onAccent; hovered sentence bg accentSoft). Decisions & to-dos list items end with chips; owner chip in speaker colours. **"Honnan jön ez?"** popover 440 px: quotes (avatar, name, ts, "Meghallgatom", quote 13/1.5), footer "Ugrás az átiratba", "Nem így hangzott el? · Jelzem". Map dock adds a **Forrás** row (accent = hovered statement, accentLine = all statements; lanes dimmed to 45%). Right column: participants + note.

### S2 Memo (dark)
TOC 236 px (section title + mono time range; active accentSoft). Section header 19/600 + time-range chip (click = play from) + speakers. Map dock adds **Szakaszok** band row (active section accent).

### S3 Targeted staleness (light)
Warn banner: "Az összefoglaló óta 3 beszélőt javítottál. 2 állítás és 1 teendő forrásában más lett a beszélő" + Frissítés / Rendben így. Affected sentences: 2 px warnLine underline + warn chips; affected owner chip dashed warn "Fehér Gábor → Varga Árpád?". Map highlights affected sources.

## Data / engine additions (suggested)
- `Meeting.calendarEvents[] { provider, id, title, start, end, organizer, invitees[{name,email,response}], agenda }`
- `Participant { personId|null, rawSpeakerIds[], source: calendar|voice|tag|manual, approved: bool, sides[] }`; `ParticipantApproval { at, candidates[{personId, group: sure|doubt|invited, checked, evidence[], mappedRaw }] }`
- `Track.speechRatio`, `Track.excludedReason: none|noSpeech|manual`
- `Evidence { kind: voice|side|calendar|tag|lineCount|similarity, value, polarity: support|contradict, fixTarget }`
- `SummaryStatement { id, text, sourceSpans[{startMs,endMs,utteranceIds[]}], staleBecause[] }`; Memo sections with `startMs/endMs/speakers`.
- Jobs: `voiceprintAnalysis` parallel to `transcribe`; approval gate is soft (never blocks the transcript).

## States checklist
Overview: processing · analysis ready (banner) · done · empty · nobody recognised · track excluded. Approval: all sure · doubts unchecked · invited not heard · add person. Editor: reading · Javítás all rows · line popover · Átnézendő groups · contaminated core · whole-speaker menu · new person + similar lines. Summary: sources on/off · source popover · memo section · targeted stale · not yet generated (tab tooltip).

## Not in this package
Settings › Hangmodellek (E01), People window changes (E02: per-model samples, person tags UI), calendar integration settings/OAuth. Tokens and controls from earlier packages still apply.

# Tanara desktop UI - current feature inventory (Qt 6 Quick/QML, Hungarian strings)

Read-only inventory. Path convention: `Q/` = `gui/qml/`, `S/` = `gui/qml/src/`, `core/` = `core/include/tanara/`. References are `file:line`. Labels are the exact `qsTr()` text (Hungarian). Semantic colour names are the `Theme.*` roles used in the QML.

Contents: 0 Shell hooks . A Transcript analyzer/editor (A1 TranscriptTab, A2 toolbar/overview, A3 rail, A4 rows, A5 popovers/bars, A6 Tracks, A7 Player, A8 MeetingHeader, A9 Summary, A10 Tags in meeting) . B People . C Settings . D Tags window . E Onboarding . F Core features WITHOUT UI . G Performance / threading facts

---------------------------------------------------------------------------------------------------

## 0. Shell hooks that frame the analyzer

| Item | Detail | Ref |
|---|---|---|
| Tab bar | `Átirat` / `Összefoglaló` (warn pill `elavult` when summary stale) / `Sávok` | Q/Main.qml:546-558 |
| Tab shortcuts | Ctrl+1 / Ctrl+2 / Ctrl+3; F2 rename meeting; Ctrl+F library search; **Ctrl+Shift+F** = switch to Átirat + open transcript search; Ctrl+Z only when tag-undo active (otherwise editor's own); Space toggles player (`spaceTogglesPlayer`) | Q/Main.qml:272-294 |
| Menu `Fájl` | `Új felvétel…` (Ctrl+N), `Hangfájl importálása…` (Ctrl+I), `Megbeszélés importálása archívumból…`, `Megbeszélés importálása mappából…`, `Beállítások…` (Ctrl+,), `Személyek…`, `Első lépések…`, `Kilépés` | Q/Main.qml:328-372 |
| Menu `Nézet` | `Átirat`, `Összefoglaló`, `Sávok`, `Résztvevők azonosítása (hang alapján)`, `Felvétel-ablak előtérbe` | Q/Main.qml:380-420 |
| Entry to Tags window | `Címkék kezelése…` in library filter popover; sidebar `Címkék` | Q/LibraryFilterPopover.qml:295-309, Q/LibrarySidebar.qml:416 |

---------------------------------------------------------------------------------------------------

## A1. TranscriptTab (Q/TranscriptTab.qml) - the editor container

Purpose: per-meeting speaker-correction editor: overview + (optional) speaker rail + utterance list + change/selection bars. State comes from `TranscriptEditorViewModel` (`editorVm`, Q/TranscriptTab.qml:225), which wraps `tanara::SpeakerEditor` (`AppController::speakerEditor(meetingId)`).

### Top-level states
| State | What the user sees | Ref |
|---|---|---|
| No transcript | file-text icon + `Ehhez a megbeszéléshez még nincs átirat.` | TranscriptTab.qml:325-338 |
| Legacy transcript (no utterance list) | info banner `Ez az átirat régebbi formátumú: olvasható és másolható, de a beszélők itt nem javíthatók. …`; button `Újra-átírás…` (`shell.retranscribe`); read-only text of transcript.md, or `Az átirat szövegfájlja (transcript.md) nem található.` | :340-402 |
| Normal | toolbar + header strip + list + bars | :404-725 |
| Empty "Bizonytalan" filter | `Nincs bizonytalan sor — minden megszólalás beszélője rendben van.` + ghost button `Beszélők újraellenőrzése…` (visible only if voice analysis available and not running; tooltip: `A megerősített és javított sorok hangjához mérem a többi sort` or the blocker sentence) | :675-697 |
| Voice analysis running | header-strip right: `Hangelemzés… %1%` + 72 px progress bar | :463-480 |
| Voice analysis unavailable | header-strip right: info icon + `Hangelemzés nélkül` (tooltip = `voiceNote`, see below) | :481-497 |
| Count label | `%1 megszólalás · %2 beszélő` | :457 |

`voiceNote` texts (S/TranscriptEditorViewModel.cpp:787-794): model missing -> `Nincs letöltve a hangmodell, ezért a bizonytalan sorok jelölése, a hasonló sorok felajánlása és a kézi hanglenyomat most nem érhető el. A szerkesztés enélkül is működik.`; audio missing/embedding failed -> same with `A megbeszélés lekevert hangja nem érhető el…`.

### Keyboard / mouse (editor)
| Input | Effect | Ref |
|---|---|---|
| Ctrl+L | toggle speaker rail (`Sávok`) | TranscriptTab.qml:274-278 |
| Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y | undo / redo (disabled while a text field has focus, or when tag undo owns Ctrl+Z) | :279-288 |
| Ctrl+C | copy selection, or playing row | :293-295, :105-108 |
| Ctrl+A | select all rows | :296 |
| B / Shift+B | next / previous uncertain row (wraps); toast `Nincs bizonytalan sor.` if none | :299-301, :88-94 |
| Up / Down | step selection | :302-305 |
| 1-9 | move selected rows to the N-th rail lane | :306-308 |
| Esc | clear selection, else close search | :309-314 |
| Space | player toggle | :315 |
| Enter | play from current row | :318-320 |
| Click row text | select (Ctrl toggle, Shift range); click again deselects | TranscriptRow.qml:126-135 |
| Double-click row text | `playFrom(startMs)` | TranscriptRow.qml:136 |
| Right-click row | row context menu (below) | TranscriptRow.qml:133 |
| Timestamp click | play from that line | TranscriptRow.qml:214-219 |
| Hover on non-head paragraph | floating timestamp chip, clickable | TranscriptRow.qml:374-400 |

### Row context menu (TMenu `rowMenu`, TranscriptTab.qml:811-878)
| Label | Notes |
|---|---|
| `Lejátszás innen` | enabled when player available |
| `Más mondta… (ez a sor)` / `Más mondta… (a kijelölt sorok)` | opens line popover after menu closes |
| `Jó így + hanglenyomat-minta ebből a sorból (%1 mp)` / `Hanglenyomat-minta ebből a sorból (%1 mp)` / `Hanglenyomat-minta ebből a sorból` | `createVoiceprintFromLine`; first variant when the row is uncertain (confirm + sample); disabled unless `lineSampleInfo.ok` |
| `Sor másolása`; `Kijelölt sorok másolása (%1)` (Ctrl+C); `Teljes átirat másolása` | clipboard; toast `%n megszólalás a vágólapra másolva.` (VM.cpp:1515) |
| `Minden sor kijelölése` (Ctrl+A) | |

### Merge confirmation dialog (TranscriptTab.qml:776-809)
Title `Összevonod a két beszélőt?`; text `%1 %n sora összeolvad ezzel: %2 (%3). Visszavonható: Ctrl+Z.`; buttons `Mégse` / `Összevonás` (primary). Only for whole-speaker merges (kinds `merge`, `reassign`, `rest`); a single line/selection move never asks (:192-208).

### Scroll/follow behaviour
Playing row follows playback only if the previous playing row was visible, or after a seek/jump (:64-67, :250-259). Shell signal `transcriptPositionRequested(ms)` scrolls to a time and clears the uncertain filter (:263-271). Rail visibility is persisted per meeting (`loadRailState/saveRailState`, S/TranscriptEditorViewModel.h:458-459).

### Demo states (screenshots)
`demoVariant`: "" two many long novoice none; `demoState`: see header comment TranscriptTab.qml:21-29 (includes `changePairOffer`, `speakerPopoverPair`, `voiceprintHas/None/Done/Short`).

---------------------------------------------------------------------------------------------------

## A2. TranscriptToolbar + overview (Q/TranscriptToolbar.qml)

| Control | Label / tooltip | Behaviour | Ref |
|---|---|---|---|
| Section label | `Áttekintés` | replaced by search field when search open | :131-135 |
| Search field | placeholder `Keresés az átiratban`; counter `Nincs találat` (dangerInk) or `%1 / %2`; chevrons `Előző találat (Shift+Enter)` / `Következő találat (Enter)`; Esc closes | `vm.searchQuery`, `searchStep` | :138-182 |
| Chip `Bizonytalan` + mono count | outlined round chip with hatch glyph; checked = accent; tooltip variants: `Minden sor mutatása`; `Csak azok a sorok, ahol a beszélő hang alapján kétséges`; when 0 uncertain + recheck possible: refresh-cw icon and `Nincs bizonytalan sor. Kattints, és a megerősített és javított sorok hangja alapján újraellenőrzöm a többit.`; when recheck blocked: `Nincs bizonytalan sor. Újraellenőrzéshez: %1`; disabled (tooltip = voiceNote) without voice analysis | click = toggle filter, or (offersRecheck) `recheckRequested` -> `root.recheckSpeakers()` | :184-211 |
| `chevrons-down` button | `Következő bizonytalan sor (B)`; visible when uncertainCount > 0 | `nextUncertain` | :212-217 |
| `Sávok` + hint `Ctrl+L` | `Beszélő-sávok elrejtése` / `Beszélő-sávok mutatása a javításhoz` | `railVisible` | :218-226 |
| `Visszavonás` + hint `Ctrl+Z` | tooltip `Visszavonás: %1` / `Nincs mit visszavonni` | `vm.undo()` | :227-234 |
| redo icon | `Újra: %1 (Ctrl+Shift+Z)` / `Nincs mit újra végrehajtani` | `vm.redo()` | :235-241 |
| search icon | `Keresés az átiratban (Ctrl+Shift+F)` / `Keresés bezárása` | | :242-247 |

### Overview (one 12 px row per speaker)
Columns: name (94/110 px, speaker ink colour, underlined on hover) . fingerprint mark . `TranscriptLaneStrip` (painted item, utterance segments on the meeting timeline; accent marks = "Megmutatom" suggestions) . talk share `NN%`. Click on name -> whole-speaker popover (tooltip `A teljes beszélő átnevezése vagy összevonása`); the `Egyéb (N)` row (colorIndex -1, muted) expands lanes (tooltip `%n keveset beszélő résztvevő — kattintásra külön sort kapnak`); `Keveset beszélők összecsukása` collapses (:381-398). Duplicate names get ` · <raw label>` suffix (:283-285). Semi-transparent rectangle = currently visible list range (:402-410). Click on timeline = `seekRequested(fraction)` -> scroll + `player.seek` (:412-418; TranscriptTab.qml:415-419).

Fingerprint mark states (Q/TranscriptToolbar.qml:312-360): `has` = solid `Theme.success` disc + white glyph, tooltip `Van hanglenyomata — kattintásra a részletek`; `none` = muted outline glyph, tooltip `Még nincs hanglenyomata — kattintásra itt készíthető`; `anonymous` = 40% opacity, not clickable, tooltip `Névtelen beszélő: hanglenyomat csak elnevezett beszélőhöz készíthető`. Click opens the VoiceprintPanel popover (TranscriptTab.qml:758-772).

---------------------------------------------------------------------------------------------------

## A3. Speaker rail (SpeakerRailHeader.qml, SpeakerRailCells.h, rail MouseArea in TranscriptTab.qml:538-651)

| Element | Description | Ref |
|---|---|---|
| Lane header | 24 px column per visible speaker: 20 px solid avatar in speaker colour; 5 px dot under it: solid `Theme.success` = person has voiceprint, hollow ring = named but none, hidden = anonymous. Click -> whole-speaker popover. Tooltip: `<name> (<raw>) · NN% · %n megszólalás · N-es billentyű` + `Van hanglenyomata.` / `Névtelen beszélő.` / `Nincs hanglenyomata.` | SpeakerRailHeader.qml:27-70 |
| `+N` group | collapsed rare speakers; tooltip `%n keveset beszélő résztvevő — kattintásra külön oszlopot kapnak`; expanded shows `chevrons-left-right`, tooltip `A keveset beszélők összecsukása` | :73-109 |
| `+` column | dashed circle, tooltip `Résztvevő hozzáadása`; opens PersonPicker (`addParticipant`) | :111-139 |
| Cell painting | one `SpeakerRailCells` per row: solid block in own lane; **uncertain** = soft fill + 135 deg hatch; **selected** = 2 px outline (Theme.text) + dashed drop targets in other lanes; **suggested** ("Megmutatom") = 2 px accent border; **drag source** = 30% opacity; **drag target** = soft fill + 2 px dashed border in target colour | S/SpeakerRailCells.h:3-9 |
| Click own block | select row | TranscriptTab.qml:621-622 |
| Click other lane | move that row (or whole selection) there: `moveRowToLane` | :623-624 |
| Click `+N` | toggle `lanesExpanded` | :625-626 |
| Click `+` in a row | select row, open picker `Új névtelen résztvevő` target "selection" | :627-629 |
| Drag (>7 px) | `moveRowsToLane(from,to,lane)`; floating accent chip `-> <name>`; edge auto-scroll 14 px / 30 ms | :597-606, :635-671 |
| Selection bar chips | see A5 | |

Colour per speaker: `Theme.speakerLine/Soft/Ink(colorIndex)`; `colorIndex` = order of first appearance, independent of edits (core/edit/SpeakerEditTypes.h:68).

---------------------------------------------------------------------------------------------------

## A4. Utterance row (Q/TranscriptRow.qml)

Row kinds: `utterance` or `gap` (`··· %n biztos sor elrejtve`, mono muted, in the uncertain filter; :52-70).

| Per-row data shown | Source role | Ref |
|---|---|---|
| Speaker name (speaker ink, semibold; clickable -> line popover; chevron when open; tooltip `Más mondta? Ennek a sornak a beszélője javítható` / `… A kijelölt sorok beszélőjének javítása`) | speakerName, head | :157-203 |
| Timestamp `mm:ss`/`h:mm:ss` mono (accent when playing/hover; tooltip `Lejátszás innen`) | timeLabel | :205-221 |
| Text; search hits as rich text (highlight `Theme.warnSoft`, current hit `warnLine`) | lineText/richText | :364-370 |
| Name header appears only on speaker change (`head`); extra top gap if not first | head/first | :139-151 |

### Row state markers
| Marker | Trigger | Look | Ref |
|---|---|---|---|
| Pill `bizonytalan` | `uncertain` (voice does not fit speaker) | `TPill` tone warn; tooltip `Hangra inkább %1 sorának tűnik` if `likelySpeakerName` set (recheck/analysis suggestion) | :223-234 |
| Pill `javítva` | `corrected` = `manuallyCorrected` (hand-moved) | tone success | :223-228 |
| Pill `egymásra beszéltek` | `noisy` (overlap rule or manual flag) | tone neutral; tooltip differs for overlap (`Ebben a sorban más is beszél egyszerre, ezért a hangját nem használom mintának …`) vs manual (`Megjelölted, hogy ezt a sort ne használjam hangmintának …`) | :236-251 |
| Ghost button `Mintának használható` | row noisy + hover | `setRowNoisy(row,false)`; tooltip `A sor hangja mégis mehet mintának` | :252-266 |
| Row background | playing = `accentSoft`; selected = `raised` + 1.5 px accent border | | :88-95 |
| Rail cell hatch / outline / accent | see A3 | | |
| Not exposed to QML | `confirmed` ("Jó így") and `rechecked` flags exist in core (`EditorUtterance`) but have no row marker; confirmed rows just lose `bizonytalan`; `rechecked` only surfaces as uncertain + `likelySpeaker*` | core/edit/SpeakerEditTypes.h:27-35, S/TranscriptListModel.cpp:40-47 | |

### Inline action row (only in "Bizonytalan" filter on uncertain head rows) (TranscriptRow.qml:269-361)
| Button | Hungarian | Action |
|---|---|---|
| lineListen | `Meghallgatom` (primary-ish, disabled w/o player) | `playLine(startMs,endMs)` |
| lineConfirm | `Jó így` (tooltip `A beszélő rendben van: a sor többé nem bizonytalan`) | `confirmRow` |
| lineConfirmNoisy | `Jó így, de nem minta` | `confirmRowNoisy` (confirm + noisy flag) |
| lineLikely | `%1 mondta` (only if `likelySpeakerKey`; tooltip `Hangra %1 sorának tűnik: a sor átkerül hozzá`) | `moveUtteranceToSpeaker` |
| lineFix | `Más mondta…` (tooltip `Csak ez a sor kerül át ahhoz, akit választasz`) | opens line popover |

---------------------------------------------------------------------------------------------------

## A5. Popovers, bars, picker

### A5.1 SpeakerPopover ("Ki mondta?", 340 px) (Q/SpeakerPopover.qml)
Opened from a row name / "Más mondta…" (scope line/selection) or from rail avatar / overview name (scope speaker).

| Part | Labels | Backend | Ref |
|---|---|---|---|
| Title | `Kinek a sora ez?` / `Kié a kijelölt %n sor?` / `%1 valójában…`; subtitle `Most: %1 · %2`, `A kattintott sor most: %1`, or `%1 megszólalás · hang alapján felismerve (NN%)` / `… · kézzel elnevezve` / `… · névtelen beszélő` | `speakerInfo` | :290-318 |
| Listen icon | tooltip `Meghallgatás: ez a sor` / `Meghallgatás: egy jellemző, hosszabb megszólalás ettől a beszélőtől` | `speakerSample` -> `player.playRange` | :261-284 |
| Scope radios (line origin only) | `Csak ez a sor`, `Kijelölt %n sor`, `%1 minden sora (%2)` | `setScope` | :323-355 |
| Search | `Név keresése vagy új személy`; Up/Down/Enter/Esc; Enter with empty box selects nothing | | :357-373 |
| `Ebben a megbeszélésben` list (line/selection scope) | rows: name + `%n sor` | `moveUtteranceToSpeaker` / `moveSelectionToSpeaker` | :213-254 |
| `Másik személy` list | sub-text `„alias” · összevonás / %n megbeszélés / nincs hanglenyomat`; row `Új személy: „…”`; row `Névtelen beszélő` (whole speaker, sub `%1 néven`) / `Új névtelen résztvevő` | `moveUtteranceToPerson`, `reassignSpeaker`, `revertSpeakerToAnonymous`, `moveUtteranceToNewParticipant` | :379-439 |
| `Összevonás ebben a megbeszélésben` (whole-speaker) | list of other speakers, sub `összevonás` | emits `mergeRequested` -> confirm dialog -> `mergeSpeakers` | :441-443, :213-254 |
| Checkbox (whole speaker, has voiceprint) | `A sorok kerüljenek ki %1 hanglenyomatából (téves felismerés)` (default checked) | `fixVoiceprints` flag of `reassignSpeaker`/`revertSpeakerToAnonymous` | :446-460 |
| **Pair recheck** (whole speaker, named, voice available, >=1 candidate) | button `Átnézés másik beszélővel…`, hint `Kivel keveredhettek össze %1 sorai?`, list of other named speakers (`%n sor`) | `editor.recheckPair(a,b)` | :462-523 |
| VoiceprintPanel (compact) | see A5.4 | | :527-536 |
| Line sample block (line scope, named) | `Hanglenyomat-minta ebből a sorból (%1 mp)` / `Jó így + hanglenyomat-minta ebből a sorból (%1 mp)`, reason text under it | `createVoiceprintFromLine` | :539-583 |
| `Üres oszlop eltávolítása` (dangerGhost) | for hand-added empty participant | `removeParticipant` | :586-602 |
| Footer | `%n sor kerül át · visszavonható: Ctrl+Z`, `Mind a/az %n sor átkerül · visszavonható: Ctrl+Z`; bulk (line popover with whole-speaker scope) in `warnSoft`/`warnInk` | | :605-628 |

### A5.2 PersonPicker (300 px) (Q/PersonPicker.qml)
Search `Név keresése vagy új személy`; rows: monogram, name, sub `„alias” · már résztvevő / %n megbeszélés / nincs hanglenyomat`, fingerprint icon; bottom row `Új személy: „…”` or `Névtelen résztvevő` / `Új névtelen résztvevő`; footer `A beszélő névtelenül is maradhat; később is elnevezheted.` Used for `+` (add participant) and for selection -> person (`addParticipant`, `moveSelectionToPerson`, `moveSelectionToNewParticipant`; TranscriptTab.qml:727-741).

### A5.3 TranscriptSelectionBar (dark bar, Q/TranscriptSelectionBar.qml)
`%n sor kijelölve` . `Áthelyezés:` . speaker chips with key number 1-9 (`moveSelectionToSpeaker`) . dashed chip `Új résztvevő…` (opens picker) . `Mégse` (`clearSelection`).

### A5.4 VoiceprintPanel (Q/VoiceprintPanel.qml)
Shown in popover from overview mark and embedded (compact) in SpeakerPopover. Header: name; state `Névtelen beszélő` / `Van hanglenyomata` / `Még nincs hanglenyomata`; detail text variants (anonymous: `Hanglenyomat csak elnevezett beszélőhöz készíthető…`; unsupported: `A megbeszélés lekevert hangja nem érhető el…` or `Nincs letöltve a hangmodell…`; usable lines + seconds; shortfall `Még kb. %1 mp tiszta beszéd kellene…`). Button `Hanglenyomat készítése` / `Új minta készítése` (compact: `Készítés` / `Új minta`) -> `editor.createVoiceprint`; result line `Elkészült %n sorból, %1 mp beszédből.` (successInk) or error (dangerInk); `Visszavonás` -> `removeVoiceprint(printId)` (deletes exactly that print); `A most készült hanglenyomat törölve.` Voiceprints are made ONLY on explicit click (file comment :5-8).

### A5.5 TranscriptChangeBar (dark notification bar under list) (Q/TranscriptChangeBar.qml)
Auto-dismiss after 12 s (timer stops while hovered or while an offer awaits an answer; :89-99). Message from VM e.g. `%n sor átkerült ide: %1`, `%1 mind a/az %n sora átkerült ide: %2` (S/TranscriptEditorViewModel.cpp:663,1158,1285), `A most készült hanglenyomat törölve: %1` (:1341), `Hanglenyomat készült: …` (:1725).

| Button | Label | Backend | Ref |
|---|---|---|---|
| changeUndo | `Visszavonás` (tooltip `Az átsorolás visszavonása (Ctrl+Z)` or `A most készült hanglenyomat törlése (az elnevezés marad)`) | `undoChange` | :137-147 |
| changeVoiceprint | `Hanglenyomat készítése` (only after a WHOLE speaker got a name, person has no print, enough material) | `createVoiceprintFromChange` | :148-157 |
| changeSimilar | `Hasonló %n sor is` | `acceptSuggestion` | :158-167 |
| changeShow | `Megmutatom` / `Elrejtem` | `suggestionShown` | :168-174 |
| changeRest | `<Forrás> mind a/az N sora` | `restRequested` -> merge dialog -> `moveRestOfSource` | :175-183 |
| changeClose | x `Bezárás` | `dismissChange` | :184-189 |
| Pair offer row (2nd line) | text `%1 és %2 hangja hasonló. Nézzem át kettejük sorait a megerősítettek alapján?`; buttons `Átnézés` (-> `acceptPairOffer` = `recheckPair`) and `Most nem` (`declinePairOffer`, not asked again this session) | | :194-254, VM.cpp:683 |

After a pair recheck the VM posts a notice (toast): `%n kétséges sor %1 és %2 között — a Bizonytalan szűrőben.` / `A megerősített sorok alapján nem találtam kétséges sort …` + optional caveats `Kevés megerősített sor (…)` and `A két hang nagyon hasonló (…), az eredmény bizonytalan — hallgass bele.` (VM.cpp:708-723). Uncertain filter auto-enabled when flagged > 0.

### A5.6 Recheck-from-confirmed (whole meeting) - entry points
| Where | Label | Ref |
|---|---|---|
| Toolbar `Bizonytalan` chip when 0 uncertain | (refresh icon) | TranscriptToolbar.qml:191-204 |
| Empty filter view button | `Beszélők újraellenőrzése…` | TranscriptTab.qml:684-696 |
| MeetingHeader "..." menu | `Beszélők újraellenőrzése…` | Q/MeetingHeader.qml:148-154 |
| `Résztvevők azonosítása` fallback when everyone is already named | confirm dialog `Mindenki azonosítva` | S/ShellActions.cpp:620-626 |
Flow: `ShellActions::recheckSpeakers` -> `runRecheck` (confirm `Újraellenőrzés`, then `SpeakerEditor::recheckFromConfirmed()`), toast `%n kétséges sort jelöltem meg — a Bizonytalan szűrőben találod.` / `A megerősített sorok alapján nem találtam kétséges sort.` + reference summary (S/ShellActions.cpp:650-690 (runRecheck :657)). Blocker sentences (core/src/edit/SpeakerEditor.cpp:1528-1541): no transcript; `Az újraellenőrzéshez nincs telepítve a hangmodell.`; `A sorok hang-elemzése még fut …`; `… még nem készült el …`; `Előbb erősíts meg vagy javíts legalább 3 sort …`.

---------------------------------------------------------------------------------------------------

## A5.7 TranscriptEditorViewModel -> backend map (S/TranscriptEditorViewModel.h)

| VM member | Core call / signal |
|---|---|
| `meetingId` -> session | `AppController::speakerEditor(id)`; `SpeakerEditor::reloaded/speakersChanged/utterancesChanged/suggestionChanged/embeddingProgress/embeddingFinished/recheckFinished` (VM.cpp:200-238) |
| `startEmbeddingIfNeeded` | `SpeakerEditor::startEmbedding()` (only if `embeddingsSupported && !complete && !running`) (VM.cpp:~311, `startEmbeddingIfNeeded`); `detach()` calls `cancelEmbedding()` (VM.cpp:250-253) |
| `moveRowToLane/moveRowsToLane/moveSelectionTo*/moveUtteranceTo*` | `SpeakerEditor::moveUtterances…` via `moveLines` |
| `addParticipant/removeParticipant/reassignSpeaker/revertSpeakerToAnonymous/mergeSpeakers` | same-named SpeakerEditor ops |
| `confirmRow/confirmRowNoisy/setRowNoisy/setUtteranceNoisy` | `confirmUtterances(ids,asNoisy)`, `setUtterancesNoisy` |
| `recheckSpeakers()` | `canRecheck()`, `recheckBlocker()`, `recheckFromConfirmed()` (VM.cpp:1446-1466); `canRecheck/recheckBlocker` mirrored in `updateRecheckState` |
| `recheckPair/acceptPairOffer/declinePairOffer/pairCandidates` | `SpeakerEditor::recheckPair`, `pairOffer`, `declinePairOffer` |
| `acceptSuggestion/dismissSuggestion/suggestionShown` | `SpeakerEditor::acceptSuggestion/dismissSuggestion` |
| `voiceprintMaterial/createVoiceprint/createVoiceprintFromLine/lineSampleInfo/removeVoiceprint` | `SpeakerEditor::voiceprintMaterial/createVoiceprint/createVoiceprintFromLines/removeVoiceprint` |
| `speakerSample` | picks a longer non-uncertain line, trimmed to ~12 s |
| `undo/redo/undoText/redoText` | `SpeakerEditor::undo/redo` (200 steps max, SpeakerEditor.cpp:36) |
| `copy*`, `selectionText` | QClipboard |
| `overview` | per-speaker `{key,name,colorIndex,pct,voiceprint,segments,marks}`; low-talk speakers folded into `Egyéb (N)` (VM.cpp:547) |
| Models | `TranscriptListModel` (fine-grained dataChanged, never resets for one move; S/TranscriptListModel.h:3-9), `TranscriptLaneStrip` and `SpeakerRailCells` are `QQuickPaintedItem`s (one painted item instead of hundreds of rectangles; software-renderer safe) |
| Not read by any QML | `SideAnalysis`, `TrackActivity`, `AppController::voiceEmbedderSet/activeVoiceModelIds`, `voiceModelsChanged` |

---------------------------------------------------------------------------------------------------

## A6. Tracks tab (Q/TracksTab.qml, Q/TrackRow.qml, S/TrackListModel.*)

Purpose: recording's audio tracks (friendly name + raw device name, waveform, state), mixdown row, permanent deletion of dropped tracks.

| Element | Labels / states | Backend | Ref |
|---|---|---|---|
| Track row | play button (tooltips `Sáv meghallgatása` / `Előnézet leállítása` / `A hangfájl hiányzik`); source icon (`iconName`: mic/call/system/mic2 etc.); name (double-click or pencil `Sáv átnevezése` -> inline edit, hint `Enter: mentés · Esc: mégse · üresen: „%1”`); raw device name mono; waveform; duration mono | `tracks.rename`, `player.playFile/stopPreview` | TrackRow.qml:71-210 |
| Pill | `hiányzik a fájl` (danger) / `eldobott · csendes` (neutral) | | TrackRow.qml:110-115 |
| Row actions | `Megkeresés…` (missing file -> native picker `shell.pickAudioFile` -> `tracks.relocateTrack`; toast `A sáv hangfájlja a megbeszélés mappájába került.`) / `Visszaállítás` (dropped -> `tracks.restore`) | TracksTab.qml:43-51 | |
| Waveform | peaks scaled to the loudest track (`peakReference`); positioned on the meeting timeline (`waveStart/waveSpan`); pulsing opacity while `peaksState==loading`; flat line if missing/dropped w/o peaks; coloured `Theme.speakerLine(colorIndex)` (muted when dropped) | `requestWaveforms` | TrackRow.qml:155-187 |
| Empty | `Ehhez a megbeszéléshez nem tartozik hangsáv.` | | TracksTab.qml:131-137 |
| Mixdown row | title `Lekeverés` (+ warn pill `elavult` when stale); sub-text per state: `nincs aktív sáv, amiből készülhetne`, `a sávok változtak azóta; a lejátszó még a régit szólaltatja meg`, `még nem készült el; ebből szól a lejátszó és ebből készül az átirat`, `%n aktív sávból; …`; running: progress bar + `NN%` (indeterminate if <0) + `Megszakítás` / `Megszakítás…` (cancellable) or `az átírás részeként`; ready/stale: mixdown waveform (60% opacity when stale) + duration; buttons `Lekeverés frissítése` / `Lekeverés készítése` (pauses player first) | `tracks.refreshMixdown`, `shell.cancelJob(.., JobKinds.Mixdown)` | TracksTab.qml:139-260 |
| Dropped tracks footer | `Az eldobott sávok megmaradnak, amíg végleg nem törlöd őket.` + `Eldobott sávok végleges törlése` -> confirm `Végleg törlöd az eldobott sávokat?` / `Végleges törlés`; toast `%n eldobott sáv törölve.`; guard `Közben másik megbeszélésre váltottál, ezért semmi nem törlődött.` | `tracks.deleteDroppedIn` | TracksTab.qml:52-68, 262-283 |
| Before transcript | Tracks tab reachable from the pre-transcript view with `Vissza az előkészítéshez` and note `A felvétel sávjai — az átírás előtt is visszaállíthatsz eldobott sávot.` | | Main.qml:567-586 |
| Demo rows | Demo track names `Saját mikrofon`, `Hívás hangja`, `Rendszerhang`, `Második mikrofon` (sample data only) | | S/TrackListModel.cpp:568-579 |

NOTE: the Tracks tab shows NO energy/activity, no mic-vs-loopback "side" information per utterance, and does not use `TrackActivity`; waveform peaks come from `WaveformService` (own cache `<audio>.peaks.json`).

---------------------------------------------------------------------------------------------------

## A7. PlayerBar (Q/PlayerBar.qml, S/PlayerController.*)

52 px bar: round play/pause (`Lejátszás (Szóköz)` / `Szünet (Szóköz)` / `Ehhez a megbeszéléshez nincs lejátszható hang`), clock `mm:ss / mm:ss` (mono), seek slider (`Pozíció`, 5 s keyboard step), speed button (menu 0.75x, 1x, 1.25x, 1.5x, 2x; tooltip `Lejátszási sebesség`), volume button -> popover with mute toggle (`Némítás` / `Némítás feloldása`) and slider (`Hangerő`). PlayerController: loads mixdown (fallback: largest active track), lazily creates audio engine, `positionMs` ~25x/s, `playRange` (one utterance, then pause), `playFile` preview (`previewPath`) (S/PlayerController.h:3-18, Q/PlayerBar.qml:12-134). The editor treats preview as "inactive" (TranscriptTab.qml:61).

---------------------------------------------------------------------------------------------------

## A8. MeetingHeader (Q/MeetingHeader.qml)

Title (double-click or F2 -> inline edit, Enter commit, Esc cancel; `A megbeszélés címe`), meta line (e.g. date . duration . `N beszélő`), button `Résztvevők azonosítása` (only with transcript; disabled while identify runs; tooltip `A névtelen beszélők párosítása az ismert hanglenyomatokkal. Megszakítható, bármikor újrafuttatható.`; -> `shell.identifyParticipants`), `...` menu (`További műveletek`): `Átnevezés` (F2), `Megnyitás mappában`, `Exportálás archívumba…`, `Beszélők újraellenőrzése…`, `Újra-átírás…`, separator, `Törlés…` (danger). Under it the tag row (A10). Identify job progress is shown in `TaskStrip`/`JobStageRow` as `3 / 5 beszélő` style (S/ShellActions.cpp:600 comment).

---------------------------------------------------------------------------------------------------

## A9. SummaryTab (Q/SummaryTab.qml, S/SummaryViewModel.*)

`SummaryViewModel.view` selects one of three loaders (SummaryTab.qml:34-50); refreshed on tab show and on app re-activation (gating after Settings).

| View | Content | Key labels | Ref |
|---|---|---|---|
| `empty` (SummaryEmptyView) | two option cards: `Gyors összefoglaló` (button `Összefoglaló készítése`; note `rövid megbeszélésnél kb. 1 perc; hosszabbnál részenként halad`), `Témánkénti elemzés` (`Témák javaslása` / `Témák megnyitása`); meeting note editor `Megjegyzés a megbeszéléshez`; provider line `Szolgáltató: %1 · …`; running state with stage list and `Megszakítás`/`Megszakítás…`; cloud teaser `Nem akarsz kulcsokkal bajlódni? A Tanara Cloud hamarosan jön.` + `Érdekel`; blocked state shows `blocker.reason` | | Q/SummaryEmptyView.qml:103-306 |
| `summary` (SummaryDocView) | stale banner (warn): `Az összefoglaló óta %n beszélőt javítottál, ezért a felelősök és a résztvevők elavultak lehetnek.` + `Frissítés`/`Témák újraelemzése` + `Rendben így` (`dismissStale`); segmented `Vezetői összefoglaló` / `Memó · %n szakasz`; sections `Döntések`, `Nyitott kérdések`, `Teendők` (owner chips coloured by speaker), `Témák`, `Tartalom` (TOC when >= 6 memo sections), timestamps -> `shell.seekTo`; right column `Résztvevők` with talk-share; note block; buttons `Témánkénti elemzés`, `Újragenerálás`, `Másolás` (menu `Vezetői összefoglaló`/`Memó`/`Mindkettő`; toast `…a vágólapra került.`), `Megnyitás mappában`; soft hint `A megjegyzés azóta változott; újrageneráláskor már az új számít.`; old summaries: `Ehhez az összefoglalóhoz nincs memó` + `Újragenerálás` | | Q/SummaryDocView.qml:204-790 |
| `topics` (SummaryTopicsView + TopicCard) | header `Vissza`/`Összefoglaló`, `Témák` + counts, `Téma hozzáadása`, primary `Hiányzók elemzése` / `Összegzés készítése`; cards: drag handle (`Húzd az átrendezéshez (Ctrl+↑ / Ctrl+↓)`), state pill `kész`/`fut`/`sorban áll`/`hibás`/`vár`, `Téma elemzése`/`Téma újraelemzése`, edit/delete (delete confirms `Törlöd a témát?`), `Teljes elemzés`/`Kevesebb`, inline error with `Betöltés nagyobb kontextussal`/`Újra`, queued: `Megszakítás` | | Q/SummaryTopicsView.qml:63-251, Q/TopicCard.qml:83-391 |
| Status banners (SummaryStatusBanners) | progress `Folyamatban…` + `Megszakítás`; error card `Az összefoglaló legutóbb nem készült el` with `Betöltés nagyobb kontextussal`, `Folytatás`/`Újra`, `Rendben` | | Q/SummaryStatusBanners.qml:55-125 |

Summary-staleness is driven by speaker edits made in the editor (`SpeakerEditor::summaryStale`, `dismissSummaryStale`); the editor itself has no stale indicator (only the tab pill in Main.qml:552 and the banner here).

---------------------------------------------------------------------------------------------------

## A10. Meeting tags: TagRow, TagField, TagInput*, TagChip, TagWhyPopover (S/MeetingTagsModel)

| Component | Behaviour | Ref |
|---|---|---|
| `TagRow` (in MeetingHeader, 26 px, never wraps) | applied chips (`#name`, hover `x`), `+N` overflow, `+ Címke` button (tooltip `Címke hozzáadása (T)`; key T when row focused) opening `TagInput`; suggestions block: label from model (`Javasolt` / `<Címke> mellé gyakran` / `Az összefoglaló alapján`), <= 2 suggestion chips (tooltip `Kattintás: hozzáadás · ×: nem illik ide`), `+N`, `Mind` (tooltip `Az összes javaslat a „Miért?” alatt`), `Miért?` (opens TagWhyPopover); computing state `Javaslatok készülnek…`; chip click -> `shell.filterByTag` | Q/TagRow.qml:65-366 |
| `TagChip` kinds | `applied` (raised, borderStrong), `suggested` (dashed accent, `+`), `llmNew` (dashed accent, sparkle + `ÚJ`), `overflow` (`+N`), `partial` (dashed, `1/3`) | Q/TagChip.qml:4-15, :115 |
| `TagInput` + `TagInputList` | placeholder `Címke…`; list header `Legutóbb használt` / `Hasonló már van`; rows with count; `Új címke: „…”` / `Mégis új: „…”`; hint `↑↓ választás · Enter hozzáadás · Esc bezárás`; Backspace on empty removes last tag | Q/TagInput.qml, Q/TagInputList.qml:23-118 |
| `TagField` | multi-chip field `Címke hozzáadása…` used in pre-transcript step 1, import dialog, bulk tagging panel (partial chips) | Q/TagField.qml:3-14 |
| `TagWhyPopover` (420 px) | `Miért ezek?`; per suggestion `Hozzáadás` / `Nem illik ide`; reasons `Közös résztvevő`, `Közös kifejezések`, `Hasonló cím`; `Hasonló megbeszélés` links open that meeting; footer `Az elutasított javaslatot ennél a megbeszélésnél nem ajánljuk újra.` | Q/TagWhyPopover.qml:21-185 |
| `MeetingTagsModel` | `add/remove/accept/reject/acceptAll/undo/requestSuggestions/whyData`; Ctrl+Z routed to tag undo when `tagUndoActive` (Main.qml:285-289) | S/MeetingTagsModel.h:56-82 |

---------------------------------------------------------------------------------------------------

## B. People window (Q/PeopleWindow.qml + PeopleListPane/Row, PeopleDetailPane, PeopleSampleRow, PeopleMergeDialog, PeopleSampleTargetDialog, PeopleUndoToast; S/PeopleViewModel, PeopleListModel, PeopleWindowHost)

Purpose: manage known people (names, aliases, notes, voice samples = voiceprints, meetings). Separate non-modal window 960x660 (min 800x560), resizable list pane (240-460 px, drag the 1 px splitter) (PeopleWindow.qml:32-43, 149-168). Opened via `Fájl > Személyek…`, Settings General `Személyek kezelése`, `shell.openPeople(person)`.

### Window-level
| Item | Detail | Ref |
|---|---|---|
| Title | `Személyek` | PeopleWindow.qml:34 |
| Shortcuts | Ctrl+F search; F2 rename; Del delete (confirm); Space play selected sample; Ctrl+Z undo (not in text fields); Ctrl+W close; Esc does NOT close | :130-136 |
| Toast (`PeopleUndoToast`) | dark bar, ~8 s, stays while hovered; optional `Visszavonás` button | PeopleUndoToast.qml |
| Toast texts | `Átnevezve: %1`, `Becenév törölve: %1`, `Minta törölve[: %1]`, `Minta áthelyezve ide: %1`, `Új személy a mintából: %1`, `Összevonva: %1 — %n megbeszélés, %n minta`, `Törölve: %1.`/`…A mintái itt maradtak: %1`, undo confirmations (`A minta visszakerült.` …), `A minta hangfájlja már nincs meg, ezért nem hallgatható meg.`, and `%n minta készült.`/`Nem készült minta.` | S/PeopleViewModel.cpp:614-883 |
| Undoable | rename, alias removal, sample removal/move, delete (kind `SampleRemoved/SampleMoved/AliasRemoved/Renamed`...); **merge is NOT undoable** (dialog says so) | PeopleViewModel.cpp:756, 828-840 |

### List pane (PeopleListPane.qml)
| Element | Label | Backend | Ref |
|---|---|---|---|
| Search | placeholder `Név vagy becenév` (accent-folded, matches aliases; hit highlighted `warnSoft`); Enter selects first; Down -> list | `vm.query` | :29-46 |
| `user-plus` button | tooltip `Új személy` -> dialog | | :47-55 |
| Count | `%n személy` / `%n találat` | | PeopleViewModel.cpp:554 |
| Sort menu | `ABC`, `Legutóbb`, `Legtöbb megbeszélés` | `vm.sort` abc/recent/meetings | :101-108 |
| Row | 28 px monogram (accent for self), name (+ pill `te`), fingerprint icon (green `successInk` = has samples, `minus` = none) + meta `„alias” · %n megbeszélés · %n minta` / `nincs hanglenyomat`; section header (`Te`, `Többiek`, or letter) | `PersonListModel` roles | PeopleListRow.qml; VM.cpp:248-253, 311 |
| Empty (only self) | dashed card `Itt jelennek meg a résztvevők` + `Amikor az átiratban elnevezel egy beszélőt, személy lesz belőle, és a következő megbeszéléseken hangja alapján felismerjük.` | | :164-193 |
| No result | `Becenevekben is kerestem.` + link `Új személy: „%1”` | | :195-236 |
| Keys | Up/Down move selection | | :148-156 |

### Detail pane (PeopleDetailPane.qml)
| Element | Detail | Backend | Ref |
|---|---|---|---|
| Empty state | `Még csak te vagy itt` + text + button `Új személy` | | :93-129 |
| Header | avatar, name (title size), meta `Utoljára: … · %n megbeszélés · … beszéd` / `Megbeszélések számolása…` / `Még egy megbeszélésen sem szerepel` | `detailMeta` | :148-191; VM.cpp:323-328 |
| `Átnevezés` | inline field + `Mentés`/`Mégse`, hint `Enter: mentés · Esc: mégse. A régi név becenévként megmarad.`; error in dangerInk | `vm.rename` | :193-264 |
| `Összevonás…` | only when > 1 person | opens merge dialog | :265-276 |
| trash icon | tooltip `Személy törlése`; absent for self | opens delete dialog | :277-287 |
| `Becenevek` | chips with remove x (`Becenév törlése: %1`), `Becenév` chip -> inline add (Enter keeps open, Esc closes), hint `A keresés és a személyválasztó ezekre is talál.` | `addAlias/removeAlias` | :296-405 |
| `Megjegyzés` | text area `Megjegyzés hozzáadása…`, saved on focus loss / person change / window close; Esc reverts | `setNote` | :407-433 |
| `Hanglenyomat` + sub-text | e.g. `%n minta · %n forrásból` | `samplesSubText` | :437-450; VM.cpp:393 |
| Sample list (5 shown; link `Az összes minta (%1)` / `Csak a legutóbbiak`) | `PeopleSampleRow`: play/pause disc, source icon (`mic`/`call`/`speaker`... by `sourceKind`), device label (`A megbeszélés lekevert hangja`, `ismeretlen eszköz` fallback), meeting title (`A megbeszélés már nincs meg` fallback), date, length mono, `...` menu: `Meghallgatás`/`Szünet`, `Új személy ebből a mintából…`, `Áthelyezés másik személyhez…`, `Minta törlése` (danger). Playing row = `accentSoft`; selected/menu-open = `raised` | `toggleSample/removeSample/moveSample`; playback through its own `PlayerBackend` (own timer, `playEndMs`) | :453-523; PeopleSampleRow.qml |
| No-voiceprint box (dashed) | `Még nincs hanglenyomata`; plan text computed lazily after stats are ready (`Megbeszélések átnézése…` while pending); variants: no lines / lines too short (`megbeszélésenként legalább 15 másodpercnyi kell, 3 másodpercnél hosszabb sorokból`) / model missing (`A hangfelismerő modell nincs telepítve ezen a gépen, ezért most nem készíthető hanglenyomat.`) / possible. Button `Minta %n megbeszélésből` + note `kb. %1 hang, a gépen marad[ · %n megbeszélésen nincs elég hosszú sor]`. While creating: spinner `Minta készítése: %1 / %2 megbeszélés…` | `vm.createVoiceprint()` -> `PeopleService::voiceprintPlan` / `createVoiceprintFromMeeting`, one meeting per 30 ms timer tick (UI-thread, per-meeting step) | :525-598; VM.cpp:405-518 |
| `Megbeszélések` | up to 3 rows (title, date mono, talk time mono), link `Az összes megbeszélés (%1)`; rows are not clickable in this version; `Megbeszélések számolása…` until stats arrive | `meetings` | :601-723 |

### Dialogs
| Dialog | Labels | Backend | Ref |
|---|---|---|---|
| New person | title `Új személy`, note `Előre felvett személy: a személyválasztóban már szerepel, hanglenyomata az első hozzárendelés után lehet.`, field `Név`, error `Adj meg egy nevet.`, `Mégse`/`Hozzáadás` | `addPerson` | PeopleWindow.qml:240-282 |
| Delete | `%1 törlése`; consequence text from `deleteText(keepSamples)`; checkbox `Hangminták megtartása névtelen személyként` (only if has samples); `Mégse`/`Törlés` (danger) | `deletePerson` | :197-238; VM.cpp:785-821 |
| Merge | `%1 összevonása`; `Ha ugyanaz az ember kétszer szerepel. A megbeszélések, minták és becenevek egy személyhez kerülnek.`; search `Kivel vonod össze?`; candidates sorted by voice similarity, meta `… · hang alapján hasonló (NN%)` (>=50) / `hang-egyezés: NN%`; radios `Megmaradó név`; result rich text `Eredmény: <b>…</b>, … „x” becenév lesz. … Az összevonás nem vonható vissza.` (+ `%n érintett összefoglaló elavultnak jelölődik.`); `Mégse`/`Összevonás` (hidden until a candidate is chosen) | `mergeCandidates/mergeResultText/merge` (`PeopleService::similarity`) | PeopleMergeDialog.qml; VM.cpp:700-772 |
| Sample target | `Minta áthelyezése` (list) / `Új személy ebből a mintából` (name); texts `A minta kikerül a mostani hanglenyomatból…`; existing-name hint; buttons `Mégse`, `Áthelyezés`/`Új személy`; errors incl. `A minta most is ennél a személynél van.` | `personChoices`, `moveSample` | PeopleSampleTargetDialog.qml:39-115 |

Data facts: the list appears immediately; meeting counts, talk time and "last seen" arrive later from `PeopleStats` (background thread, per-meeting cache keyed by mtime+size) -> `statsReady` (S/PeopleViewModel.h:7-13; core/people/PeopleStats.h:3-14). Samples show NO model id; one sample can have several sibling prints (one per enabled voice model) but the UI lists them as one sample (S/PeopleViewModel.cpp:337-360; core/Types.h:178-189).

---------------------------------------------------------------------------------------------------

## C. Settings window (Q/SettingsWindow.qml + pages; S/SettingsViewModel, SettingsProviderModel, SettingsEmbeddingModel, SettingsDeviceModel, SettingsCloudModel)

Window `Beállítások` 900x680 (min 760x560), non-modal. Draft model: edits go to a draft copy; `Mentés` applies only the diff on top of current core settings; `Mégse` discards; theme previews live but is persisted on save (S/SettingsViewModel.h:3-14). Secrets go to KeyStore, not settings.json.

| Item | Detail | Ref |
|---|---|---|
| Navigation (208 px) | `Általános`, `Rögzítés`, `Hívásfigyelő`, `Szolgáltatások` (warn dot + tooltip `Egy szolgáltató beállítása hiányzik vagy nem érhető el`), `Összefoglaló`; version label `Tanara <ver>` bottom-left | SettingsWindow.qml:104-152 |
| Footer | dirty dot (warn) + `Nincs mentetlen változás` / `%n nem mentett változás` / `Tanara Cloud kiválasztva · mentéskor átvált` / `Elmentve` (2.5 s); `Mégse`; `Mentés` (primary, disabled when clean) -> label `Mentés és újraindítás` when embedding model change pending | :235-262; S/SettingsViewModel.cpp:535-543 |
| Shortcuts | Ctrl+S (when dirty) save; Ctrl+W close request; Esc not bound | :99-102 |
| Close with changes | dialog `Mented a változásokat?` / `%n nem mentett változásod van a beállításokban.` / `Mégse`, `Elvetés`, `Mentés` | :277-296 |
| Other dialogs | `Visszaállítod az alapértelmezett utasítást?` (prompt reset, `Visszaállítás`), `Visszaállítod az elutasított javaslatokat?` (`%n elutasított javaslatot …`), `Kijelentkezel a Tanara Cloudból?` (`Kijelentkezés`), `Kimeneti forma` (read-only schema, `Bezárás`) | :305-402 |
| Deep link | `shell.openSettings(page, focusField)`; focusField `stt`/`llm` highlights the missing provider card and returns to the meeting after save | Q/CONTRACT.md |

### C1 General (SettingsGeneralPage.qml)
Section `Te`: `Saját neved` text field (error from `vm.errors.userName`, hint `Így jelensz meg az átiratokban; a saját hanglenyomatod ehhez a névhez tartozik.`); voiceprint status row (fingerprint icon `successInk` when has; text `Hanglenyomat: %n minta, …` / `Ehhez a névhez még nincs hanglenyomat.` from `VoiceprintStore::printsFor(userSpeakerName)`, S/SettingsViewModel.cpp:599-615) + link `Személyek kezelése` (opens People). Section `Megjelenés`: `Nyelv` (applies on next start), `Téma` segmented `Rendszer`/`Világos`/`Sötét`. Section `Mappák`: three cards `Felvételek`, `Jegyzetek`, `Belső adatok` with path (mono), disk usage computed on a worker (`QThreadPool::globalInstance`, generation-guarded; text `… · %n megbeszélés` / `még nincs ilyen mappa — mentéskor létrejön`), `Tallózás…`, open-in-file-manager button (hidden when path pinned by TANARA_HOME). Footer link `Az első indításkor látott bevezető:` `Első lépések` (opens Onboarding). Refs: SettingsGeneralPage.qml:17-290; S/SettingsViewModel.cpp:660-720.
No voice-model controls anywhere on this page.

### C2 Recording (SettingsRecordingPage.qml, S/SettingsDeviceModel)
`Alapértelmezett források` (explanatory text; empty state `Nem találtam hangeszközt. Csatlakoztass mikrofont, vagy ellenőrizd a rendszer hangbeállításait.`); device list in three groups as in the recorder: toggle (tooltips `Alapból sávot kap` / `Alapból nem kap sávot`), friendly + raw name, `alapért.`, 14-segment live level meter with peak hold (monitoring only while this page is visible: `Binding monitoring`, SettingsWindow.qml:96), rename (`Átnevezés` / `Átnevezés (eredeti név: %1)`). Switch `Minden eszköz rögzítése automatikusan` (+ helper). `Hangminőség` segmented (options/hint from VM). `Lekeverés` radios `Automatikusan, a felvétel után` / `Kézzel, a Sávok fülön`.

### C3 Watcher (SettingsWatcherPage.qml)
Main switch `Hívásfigyelés` with live line `Most: <b>%1</b> használja a mikrofont` / `Most: egyik figyelt alkalmazás sem használja a mikrofont` / `Most: az észlelés ezen a gépen nem érhető el` (polling only while page visible: `watching` binding); `Induljon el a bejelentkezéskor`; `Kérdezzen rá, ha véget ért a megbeszélés` with stepper `Csend után:` (`kikapcsolva` / `%n perc`); `Figyelt alkalmazások` chips (x remove `%1 eltávolítása`, currently-using app highlighted `Most ez az alkalmazás használja a mikrofont`); `Alkalmazás hozzáadása` panel (`Folyamatnév vagy annak egy része`, `Most futó alkalmazások` list, `Hozzáadás`); collapsible `Haladó`: `Ellenőrzés gyakorisága:` stepper (`%n mp`).

### C4 Services (SettingsServicesPage.qml, SettingsProviderCard, SettingsProviderFields, SettingsEmbeddingCard, SettingsCloudPanel, SettingsWaitlistPanel, SettingsStatusPill)
| Block | Detail |
|---|---|
| Mode cards | `Saját kulcs` (caption `A te fiókod a szolgáltatóknál; nekik fizetsz közvetlenül.`) / `Tanara Cloud` (caption `Bejelentkezés és egyenleg; nincs kulcskezelés.`, pill `hamarosan` in teaser build) |
| Provider cards | `Átírás` (`beszédből szöveg (STT)`) and `Összefoglaló` (`nyelvi modell (LLM)`): collapsible when configured (header shows status), dropdown `Szolgáltató` (`Válassz szolgáltatót…`), data-driven fields from `ProviderDescriptor.fields` (text/url/secret with eye toggle `Kulcs megmutatása`/`Kulcs elrejtése`/number/list with `Lekérés`), LLM-only: `A modell gondolkodása az összefoglalónál` (`Automatikus (kikapcsolva)`, `Kikapcsolva`, `Bekapcsolva`), `A modell kontextusa`; `Kapcsolat tesztelése` -> `SettingsStatusPill` (`Kapcsolódva · N ms` success / `Nem érhető el` danger / `Tesztelés…`); `Haladó`; Cloud mode: `Ezt a lépést a Tanara Cloud végzi: nincs kulcs, a fiókod egyenlegéből megy.` |
| `Beágyazás` card (`címkejavaslatokhoz (embedding)`) | mode None (default, neutral) / Helyi végpont (address, model + `Lekérés`, `Haladó` key, `Kapcsolat tesztelése`) / Tanara Cloud; model-change warning before save; `Könyvtár előkészítése` states: pending (`Mentés után indul: …`), running (count, ETA, `Megszakítás`, `Közben dolgozhatsz; …`), error (`Folytatás`), done (`Újraelőkészítés`), idle (`%1 előkészítve`, `Előkészítés`) -> `core EmbeddingPreparer` |
| `Címkejavaslatok` switches | `Címkék javaslása` (`Hasonló korábbi megbeszélések és együtt járó címkék alapján. Sosem kerül fel magától.`), `A nyelvi modell is javasoljon az összefoglaló után`, `Elutasított javaslatok: %1` + `Visszaállítás` (immediate `TagService::clearRejected`, confirmed by dialog) |
| Cloud panel (live) | account row (`Nincs bejelentkezve` / email; `Bejelentkezés`/`Kijelentkezés`; `Részletek`), `Egyenleg` (pill `kevés`/`elfogyott`, refresh), `Minőség` (`Gyors`/`Pontos`, `Expert mód…`), `Átírás`/`Összefoglaló` tier cards, `A megbeszélések nyelve`, switch `Költségbecslés minden feldolgozás előtt`, links `ÁSZF megtekintése…`, `Napló a weben`, `Fiókom a weben` |
| Waitlist panel (teaser) | `Tanara Cloud` + pill `hamarosan`, email form, `Értesítést kérek`, `Adatkezelési tájékoztató`; sends data only on button press |
| Footnote | keys stored in internal data folder, readable only by the user (SettingsServicesPage.qml:243) |
No voice-model enable/disable/download appears here.

### C5 Summary (SettingsSummaryPage.qml)
`Összefoglaló nyelve` free text (placeholder `pl. magyar`, `Bármilyen nyelvet beírhatsz; független a felület nyelvétől.`); five prompt tabs in two groups with `módosítva` marker, `Alapértelmezett` reset button (`Visszaállítás a beépített utasításra`), monospace editor with line numbers and `{{VARIABLE}}` highlighting (`SettingsPromptHighlighter`), legend, `Kimeneti forma:` `Megtekintés` (read-only schema).

---------------------------------------------------------------------------------------------------

## D. Tags window (Q/TagsWindow.qml, TagsListPane, TagsListRow, TagsDetailPane, TagsMergeDialog; S/TagsViewModel, TagListModel, TagBackend/TagControllerBackend)

`Címkék` window 960x660, same structure as People. Shortcuts: Ctrl+F search, F2 rename, Del delete (confirm), Ctrl+Z undo, Ctrl+W close (TagsWindow.qml:85-89). Reuses `PeopleUndoToast` (`tagsToast`). All edits are undoable backend steps (`beginGroup`/`step` in `TagService` backend).

| Element | Labels | Ref |
|---|---|---|
| List | search `Címke keresése`; count `%1 címke` / `%1 / %2 címke`; sort `Legutóbb használt`, `ABC`, `Leggyakoribb`; row `#name` + `%1 megbeszélés · <date>`; empty `Itt jelennek meg a címkék, amint az első megbeszélésre felkerül egy.`; `Nincs ilyen címke.` | TagsListPane.qml:27-165; S/TagsViewModel.cpp:139-171 |
| Detail empty (T13) | `Még nincs címke` + `Címkét a megbeszélés fejlécében, a felvevőben vagy importkor adhatsz. …` | TagsDetailPane.qml:65-73 |
| Header actions | `Átnevezés` (inline, `Mentés`/`Mégse`, hint `Enter: mentés · Esc: mégse. A régi névre a beviteli mező ezt a címkét ajánlja.`; dup name error `Már van „%1” nevű címke. Ha ugyanazt jelentik, vond össze őket.`), `Összevonás…`, trash icon `Címke törlése` | :150-232; VM.cpp:255-264 |
| Meta | `%1 megbeszélés · először: %2 · utoljára: %3` | VM.cpp:213 |
| Learned profile | note `A profilt a rendszer a címke használatából tanulja; a javaslatok indoklása ebből jön. Nem kell szerkeszteni.`; `Jellemző résztvevők` (or `Még nincs elég megbeszélés hozzá.`), `Jellemző kifejezések` (round pills, no #), `Gyakran együtt` (tag chips, click selects that tag) | :251-362 |
| Meetings | list (title, date, duration), row click -> `meetingRequested` (open meeting in main window); `Megnyitás a könyvtárban szűrőként` -> library filter | :376-470 |
| Merge dialog | title `#%1 összevonása`; `Ha ugyanazt jelenti két név. A megbeszélések egy címke alá kerülnek, a profilok összeadódnak.`; candidates: similar names first, then frequent co-occurrence (`%1 megbeszélés · hasonló név`, `… · %2× szerepeltek együtt`); `Megmaradó név` radios; result `Eredmény: <b>%1 megbeszélés</b> (%2 közös). A „%3” név megszűnik; …`; `Nincs hasonló nevű vagy gyakran együtt járó címke.`; `Mégse`/`Összevonás` | TagsMergeDialog.qml; VM.cpp:286-322 |
| Delete dialog | title `Törlöd %1 #%2 címkét?`; text `%1 megbeszélésről lekerül. Maguk a megbeszélések, átiratok és összefoglalók megmaradnak. …`; `Mégse`/`Törlés` | TagsWindow.qml:128-157; VM.cpp:334-349 |
| Toasts | `Átnevezve: #a -> #b`, `Összevonva: #a -> #b`, `Címke törölve: #%1` (with `Visszavonás`) | VM.cpp:264,322,349 |

---------------------------------------------------------------------------------------------------

## E. Onboarding window `Első lépések` (Q/OnboardingWindow.qml, S/OnboardingViewModel, OnboardingWindowHost)

Non-modal 720x640 (min 640x540) wizard, six steps (left rail with step list + `%1 / %2` counter), nothing mandatory; opened automatically once (`onboardingDone` set on close) and via `Fájl > Első lépések…` or Settings General link. Ctrl+W closes (:52). Draft model per step: `Tovább` writes only the current step's diff; `Kihagyom` discards that step's draft; closing discards unsaved step; name goes via `AppController::setUserSpeakerName` (also fixes person DB) (S/OnboardingViewModel.h:3-24).

| Step | Content | Ref |
|---|---|---|
| welcome | `Üdv a Tanarában!`, flow chips `Felvétel` -> `Átirat` -> `Összefoglaló`, note "settings can be changed later" | :259-290 |
| you | `Te`: `Saját neved` (hint mentions own voiceprint), `Nyelv`, `Téma` (`Rendszer`/`Világos`/`Sötét`) | :295-360 |
| folders | `Mappák`: audio + notes folders with `alapértelmezett` marker, `Tallózás…`, reset arrow; note `A belső adatok mappáját (hanglenyomatok, index) a Beállítások › Általános lapon találod.` | :375-450 |
| providers | `Szolgáltatások`: readiness lines (Soniox key needed / Cloud selected), `Beállítás most` (opens Settings), `Beállítások › Szolgáltatások`, `Tanara Cloud: hamarosan — …` | :461-512 |
| watcher | `Hívásfigyelő`: switch `Induljon el a figyelő bejelentkezéskor` + pointer to Settings | :523-548 |
| done | `Kész is vagyunk`; per-step summary (`%1 — kihagyva` / `%1 — nem nézted meg`) | :560-590 |
| Footer | `Később` (tooltip `Bezárás — a Fájl › Első lépések… menüből újranyitható`), `Vissza`, `Kihagyom`, `Tovább`/`Kezdjük`; `refreshReadiness` when window re-activates | :611-633, :36-37 |
Nothing in onboarding mentions voice models or the speaker model download.

---------------------------------------------------------------------------------------------------

## F. Core features: UI entry point status

| Core feature (where) | UI entry? | Detail |
|---|---|---|
| **Voice-model selection / enable-disable** (`core/voiceid/VoiceModelRegistry.h`, `SettingsManager::enabledVoiceModels`, settings.json `voiceModels`; CLI `voice-models enable|disable`, cli/VoiceCommands.cpp:71-97) | **NO** | No QML or GUI C++ reads `voiceModels`/`activeVoiceModelIds`/`voiceModelsChanged`. Default is `["campplus"]` (Types.h:344). The editor/People UI is model-agnostic (fused vectors, sibling prints). |
| **Voice-model download** (`voice-models fetch <id> [--force]`, VoiceCommands.cpp:100-) | **NO** | No progress/consent/license UI. Missing model surfaces only as: `Hangelemzés nélkül` strip + tooltip `Nincs letöltve a hangmodell…` (editor), `A hangfelismerő modell nincs telepítve…` (People plan), `Az azonosításhoz nincs telepítve a hangmodell.` toast (identify), `Nincs letöltve a hangmodell, ezért itt most nem készíthető hanglenyomat.` (VoiceprintPanel). Install instructions are README-only (README.md:157 curl) / Windows installer bundles CAM++. |
| **Voice-model comparison `voice-eval`** (`core/voiceid/VoiceEval.h`, VoiceCommands.cpp) | **NO** | CLI-only, read-only measurement. |
| **Per-model voiceprints** (`Voiceprint.model`, `sourceRefs`, siblings per enabled model; `VoiceprintStore`) | **NO (invisible)** | `createVoiceprint` creates sibling prints for every active model (SpeakerEditor.cpp, `printId` = first; undo removes the siblings together). People window lists samples without model; no per-model counts, no per-model delete. |
| **Backfill of voiceprints for newly enabled models** (`AppController::backfillVoiceprints`, `voiceprintBackfillRunning`, signal `voiceprintBackfillFinished`; core/AppController.h:143-149) | **NO UI** | Runs automatically: at startup (`QTimer::singleShot(0, … backfillVoiceprints)`, AppController.cpp:1124) and when the active model list grows (:1229); 1-thread pool `tanara-voiceprint-backfill`. No GUI code calls it, shows running state, or connects to the finished signal. |
| **Track-side analysis** (`core/edit/SideAnalysis.h` `analyzeSides`, `TrackActivity.h` `computeMeetingActivity`, cache `tracks.activity.bin`; CLI `track-sides`, cli/TrackSidesCommand.cpp) | **NO** | Core + CLI only (grep: no QML/gui reference). Nothing shows mic-vs-loopback side per line (`Local/Remote/Mixed/Unknown`), conflicts with the speaker's side, person side verdicts, thresholds/histogram. Single-track meetings would be silently inactive by design. |
| **Pair recheck** (`SpeakerEditor::recheckPair`, `pairOffer`, `declinePairOffer`) | **YES** | (1) SpeakerPopover whole-speaker block `Átnézés másik beszélővel…` (SpeakerPopover.qml:462-523); (2) change-bar offer `Átnézés` / `Most nem` after a manual move when the two named voices are too similar (TranscriptChangeBar.qml:194-254); result as toast + uncertain filter. |
| **Recheck from confirmed** (`SpeakerEditor::recheckFromConfirmed`, `canRecheck`, `recheckBlocker`) | **YES** | Toolbar chip `Bizonytalan` when 0 uncertain; empty-filter button; MeetingHeader menu `Beszélők újraellenőrzése…`; fallback of `Résztvevők azonosítása` when everyone is named (all via `ShellActions::recheckSpeakers`/`runRecheck` with confirm dialog). In-editor direct call exists only without a shell (demo). |
| Voice-aware "uncertain" analysis / suggestions (`startEmbedding`, `computeUncertain*`) | YES (indirect) | Progress `Hangelemzés… NN%`, `bizonytalan` pills, hatched rail cells, `Hasonló %n sor is`. Thresholds not user-adjustable. |
| Retranscribe impact (`retranscribeImpact()`) | YES (shell dialog M10, not in these files) | Used by `shell.retranscribe` confirmation. |
| `confirmed` / `rechecked` per-utterance flags | NO marker | See A4. |

---------------------------------------------------------------------------------------------------

## G. Performance / threading facts found in comments and code

| Fact | Ref |
|---|---|
| Utterance embeddings run on a dedicated `QThread` created in `SpeakerEditor::startEmbedding()`; the thread builds its OWN embedder via the factory (embedders are not shared across threads), decodes the meeting audio ONCE (single ffmpeg run to 16 kHz mono, then slices; per-utterance ffmpeg would be too slow for 400-1500 lines), computes all missing models per line from that one PCM, flushes results to the main thread in batches (16 lines or 300 ms) via queued invoke; cancel via atomic flag; cache `transcript.embeddings.bin` (v2, per model) in the meeting folder bound to the transcript fingerprint | core/src/edit/SpeakerEditor.cpp:1766-1836; core/edit/UtteranceEmbeddings.h:3-8, 41-44, 72-95 |
| Only lines >= 1.5 s are embedded (`kMinEmbedMs`), long monologues use ~10 s from the middle (`kMaxEmbedMs`), embeddings are "reliable" from 3 s | core/edit/SpeakerAnalysis.h:21-26 |
| Closing/switching a meeting cancels its running embedding (finished part stays in cache and resumes later) | S/TranscriptEditorViewModel.cpp:248-254 |
| `transcript.md` is regenerated on a background thread with 1.5 s coalescing delay (~120 ms for a 2 h meeting on the main thread otherwise) | core/src/edit/SpeakerEditor.cpp:38, ~170-175 |
| Edits (overlay, meeting.json) are written to disk immediately; `flushPendingWrites()` on exit/export | core/edit/SpeakerEditor.h:236-241 |
| `reloadAll` and `SpeakerEditor::load` are timed with `lcPerf` (speakers/rows/search/overview, meeting/transcript/overlay/cache/assigned/uncertain) | S/TranscriptEditorViewModel.cpp:283-300; SpeakerEditor.cpp:184-215 |
| List model: never resets for a single move (dataChanged/insert/remove only); overview/lane strips and rail cells are single painted items; list uses `reuseItems`, `cacheBuffer: 800`; overview rebuild debounced (`m_overviewTimer`) | S/TranscriptListModel.h:3-9; S/SpeakerRailCells.h; Q/TranscriptTab.qml:511-513 |
| Voiceprint backfill: 1-thread `QThreadPool`, decodes sample audio and adds sibling prints, never blocks the main thread; runs at startup and on model-list growth | core/AppController.h:143-149; AppController.cpp:544-557, 1030-1031 |
| `PeopleStats` (meetings count, talk time, last seen) on a background thread with per-meeting mtime+size cache (process lifetime) | core/people/PeopleStats.h:3-14 |
| People voiceprint creation is cooperative on the UI thread: one meeting per 30 ms timer tick (audio decode a few seconds per meeting) | S/PeopleViewModel.cpp:481-520 |
| `identifyMeetingAsync`: audio decode + embedding on a background thread, matching/saving on main; cancellable, progress in task strip | core/AppController.h:411-416 |
| Waveforms: async via ffmpeg `QProcess`, PCM processed in chunks in the event loop (UI never blocks), cache `<audio>.peaks.json` bound to size+mtime | core/audio/WaveformService.h:3-9 |
| Tag profiles: `MeetingProfiles` background thread `tanara-profiles`; tag suggestions computed off-thread but all signals delivered on GUI thread | core/tags/MeetingProfiles.h:3-30; S/TagControllerBackend.h:6 |
| Settings folder-usage measurement on `QThreadPool::globalInstance`; level monitor on its own thread (`DeviceMonitor`) | S/SettingsViewModel.cpp:693-712; core/audio/DeviceMonitor.h:9 |
| `TrackActivity` (not wired to GUI): one ffmpeg run per track file, streamed 16 kHz mono PCM, 50 ms frames, cache `tracks.activity.bin` | core/edit/TrackActivity.h:3-14 |
| Screenshot/software renderer restrictions: no MultiEffect/ShaderEffect; use `TShadow`/painted items | Q/CONTRACT.md:107-109 |

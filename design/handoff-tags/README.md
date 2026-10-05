# Handoff: Tanara — tags & tag suggestions (Qt Quick / QML)

## Overview
Adds **tags** to meetings and **tag suggestions** (brief: `design/brief-cimkek.md`, C01–C09). Covers the tag chip, tag input, the tag row in the meeting header, suggestions with reasons, library filter/search/row display, multi-select bulk tagging, recorder and import entry, a Tags manager window, and the embedding-model settings.

Target: Qt 6, Qt Quick / QML, existing `Theme` tokens and `T*` controls. **No new colours.** Light and dark.

## About the design files
`design/Tanara Tags.dc.html` is an HTML design reference (open in a browser with `support.js` and `fonts/` next to it). Recreate natively. Tweak `theme` = mixed/light/dark. One PNG per state in `screenshots/` (T01–T16). All names and numbers are fictional.

## Fidelity
High-fidelity. Sizes, colours, copy and states are final.

## Decisions (answers to the brief's open questions)
1. **Tag colour (C01): none.** The colours belong to speakers. With 50–200 tags colours would not be distinguishable anyway, and they would compete with speaker colours. The tag is identified by shape: square-ish corner (4 px) + "#" glyph, neutral colours.
2. **Suggestions live in one place:** the header tag row, on every tab. LLM suggestions also arrive there (✦), not in the Summary right column. No pop-ups, never auto-applied.
3. **No layout jumps:** the tag row always reserves 26 px. **At most 2 suggestions are visible**, the rest go behind a "+N" chip; "Miért?" lists all of them. The row never wraps.
4. **Multi-select (C05): yes.** Ctrl+click toggles, Shift+click selects a range, Esc clears. The right pane becomes a selection panel for bulk tagging.
5. **Entry point:** sidebar footer = "Személyek" · "Címkék" · ⚙ (Settings shrinks to an icon with tooltip, Ctrl+,). Secondary paths: "Címkék kezelése…" at the bottom of the tag filter, and "Megnyitás a kezelőben" in a chip's context menu.
6. **Before transcript (C07):** while there is no transcript, the tag row moves from the header into step 1 ("Miről szólt a megbeszélés?"), above the note. The header has no tag row in this state. Note cards show their tag, which prepares the later tag-based note suggestions.

## Tokens
No new colours. New component sizes: tag chip height 24 (recorder: 22), radius **4** (`Theme.radiusTag: 4`), label 12.5/500, max width 180 px (ellipsis + tooltip), "#" / "+" glyph in Plex Mono 12.

| Variant | Background | Border | Text | Glyph |
|---|---|---|---|---|
| Applied | `raised` | 1px solid `borderStrong` (hover/focus: `text`) | `text` | "#" `textMuted` |
| Suggested (existing tag) | transparent | 1px **dashed** `accent` | `accent` | "+" `accent`, trailing × |
| LLM new name | transparent | 1px dashed `accent` | `accent` | ✦ sparkles 12 px + "ÚJ" badge (9.5/700, `accentSoft` bg, `accent`), trailing × |
| Overflow "+N" | `sunken` | none | `textMuted` | — |
| Partial (multi-select) | `raised` | 1px dashed `borderStrong` | `text` | "#" + count "1/3" mono 11 |

Compare: person chip is pill-shaped and coloured; status pill is small, round and uppercase-free (11/600). Filter chips for tags use the tag chip (with ×).

QML suggestion: one `TagChip` with `kind: applied | suggested | llmNew | overflow | partial`, `compact: bool`, signals `clicked`, `removeRequested`.

## C02 — Tag input
- Inline field, 24 px (28 px in standalone forms), accent border + 3 px `accentSoft` ring, leading "#" icon.
- Popover 300 px (recorder: 250 px, 28 px rows, max 3 rows), radius 8, `raised`, shadow. Rows 32 px: `hash` icon, name with match highlighted (`warnSoft` bg), count mono 12 muted. Selected row `sunken`.
- States: empty field → "LEGUTÓBB HASZNÁLT" (4–5 recents). Typing → matches, accent- and case-insensitive (NFD + strip diacritics), max 5. Always ends with "+ Új címke: „…”". No match → that row is selected. Near-duplicate ("Museum Plus" vs "MuseumPlus": normalized without spaces/punctuation, or small edit distance) → header "HASONLÓ MÁR VAN", existing tag selected, "Mégis új: „…”" below.
- Footer hint 11 muted: "↑↓ választás · Enter hozzáadás · Esc bezárás".
- Keys: Enter adds the selected item, Backspace in an empty field removes the last tag, ↑/↓ navigate, Esc closes and returns focus. In the header, **T** opens the field; in the recorder, **Ctrl+T**.

## C03/C04 — Tag row in the meeting header (T02, T03, T06)
Placed under the meta line (margin-top 10), above the tabs; shared by all tabs. Layout, single line, gap 6:
`[applied chips…] [+N] [+ Címke]  |  [✦] label  [≤2 suggestion chips] [+N] Mind  (i) Miért?`
- "+ Címke": ghost, 24 px, muted 12.5/500, plus icon.
- Divider 1×16 `border`, label 12 muted. The label tells the source: "Javasolt" (similar meetings), "<Tag> mellé gyakran" (co-occurring, right after adding a tag), "Az összefoglaló alapján" with ✦ (LLM).
- Suggested chip: click = accept, × = reject (no confirmation; toast with Visszavonás; Ctrl+Z). "Mind" accepts all visible and hidden suggestions.
- **Reason popover "Miért?"** (T02): 420 px. Per suggestion: chip, "Hozzáadás" (primary, 26 px), "Nem illik ide" (secondary); reason lines in a grid `130px | 1fr` ("Közös résztvevő", "Közös kifejezések", "Hasonló cím"); "Hasonló megbeszélés" links in accent (open that meeting). Footer: "Az elutasított javaslatot ennél a megbeszélésnél nem ajánljuk újra."
- Computing state: divider + spinner icon + "Javaslatok készülnek…". No suggestions → nothing after "+ Címke".
- Rejections are stored per (meeting, tag) and never re-suggested.

## C05 — Library (T04, T05)
- **Row:** a third line with tags as plain text, 12 muted, gap 8 ("#Nordvik  #Partnerek  +1"). It fits as many as ~24 characters allow, then "+N". Rows without tags keep 2 lines.
- **Filter:** tag chips (with ×) join the existing filter chips; "+ Szűrő" (dashed pill) opens a 300 px popover: ÁLLAPOT checkboxes · CÍMKE with a segmented "Bármelyik / Mindegyik" (OR / AND), search field, tag checkboxes with counts, "Címke nélkül" (count), footer link "Címkék kezelése…".
- **Search** also matches tags. Count line: "4 találat · címben, címkében, átiratban". A tag hit shows a snippet line with a tag icon: "címke: **#Nordvik**" (highlight `warnSoft`); a combined hit, e.g. "· átiratban 4×".
- **Multi-select (T05):** checkboxes appear on all rows (16 px, checked = accent). Right pane: "3 megbeszélés kijelölve" + key hints, a list of selected items, CÍMKÉK with chips (full = solid border "3/3"; partial = dashed border "1/3", click applies to all; × removes from all), and an input "Címke hozzáadása mind a 3 megbeszéléshez…". "Kijelölés megszüntetése". Bulk changes are one undo step.

## C06 — Recorder (T08a–c)
A row under the title field: compact chips (22 px, × visible), "+ Címke" with the "Ctrl+T" hint. Editable before and **during** recording, also in the collapsed view. Not shown in pill mode. Input + popover per C02 (compact). Recorder width 420.

## C07 — Before transcript & import (T07, T09)
- Step 1: label "Címkék", a 36 px multi-chip field ("Címke hozzáadása…"), a suggestion line ("Javasolt: [+ …]" + short reason, e.g. "hasonló cím: … · Észlelt hívás: Microsoft Teams"), then "Megjegyzés" with note cards. Each card shows its tag as a mini chip (18 px, 11 px).
- Import dialog: "Címkék" field under Cím / Mikor készült, the same suggestion line (title / file-name similarity).

## C08 — Tags manager window (T10–T13)
Separate window, 960 × 660, same structure as Személyek.
- List 288 px: search "Címke keresése", count, sort (Legutóbb használt / ABC / Leggyakoribb). Row: 28 px `sunken` tile with "#", name 14/500 (selected 600, `accentSoft`), meta "14 megbeszélés · okt. 2.".
- Detail: 44 px tile, name 20/600, meta (count · first · last use); Átnevezés, Összevonás…, delete (danger icon). An info line says the profile is learned and read-only. Two columns: JELLEMZŐ RÉSZTVEVŐK (avatar, name, "12 / 14"); JELLEMZŐ KIFEJEZÉSEK (round `sunken` pills; no "#", so they cannot be mistaken for tags); GYAKRAN EGYÜTT (tag chips + "6×"). MEGBESZÉLÉSEK list + "Megnyitás a könyvtárban szűrőként".
- Rename: inline, as in People. Merge (T11): 500 px dialog, candidates (similar names first, then frequent co-occurrence), "MEGMARADÓ NÉV" radio, result with numbers; afterwards the input offers the kept name when the old one is typed. Delete (T12): "14 megbeszélésről lekerül. A megbeszélések… megmaradnak. A profil… törlődik." Danger button. Empty (T13): "Még nincs címke" + where to add tags.

## C09 — Settings › Szolgáltatások (T14–T16)
- Already configured roles (Átírás, Összefoglaló) are shown collapsed as one-line cards with a status pill.
- New card **Beágyazás** ("címkejavaslatokhoz (embedding)") + status pill. Segmented: **Nincs (alap)** / **Helyi végpont** / **Tanara Cloud**. Description text per option; "alap" is neutral, never a warning.
- Local: Cím (URL), Modell + Lekérés, "Kapcsolat tesztelése".
- Cloud: info box (`accentSoft`): the transcript text goes to Tanara Cloud, as with cloud summaries; audio and voiceprints stay local; credit estimate.
- **KÖNYVTÁR ELŐKÉSZÍTÉSE:** running = "23 / 40 megbeszélés", "kb. 4 perc van hátra", 6 px progress bar, Megszakítás, plus a note that you can keep working. Error = danger box "Megállt 31 / 40-nél: …" + Folytatás. Done = check + "Naprakész · 40 megbeszélés · okt. 5. 09:12" + "Újraelőkészítés".
- Model change (T15): warn box **before saving** ("…a könyvtár előkészítése elölről indul (40 megbeszélés, kb. 6 perc). Addig a javaslatok az alap szinten működnek."). The footer says "Modellváltás · mentéskor újraindul az előkészítés" and the button reads "Mentés és újraindítás".
- CÍMKEJAVASLATOK switches: "Címkék javaslása"; "A nyelvi modell is javasoljon az összefoglaló után" (max 2 new names). Line "Elutasított javaslatok: 12 · Visszaállítás".

## Data (suggested)
- `Tag { id, name, normalizedName, createdAt, lastUsedAt, meetingCount }`
- `MeetingTag { meetingId, tagId, addedAt, source: manual|suggestion|llm|bulk }`
- `TagSuggestion { meetingId, tagName, tagId?, source: similar|cooccur|llm, isNew, score, reasons[{kind: participant|terms|title, values[]}], similarMeetingIds[] }`
- `RejectedSuggestion { meetingId, tagId|normalizedName }`
- `TagProfile { tagId, topParticipants[{personId, count}], topTerms[], cooccurring[{tagId, count}] }` (derived)
- `EmbeddingState { provider: none|local|cloud, model, preparedCount, total, status: idle|running|error|done, lastRun }`
- Leave room for later (brief §9): tag-bound note (names, terms, mishearings) on the `Tag`; tag-based note suggestions in step 1 and Summary; title ↔ tag prefill in the recorder.

## Files
- `design/Tanara Tags.dc.html`, `design/support.js`, `design/fonts/`
- `design/brief-cimkek.md` — the original brief (Hungarian)
- `qml/theme/Theme.qml`, `qmldir` — same singleton with `radiusTag` added
- `screenshots/` — T01 (light) / T01b (dark) elements, T02–T07 main window, T08a–c recorder, T09 import, T10–T13 manager, T14–T16 settings

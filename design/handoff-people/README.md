# Handoff: Tanara — People window (Qt Quick / QML)

## Overview
Separate, resizable window (default 960 × 660, min ~800 × 560) to manage known **persons** (valid across all meetings) and their **voiceprint samples**. Replaces today's list + two truncated lists. Opened from the main window sidebar footer ("Személyek") and from Settings › Általános.

Target: Qt 6, Qt Quick / QML. Tokens: `qml/theme/Theme.qml` (same singleton as the other packages).

## About the design files
`design/Tanara People.dc.html` is an HTML design reference (open with `support.js` and `fonts/` next to it). Recreate natively. Tweak `theme` = mixed/light/dark. Screenshots in `screenshots/` (P01–P06). Names are fictional.

## Fidelity
High-fidelity.

## Layout
- Title bar 36px `surface`: "Személyek", close.
- **List pane** 288px, `surface`, right border:
  - Search field 32px (`sunken`; focused/with text = `raised` + accent border + 3px accentSoft ring), placeholder "Név vagy becenév", clear ×. Matches names **and aliases**, case/diacritic-insensitive; match highlighted with `warnSoft` background.
  - "Új személy" 32px icon button (user-plus).
  - Count line 12 muted ("14 személy" / "3 találat") + sort dropdown (ABC / Legutóbb / Legtöbb megbeszélés).
  - `ListView` with section headers: "TE" (self, always first), then initial letters (11/600 uppercase muted). No headers while searching. Expect 80+ entries.
  - Row: 28px monogram avatar (`sunken` + border; self = accent fill + onAccent), name 14/500 (selected 600, **not elided** in normal widths — let the list pane be resizable instead), "te" pill for self; meta 12 muted with icon: fingerprint (`successInk`) "23 megbeszélés · 9 minta", or minus (muted) "… · nincs hanglenyomat". Selected = `accentSoft`, radius 6.
  - Search with no result: "Becenevekben is kerestem." + accent link "Új személy: „…”".
- **Detail pane** padding 22/28, scrollable:
  - Header: 44px avatar, name 20/600, meta 13 muted ("Utoljára: okt. 1. · 23 megbeszélés · 6 ó 12 p beszéd"); actions: "Átnevezés" (pencil), "Összevonás…" (merge), delete icon button (trash, `dangerInk`). Secondary buttons 30px.
  - Two columns: **Becenevek** — chips 26px radius 13 (`raised` + border, × to remove), dashed "+ Becenév" chip turns into an inline field; helper "A keresés és a személyválasztó ezekre is talál." **Megjegyzés** — multi-line field, free text, autosave on blur.
  - **Hanglenyomat** ("9 minta · 5 forrásból"): bordered list, row grid `28 | 1fr | 86 | 40 | 28`, gap 12, padding 8/10/8/12:
    - 28px round play button (`raised`; playing = accent fill + pause, row bg `accentSoft`).
    - Source line 13/500 with device icon (mic / headphones / speaker) — e.g. "Trust USB mikrofon", "Hívás hangja · Teams", "ismeretlen eszköz"; below it the meeting title 12 muted **wrapping to full length** (no truncation).
    - Date mono 12 (YYYY-MM-DD), length mono 12 right-aligned.
    - "…" menu (opens directly under its row, 270px popover): Meghallgatás · Új személy ebből a mintából… · Áthelyezés másik személyhez… · ─ · Minta törlése (dangerInk).
    - Footer link "Mind a 9 minta" (show first 5).
  - **No voiceprint** (P03): dashed box, fingerprint tile, "Még nincs hanglenyomata", explanation (lines were assigned manually), primary "Minta a 3 megbeszélésből" + note "kb. 2 perc hang, a gépen marad". Samples are taken from confident, manually assigned lines.
  - **Megbeszélések** ("23 · legújabb elöl"): list grid `1fr | 86 | 56`: full title (wraps), date mono, talk time mono. Footer "Mind a 23 megbeszélés". Rows are not actions (no open-meeting from here in this version).

## Dialogs & feedback
- **Rename** (P02): inline in header — 34px field 18/600 with focus ring, "Mentés" (primary), "Mégse"; hint "Enter: mentés · Esc: mégse. A régi név becenévként megmarad." Old name is added to aliases automatically.
- **Merge** (P04): 500px dialog on scrim. Title "<Név> összevonása", explanation; search field; candidate list (radio, avatar, name, meta incl. voice similarity "hang alapján hasonló (88%)" — sort by similarity); "MEGMARADÓ NÉV" radios; result line with numbers ("27 megbeszélés, 11 minta. „B. Gergő” becenév lesz. Az érintett összefoglalók elavultnak jelölődnek."). Mégse / primary "Összevonás".
- **Delete** (P05): 440px dialog: "Törlöd <Nevet>?", consequence ("23 megbeszélésen névtelen beszélőként marad meg (pl. „Távoli 2”), a szöveg nem változik. A 9 hangmintája törlődik…"), checkbox "Hangminták megtartása névtelen személyként" (default off). Mégse / danger "Törlés". The self person cannot be deleted (hide the button).
- **Undo toast** (P03): bottom-centre, inverted (`text` bg, `bg` fg), radius 8: "Minta törölve: <megbeszélés>" + "Visszavonás". ~8 s. Used for sample delete, sample move, alias removal. Ctrl+Z also works.
- **Empty** (P06): list shows only self + dashed hint card ("Itt jelennek meg a résztvevők…"); detail shows centred empty state "Még csak te vagy itt" + "Új személy".

## Behaviour / data
- `Person { id, name, aliases[], note, isSelf, meetingCount, talkTime, lastSeen }`
- `VoiceSample { id, personId, sourceKind (mic|call|system|unknown), deviceLabel, meetingId, meetingTitle, recordedAt, duration, audioRef }`
- "Új személy ebből a mintából…" → small dialog asking a name (or picking an existing person = move); the sample leaves the current voiceprint.
- Any change that affects speaker identity (merge, delete, move sample) marks summaries of affected meetings as stale.
- Keyboard: Ctrl+F focuses search, F2 rename, Del delete (with confirmation), Space plays selected sample.

## Files
- `design/Tanara People.dc.html`, `design/support.js`, `design/fonts/`
- `qml/theme/Theme.qml`, `qmldir`
- `screenshots/` — P01_overview_and_sample_menu, P02_search_and_rename, P03_person_without_voiceprint, P04_merge_confirm_dialog, P05_delete_person_confirm, P06_empty_list

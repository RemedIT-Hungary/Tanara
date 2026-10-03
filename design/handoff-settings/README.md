# Handoff: Tanara — Settings window (Qt Quick / QML)

## Overview
Separate, resizable settings window (default 900 × 680, min ~760 × 560) with a left navigation of five pages: Általános, Rögzítés, Hívásfigyelő, Szolgáltatások, Összefoglaló. Content area scrolls (`Flickable`/`ScrollView`); the mockups show each page at full length.

Target: Qt 6, Qt Quick / QML, custom-styled Controls. Tokens: `qml/theme/Theme.qml` (same singleton as the other packages).

## About the design files
`design/Tanara Settings.dc.html` is an **HTML design reference** (open in a browser with `support.js` and `fonts/` next to it). Recreate natively. Tweak `theme` = mixed/light/dark. Screenshots of each state in `screenshots/` (light theme).

## Fidelity
High-fidelity. Provider list, Cloud credit numbers and the "magyarhoz ajánlott" tag are placeholders to confirm with product.

## Window structure
- Title bar 36px `surface`: "Beállítások" 13/600, close button.
- **Nav** 208px, `surface`, right border, padding 12/10: items 36px, radius 6, 16px icon + 14px label; active = `accentSoft` bg, `text`, 600; inactive `muted`. A `warn` 8px dot on "Szolgáltatások" when a required provider is missing/failing. Icons: settings, mic, radar, plug, sparkles.
- **Content** padding 24/32, max-width ~620. Section labels 12/600 uppercase +0.06em muted; sections separated by 1px `border` top + 18px padding.
- **Footer** 56px `surface`, top border: dirty indicator (8px warn dot + "1 nem mentett változás") or "Nincs mentetlen változás"; "Mégse" (secondary), "Mentés" (primary when dirty; disabled = `sunken` bg + muted label). Closing with unsaved changes asks (Mentés / Elvetés / Mégse).

## Controls
- Text field 34px (32 in service cards), radius 6, `raised`, 1px `borderStrong`; focus = accent border + 3px `accentSoft` ring. Paths/URLs/keys in Plex Mono 13; read-only path fields `sunken`.
- Switch 30×18 (large 38×22 for the master toggle): on = accent fill + onAccent knob; off = `borderStrong` outline + knob.
- Segmented control: `sunken` track, padding 3, items 28px; selected = `raised` + 1px shadow, 600.
- Radio 16px: off 1.5px `borderStrong`; on 5px accent border.
- Stepper: 32px, − / value (mono) / +.
- Chip (app tag): 30px, radius 15, label + process name mono 11 muted + ×; active (currently detected) = accentSoft/accentLine. Add chip dashed.
- Status pill: "Kapcsolódva · 210 ms" successSoft/successInk + circle-check; "Nem érhető el" dangerSoft/dangerInk + circle-x.

## Pages
**B01 Általános** — "TE": Saját neved field + helper; voiceprint row (fingerprint icon successInk, "Hanglenyomat: 23 minta, 9 megbeszélésből", link "Személyek kezelése →"). "MEGJELENÉS": Nyelv dropdown (Rendszer nyelve / Magyar / English), Téma segmented Rendszer / Világos / Sötét (monitor/sun/moon). "MAPPÁK": Felvételek, Jegyzetek, Belső adatok — label + disk usage (12/muted), path field, "Tallózás…", open-folder icon button, helper line. Warn against syncing internal data to cloud storage.

**B02 Rögzítés** — "ALAPÉRTELMEZETT FORRÁSOK": same grouped device list as the recorder (Mikrofonok / Hangkimenetek / Egyéb bemenetek) with switch, friendly name + pencil (click to rename inline; editing = 28px field with focus ring), "alapért." pill + raw OS name (mono 11), and a live 14-segment VU meter. Switch "Minden eszköz rögzítése automatikusan" + helper. "Hangminőség" segmented Takarékos / Beszéd / Magas with size estimate ("32 kbps Opus · kb. 22 MB sávonként 1,5 óra alatt"). "Lekeverés" radios: Automatikusan a felvétel után / Kézzel a Sávok fülön. Device renames apply everywhere (recorder, Tracks tab).

**B03 Hívásfigyelő** — master card: large switch, "Hívásfigyelés", explanation, live line "Most: Microsoft Teams használja a mikrofont". Switches: "Induljon el a bejelentkezéskor"; "Kérdezzen rá, ha véget ért a megbeszélés" + "Csend után: [− 3 perc +]". "FIGYELT ALKALMAZÁSOK" chips (Zoom, Microsoft Teams, Webex, Slack, Discord, Google Meet) with process match; detected one highlighted; "+ Alkalmazás hozzáadása" (picker of running processes or free text). Collapsed "Haladó": poll interval (default 8 s).

**Szolgáltatások**
- Top: two radio cards — **Saját kulcs** (key-round) / **Tanara Cloud** (cloud). Selected = `raised` + 1.5px accent border.
- **Own key**: two cards — "Átírás · beszédből szöveg (STT)" and "Összefoglaló · nyelvi modell (LLM)". Fields depend on provider: Szolgáltató dropdown, Cím (URL) (only for custom/OpenAI-compatible or editable default), Modell (dropdown; "Lekérés" button fetches `/models`), API-kulcs (masked, eye toggle; "nem kell (helyi végpont)" for localhost). "Kapcsolat tesztelése" per card → status pill. LLM card has collapsible "Haladó": Hőmérséklet slider (0–1, default 0,20), Max. tokenek (30 000). Store keys in the OS keychain (Secret Service / Windows Credential Manager), never in plain config.
- **B04 deep link** from M03 "Szolgáltató beállítása →": info banner (accentSoft) "Az átíráshoz kell egy szolgáltató… visszaviszünk a megbeszéléshez.", STT card highlighted (accent border + ring), provider dropdown opened (Soniox [magyarhoz ajánlott], Deepgram, OpenAI, Speechmatics, OpenAI-kompatibilis végpont), key field disabled until a provider is chosen. After a successful save, return to the meeting and enable "Átírás indítása".
- **B05 error**: card border `dangerLine`, pill "Nem érhető el", danger box with human message ("A végpont nem válaszol. Fut a helyi szerver (pl. LM Studio), és jó a port?") + mono error code.
- **B06 Tanara Cloud**: account row (avatar, e-mail, "Bejelentkezve · Tanara Cloud", Kijelentkezés); cards "EGYENLEG" (mono 24 "8 420" kredit + time estimate + "Feltöltés ↗" opens browser) and "MINŐSÉG" segmented Gyors / Pontos + explanation and cost per hour; switch "Költségbecslés minden feldolgozás előtt". Footnote: own-key settings are kept; recordings and voiceprints stay local. Switching mode takes effect on save.

**B07 Összefoglaló** — "Összefoglaló nyelve" dropdown. "UTASÍTÁSOK A MODELLNEK" + "módosítva" warn pill + "Alapértelmezett" reset (confirm). Tabs per prompt: Gyors összefoglaló / Témajavaslat / Témánkénti elemzés. Code editor (`TextArea` in Plex Mono 12/1.65, `sunken`, line-number gutter, `{{VÁLTOZÓ}}` highlighted accentSoft/accent via `SyntaxHighlighter` or QML `TextEdit` + custom highlighter). Legend of variables: {{NYELV}}, {{KONTEXTUS}}, {{SZÓJEGYZÉK}}. Row "Kimeneti séma: execSummary, decisions[], actionItems[], participants[]" + "Megtekintés" (read-only dialog; schema not editable).

## Behaviour
- Settings apply on **Mentés**; Mégse discards. Exception: theme previews live.
- Deep links: `openSettings(page, focusField)` used by M03, recorder R10 ("Rögzítés beállításai"), M01 ("Hívásfigyelő beállítása").
- Validation inline under fields; never block navigation between pages.

## Files
- `design/Tanara Settings.dc.html`, `design/support.js`, `design/fonts/`
- `qml/theme/Theme.qml`, `qmldir`
- `screenshots/B01…B07`

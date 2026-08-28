<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en_US" sourcelanguage="hu_HU">
<context>
    <name>AppController</name>
    <message>
        <source>Hang feltöltése…</source>
        <translation>Uploading audio…</translation>
    </message>
    <message>
        <source>Várakozás a Soniox sorban…</source>
        <translation>Waiting in the Soniox queue…</translation>
    </message>
    <message>
        <source>Átírás folyamatban…</source>
        <translation>Transcription in progress…</translation>
    </message>
    <message>
        <source>Sáv kész</source>
        <translation>Track done</translation>
    </message>
    <message>
        <source>Hiba</source>
        <translation>Error</translation>
    </message>
</context>
<context>
    <name>BuiltinProviders</name>
    <message>
        <source>Alap URL</source>
        <translation>Base URL</translation>
    </message>
    <message>
        <source>Modell</source>
        <translation>Model</translation>
    </message>
    <message>
        <source>API-kulcs</source>
        <translation>API key</translation>
    </message>
    <message>
        <source>OpenAI-kompatibilis végpont</source>
        <translation>OpenAI-compatible endpoint</translation>
    </message>
    <message>
        <source>Hőmérséklet</source>
        <translation>Temperature</translation>
    </message>
    <message>
        <source>Max. tokenek</source>
        <translation>Max tokens</translation>
    </message>
    <message>
        <source>Whisper (OpenAI-kompatibilis)</source>
        <translation>Whisper (OpenAI-compatible)</translation>
    </message>
    <message>
        <source>Lokális whisper-szerver (faster-whisper-server, speaches) vagy az OpenAI API (https://api.openai.com/v1 — ott 25 MB a fájllimit).</source>
        <translation>A local whisper server (faster-whisper-server, speaches) or the OpenAI API (https://api.openai.com/v1 — with a 25 MB file limit).</translation>
    </message>
    <message>
        <source>Lokális szervernél a betöltött modell neve (pl. Systran/faster-whisper-large-v3), az OpenAI API-nál whisper-1.</source>
        <translation>On a local server, the name of the loaded model (e.g. Systran/faster-whisper-large-v3); on the OpenAI API, whisper-1.</translation>
    </message>
    <message>
        <source>Nyelv</source>
        <translation>Language</translation>
    </message>
    <message>
        <source>ISO-639-1 nyelvkód (pl. en, de). Üresen hagyva a szerver automatikusan felismeri a nyelvet — vegyes/nem-magyar felvételekhez ezt hagyd üresen.</source>
        <translation>ISO-639-1 language code (e.g. en, de). Leave it empty and the server detects the language — keep it empty for mixed or non-Hungarian recordings.</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <source>Tanara</source>
        <translation>Tanara</translation>
    </message>
    <message>
        <source>QPushButton { font-weight: bold; padding: 6px 14px; }</source>
        <translation>QPushButton { font-weight: bold; padding: 6px 14px; }</translation>
    </message>
    <message>
        <source>🔴  Új felvétel</source>
        <translation>🔴  New recording</translation>
    </message>
    <message>
        <source>A felvétel-vezérlőt külön, leválasztott ablakban nyitja meg.</source>
        <translation>Opens the recording controls in a separate, detached window.</translation>
    </message>
    <message>
        <source>👥  Emberek</source>
        <translation>👥  People</translation>
    </message>
    <message>
        <source>⚙  Beállítások</source>
        <translation>⚙  Settings</translation>
    </message>
    <message>
        <source>QLabel { font-size: 18px; font-weight: bold; }</source>
        <translation>QLabel { font-size: 18px; font-weight: bold; }</translation>
    </message>
    <message>
        <source>QLabel { color: palette(mid); }</source>
        <translation>QLabel { color: palette(mid); }</translation>
    </message>
    <message>
        <source>QFrame#speakersBar { background: palette(alternate-base); border: 1px solid palette(midlight); border-radius: 6px; }</source>
        <translation>QFrame#speakersBar { background: palette(alternate-base); border: 1px solid palette(midlight); border-radius: 6px; }</translation>
    </message>
    <message>
        <source>👥  Beszélők:</source>
        <translation>👥  Speakers:</translation>
    </message>
    <message>
        <source>QLabel { font-weight: bold; }</source>
        <translation>QLabel { font-weight: bold; }</translation>
    </message>
    <message>
        <source>🔍  Résztvevők azonosítása</source>
        <translation>🔍  Identify participants</translation>
    </message>
    <message>
        <source>A beszélők azonosítása a hang-lenyomatok alapján; a biztos találatokat beírja, a többit „ismeretlen partner&quot;-ként hagyja. A neveket az átiraton a névre kattintva adhatod meg.</source>
        <translation>Identifies speakers based on their voiceprints; confident matches are filled in, the rest are left as &quot;unknown partner&quot;. You can assign names by clicking a name in the transcript.</translation>
    </message>
    <message>
        <source>⟳  Újra-átírás…</source>
        <translation>⟳  Re-transcribe…</translation>
    </message>
    <message>
        <source>Az átirat újrakészítése az aktuálisan beállított STT-providerrel. A mostani átirat és a beszélő-hozzárendelések felülíródnak.</source>
        <translation>Create the transcript again with the currently selected STT provider. The current transcript and the speaker assignments are overwritten.</translation>
    </message>
    <message>
        <source>Átirat</source>
        <translation>Transcript</translation>
    </message>
    <message>
        <source>Összefoglaló</source>
        <translation>Summary</translation>
    </message>
    <message>
        <source>Sávok</source>
        <translation>Tracks</translation>
    </message>
    <message>
        <source>QFrame#stepBox { background: palette(alternate-base); border: 1px solid palette(midlight); border-radius: 8px; }</source>
        <translation>QFrame#stepBox { background: palette(alternate-base); border: 1px solid palette(midlight); border-radius: 8px; }</translation>
    </message>
    <message>
        <source>QLabel { font-size: 15px; font-weight: bold; }</source>
        <translation>QLabel { font-size: 15px; font-weight: bold; }</translation>
    </message>
    <message>
        <source>Mit csináljunk ezzel a felvétellel?</source>
        <translation>What should we do with this recording?</translation>
    </message>
    <message>
        <source>&lt;span style=&apos;color:#1a9e3a;&apos;&gt;✓&lt;/span&gt;  &lt;b&gt;① Felvéve&lt;/b&gt;</source>
        <translation>&lt;span style=&apos;color:#1a9e3a;&apos;&gt;✓&lt;/span&gt;  &lt;b&gt;① Recorded&lt;/b&gt;</translation>
    </message>
    <message>
        <source>QFrame#step2box { background: rgba(54,122,204,0.14); border: 1px solid rgba(54,122,204,0.5); border-radius: 6px; }</source>
        <translation>QFrame#step2box { background: rgba(54,122,204,0.14); border: 1px solid rgba(54,122,204,0.5); border-radius: 6px; }</translation>
    </message>
    <message>
        <source>&lt;b&gt;② Átirat&lt;/b&gt;</source>
        <translation>&lt;b&gt;② Transcript&lt;/b&gt;</translation>
    </message>
    <message>
        <source>QPushButton { font-weight: bold; padding: 7px 16px; }</source>
        <translation>QPushButton { font-weight: bold; padding: 7px 16px; }</translation>
    </message>
    <message>
        <source>Átírás indítása ▸</source>
        <translation>Start transcription ▸</translation>
    </message>
    <message>
        <source>🔒  &lt;b&gt;③ Összefoglaló&lt;/b&gt; — előbb átirat kell</source>
        <translation>🔒  &lt;b&gt;③ Summary&lt;/b&gt; — transcript needed first</translation>
    </message>
    <message>
        <source>QLabel { color: palette(mid); font-style: italic; }</source>
        <translation>QLabel { color: palette(mid); font-style: italic; }</translation>
    </message>
    <message>
        <source>A beszélők azonosítása opcionális, és az átirat bármikor elkészül nélküle is — a neveket utólag is hozzárendelheted.</source>
        <translation>Identifying speakers is optional, and the transcript can be created without it at any time — you can also assign names later.</translation>
    </message>
    <message>
        <source>QFrame#playerBarHost { background: palette(alternate-base); border: 1px solid palette(midlight); border-radius: 6px; }</source>
        <translation>QFrame#playerBarHost { background: palette(alternate-base); border: 1px solid palette(midlight); border-radius: 6px; }</translation>
    </message>
    <message>
        <source>Húzd az elemzés-doboz átméretezéséhez</source>
        <translation>Drag to resize the analysis box</translation>
    </message>
    <message numerus="yes">
        <source>%n különböző partner azonosítva</source>
        <translation>
            <numerusform>%n distinct partner identified</numerusform>
            <numerusform>%n distinct partners identified</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source> és %n ismeretlen partner</source>
        <translation>
            <numerusform> and %n unknown partner</numerusform>
            <numerusform> and %n unknown partners</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>MeetingItemDelegate</name>
    <message>
        <source>%1 átirat   %2 össz   %3 azonosítva</source>
        <translation>%1 transcript   %2 summary   %3 identified</translation>
    </message>
    <message>
        <source>%1ó %2p</source>
        <translation>%1h %2m</translation>
    </message>
    <message>
        <source>%1p</source>
        <translation>%1m</translation>
    </message>
</context>
<context>
    <name>MeetingTableModel</name>
    <message>
        <source>%1 átirat   %2 össz   %3 azonosítva</source>
        <translation>%1 transcript   %2 summary   %3 identified</translation>
    </message>
</context>
<context>
    <name>ReadinessModel</name>
    <message>
        <source>Hiányzik: %1 (%2)</source>
        <translation>Missing: %1 (%2)</translation>
    </message>
    <message>
        <source>Nincs bejelentkezve.</source>
        <translation>Not signed in.</translation>
    </message>
    <message>
        <source>Nincs hangsáv az átíráshoz.</source>
        <translation>No audio track to transcribe.</translation>
    </message>
    <message>
        <source>Ismeretlen vagy nem regisztrált STT-provider: %1</source>
        <translation>Unknown or unregistered STT provider: %1</translation>
    </message>
    <message>
        <source>Nincs átirat — előbb futtass átírást.</source>
        <translation>No transcript — run transcription first.</translation>
    </message>
    <message>
        <source>Ismeretlen vagy nem regisztrált LLM-provider: %1</source>
        <translation>Unknown or unregistered LLM provider: %1</translation>
    </message>
</context>
<context>
    <name>RecordBar</name>
    <message>
        <source>⏺  Felvétel indítása</source>
        <translation>⏺  Start recording</translation>
    </message>
    <message>
        <source>Dupla kattintás a szerkesztéshez (húzva mozgatható az ablak)</source>
        <translation>Double-click to edit (drag to move the window)</translation>
    </message>
    <message>
        <source>Megbeszélés</source>
        <translation>Meeting</translation>
    </message>
    <message>
        <source>Cím szerkesztése</source>
        <translation>Edit title</translation>
    </message>
    <message>
        <source>✏</source>
        <translation>✏</translation>
    </message>
    <message>
        <source>Megbeszélés címe… (opcionális)</source>
        <translation>Meeting title… (optional)</translation>
    </message>
    <message>
        <source>Tálcára</source>
        <translation>To tray</translation>
    </message>
    <message>
        <source>—</source>
        <translation>—</translation>
    </message>
    <message>
        <source>Bezárás (visszadokkolás a főablakba)</source>
        <translation>Close (dock back into the main window)</translation>
    </message>
    <message>
        <source>✕</source>
        <translation>✕</translation>
    </message>
    <message>
        <source>QLabel { color: palette(text); }</source>
        <translation>QLabel { color: palette(text); }</translation>
    </message>
    <message>
        <source>Nincs kiválasztott forrás</source>
        <translation>No source selected</translation>
    </message>
    <message>
        <source>Rögzítendő hangforrások kiválasztása (a sávjukon a VU-szint is látszik)</source>
        <translation>Select the audio sources to record (each row also shows its VU level)</translation>
    </message>
    <message>
        <source>▸ Rögzítendő hangforrások módosítása</source>
        <translation>▸ Change audio sources to record</translation>
    </message>
    <message>
        <source>Hangeszközök</source>
        <translation>Audio devices</translation>
    </message>
    <message>
        <source>Beszélj a mikrofonba / játssz le hangot — a mozgó sáv mutatja, melyik eszköz aktív.</source>
        <translation>Speak into the microphone / play some audio — the moving bar shows which device is active.</translation>
    </message>
    <message>
        <source>🎤 Mikrofonok</source>
        <translation>🎤 Microphones</translation>
    </message>
    <message>
        <source>🔊 Hangkimenetek</source>
        <translation>🔊 Audio outputs</translation>
    </message>
    <message>
        <source>Egyéb</source>
        <translation>Other</translation>
    </message>
</context>
<context>
    <name>TracksPanel</name>
    <message>
        <source>🎤 mikrofon</source>
        <translation>🎤 microphone</translation>
    </message>
    <message>
        <source>🔊 hangkimenet</source>
        <translation>🔊 audio output</translation>
    </message>
    <message>
        <source>egyéb</source>
        <translation>other</translation>
    </message>
</context>
<context>
    <name>cli</name>
    <message>
        <source>egyéb</source>
        <translation>other</translation>
    </message>
    <message>
        <source>Felvehető eszközök (%1):</source>
        <translation>Recordable devices (%1):</translation>
    </message>
    <message>
        <source>Meetingek (%1):</source>
        <translation>Meetings (%1):</translation>
    </message>
    <message>
        <source>Index újraépítve a lemezről — %1 meeting.</source>
        <translation>Index rebuilt from disk — %1 meetings.</translation>
    </message>
    <message>
        <source>Nincs elérhető meeting-detektor ezen a platformon (pw-dump?).</source>
        <translation>No meeting detector available on this platform (pw-dump?).</translation>
    </message>
    <message>
        <source>Detektor: %1  (ismert appok: %2)</source>
        <translation>Detector: %1  (known apps: %2)</translation>
    </message>
    <message>
        <source>  ● MEETING: %1  [appId=%2, ablak=&quot;%3&quot;, forrás=%4]</source>
        <translation>  ● MEETING: %1  [appId=%2, window=&quot;%3&quot;, source=%4]</translation>
    </message>
    <message>
        <source>  ○ nincs aktív hívás</source>
        <translation>  ○ no active call</translation>
    </message>
    <message>
        <source>(figyelés %1 mp-enként — Ctrl-C a leállításhoz)</source>
        <translation>(checking every %1 s — Ctrl-C to stop)</translation>
    </message>
    <message>
        <source>Nincs kiválasztható eszköz.</source>
        <translation>No selectable device.</translation>
    </message>
    <message numerus="yes">
        <source>Felvétel: &quot;%1&quot; — %n sáv</source>
        <translation>
            <numerusform>Recording: &quot;%1&quot; — %n track</numerusform>
            <numerusform>Recording: &quot;%1&quot; — %n tracks</numerusform>
        </translation>
    </message>
    <message>
        <source>KÉSZ. Mappa: %1</source>
        <translation>DONE. Folder: %1</translation>
    </message>
    <message>
        <source>  sáv: %1  (%2)</source>
        <translation>  track: %1  (%2)</translation>
    </message>
    <message>
        <source>HIBA: %1</source>
        <translation>ERROR: %1</translation>
    </message>
    <message>
        <source>(Felvétel folyik — nyomj ENTER-t a leállításhoz)</source>
        <translation>(Recording in progress — press ENTER to stop)</translation>
    </message>
    <message>
        <source>Hiányzó meetingId.</source>
        <translation>Missing meetingId.</translation>
    </message>
    <message>
        <source>Átirat kész: %1</source>
        <translation>Transcript done: %1</translation>
    </message>
    <message>
        <source>Összefoglaló kész: %1</source>
        <translation>Summary done: %1</translation>
    </message>
    <message>
        <source>Használat: rename &lt;meetingId&gt; &lt;nyersCímke&gt; &lt;név&gt;</source>
        <translation>Usage: rename &lt;meetingId&gt; &lt;rawLabel&gt; &lt;name&gt;</translation>
    </message>
    <message>
        <source>Átnevezve: &quot;%1&quot; → &quot;%2&quot;</source>
        <translation>Renamed: &quot;%1&quot; → &quot;%2&quot;</translation>
    </message>
    <message>
        <source>Használat: identify &lt;meetingId&gt;</source>
        <translation>Usage: identify &lt;meetingId&gt;</translation>
    </message>
    <message>
        <source>Auto-azonosítás kész. Leképezés (speakerMap):</source>
        <translation>Auto-identification done. Mapping (speakerMap):</translation>
    </message>
    <message>
        <source>  (üres — nincs küszöb feletti találat, vagy nincs modell/lenyomat)</source>
        <translation>  (empty — no match above threshold, or no model/voiceprint)</translation>
    </message>
    <message>
        <source>Használat: participants &lt;meetingId&gt;</source>
        <translation>Usage: participants &lt;meetingId&gt;</translation>
    </message>
    <message>
        <source>Résztvevők azonosítása (átírás előtt, lokálisan)…</source>
        <translation>Identifying participants (before transcription, locally)…</translation>
    </message>
    <message>
        <source>  (nincs találat — nincs modell/aktív sáv, vagy csend)</source>
        <translation>  (no match — no model/active track, or silence)</translation>
    </message>
    <message>
        <source>ISMERETLEN</source>
        <translation>UNKNOWN</translation>
    </message>
    <message numerus="yes">
        <source>  • %1  →  %2   [%n ablak, minta: %3]</source>
        <translation>
            <numerusform>  • %1  →  %2   [%n window, sample: %3]</numerusform>
            <numerusform>  • %1  →  %2   [%n windows, sample: %3]</numerusform>
        </translation>
    </message>
    <message>
        <source>Hang-lenyomatok (%1, %2):</source>
        <translation>Voiceprints (%1, %2):</translation>
    </message>
    <message numerus="yes">
        <source>%n személy</source>
        <translation>
            <numerusform>%n person</numerusform>
            <numerusform>%n people</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n lenyomat</source>
        <translation>
            <numerusform>%n voiceprint</numerusform>
            <numerusform>%n voiceprints</numerusform>
        </translation>
    </message>
    <message>
        <source>Parancsok: devices | record [--title T --seconds N --device IDX] | list | transcribe &lt;id&gt; | summarize &lt;id&gt; | rename &lt;id&gt; &lt;nyersCímke&gt; &lt;név&gt; | identify &lt;id&gt; | voiceprints</source>
        <translation>Commands: devices | record [--title T --seconds N --device IDX] | list | transcribe &lt;id&gt; | summarize &lt;id&gt; | rename &lt;id&gt; &lt;rawLabel&gt; &lt;name&gt; | identify &lt;id&gt; | voiceprints</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <source>Felvétel %1</source>
        <translation>Recording %1</translation>
    </message>
    <message>
        <source>Tanara — Felvétel</source>
        <translation>Tanara — Recording</translation>
    </message>
    <message>
        <source>Nincs rögzíthető hangeszköz.</source>
        <translation>No recordable audio device.</translation>
    </message>
</context>
<context>
    <name>tanara::AppController</name>
    <message>
        <source>Ismeretlen meeting: %1</source>
        <translation>Unknown meeting: %1</translation>
    </message>
    <message>
        <source>Nincs aktív hangsáv a lekeveréshez.</source>
        <translation>No active audio track for mixdown.</translation>
    </message>
    <message>
        <source>A lekeverés (ffmpeg) sikertelen.</source>
        <translation>Mixdown (ffmpeg) failed.</translation>
    </message>
    <message>
        <source>Már folyik felvétel.</source>
        <translation>A recording is already in progress.</translation>
    </message>
    <message>
        <source>Nincs felvehető hangeszköz.</source>
        <translation>No recordable audio device.</translation>
    </message>
    <message>
        <source>A lekeverés sikertelen — az átírás nem indult.</source>
        <translation>Mixdown failed — transcription was not started.</translation>
    </message>
    <message>
        <source>Lekeverés az átíráshoz…</source>
        <translation>Mixing down for transcription…</translation>
    </message>
    <message>
        <source>Ismeretlen STT-provider: %1.</source>
        <translation>Unknown STT provider: %1.</translation>
    </message>
    <message>
        <source>Átírás indítása…</source>
        <translation>Starting transcription…</translation>
    </message>
    <message>
        <source>Átírás-hiba: %1</source>
        <translation>Transcription error: %1</translation>
    </message>
    <message>
        <source>Nincs átirat — előbb futtass átírást.</source>
        <translation>No transcript — run transcription first.</translation>
    </message>
    <message>
        <source>Ismeretlen LLM-provider: %1.</source>
        <translation>Unknown LLM provider: %1.</translation>
    </message>
    <message>
        <source>Összefoglalás a helyi modellel (Gemma)…</source>
        <translation>Summarizing with the local model (Gemma)…</translation>
    </message>
    <message>
        <source>Összefoglaló hiba: %1</source>
        <translation>Summary error: %1</translation>
    </message>
    <message>
        <source>Témák kigyűjtése a helyi modellel…</source>
        <translation>Extracting topics with the local model…</translation>
    </message>
    <message>
        <source>Téma-kinyerés hiba: %1</source>
        <translation>Topic extraction error: %1</translation>
    </message>
    <message>
        <source>Nincs téma a komplex összefoglalóhoz.</source>
        <translation>No topics for the complex summary.</translation>
    </message>
    <message>
        <source>A témához cím kell az elemzéshez.</source>
        <translation>The topic needs a title for analysis.</translation>
    </message>
    <message>
        <source>%1 téma elemzése nem sikerült — futtasd újra a kártyáján, majd kérd a végső összegzést.</source>
        <translation>Analysis failed for %1 topics — rerun them on their cards, then request the final synthesis.</translation>
    </message>
    <message>
        <source>Ismeretlen meeting.</source>
        <translation>Unknown meeting.</translation>
    </message>
    <message>
        <source>„%1” téma elemzése…</source>
        <translation>Analyzing topic &quot;%1&quot;…</translation>
    </message>
    <message>
        <source>Nincs kész téma-elemzés — előbb futtasd a témánkénti elemzést.</source>
        <translation>No completed topic analysis — run the per-topic analysis first.</translation>
    </message>
    <message>
        <source>Összegzés (vezetői összefoglaló + teendők)…</source>
        <translation>Synthesis (executive summary + action items)…</translation>
    </message>
    <message>
        <source>Komplex összefoglaló hiba: %1</source>
        <translation>Complex summary error: %1</translation>
    </message>
</context>
<context>
    <name>tanara::ComplexSummaryService</name>
    <message>
        <source>Nincs beállított LLM provider.</source>
        <translation>No LLM provider configured.</translation>
    </message>
    <message>
        <source>A provider nem adott vissza jobot.</source>
        <translation>The provider did not return a job.</translation>
    </message>
    <message>
        <source>Nem sikerült témát kinyerni a válaszból (sem `## cím` szakasz, sem JSON).</source>
        <translation>Could not extract topics from the response (no `## title` sections and no JSON).</translation>
    </message>
    <message>
        <source>Nem sikerült a téma-elemzést értelmezni („%1”).</source>
        <translation>Could not parse the topic analysis (&quot;%1&quot;).</translation>
    </message>
    <message>
        <source>Nem sikerült az összegzést értelmezni.</source>
        <translation>Could not parse the synthesis.</translation>
    </message>
</context>
<context>
    <name>tanara::OpenAiCompatibleJob</name>
    <message>
        <source>Nincs hálózati válasz (reply == null).</source>
        <translation>No network response (reply == null).</translation>
    </message>
    <message>
        <source>Hálózati hiba: %1</source>
        <translation>Network error: %1</translation>
    </message>
    <message>
        <source>LLM hiba (HTTP %1): %2</source>
        <translation>LLM error (HTTP %1): %2</translation>
    </message>
    <message>
        <source>Érvénytelen JSON válasz: %1</source>
        <translation>Invalid JSON response: %1</translation>
    </message>
    <message>
        <source>A válasz nem tartalmaz &apos;choices&apos; tömböt.</source>
        <translation>The response contains no &apos;choices&apos; array.</translation>
    </message>
    <message>
        <source>Üres LLM-válasz (sem content, sem reasoning_content).</source>
        <translation>Empty LLM response (no content and no reasoning_content).</translation>
    </message>
</context>
<context>
    <name>tanara::RecordingSession</name>
    <message>
        <source>RecordingSession már fut vagy nem üresjáratban van.</source>
        <translation>RecordingSession is already running or not idle.</translation>
    </message>
    <message>
        <source>Nincs felvételre kijelölt eszköz.</source>
        <translation>No device selected for recording.</translation>
    </message>
    <message>
        <source>Nem hozható létre a meeting-mappa: </source>
        <translation>Cannot create the meeting folder: </translation>
    </message>
    <message>
        <source>Az audio motor nem indult el (nincs elérhető eszköz/backend).</source>
        <translation>The audio engine did not start (no available device/backend).</translation>
    </message>
    <message>
        <source>Nem indult el az ffmpeg encoder.</source>
        <translation>The ffmpeg encoder did not start.</translation>
    </message>
    <message>
        <source>Nincs futó felvétel a leállításhoz.</source>
        <translation>No running recording to stop.</translation>
    </message>
</context>
<context>
    <name>tanara::SonioxJob</name>
    <message>
        <source>Fájl feltöltése…</source>
        <translation>Uploading file…</translation>
    </message>
    <message>
        <source>Nem nyitható meg az audiofájl: %1</source>
        <translation>Cannot open the audio file: %1</translation>
    </message>
    <message>
        <source>Feltöltés hiba: %1</source>
        <translation>Upload error: %1</translation>
    </message>
    <message>
        <source>Nincs file id a válaszban: %1</source>
        <translation>No file id in the response: %1</translation>
    </message>
    <message>
        <source>Átírás indítása…</source>
        <translation>Starting transcription…</translation>
    </message>
    <message>
        <source>Átírás létrehozási hiba: %1</source>
        <translation>Transcription creation error: %1</translation>
    </message>
    <message>
        <source>Nincs transcription id a válaszban: %1</source>
        <translation>No transcription id in the response: %1</translation>
    </message>
    <message>
        <source>Feldolgozás…</source>
        <translation>Processing…</translation>
    </message>
    <message>
        <source>Státusz lekérdezési hiba: %1</source>
        <translation>Status query error: %1</translation>
    </message>
    <message>
        <source>Átirat letöltése…</source>
        <translation>Downloading transcript…</translation>
    </message>
    <message>
        <source>ismeretlen Soniox hiba</source>
        <translation>unknown Soniox error</translation>
    </message>
    <message>
        <source>Feldolgozás (%1)… %2 · %3. életjel</source>
        <translation>Processing (%1)… %2 · heartbeat %3</translation>
    </message>
    <message>
        <source>Átirat letöltési hiba: %1</source>
        <translation>Transcript download error: %1</translation>
    </message>
    <message>
        <source>Kész</source>
        <translation>Done</translation>
    </message>
</context>
<context>
    <name>tanara::SummaryService</name>
    <message>
        <source>Nincs beállított LLM provider.</source>
        <translation>No LLM provider configured.</translation>
    </message>
    <message>
        <source>A provider nem adott vissza jobot.</source>
        <translation>The provider did not return a job.</translation>
    </message>
    <message>
        <source>Nem sikerült JSON-ként értelmezni a választ: %1</source>
        <translation>Could not parse the response as JSON: %1</translation>
    </message>
</context>
<context>
    <name>tanara::WhisperCompatJob</name>
    <message>
        <source>Megszakítva.</source>
        <translation>Cancelled.</translation>
    </message>
    <message>
        <source>Nem olvasható a hangfájl: %1</source>
        <translation>Cannot read the audio file: %1</translation>
    </message>
    <message>
        <source>Hang feltöltése…</source>
        <translation>Uploading audio…</translation>
    </message>
    <message>
        <source>Átírás folyamatban…</source>
        <translation>Transcription in progress…</translation>
    </message>
    <message>
        <source>A szerver hibát adott (HTTP %1).</source>
        <translation>The server returned an error (HTTP %1).</translation>
    </message>
    <message>
        <source>Hálózati hiba: %1</source>
        <translation>Network error: %1</translation>
    </message>
    <message>
        <source>Érvénytelen válasz a szervertől (nem JSON).</source>
        <translation>Invalid response from the server (not JSON).</translation>
    </message>
    <message>
        <source>A válaszban nincs időbélyeges átirat (words/segments).</source>
        <translation>The response contains no timestamped transcript (words/segments).</translation>
    </message>
    <message>
        <source>Kész</source>
        <translation>Done</translation>
    </message>
</context>
<context>
    <name>tanara_gui::FloatingRecorder</name>
    <message>
        <source>Tanara — Felvétel</source>
        <translation>Tanara — Recording</translation>
    </message>
</context>
<context>
    <name>tanara_gui::MainWindow</name>
    <message>
        <source>Lekeverés kész.</source>
        <translation>Mixdown done.</translation>
    </message>
    <message>
        <source>A lekeverés sikertelen.</source>
        <translation>Mixdown failed.</translation>
    </message>
    <message>
        <source>Miről szólt a meeting?</source>
        <translation>What was the meeting about?</translation>
    </message>
    <message>
        <source>Pár szóban a téma, fontos nevek, szakszavak… (opcionális)</source>
        <translation>A few words on the topic, important names, jargon… (optional)</translation>
    </message>
    <message>
        <source>Ez a kontextus segíti a pontosabb átiratot: az átíró (Soniox) ezzel jobban dönt a kétes/félreérthető részeknél — nevek, szakszavak, téma.</source>
        <translation>This context helps produce a more accurate transcript: the transcriber (Soniox) uses it to make better decisions on unclear/ambiguous parts — names, jargon, topic.</translation>
    </message>
    <message>
        <source>👥  Résztvevők azonosítása (hang alapján)</source>
        <translation>👥  Identify participants (by voice)</translation>
    </message>
    <message>
        <source>A résztvevők megtippelése a hangsávok alapján — átirat nélkül is futtatható; a felismert neveket a context-be is beépíti.</source>
        <translation>Guesses the participants from the audio tracks — can run without a transcript; recognized names are also added to the context.</translation>
    </message>
    <message>
        <source>🎧  Lekeverés készítése (hallgatáshoz)</source>
        <translation>🎧  Create mixdown (for listening)</translation>
    </message>
    <message>
        <source>Egyetlen, hangosságra normalizált hangfájlt készít a sávokból — kényelmes visszahallgatáshoz. Opcionális: az átíráshoz nem kell.</source>
        <translation>Creates a single, loudness-normalized audio file from the tracks — convenient for listening back. Optional: not needed for transcription.</translation>
    </message>
    <message>
        <source>Összefoglaló</source>
        <translation>Summary</translation>
    </message>
    <message>
        <source>✨  Gyors összefoglaló</source>
        <translation>✨  Quick summary</translation>
    </message>
    <message>
        <source>Egy lépésben, egy modell-hívással készít vezetői összefoglalót + teendőket.</source>
        <translation>Creates an executive summary + action items in one step, with a single model call.</translation>
    </message>
    <message>
        <source>🧩  Témánként</source>
        <translation>🧩  By topic</translation>
    </message>
    <message>
        <source>Több körös: a modell kigyűjti a témákat, te szerkeszted, majd témánként részletes elemzést készít. Pontosabb hosszú/összetett felvételekhez.</source>
        <translation>Multi-round: the model extracts the topics, you edit them, then it produces a detailed analysis per topic. More accurate for long/complex recordings.</translation>
    </message>
    <message>
        <source>⚙  Kontextus</source>
        <translation>⚙  Context</translation>
    </message>
    <message>
        <source>Pár szó a témáról/nevekről — pontosabb összefoglalót ad. Átirat után is módosítható.</source>
        <translation>A few words on the topic/names — gives a more accurate summary. Can also be changed after transcription.</translation>
    </message>
    <message>
        <source>↻  Újragenerálás</source>
        <translation>↻  Regenerate</translation>
    </message>
    <message>
        <source>A gyors összefoglaló újragenerálása a (módosított) kontextussal — az átiratot nem érinti.</source>
        <translation>Regenerates the quick summary with the (updated) context — the transcript is not affected.</translation>
    </message>
    <message>
        <source>Miről szólt? (téma, nevek, szakszavak…) — a pontosabb összefoglalóhoz</source>
        <translation>What was it about? (topic, names, jargon…) — for a more accurate summary</translation>
    </message>
    <message>
        <source>Még nincs összefoglaló.
Válassz fent: „Gyors összefoglaló” egy lépésben, vagy „Témánként” a részletes, szerkeszthető elemzéshez.</source>
        <translation>No summary yet.
Choose above: &quot;Quick summary&quot; in one step, or &quot;By topic&quot; for a detailed, editable analysis.</translation>
    </message>
    <message>
        <source>‹  Vissza az összefoglalóhoz</source>
        <translation>‹  Back to summary</translation>
    </message>
    <message>
        <source>Szerkeszd a témákat (cím + gist), majd „Elemzés indítása”. Minden elemzés azonnal mentődik; a ▶/↻ gombbal témánként is futtatható.</source>
        <translation>Edit the topics (title + gist), then &quot;Start analysis&quot;. Every analysis is saved immediately; the ▶/↻ button runs it per topic, too.</translation>
    </message>
    <message>
        <source>➕  Új téma</source>
        <translation>➕  New topic</translation>
    </message>
    <message>
        <source>Elemzés indítása →</source>
        <translation>Start analysis →</translation>
    </message>
    <message>
        <source>A hiányzó témák elemzése lefut, majd elkészül a végső vezetői összefoglaló.</source>
        <translation>Analyzes the remaining topics, then produces the final executive summary.</translation>
    </message>
    <message>
        <source>Készen áll</source>
        <translation>Ready</translation>
    </message>
    <message>
        <source>🎧 Lekeverés %p%</source>
        <translation>🎧 Mixdown %p%</translation>
    </message>
    <message>
        <source>Újra-átírás</source>
        <translation>Re-transcribe</translation>
    </message>
    <message>
        <source>Újraírod az átiratot ezzel: %1?

A mostani átirat és a beszélő-hozzárendelések felülíródnak. Az összefoglaló a régi marad, amíg újra nem futtatod.</source>
        <translation>Re-transcribe with %1?

The current transcript and the speaker assignments are overwritten. The summary stays the old one until you run it again.</translation>
    </message>
    <message>
        <source>&amp;Fájl</source>
        <translation>&amp;File</translation>
    </message>
    <message>
        <source>🔴  Új felvétel…</source>
        <translation>🔴  New recording…</translation>
    </message>
    <message>
        <source>Beállítások…</source>
        <translation>Settings…</translation>
    </message>
    <message>
        <source>Személyek…</source>
        <translation>People…</translation>
    </message>
    <message>
        <source>Kilépés</source>
        <translation>Quit</translation>
    </message>
    <message>
        <source>&amp;Nézet</source>
        <translation>&amp;View</translation>
    </message>
    <message>
        <source>Sávok</source>
        <translation>Tracks</translation>
    </message>
    <message>
        <source>A kiválasztott megbeszélés hangsávjai.</source>
        <translation>Audio tracks of the selected meeting.</translation>
    </message>
    <message>
        <source>Résztvevők azonosítása (hang alapján)…</source>
        <translation>Identify participants (by voice)…</translation>
    </message>
    <message>
        <source>A kiválasztott felvétel résztvevőinek megtippelése a hangsávok alapján (átirat nélkül is).</source>
        <translation>Guesses the participants of the selected recording from the audio tracks (even without a transcript).</translation>
    </message>
    <message>
        <source>Felvétel-ablak előtérbe</source>
        <translation>Bring recording window to front</translation>
    </message>
    <message>
        <source>A leválasztott felvétel-vezérlő ablakot előtérbe hozza (vagy megnyitja).</source>
        <translation>Brings the detached recording-control window to the front (or opens it).</translation>
    </message>
    <message>
        <source>Beállítás szükséges: %1</source>
        <translation>Setup needed: %1</translation>
    </message>
    <message>
        <source>Nem futtatható: %1</source>
        <translation>Cannot run: %1</translation>
    </message>
    <message>
        <source>%1ó %2p</source>
        <translation>%1h %2m</translation>
    </message>
    <message>
        <source>%1p</source>
        <translation>%1m</translation>
    </message>
    <message>
        <source>%1 ismeretlen</source>
        <translation>%1 unknown</translation>
    </message>
    <message>
        <source>még nincs azonosítva — a neveket az Átiraton add meg</source>
        <translation>not identified yet — assign names in the Transcript tab</translation>
    </message>
    <message>
        <source>A résztvevők megtippelése a hangsávok alapján — átirat nélkül is futtatható.</source>
        <translation>Guesses the participants from the audio tracks — can run without a transcript.</translation>
    </message>
    <message>
        <source>Nincs aktív hangsáv ehhez a felvételhez.</source>
        <translation>No active audio track for this recording.</translation>
    </message>
    <message>
        <source>🔎 Résztvevők: %1</source>
        <translation>🔎 Participants: %1</translation>
    </message>
    <message>
        <source>Résztvevők: %1</source>
        <translation>Participants: %1</translation>
    </message>
    <message>
        <source>&lt;b&gt;② Átirat&lt;/b&gt;</source>
        <translation>&lt;b&gt;② Transcript&lt;/b&gt;</translation>
    </message>
    <message>
        <source>Átírás indítása ▸</source>
        <translation>Start transcription ▸</translation>
    </message>
    <message>
        <source>&lt;b&gt;② Átirat&lt;/b&gt;&lt;br&gt;&lt;span style=&apos;color:#b35900;&apos;&gt;Előbb: %1&lt;/span&gt;</source>
        <translation>&lt;b&gt;② Transcript&lt;/b&gt;&lt;br&gt;&lt;span style=&apos;color:#b35900;&apos;&gt;First: %1&lt;/span&gt;</translation>
    </message>
    <message>
        <source>⚙ Beállítás…</source>
        <translation>⚙ Set up…</translation>
    </message>
    <message>
        <source>Nem indítható: </source>
        <translation>Cannot start: </translation>
    </message>
    <message>
        <source>Átírás indítása…</source>
        <translation>Starting transcription…</translation>
    </message>
    <message>
        <source>Összefoglaló készítése…</source>
        <translation>Creating summary…</translation>
    </message>
    <message>
        <source>Témák kigyűjtése…</source>
        <translation>Extracting topics…</translation>
    </message>
    <message>
        <source>Állapot</source>
        <translation>Status</translation>
    </message>
    <message>
        <source>Téma címe</source>
        <translation>Topic title</translation>
    </message>
    <message>
        <source>Ennek a témának az elemzése (a kész eredmény mentődik)</source>
        <translation>Analyze this topic (the result is saved when done)</translation>
    </message>
    <message>
        <source>Részletek: leírás + elemzés</source>
        <translation>Details: description + analysis</translation>
    </message>
    <message>
        <source>Rövid leírás a modellnek (opcionális):</source>
        <translation>Short description for the model (optional):</translation>
    </message>
    <message>
        <source>Miről szól ez a téma — 1-2 mondat</source>
        <translation>What this topic is about — 1-2 sentences</translation>
    </message>
    <message>
        <source>Elemzés eredménye:</source>
        <translation>Analysis result:</translation>
    </message>
    <message>
        <source>Téma törlése</source>
        <translation>Delete topic</translation>
    </message>
    <message>
        <source>Biztosan törlöd ezt a témát? A kész elemzése is elvész.</source>
        <translation>Delete this topic? Its finished analysis will be lost, too.</translation>
    </message>
    <message>
        <source>Biztosan törlöd a(z) „%1” témát? A kész elemzése is elvész.</source>
        <translation>Delete topic &quot;%1&quot;? Its finished analysis will be lost, too.</translation>
    </message>
    <message>
        <source>A témához cím kell az elemzéshez.</source>
        <translation>The topic needs a title for analysis.</translation>
    </message>
    <message>
        <source>Sorban</source>
        <translation>Queued</translation>
    </message>
    <message>
        <source>Elemzés…</source>
        <translation>Analyzing…</translation>
    </message>
    <message>
        <source>Kész</source>
        <translation>Done</translation>
    </message>
    <message>
        <source>Hiba</source>
        <translation>Error</translation>
    </message>
    <message>
        <source>Téma-elemzés hiba: %1</source>
        <translation>Topic analysis error: %1</translation>
    </message>
    <message numerus="yes">
        <source>%n téma elemzése kész.</source>
        <translation>
            <numerusform>%n topic analyzed.</numerusform>
            <numerusform>%n topics analyzed.</numerusform>
        </translation>
    </message>
    <message>
        <source>%1, %2 — a hibásak a kártyájukon újrafuttathatók.</source>
        <translation>%1, %2 — rerun the failed ones from their cards.</translation>
    </message>
    <message numerus="yes">
        <source>%n téma kész</source>
        <translation>
            <numerusform>%n topic done</numerusform>
            <numerusform>%n topics done</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n hibázott</source>
        <translation>
            <numerusform>%n failed</numerusform>
            <numerusform>%n failed</numerusform>
        </translation>
    </message>
    <message>
        <source>Adj meg legalább egy témát.</source>
        <translation>Enter at least one topic.</translation>
    </message>
    <message>
        <source>Témánkénti elemzés…</source>
        <translation>Per-topic analysis…</translation>
    </message>
    <message>
        <source>Résztvevők azonosítása a hang alapján…</source>
        <translation>Identifying participants by voice…</translation>
    </message>
    <message>
        <source>Megszakítás</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Résztvevők azonosítása</source>
        <translation>Identify participants</translation>
    </message>
    <message>
        <source>Azonosítás megszakítva.</source>
        <translation>Identification canceled.</translation>
    </message>
    <message>
        <source>Átirat elkészült.</source>
        <translation>Transcript completed.</translation>
    </message>
    <message>
        <source>Összefoglaló elkészült.</source>
        <translation>Summary completed.</translation>
    </message>
    <message>
        <source>Hiba: </source>
        <translation>Error: </translation>
    </message>
    <message>
        <source>Felvétel kész: %1</source>
        <translation>Recording done: %1</translation>
    </message>
    <message>
        <source>Átnevezés…</source>
        <translation>Rename…</translation>
    </message>
    <message>
        <source>📂  Mappa megnyitása</source>
        <translation>📂  Open folder</translation>
    </message>
    <message>
        <source>🗑  Törlés…</source>
        <translation>🗑  Delete…</translation>
    </message>
    <message>
        <source>Megbeszélés törlése</source>
        <translation>Delete meeting</translation>
    </message>
    <message>
        <source>Biztosan törlöd: „%1”?</source>
        <translation>Delete &quot;%1&quot;?</translation>
    </message>
    <message>
        <source>A felvétel (hangsávok), az átirat és az összefoglaló is VÉGLEGESEN törlődik. Ez nem visszavonható.</source>
        <translation>The recording (audio tracks), the transcript and the summary will be PERMANENTLY deleted. This cannot be undone.</translation>
    </message>
    <message>
        <source>Törlés</source>
        <translation>Delete</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Megbeszélés átnevezése</source>
        <translation>Rename meeting</translation>
    </message>
    <message>
        <source>Új cím:</source>
        <translation>New title:</translation>
    </message>
</context>
<context>
    <name>tanara_gui::MeetingTableModel</name>
    <message>
        <source>Idő</source>
        <translation>Time</translation>
    </message>
    <message>
        <source>Hossz</source>
        <translation>Length</translation>
    </message>
    <message>
        <source>Név</source>
        <translation>Name</translation>
    </message>
</context>
<context>
    <name>tanara_gui::PeopleManagerDialog</name>
    <message>
        <source>Személyek</source>
        <translation>People</translation>
    </message>
    <message>
        <source>Ismert személynevek (minden meetingre érvényes). A ceruzával (✎) átnevezhető a kijelölt sor.</source>
        <translation>Known person names (apply to all meetings). Use the pencil (✎) to rename the selected row.</translation>
    </message>
    <message>
        <source>Mely meetingeken:</source>
        <translation>In which meetings:</translation>
    </message>
    <message>
        <source>Hang-lenyomatok:</source>
        <translation>Voiceprints:</translation>
    </message>
    <message>
        <source>▶ Meghallgatás</source>
        <translation>▶ Listen</translation>
    </message>
    <message>
        <source>A kijelölt hang-lenyomat reprezentatív szegmensének lejátszása (így törlés/azonosítás előtt ki lehet hallgatni, kié).</source>
        <translation>Plays a representative segment of the selected voiceprint (so you can hear whose voice it is before deleting/identifying).</translation>
    </message>
    <message>
        <source>Lenyomat törlése</source>
        <translation>Delete voiceprint</translation>
    </message>
    <message>
        <source>✎ Átnevezés</source>
        <translation>✎ Rename</translation>
    </message>
    <message>
        <source>Törlés</source>
        <translation>Delete</translation>
    </message>
    <message>
        <source>Összevonás…</source>
        <translation>Merge…</translation>
    </message>
    <message>
        <source>A kijelölt személy összevonása egy másikkal (a hang-lenyomatok és a meeting-címkézések átkerülnek).</source>
        <translation>Merges the selected person into another (voiceprints and meeting labels are transferred).</translation>
    </message>
    <message>
        <source>Bezárás</source>
        <translation>Close</translation>
    </message>
    <message>
        <source>%1  (én)</source>
        <translation>%1  (me)</translation>
    </message>
    <message>
        <source>Hang-lenyomatok (%1):</source>
        <translation>Voiceprints (%1):</translation>
    </message>
    <message>
        <source>ismeretlen eszköz</source>
        <translation>unknown device</translation>
    </message>
    <message>
        <source>%1 — %2</source>
        <translation>%1 — %2</translation>
    </message>
    <message>
        <source>(nincs találat)</source>
        <translation>(no matches)</translation>
    </message>
    <message>
        <source>(én)</source>
        <translation>(me)</translation>
    </message>
    <message>
        <source>Biztosan törlöd a(z) „%1” személyt minden meetingből?</source>
        <translation>Delete person &quot;%1&quot; from all meetings?</translation>
    </message>
    <message>
        <source>Összevonás</source>
        <translation>Merge</translation>
    </message>
    <message>
        <source>„%1” összevonása ezzel a személlyel:</source>
        <translation>Merge &quot;%1&quot; into this person:</translation>
    </message>
    <message>
        <source>Összevonás megerősítése</source>
        <translation>Confirm merge</translation>
    </message>
    <message>
        <source>Biztosan összevonod: „%1” → „%2”?
A(z) „%1” hang-lenyomatai és minden meeting-címkézése átkerül „%2” alá, és „%1” megszűnik.</source>
        <translation>Merge &quot;%1&quot; → &quot;%2&quot;?
All voiceprints and meeting labels of &quot;%1&quot; will be transferred to &quot;%2&quot;, and &quot;%1&quot; will be removed.</translation>
    </message>
    <message>
        <source>Törlöd ezt a hang-lenyomatot? (A személy és a címkézés megmarad.)</source>
        <translation>Delete this voiceprint? (The person and the labels are kept.)</translation>
    </message>
    <message>
        <source>Meghallgatás</source>
        <translation>Listen</translation>
    </message>
    <message>
        <source>A forrás-hangsáv nem található:
%1</source>
        <translation>The source audio track was not found:
%1</translation>
    </message>
</context>
<context>
    <name>tanara_gui::RecordBar</name>
    <message>
        <source>Megbeszélés</source>
        <translation>Meeting</translation>
    </message>
    <message>
        <source>Hangforrások</source>
        <translation>Audio sources</translation>
    </message>
    <message>
        <source> Rögzítendő hangforrások módosítása</source>
        <translation> Change audio sources to record</translation>
    </message>
    <message>
        <source>Nincs kiválasztott hangforrás</source>
        <translation>No audio source selected</translation>
    </message>
    <message numerus="yes">
        <source>● Rögzítés — %n hangforrás</source>
        <translation>
            <numerusform>● Recording — %n audio source</numerusform>
            <numerusform>● Recording — %n audio sources</numerusform>
        </translation>
    </message>
    <message>
        <source>a hívás</source>
        <translation>the call</translation>
    </message>
    <message>
        <source>Vége a meetingnek?</source>
        <translation>Is the meeting over?</translation>
    </message>
    <message>
        <source>Úgy tűnik, véget ért: %1 (az app leállt vagy elengedte a mikrofont).

Leállítsam a rögzítést?</source>
        <translation>It looks like it has ended: %1 (the app quit or released the microphone).

Stop the recording?</translation>
    </message>
    <message numerus="yes">
        <source>🎙 %n hangforrás kiválasztva</source>
        <translation>
            <numerusform>🎙 %n audio source selected</numerusform>
            <numerusform>🎙 %n audio sources selected</numerusform>
        </translation>
    </message>
    <message>
        <source>  (alapértelmezett)</source>
        <translation>  (default)</translation>
    </message>
    <message>
        <source>⏹  Leállítás
%1</source>
        <translation>⏹  Stop
%1</translation>
    </message>
    <message>
        <source>⏺  Felvétel indítása</source>
        <translation>⏺  Start recording</translation>
    </message>
    <message>
        <source>Leállítás…</source>
        <translation>Stopping…</translation>
    </message>
    <message>
        <source>Kódolás…</source>
        <translation>Encoding…</translation>
    </message>
</context>
<context>
    <name>tanara_gui::SettingsDialog</name>
    <message>
        <source>Beállítások</source>
        <translation>Settings</translation>
    </message>
    <message>
        <source>Tallózás…</source>
        <translation>Browse…</translation>
    </message>
    <message>
        <source>Azonosítás</source>
        <translation>Identification</translation>
    </message>
    <message>
        <source>Saját beszélő neve:</source>
        <translation>Your speaker name:</translation>
    </message>
    <message>
        <source>Rendszer nyelve (automatikus)</source>
        <translation>System language (automatic)</translation>
    </message>
    <message>
        <source>A nyelv váltása a következő indításkor lép életbe.</source>
        <translation>The language change takes effect on the next start.</translation>
    </message>
    <message>
        <source>Nyelv / Language:</source>
        <translation>Language / Nyelv:</translation>
    </message>
    <message>
        <source>Mappák</source>
        <translation>Folders</translation>
    </message>
    <message>
        <source>Felvételek mappája:</source>
        <translation>Recordings folder:</translation>
    </message>
    <message>
        <source>Felvételek mappája</source>
        <translation>Recordings folder</translation>
    </message>
    <message>
        <source>Jegyzetek mappája:</source>
        <translation>Notes folder:</translation>
    </message>
    <message>
        <source>Jegyzetek mappája</source>
        <translation>Notes folder</translation>
    </message>
    <message>
        <source>Metaadat mappája:</source>
        <translation>Metadata folder:</translation>
    </message>
    <message>
        <source>Metaadat mappája</source>
        <translation>Metadata folder</translation>
    </message>
    <message>
        <source>Általános</source>
        <translation>General</translation>
    </message>
    <message>
        <source>Automatikus rögzítés (minden eszköz)</source>
        <translation>Automatic recording (all devices)</translation>
    </message>
    <message>
        <source>Bekapcsolva minden bemenetet rögzít; a csendes sávokat a felvétel után automatikusan eldobja (a fájl megmarad, visszaállítható). Kikapcsolva az alább kijelölt eszközöket rögzíti.</source>
        <translation>When enabled, records every input; silent tracks are automatically discarded after the recording (the file is kept and can be restored). When disabled, records the devices selected below.</translation>
    </message>
    <message>
        <source>Rögzítendő eszközök (alapértelmezés)</source>
        <translation>Devices to record (default)</translation>
    </message>
    <message>
        <source>Mely eszközöket (sávokat) vegye fel alapból kézi módban. A vonalbemenet/AUX alapból kimarad (kézzel bepipálható). Auto-rögzítésnél ez a választás nem számít.</source>
        <translation>Which devices (tracks) to record by default in manual mode. Line-in/AUX is left out by default (can be checked manually). In auto-recording mode this selection does not matter.</translation>
    </message>
    <message>
        <source>Hangminőség és lekeverés</source>
        <translation>Audio quality and mixdown</translation>
    </message>
    <message>
        <source>Legjobb (64 kbps)</source>
        <translation>Best (64 kbps)</translation>
    </message>
    <message>
        <source>Magas (48 kbps)</source>
        <translation>High (48 kbps)</translation>
    </message>
    <message>
        <source>Közepes (32 kbps)</source>
        <translation>Medium (32 kbps)</translation>
    </message>
    <message>
        <source>Takarékos (24 kbps)</source>
        <translation>Economy (24 kbps)</translation>
    </message>
    <message>
        <source>Hangminőség:</source>
        <translation>Audio quality:</translation>
    </message>
    <message>
        <source>~16 MB / sáv / 1,5h — a legkisebb, STT-talp.</source>
        <translation>~16 MB / track / 1.5h — smallest, the STT floor.</translation>
    </message>
    <message>
        <source>~22 MB / sáv / 1,5h — jó beszédre.</source>
        <translation>~22 MB / track / 1.5h — good for speech.</translation>
    </message>
    <message>
        <source>~32 MB / sáv / 1,5h.</source>
        <translation>~32 MB / track / 1.5h.</translation>
    </message>
    <message>
        <source>~43 MB / sáv / 1,5h — a legjobb (jelenlegi alap).</source>
        <translation>~43 MB / track / 1.5h — best (current default).</translation>
    </message>
    <message>
        <source>Automatikusan, a felvétel után (háttérben)</source>
        <translation>Automatically, after the recording (in the background)</translation>
    </message>
    <message>
        <source>Kézzel, később (a felvétel paneljéből)</source>
        <translation>Manually, later (from the recording panel)</translation>
    </message>
    <message>
        <source>A lekevert, normalizált fájl csak kényelmes hallgatásra kell — az átíráshoz nem. Automatikus módban a felvétel után a háttérben készül el (nem fagyaszt, közben új felvétel is indítható). Kézi módban a felvétel review-paneljén indíthatod.</source>
        <translation>The mixed-down, normalized file is only for convenient listening — not needed for transcription. In automatic mode it is created in the background after the recording (no freezing; you can even start a new recording meanwhile). In manual mode you can start it from the recording review panel.</translation>
    </message>
    <message>
        <source>Lekeverés:</source>
        <translation>Mixdown:</translation>
    </message>
    <message>
        <source>Rögzítés</source>
        <translation>Recording</translation>
    </message>
    <message>
        <source>Aktív hívás észlelése (a tálca-figyelő felajánlja a rögzítést)</source>
        <translation>Detect active calls (the tray watcher offers to record)</translation>
    </message>
    <message>
        <source>Bekapcsolva a háttér-figyelő időnként megnézi, fogja-e egy ismert hívás-app a mikrofont, és értesítéssel felajánlja a felvétel indítását. Csak figyel — a rögzítéshez a felvevőt indítja.</source>
        <translation>When enabled, the background watcher periodically checks whether a known call app is holding the microphone, and offers to start recording via a notification. It only watches — for recording it launches the recorder.</translation>
    </message>
    <message>
        <source> mp</source>
        <translation> s</translation>
    </message>
    <message>
        <source>Milyen gyakran nézzen körül a figyelő. Rövidebb = gyorsabb felajánlás, több CPU.</source>
        <translation>How often the watcher looks around. Shorter = faster offers, more CPU.</translation>
    </message>
    <message>
        <source>Ellenőrzés gyakorisága:</source>
        <translation>Check interval:</translation>
    </message>
    <message>
        <source>A figyelő induljon bejelentkezéskor</source>
        <translation>Start the watcher at login</translation>
    </message>
    <message>
        <source>Bejelentkezéskor automatikusan elindul a háttér-figyelő (a rendszertálcára dokkolva).</source>
        <translation>The background watcher starts automatically at login (docked in the system tray).</translation>
    </message>
    <message>
        <source>Felvétel közben kérdezzen rá a leállításra, ha a hívás véget ér</source>
        <translation>While recording, ask whether to stop when the call ends</translation>
    </message>
    <message>
        <source>A rögzítő is figyeli a hívást: ha a hívás-app leáll vagy elengedi a mikrofont, felugró kérdéssel ajánlja a rögzítés leállítását. Magától sosem állít le.</source>
        <translation>The recorder also watches the call: when the call app quits or releases the microphone, a pop-up question offers to stop the recording. It never stops on its own.</translation>
    </message>
    <message>
        <source>Ismert hívás-appok</source>
        <translation>Known call apps</translation>
    </message>
    <message>
        <source>Soronként egy app (bináris- vagy név-részlet, pl. „zoom”, „teams”). A figyelő ezekre jelez, ha aktívan fogják a mikrofont.</source>
        <translation>One app per line (binary or name fragment, e.g. &quot;zoom&quot;, &quot;teams&quot;). The watcher alerts on these when they are actively holding the microphone.</translation>
    </message>
    <message>
        <source>Figyelő</source>
        <translation>Watcher</translation>
    </message>
    <message>
        <source>Az átírást és az összefoglalót KÜLSŐ szolgáltatások végzik a saját kulcsoddal / végpontoddal — ezeket nem a Tanara futtatja. Bármikor válthatsz, nincs lock-in.</source>
        <translation>Transcription and summarization are done by EXTERNAL services with your own key / endpoint — Tanara does not run these. You can switch anytime, no lock-in.</translation>
    </message>
    <message>
        <source>Szolgáltató:</source>
        <translation>Provider:</translation>
    </message>
    <message>
        <source>Átírás (STT)</source>
        <translation>Transcription (STT)</translation>
    </message>
    <message>
        <source>Összefoglaló (LLM)</source>
        <translation>Summary (LLM)</translation>
    </message>
    <message>
        <source>Külső szolgáltatások</source>
        <translation>External services</translation>
    </message>
    <message>
        <source>Az összefoglalót készítő modell rendszer-promptjai (utasítások + JSON-séma). A választóval válthatsz az egyszerű, egy-körös prompt és a komplex (több körös) mód két prompt-ja között. A kapott átirat és a kontextus automatikusan a prompt UTÁN kerül a modellhez.</source>
        <translation>System prompts of the model that creates the summary (instructions + JSON schema). Use the selector to switch between the simple, single-round prompt and the two prompts of the complex (multi-round) mode. The transcript and the context are automatically sent to the model AFTER the prompt.</translation>
    </message>
    <message>
        <source>Összefoglaló nyelve:</source>
        <translation>Summary language:</translation>
    </message>
    <message>
        <source>Az elkészülő összefoglaló nyelve — bármilyen nyelvet beírhatsz szabad szöveggel. Független a felület nyelvétől. A szerkezeti szakaszcímek (## Döntések, ## Teendők) mindig magyarok maradnak.</source>
        <translation>The language of the generated summary — you can type any language as free text. Independent from the UI language. The structural section headers (## Döntések, ## Teendők) always stay Hungarian.</translation>
    </message>
    <message>
        <source>Prompt:</source>
        <translation>Prompt:</translation>
    </message>
    <message>
        <source>Egyszerű összefoglaló</source>
        <translation>Simple summary</translation>
    </message>
    <message>
        <source>Komplex — Téma-kinyerés (1. kör)</source>
        <translation>Complex — Topic extraction (round 1)</translation>
    </message>
    <message>
        <source>Komplex — Téma-elemzés (2. kör)</source>
        <translation>Complex — Topic analysis (round 2)</translation>
    </message>
    <message>
        <source>Rendszer-prompt…</source>
        <translation>System prompt…</translation>
    </message>
    <message>
        <source>Visszaállítás alapértelmezettre</source>
        <translation>Reset to default</translation>
    </message>
    <message>
        <source>Összefoglaló</source>
        <translation>Summary</translation>
    </message>
    <message>
        <source>Nincs észlelt hangeszköz.</source>
        <translation>No audio devices detected.</translation>
    </message>
    <message>
        <source>Mikrofonok</source>
        <translation>Microphones</translation>
    </message>
    <message>
        <source>Rendszerhang (loopback)</source>
        <translation>System audio (loopback)</translation>
    </message>
    <message>
        <source>Egyéb (vonalbemenet/AUX)</source>
        <translation>Other (line-in/AUX)</translation>
    </message>
    <message>
        <source>  (alapértelmezett)</source>
        <translation>  (default)</translation>
    </message>
    <message>
        <source>(változatlan, ha üresen hagyod)</source>
        <translation>(unchanged if left empty)</translation>
    </message>
    <message>
        <source>Modellek lekérése</source>
        <translation>Fetch models</translation>
    </message>
    <message>
        <source>Lekérés…</source>
        <translation>Fetching…</translation>
    </message>
    <message>
        <source>Nem sikerült lekérni a modelleket: %1</source>
        <translation>Could not fetch the models: %1</translation>
    </message>
</context>
<context>
    <name>tanara_gui::TracksPanel</name>
    <message>
        <source>A felvétel sávjai. A csendesnek ítélt sávok automatikusan „eldobott” állapotba kerülnek (a fájl megmarad). Visszaállíthatod, vagy véglegesen törölheted (fájllal együtt).</source>
        <translation>Tracks of the recording. Tracks judged silent are automatically put into &quot;discarded&quot; state (the file is kept). You can restore them, or delete them permanently (file included).</translation>
    </message>
    <message>
        <source>▶ Meghallgatás</source>
        <translation>▶ Listen</translation>
    </message>
    <message>
        <source>A kijelölt sáv lejátszása (eldobott sávé is, hogy törlés előtt ellenőrizhető legyen).</source>
        <translation>Plays the selected track (discarded tracks too, so you can check before deleting).</translation>
    </message>
    <message>
        <source>Visszaállítás</source>
        <translation>Restore</translation>
    </message>
    <message>
        <source>🗑 Törlés (végleges)</source>
        <translation>🗑 Delete (permanent)</translation>
    </message>
    <message>
        <source>🔀 Lekeverés frissítése</source>
        <translation>🔀 Update mixdown</translation>
    </message>
    <message>
        <source>A kevert hang (mixdown.mp3) újragenerálása az AKTÍV sávokból — a lejátszó ezt használja. Sáv eldobása/törlése után érdemes frissíteni.</source>
        <translation>Regenerates the mixed audio (mixdown.mp3) from the ACTIVE tracks — the player uses this. Worth updating after discarding/deleting a track.</translation>
    </message>
    <message>
        <source>%1  (%2)</source>
        <translation>%1  (%2)</translation>
    </message>
    <message>
        <source>  —  ELDOBOTT (csendes)</source>
        <translation>  —  DISCARDED (silent)</translation>
    </message>
    <message>
        <source>⏳ Lekeverés folyamatban…</source>
        <translation>⏳ Mixdown in progress…</translation>
    </message>
    <message>
        <source>🔀 Lekeverés frissítése (elavult)</source>
        <translation>🔀 Update mixdown (outdated)</translation>
    </message>
    <message>
        <source>Meghallgatás</source>
        <translation>Listen</translation>
    </message>
    <message>
        <source>A hangsáv-fájl nem található:
%1</source>
        <translation>The audio track file was not found:
%1</translation>
    </message>
    <message>
        <source>Sáv törlése</source>
        <translation>Delete track</translation>
    </message>
    <message>
        <source>Véglegesen törlöd ezt a hangsávot? A hangfájl fizikailag törlődik, ez nem visszavonható.</source>
        <translation>Permanently delete this audio track? The audio file is physically deleted; this cannot be undone.</translation>
    </message>
</context>
<context>
    <name>tanara_gui::TranscriptPlayer</name>
    <message>
        <source>▶ Lejátszás</source>
        <translation>▶ Play</translation>
    </message>
    <message>
        <source>Hangerő</source>
        <translation>Volume</translation>
    </message>
    <message>
        <source>Beszélők:</source>
        <translation>Speakers:</translation>
    </message>
    <message>
        <source> (én)</source>
        <translation> (me)</translation>
    </message>
    <message>
        <source>Kattints a névadáshoz / átnevezéshez</source>
        <translation>Click to name / rename</translation>
    </message>
    <message>
        <source>„%1” — átnevezés / kezelés</source>
        <translation>&quot;%1&quot; — rename / manage</translation>
    </message>
    <message>
        <source>„%1” — ki ez a beszélő?</source>
        <translation>&quot;%1&quot; — who is this speaker?</translation>
    </message>
    <message>
        <source>✏  Új név…</source>
        <translation>✏  New name…</translation>
    </message>
    <message>
        <source>▶  Meghallgatás</source>
        <translation>▶  Listen</translation>
    </message>
    <message>
        <source>✖  Név törlése (ismeretlen)</source>
        <translation>✖  Clear name (unknown)</translation>
    </message>
    <message>
        <source>Új név</source>
        <translation>New name</translation>
    </message>
    <message>
        <source>Ki ez a beszélő? (a hangja a személy-adatbázisba kerül)</source>
        <translation>Who is this speaker? (their voice is added to the people database)</translation>
    </message>
    <message>
        <source>Nincs átirat — futtass Átírást.

(A felvételbe addig is belehallgathatsz lent a lejátszóval.)</source>
        <translation>No transcript — run Transcription.

(Meanwhile you can listen to the recording with the player below.)</translation>
    </message>
    <message>
        <source>⏸ Szünet</source>
        <translation>⏸ Pause</translation>
    </message>
</context>
<context>
    <name>tanara_watcher::TrayWatcher</name>
    <message>
        <source>Rögzítés azonnali indítása</source>
        <translation>Start recording immediately</translation>
    </message>
    <message>
        <source>Rögzítő megnyitása…</source>
        <translation>Open recorder…</translation>
    </message>
    <message>
        <source>Elemző megnyitása</source>
        <translation>Open analyzer</translation>
    </message>
    <message>
        <source>Kilépés</source>
        <translation>Quit</translation>
    </message>
    <message>
        <source>Rögzítő megnyitása</source>
        <translation>Open recorder</translation>
    </message>
    <message>
        <source>Hívás észlelve — %1</source>
        <translation>Call detected — %1</translation>
    </message>
    <message>
        <source>Aktív hívást észleltem.</source>
        <translation>An active call was detected.</translation>
    </message>
    <message>
        <source>A Tanara tálca-ikonra kattintva indíthatod a rögzítést (azonnali indítás vagy a rögzítő megnyitása).</source>
        <translation>Click the Tanara tray icon to start recording (immediate start or open the recorder).</translation>
    </message>
    <message>
        <source>Már folyik felvétel</source>
        <translation>Recording already in progress</translation>
    </message>
    <message>
        <source>Egy rögzítés már fut.</source>
        <translation>A recording is already running.</translation>
    </message>
    <message>
        <source>Tanara — felvétel folyamatban</source>
        <translation>Tanara — recording in progress</translation>
    </message>
    <message>
        <source>Tanara — hívás észlelve: %1</source>
        <translation>Tanara — call detected: %1</translation>
    </message>
    <message>
        <source>Tanara — figyel</source>
        <translation>Tanara — watching</translation>
    </message>
</context>
</TS>

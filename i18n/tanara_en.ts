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
        <source>Cím (URL)</source>
        <translation>Address (URL)</translation>
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
    <message>
        <source>Tanara Cloud — bejelentkezés</source>
        <translation>Tanara Cloud — sign in</translation>
    </message>
    <message>
        <source>Helyi végpont</source>
        <translation>Local endpoint</translation>
    </message>
    <message>
        <source>Többnyelvű beágyazó modell (pl. bge-m3). Modellváltáskor a könyvtár előkészítése elölről indul.</source>
        <translation>Multilingual embedding model (e.g. bge-m3). Changing the model restarts preparing the library from scratch.</translation>
    </message>
    <message>
        <source>Tanara Cloud</source>
        <translation>Tanara Cloud</translation>
    </message>
</context>
<context>
    <name>CandidateRanker</name>
    <message>
        <source>nincs hangadat</source>
        <translation>no voice data</translation>
    </message>
    <message>
        <source>hang %1</source>
        <translation>voice %1</translation>
    </message>
    <message>
        <source>a megerősített soraihoz mérve</source>
        <translation>measured against their confirmed lines</translation>
    </message>
    <message>
        <source>a hanglenyomatához mérve</source>
        <translation>measured against their voiceprint</translation>
    </message>
    <message>
        <source>a többi sorához mérve</source>
        <translation>measured against their other lines</translation>
    </message>
    <message>
        <source>másik sávon beszél</source>
        <translation>speaks on a different track</translation>
    </message>
    <message>
        <source>%1: %2; ez: %3</source>
        <translation>%1: %2; this one: %3</translation>
    </message>
    <message>
        <source>a hívás hangján</source>
        <translation>on the call audio</translation>
    </message>
    <message>
        <source>a mikrofonon</source>
        <translation>on the microphone</translation>
    </message>
    <message>
        <source>kézi sáv-beosztás</source>
        <translation>manual track assignment</translation>
    </message>
    <message>
        <source>kizárva: a sávja szerint nem ő</source>
        <translation>excluded: not them, per their track</translation>
    </message>
    <message>
        <source>%1 kézi sáv-beosztása: %2; ez a sor: %3</source>
        <translation>%1 manual track assignment: %2; this line: %3</translation>
    </message>
    <message numerus="yes">
        <source>%n sor itt</source>
        <translation>
            <numerusform>%n line here</numerusform>
            <numerusform>%n lines here</numerusform>
        </translation>
    </message>
    <message>
        <source>nincs hanglenyomata</source>
        <translation>has no voiceprint</translation>
    </message>
    <message>
        <source>a sorai a másik sávon szólnak</source>
        <translation>its lines are on the other track</translation>
    </message>
    <message>
        <source>hasonló hang: %1</source>
        <translation>similar voice: %1</translation>
    </message>
    <message>
        <source>a két megerősített hang %1-ban egyezik</source>
        <translation>the two confirmed voices match at %1</translation>
    </message>
    <message>
        <source>mikrofon</source>
        <translation>microphone</translation>
    </message>
    <message>
        <source>hívás hangja</source>
        <translation>call audio</translation>
    </message>
    <message>
        <source>vegyes</source>
        <translation>mixed</translation>
    </message>
    <message>
        <source>ismeretlen</source>
        <translation>unknown</translation>
    </message>
    <message>
        <source>%1 címkéje: %2 (%3 / %4 megbeszélésen volt ott).</source>
        <translation>Tag of %1: %2 (was there in %3 of %4 meetings).</translation>
    </message>
    <message>
        <source>A %1 megbeszéléseken szokott lenni (%2 / %3).</source>
        <translation>Usually attends the %1 meetings (%2 / %3).</translation>
    </message>
    <message>
        <source>Közös címkék: %1.</source>
        <translation>Tags in common: %1.</translation>
    </message>
    <message>
        <source>más címkék</source>
        <translation>other tags</translation>
    </message>
    <message>
        <source>A %1 %2 megbeszéléséből egyiken sem volt.</source>
        <translation>Was not at any of the %2 meetings of %1.</translation>
    </message>
    <message>
        <source>A %1 megbeszélésein egyiken sem volt.</source>
        <translation>Was not at any of the meetings of %1.</translation>
    </message>
    <message>
        <source>Más címkéken szokott lenni: %1.</source>
        <translation>Usually attends meetings with other tags: %1.</translation>
    </message>
</context>
<context>
    <name>CandidateRow</name>
    <message>
        <source>Rátesszük a címkét a személyre — egy kattintás, visszavonható</source>
        <translation>Apply the tag to the person — one click, undoable</translation>
    </message>
</context>
<context>
    <name>Cloud</name>
    <message>
        <source>%1 óra</source>
        <translation>%1 h</translation>
    </message>
    <message>
        <source>%1 perc</source>
        <translation>%1 min</translation>
    </message>
    <message>
        <source>ÁFA nélkül, fordított adózás</source>
        <translation>Excl. VAT, reverse charge</translation>
    </message>
    <message>
        <source>ÁFA-t tartalmaz</source>
        <translation>VAT included</translation>
    </message>
    <message>
        <source>Nem értük el a szervert. Ellenőrizd az internetkapcsolatot.</source>
        <translation>We couldn&apos;t reach the server. Check your internet connection.</translation>
    </message>
    <message>
        <source>Nincs elég egyenleg.</source>
        <translation>Not enough balance.</translation>
    </message>
    <message>
        <source>Ehhez a lépéshez legalább %1 fedezet kell az egyenlegeden. A tényleges díj ennél kevesebb lehet.</source>
        <translation>This step needs at least %1 available. The actual charge may be lower.</translation>
    </message>
    <message>
        <source>Ehhez a lépéshez %1 kell.</source>
        <translation>This step needs %1.</translation>
    </message>
    <message>
        <source>Az egyenleged: %1.</source>
        <translation>Your balance: %1.</translation>
    </message>
    <message>
        <source>Feltöltés: %1</source>
        <translation>Top up: %1</translation>
    </message>
    <message>
        <source>A feltöltéshez írj nekünk: %1</source>
        <translation>To top up, contact us: %1</translation>
    </message>
    <message>
        <source>Frissítsd a Tanarát a Tanara Cloudhoz: legalább %1 kell; neked %2 van. A saját kulcsos mód addig is működik.</source>
        <translation>Update Tanara to use Tanara Cloud (%1 or newer; you have %2). Your own-keys mode keeps working.</translation>
    </message>
    <message>
        <source>Letöltés: %1</source>
        <translation>Download: %1</translation>
    </message>
    <message>
        <source>Karbantartás miatt a Tanara Cloud most nem elérhető.</source>
        <translation>Tanara Cloud is under maintenance.</translation>
    </message>
    <message>
        <source>Tervezett karbantartás %1–%2 között.</source>
        <translation>Planned maintenance between %1 and %2.</translation>
    </message>
    <message>
        <source>A feldolgozó szolgáltatás átmenetileg nem elérhető. Próbáld újra később.</source>
        <translation>The processing service is temporarily unavailable. Please try again later.</translation>
    </message>
    <message>
        <source>Túl sok kérés érkezett. Próbáld újra %1 mp múlva.</source>
        <translation>Too many requests. Try again in %1 s.</translation>
    </message>
    <message>
        <source>Elérted ennek az eszköznek a költési limitjét.</source>
        <translation>This device reached its spending limit.</translation>
    </message>
    <message>
        <source>Beállítások a weben: %1</source>
        <translation>Settings on the web: %1</translation>
    </message>
    <message>
        <source>Ezt az eszközt leválasztották a fiókodról. Jelentkezz be újra.</source>
        <translation>This device was disconnected from your account. Please sign in again.</translation>
    </message>
    <message>
        <source>A fiókod fizetési vita miatt fel van függesztve. Részletek a weben.</source>
        <translation>Your account is suspended due to a payment dispute. Details on the web.</translation>
    </message>
    <message>
        <source>A Tanara Cloud fiókod fel van függesztve. Kérdés esetén írj a supportnak.</source>
        <translation>Your Tanara Cloud account is suspended. Contact support if you have questions.</translation>
    </message>
    <message>
        <source>A feldolgozáshoz el kell fogadnod az új ÁSZF-et (%1): %2</source>
        <translation>Accept the new Terms (%1) to continue processing: %2</translation>
    </message>
    <message>
        <source>A feldolgozás hibával leállt.</source>
        <translation>Processing stopped with an error.</translation>
    </message>
    <message>
        <source>Az eddig elkészült részek díja: %1. Folytathatod, ekkor csak a hátralévő részekért fizetsz.</source>
        <translation>Cost of the finished parts: %1. You can continue and pay only for the remaining parts.</translation>
    </message>
    <message>
        <source>Nem terheltünk semmit.</source>
        <translation>Nothing was charged.</translation>
    </message>
    <message>
        <source>Hibaazonosító: %1 — ha írsz nekünk, küldd el ezt is. A meeting tartalmát nem látjuk.</source>
        <translation>Error ID: %1 — include it if you contact us. We can&apos;t see your meeting content.</translation>
    </message>
</context>
<context>
    <name>CloudComingSoon</name>
    <message>
        <source>Tanara Cloud — hamarosan</source>
        <translation>Tanara Cloud — coming soon</translation>
    </message>
    <message>
        <source>A Tanara Cloud előkészületben van. Részletek és értesítés:</source>
        <translation>Tanara Cloud is in the works. Details and updates:</translation>
    </message>
    <message>
        <source>Tanara Cloud — hamarosan.</source>
        <translation>Tanara Cloud — coming soon.</translation>
    </message>
</context>
<context>
    <name>ConnectionTester</name>
    <message>
        <source>A végpont nem válaszol. Fut a helyi szerver (pl. LM Studio), és jó a port?</source>
        <translation>The endpoint does not respond. Is the local server (e.g. LM Studio) running, and is the port right?</translation>
    </message>
    <message>
        <source>A szolgáltató visszautasította a kapcsolatot. Jó a cím és a port?</source>
        <translation>The provider refused the connection. Are the address and the port right?</translation>
    </message>
    <message>
        <source>Nincs ilyen gép ezen a címen. Ellenőrizd a címet és az internetkapcsolatot.</source>
        <translation>No host was found at this address. Check the address and your internet connection.</translation>
    </message>
    <message>
        <source>A helyi szerver nem válaszolt időben. Lehet, hogy éppen modellt tölt be.</source>
        <translation>The local server did not respond in time. It may be loading a model.</translation>
    </message>
    <message>
        <source>A szolgáltató nem válaszolt időben. Ellenőrizd az internetkapcsolatot.</source>
        <translation>The provider did not respond in time. Check your internet connection.</translation>
    </message>
    <message>
        <source>A biztonságos kapcsolat nem jött létre (tanúsítványhiba). Helyi szervernél http:// kell a cím elejére?</source>
        <translation>The secure connection could not be established (certificate error). For a local server, should the address start with http://?</translation>
    </message>
    <message>
        <source>A végpont megszakította a kapcsolatot. Biztosan ez az API címe?</source>
        <translation>The endpoint closed the connection. Is this really the API address?</translation>
    </message>
    <message>
        <source>Nincs hálózati kapcsolat.</source>
        <translation>There is no network connection.</translation>
    </message>
    <message>
        <source>A cím nem használható. http:// vagy https:// kezdetű címet adj meg.</source>
        <translation>This address cannot be used. Enter an address that starts with http:// or https://.</translation>
    </message>
    <message>
        <source>A végpont nem érhető el.</source>
        <translation>The endpoint cannot be reached.</translation>
    </message>
    <message>
        <source>LM Studio — a beállított modell nincs a letöltött modellek között.</source>
        <translation>LM Studio — the configured model is not among the downloaded models.</translation>
    </message>
    <message>
        <source>LM Studio — a modell nincs betöltve; a Tanara az első feladatnál betölti a szükséges kontextussal, egy szálon.</source>
        <translation>LM Studio — the model is not loaded; Tanara loads it with the required context, single-threaded, at the first job.</translation>
    </message>
    <message>
        <source>LM Studio — betöltve %1 tokenes kontextussal, párhuzamos kérések: %2.</source>
        <translation>LM Studio — loaded with a %1-token context, parallel requests: %2.</translation>
    </message>
    <message>
        <source>LM Studio — a modell be van töltve.</source>
        <translation>LM Studio — the model is loaded.</translation>
    </message>
    <message>
        <source>A modell %1 párhuzamos kérésre van betöltve — ez sok videómemóriát foglal, és instabil lehet. A Tanara a következő feladat előtt egy szálon tölti újra.</source>
        <translation>The model is loaded for %1 parallel requests — this uses a lot of video memory and can be unstable. Tanara reloads it single-threaded before the next job.</translation>
    </message>
    <message>
        <source>A betöltött kontextus (%1 token) kevés egy tipikus összefoglaló-részhez (kb. %2 token). A Tanara a feladat előtt nagyobb kontextussal tölti újra.</source>
        <translation>The loaded context (%1 tokens) is too small for a typical summary part (about %2 tokens). Tanara reloads it with a larger context before the job.</translation>
    </message>
    <message>
        <source>A modell maximuma: %1 token.</source>
        <translation>The model&apos;s maximum: %1 tokens.</translation>
    </message>
    <message>
        <source>Más betöltött modell: %1.</source>
        <translation>Other loaded model: %1.</translation>
    </message>
    <message>
        <source>A szolgáltató elutasította a kulcsot. Ellenőrizd, hogy jól másoltad-e be, és nem járt-e le.</source>
        <translation>The provider rejected the key. Check that you pasted it correctly and that it has not expired.</translation>
    </message>
    <message>
        <source>A végpont API-kulcsot kér. Add meg a kulcsot.</source>
        <translation>The endpoint requires an API key. Enter the key.</translation>
    </message>
    <message>
        <source>A cím válaszol, de nem találja az API-t. A cím vége általában /v1.</source>
        <translation>The address responds, but the API was not found there. The address usually ends in /v1.</translation>
    </message>
    <message>
        <source>A szolgáltató elérhető, de most korlátozza a kéréseket. Próbáld újra kicsit később.</source>
        <translation>The provider is reachable but is limiting requests right now. Try again a little later.</translation>
    </message>
    <message>
        <source>A helyi szerver hibát jelzett. Nézd meg a szerver naplóját.</source>
        <translation>The local server reported an error. Check the server&apos;s log.</translation>
    </message>
    <message>
        <source>A szolgáltató hibát jelzett. Próbáld újra később.</source>
        <translation>The provider reported an error. Try again later.</translation>
    </message>
    <message>
        <source>A végpont váratlan választ adott.</source>
        <translation>The endpoint gave an unexpected response.</translation>
    </message>
    <message>
        <source>Ehhez a szolgáltatóhoz nincs kapcsolat-teszt.</source>
        <translation>There is no connection test for this provider.</translation>
    </message>
    <message>
        <source>A cím válaszol, de nem a várt API-választ adja. Biztosan ez az API címe?</source>
        <translation>The address responds, but not with the expected API response. Is this really the API address?</translation>
    </message>
    <message>
        <source>A beállított modell (%1) nincs a szolgáltató listájában.</source>
        <translation>The configured model (%1) is not in the provider&apos;s list.</translation>
    </message>
</context>
<context>
    <name>ContaminatedCoreBanner</name>
    <message>
        <source>(%1 / %2 sor)</source>
        <translation>(%1 / %2 lines)</translation>
    </message>
    <message>
        <source>Szétválasztás</source>
        <translation>Split</translation>
    </message>
    <message>
        <source> — visszavonható: Ctrl+Z</source>
        <translation> — undo with Ctrl+Z</translation>
    </message>
    <message>
        <source>Miért?</source>
        <translation>Why?</translation>
    </message>
</context>
<context>
    <name>EmptyLibraryView</name>
    <message>
        <source>Vegyük fel az első megbeszélést</source>
        <translation>Let&apos;s record your first meeting</translation>
    </message>
    <message>
        <source>A Tanara minden hangforrást külön sávra rögzít, utána átiratot és összefoglalót készít. A felvétel a gépeden marad.</source>
        <translation>Tanara records every audio source on its own track, then creates a transcript and a summary. The recording stays on your computer.</translation>
    </message>
    <message>
        <source>Felvétel</source>
        <translation>Recording</translation>
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
        <source>Felvétel indítása</source>
        <translation>Start recording</translation>
    </message>
    <message>
        <source>Hangfájl importálása</source>
        <translation>Import audio file</translation>
    </message>
    <message>
        <source>Hívásfigyelő beállítása</source>
        <translation>Set up call watcher</translation>
    </message>
    <message>
        <source>Már megvan a felvétel? Importálj hang- vagy videófájlt (ide is húzhatod): ugyanúgy készül belőle átirat és összefoglaló.</source>
        <translation>Already have a recording? Import an audio or video file (you can also drop it here): it gets a transcript and a summary just the same.</translation>
    </message>
    <message>
        <source>Másik gépen rögzített megbeszélést hoznál át (.tanara.zip vagy kimásolt mappa)?</source>
        <translation>Bringing over a meeting recorded on another machine (.tanara.zip or a copied folder)?</translation>
    </message>
    <message>
        <source>Archívum importálása</source>
        <translation>Import archive</translation>
    </message>
    <message>
        <source>Mappa importálása</source>
        <translation>Import folder</translation>
    </message>
    <message>
        <source>A hívásfigyelő a tálcán fut, és szól, ha Teams, Meet vagy Zoom hívást észlel. A felvételt mindig te indítod.</source>
        <translation>The call watcher runs in the tray and notifies you when it detects a Teams, Meet or Zoom call. You always start the recording yourself.</translation>
    </message>
</context>
<context>
    <name>EvidenceReason</name>
    <message>
        <source>Egy kattintás, visszavonható</source>
        <translation>One click, undoable</translation>
    </message>
</context>
<context>
    <name>JobErrors</name>
    <message>
        <source>A modell %1 tokenes kontextussal van betöltve, a kérés %2 token volt — nem fér bele.</source>
        <translation>The model is loaded with a %1-token context, the request was %2 tokens — it does not fit.</translation>
    </message>
    <message>
        <source>A modell %1 tokenes kontextussal van betöltve, és a kérés nem fért bele.</source>
        <translation>The model is loaded with a %1-token context, and the request did not fit.</translation>
    </message>
    <message>
        <source>A kérés nem fért bele a modell kontextusába.</source>
        <translation>The request did not fit into the model&apos;s context.</translation>
    </message>
    <message>
        <source>A Tanara Cloud modellje ekkora kérést nem tud feldolgozni.</source>
        <translation>The Tanara Cloud model cannot process a request this large.</translation>
    </message>
    <message>
        <source>A Tanara újra tudja tölteni az LM Studióban legalább %1 tokenes kontextussal, és újraindítja a feladatot.</source>
        <translation>Tanara can reload it in LM Studio with a context of at least %1 tokens and restart the job.</translation>
    </message>
    <message>
        <source>Töltsd be nagyobb kontextussal, és próbáld újra.</source>
        <translation>Load it with a larger context and try again.</translation>
    </message>
    <message>
        <source>Töltsd be a modellt a szerveren legalább %1 tokenes kontextussal, vagy válassz nagyobb kontextusú modellt.</source>
        <translation>Load the model on the server with a context of at least %1 tokens, or choose a model with a larger context.</translation>
    </message>
    <message>
        <source>Töltsd be a modellt a szerveren nagyobb kontextussal, vagy válassz nagyobb kontextusú modellt.</source>
        <translation>Load the model on the server with a larger context, or choose a model with a larger context.</translation>
    </message>
    <message>
        <source>kérés %1 token</source>
        <translation>request %1 tokens</translation>
    </message>
    <message>
        <source>kontextus %1 token</source>
        <translation>context %1 tokens</translation>
    </message>
    <message>
        <source>hálózat · nincs válasz</source>
        <translation>network · no response</translation>
    </message>
    <message>
        <source>hálózat · %1</source>
        <translation>network · %1</translation>
    </message>
    <message>
        <source>Ismeretlen hiba történt.</source>
        <translation>An unknown error occurred.</translation>
    </message>
    <message>
        <source>Nem sikerült elérni a szolgáltatót. Ellenőrizd a hálózati kapcsolatot és a szolgáltató címét.</source>
        <translation>Could not reach the provider. Check your network connection and the provider address.</translation>
    </message>
    <message>
        <source>A szolgáltató nem fogadta el az API-kulcsot. Ellenőrizd vagy cseréld le a kulcsot a Beállításokban.</source>
        <translation>The provider rejected the API key. Check or replace the key in Settings.</translation>
    </message>
    <message>
        <source>A szolgáltatónál elfogyott az egyenleg vagy a keret.</source>
        <translation>The provider account is out of balance or quota.</translation>
    </message>
    <message>
        <source>A szolgáltató nem ismeri a beállított modellt vagy címet.</source>
        <translation>The provider does not recognise the configured model or address.</translation>
    </message>
    <message>
        <source>A szolgáltató nem válaszolt időben. Próbáld újra.</source>
        <translation>The provider did not respond in time. Try again.</translation>
    </message>
    <message>
        <source>A szolgáltató szerint túl nagy a kérés (túl hosszú felvétel vagy átirat).</source>
        <translation>The provider says the request is too large (the recording or transcript is too long).</translation>
    </message>
    <message>
        <source>A szolgáltató átmenetileg korlátozza a kéréseket (túl sok kérés vagy elfogyott keret). Próbáld újra később.</source>
        <translation>The provider is temporarily limiting requests (too many requests or the quota is used up). Try again later.</translation>
    </message>
    <message>
        <source>A szolgáltatónál hiba történt. Próbáld újra később.</source>
        <translation>The provider reported an error. Try again later.</translation>
    </message>
    <message>
        <source>A szolgáltató elutasította a kérést.</source>
        <translation>The provider rejected the request.</translation>
    </message>
    <message>
        <source>Nem sikerült elérni a Tanara Cloudot. Ellenőrizd a hálózati kapcsolatot.</source>
        <translation>Could not reach Tanara Cloud. Check your network connection.</translation>
    </message>
    <message>
        <source>A Tanara Cloud hibát jelzett.</source>
        <translation>Tanara Cloud reported an error.</translation>
    </message>
</context>
<context>
    <name>JobStageRow</name>
    <message>
        <source>kihagyva</source>
        <translation>skipped</translation>
    </message>
    <message>
        <source>nem sikerült</source>
        <translation>failed</translation>
    </message>
    <message>
        <source>vár</source>
        <translation>waiting</translation>
    </message>
</context>
<context>
    <name>JobSupport</name>
    <message>
        <source>Beállítások</source>
        <translation>Settings</translation>
    </message>
</context>
<context>
    <name>LibraryFilterPopover</name>
    <message>
        <source>Állapot</source>
        <translation>Status</translation>
    </message>
    <message>
        <source>Nincs összefoglaló</source>
        <translation>No summary</translation>
    </message>
    <message>
        <source>Nincs átirat</source>
        <translation>No transcript</translation>
    </message>
    <message>
        <source>Résztvevő</source>
        <translation>Participant</translation>
    </message>
    <message>
        <source>Címke</source>
        <translation>Tag</translation>
    </message>
    <message>
        <source>Bármelyik</source>
        <translation>Any</translation>
    </message>
    <message>
        <source>Mindegyik</source>
        <translation>All</translation>
    </message>
    <message>
        <source>Címke keresése</source>
        <translation>Search tags</translation>
    </message>
    <message>
        <source>Nincs ilyen címke</source>
        <translation>No such tag</translation>
    </message>
    <message>
        <source>Címke nélkül</source>
        <translation>Untagged</translation>
    </message>
    <message>
        <source>Címkék kezelése…</source>
        <translation>Manage tags…</translation>
    </message>
</context>
<context>
    <name>LibraryItem</name>
    <message>
        <source>címke:</source>
        <translation>tag:</translation>
    </message>
</context>
<context>
    <name>LibrarySelectionPanel</name>
    <message numerus="yes">
        <source>%n megbeszélés kijelölve</source>
        <translation>
            <numerusform>%n meeting selected</numerusform>
            <numerusform>%n meetings selected</numerusform>
        </translation>
    </message>
    <message>
        <source>Ctrl+kattintás: hozzáadás vagy elvétel · Shift+kattintás: tartomány · Esc: kijelölés megszüntetése</source>
        <translation>Ctrl+click: add or remove · Shift+click: range · Esc: clear selection</translation>
    </message>
    <message>
        <source>Címkék</source>
        <translation>Tags</translation>
    </message>
    <message>
        <source>A kijelölt megbeszéléseken még nincs címke.</source>
        <translation>The selected meetings have no tags yet.</translation>
    </message>
    <message numerus="yes">
        <source>Kattintásra mind a %n megbeszélésre felkerül</source>
        <translation>
            <numerusform>Click to add to the %n meeting</numerusform>
            <numerusform>Click to add to all %n meetings</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Címke hozzáadása mind a %n megbeszéléshez…</source>
        <translation>
            <numerusform>Add a tag to the %n meeting…</numerusform>
            <numerusform>Add a tag to all %n meetings…</numerusform>
        </translation>
    </message>
    <message>
        <source>A szaggatott keretes címke csak némelyiken van rajta: kattintásra mindegyikre felkerül. Egy lépésben visszavonható.</source>
        <translation>A tag with a dashed border is only on some of them: click to add it to all. Can be undone in one step.</translation>
    </message>
    <message>
        <source>Kijelölés megszüntetése</source>
        <translation>Clear selection</translation>
    </message>
</context>
<context>
    <name>LibrarySidebar</name>
    <message>
        <source>Új felvétel</source>
        <translation>New recording</translation>
    </message>
    <message>
        <source>A felvevő megnyitása külön ablakban (Ctrl+N)</source>
        <translation>Open the recorder in a separate window (Ctrl+N)</translation>
    </message>
    <message>
        <source>Hangfájl importálása… (Ctrl+I)</source>
        <translation>Import audio file… (Ctrl+I)</translation>
    </message>
    <message>
        <source>Keresés</source>
        <translation>Search</translation>
    </message>
    <message>
        <source>Nincs átirat</source>
        <translation>No transcript</translation>
    </message>
    <message>
        <source>Nincs összefoglaló</source>
        <translation>No summary</translation>
    </message>
    <message>
        <source>Címke nélkül</source>
        <translation>Untagged</translation>
    </message>
    <message>
        <source>Szűrő</source>
        <translation>Filter</translation>
    </message>
    <message>
        <source>Még nincs megbeszélés</source>
        <translation>No meetings yet</translation>
    </message>
    <message>
        <source>A felvételek itt jelennek meg, legújabb felül.</source>
        <translation>Recordings appear here, newest first.</translation>
    </message>
    <message>
        <source>Nincs találat</source>
        <translation>No results</translation>
    </message>
    <message>
        <source>Próbálj más kifejezést, vagy vedd le a szűrőket.</source>
        <translation>Try a different term, or clear the filters.</translation>
    </message>
    <message>
        <source>Szűrők törlése</source>
        <translation>Clear filters</translation>
    </message>
    <message>
        <source>Személyek</source>
        <translation>People</translation>
    </message>
    <message>
        <source>Címkék</source>
        <translation>Tags</translation>
    </message>
    <message>
        <source>Beállítások · Ctrl+,</source>
        <translation>Settings · Ctrl+,</translation>
    </message>
    <message>
        <source>Átnevezés</source>
        <translation>Rename</translation>
    </message>
    <message>
        <source>Megnyitás mappában</source>
        <translation>Show in folder</translation>
    </message>
    <message>
        <source>Törlés…</source>
        <translation>Delete…</translation>
    </message>
</context>
<context>
    <name>LinePopover</name>
    <message>
        <source>%1 címkéje lett: #%2</source>
        <translation>%1 now has the tag #%2</translation>
    </message>
    <message>
        <source>Meghallgatás: ez a sor</source>
        <translation>Listen: this line</translation>
    </message>
    <message>
        <source>Meghallgatás: egy jellemző, hosszabb megszólalás ettől a beszélőtől</source>
        <translation>Listen: a typical, longer utterance from this speaker</translation>
    </message>
    <message>
        <source>Meghallgatás</source>
        <translation>Listen</translation>
    </message>
    <message>
        <source>Kinek a sora ez?</source>
        <translation>Whose line is this?</translation>
    </message>
    <message numerus="yes">
        <source>Kié a kijelölt %n sor?</source>
        <translation>
            <numerusform>Whose is the %n selected line?</numerusform>
            <numerusform>Whose are the %n selected lines?</numerusform>
        </translation>
    </message>
    <message>
        <source>%1 valójában…</source>
        <translation>%1 is actually…</translation>
    </message>
    <message>
        <source>A kattintott sor most: %1</source>
        <translation>The clicked line is now: %1</translation>
    </message>
    <message>
        <source>Most: %1 · %2</source>
        <translation>Now: %1 · %2</translation>
    </message>
    <message>
        <source>Miért ő?</source>
        <translation>Why them?</translation>
    </message>
    <message>
        <source>Csak ez a sor</source>
        <translation>Only this line</translation>
    </message>
    <message numerus="yes">
        <source>Kijelölt %n sor</source>
        <translation>
            <numerusform>%n selected line</numerusform>
            <numerusform>%n selected lines</numerusform>
        </translation>
    </message>
    <message>
        <source>%1 mind az %2 sora</source>
        <translation>All %2 lines are %1&apos;s</translation>
    </message>
    <message>
        <source>%1 mind a %2 sora</source>
        <translation>All %2 lines are %1&apos;s</translation>
    </message>
    <message>
        <source>Az új személy neve (üresen: névtelen)</source>
        <translation>New person&apos;s name (leave empty for unnamed)</translation>
    </message>
    <message>
        <source>Név keresése vagy új személy</source>
        <translation>Search name or new person</translation>
    </message>
    <message>
        <source>EBBEN A MEGBESZÉLÉSBEN</source>
        <translation>IN THIS MEETING</translation>
    </message>
    <message>
        <source>ÖSSZEVONÁS VAGY ÁTNEVEZÉS</source>
        <translation>MERGE OR RENAME</translation>
    </message>
    <message>
        <source>JAVASOLT</source>
        <translation>SUGGESTED</translation>
    </message>
    <message>
        <source>a bizonyítékok sorrendjében</source>
        <translation>in order of evidence</translation>
    </message>
    <message>
        <source>MÁSIK SZEMÉLY</source>
        <translation>SOMEONE ELSE</translation>
    </message>
    <message>
        <source>„%1”</source>
        <translation>&quot;%1&quot;</translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélés</source>
        <translation>
            <numerusform>%n meeting</numerusform>
            <numerusform>%n meetings</numerusform>
        </translation>
    </message>
    <message>
        <source>nincs hanglenyomat</source>
        <translation>no voiceprint</translation>
    </message>
    <message>
        <source>Új személy: „%1”</source>
        <translation>New person: &quot;%1&quot;</translation>
    </message>
    <message>
        <source>Névtelen beszélő</source>
        <translation>Unnamed speaker</translation>
    </message>
    <message>
        <source>Új névtelen résztvevő</source>
        <translation>New unnamed participant</translation>
    </message>
    <message>
        <source>MIÉRT NEM %1?</source>
        <translation>WHY NOT %1?</translation>
    </message>
    <message>
        <source>Ha másé, a sorai kerüljenek ki %1 hanglenyomatából (téves felismerés)</source>
        <translation>If it&apos;s someone else&apos;s, remove its lines from %1&apos;s voiceprint (wrong match)</translation>
    </message>
    <message>
        <source>Új személy a kijelölt sorokból…</source>
        <translation>New person from the selected lines…</translation>
    </message>
    <message>
        <source>Új személy ebből a sorból…</source>
        <translation>New person from this line…</translation>
    </message>
    <message>
        <source>ha senki sem ő</source>
        <translation>if it&apos;s nobody</translation>
    </message>
    <message>
        <source>Újra azonosítható</source>
        <translation>Identifiable again</translation>
    </message>
    <message numerus="yes">
        <source>Nem azonosítható (a kijelölt %n sor)</source>
        <translation>
            <numerusform>Unidentifiable (the selected line)</numerusform>
            <numerusform>Unidentifiable (the %n selected lines)</numerusform>
        </translation>
    </message>
    <message>
        <source>Nem azonosítható — nem tudni, ki mondta</source>
        <translation>Unidentifiable — no way to tell who said it</translation>
    </message>
    <message>
        <source>A sor újra részt vesz a hang-elemzésben</source>
        <translation>The line takes part in the voice analysis again</translation>
    </message>
    <message>
        <source>A név marad, de a sor hangját semmire nem használom (minta, bizonyíték, átnézendő)</source>
        <translation>The name stays, but the line&apos;s voice is not used for anything (sample, evidence, review)</translation>
    </message>
    <message>
        <source>Jó így + hanglenyomat-minta ebből a sorból (%1 mp)</source>
        <translation>Fine as is + voiceprint sample from this line (%1 s)</translation>
    </message>
    <message>
        <source>Hanglenyomat-minta ebből a sorból (%1 mp)</source>
        <translation>Voiceprint sample from this line (%1 s)</translation>
    </message>
    <message>
        <source>A sort megerősítem („Jó így”), és a hangja %1 hanglenyomat-mintája lesz</source>
        <translation>I confirm the line (&quot;Fine as is&quot;), and its audio becomes a voiceprint sample of %1</translation>
    </message>
    <message>
        <source>A sor hangja %1 hanglenyomat-mintája lesz</source>
        <translation>The audio of this line becomes a voiceprint sample of %1</translation>
    </message>
    <message numerus="yes">
        <source>%n sor kerül át · Ctrl+Z visszavonja · a gép tanul belőle</source>
        <translation>
            <numerusform>%n line will move · Ctrl+Z undoes it · the app learns from it</numerusform>
            <numerusform>%n lines will move · Ctrl+Z undoes it · the app learns from it</numerusform>
        </translation>
    </message>
    <message>
        <source>Ctrl+Z visszavonja</source>
        <translation>Ctrl+Z undoes it</translation>
    </message>
    <message numerus="yes">
        <source>A teljes beszélőre vonatkozik: mind az %n sor · Ctrl+Z visszavonja</source>
        <translation>
            <numerusform>Applies to the whole speaker: all %n line · Ctrl+Z undoes it</numerusform>
            <numerusform>Applies to the whole speaker: all %n lines · Ctrl+Z undoes it</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>A teljes beszélőre vonatkozik: mind a %n sor · Ctrl+Z visszavonja</source>
        <translation>
            <numerusform>Applies to the whole speaker: all %n line · Ctrl+Z undoes it</numerusform>
            <numerusform>Applies to the whole speaker: all %n lines · Ctrl+Z undoes it</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>LlmServer</name>
    <message>
        <source>A(z) „%1” modellt újra kellene tölteni (%2 tokenes kontextussal, egy szálon), de a Tanara épp egy másik kérést futtat rajta. Próbáld újra, ha az végzett.</source>
        <translation>The model &quot;%1&quot; would have to be reloaded (with a %2-token context, single-threaded), but Tanara is running another request on it. Try again when that has finished.</translation>
    </message>
    <message>
        <source>Nem sikerült kivenni a(z) „%1” modell régi példányát az LM Studióból, ezért nem tölthető be újra nagyobb kontextussal.</source>
        <translation>Could not unload the old instance of the model &quot;%1&quot; from LM Studio, so it cannot be reloaded with a larger context.</translation>
    </message>
    <message>
        <source>kivétel · %1</source>
        <translation>unload · %1</translation>
    </message>
    <message>
        <source>betöltés · %1</source>
        <translation>load · %1</translation>
    </message>
    <message>
        <source>Az LM Studio nem válaszolt a(z) „%1” modell betöltése közben.</source>
        <translation>LM Studio did not respond while loading the model &quot;%1&quot;.</translation>
    </message>
    <message>
        <source>Az LM Studio nem találja a beállított modellt (%1).</source>
        <translation>LM Studio cannot find the configured model (%1).</translation>
    </message>
    <message>
        <source>Az LM Studio nem tudta betölteni a(z) „%1” modellt %2 tokenes kontextussal. Valószínűleg ekkora kontextussal nem fér a videókártyára — állíts be kisebb kontextust a Beállításokban.</source>
        <translation>LM Studio could not load the model &quot;%1&quot; with a %2-token context. With a context this large it probably does not fit on the graphics card — set a smaller context in Settings.</translation>
    </message>
    <message>
        <source>A szerveren más modell is be van töltve (%1); azt a Tanara nem veszi ki.</source>
        <translation>Another model is also loaded on the server (%1); Tanara does not unload it.</translation>
    </message>
</context>
<context>
    <name>Main</name>
    <message>
        <source>Fájl</source>
        <translation>File</translation>
    </message>
    <message>
        <source>Új felvétel…</source>
        <translation>New recording…</translation>
    </message>
    <message>
        <source>Hangfájl importálása…</source>
        <translation>Import audio file…</translation>
    </message>
    <message>
        <source>Megbeszélés importálása archívumból…</source>
        <translation>Import meeting from archive…</translation>
    </message>
    <message>
        <source>Megbeszélés importálása mappából…</source>
        <translation>Import meeting from folder…</translation>
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
        <source>Első lépések…</source>
        <translation>Getting started…</translation>
    </message>
    <message>
        <source>Kilépés</source>
        <translation>Quit</translation>
    </message>
    <message>
        <source>Megbeszélés</source>
        <translation>Meeting</translation>
    </message>
    <message>
        <source>Nézet</source>
        <translation>View</translation>
    </message>
    <message>
        <source>Áttekintés</source>
        <translation>Overview</translation>
    </message>
    <message>
        <source>Vezetői összefoglaló</source>
        <translation>Executive summary</translation>
    </message>
    <message>
        <source>Memó</source>
        <translation>Memo</translation>
    </message>
    <message>
        <source>Olvasás</source>
        <translation>Read</translation>
    </message>
    <message>
        <source>Javítás</source>
        <translation>Correct</translation>
    </message>
    <message>
        <source>Átnézendő (%1)</source>
        <translation>To review (%1)</translation>
    </message>
    <message>
        <source>Beszélő-oszlopok</source>
        <translation>Speaker columns</translation>
    </message>
    <message>
        <source>Felvétel-ablak előtérbe</source>
        <translation>Bring recording window to front</translation>
    </message>
    <message>
        <source>Téma: a rendszer szerint</source>
        <translation>Theme: follow system</translation>
    </message>
    <message>
        <source>Világos téma</source>
        <translation>Light theme</translation>
    </message>
    <message>
        <source>Sötét téma</source>
        <translation>Dark theme</translation>
    </message>
    <message>
        <source>Felvétel folyamatban</source>
        <translation>Recording in progress</translation>
    </message>
    <message>
        <source>Előbb fejeződjön be a futó importálás</source>
        <translation>Wait for the running import to finish first</translation>
    </message>
    <message>
        <source>Engedd el az importáláshoz</source>
        <translation>Drop to import</translation>
    </message>
    <message>
        <source>A hang- vagy videófájlokból új megbeszélés lesz: fájlonként egy sáv. Egy .tanara.zip archívum a teljes megbeszélést hozza be.</source>
        <translation>Audio or video files become a new meeting, one track per file. A .tanara.zip archive brings in the whole meeting.</translation>
    </message>
    <message>
        <source>Felvétel leállítása, kilépés utána…</source>
        <translation>Stopping the recording, quitting afterwards…</translation>
    </message>
    <message>
        <source>Átirat</source>
        <translation>Transcript</translation>
    </message>
    <message>
        <source>A „Ki volt ott?” párbeszéd még nincs beépítve ebbe a változatba.</source>
        <translation>The &quot;Who was there?&quot; dialog is not available in this version yet.</translation>
    </message>
    <message>
        <source>Lejátszási hiba: %1</source>
        <translation>Playback error: %1</translation>
    </message>
    <message>
        <source>Visszavonva: %1</source>
        <translation>Undone: %1</translation>
    </message>
    <message>
        <source>Az importálás nem sikerült: %1</source>
        <translation>Import failed: %1</translation>
    </message>
</context>
<context>
    <name>MapDock</name>
    <message>
        <source>Forrás</source>
        <translation>Source</translation>
    </message>
    <message>
        <source>Szakaszok</source>
        <translation>Sections</translation>
    </message>
    <message>
        <source>2 p 41 mp</source>
        <translation>2 min 41 s</translation>
    </message>
    <message>
        <source>1 p 12 mp</source>
        <translation>1 min 12 s</translation>
    </message>
    <message>
        <source>58 mp</source>
        <translation>58 s</translation>
    </message>
    <message>
        <source>41 mp</source>
        <translation>41 s</translation>
    </message>
    <message numerus="yes">
        <source>%n keveset beszélő résztvevő — kattintásra külön sort kapnak</source>
        <translation>
            <numerusform>%n participant who spoke little — click to give them their own row</numerusform>
            <numerusform>%n participants who spoke little — click to give them their own rows</numerusform>
        </translation>
    </message>
    <message>
        <source>%1 — a beszélő menüje (átnevezés, összevonás, hanglenyomat)</source>
        <translation>%1 — speaker menu (rename, merge, voiceprint)</translation>
    </message>
    <message>
        <source>Van hanglenyomata — kattintásra a részletek</source>
        <translation>Has a voiceprint — click for details</translation>
    </message>
    <message>
        <source>Névtelen beszélő: hanglenyomat csak elnevezett beszélőhöz készíthető</source>
        <translation>Unnamed speaker: a voiceprint can only be created for a named speaker</translation>
    </message>
    <message>
        <source>Még nincs hanglenyomata — kattintásra itt készíthető</source>
        <translation>No voiceprint yet — click to create one here</translation>
    </message>
    <message numerus="yes">
        <source>%n minta</source>
        <translation>
            <numerusform>%n sample</numerusform>
            <numerusform>%n samples</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%1 · %n minta-sor</source>
        <translation>
            <numerusform>%1 · %n sample line</numerusform>
            <numerusform>%1 · %n sample lines</numerusform>
        </translation>
    </message>
    <message>
        <source>+%1 most — kattintásra ezek a sorok</source>
        <translation>+%1 now — click to show these lines</translation>
    </message>
    <message>
        <source>Keveset beszélők összecsukása</source>
        <translation>Collapse speakers who spoke little</translation>
    </message>
    <message>
        <source>Kétes</source>
        <translation>Uncertain</translation>
    </message>
    <message>
        <source>%1 előtted</source>
        <translation>%1 ahead</translation>
    </message>
    <message>
        <source>%1 / %2 kész</source>
        <translation>%1 / %2 done</translation>
    </message>
    <message>
        <source>Space lejátszás · ←/→ sor eleje / köv. megálló · Shift+←/→ sor · [ ] tempó</source>
        <translation>Space play · ←/→ line start / next stop · Shift+←/→ line · [ ] speed</translation>
    </message>
    <message>
        <source>a beszélő-térkép a keresősáv: kattintás = ugrás, név = beszélő menü</source>
        <translation>the speaker map is the seek bar: click = jump, name = speaker menu</translation>
    </message>
</context>
<context>
    <name>MeetingArchive</name>
    <message>
        <source>A megbeszélés mappája nem található: %1</source>
        <translation>The meeting folder was not found: %1</translation>
    </message>
    <message>
        <source>Az archívum nem hozható létre: %1 (%2)</source>
        <translation>The archive could not be created: %1 (%2)</translation>
    </message>
    <message>
        <source>Az archívum írása nem sikerült: %1</source>
        <translation>Writing the archive failed: %1</translation>
    </message>
    <message>
        <source>Megszakítva.</source>
        <translation>Cancelled.</translation>
    </message>
    <message>
        <source>A fájl nem olvasható: %1 (%2)</source>
        <translation>The file could not be read: %1 (%2)</translation>
    </message>
    <message>
        <source>Az archívum írása nem sikerült (%1): %2</source>
        <translation>Writing the archive failed (%1): %2</translation>
    </message>
    <message>
        <source>Az archívum lezárása nem sikerült: %1</source>
        <translation>Finalising the archive failed: %1</translation>
    </message>
    <message>
        <source>A meglévő fájl nem írható felül: %1</source>
        <translation>The existing file could not be overwritten: %1</translation>
    </message>
    <message>
        <source>Az archívum nem nevezhető át: %1</source>
        <translation>The archive could not be renamed: %1</translation>
    </message>
    <message>
        <source>Az archívum nem nyitható meg: %1 (%2)</source>
        <translation>The archive could not be opened: %1 (%2)</translation>
    </message>
    <message>
        <source>A fájl nem érvényes ZIP-archívum: %1</source>
        <translation>The file is not a valid ZIP archive: %1</translation>
    </message>
    <message>
        <source>Sérült archívum: %1</source>
        <translation>Corrupted archive: %1</translation>
    </message>
    <message>
        <source>Az archívum tiltott útvonalat tartalmaz: %1</source>
        <translation>The archive contains a forbidden path: %1</translation>
    </message>
    <message>
        <source>Az archívum titkosított vagy nem támogatott bejegyzést tartalmaz: %1</source>
        <translation>The archive contains an encrypted or unsupported entry: %1</translation>
    </message>
    <message>
        <source>Az archívum gyökerében ismeretlen fájl van: %1</source>
        <translation>There is an unknown file in the archive root: %1</translation>
    </message>
    <message>
        <source>Az archívumban több megbeszélés-mappa van (%1, %2) — egyszerre egy hozható be.</source>
        <translation>The archive contains several meeting folders (%1, %2) — only one can be imported at a time.</translation>
    </message>
    <message>
        <source>Ez nem Tanara-archívum (hiányzik a %1).</source>
        <translation>This is not a Tanara archive (%1 is missing).</translation>
    </message>
    <message>
        <source>Sérült archívum-manifeszt.</source>
        <translation>Corrupted archive manifest.</translation>
    </message>
    <message>
        <source>Sérült archívum-manifeszt: %1</source>
        <translation>Corrupted archive manifest: %1</translation>
    </message>
    <message>
        <source>Az archívum formátum-verziója (%1) nem támogatott; ez a Tanara a(z) %2. verziót ismeri.</source>
        <translation>The archive format version (%1) is not supported; this Tanara knows version %2.</translation>
    </message>
    <message>
        <source>Az archívumban nincs megbeszélés (meeting.json vagy track_*.ogg).</source>
        <translation>The archive contains no meeting (meeting.json or track_*.ogg).</translation>
    </message>
    <message>
        <source>Az ideiglenes mappa nem hozható létre: %1</source>
        <translation>The temporary folder could not be created: %1</translation>
    </message>
    <message>
        <source>A mappa nem hozható létre: %1</source>
        <translation>The folder could not be created: %1</translation>
    </message>
    <message>
        <source>A fájl nem írható: %1 (%2)</source>
        <translation>The file could not be written: %1 (%2)</translation>
    </message>
    <message>
        <source>A kicsomagolás nem sikerült (%1): %2</source>
        <translation>Extraction failed (%1): %2</translation>
    </message>
</context>
<context>
    <name>MeetingHeader</name>
    <message>
        <source>Átnevezés (F2)</source>
        <translation>Rename (F2)</translation>
    </message>
    <message>
        <source>A megbeszélés címe</source>
        <translation>Meeting title</translation>
    </message>
    <message>
        <source>Áttekintés</source>
        <translation>Overview</translation>
    </message>
    <message>
        <source>Átirat</source>
        <translation>Transcript</translation>
    </message>
    <message>
        <source>Vezetői összefoglaló</source>
        <translation>Executive summary</translation>
    </message>
    <message>
        <source>Memó</source>
        <translation>Memo</translation>
    </message>
    <message>
        <source>elavult</source>
        <translation>outdated</translation>
    </message>
    <message>
        <source>Megbeszélés: átnevezés, azonosítás, újra-átírás, export, törlés</source>
        <translation>Meeting: rename, identify, re-transcribe, export, delete</translation>
    </message>
    <message>
        <source>Még nincs átirat</source>
        <translation>No transcript yet</translation>
    </message>
    <message>
        <source>Még nincs memó</source>
        <translation>No memo yet</translation>
    </message>
    <message>
        <source>Még nincs vezetői összefoglaló</source>
        <translation>No executive summary yet</translation>
    </message>
    <message>
        <source>Az átírás fut (%1%). Ha elkészül, itt olvashatod és javíthatod.</source>
        <translation>Transcription is running (%1%). When it finishes, you can read and correct it here.</translation>
    </message>
    <message>
        <source>A felvételt még nem írtuk át. Az átírás után itt olvashatod és javíthatod a beszélőket.</source>
        <translation>The recording has not been transcribed yet. After transcription you can read it here and correct the speakers.</translation>
    </message>
    <message>
        <source>Az összefoglaló az átiratból készül: előbb az átírásnak kell elkészülnie.</source>
        <translation>The summary is built from the transcript, so transcription has to finish first.</translation>
    </message>
    <message>
        <source>Készül az összefoglaló (%1%).</source>
        <translation>Building the summary (%1%).</translation>
    </message>
    <message>
        <source>Az átirat kész. A gyors összefoglaló kb. 1 perc, a Memó ugyanabból készül.</source>
        <translation>The transcript is ready. The quick summary takes about 1 minute; the Memo is built from the same material.</translation>
    </message>
    <message>
        <source>Átírás indítása</source>
        <translation>Start transcription</translation>
    </message>
    <message>
        <source>Összefoglaló készítése</source>
        <translation>Create summary</translation>
    </message>
    <message>
        <source>Más lehetőségek…</source>
        <translation>More options…</translation>
    </message>
</context>
<context>
    <name>MeetingMenu</name>
    <message>
        <source>Átnevezés</source>
        <translation>Rename</translation>
    </message>
    <message>
        <source>Ki volt ott?</source>
        <translation>Who was there?</translation>
    </message>
    <message>
        <source>Hanglenyomat-elemzés újra</source>
        <translation>Re-run voiceprint analysis</translation>
    </message>
    <message>
        <source>Újraellenőrzés…</source>
        <translation>Re-check…</translation>
    </message>
    <message>
        <source>Újra-átírás…</source>
        <translation>Re-transcribe…</translation>
    </message>
    <message>
        <source>Exportálás archívumba…</source>
        <translation>Export to archive…</translation>
    </message>
    <message>
        <source>Megnyitás mappában</source>
        <translation>Show in folder</translation>
    </message>
    <message>
        <source>Törlés…</source>
        <translation>Delete…</translation>
    </message>
</context>
<context>
    <name>MeetingNoteEditor</name>
    <message>
        <source>Például: Résztvevők: Kovács Anna, Szabó Bence. A „Nordwig” helyesen: Nordvik. A „kvarc modul” helyesen: Qvarko-modul.</source>
        <translation>For example: Participants: Anna Kovács, Bence Szabó. “Nordwig” is really Nordvik. “quark module” is really Qvarko module.</translation>
    </message>
    <message>
        <source>Megjegyzés a megbeszéléshez</source>
        <translation>Note on the meeting</translation>
    </message>
    <message>
        <source>Korábbi, hasonló megbeszélések megjegyzései — kattintásra a mezőbe kerül:</source>
        <translation>Notes from earlier, similar meetings — click to copy into the field:</translation>
    </message>
    <message>
        <source>Javaslat: %1, %2</source>
        <translation>Suggestion: %1, %2</translation>
    </message>
    <message>
        <source>Észlelt hívás: %1</source>
        <translation>Detected call: %1</translation>
    </message>
    <message>
        <source>Már van megjegyzés</source>
        <translation>There is already a note</translation>
    </message>
    <message>
        <source>Lecseréled a mostanit a korábbi megbeszélés megjegyzésére, vagy hozzáfűzöd a végéhez?</source>
        <translation>Replace the current note with the earlier meeting’s note, or append it to the end?</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Hozzáfűzés</source>
        <translation>Append</translation>
    </message>
    <message>
        <source>Csere</source>
        <translation>Replace</translation>
    </message>
</context>
<context>
    <name>MeetingStore</name>
    <message>
        <source>Nincs ilyen mappa: %1</source>
        <translation>No such folder: %1</translation>
    </message>
    <message>
        <source>A mappában nincs meeting.json és nincs track_*.ogg sáv: %1</source>
        <translation>The folder has neither a meeting.json nor any track_*.ogg track: %1</translation>
    </message>
    <message>
        <source>A meeting.json nem olvasható: %1</source>
        <translation>meeting.json could not be read: %1</translation>
    </message>
    <message>
        <source>Már van ilyen nevű felvétel: %1 — a meglévőt nem írom felül.</source>
        <translation>A recording with this name already exists: %1 — the existing one is not overwritten.</translation>
    </message>
    <message>
        <source>Ez a megbeszélés már szerepel a könyvtárban: „%1” (%2).</source>
        <translation>This meeting is already in the library: &quot;%1&quot; (%2).</translation>
    </message>
    <message>
        <source>A felvételek mappája nem hozható létre: %1</source>
        <translation>The recordings folder could not be created: %1</translation>
    </message>
    <message>
        <source>A mappa másolása nem sikerült: %1 → %2</source>
        <translation>Copying the folder failed: %1 → %2</translation>
    </message>
</context>
<context>
    <name>MemoToc</name>
    <message numerus="yes">
        <source>Tartalom · %n szakasz</source>
        <translation>
            <numerusform>Contents · %n section</numerusform>
            <numerusform>Contents · %n sections</numerusform>
        </translation>
    </message>
    <message>
        <source>Egyéb</source>
        <translation>Other</translation>
    </message>
</context>
<context>
    <name>NoSelectionView</name>
    <message>
        <source>Válassz egy megbeszélést</source>
        <translation>Select a meeting</translation>
    </message>
    <message>
        <source>Ezek várnak rád:</source>
        <translation>Waiting for you:</translation>
    </message>
    <message>
        <source>A bal oldali listából nyithatsz meg egyet. Most semmi nem vár rád.</source>
        <translation>Open one from the list on the left. Nothing is waiting for you right now.</translation>
    </message>
</context>
<context>
    <name>OnboardingWindow</name>
    <message>
        <source>Első lépések</source>
        <translation>Getting started</translation>
    </message>
    <message>
        <source>%1 / %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <source>Minden beállítás később is módosítható a Beállításokban.</source>
        <translation>Every setting can be changed later in Settings.</translation>
    </message>
    <message>
        <source>Üdv a Tanarában!</source>
        <translation>Welcome to Tanara!</translation>
    </message>
    <message>
        <source>A Tanara rögzíti a megbeszéléseidet — minden hangforrást külön sávra —, majd átiratot és összefoglalót készít belőlük. A felvételek a gépeden maradnak, a szolgáltatókat a saját kulcsoddal éred el.</source>
        <translation>Tanara records your meetings — every audio source on its own track — then produces a transcript and a summary. Recordings stay on your machine; you reach the providers with your own keys.</translation>
    </message>
    <message>
        <source>Felvétel</source>
        <translation>Recording</translation>
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
        <source>Néhány lépésben beállítjuk a legfontosabbakat. Bármelyiket kihagyhatod, és az ablakot is bármikor bezárhatod.</source>
        <translation>A few steps set up the essentials. You can skip any of them, and close this window at any time.</translation>
    </message>
    <message>
        <source>Minden beállítás később is módosítható a Beállításokban. Ezt az ablakot a Fájl › Első lépések… menüből bármikor újranyithatod.</source>
        <translation>Every setting can be changed later in Settings. You can reopen this window any time from File › Getting started….</translation>
    </message>
    <message>
        <source>Te</source>
        <translation>You</translation>
    </message>
    <message>
        <source>A saját mikrofonod sávja ezen a néven szerepel. A nevet az operációs rendszer fiókjából vettük — írd át, ha másként szólítanak.</source>
        <translation>Your own microphone track appears under this name. It was taken from your operating system account — change it if people call you something else.</translation>
    </message>
    <message>
        <source>Saját neved</source>
        <translation>Your name</translation>
    </message>
    <message>
        <source>Így jelensz meg az átiratokban; a saját hanglenyomatod ehhez a névhez tartozik.</source>
        <translation>This is how you appear in transcripts; your own voiceprint belongs to this name.</translation>
    </message>
    <message>
        <source>Nyelv</source>
        <translation>Language</translation>
    </message>
    <message>
        <source>A nyelv a Tanara következő indításakor vált.</source>
        <translation>The language changes the next time Tanara starts.</translation>
    </message>
    <message>
        <source>Téma</source>
        <translation>Theme</translation>
    </message>
    <message>
        <source>Rendszer</source>
        <translation>System</translation>
    </message>
    <message>
        <source>Világos</source>
        <translation>Light</translation>
    </message>
    <message>
        <source>Sötét</source>
        <translation>Dark</translation>
    </message>
    <message>
        <source>Mappák</source>
        <translation>Folders</translation>
    </message>
    <message>
        <source>Ide kerülnek a felvételek és az összefoglalók Markdown-másolata. Az alapértelmezés a legtöbbször jó; ha a jegyzeteidet máshol tartod, válaszd azt a mappát.</source>
        <translation>Recordings and the Markdown copies of the summaries go here. The default is usually fine; if you keep your notes elsewhere, pick that folder.</translation>
    </message>
    <message>
        <source>alapértelmezett</source>
        <translation>default</translation>
    </message>
    <message>
        <source>Tallózás…</source>
        <translation>Browse…</translation>
    </message>
    <message>
        <source>Vissza az alapértelmezettre</source>
        <translation>Back to default</translation>
    </message>
    <message>
        <source>A belső adatok mappáját (hanglenyomatok, index) a Beállítások › Általános lapon találod.</source>
        <translation>The internal data folder (voiceprints, index) is on the Settings › General page.</translation>
    </message>
    <message>
        <source>Szolgáltatások</source>
        <translation>Services</translation>
    </message>
    <message>
        <source>A Tanara a saját kulcsoddal dolgozik: az átírást a Soniox végzi (API-kulcs kell hozzá), az összefoglalót egy OpenAI-kompatibilis végpont — például a gépeden futó LM Studio vagy egy felhős szolgáltató.</source>
        <translation>Tanara works with your own keys: Soniox does the transcription (an API key is needed), and an OpenAI-compatible endpoint writes the summary — for example LM Studio on your machine or a cloud service.</translation>
    </message>
    <message>
        <source>Beállítás most</source>
        <translation>Set up now</translation>
    </message>
    <message>
        <source>Beállítások › Szolgáltatások</source>
        <translation>Settings › Services</translation>
    </message>
    <message>
        <source>A kulcs a gépeden marad. A Beállításokban a kapcsolatot is kipróbálhatod; átírás nélkül is rögzíthetsz, a kulcs csak a feldolgozáshoz kell.</source>
        <translation>The key stays on your machine. You can test the connection in Settings; you can record without transcription, the key is only needed for processing.</translation>
    </message>
    <message>
        <source>Hívásfigyelő</source>
        <translation>Call watcher</translation>
    </message>
    <message>
        <source>A hívásfigyelő a tálcán fut, és szól, ha Teams, Meet vagy Zoom hívást észlel. A felvételt mindig te indítod.</source>
        <translation>The call watcher runs in the tray and tells you when it notices a Teams, Meet or Zoom call. You always start the recording yourself.</translation>
    </message>
    <message>
        <source>Induljon el a figyelő bejelentkezéskor</source>
        <translation>Start the watcher at login</translation>
    </message>
    <message>
        <source>A figyelt alkalmazásokat és a megbeszélés végi rákérdezést a Beállítások › Hívásfigyelő lapon állíthatod.</source>
        <translation>The watched applications and the end-of-meeting prompt are on the Settings › Call watcher page.</translation>
    </message>
    <message>
        <source>Kész is vagyunk</source>
        <translation>All done</translation>
    </message>
    <message>
        <source>Vegyük fel az első megbeszélést: indíts felvételt, vagy importálj egy meglévő hang- vagy videófájlt.</source>
        <translation>Let’s record the first meeting: start a recording, or import an existing audio or video file.</translation>
    </message>
    <message>
        <source>Név és megjelenés</source>
        <translation>Name and appearance</translation>
    </message>
    <message>
        <source>%1 — kihagyva</source>
        <translation>%1 — skipped</translation>
    </message>
    <message>
        <source>%1 — nem nézted meg</source>
        <translation>%1 — not viewed</translation>
    </message>
    <message>
        <source>Minden beállítás később is módosítható a Beállításokban (Fájl › Beállítások…, Ctrl+,).</source>
        <translation>Every setting can be changed later in Settings (File › Settings…, Ctrl+,).</translation>
    </message>
    <message>
        <source>Később</source>
        <translation>Later</translation>
    </message>
    <message>
        <source>Bezárás — a Fájl › Első lépések… menüből újranyitható</source>
        <translation>Close — can be reopened from File › Getting started…</translation>
    </message>
    <message>
        <source>Vissza</source>
        <translation>Back</translation>
    </message>
    <message>
        <source>Kihagyom</source>
        <translation>Skip</translation>
    </message>
    <message>
        <source>Kezdjük</source>
        <translation>Let’s go</translation>
    </message>
    <message>
        <source>Tovább</source>
        <translation>Next</translation>
    </message>
</context>
<context>
    <name>OverviewTab</name>
    <message>
        <source>Ehhez add meg a saját neved: Beállítások › Beszélők › Te.</source>
        <translation>To do this, enter your own name: Settings › Speakers › You.</translation>
    </message>
    <message>
        <source>Visszavonva: %1</source>
        <translation>Undone: %1</translation>
    </message>
    <message>
        <source>Visszavonás · Ctrl+Z</source>
        <translation>Undo · Ctrl+Z</translation>
    </message>
    <message>
        <source>MIRŐL SZÓLT</source>
        <translation>WHAT IT WAS ABOUT</translation>
    </message>
    <message>
        <source>MIRŐL SZÓL</source>
        <translation>WHAT IT IS ABOUT</translation>
    </message>
    <message>
        <source>Forrás</source>
        <translation>Source</translation>
    </message>
    <message>
        <source>Leírás</source>
        <translation>Description</translation>
    </message>
    <message>
        <source>Naptár-esemény (hamarosan)</source>
        <translation>Calendar event (coming soon)</translation>
    </message>
    <message>
        <source>KIK VOLTAK OTT</source>
        <translation>WHO WAS THERE</translation>
    </message>
    <message>
        <source>Módosítás</source>
        <translation>Edit</translation>
    </message>
    <message>
        <source>+ Résztvevő</source>
        <translation>+ Participant</translation>
    </message>
    <message>
        <source>Még nem tudjuk, kik voltak ott</source>
        <translation>We don&apos;t know yet who was there</translation>
    </message>
    <message>
        <source>Add meg, kik voltak ott: a beszélők így a nevükre kerülnek, és legközelebb a hangjukról is felismerjük őket.</source>
        <translation>Enter who was there: the speakers will get their names, and next time we&apos;ll recognize them by voice too.</translation>
    </message>
    <message>
        <source>A hanglenyomat-elemzés után ide jönnek a jelöltek. Ha tudod, add meg most: az azonosítás így pontosabb lesz.</source>
        <translation>Candidates will appear here after the voiceprint analysis. If you know, enter them now: identification will be more accurate.</translation>
    </message>
    <message>
        <source>Csak én beszéltem</source>
        <translation>Only I spoke</translation>
    </message>
    <message>
        <source>Minden beszéd a te nevedre kerül, az azonosítás kimarad</source>
        <translation>All speech goes under your name; identification is skipped</translation>
    </message>
    <message numerus="yes">
        <source>%n beszélő, ismeretlen hanggal</source>
        <translation>
            <numerusform>%n speaker with an unknown voice</numerusform>
            <numerusform>%n speakers with unknown voices</numerusform>
        </translation>
    </message>
    <message>
        <source>Egyikük hangja sem hasonlít egy ismert emberére sem. Elnevezheted őket; legközelebb már felismerjük a hangjukat.</source>
        <translation>None of their voices resemble anyone known. You can name them; next time we&apos;ll recognize their voices.</translation>
    </message>
    <message>
        <source>Elnevezem őket</source>
        <translation>Name them</translation>
    </message>
    <message numerus="yes">
        <source>A hanglenyomat-elemzés kész: %n jelölt</source>
        <translation>
            <numerusform>Voiceprint analysis finished: %n candidate</numerusform>
            <numerusform>Voiceprint analysis finished: %n candidates</numerusform>
        </translation>
    </message>
    <message>
        <source>Nézd át, kik voltak ott. Ebből kötöm az átirat beszélőit a nevekhez.</source>
        <translation>Review who was there. I use this to match the transcript&apos;s speakers to names.</translation>
    </message>
    <message>
        <source>Nézd át, kik voltak ott. Ebből köti az átirat a beszélőket, amikor megérkezik.</source>
        <translation>Review who was there. The transcript uses this to match speakers to names when it arrives.</translation>
    </message>
    <message>
        <source>Átnézem</source>
        <translation>Review</translation>
    </message>
    <message>
        <source>Később</source>
        <translation>Later</translation>
    </message>
    <message>
        <source>ADATOK</source>
        <translation>DETAILS</translation>
    </message>
    <message>
        <source>FELDOLGOZÁS ÉS SÁVOK</source>
        <translation>PROCESSING AND TRACKS</translation>
    </message>
    <message>
        <source>Minden lépés kész</source>
        <translation>All steps done</translation>
    </message>
    <message>
        <source>Sávok</source>
        <translation>Tracks</translation>
    </message>
    <message numerus="yes">
        <source>%n aktív</source>
        <translation>
            <numerusform>%n active</numerusform>
            <numerusform>%n active</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n kimaradt</source>
        <translation>
            <numerusform>%n skipped</numerusform>
            <numerusform>%n skipped</numerusform>
        </translation>
    </message>
    <message>
        <source>FELDOLGOZÁS</source>
        <translation>PROCESSING</translation>
    </message>
    <message>
        <source>SÁVOK</source>
        <translation>TRACKS</translation>
    </message>
    <message>
        <source>Részletek</source>
        <translation>Details</translation>
    </message>
    <message>
        <source>Lekeverés frissítése</source>
        <translation>Update mixdown</translation>
    </message>
    <message>
        <source>Lekeverés indítása</source>
        <translation>Start mixdown</translation>
    </message>
    <message>
        <source>Résztvevők azonosítása hang alapján</source>
        <translation>Identify participants by voice</translation>
    </message>
    <message>
        <source>Azonosítás hang alapján</source>
        <translation>Identify by voice</translation>
    </message>
    <message>
        <source>Kihagyva: a beszélők névtelenek maradnak</source>
        <translation>Skipped: the speakers stay anonymous</translation>
    </message>
    <message>
        <source>Újraelemzés</source>
        <translation>Re-analyze</translation>
    </message>
    <message>
        <source>Elemzés indítása</source>
        <translation>Start analysis</translation>
    </message>
    <message>
        <source>A hanglenyomat-elemzés újra: a névtelen beszélők a mentett hangmintákhoz</source>
        <translation>Re-run the voiceprint analysis: anonymous speakers are matched against the saved voice samples</translation>
    </message>
    <message>
        <source>Hanglenyomat-elemzés a nyers sávokon, átirat nélkül</source>
        <translation>Voice-print analysis on the raw tracks, without a transcript</translation>
    </message>
    <message>
        <source>A hanglenyomat-elemzés újra a sávokon</source>
        <translation>Re-run the voiceprint analysis on the tracks</translation>
    </message>
    <message>
        <source>A beszélő minden sora a választott névre kerül; Ctrl+Z visszavonja.</source>
        <translation>All lines of the speaker go to the chosen name; Ctrl+Z undoes it.</translation>
    </message>
    <message>
        <source>Sávok részletei</source>
        <translation>Track details</translation>
    </message>
    <message>
        <source>Bezárás</source>
        <translation>Close</translation>
    </message>
</context>
<context>
    <name>ParticipantAnalysis</name>
    <message>
        <source>mikrofon</source>
        <translation>microphone</translation>
    </message>
    <message>
        <source>hívás</source>
        <translation>call</translation>
    </message>
    <message>
        <source>kézzel felvéve</source>
        <translation>added by hand</translation>
    </message>
    <message>
        <source>hang %1%</source>
        <translation>voice %1%</translation>
    </message>
    <message>
        <source>ismeretlen hang</source>
        <translation>unknown voice</translation>
    </message>
    <message>
        <source>kézzel: %1</source>
        <translation>by hand: %1</translation>
    </message>
    <message>
        <source> és </source>
        <translation> and </translation>
    </message>
</context>
<context>
    <name>ParticipantList</name>
    <message>
        <source>Kattints: Miért ő? (javítás a szerkesztőben)</source>
        <translation>Click: Why them? (correct it in the editor)</translation>
    </message>
    <message>
        <source>Elnevezés…</source>
        <translation>Name…</translation>
    </message>
    <message>
        <source>Kié ez a hang? A sorai a választott névre kerülnek</source>
        <translation>Whose voice is this? Its lines go to the chosen name</translation>
    </message>
    <message>
        <source>Hozzáadás</source>
        <translation>Add</translation>
    </message>
    <message>
        <source>Nem volt ott: a javaslat ennél a megbeszélésnél nem jön elő (Ctrl+Z visszahozza)</source>
        <translation>Was not there: the suggestion will not come up for this meeting (Ctrl+Z brings it back)</translation>
    </message>
    <message>
        <source>Törlés a résztvevők közül (Ctrl+Z visszavonja)</source>
        <translation>Remove from the participants (Ctrl+Z undoes it)</translation>
    </message>
    <message>
        <source>Nem volt ott: törlés a résztvevők közül, a sorai névtelenek lesznek (Ctrl+Z visszavonja)</source>
        <translation>Was not there: removed from the participants, their lines become unnamed (Ctrl+Z undoes it)</translation>
    </message>
</context>
<context>
    <name>ParticipantRow</name>
    <message>
        <source>Elnevezés…</source>
        <translation>Name…</translation>
    </message>
    <message>
        <source>Elvetés</source>
        <translation>Discard</translation>
    </message>
</context>
<context>
    <name>ParticipantTrackChips</name>
    <message>
        <source>Kézzel ehhez a sávhoz kötve · kattintva leveszed</source>
        <translation>Manually tied to this track · click to remove</translation>
    </message>
    <message>
        <source>Kattintva ezt a sávot is hozzáköti</source>
        <translation>Click to tie this track as well</translation>
    </message>
    <message>
        <source>A hang alapján ezen a sávon beszél · kattintva rögzíted</source>
        <translation>Speaks on this track by voice · click to pin it</translation>
    </message>
    <message>
        <source>Kattintva kézzel ehhez a sávhoz köti</source>
        <translation>Click to tie to this track manually</translation>
    </message>
</context>
<context>
    <name>ParticipantsDialog</name>
    <message numerus="yes">
        <source>Résztvevők jóváhagyva: %n.</source>
        <translation>
            <numerusform>Participants approved: %n.</numerusform>
            <numerusform>Participants approved: %n.</numerusform>
        </translation>
    </message>
    <message>
        <source>A bejelölt hang ennek a személynek a nevére kerül.</source>
        <translation>The selected voice will be assigned to this person.</translation>
    </message>
    <message>
        <source>Ki volt ott?</source>
        <translation>Who was there?</translation>
    </message>
    <message>
        <source>hanglenyomat-elemzés · fut</source>
        <translation>voiceprint analysis · running</translation>
    </message>
    <message>
        <source>Vedd ki, akit tévesen talált, és add hozzá, akit nem ismert fel. A bejelöltek nevére kerülnek a beszélőik; később is módosíthatod.</source>
        <translation>Remove anyone it found by mistake and add anyone it missed. The speakers will be assigned to the checked names; you can change this later.</translation>
    </message>
    <message>
        <source>Vedd ki, akit tévesen talált, és add hozzá, akit nem ismert fel. Az átirat ebből köti a beszélőket, amikor megérkezik; később is módosíthatod.</source>
        <translation>Remove anyone it found by mistake and add anyone it missed. The transcript uses this to match speakers when it arrives; you can change this later.</translation>
    </message>
    <message>
        <source>Hanglenyomat-elemzés fut…</source>
        <translation>Voiceprint analysis running…</translation>
    </message>
    <message>
        <source>A jelöltek pár másodperc múlva itt lesznek. Addig is felvehetsz résztvevőt kézzel.</source>
        <translation>Candidates will be here in a few seconds. Meanwhile you can add a participant manually.</translation>
    </message>
    <message>
        <source>Még nincs jelölt: a hangelemzés senkit sem talált. Vedd fel kézzel, akik ott voltak.</source>
        <translation>No candidates yet: the voice analysis found no one. Add the people who were there manually.</translation>
    </message>
    <message>
        <source>Résztvevő hozzáadása…</source>
        <translation>Add participant…</translation>
    </message>
    <message>
        <source>A felvett résztvevő a „Tovább”-bal kerül a megbeszélésre.</source>
        <translation>The participant you add is attached to the meeting with &quot;Continue&quot;.</translation>
    </message>
    <message>
        <source>Javasolt résztvevő: %1</source>
        <translation>Suggested participant: %1</translation>
    </message>
    <message>
        <source>Később</source>
        <translation>Later</translation>
    </message>
    <message>
        <source>Kihagyás</source>
        <translation>Skip</translation>
    </message>
    <message>
        <source>Nem kötünk senkit; a jelöltek megmaradnak, később is jóváhagyhatod.</source>
        <translation>Nobody will be matched; the candidates stay and you can approve them later.</translation>
    </message>
    <message numerus="yes">
        <source>Tovább · %n résztvevő</source>
        <translation>
            <numerusform>Continue · %n participant</numerusform>
            <numerusform>Continue · %n participants</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>PeopleDetailPane</name>
    <message>
        <source>Még csak te vagy itt</source>
        <translation>It&apos;s just you here so far</translation>
    </message>
    <message>
        <source>Nevezd el a beszélőket egy megbeszélés átiratában, vagy vegyél fel valakit előre, hogy a személyválasztóban már szerepeljen.</source>
        <translation>Name the speakers in a meeting&apos;s transcript, or add someone in advance so they already appear in the person picker.</translation>
    </message>
    <message>
        <source>Új személy</source>
        <translation>New person</translation>
    </message>
    <message>
        <source>A minták újraszámolása az új modellel…</source>
        <translation>Recalculating the samples with the new model…</translation>
    </message>
    <message>
        <source>A személy új neve</source>
        <translation>The person&apos;s new name</translation>
    </message>
    <message>
        <source>Mentés</source>
        <translation>Save</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Enter: mentés · Esc: mégse. A régi név becenévként megmarad.</source>
        <translation>Enter: save · Esc: cancel. The old name is kept as an alias.</translation>
    </message>
    <message>
        <source>Átnevezés</source>
        <translation>Rename</translation>
    </message>
    <message>
        <source>Összevonás…</source>
        <translation>Merge…</translation>
    </message>
    <message>
        <source>Személy törlése</source>
        <translation>Delete person</translation>
    </message>
    <message>
        <source>Jellemzően:</source>
        <translation>Usually:</translation>
    </message>
    <message>
        <source>helyi</source>
        <translation>local</translation>
    </message>
    <message>
        <source>Jellemzően a mikrofonon szól (melletted ül)</source>
        <translation>Usually heard on the microphone (sits next to you)</translation>
    </message>
    <message>
        <source>távoli</source>
        <translation>remote</translation>
    </message>
    <message>
        <source>Jellemzően a hívás hangján szól</source>
        <translation>Usually heard on the call audio</translation>
    </message>
    <message>
        <source>nincs adat</source>
        <translation>no data</translation>
    </message>
    <message>
        <source>A sáv-beosztás ebből indul ki, ha a hang nem dönt egyértelműen. A kézzel beállított értéket a program nem írja felül.</source>
        <translation>The track assignment starts from this when the voice is not clear-cut. A value set manually is never overwritten by the program.</translation>
    </message>
    <message>
        <source>Becenevek</source>
        <translation>Aliases</translation>
    </message>
    <message>
        <source>Becenév törlése: %1</source>
        <translation>Remove alias: %1</translation>
    </message>
    <message>
        <source>Új becenév</source>
        <translation>New alias</translation>
    </message>
    <message>
        <source>Becenév</source>
        <translation>Alias</translation>
    </message>
    <message>
        <source>A keresés és a személyválasztó ezekre is talál.</source>
        <translation>Search and the person picker find these too.</translation>
    </message>
    <message>
        <source>Megjegyzés</source>
        <translation>Note</translation>
    </message>
    <message>
        <source>Megjegyzés hozzáadása…</source>
        <translation>Add a note…</translation>
    </message>
    <message>
        <source>Megjegyzés a személyhez</source>
        <translation>Note about the person</translation>
    </message>
    <message>
        <source>Hanglenyomat</source>
        <translation>Voiceprint</translation>
    </message>
    <message>
        <source>Csak a legutóbbiak</source>
        <translation>Only the most recent</translation>
    </message>
    <message>
        <source>Az összes minta (%1)</source>
        <translation>All samples (%1)</translation>
    </message>
    <message>
        <source>Még nincs hanglenyomata</source>
        <translation>No voiceprint yet</translation>
    </message>
    <message>
        <source>Megbeszélések átnézése…</source>
        <translation>Checking the meetings…</translation>
    </message>
    <message>
        <source>Minta készítése: %1</source>
        <translation>Creating samples: %1</translation>
    </message>
    <message>
        <source>Megbeszélések</source>
        <translation>Meetings</translation>
    </message>
    <message>
        <source>%1 · legújabb elöl</source>
        <translation>%1 · newest first</translation>
    </message>
    <message>
        <source>Megbeszélések számolása…</source>
        <translation>Counting meetings…</translation>
    </message>
    <message>
        <source>Még egy megbeszélésen sem szerepel.</source>
        <translation>Does not appear in any meeting yet.</translation>
    </message>
    <message>
        <source>Az összes megbeszélés (%1)</source>
        <translation>All meetings (%1)</translation>
    </message>
</context>
<context>
    <name>PeopleListPane</name>
    <message>
        <source>Név, becenév vagy címke</source>
        <translation>Name, nickname or tag</translation>
    </message>
    <message>
        <source>Keresés a személyek között</source>
        <translation>Search people</translation>
    </message>
    <message>
        <source>Új személy</source>
        <translation>New person</translation>
    </message>
    <message>
        <source>Rendezés: %1</source>
        <translation>Sort: %1</translation>
    </message>
    <message>
        <source>ABC</source>
        <translation>A–Z</translation>
    </message>
    <message>
        <source>Legutóbb</source>
        <translation>Most recent</translation>
    </message>
    <message>
        <source>Legtöbb megbeszélés</source>
        <translation>Most meetings</translation>
    </message>
    <message>
        <source>Itt jelennek meg a résztvevők</source>
        <translation>Participants will appear here</translation>
    </message>
    <message>
        <source>Amikor az átiratban elnevezel egy beszélőt, személy lesz belőle, és a következő megbeszéléseken hangja alapján felismerjük.</source>
        <translation>When you name a speaker in a transcript, they become a person, and we recognise them by voice in later meetings.</translation>
    </message>
    <message>
        <source>Becenevekben is kerestem.</source>
        <translation>I searched aliases too.</translation>
    </message>
    <message>
        <source>Új személy: „%1”</source>
        <translation>New person: &quot;%1&quot;</translation>
    </message>
</context>
<context>
    <name>PeopleListRow</name>
    <message>
        <source>te</source>
        <translation>you</translation>
    </message>
</context>
<context>
    <name>PeopleMergeDialog</name>
    <message>
        <source>%1 összevonása</source>
        <translation>Merge %1</translation>
    </message>
    <message>
        <source>Ha ugyanaz az ember kétszer szerepel. A megbeszélések, minták és becenevek egy személyhez kerülnek.</source>
        <translation>For when the same human appears twice. The meetings, samples and aliases end up with one person.</translation>
    </message>
    <message>
        <source>Kivel vonod össze?</source>
        <translation>Merge with whom?</translation>
    </message>
    <message>
        <source>Nincs ilyen személy (becenevekben is kerestem).</source>
        <translation>No such person (I searched aliases too).</translation>
    </message>
    <message>
        <source>Nincs másik személy, akivel összevonható.</source>
        <translation>There is no other person to merge with.</translation>
    </message>
    <message>
        <source>Megmaradó név</source>
        <translation>Name to keep</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Összevonás</source>
        <translation>Merge</translation>
    </message>
</context>
<context>
    <name>PeopleSampleRow</name>
    <message>
        <source>Szünet</source>
        <translation>Pause</translation>
    </message>
    <message>
        <source>Minta meghallgatása</source>
        <translation>Listen to the sample</translation>
    </message>
    <message>
        <source>A minta műveletei</source>
        <translation>Sample actions</translation>
    </message>
    <message>
        <source>Meghallgatás</source>
        <translation>Listen</translation>
    </message>
    <message>
        <source>Új személy ebből a mintából…</source>
        <translation>New person from this sample…</translation>
    </message>
    <message>
        <source>Áthelyezés másik személyhez…</source>
        <translation>Move to another person…</translation>
    </message>
    <message>
        <source>Minta törlése</source>
        <translation>Delete sample</translation>
    </message>
</context>
<context>
    <name>PeopleSampleTargetDialog</name>
    <message>
        <source>Minta áthelyezése</source>
        <translation>Move sample</translation>
    </message>
    <message>
        <source>Új személy ebből a mintából</source>
        <translation>New person from this sample</translation>
    </message>
    <message>
        <source>A minta kikerül a mostani hanglenyomatból, és a választott személyhez kerül.</source>
        <translation>The sample leaves the current voiceprint and goes to the person you choose.</translation>
    </message>
    <message>
        <source>Ha a minta valaki más hangja: add meg a nevét. A minta kikerül a mostani hanglenyomatból. Létező névnél a minta ahhoz a személyhez kerül.</source>
        <translation>If the sample is someone else&apos;s voice, enter their name. The sample leaves the current voiceprint. If the name already exists, the sample goes to that person.</translation>
    </message>
    <message>
        <source>Név vagy becenév</source>
        <translation>Name or alias</translation>
    </message>
    <message>
        <source>Az új személy neve</source>
        <translation>The new person&apos;s name</translation>
    </message>
    <message>
        <source>Nincs ilyen személy. Enter vagy „Új személy”: ezzel a névvel létrejön, és megkapja a mintát.</source>
        <translation>No such person. Enter or &quot;New person&quot;: they are created with this name and get the sample.</translation>
    </message>
    <message>
        <source>A minta most is ennél a személynél van.</source>
        <translation>The sample already belongs to this person.</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Áthelyezés</source>
        <translation>Move</translation>
    </message>
    <message>
        <source>Új személy</source>
        <translation>New person</translation>
    </message>
</context>
<context>
    <name>PeopleUndoToast</name>
    <message>
        <source>Visszavonás</source>
        <translation>Undo</translation>
    </message>
</context>
<context>
    <name>PeopleWindow</name>
    <message>
        <source>Személyek</source>
        <translation>People</translation>
    </message>
    <message>
        <source>Minta törölve: %1</source>
        <translation>Sample deleted: %1</translation>
    </message>
    <message>
        <source>A minta visszakerült.</source>
        <translation>The sample is back.</translation>
    </message>
    <message>
        <source>Adj meg egy nevet.</source>
        <translation>Enter a name.</translation>
    </message>
    <message>
        <source>%1 törlése</source>
        <translation>Delete %1</translation>
    </message>
    <message>
        <source>Hangminták megtartása névtelen személyként</source>
        <translation>Keep the voice samples as an anonymous person</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Törlés</source>
        <translation>Delete</translation>
    </message>
    <message>
        <source>Új személy</source>
        <translation>New person</translation>
    </message>
    <message>
        <source>Előre felvett személy: a személyválasztóban már szerepel, hanglenyomata az első hozzárendelés után lehet.</source>
        <translation>A person added in advance: they already appear in the person picker; they can get a voiceprint after their first assignment.</translation>
    </message>
    <message>
        <source>Név</source>
        <translation>Name</translation>
    </message>
    <message>
        <source>Hozzáadás</source>
        <translation>Add</translation>
    </message>
</context>
<context>
    <name>PersonPicker</name>
    <message>
        <source>Névtelen résztvevő</source>
        <translation>Unnamed participant</translation>
    </message>
    <message>
        <source>Név keresése vagy új személy</source>
        <translation>Search name or new person</translation>
    </message>
    <message>
        <source>Név keresése</source>
        <translation>Search name</translation>
    </message>
    <message>
        <source>„%1”</source>
        <translation>&quot;%1&quot;</translation>
    </message>
    <message>
        <source>már résztvevő</source>
        <translation>already a participant</translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélés</source>
        <translation>
            <numerusform>%n meeting</numerusform>
            <numerusform>%n meetings</numerusform>
        </translation>
    </message>
    <message>
        <source>nincs hanglenyomat</source>
        <translation>no voiceprint</translation>
    </message>
    <message>
        <source>Új személy: „%1”</source>
        <translation>New person: &quot;%1&quot;</translation>
    </message>
    <message>
        <source>A beszélő névtelenül is maradhat; később is elnevezheted.</source>
        <translation>Speakers can stay unnamed; you can name them later.</translation>
    </message>
</context>
<context>
    <name>PersonTagsSection</name>
    <message>
        <source>Címkék</source>
        <translation>Tags</translation>
    </message>
    <message>
        <source>Minden megbeszéléseden ott vagy, ezért a címkék rád nem adnak bizonyítékot.</source>
        <translation>You&apos;re in all of your meetings, so tags give no evidence about you.</translation>
    </message>
    <message>
        <source>Címke hozzáadása…</source>
        <translation>Add tag…</translation>
    </message>
    <message>
        <source>Ezeken szokott ott lenni:</source>
        <translation>They are usually there at these:</translation>
    </message>
    <message>
        <source>A többi javaslat</source>
        <translation>Other suggestions</translation>
    </message>
    <message>
        <source>Mind</source>
        <translation>All</translation>
    </message>
    <message>
        <source>Mindegyik javaslat felrakása</source>
        <translation>Apply all suggestions</translation>
    </message>
</context>
<context>
    <name>PipelineSteps</name>
    <message>
        <source>‖ párhuzamosan</source>
        <translation>‖ in parallel</translation>
    </message>
</context>
<context>
    <name>PlayerBar</name>
    <message>
        <source>lekevert hang</source>
        <translation>mixed audio</translation>
    </message>
    <message>
        <source>Előző sor (←)</source>
        <translation>Previous line (←)</translation>
    </message>
    <message>
        <source>Ehhez a megbeszéléshez nincs lejátszható hang</source>
        <translation>This meeting has no playable audio</translation>
    </message>
    <message>
        <source>Szünet (Szóköz)</source>
        <translation>Pause (Space)</translation>
    </message>
    <message>
        <source>Lejátszás (Szóköz)</source>
        <translation>Play (Space)</translation>
    </message>
    <message>
        <source>Következő sor (→)</source>
        <translation>Next line (→)</translation>
    </message>
    <message>
        <source>sáv-előnézet</source>
        <translation>track preview</translation>
    </message>
    <message>
        <source>Pozíció</source>
        <translation>Position</translation>
    </message>
    <message>
        <source>Lejátszási sebesség</source>
        <translation>Playback speed</translation>
    </message>
    <message>
        <source>Hangerő</source>
        <translation>Volume</translation>
    </message>
    <message>
        <source>Némítás feloldása</source>
        <translation>Unmute</translation>
    </message>
    <message>
        <source>Némítás</source>
        <translation>Mute</translation>
    </message>
</context>
<context>
    <name>PreTranscriptView</name>
    <message>
        <source>Ehhez a felvételhez még nincs átirat. Három lépés:</source>
        <translation>This recording has no transcript yet. Three steps:</translation>
    </message>
    <message>
        <source>Miről szólt a megbeszélés?</source>
        <translation>What was the meeting about?</translation>
    </message>
    <message>
        <source>Címkék</source>
        <translation>Tags</translation>
    </message>
    <message>
        <source>Javasolt:</source>
        <translation>Suggested:</translation>
    </message>
    <message>
        <source>Kattintás: hozzáadás · ×: nem illik ide</source>
        <translation>Click: add · ×: doesn’t fit</translation>
    </message>
    <message>
        <source>Észlelt hívás: %1</source>
        <translation>Detected call: %1</translation>
    </message>
    <message>
        <source>Megjegyzés</source>
        <translation>Note</translation>
    </message>
    <message>
        <source>Témák, nevek, szakszavak és az ismert félrehallások: az átíró ebből pontosabb átiratot készít, és az összefoglaló is megkapja. Később, az Összefoglaló fülön is módosítható. Elhagyható.</source>
        <translation>Topics, names, jargon and known mishearings: the transcriber uses them for a more accurate transcript, and the summary gets them too. You can change it later on the Summary tab. Optional.</translation>
    </message>
    <message>
        <source>Előkészítés</source>
        <translation>Preparation</translation>
    </message>
    <message>
        <source>opcionális</source>
        <translation>optional</translation>
    </message>
    <message>
        <source>Résztvevők azonosítása hang alapján</source>
        <translation>Identify participants by voice</translation>
    </message>
    <message>
        <source>Az ismert személyeket a hanglenyomatuk alapján előre megnevezi. Kihagyható: az átirat névtelen beszélőkkel is elkészül.</source>
        <translation>Names known people in advance based on their voiceprints. Can be skipped: the transcript is created with unnamed speakers as well.</translation>
    </message>
    <message>
        <source>Ezen a gépen nincs telepítve a hangfelismerő modell, ezért ez a lépés kimarad. Az átirat névtelen beszélőkkel készül el, a neveket utólag is megadhatod.</source>
        <translation>The voice recognition model is not installed on this computer, so this step is skipped. The transcript is created with unnamed speakers; you can add the names later.</translation>
    </message>
    <message>
        <source>Lekeverés készítése</source>
        <translation>Create mixdown</translation>
    </message>
    <message>
        <source>Kész: a sávokból egy fájl a visszahallgatáshoz. Az átírás is ebből dolgozik.</source>
        <translation>Done: one file made from the tracks for playback. Transcription uses it too.</translation>
    </message>
    <message>
        <source>Most készül a sávokból egy fájl a visszahallgatáshoz és az átíráshoz.</source>
        <translation>Creating one file from the tracks for playback and transcription.</translation>
    </message>
    <message>
        <source>A sávok változtak a legutóbbi lekeverés óta. Az átírás indításakor magától frissül, de előre is elkészítheted.</source>
        <translation>The tracks have changed since the last mixdown. It is updated automatically when transcription starts, but you can create it in advance.</translation>
    </message>
    <message>
        <source>A sávokból egy fájl a visszahallgatáshoz. Az átírás is ebből dolgozik, ezért az indításkor magától elkészül; előre is elkészítheted.</source>
        <translation>One file made from the tracks for playback. Transcription uses it too, so it is created automatically when transcription starts; you can also create it in advance.</translation>
    </message>
    <message>
        <source>Megszakítás</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Lekeverés frissítése</source>
        <translation>Update mixdown</translation>
    </message>
    <message>
        <source>Lekeverés most</source>
        <translation>Mix down now</translation>
    </message>
    <message>
        <source>Átírás</source>
        <translation>Transcription</translation>
    </message>
    <message>
        <source>Átírás indítása</source>
        <translation>Start transcription</translation>
    </message>
    <message>
        <source>Szolgáltató: %1</source>
        <translation>Provider: %1</translation>
    </message>
    <message>
        <source>az átírással egy menetben</source>
        <translation>together with transcription</translation>
    </message>
    <message>
        <source>Megszakítás folyamatban; a felvétel és a sávok érintetlenek maradnak.</source>
        <translation>Cancelling; the recording and the tracks are left untouched.</translation>
    </message>
    <message>
        <source>Közben nyugodtan dolgozz tovább; szólunk, ha kész.</source>
        <translation>Feel free to keep working; we&apos;ll let you know when it&apos;s done.</translation>
    </message>
    <message>
        <source>Az átírás nem sikerült</source>
        <translation>Transcription failed</translation>
    </message>
    <message>
        <source>A felvétel és a sávok érintetlenek.</source>
        <translation>The recording and the tracks are untouched.</translation>
    </message>
    <message>
        <source>Technikai részletek</source>
        <translation>Technical details</translation>
    </message>
    <message>
        <source>Újrapróbálás</source>
        <translation>Try again</translation>
    </message>
    <message>
        <source>Kontextus és előkészítés módosítása</source>
        <translation>Edit context and preparation</translation>
    </message>
</context>
<context>
    <name>PromptLibrary</name>
    <message>
        <source>az összefoglaló nyelve angolul (pl. Hungarian)</source>
        <translation>the summary language in English (e.g. Hungarian)</translation>
    </message>
    <message>
        <source>az összefoglaló nyelve, ahogy a beállításban áll</source>
        <translation>the summary language as written in the setting</translation>
    </message>
    <message>
        <source>## Cím + 1–2 mondat, témánként</source>
        <translation>## Title + 1–2 sentences, per topic</translation>
    </message>
    <message>
        <source>## &lt;a téma címe&gt;
&lt;1–2 mondatos összegzés&gt;

## &lt;a következő téma címe&gt;
&lt;1–2 mondatos összegzés&gt;</source>
        <translation>## &lt;topic title&gt;
&lt;1–2 sentence summary&gt;

## &lt;next topic title&gt;
&lt;1–2 sentence summary&gt;</translation>
    </message>
    <message>
        <source>összegzés, ## Döntések, ## Nyitott kérdések, ## Teendők</source>
        <translation>summary, ## Döntések, ## Nyitott kérdések, ## Teendők</translation>
    </message>
    <message>
        <source>&lt;egy bekezdés összegzés a témáról&gt;

## Döntések
- &lt;egy döntés soronként&gt;

## Nyitott kérdések
- &lt;egy nyitott kérdés soronként&gt;

## Teendők
- &lt;a teendő szövege&gt; — &lt;felelős&gt; (&lt;határidő&gt;)</source>
        <translation>&lt;one paragraph summarising the topic&gt;

## Döntések
- &lt;one decision per line&gt;

## Nyitott kérdések
- &lt;one open question per line&gt;

## Teendők
- &lt;the action item&gt; — &lt;owner&gt; (&lt;deadline&gt;)</translation>
    </message>
    <message>
        <source>két sor: pick / new</source>
        <translation>two lines: pick / new</translation>
    </message>
    <message>
        <source>egy bekezdés</source>
        <translation>one paragraph</translation>
    </message>
    <message>
        <source>&lt;2–4 mondatos vezetői összefoglaló&gt;</source>
        <translation>&lt;executive summary of 2–4 sentences&gt;</translation>
    </message>
</context>
<context>
    <name>QmlShellBridge</name>
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
    <name>ReadinessModel</name>
    <message>
        <source>Hiányzik: %1 (%2)</source>
        <translation>Missing: %1 (%2)</translation>
    </message>
    <message>
        <source>Jelentkezz be a Tanara Cloudba</source>
        <translation>Sign in to Tanara Cloud</translation>
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
    <name>RecorderDefaultPill</name>
    <message>
        <source>alapért.</source>
        <translation>default</translation>
    </message>
</context>
<context>
    <name>RecorderGroupHeader</name>
    <message>
        <source>MIKROFONOK</source>
        <translation>MICROPHONES</translation>
    </message>
    <message>
        <source>HANGKIMENETEK</source>
        <translation>AUDIO OUTPUTS</translation>
    </message>
    <message>
        <source>EGYÉB BEMENETEK</source>
        <translation>OTHER INPUTS</translation>
    </message>
    <message>
        <source>amit mondasz</source>
        <translation>what you say</translation>
    </message>
    <message>
        <source>amit hallasz</source>
        <translation>what you hear</translation>
    </message>
</context>
<context>
    <name>RecorderPill</name>
    <message>
        <source>Felvétel indítása</source>
        <translation>Start recording</translation>
    </message>
    <message>
        <source>Leállítás</source>
        <translation>Stop</translation>
    </message>
    <message>
        <source>Kinyitás</source>
        <translation>Expand</translation>
    </message>
</context>
<context>
    <name>RecorderView</name>
    <message>
        <source>Tanara felvevő</source>
        <translation>Tanara Recorder</translation>
    </message>
    <message>
        <source>Mindig felül</source>
        <translation>Always on top</translation>
    </message>
    <message>
        <source>Mindig felül: ezen a rendszeren az ablakkezelő dönt (Wayland)</source>
        <translation>Always on top: on this system the window manager decides (Wayland)</translation>
    </message>
    <message>
        <source>Összecsukás</source>
        <translation>Collapse</translation>
    </message>
    <message>
        <source>Kinyitás</source>
        <translation>Expand</translation>
    </message>
    <message>
        <source>Pirula méret</source>
        <translation>Pill size</translation>
    </message>
    <message>
        <source>Háttérbe (tálcára)</source>
        <translation>Send to background (tray)</translation>
    </message>
    <message>
        <source>Bezárás</source>
        <translation>Close</translation>
    </message>
    <message>
        <source>Vége a megbeszélésnek?</source>
        <translation>Is the meeting over?</translation>
    </message>
    <message>
        <source>Folytatom</source>
        <translation>Keep recording</translation>
    </message>
    <message>
        <source>Leállítom</source>
        <translation>Stop</translation>
    </message>
    <message>
        <source>Automatikus név · kattints az átnevezéshez</source>
        <translation>Automatic name · click to rename</translation>
    </message>
    <message>
        <source>Címke hozzáadása</source>
        <translation>Add tag</translation>
    </message>
    <message>
        <source>Címke</source>
        <translation>Tag</translation>
    </message>
    <message>
        <source>Címke…</source>
        <translation>Tag…</translation>
    </message>
    <message>
        <source>Felvétel indítása</source>
        <translation>Start recording</translation>
    </message>
    <message>
        <source>Kapcsolj be legalább egy forrást</source>
        <translation>Turn on at least one source</translation>
    </message>
    <message>
        <source>LEZÁRÁS…</source>
        <translation>FINISHING…</translation>
    </message>
    <message>
        <source>FELVÉTEL</source>
        <translation>RECORDING</translation>
    </message>
    <message>
        <source>Leállítás</source>
        <translation>Stop</translation>
    </message>
    <message>
        <source>Elmentve, de hiányosan</source>
        <translation>Saved, but incomplete</translation>
    </message>
    <message>
        <source>Elmentve</source>
        <translation>Saved</translation>
    </message>
    <message>
        <source>Megnyitás az elemzőben</source>
        <translation>Open in analyzer</translation>
    </message>
    <message>
        <source>Új felvétel</source>
        <translation>New recording</translation>
    </message>
    <message>
        <source>Nem találok hangeszközt</source>
        <translation>No audio device found</translation>
    </message>
    <message>
        <source>Csatlakoztass mikrofont vagy fejhallgatót. Linuxon ellenőrizd, hogy fut-e a PipeWire vagy a PulseAudio.</source>
        <translation>Connect a microphone or headphones. On Linux, check that PipeWire or PulseAudio is running.</translation>
    </message>
    <message>
        <source>Csatlakoztass mikrofont vagy fejhallgatót, és ellenőrizd a rendszer hangbeállításait.</source>
        <translation>Connect a microphone or headphones, and check the system sound settings.</translation>
    </message>
    <message>
        <source>Újrakeresés</source>
        <translation>Search again</translation>
    </message>
    <message>
        <source>Rögzítés beállításai</source>
        <translation>Recording settings</translation>
    </message>
    <message numerus="yes">
        <source>%n sáv</source>
        <translation>
            <numerusform>%n track</numerusform>
            <numerusform>%n tracks</numerusform>
        </translation>
    </message>
    <message>
        <source>%1 / %2 forrás rögzül</source>
        <translation>Recording %1 of %2 sources</translation>
    </message>
    <message>
        <source>Források</source>
        <translation>Sources</translation>
    </message>
    <message>
        <source>A kikapcsolt eszközök jelzője tájékoztató.</source>
        <translation>Meters of switched-off devices are for information only.</translation>
    </message>
    <message>
        <source>Beszélj vagy játssz le hangot: a mozgó jelző mutatja, melyik eszközön jön hang.</source>
        <translation>Speak or play some audio: the moving meter shows which device carries sound.</translation>
    </message>
    <message>
        <source>Az eszközt leválasztották; a sávja lezárult</source>
        <translation>The device was disconnected; its track is closed</translation>
    </message>
    <message>
        <source>Kikapcsolás: a sáv itt lezárul; újra bekapcsolva új szakasz indul</source>
        <translation>Turn off: the track ends here; turning it on again starts a new segment</translation>
    </message>
    <message>
        <source>Bekapcsolás: a sávja ettől a pillanattól indul</source>
        <translation>Turn on: its track starts from this moment</translation>
    </message>
    <message>
        <source>A rögzített sávok felvétel közben nem kapcsolhatók ki. Új eszköz bekapcsolható: a sávja attól a pillanattól indul.</source>
        <translation>Tracks being recorded cannot be turned off during recording. A new device can be turned on: its track starts from that moment.</translation>
    </message>
    <message>
        <source>A felvétel még fut</source>
        <translation>The recording is still running</translation>
    </message>
    <message>
        <source>Bezárás helyett a háttérben is folytatódhat; a tálca-ikonról bármikor visszahozod.</source>
        <translation>Instead of closing, it can continue in the background; bring it back from the tray icon at any time.</translation>
    </message>
    <message>
        <source>Fusson a háttérben</source>
        <translation>Run in the background</translation>
    </message>
    <message>
        <source>Leállítás és bezárás</source>
        <translation>Stop and close</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
</context>
<context>
    <name>RecorderWindow</name>
    <message>
        <source>Tanara felvevő</source>
        <translation>Tanara Recorder</translation>
    </message>
</context>
<context>
    <name>ReviewFocusCard</name>
    <message>
        <source>Lejátszás / szünet (Szóköz) · újra az elejéről: R</source>
        <translation>Play / pause (Space) · from the start: R</translation>
    </message>
    <message>
        <source>javaslat frissült</source>
        <translation>suggestion updated</translation>
    </message>
    <message>
        <source>eldöntve: nem azonosítható</source>
        <translation>decided: unidentifiable</translation>
    </message>
    <message>
        <source>eldöntve: javítva · %1</source>
        <translation>decided: corrected · %1</translation>
    </message>
    <message>
        <source>nem minta</source>
        <translation>not a sample</translation>
    </message>
    <message>
        <source>minta</source>
        <translation>sample</translation>
    </message>
    <message>
        <source>◇ később hagyva</source>
        <translation>◇ left for later</translation>
    </message>
    <message>
        <source>új · a döntéseid miatt</source>
        <translation>new · because of your decisions</translation>
    </message>
    <message>
        <source>új</source>
        <translation>new</translation>
    </message>
    <message>
        <source>Új beszélő</source>
        <translation>New speaker</translation>
    </message>
    <message>
        <source>marad · (= J)</source>
        <translation>stays · (= J)</translation>
    </message>
    <message>
        <source>javaslat · hívás hangja</source>
        <translation>suggestion · call audio</translation>
    </message>
    <message>
        <source>javaslat · mikrofon</source>
        <translation>suggestion · microphone</translation>
    </message>
    <message>
        <source>javaslat · %1</source>
        <translation>suggestion · %1</translation>
    </message>
    <message>
        <source>javaslat</source>
        <translation>suggestion</translation>
    </message>
    <message>
        <source>javasolt</source>
        <translation>suggested</translation>
    </message>
    <message>
        <source>hangra kétes</source>
        <translation>uncertain by voice</translation>
    </message>
    <message>
        <source>✓ magától eldőlt</source>
        <translation>✓ settled on its own</translation>
    </message>
    <message>
        <source>eldöntve: marad · %1</source>
        <translation>decided: stays · %1</translation>
    </message>
    <message>
        <source>A sor ide kerül: %1 · Shift: nem minta</source>
        <translation>The line goes to: %1 · Shift: not a sample</translation>
    </message>
    <message>
        <source>A név marad: %1 (nincs jobb jelölt) · Shift: nem minta</source>
        <translation>The name stays: %1 (no better candidate) · Shift: not a sample</translation>
    </message>
    <message>
        <source>marad</source>
        <translation>stays</translation>
    </message>
    <message>
        <source>A mostani név marad (%1) · Shift: nem minta</source>
        <translation>The current name stays (%1) · Shift: not a sample</translation>
    </message>
    <message>
        <source>Újra azonosítható</source>
        <translation>Identifiable again</translation>
    </message>
    <message>
        <source>Nem azonosítható</source>
        <translation>Unidentifiable</translation>
    </message>
    <message>
        <source>újra számít a hangja</source>
        <translation>its voice counts again</translation>
    </message>
    <message>
        <source>a név marad, nem minta</source>
        <translation>name stays, not a sample</translation>
    </message>
    <message>
        <source>A név marad, de a hang-elemzés teljesen kihagyja: nem minta, nem bizonyíték</source>
        <translation>The name stays, but voice analysis ignores the line completely: not a sample, not evidence</translation>
    </message>
    <message>
        <source>Más…</source>
        <translation>Other…</translation>
    </message>
    <message>
        <source>személyválasztó</source>
        <translation>person picker</translation>
    </message>
    <message>
        <source>A sor panelje: személyválasztó, új beszélő</source>
        <translation>The line panel: person picker, new speaker</translation>
    </message>
    <message>
        <source>a lista végére</source>
        <translation>to the end of the list</translation>
    </message>
    <message>
        <source>= ugyanez, de nem minta</source>
        <translation>= the same, but not a sample</translation>
    </message>
    <message>
        <source>Új beszélő…</source>
        <translation>New speaker…</translation>
    </message>
    <message>
        <source>A sor egy új, névtelen beszélőhöz (+)</source>
        <translation>Move the line to a new, unnamed speaker (+)</translation>
    </message>
    <message>
        <source>Később</source>
        <translation>Later</translation>
    </message>
    <message>
        <source>Nem döntés: a sor a kör végére kerül (◇)</source>
        <translation>Not a decision: the line moves to the end of the round (◇)</translation>
    </message>
    <message>
        <source>most</source>
        <translation>now</translation>
    </message>
    <message>
        <source>→ %1</source>
        <translation>→ %1</translation>
    </message>
    <message>
        <source>%1: marad</source>
        <translation>%1: stays</translation>
    </message>
    <message>
        <source> · nem minta</source>
        <translation> · not a sample</translation>
    </message>
</context>
<context>
    <name>ReviewFocusContextRow</name>
    <message>
        <source>+%1 s</source>
        <translation>+%1 s</translation>
    </message>
    <message>
        <source>Meghallgatás (,)</source>
        <translation>Listen (,)</translation>
    </message>
    <message>
        <source>Meghallgatás (.)</source>
        <translation>Listen (.)</translation>
    </message>
</context>
<context>
    <name>ReviewFocusKeyButton</name>
    <message>
        <source> · nem minta</source>
        <translation> · not a sample</translation>
    </message>
</context>
<context>
    <name>ReviewFocusOffer</name>
    <message>
        <source>sáv-ellentmondás</source>
        <translation>track conflict</translation>
    </message>
    <message>
        <source>eltér a beszélő hangjától</source>
        <translation>differs from the speaker&apos;s voice</translation>
    </message>
    <message>
        <source>az új személyhez hasonló</source>
        <translation>similar to the new person</translation>
    </message>
    <message>
        <source>hangra kétes</source>
        <translation>uncertain by voice</translation>
    </message>
    <message>
        <source>Mind az %1 → %2</source>
        <translation>All %1 → %2</translation>
    </message>
    <message>
        <source>Mind a %1 → %2</source>
        <translation>All %1 → %2</translation>
    </message>
    <message numerus="yes">
        <source>A hátralévőből még &lt;b&gt;%n sor ugyanilyen&lt;/b&gt;</source>
        <translation>
            <numerusform>&lt;b&gt;%n more line&lt;/b&gt; ahead is the same</numerusform>
            <numerusform>&lt;b&gt;%n more lines&lt;/b&gt; ahead are the same</numerusform>
        </translation>
    </message>
    <message>
        <source>Elrejtem</source>
        <translation>Hide</translation>
    </message>
    <message>
        <source>Megmutatom</source>
        <translation>Show</translation>
    </message>
    <message numerus="yes">
        <source>A %n sor listája, egyenként meghallgatható</source>
        <translation>
            <numerusform>The %n line, playable one by one</numerusform>
            <numerusform>The %n lines, playable one by one</numerusform>
        </translation>
    </message>
    <message>
        <source>Igen</source>
        <translation>Yes</translation>
    </message>
    <message>
        <source>Egy visszavonási lépés · Shift+I: egyik sem lesz minta</source>
        <translation>One undo step · Shift+I: none of them becomes a sample</translation>
    </message>
    <message>
        <source>Nem</source>
        <translation>No</translation>
    </message>
    <message>
        <source>Ez a minta ebben a körben nem jön vissza</source>
        <translation>This pattern won&apos;t be offered again in this round</translation>
    </message>
</context>
<context>
    <name>ReviewFocusOpener</name>
    <message>
        <source>új beszélő (hívás hangja)</source>
        <translation>new speaker (call audio)</translation>
    </message>
    <message>
        <source>új beszélő (mikrofon)</source>
        <translation>new speaker (microphone)</translation>
    </message>
    <message>
        <source>új beszélő</source>
        <translation>new speaker</translation>
    </message>
    <message>
        <source>Mind az %1 → %2</source>
        <translation>All %1 → %2</translation>
    </message>
    <message>
        <source>Mind a %1 → %2</source>
        <translation>All %1 → %2</translation>
    </message>
    <message>
        <source>Nyitó-javaslat %1 / %2 · a kör elején</source>
        <translation>Opening suggestion %1 / %2 · start of the round</translation>
    </message>
    <message>
        <source>Nyitó-javaslat · a kör elején</source>
        <translation>Opening suggestion · start of the round</translation>
    </message>
    <message>
        <source>nagy, erős csoport — egyben, mielőtt a sorok egyenként jönnek</source>
        <translation>a large, strong group — all at once, before the lines come one by one</translation>
    </message>
    <message numerus="yes">
        <source>%n sor → %1</source>
        <translation>
            <numerusform>%n line → %1</numerusform>
            <numerusform>%n lines → %1</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>· %n sor</source>
        <translation>
            <numerusform>· %n line</numerusform>
            <numerusform>· %n lines</numerusform>
        </translation>
    </message>
    <message>
        <source>Hatás, ha elfogadod:</source>
        <translation>Effect if you accept:</translation>
    </message>
    <message>
        <source>%1 +%2 sor (nem minta)</source>
        <translation>%1 +%2 lines (not samples)</translation>
    </message>
    <message>
        <source>%1 +%2 sor (+%3 minta)</source>
        <translation>%1 +%2 lines (+%3 samples)</translation>
    </message>
    <message>
        <source>%1 −%2</source>
        <translation>%1 −%2</translation>
    </message>
    <message>
        <source>kétes marad: %1</source>
        <translation>still uncertain: %1</translation>
    </message>
    <message numerus="yes">
        <source>Szúrópróba · %n időben távoli sor</source>
        <translation>
            <numerusform>Spot check · %n line spread in time</numerusform>
            <numerusform>Spot check · %n lines spread in time</numerusform>
        </translation>
    </message>
    <message>
        <source>1–3: lejátszás · meghallgatás után ✓ / ✗ (Shift+1–3)</source>
        <translation>1–3: play · after listening ✓ / ✗ (Shift+1–3)</translation>
    </message>
    <message>
        <source>%1/%2 meghallgatva · %3 egyezett — ez nem garancia</source>
        <translation>%1/%2 listened · %3 matched — this is no guarantee</translation>
    </message>
    <message>
        <source>szól…</source>
        <translation>playing…</translation>
    </message>
    <message>
        <source>egyezett</source>
        <translation>matched</translation>
    </message>
    <message>
        <source>nem egyezett</source>
        <translation>no match</translation>
    </message>
    <message>
        <source>Kattintás (vagy Shift+%1): ✓ / ✗ váltás — a füled ítélete</source>
        <translation>Click (or Shift+%1): toggle ✓ / ✗ — your ear decides</translation>
    </message>
    <message>
        <source>Egy visszavonási lépés · Shift: egyik sem lesz minta</source>
        <translation>One undo step · Shift: none of them becomes a sample</translation>
    </message>
    <message>
        <source>Egyenként</source>
        <translation>One by one</translation>
    </message>
    <message numerus="yes">
        <source>A %n sor a munkalista elejére kerül, és egyenként jön</source>
        <translation>
            <numerusform>The %n line moves to the front of the worklist and comes one by one</numerusform>
            <numerusform>The %n lines move to the front of the worklist and come one by one</numerusform>
        </translation>
    </message>
    <message>
        <source>Később</source>
        <translation>Later</translation>
    </message>
    <message>
        <source>A nyitó kimarad; a sorok a helyükön maradnak a munkalistán</source>
        <translation>The suggestion is skipped; the lines stay in place on the worklist</translation>
    </message>
    <message>
        <source>Shift = nem minta</source>
        <translation>Shift = not a sample</translation>
    </message>
</context>
<context>
    <name>ReviewFocusSummary</name>
    <message>
        <source>%1. kör kész</source>
        <translation>Round %1 done</translation>
    </message>
    <message>
        <source>Kész: &lt;b&gt;%1 eldöntve&lt;/b&gt;, %2 nem azonosítható, %3 később.</source>
        <translation>Done: &lt;b&gt;%1 decided&lt;/b&gt;, %2 unidentifiable, %3 later.</translation>
    </message>
    <message numerus="yes">
        <source>%n sor magától eldőlt (a háttér-elemzés szerint már nem kétes).</source>
        <translation>
            <numerusform>%n line settled on its own (the background analysis no longer finds it uncertain).</numerusform>
            <numerusform>%n lines settled on their own (the background analysis no longer finds them uncertain).</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Megerősítve %n lesz minta.</source>
        <translation>
            <numerusform>Once confirmed, %n becomes a sample.</numerusform>
            <numerusform>Once confirmed, %n become samples.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor magától eldőlt; mind rövid, nem lenne minta.</source>
        <translation>
            <numerusform>%n line settled on its own; it is short and would not be a sample.</numerusform>
            <numerusform>%n lines settled on their own; all are short and would not be samples.</numerusform>
        </translation>
    </message>
    <message>
        <source>Mind megerősítem</source>
        <translation>Confirm all</translation>
    </message>
    <message>
        <source>Egy lépésben, visszavonható (Ctrl+Z)</source>
        <translation>In one step, can be undone (Ctrl+Z)</translation>
    </message>
    <message>
        <source>%1 +%2 minta</source>
        <translation>%1 +%2 samples</translation>
    </message>
    <message>
        <source>A háttér-elemzés nem talált új kétes sort.</source>
        <translation>The background analysis found no new uncertain lines.</translation>
    </message>
    <message numerus="yes">
        <source>Az elemzés %n új kétes sort talált (ebből %1 a mostani döntéseid miatt). Kör közben egyik sem került eléd.</source>
        <translation>
            <numerusform>The analysis found %n new uncertain line (%1 of them because of your decisions). None was put ahead of you during the round.</numerusform>
            <numerusform>The analysis found %n new uncertain lines (%1 of them because of your decisions). None was put ahead of you during the round.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Az elemzés %n új kétes sort talált. Kör közben egyik sem került eléd.</source>
        <translation>
            <numerusform>The analysis found %n new uncertain line. None was put ahead of you during the round.</numerusform>
            <numerusform>The analysis found %n new uncertain lines. None was put ahead of you during the round.</numerusform>
        </translation>
    </message>
    <message>
        <source>Következő kör (%1)</source>
        <translation>Next round (%1)</translation>
    </message>
    <message>
        <source>Az új kétes sorok és a később hagyottak, időrendben</source>
        <translation>The new uncertain lines and the ones left for later, in time order</translation>
    </message>
    <message>
        <source>Később hagyottak (%1)</source>
        <translation>Left for later (%1)</translation>
    </message>
    <message>
        <source>Csak a később hagyott sorok (az új kétesek várnak)</source>
        <translation>Only the lines left for later (the new uncertain lines wait)</translation>
    </message>
    <message>
        <source>Kész</source>
        <translation>Done</translation>
    </message>
    <message>
        <source>Vissza a Javítás nézetbe</source>
        <translation>Back to the Fix view</translation>
    </message>
</context>
<context>
    <name>ReviewFocusView</name>
    <message>
        <source>%1. kör</source>
        <translation>Round %1</translation>
    </message>
    <message>
        <source>elemzés fut…</source>
        <translation>analysis running…</translation>
    </message>
    <message numerus="yes">
        <source>%n magától eldőlt</source>
        <translation>
            <numerusform>%n settled on its own</numerusform>
            <numerusform>%n settled on their own</numerusform>
        </translation>
    </message>
    <message>
        <source>Mind megerősítem</source>
        <translation>Confirm all</translation>
    </message>
    <message numerus="yes">
        <source>%n sor megerősítése egy lépésben (minta lesz); a rövid (1,5 mp alatti) sorokhoz nem nyúl</source>
        <translation>
            <numerusform>Confirm %n line in one step (it becomes a sample); short lines (under 1.5 s) are left alone</numerusform>
            <numerusform>Confirm %n lines in one step (they become samples); short lines (under 1.5 s) are left alone</numerusform>
        </translation>
    </message>
    <message>
        <source>A magától eldőlt sorok mind rövidek (1,5 mp alatt): nem lennének minta</source>
        <translation>The lines settled on their own are all short (under 1.5 s): they would not be samples</translation>
    </message>
    <message>
        <source>haladás</source>
        <translation>progress</translation>
    </message>
    <message>
        <source>%1 / %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <source>+%1 új vár</source>
        <translation>+%1 new waiting</translation>
    </message>
    <message>
        <source>nincs új</source>
        <translation>nothing new</translation>
    </message>
    <message>
        <source>A háttér-elemzés %1 új kétes sort talált. Kattintás: a kör végére kerülnek (előtted semmi nem mozdul).</source>
        <translation>The background analysis found %1 new uncertain lines. Click: they go to the end of the round (nothing ahead of you moves).</translation>
    </message>
    <message>
        <source>A háttér-elemzés új találatai itt gyűlnek; a munkalista közben nem változik.</source>
        <translation>New findings of the background analysis collect here; the worklist does not change meanwhile.</translation>
    </message>
    <message>
        <source>Auto-lejátszás</source>
        <translation>Autoplay</translation>
    </message>
    <message>
        <source>A kártyára lépő sor magától megszólal (a sor + 0,3 mp)</source>
        <translation>The line on the card plays by itself (the line + 0.3 s)</translation>
    </message>
    <message>
        <source>Az átnézendő sorok keresése…</source>
        <translation>Looking for lines to review…</translation>
    </message>
    <message>
        <source>Nincs átnézendő sor — minden megszólalás beszélője rendben van.</source>
        <translation>Nothing to review — every utterance has the right speaker.</translation>
    </message>
    <message>
        <source>Beszélők újraellenőrzése…</source>
        <translation>Re-check speakers…</translation>
    </message>
    <message>
        <source>A megerősített és javított sorok hangjához mérem a többi sort</source>
        <translation>Compares the other lines with the voice of the confirmed and corrected lines</translation>
    </message>
</context>
<context>
    <name>ReviewFocusWorklist</name>
    <message>
        <source>%1 · következő kétes %2</source>
        <translation>%1 · next uncertain %2</translation>
    </message>
    <message>
        <source>%1 · nincs több kétes</source>
        <translation>%1 · no more uncertain lines</translation>
    </message>
    <message>
        <source>Munkalista</source>
        <translation>Worklist</translation>
    </message>
    <message>
        <source>↑ / ↓ tételek · B következő kétes</source>
        <translation>↑ / ↓ items · B next uncertain</translation>
    </message>
    <message>
        <source>eldöntve</source>
        <translation>decided</translation>
    </message>
    <message>
        <source>hátra</source>
        <translation>to do</translation>
    </message>
    <message>
        <source>később</source>
        <translation>later</translation>
    </message>
    <message>
        <source>nem azon.</source>
        <translation>unident.</translation>
    </message>
    <message>
        <source>magától eldőlt</source>
        <translation>settled on its own</translation>
    </message>
</context>
<context>
    <name>ReviewGroups</name>
    <message>
        <source>új résztvevő</source>
        <translation>new participant</translation>
    </message>
    <message>
        <source>új távoli beszélő</source>
        <translation>new remote speaker</translation>
    </message>
    <message>
        <source>új helyi beszélő</source>
        <translation>new local speaker</translation>
    </message>
    <message>
        <source>hang %1</source>
        <translation>voice %1</translation>
    </message>
    <message>
        <source>átlag %1 hangjához</source>
        <translation>average for %1&apos;s voice</translation>
    </message>
    <message numerus="yes">
        <source>%n sor a másik sávon</source>
        <translation>
            <numerusform>%n line on the other track</numerusform>
            <numerusform>%n lines on the other track</numerusform>
        </translation>
    </message>
    <message>
        <source>Most: %1 · javaslat: %2</source>
        <translation>Now: %1 · suggestion: %2</translation>
    </message>
    <message>
        <source>Most: %1 · hang nélkül nem javaslok nevet, egyenként dönthető</source>
        <translation>Now: %1 · no name suggested without voice data, decide one by one</translation>
    </message>
    <message>
        <source>másik sávon szóltak</source>
        <translation>spoke on a different track</translation>
    </message>
    <message>
        <source>%1: %2; a sorok: %3</source>
        <translation>%1: %2; lines: %3</translation>
    </message>
    <message>
        <source>%1 a hívás hangján beszél</source>
        <translation>%1 speaks on the call audio</translation>
    </message>
    <message>
        <source>%1 a mikrofonon beszél</source>
        <translation>%1 speaks on the microphone</translation>
    </message>
    <message numerus="yes">
        <source>Még %n sor hangja hasonlít: %1</source>
        <translation>
            <numerusform>%n more line sounds alike: %1</numerusform>
            <numerusform>%n more lines sound alike: %1</numerusform>
        </translation>
    </message>
    <message>
        <source>hang %1, mind a(z) %2 oldalon</source>
        <translation>voice %1, all on the %2 side</translation>
    </message>
    <message>
        <source>mind a hívás hangján</source>
        <translation>all on the call audio</translation>
    </message>
    <message>
        <source>mind a mikrofonon</source>
        <translation>all on the microphone</translation>
    </message>
    <message>
        <source>%1 megerősített sorai két hangra esnek</source>
        <translation>%1&apos;s confirmed lines fall into two voices</translation>
    </message>
    <message numerus="yes">
        <source>Szétválasztás: %n sor → %1</source>
        <translation>
            <numerusform>Split: %n line → %1</numerusform>
            <numerusform>Split: %n lines → %1</numerusform>
        </translation>
    </message>
    <message>
        <source>két hang: %1 egyezés</source>
        <translation>two voices: %1 match</translation>
    </message>
    <message>
        <source>%1 + %2 megerősített sor</source>
        <translation>%1 + %2 confirmed lines</translation>
    </message>
    <message numerus="yes">
        <source>%n sor hangja inkább: %1</source>
        <translation>
            <numerusform>%n line sounds more like: %1</numerusform>
            <numerusform>%n lines sound more like: %1</numerusform>
        </translation>
    </message>
    <message>
        <source>Most: %1</source>
        <translation>Now: %1</translation>
    </message>
    <message>
        <source>%1 hangjához csak %2</source>
        <translation>to %1&apos;s voice only %2</translation>
    </message>
    <message>
        <source>a megerősített soraihoz mérve</source>
        <translation>measured against their confirmed lines</translation>
    </message>
    <message numerus="yes">
        <source>%n rövid sor</source>
        <translation>
            <numerusform>%n short line</numerusform>
            <numerusform>%n short lines</numerusform>
        </translation>
    </message>
    <message>
        <source>Túl rövidek a hang-ellenőrzéshez — csak tájékoztató</source>
        <translation>Too short for the voice check — informational only</translation>
    </message>
</context>
<context>
    <name>SettingsCombo</name>
    <message>
        <source>Lista megnyitása</source>
        <translation>Open the list</translation>
    </message>
</context>
<context>
    <name>SettingsEmbeddingCard</name>
    <message>
        <source>Beágyazás</source>
        <translation>Embedding</translation>
    </message>
    <message>
        <source>címkejavaslatokhoz (embedding)</source>
        <translation>for tag suggestions (embedding)</translation>
    </message>
    <message>
        <source>Könyvtár előkészítése</source>
        <translation>Preparing the library</translation>
    </message>
    <message>
        <source>Mentés után indul: a meglévő átiratokat egyszer előkészítjük, utána az újakat magától.</source>
        <translation>Starts after saving: existing transcripts are prepared once, new ones automatically after that.</translation>
    </message>
    <message>
        <source>Megszakítás</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Közben dolgozhatsz; a már előkészített megbeszéléseknél a javaslatok azonnal jobbak.</source>
        <translation>You can keep working; suggestions improve right away for meetings already prepared.</translation>
    </message>
    <message>
        <source>Folytatás</source>
        <translation>Continue</translation>
    </message>
    <message>
        <source>Újraelőkészítés</source>
        <translation>Prepare again</translation>
    </message>
    <message>
        <source>%1 előkészítve</source>
        <translation>%1 prepared</translation>
    </message>
    <message>
        <source>Előkészítés</source>
        <translation>Preparation</translation>
    </message>
    <message>
        <source>Kapcsolat tesztelése</source>
        <translation>Test connection</translation>
    </message>
    <message>
        <source>Haladó</source>
        <translation>Advanced</translation>
    </message>
</context>
<context>
    <name>SettingsGeneralPage</name>
    <message>
        <source>Megjelenés</source>
        <translation>Appearance</translation>
    </message>
    <message>
        <source>Nyelv</source>
        <translation>Language</translation>
    </message>
    <message>
        <source>A nyelv a Tanara következő indításakor vált.</source>
        <translation>The language changes the next time Tanara starts.</translation>
    </message>
    <message>
        <source>Téma</source>
        <translation>Theme</translation>
    </message>
    <message>
        <source>Rendszer</source>
        <translation>System</translation>
    </message>
    <message>
        <source>Világos</source>
        <translation>Light</translation>
    </message>
    <message>
        <source>Sötét</source>
        <translation>Dark</translation>
    </message>
    <message>
        <source>Mappák</source>
        <translation>Folders</translation>
    </message>
    <message>
        <source>Tallózás…</source>
        <translation>Browse…</translation>
    </message>
    <message>
        <source>Megnyitás a fájlkezelőben</source>
        <translation>Open in the file manager</translation>
    </message>
    <message>
        <source>Az első indításkor látott bevezető:</source>
        <translation>The introduction shown at first start:</translation>
    </message>
    <message>
        <source>Első lépések</source>
        <translation>Getting started</translation>
    </message>
</context>
<context>
    <name>SettingsProviderCard</name>
    <message>
        <source>Kinyitás</source>
        <translation>Expand</translation>
    </message>
    <message>
        <source>Összecsukás</source>
        <translation>Collapse</translation>
    </message>
    <message>
        <source>Beállítva</source>
        <translation>Configured</translation>
    </message>
    <message>
        <source>Szolgáltató</source>
        <translation>Provider</translation>
    </message>
    <message>
        <source>Válassz szolgáltatót…</source>
        <translation>Choose a provider…</translation>
    </message>
    <message>
        <source>Ez a lépés a Tanara Cloudra van állítva, ami még nem érhető el. Válassz saját kulcsos szolgáltatót.</source>
        <translation>This step is set to Tanara Cloud, which is not available yet. Pick a provider with your own key.</translation>
    </message>
    <message>
        <source>A modell gondolkodása az összefoglalónál</source>
        <translation>Model reasoning for summaries</translation>
    </message>
    <message>
        <source>Automatikus (kikapcsolva)</source>
        <translation>Automatic (off)</translation>
    </message>
    <message>
        <source>Kikapcsolva</source>
        <translation>Off</translation>
    </message>
    <message>
        <source>Bekapcsolva</source>
        <translation>On</translation>
    </message>
    <message>
        <source>Kikapcsolva gyorsabb, és a kis modellek nem élik fel a válaszkeretet gondolkodásra, mielőtt válaszolnának. Az Automatikus a modellcsaládnak megfelelő módon kapcsolja ki.</source>
        <translation>Off is faster, and small models do not use up their output budget on reasoning before they answer. Automatic switches it off in the way that suits the model family.</translation>
    </message>
    <message>
        <source>A modell kontextusa</source>
        <translation>Model context</translation>
    </message>
    <message>
        <source>LM Studiónál a Tanara a feladat előtt ekkorával, egy szálon tölti be a modellt; más szervernél ott kell beállítani.</source>
        <translation>With LM Studio, Tanara loads the model with this context, single-threaded, before the job; on other servers set it there.</translation>
    </message>
    <message>
        <source>Kapcsolat tesztelése</source>
        <translation>Test connection</translation>
    </message>
    <message>
        <source>Haladó</source>
        <translation>Advanced</translation>
    </message>
</context>
<context>
    <name>SettingsProviderFields</name>
    <message>
        <source>Kulcs elrejtése</source>
        <translation>Hide key</translation>
    </message>
    <message>
        <source>Kulcs megmutatása</source>
        <translation>Show key</translation>
    </message>
    <message>
        <source>Lekérés…</source>
        <translation>Fetching…</translation>
    </message>
    <message>
        <source>Lekérés</source>
        <translation>Fetch</translation>
    </message>
    <message>
        <source>A modellek lekérése a megadott címről</source>
        <translation>Fetch the models from the given address</translation>
    </message>
</context>
<context>
    <name>SettingsRecordingPage</name>
    <message>
        <source>Alapértelmezett források</source>
        <translation>Default sources</translation>
    </message>
    <message>
        <source>Ezek kapnak sávot, amikor megnyitod a felvevőt; felvételenként ott is módosíthatod. A nevükre kattintva átnevezheted őket.</source>
        <translation>These get a track when you open the recorder; you can change them there for each recording. Click a name to rename the device.</translation>
    </message>
    <message>
        <source>Nem találtam hangeszközt. Csatlakoztass mikrofont, vagy ellenőrizd a rendszer hangbeállításait.</source>
        <translation>No audio device was found. Connect a microphone or check the system sound settings.</translation>
    </message>
    <message>
        <source>Alapból sávot kap</source>
        <translation>Gets a track by default</translation>
    </message>
    <message>
        <source>Alapból nem kap sávot</source>
        <translation>Gets no track by default</translation>
    </message>
    <message>
        <source>Átnevezés (eredeti név: %1)</source>
        <translation>Rename (original name: %1)</translation>
    </message>
    <message>
        <source>Átnevezés</source>
        <translation>Rename</translation>
    </message>
    <message>
        <source>Az eszköz neve</source>
        <translation>Device name</translation>
    </message>
    <message>
        <source>Minden eszköz rögzítése automatikusan</source>
        <translation>Record every device automatically</translation>
    </message>
    <message>
        <source>Ha nem tudod előre, honnan jön a hang. A csendes sávokat utólag eldobottnak jelöli, de megtartja.</source>
        <translation>For when you cannot tell in advance where the sound will come from. Silent tracks are marked as discarded afterwards, but kept.</translation>
    </message>
    <message>
        <source>Hangminőség</source>
        <translation>Audio quality</translation>
    </message>
    <message>
        <source>Lekeverés</source>
        <translation>Mixdown</translation>
    </message>
    <message>
        <source>Automatikusan, a felvétel után</source>
        <translation>Automatically, after the recording</translation>
    </message>
    <message>
        <source>Kézzel, a Sávok fülön</source>
        <translation>Manually, on the Tracks tab</translation>
    </message>
    <message>
        <source>A lekevert fájlból megy a visszahallgatás és az átírás. Ha még nincs meg, az átírás előtt magától elkészül.</source>
        <translation>Playback and transcription use the mixdown. If it does not exist yet, it is created automatically before transcription.</translation>
    </message>
</context>
<context>
    <name>SettingsServicesPage</name>
    <message>
        <source>Saját kulcs</source>
        <translation>Own keys</translation>
    </message>
    <message>
        <source>A te fiókod a szolgáltatóknál; nekik fizetsz közvetlenül.</source>
        <translation>Your own accounts with the providers; you pay them directly.</translation>
    </message>
    <message>
        <source>Tanara Cloud</source>
        <translation>Tanara Cloud</translation>
    </message>
    <message>
        <source>Nincs kulcskezelés: az átírás és az összefoglaló a fiókodon fut.</source>
        <translation>No key handling: transcription and summaries run on your account.</translation>
    </message>
    <message>
        <source>hamarosan</source>
        <translation>coming soon</translation>
    </message>
    <message>
        <source>A beállítás a Tanara Cloudot választja, ami még nem érhető el. Válassz saját kulcsos szolgáltatót.</source>
        <translation>The setting selects Tanara Cloud, which is not available yet. Pick a provider with your own key.</translation>
    </message>
    <message>
        <source>Átírás</source>
        <translation>Transcription</translation>
    </message>
    <message>
        <source>beszédből szöveg (STT)</source>
        <translation>speech to text (STT)</translation>
    </message>
    <message>
        <source>Összefoglaló</source>
        <translation>Summary</translation>
    </message>
    <message>
        <source>nyelvi modell (LLM)</source>
        <translation>language model (LLM)</translation>
    </message>
    <message>
        <source>Címkejavaslatok</source>
        <translation>Tag suggestions</translation>
    </message>
    <message>
        <source>Címkék javaslása</source>
        <translation>Suggest tags</translation>
    </message>
    <message>
        <source>Hasonló korábbi megbeszélések és együtt járó címkék alapján. Sosem kerül fel magától.</source>
        <translation>Based on similar earlier meetings and tags that go together. Never added automatically.</translation>
    </message>
    <message>
        <source>A nyelvi modell is javasoljon az összefoglaló után</source>
        <translation>Let the language model suggest too, after the summary</translation>
    </message>
    <message>
        <source>Meglévő címkéket, és legfeljebb 2 új címke ötletét.</source>
        <translation>Existing tags, plus at most 2 new tag ideas.</translation>
    </message>
    <message>
        <source>Elutasított javaslatok: %1</source>
        <translation>Dismissed suggestions: %1</translation>
    </message>
    <message>
        <source>Visszaállítás</source>
        <translation>Restore</translation>
    </message>
    <message>
        <source>Az elutasított javaslatokat újra felajánlhatjuk</source>
        <translation>Dismissed suggestions can be offered again</translation>
    </message>
</context>
<context>
    <name>SettingsSpeakersPage</name>
    <message>
        <source>Törlöd a(z) %1 modellt?</source>
        <translation>Delete the %1 model?</translation>
    </message>
    <message>
        <source>A letöltött modellfájl törlődik a gépről. A vele készült hanglenyomatok megmaradnak, és ha később újra letöltöd, ismét használhatók.</source>
        <translation>The downloaded model file is deleted from this computer. Voiceprints made with it are kept, and they can be used again if you download it later.</translation>
    </message>
    <message>
        <source>Amíg nincs meg, a felismerés a többi bekapcsolt modellel megy (ha egy sincs, a CAM++ tartalékkal).</source>
        <translation>Until it is available, recognition runs with the other enabled models (with the CAM++ fallback if there are none).</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Törlés</source>
        <translation>Delete</translation>
    </message>
    <message>
        <source>Te</source>
        <translation>You</translation>
    </message>
    <message>
        <source>Saját neved</source>
        <translation>Your name</translation>
    </message>
    <message>
        <source>Így jelensz meg az átiratokban; a saját hanglenyomatod ehhez a névhez tartozik.</source>
        <translation>This is how you appear in transcripts; your own voiceprint belongs to this name.</translation>
    </message>
    <message>
        <source>Személyek kezelése</source>
        <translation>Manage people</translation>
    </message>
    <message>
        <source>Hangmodellek</source>
        <translation>Voice models</translation>
    </message>
    <message>
        <source>Ezekkel ismeri fel a Tanara, ki beszél. A modellek a gépeden futnak; a felvétel nem megy sehova.</source>
        <translation>Tanara uses these to recognize who is speaking. The models run on your machine; the recording goes nowhere.</translation>
    </message>
    <message>
        <source>A hangelemzés ki van kapcsolva</source>
        <translation>Voice analysis is turned off</translation>
    </message>
    <message>
        <source>Nincs letöltött hangmodell. Az átirat így is szerkeszthető, de a beszélőket nem ismerem fel hangra, és hanglenyomat sem készül.</source>
        <translation>No voice model is downloaded. The transcript can still be edited, but speakers are not recognized by voice and no voiceprints are created.</translation>
    </message>
    <message>
        <source>WeSpeaker letöltése (%1)</source>
        <translation>Download WeSpeaker (%1)</translation>
    </message>
    <message>
        <source>Ajánlott: WeSpeaker egyedül; a CAM++ tartalék.</source>
        <translation>Recommended: WeSpeaker alone; CAM++ is the fallback.</translation>
    </message>
    <message>
        <source>Kézzel másoltál be modellfájlt?</source>
        <translation>Did you copy a model file in manually?</translation>
    </message>
    <message>
        <source>Újraolvasás</source>
        <translation>Rescan</translation>
    </message>
    <message>
        <source>A modellek mappájának újraolvasása és a fájlok ellenőrzése</source>
        <translation>Rescan the models folder and verify the files</translation>
    </message>
    <message>
        <source>letöltés… %1</source>
        <translation>downloading… %1</translation>
    </message>
    <message>
        <source>Egy hangmodell sincs bekapcsolva, ezért a hangelemzés ki van kapcsolva.</source>
        <translation>No voice model is turned on, so voice analysis is turned off.</translation>
    </message>
    <message>
        <source>Most a CAM++ tartalék fut, mert a WeSpeaker nincs letöltve.</source>
        <translation>The CAM++ fallback is running now because WeSpeaker is not downloaded.</translation>
    </message>
    <message>
        <source>Most a CAM++ tartalék fut, mert a WeSpeaker nincs bekapcsolva.</source>
        <translation>The CAM++ fallback is running now because WeSpeaker is not enabled.</translation>
    </message>
    <message>
        <source>Letöltés (%1)</source>
        <translation>Download (%1)</translation>
    </message>
    <message>
        <source>Nincs internetkapcsolat: a letöltés most nem indul. A már letöltött modellek működnek.</source>
        <translation>No internet connection: the download will not start now. Models already downloaded still work.</translation>
    </message>
    <message>
        <source>Személyek</source>
        <translation>People</translation>
    </message>
    <message>
        <source>Melyik minták?</source>
        <translation>Which samples?</translation>
    </message>
    <message>
        <source>Melyik minták? (%1)</source>
        <translation>Which samples? (%1)</translation>
    </message>
    <message>
        <source>hiányzik: %1</source>
        <translation>missing: %1</translation>
    </message>
    <message>
        <source>Ezekhez a mintákhoz már nincs meg a felvétel, ezért csak a régi modellel számolnak. A Személyek ablakban újra felveheted őket.</source>
        <translation>The recordings for these samples are gone, so they only count with the old model. You can add them again in the People window.</translation>
    </message>
    <message>
        <source>Közben dolgozhatsz; a felismerés a régi mintákkal is működik.</source>
        <translation>You can keep working; recognition also works with the old samples.</translation>
    </message>
    <message>
        <source>Több bekapcsolt modell együtt dönt; a lassabb modellek lassítják az elemzést.</source>
        <translation>Several turned-on models decide together; slower models slow down the analysis.</translation>
    </message>
</context>
<context>
    <name>SettingsStepper</name>
    <message>
        <source>Kevesebb</source>
        <translation>Less</translation>
    </message>
    <message>
        <source>Több</source>
        <translation>More</translation>
    </message>
</context>
<context>
    <name>SettingsSummaryPage</name>
    <message>
        <source>Összefoglaló nyelve</source>
        <translation>Summary language</translation>
    </message>
    <message>
        <source>pl. magyar</source>
        <translation>e.g. magyar</translation>
    </message>
    <message>
        <source>Bármilyen nyelvet beírhatsz; független a felület nyelvétől.</source>
        <translation>You can type any language; it is independent of the interface language.</translation>
    </message>
    <message>
        <source>Utasítások a modellnek</source>
        <translation>Instructions for the model</translation>
    </message>
    <message>
        <source>módosítva</source>
        <translation>modified</translation>
    </message>
    <message>
        <source>Alapértelmezett</source>
        <translation>Default</translation>
    </message>
    <message>
        <source>Visszaállítás a beépített utasításra</source>
        <translation>Restore the built-in instruction</translation>
    </message>
    <message>
        <source>A modellnek adott utasítás</source>
        <translation>The instruction given to the model</translation>
    </message>
    <message>
        <source>Az átirat és a megbeszélés kontextusa az utasítás után, külön üzenetben megy a modellnek.</source>
        <translation>The transcript and the meeting context are sent to the model after the instruction, in a separate message.</translation>
    </message>
    <message>
        <source>Kimeneti forma:</source>
        <translation>Output format:</translation>
    </message>
    <message>
        <source>Megtekintés</source>
        <translation>View</translation>
    </message>
</context>
<context>
    <name>SettingsVoiceModelCard</name>
    <message>
        <source>ajánlott</source>
        <translation>recommended</translation>
    </message>
    <message>
        <source>letöltve</source>
        <translation>downloaded</translation>
    </message>
    <message>
        <source>a csomagban</source>
        <translation>bundled</translation>
    </message>
    <message>
        <source>letöltés… %1</source>
        <translation>downloading… %1</translation>
    </message>
    <message>
        <source>ellenőrzés…</source>
        <translation>verifying…</translation>
    </message>
    <message>
        <source>hiba (méret nem egyezik)</source>
        <translation>error (size mismatch)</translation>
    </message>
    <message>
        <source>hiba</source>
        <translation>error</translation>
    </message>
    <message>
        <source>offline</source>
        <translation>offline</translation>
    </message>
    <message>
        <source>nincs letöltve</source>
        <translation>not downloaded</translation>
    </message>
    <message>
        <source>Bekapcsolva</source>
        <translation>On</translation>
    </message>
    <message>
        <source>%1 bekapcsolása</source>
        <translation>Turn on %1</translation>
    </message>
    <message>
        <source>Előbb töltsd le a modellt</source>
        <translation>Download the model first</translation>
    </message>
    <message>
        <source>%1 bekapcsolva</source>
        <translation>%1 turned on</translation>
    </message>
    <message>
        <source>A modellfájl műveletei</source>
        <translation>Model file actions</translation>
    </message>
    <message>
        <source>%1: a modellfájl műveletei</source>
        <translation>%1: model file actions</translation>
    </message>
    <message>
        <source>Ellenőrzés újra</source>
        <translation>Verify again</translation>
    </message>
    <message>
        <source>Törlés…</source>
        <translation>Delete…</translation>
    </message>
    <message>
        <source>Törlés… (letöltés közben nem)</source>
        <translation>Delete… (not while downloading)</translation>
    </message>
    <message>
        <source>Licenc: %1</source>
        <translation>License: %1</translation>
    </message>
    <message>
        <source>forrás:</source>
        <translation>source:</translation>
    </message>
    <message>
        <source>Letöltés (%1)</source>
        <translation>Download (%1)</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Újra</source>
        <translation>Retry</translation>
    </message>
    <message>
        <source>Letöltés újrapróbálása</source>
        <translation>Retry download</translation>
    </message>
</context>
<context>
    <name>SettingsWatcherPage</name>
    <message>
        <source>Hívásfigyelés</source>
        <translation>Call watching</translation>
    </message>
    <message>
        <source>A tálcán fut, és szól, ha egy hívásalkalmazás használni kezdi a mikrofont. A felvételt mindig te indítod.</source>
        <translation>It runs in the tray and tells you when a call app starts using the microphone. You always start the recording yourself.</translation>
    </message>
    <message>
        <source>Most: az észlelés ezen a gépen nem érhető el</source>
        <translation>Now: detection is not available on this computer</translation>
    </message>
    <message>
        <source>Most: &lt;b&gt;%1&lt;/b&gt; használja a mikrofont</source>
        <translation>Now: &lt;b&gt;%1&lt;/b&gt; is using the microphone</translation>
    </message>
    <message>
        <source>Most: egyik figyelt alkalmazás sem használja a mikrofont</source>
        <translation>Now: none of the watched apps is using the microphone</translation>
    </message>
    <message>
        <source>Induljon el a bejelentkezéskor</source>
        <translation>Start when I log in</translation>
    </message>
    <message>
        <source>Kérdezzen rá, ha véget ért a megbeszélés</source>
        <translation>Ask when the meeting has ended</translation>
    </message>
    <message>
        <source>Felvétel közben, ha a hívás véget ér vagy a sávokon csend van. Magától sosem állít le.</source>
        <translation>While recording, when the call ends or the tracks are silent. It never stops the recording by itself.</translation>
    </message>
    <message>
        <source>Csend után:</source>
        <translation>After silence of:</translation>
    </message>
    <message>
        <source>Csend után ennyi perccel kérdezzen</source>
        <translation>Ask after this many minutes of silence</translation>
    </message>
    <message>
        <source>kikapcsolva</source>
        <translation>off</translation>
    </message>
    <message numerus="yes">
        <source>%n perc</source>
        <translation>
            <numerusform>%n minute</numerusform>
            <numerusform>%n minutes</numerusform>
        </translation>
    </message>
    <message>
        <source>Figyelt alkalmazások</source>
        <translation>Watched apps</translation>
    </message>
    <message>
        <source>Ezekre jelez, ha mikrofont használnak. Folyamatnév vagy annak egy része.</source>
        <translation>It alerts you when these use the microphone. A process name or a part of it.</translation>
    </message>
    <message>
        <source>%1 eltávolítása</source>
        <translation>Remove %1</translation>
    </message>
    <message>
        <source>Most ez az alkalmazás használja a mikrofont</source>
        <translation>This app is using the microphone right now</translation>
    </message>
    <message>
        <source>Alkalmazás hozzáadása</source>
        <translation>Add app</translation>
    </message>
    <message>
        <source>Folyamatnév vagy annak egy része</source>
        <translation>Process name or a part of it</translation>
    </message>
    <message>
        <source>pl. signal</source>
        <translation>e.g. signal</translation>
    </message>
    <message>
        <source>Hozzáadás</source>
        <translation>Add</translation>
    </message>
    <message>
        <source>Most futó alkalmazások</source>
        <translation>Apps running now</translation>
    </message>
    <message>
        <source>Haladó</source>
        <translation>Advanced</translation>
    </message>
    <message numerus="yes">
        <source>ellenőrzés %n másodpercenként</source>
        <translation>
            <numerusform>checks every %n second</numerusform>
            <numerusform>checks every %n seconds</numerusform>
        </translation>
    </message>
    <message>
        <source>Ellenőrzés gyakorisága:</source>
        <translation>Check interval:</translation>
    </message>
    <message>
        <source>Ellenőrzés gyakorisága másodpercben</source>
        <translation>Check interval in seconds</translation>
    </message>
    <message numerus="yes">
        <source>%n mp</source>
        <translation>
            <numerusform>%n s</numerusform>
            <numerusform>%n s</numerusform>
        </translation>
    </message>
    <message>
        <source>Rövidebb idő: hamarabb szól, de többször néz körül.</source>
        <translation>A shorter interval: it tells you sooner, but looks around more often.</translation>
    </message>
</context>
<context>
    <name>SettingsWindow</name>
    <message>
        <source>Beállítások</source>
        <translation>Settings</translation>
    </message>
    <message>
        <source>Általános</source>
        <translation>General</translation>
    </message>
    <message>
        <source>Rögzítés</source>
        <translation>Recording</translation>
    </message>
    <message>
        <source>Hívásfigyelő</source>
        <translation>Call watcher</translation>
    </message>
    <message>
        <source>Szolgáltatások</source>
        <translation>Services</translation>
    </message>
    <message>
        <source>Beszélők</source>
        <translation>Speakers</translation>
    </message>
    <message>
        <source>Összefoglaló</source>
        <translation>Summary</translation>
    </message>
    <message>
        <source>Egy szolgáltató beállítása hiányzik vagy nem érhető el</source>
        <translation>A provider is not set up or cannot be reached</translation>
    </message>
    <message>
        <source>Elmentve</source>
        <translation>Saved</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Mentés</source>
        <translation>Save</translation>
    </message>
    <message>
        <source>Mentés és újraindítás</source>
        <translation>Save and restart</translation>
    </message>
    <message>
        <source>Mented a változásokat?</source>
        <translation>Save your changes?</translation>
    </message>
    <message numerus="yes">
        <source>%n nem mentett változásod van a beállításokban.</source>
        <translation>
            <numerusform>You have %n unsaved change in the settings.</numerusform>
            <numerusform>You have %n unsaved changes in the settings.</numerusform>
        </translation>
    </message>
    <message>
        <source>Elvetés</source>
        <translation>Discard</translation>
    </message>
    <message>
        <source>Visszaállítod az alapértelmezett utasítást?</source>
        <translation>Restore the default instruction?</translation>
    </message>
    <message>
        <source>A saját szöveged elvész, és ennél a lapnál újra a beépített utasítás lesz érvényben. A változás a mentéssel lép életbe.</source>
        <translation>Your own text will be lost, and the built-in instruction will apply to this tab again. The change takes effect when you save.</translation>
    </message>
    <message>
        <source>Visszaállítás</source>
        <translation>Restore</translation>
    </message>
    <message>
        <source>Visszaállítod az elutasított javaslatokat?</source>
        <translation>Restore dismissed suggestions?</translation>
    </message>
    <message numerus="yes">
        <source>%n elutasított javaslatot újra felajánlhatunk a megbeszéléseknél. A felrakott címkékhez nem nyúlunk. A változás azonnal érvényes.</source>
        <translation>
            <numerusform>%n dismissed suggestion can be offered again on meetings. Tags already added stay as they are. The change takes effect immediately.</numerusform>
            <numerusform>%n dismissed suggestions can be offered again on meetings. Tags already added stay as they are. The change takes effect immediately.</numerusform>
        </translation>
    </message>
    <message>
        <source>Kimeneti forma</source>
        <translation>Output format</translation>
    </message>
    <message>
        <source>A modellnek ezt a JSON-objektumot kell visszaadnia; a Tanara ebből építi fel az összefoglalót. A forma nem szerkeszthető — ha az utasításod mást kér, az összefoglaló üres maradhat.</source>
        <translation>The model has to return this JSON object; Tanara builds the summary from it. The format cannot be edited — if your instruction asks for something else, the summary may stay empty.</translation>
    </message>
    <message>
        <source>A modellnek ilyen szerkezetű szöveget kell visszaadnia; a Tanara ebből olvassa ki a témákat, döntéseket és teendőket. A forma nem szerkeszthető — a szakaszcímeknek pontosan így kell szerepelniük.</source>
        <translation>The model has to return text with this structure; Tanara reads the topics, decisions and action items from it. The format cannot be edited — the section headings must appear exactly like this.</translation>
    </message>
    <message>
        <source>A kimeneti forma leírása</source>
        <translation>Description of the output format</translation>
    </message>
    <message>
        <source>Bezárás</source>
        <translation>Close</translation>
    </message>
</context>
<context>
    <name>ShellDialogs</name>
    <message>
        <source>Újra-átírod a megbeszélést?</source>
        <translation>Re-transcribe the meeting?</translation>
    </message>
    <message>
        <source>A mostani átirat maradjon meg másolatként</source>
        <translation>Keep the current transcript as a copy</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Újra-átírás</source>
        <translation>Re-transcribe</translation>
    </message>
    <message>
        <source>Törlöd a megbeszélést?</source>
        <translation>Delete the meeting?</translation>
    </message>
    <message>
        <source>„%1” felvétele (a hangsávok), az átirata és az összefoglalója véglegesen törlődik a gépedről. Ez nem vonható vissza.</source>
        <translation>The recording (audio tracks), transcript and summary of &quot;%1&quot; will be permanently deleted from your computer. This cannot be undone.</translation>
    </message>
    <message>
        <source>Törlés</source>
        <translation>Delete</translation>
    </message>
    <message>
        <source>Éppen felvétel megy</source>
        <translation>A recording is in progress</translation>
    </message>
    <message>
        <source>Ha most kilépsz, a felvétel megszakadna. Folytathatod a háttérben (a felvevő ablaka nyitva marad, a főablak a felvétel végén visszajön), vagy leállíthatod a felvételt, és utána lépünk ki.</source>
        <translation>If you quit now, the recording would be interrupted. You can continue in the background (the recorder window stays open and the main window returns when the recording ends), or stop the recording and quit afterwards.</translation>
    </message>
    <message>
        <source>Leállítom és kilépek</source>
        <translation>Stop and quit</translation>
    </message>
    <message>
        <source>Háttérben folytatom</source>
        <translation>Continue in the background</translation>
    </message>
    <message>
        <source>Vége a meetingnek?</source>
        <translation>Is the meeting over?</translation>
    </message>
    <message>
        <source>Leállítsam a rögzítést? Ha nem válaszolsz, a felvétel megy tovább.</source>
        <translation>Stop the recording? If you don&apos;t answer, the recording continues.</translation>
    </message>
    <message>
        <source>Folytatom a felvételt</source>
        <translation>Keep recording</translation>
    </message>
    <message>
        <source>Leállítás</source>
        <translation>Stop</translation>
    </message>
    <message>
        <source>Rendben</source>
        <translation>OK</translation>
    </message>
</context>
<context>
    <name>ShellFormat</name>
    <message>
        <source>&lt;1 p</source>
        <translation>&lt;1 min</translation>
    </message>
    <message>
        <source>%1 ó %2 p</source>
        <translation>%1 h %2 min</translation>
    </message>
    <message>
        <source>%1 p</source>
        <translation>%1 min</translation>
    </message>
</context>
<context>
    <name>ShellImportDialog</name>
    <message>
        <source>Importálás folyamatban</source>
        <translation>Import in progress</translation>
    </message>
    <message>
        <source>Hangfájl importálása</source>
        <translation>Import audio file</translation>
    </message>
    <message>
        <source>Közben nyugodtan dolgozhatsz tovább: az importálás a háttérben is megy, a végén az új megbeszélés megjelenik a könyvtárban. Az eredeti fájlokhoz nem nyúlunk.</source>
        <translation>Feel free to keep working: the import continues in the background, and the new meeting appears in the library when it is done. The original files are not touched.</translation>
    </message>
    <message>
        <source>Az importálás nem sikerült</source>
        <translation>Import failed</translation>
    </message>
    <message>
        <source>Húzd ide a hang- vagy videófájlokat</source>
        <translation>Drop audio or video files here</translation>
    </message>
    <message>
        <source>Fájlok kiválasztása…</source>
        <translation>Choose files…</translation>
    </message>
    <message>
        <source>Adatok beolvasása…</source>
        <translation>Reading file details…</translation>
    </message>
    <message>
        <source>Kivétel a listából</source>
        <translation>Remove from the list</translation>
    </message>
    <message>
        <source>Csatornánként külön sávra</source>
        <translation>Split channels into separate tracks</translation>
    </message>
    <message>
        <source>Fájl hozzáadása…</source>
        <translation>Add file…</translation>
    </message>
    <message>
        <source>Több fájl egyszerre indul: mindegyik külön sáv, közös kezdettel.</source>
        <translation>Multiple files start together: each is a separate track with a shared start.</translation>
    </message>
    <message>
        <source>Cím</source>
        <translation>Title</translation>
    </message>
    <message>
        <source>A megbeszélés címe</source>
        <translation>Meeting title</translation>
    </message>
    <message>
        <source>Mikor készült?</source>
        <translation>When was it recorded?</translation>
    </message>
    <message>
        <source>Címkék</source>
        <translation>Tags</translation>
    </message>
    <message>
        <source>Javasolt:</source>
        <translation>Suggested:</translation>
    </message>
    <message>
        <source>Ez az én mikrofonom felvétele</source>
        <translation>This is a recording of my microphone</translation>
    </message>
    <message>
        <source>Melyik sáv a te mikrofonod?</source>
        <translation>Which track is your microphone?</translation>
    </message>
    <message>
        <source>Egyik sem</source>
        <translation>None</translation>
    </message>
    <message>
        <source>Elhagyható. A megjelölt sáv a te neveden szerepel majd; a többi sávon a beszélőket az átírás választja szét.</source>
        <translation>Optional. The marked track will appear under your name; on the other tracks, transcription separates the speakers.</translation>
    </message>
    <message>
        <source>Az eredeti fájlok érintetlenek maradnak. Az átírást utána te indítod.</source>
        <translation>The original files are left untouched. You start transcription afterwards.</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Importálás</source>
        <translation>Import</translation>
    </message>
    <message>
        <source>Megszakítás…</source>
        <translation>Cancelling…</translation>
    </message>
    <message>
        <source>Megszakítás</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Háttérben folytatom</source>
        <translation>Continue in the background</translation>
    </message>
</context>
<context>
    <name>ShellImportStrip</name>
    <message>
        <source>Importálás: %1</source>
        <translation>Importing: %1</translation>
    </message>
    <message>
        <source>Importálás megszakítása</source>
        <translation>Cancel import</translation>
    </message>
</context>
<context>
    <name>ShellToast</name>
    <message>
        <source>Hibaazonosító: %1</source>
        <translation>Error ID: %1</translation>
    </message>
    <message>
        <source>Másolás</source>
        <translation>Copy</translation>
    </message>
    <message>
        <source>Napló a weben</source>
        <translation>Log on the web</translation>
    </message>
    <message>
        <source>Megnyitás mappában</source>
        <translation>Show in folder</translation>
    </message>
    <message>
        <source>Visszavonás</source>
        <translation>Undo</translation>
    </message>
    <message>
        <source>Bezárás</source>
        <translation>Close</translation>
    </message>
</context>
<context>
    <name>SourceCard</name>
    <message>
        <source>Leírás</source>
        <translation>Description</translation>
    </message>
    <message>
        <source>kézzel megadva</source>
        <translation>entered manually</translation>
    </message>
    <message>
        <source>Szerkesztés</source>
        <translation>Edit</translation>
    </message>
    <message>
        <source>Nincs hozzá naptár-esemény</source>
        <translation>No calendar event attached</translation>
    </message>
    <message>
        <source>Egy rövid leírás vagy egy összekapcsolt esemény segít a címkéknél, a résztvevőknél és az átírásnál is. Nem kötelező.</source>
        <translation>A short description or a linked event helps with tags, participants and transcription. Optional.</translation>
    </message>
    <message>
        <source>Leírás megadása</source>
        <translation>Add description</translation>
    </message>
    <message>
        <source>Naptár összekapcsolása</source>
        <translation>Link calendar</translation>
    </message>
    <message>
        <source>Hamarosan: Google Naptár és Outlook. Addig a leírás segít.</source>
        <translation>Coming soon: Google Calendar and Outlook. Until then, the description helps.</translation>
    </message>
    <message>
        <source>Miről szól? Téma, résztvevők, szakszavak, ismert félrehallások („A „…” helyesen: …”).</source>
        <translation>What is it about? Topic, participants, jargon, known mishearings (&quot;&apos;…&apos; should be: …&quot;).</translation>
    </message>
    <message>
        <source>Az átírás és az összefoglaló is felhasználja. Ctrl+Enter: mentés · Esc: mégse</source>
        <translation>Used by transcription and the summary. Ctrl+Enter: save · Esc: cancel</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Mentés</source>
        <translation>Save</translation>
    </message>
    <message>
        <source>Sablon hasonló megbeszélésből:</source>
        <translation>Template from a similar meeting:</translation>
    </message>
    <message>
        <source>Sablon: %1</source>
        <translation>Template: %1</translation>
    </message>
    <message>
        <source>Észlelt hívás: %1</source>
        <translation>Detected call: %1</translation>
    </message>
</context>
<context>
    <name>SourcePopover</name>
    <message>
        <source>Jelezted, hogy ez nem így hangzott el. Újrageneráláskor figyelembe veszem.</source>
        <translation>You flagged that this was not said this way. It will be taken into account when regenerating.</translation>
    </message>
    <message>
        <source>Honnan jön ez?</source>
        <translation>Where does this come from?</translation>
    </message>
    <message numerus="yes">
        <source>%n helyen hangzott el</source>
        <translation>
            <numerusform>Said in %n place</numerusform>
            <numerusform>Said in %n places</numerusform>
        </translation>
    </message>
    <message>
        <source>A forrásban azóta más a beszélő: %1</source>
        <translation>In the source the speaker is different now: %1</translation>
    </message>
    <message>
        <source>Meghallgatom</source>
        <translation>Listen</translation>
    </message>
    <message>
        <source>Az átirat sora nem található (újra-átírás után a hivatkozás elavulhat).</source>
        <translation>The transcript line was not found (the link may become outdated after re-transcription).</translation>
    </message>
    <message>
        <source>Ugrás az átiratba</source>
        <translation>Jump to transcript</translation>
    </message>
    <message>
        <source>Nem így hangzott el?</source>
        <translation>Not said like this?</translation>
    </message>
    <message>
        <source>Jelezve</source>
        <translation>Flagged</translation>
    </message>
    <message>
        <source>Jelzem</source>
        <translation>Flag</translation>
    </message>
</context>
<context>
    <name>SpeakerRailHeader</name>
    <message numerus="yes">
        <source>%n megszólalás</source>
        <translation>
            <numerusform>%n utterance</numerusform>
            <numerusform>%n utterances</numerusform>
        </translation>
    </message>
    <message>
        <source>%1-es billentyű</source>
        <translation>key %1</translation>
    </message>
    <message>
        <source>Van hanglenyomata.</source>
        <translation>Has a voiceprint.</translation>
    </message>
    <message>
        <source>Névtelen beszélő.</source>
        <translation>Unnamed speaker.</translation>
    </message>
    <message>
        <source>Nincs hanglenyomata.</source>
        <translation>No voiceprint.</translation>
    </message>
    <message>
        <source>A mikrofonon beszél.</source>
        <translation>Speaks on the microphone.</translation>
    </message>
    <message>
        <source>A hívás hangján beszél.</source>
        <translation>Speaks on the call audio.</translation>
    </message>
    <message>
        <source>Kattintásra: miért ő, és hol javítható</source>
        <translation>Click to see why them and where to fix it</translation>
    </message>
    <message numerus="yes">
        <source>%n keveset beszélő résztvevő — kattintásra külön oszlopot kapnak</source>
        <translation>
            <numerusform>%n participant who spoke little — click to give them their own column</numerusform>
            <numerusform>%n participants who spoke little — click to give each their own column</numerusform>
        </translation>
    </message>
    <message>
        <source>A keveset beszélők összecsukása</source>
        <translation>Collapse participants who spoke little</translation>
    </message>
    <message>
        <source>Résztvevő hozzáadása</source>
        <translation>Add participant</translation>
    </message>
</context>
<context>
    <name>SpeakerWhyPopover</name>
    <message>
        <source>Meghallgatás: egy jellemző, hosszabb megszólalás ettől a beszélőtől</source>
        <translation>Listen: a typical, longer utterance from this speaker</translation>
    </message>
    <message>
        <source>Meghallgatás</source>
        <translation>Listen</translation>
    </message>
    <message numerus="yes">
        <source>%n sor ezen a megbeszélésen</source>
        <translation>
            <numerusform>%n line in this meeting</numerusform>
            <numerusform>%n lines in this meeting</numerusform>
        </translation>
    </message>
    <message>
        <source>%1 · azonosítva</source>
        <translation>%1 · identified</translation>
    </message>
    <message>
        <source>%1 · kézzel elnevezve</source>
        <translation>%1 · named manually</translation>
    </message>
    <message>
        <source>%1 · névtelen beszélő</source>
        <translation>%1 · unnamed speaker</translation>
    </message>
    <message>
        <source>MIÉRT Ő?</source>
        <translation>WHY THEM?</translation>
    </message>
    <message>
        <source>erre támaszkodtunk</source>
        <translation>what we relied on</translation>
    </message>
    <message>
        <source>MELYIK SÁVON BESZÉL?</source>
        <translation>WHICH TRACK DO THEY SPEAK ON?</translation>
    </message>
    <message>
        <source>Címke:</source>
        <translation>Tag:</translation>
    </message>
    <message>
        <source>még nincs közös címkéje a megbeszéléssel</source>
        <translation>has no tag in common with the meeting yet</translation>
    </message>
    <message>
        <source>Címkéi</source>
        <translation>Tags</translation>
    </message>
    <message>
        <source>%1 címkéje lett: #%2</source>
        <translation>%1 now has the tag #%2</translation>
    </message>
    <message>
        <source>+ #%1 ráteszem</source>
        <translation>+ apply #%1</translation>
    </message>
    <message>
        <source>ezen a megbeszélésen · több is jelölhető</source>
        <translation>in this meeting · you can select several</translation>
    </message>
    <message>
        <source>%1 nem ezen a sávon beszél (ha egy sáv sem marad, a gép újra maga dönt) — Ctrl+Z</source>
        <translation>%1 does not speak on this track (if no track is left, the computer decides again on its own) — Ctrl+Z</translation>
    </message>
    <message>
        <source>%1 ezen a sávon (is) beszél — a sáv-ellentmondás ehhez igazodik (Ctrl+Z)</source>
        <translation>%1 speaks on this track (too) — the track conflict follows this (Ctrl+Z)</translation>
    </message>
    <message>
        <source>NEM Ő? VALÓJÁBAN…</source>
        <translation>NOT THEM? ACTUALLY…</translation>
    </message>
    <message>
        <source>Új személy…</source>
        <translation>New person…</translation>
    </message>
    <message numerus="yes">
        <source>a %n sor egy új névhez kerül</source>
        <translation>
            <numerusform>the %n line will go to a new name</numerusform>
            <numerusform>the %n lines will go to a new name</numerusform>
        </translation>
    </message>
    <message>
        <source>Összevonás egy másik beszélővel…</source>
        <translation>Merge with another speaker…</translation>
    </message>
    <message numerus="yes">
        <source>%n sor</source>
        <translation>
            <numerusform>%n line</numerusform>
            <numerusform>%n lines</numerusform>
        </translation>
    </message>
    <message>
        <source>Átnézés másik beszélővel…</source>
        <translation>Review with another speaker…</translation>
    </message>
    <message>
        <source>ha két hang sorai összekeveredtek</source>
        <translation>if the lines of two voices got mixed up</translation>
    </message>
    <message>
        <source>Ő nem volt ott</source>
        <translation>They weren&apos;t there</translation>
    </message>
    <message>
        <source>a „Ki volt ott?” is frissül</source>
        <translation>&quot;Who was there?&quot; is updated too</translation>
    </message>
    <message>
        <source>Üres oszlop eltávolítása</source>
        <translation>Remove empty column</translation>
    </message>
    <message>
        <source>Ha másé, a sorai kerüljenek ki %1 hanglenyomatából (téves felismerés)</source>
        <translation>If it&apos;s someone else&apos;s, remove its lines from %1&apos;s voiceprint (wrong match)</translation>
    </message>
    <message>
        <source>Ctrl+Z visszavonja</source>
        <translation>Ctrl+Z undoes it</translation>
    </message>
    <message numerus="yes">
        <source>A teljes beszélőre vonatkozik: mind az %n sor · Ctrl+Z visszavonja</source>
        <translation>
            <numerusform>Applies to the whole speaker: all %n line · Ctrl+Z undoes it</numerusform>
            <numerusform>Applies to the whole speaker: all %n lines · Ctrl+Z undoes it</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>A teljes beszélőre vonatkozik: mind a %n sor · Ctrl+Z visszavonja</source>
        <translation>
            <numerusform>Applies to the whole speaker: all %n line · Ctrl+Z undoes it</numerusform>
            <numerusform>Applies to the whole speaker: all %n lines · Ctrl+Z undoes it</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>SummaryDocView</name>
    <message numerus="yes">
        <source>Az összefoglaló óta %n beszélőt javítottál, ezért a felelősök és a résztvevők elavultak lehetnek.</source>
        <translation>
            <numerusform>You have corrected %n speaker since the summary, so owners and participants may be outdated.</numerusform>
            <numerusform>You have corrected %n speakers since the summary, so owners and participants may be outdated.</numerusform>
        </translation>
    </message>
    <message>
        <source>Az összefoglaló óta változtak a beszélők, ezért a felelősök és a résztvevők elavultak lehetnek.</source>
        <translation>The speakers have changed since the summary, so owners and participants may be outdated.</translation>
    </message>
    <message>
        <source>Témák újraelemzése</source>
        <translation>Re-analyze topics</translation>
    </message>
    <message>
        <source>Frissítés</source>
        <translation>Update</translation>
    </message>
    <message>
        <source>Rendben így</source>
        <translation>Keep as is</translation>
    </message>
    <message>
        <source>Vezetői összefoglaló</source>
        <translation>Executive summary</translation>
    </message>
    <message numerus="yes">
        <source>Memó · %n szakasz</source>
        <translation>
            <numerusform>Memo · %n section</numerusform>
            <numerusform>Memo · %n sections</numerusform>
        </translation>
    </message>
    <message>
        <source>Memó</source>
        <translation>Memo</translation>
    </message>
    <message>
        <source>Döntések</source>
        <translation>Decisions</translation>
    </message>
    <message>
        <source>Ugrás ide: %1</source>
        <translation>Jump to %1</translation>
    </message>
    <message>
        <source>A vezetői összefoglaló a vágólapra került.</source>
        <translation>The executive summary was copied to the clipboard.</translation>
    </message>
    <message>
        <source>A memó a vágólapra került.</source>
        <translation>The memo was copied to the clipboard.</translation>
    </message>
    <message>
        <source>Nyitott kérdések</source>
        <translation>Open questions</translation>
    </message>
    <message>
        <source>Teendők</source>
        <translation>Action items</translation>
    </message>
    <message>
        <source>Témák</source>
        <translation>Topics</translation>
    </message>
    <message>
        <source>Ehhez az összefoglalóhoz nincs memó</source>
        <translation>This summary has no memo</translation>
    </message>
    <message>
        <source>Ez az összefoglaló még a memó bevezetése előtt készült. Újragenerálva a vezetői összefoglaló mellé időrendi, szakaszonkénti memó is készül.</source>
        <translation>This summary was made before memos existed. Regenerate it to get a chronological memo, section by section, next to the executive summary.</translation>
    </message>
    <message>
        <source>Tartalom</source>
        <translation>Contents</translation>
    </message>
    <message>
        <source>Egyéb</source>
        <translation>Other</translation>
    </message>
    <message>
        <source>Résztvevők</source>
        <translation>Participants</translation>
    </message>
    <message>
        <source>Megjegyzés a megbeszéléshez</source>
        <translation>Note on the meeting</translation>
    </message>
    <message>
        <source>Nincs megjegyzés. Ide írhatod a neveket, szakszavakat és a félrehallott szavak helyes alakját.</source>
        <translation>No note. Write names, jargon and the correct form of misheard words here.</translation>
    </message>
    <message>
        <source>Észlelt hívás: %1</source>
        <translation>Detected call: %1</translation>
    </message>
    <message>
        <source>Nevek, szakszavak, ismert félrehallások (A „…” helyesen: …). Az újragenerált összefoglaló és egy újra-átírás is ezt kapja.</source>
        <translation>Names, jargon, known mishearings (“…” is really …). A regenerated summary and a re-transcription both get this note.</translation>
    </message>
    <message>
        <source>Témánkénti elemzés</source>
        <translation>Topic-by-topic analysis</translation>
    </message>
    <message>
        <source>Újragenerálás</source>
        <translation>Regenerate</translation>
    </message>
    <message>
        <source>Másolás</source>
        <translation>Copy</translation>
    </message>
    <message>
        <source>Az összefoglaló a vágólapra került.</source>
        <translation>Summary copied to the clipboard.</translation>
    </message>
    <message>
        <source>A megjegyzés azóta változott; újrageneráláskor már az új számít.</source>
        <translation>The note has changed since; regenerating uses the new one.</translation>
    </message>
    <message>
        <source>Mindkettő</source>
        <translation>Both</translation>
    </message>
    <message>
        <source>Megnyitás mappában</source>
        <translation>Show in folder</translation>
    </message>
</context>
<context>
    <name>SummaryEmptyView</name>
    <message>
        <source>Még nincs összefoglaló</source>
        <translation>No summary yet</translation>
    </message>
    <message>
        <source>Kétféle összefoglaló készülhet:</source>
        <translation>There are two kinds of summary:</translation>
    </message>
    <message>
        <source>Megszakítás folyamatban; a kész részek jegyzetei megmaradnak, a következő futás onnan folytatja.</source>
        <translation>Cancelling; the notes of the finished parts are kept, and the next run continues from them.</translation>
    </message>
    <message>
        <source>Megszakítás folyamatban; az átirat érintetlen marad.</source>
        <translation>Cancelling; the transcript is left untouched.</translation>
    </message>
    <message>
        <source>Közben nyugodtan dolgozz tovább; szólunk, ha kész.</source>
        <translation>Feel free to keep working; we&apos;ll let you know when it&apos;s done.</translation>
    </message>
    <message>
        <source>Megszakítás</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Megszakítás…</source>
        <translation>Cancelling…</translation>
    </message>
    <message>
        <source>Gyors összefoglaló</source>
        <translation>Quick summary</translation>
    </message>
    <message>
        <source>Vezetői összefoglaló döntésekkel, nyitott kérdésekkel és teendőkkel, mellette időrendi memó a megbeszélés menetéről.</source>
        <translation>An executive summary with decisions, open questions and action items, plus a chronological memo of how the meeting went.</translation>
    </message>
    <message>
        <source>rövid megbeszélésnél kb. 1 perc; hosszabbnál részenként halad</source>
        <translation>about 1 minute for a short meeting; longer ones go part by part</translation>
    </message>
    <message>
        <source>Összefoglaló készítése</source>
        <translation>Create summary</translation>
    </message>
    <message>
        <source>Témánkénti elemzés</source>
        <translation>Topic-by-topic analysis</translation>
    </message>
    <message>
        <source>A modell témákat javasol, te átírod, törlöd vagy kiegészíted őket, aztán témánként részletes elemzés készül.</source>
        <translation>The model suggests topics; you edit, delete or add to them, then a detailed analysis is created for each topic.</translation>
    </message>
    <message>
        <source>a téma-lista már megvan; onnan folytatható, ahol abbamaradt</source>
        <translation>the topic list already exists; continue where you left off</translation>
    </message>
    <message>
        <source>2 lépés, témánként 1–2 perc</source>
        <translation>2 steps, 1–2 minutes per topic</translation>
    </message>
    <message>
        <source>Témák megnyitása</source>
        <translation>Open topics</translation>
    </message>
    <message>
        <source>Témák javaslása</source>
        <translation>Suggest topics</translation>
    </message>
    <message>
        <source>Megjegyzés a megbeszéléshez</source>
        <translation>Note on the meeting</translation>
    </message>
    <message>
        <source>Ide valók a nevek, szakszavak és az ismert félrehallások (például: A „…” helyesen: …). Az összefoglaló ezt megbízható kontextusként kapja, és egy újra-átírás is ezt küldi az átírónak.</source>
        <translation>Names, jargon and known mishearings go here (for example: “…” is really …). The summary takes this as reliable context, and a re-transcription sends it to the transcriber too.</translation>
    </message>
    <message>
        <source>Szolgáltató: %1 · a modell a Beállítások › Összefoglaló alatt cserélhető</source>
        <translation>Provider: %1 · change the model under Settings › Summary</translation>
    </message>
    <message>
        <source>Megnyitás</source>
        <translation>Open</translation>
    </message>
</context>
<context>
    <name>SummaryExecView</name>
    <message>
        <source>A felelősök és a résztvevők elavultak lehetnek.</source>
        <translation>The owners and participants may be outdated.</translation>
    </message>
    <message>
        <source>%1 és %2 forrásában más lett a beszélő; ezeket jelöltük.</source>
        <translation>The speaker changed in the sources of %1 and %2; we flagged them.</translation>
    </message>
    <message numerus="yes">
        <source>%n állítás</source>
        <translation>
            <numerusform>%n claim</numerusform>
            <numerusform>%n claims</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n teendő</source>
        <translation>
            <numerusform>%n action item</numerusform>
            <numerusform>%n action items</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n állítás forrásában más lett a beszélő; ezeket jelöltük.</source>
        <translation>
            <numerusform>The speaker changed in the source of %n claim; we flagged it.</numerusform>
            <numerusform>The speaker changed in the sources of %n claims; we flagged them.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n teendő forrásában más lett a beszélő; ezeket jelöltük.</source>
        <translation>
            <numerusform>The speaker changed in the source of %n action item; we flagged it.</numerusform>
            <numerusform>The speaker changed in the sources of %n action items; we flagged them.</numerusform>
        </translation>
    </message>
    <message>
        <source>Egyik állítás forrását sem érinti.</source>
        <translation>None of the claims&apos; sources are affected.</translation>
    </message>
    <message>
        <source>Nincs forrás: ehhez a modell nem jelölt időpontot.</source>
        <translation>No source: the model did not mark a time for this.</translation>
    </message>
    <message>
        <source>Honnan jön ez? (%1)</source>
        <translation>Where does this come from? (%1)</translation>
    </message>
    <message numerus="yes">
        <source>Az összefoglaló óta %n beszélőt javítottál.</source>
        <translation>
            <numerusform>You have corrected %n speaker since the summary.</numerusform>
            <numerusform>You have corrected %n speakers since the summary.</numerusform>
        </translation>
    </message>
    <message>
        <source>Az összefoglaló óta változtak a beszélők.</source>
        <translation>The speakers have changed since the summary.</translation>
    </message>
    <message>
        <source>Frissítés</source>
        <translation>Update</translation>
    </message>
    <message>
        <source>Rendben így</source>
        <translation>Keep as is</translation>
    </message>
    <message>
        <source>Döntések</source>
        <translation>Decisions</translation>
    </message>
    <message>
        <source>Nyitott kérdések</source>
        <translation>Open questions</translation>
    </message>
    <message>
        <source>Teendők</source>
        <translation>Action items</translation>
    </message>
    <message>
        <source>Résztvevők</source>
        <translation>Participants</translation>
    </message>
    <message>
        <source>A források a megerősített beszélőkre mutatnak. Ha az Átiratban javítasz, az érintett állítások jelölést kapnak.</source>
        <translation>The sources point to the confirmed speakers. If you correct the Transcript, the affected claims get flagged.</translation>
    </message>
    <message>
        <source>Megjegyzés a megbeszéléshez</source>
        <translation>Note on the meeting</translation>
    </message>
    <message>
        <source>Nincs megjegyzés. Ide írhatod a neveket, szakszavakat és a félrehallott szavak helyes alakját.</source>
        <translation>No note. Write names, jargon and the correct form of misheard words here.</translation>
    </message>
    <message>
        <source>Nevek, szakszavak, ismert félrehallások (A „…” helyesen: …). Az újragenerált összefoglaló és egy újra-átírás is ezt kapja.</source>
        <translation>Names, jargon, known mishearings (“…” is really …). A regenerated summary and a re-transcription both get this note.</translation>
    </message>
    <message>
        <source>A megjegyzés azóta változott; újrageneráláskor már az új számít.</source>
        <translation>The note has changed since; regenerating uses the new one.</translation>
    </message>
    <message>
        <source>Témánkénti elemzés</source>
        <translation>Topic-by-topic analysis</translation>
    </message>
    <message>
        <source>Megnyitás mappában</source>
        <translation>Show in folder</translation>
    </message>
</context>
<context>
    <name>SummaryMapRows</name>
    <message>
        <source>Szakaszok</source>
        <translation>Sections</translation>
    </message>
    <message>
        <source>Forrás</source>
        <translation>Source</translation>
    </message>
</context>
<context>
    <name>SummaryMemoView</name>
    <message>
        <source>Nincs forrás: ehhez a modell nem jelölt időpontot.</source>
        <translation>No source: the model did not mark a time for this.</translation>
    </message>
    <message>
        <source>Honnan jön ez? (%1)</source>
        <translation>Where does this come from? (%1)</translation>
    </message>
    <message numerus="yes">
        <source>Az összefoglaló óta %n beszélőt javítottál, ezért a memó beszélői elavultak lehetnek.</source>
        <translation>
            <numerusform>You have corrected %n speaker since the summary, so the speakers in the memo may be outdated.</numerusform>
            <numerusform>You have corrected %n speakers since the summary, so the speakers in the memo may be outdated.</numerusform>
        </translation>
    </message>
    <message>
        <source>Az összefoglaló óta változtak a beszélők, ezért a memó beszélői elavultak lehetnek.</source>
        <translation>The speakers have changed since the summary, so the speakers in the memo may be outdated.</translation>
    </message>
    <message>
        <source>Frissítés</source>
        <translation>Update</translation>
    </message>
    <message>
        <source>Rendben így</source>
        <translation>Keep as is</translation>
    </message>
    <message>
        <source>Egyéb</source>
        <translation>Other</translation>
    </message>
    <message>
        <source>Lejátszás innen: %1</source>
        <translation>Play from %1</translation>
    </message>
</context>
<context>
    <name>SummaryPipeline</name>
    <message>
        <source>Jegyzet</source>
        <translation>Notes</translation>
    </message>
    <message>
        <source>a modell válasza üres</source>
        <translation>the model response is empty</translation>
    </message>
    <message>
        <source>a válaszban nincs JSON-objektum (eleje: „%1”)</source>
        <translation>the response contains no JSON object (it starts with &quot;%1&quot;)</translation>
    </message>
    <message>
        <source>a válasz nem érvényes JSON (%1 a(z) %2. karakternél: „%3”)</source>
        <translation>the response is not valid JSON (%1 at character %2: &quot;%3&quot;)</translation>
    </message>
    <message>
        <source>a JSON-ban nincs egyik várt mező sem (execSummary, decisions, openQuestions, actionItems)</source>
        <translation>the JSON contains none of the expected fields (execSummary, decisions, openQuestions, actionItems)</translation>
    </message>
</context>
<context>
    <name>SummaryProgress</name>
    <message>
        <source>Összefoglalás egy lépésben</source>
        <translation>Summarising in one step</translation>
    </message>
    <message numerus="yes">
        <source>%n rész jegyzete egy korábbi, félbemaradt futásból megvan; ezek nem futnak újra.</source>
        <translation>
            <numerusform>The notes of %n part are kept from an earlier, unfinished run; it does not run again.</numerusform>
            <numerusform>The notes of %n parts are kept from an earlier, unfinished run; they do not run again.</numerusform>
        </translation>
    </message>
    <message>
        <source>%1 / %2 rész</source>
        <translation>%1 / %2 parts</translation>
    </message>
    <message>
        <source>Összefésülés</source>
        <translation>Merging</translation>
    </message>
    <message>
        <source>Jegyzetek készítése: %1 / %2 rész</source>
        <translation>Taking notes: %1 / %2 parts</translation>
    </message>
    <message>
        <source>Jegyzetek készítése</source>
        <translation>Taking notes</translation>
    </message>
</context>
<context>
    <name>SummaryStatusBanners</name>
    <message>
        <source>Folyamatban…</source>
        <translation>In progress…</translation>
    </message>
    <message>
        <source>Megszakítás…</source>
        <translation>Cancelling…</translation>
    </message>
    <message>
        <source>Megszakítás</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Az összefoglaló legutóbb nem készült el</source>
        <translation>The last summary attempt did not complete</translation>
    </message>
    <message>
        <source>A már elkészült részek jegyzetei megmaradtak; a folytatás csak a hiányzó részeket és az összefésülést futtatja.</source>
        <translation>The notes of the finished parts are kept; continuing runs only the missing parts and the merge.</translation>
    </message>
    <message>
        <source>Betöltés nagyobb kontextussal</source>
        <translation>Load with a larger context</translation>
    </message>
    <message>
        <source>Újra</source>
        <translation>Retry</translation>
    </message>
    <message>
        <source>Folytatás</source>
        <translation>Continue</translation>
    </message>
    <message>
        <source>Rendben</source>
        <translation>OK</translation>
    </message>
</context>
<context>
    <name>SummaryToolsRow</name>
    <message>
        <source>A vezetői összefoglaló a vágólapra került.</source>
        <translation>The executive summary was copied to the clipboard.</translation>
    </message>
    <message>
        <source>A memó a vágólapra került.</source>
        <translation>The memo was copied to the clipboard.</translation>
    </message>
    <message>
        <source>Az összefoglaló a vágólapra került.</source>
        <translation>Summary copied to the clipboard.</translation>
    </message>
    <message>
        <source>Vezetői összefoglaló</source>
        <translation>Executive summary</translation>
    </message>
    <message>
        <source>Memó</source>
        <translation>Memo</translation>
    </message>
    <message>
        <source>Források</source>
        <translation>Sources</translation>
    </message>
    <message>
        <source>Az idő-chipek és a forrás-jelölések elrejtése</source>
        <translation>Hide the time chips and source markers</translation>
    </message>
    <message>
        <source>Minden állítás mellett: hol hangzott el</source>
        <translation>Next to every claim: where it was said</translation>
    </message>
    <message>
        <source>Másolás</source>
        <translation>Copy</translation>
    </message>
    <message>
        <source>A memó másolása</source>
        <translation>Copy the memo</translation>
    </message>
    <message>
        <source>A vezetői összefoglaló másolása (források nélkül)</source>
        <translation>Copy the executive summary (without sources)</translation>
    </message>
    <message>
        <source>Újragenerálás</source>
        <translation>Regenerate</translation>
    </message>
</context>
<context>
    <name>SummaryTopicsView</name>
    <message>
        <source>Törlöd a témát?</source>
        <translation>Delete the topic?</translation>
    </message>
    <message>
        <source>„%1” elemzése kikerül az összegzésből. A többi téma eredménye megmarad.</source>
        <translation>The analysis of &quot;%1&quot; is removed from the overall summary. The results of the other topics are kept.</translation>
    </message>
    <message>
        <source>Téma törlése</source>
        <translation>Delete topic</translation>
    </message>
    <message>
        <source>Összefoglaló</source>
        <translation>Summary</translation>
    </message>
    <message>
        <source>Vissza</source>
        <translation>Back</translation>
    </message>
    <message>
        <source>Vissza az összefoglalóhoz</source>
        <translation>Back to the summary</translation>
    </message>
    <message>
        <source>Vissza a választáshoz</source>
        <translation>Back to the options</translation>
    </message>
    <message>
        <source>Témák</source>
        <translation>Topics</translation>
    </message>
    <message>
        <source>Téma hozzáadása</source>
        <translation>Add topic</translation>
    </message>
    <message>
        <source>Hiányzók elemzése</source>
        <translation>Analyze missing</translation>
    </message>
    <message>
        <source>Összegzés készítése</source>
        <translation>Create overall summary</translation>
    </message>
    <message>
        <source>Még nincs téma. Vegyél fel egyet kézzel, vagy térj vissza, és kérj javaslatot a modelltől.</source>
        <translation>No topics yet. Add one manually, or go back and ask the model for suggestions.</translation>
    </message>
    <message>
        <source>Az új téma címe</source>
        <translation>Title of the new topic</translation>
    </message>
    <message>
        <source>Rövid leírás: mire figyeljen az elemzés (elhagyható)</source>
        <translation>Short description: what the analysis should focus on (optional)</translation>
    </message>
    <message>
        <source>Az új téma leírása</source>
        <translation>Description of the new topic</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Hozzáadás</source>
        <translation>Add</translation>
    </message>
</context>
<context>
    <name>SweepDecisionBar</name>
    <message>
        <source>%1 mondta</source>
        <translation>%1 said it</translation>
    </message>
    <message>
        <source>Jó így</source>
        <translation>Looks right</translation>
    </message>
    <message>
        <source>Az alapértelmezés (Enter). Shift+Enter: ugyanez, de a hangja nem lesz minta</source>
        <translation>The default (Enter). Shift+Enter: the same, but its voice won&apos;t become a sample</translation>
    </message>
    <message>
        <source>Nem azonosítható</source>
        <translation>Unidentifiable</translation>
    </message>
    <message>
        <source>A név marad, de a sor hangját semmire nem használom</source>
        <translation>The name stays, but the line&apos;s voice is not used for anything</translation>
    </message>
    <message>
        <source>Más…</source>
        <translation>Other…</translation>
    </message>
    <message>
        <source>Később</source>
        <translation>Later</translation>
    </message>
    <message>
        <source>Most kihagyom; a szakasz végén visszanézhető</source>
        <translation>Skip for now; you can come back to it at the end of the section</translation>
    </message>
    <message>
        <source>A lejátszás itt megállt · Space: tovább döntés nélkül · ←: a sor eleje · Shift: nem minta</source>
        <translation>Playback stopped here · Space: continue without deciding · ←: line start · Shift: not a sample</translation>
    </message>
</context>
<context>
    <name>SweepSectionBar</name>
    <message>
        <source>Szakasz kész · %1</source>
        <translation>Section done · %1</translation>
    </message>
    <message>
        <source>eldöntve</source>
        <translation>decided</translation>
    </message>
    <message>
        <source>nem azonosítható</source>
        <translation>unidentifiable</translation>
    </message>
    <message>
        <source>később</source>
        <translation>later</translation>
    </message>
    <message>
        <source>a háttérből jött</source>
        <translation>from the background</translation>
    </message>
    <message>
        <source>Bezárás</source>
        <translation>Close</translation>
    </message>
    <message>
        <source>Új minták: %1</source>
        <translation>New samples: %1</translation>
    </message>
    <message numerus="yes">
        <source>A háttér-elemzés %n már elhagyott sort újra kétesnek jelölt; a soraid nem változtak.</source>
        <translation>
            <numerusform>The background analysis marked %n passed line uncertain again; your lines did not change.</numerusform>
            <numerusform>The background analysis marked %n passed lines uncertain again; your lines did not change.</numerusform>
        </translation>
    </message>
    <message>
        <source>Kész</source>
        <translation>Done</translation>
    </message>
    <message>
        <source>Következő szakasz (%1)</source>
        <translation>Next section (%1)</translation>
    </message>
    <message>
        <source>Visszanézem őket (%1)</source>
        <translation>Review them (%1)</translation>
    </message>
    <message>
        <source>Kihagyottak (%1)</source>
        <translation>Skipped (%1)</translation>
    </message>
</context>
<context>
    <name>TagChip</name>
    <message>
        <source>Javasolt címke: %1</source>
        <translation>Suggested tag: %1</translation>
    </message>
    <message>
        <source>Új címke javaslat: %1</source>
        <translation>New tag suggestion: %1</translation>
    </message>
    <message>
        <source>ÚJ</source>
        <translation>NEW</translation>
    </message>
</context>
<context>
    <name>TagField</name>
    <message>
        <source>Címke hozzáadása…</source>
        <translation>Add tag…</translation>
    </message>
    <message>
        <source>Címkék</source>
        <translation>Tags</translation>
    </message>
</context>
<context>
    <name>TagInput</name>
    <message>
        <source>Címke…</source>
        <translation>Tag…</translation>
    </message>
    <message>
        <source>Címke hozzáadása</source>
        <translation>Add tag</translation>
    </message>
</context>
<context>
    <name>TagInputList</name>
    <message>
        <source>Hasonló már van</source>
        <translation>Similar tag exists</translation>
    </message>
    <message>
        <source>Legutóbb használt</source>
        <translation>Recently used</translation>
    </message>
    <message>
        <source>Mégis új: „%1”</source>
        <translation>Create anyway: &quot;%1&quot;</translation>
    </message>
    <message>
        <source>Új címke: „%1”</source>
        <translation>New tag: &quot;%1&quot;</translation>
    </message>
    <message>
        <source>Enter hozzáad · Esc bezár</source>
        <translation>Enter adds · Esc closes</translation>
    </message>
    <message>
        <source>↑↓ választás · Enter hozzáadás · Esc bezárás</source>
        <translation>↑↓ select · Enter add · Esc close</translation>
    </message>
</context>
<context>
    <name>TagLearnBar</name>
    <message>
        <source>Igen</source>
        <translation>Yes</translation>
    </message>
    <message>
        <source>A címke rájuk kerül (Ctrl+Z: visszavonás)</source>
        <translation>The tag is applied to them (Ctrl+Z: undo)</translation>
    </message>
    <message>
        <source>Nem</source>
        <translation>No</translation>
    </message>
    <message>
        <source>Nem tartoznak ide: nem javasoljuk újra</source>
        <translation>They do not belong here: we will not suggest it again</translation>
    </message>
    <message>
        <source>Egyenként ▴</source>
        <translation>One by one ▴</translation>
    </message>
    <message>
        <source>Egyenként…</source>
        <translation>One by one…</translation>
    </message>
    <message>
        <source>Személyenként és címkénként külön döntés</source>
        <translation>A separate decision for each person and tag</translation>
    </message>
    <message>
        <source>Most nem (a javaslat a Személyek ablakban megmarad)</source>
        <translation>Not now (the suggestion stays in the People window)</translation>
    </message>
</context>
<context>
    <name>TagLearning</name>
    <message>
        <source>%1 és %2</source>
        <translation>%1 and %2</translation>
    </message>
    <message>
        <source>%1 új személyek. Rájuk teszem a %2 címkét?</source>
        <translation>%1 are new people. Shall I apply the %2 tag to them?</translation>
    </message>
    <message>
        <source>%1 új személy. Ráteszem a %2 címkét?</source>
        <translation>%1 is a new person. Shall I apply the %2 tag?</translation>
    </message>
    <message>
        <source>%1 másodszor van %2 megbeszélésen. Rájuk teszem a címkét?</source>
        <translation>%1 are in a %2 meeting for the second time. Shall I apply the tag to them?</translation>
    </message>
    <message>
        <source>%1 másodszor van %2 megbeszélésen. Ráteszem a címkét?</source>
        <translation>%1 is in a %2 meeting for the second time. Shall I apply the tag?</translation>
    </message>
    <message>
        <source>Címke a személyen: %1 · %2</source>
        <translation>Tag on the person: %1 · %2</translation>
    </message>
    <message>
        <source>Nem javasoljuk újra: %1 · %2</source>
        <translation>Will not be suggested again: %1 · %2</translation>
    </message>
    <message>
        <source>új személy</source>
        <translation>new person</translation>
    </message>
    <message>
        <source>másodszor</source>
        <translation>for the second time</translation>
    </message>
</context>
<context>
    <name>TagPeopleList</name>
    <message>
        <source>Személyek</source>
        <translation>People</translation>
    </message>
    <message>
        <source>Személy</source>
        <translation>Person</translation>
    </message>
    <message>
        <source>Személy hozzáadása ehhez a címkéhez</source>
        <translation>Add a person to this tag</translation>
    </message>
    <message>
        <source>Megbeszélések számolása…</source>
        <translation>Counting meetings…</translation>
    </message>
    <message>
        <source>címkéje</source>
        <translation>tags</translation>
    </message>
    <message>
        <source>Levétel: %1</source>
        <translation>Remove: %1</translation>
    </message>
    <message>
        <source>Ráteszem</source>
        <translation>Apply</translation>
    </message>
    <message>
        <source>A címke rákerül: %1</source>
        <translation>Tag applied to: %1</translation>
    </message>
    <message>
        <source>Ráteszem: %1</source>
        <translation>Apply: %1</translation>
    </message>
</context>
<context>
    <name>TagRow</name>
    <message>
        <source>Címkék</source>
        <translation>Tags</translation>
    </message>
    <message>
        <source>Címke hozzáadása</source>
        <translation>Add tag</translation>
    </message>
    <message>
        <source>Címke</source>
        <translation>Tag</translation>
    </message>
    <message>
        <source>Címke hozzáadása (T)</source>
        <translation>Add tag (T)</translation>
    </message>
    <message>
        <source>Kattintás: hozzáadás · ×: nem illik ide</source>
        <translation>Click: add · ×: doesn’t fit</translation>
    </message>
    <message>
        <source>Az összes javaslat a „Miért?” alatt</source>
        <translation>All suggestions under “Why?”</translation>
    </message>
    <message>
        <source>Minden javaslat hozzáadása</source>
        <translation>Add all suggestions</translation>
    </message>
    <message>
        <source>Mind</source>
        <translation>All</translation>
    </message>
    <message>
        <source>Miért ezek a javaslatok?</source>
        <translation>Why these suggestions?</translation>
    </message>
    <message>
        <source>Miért?</source>
        <translation>Why?</translation>
    </message>
    <message>
        <source>Javaslatok készülnek…</source>
        <translation>Preparing suggestions…</translation>
    </message>
</context>
<context>
    <name>TagRowOverview</name>
    <message>
        <source>Címkék</source>
        <translation>Tags</translation>
    </message>
    <message>
        <source>Címke…</source>
        <translation>Tag…</translation>
    </message>
    <message>
        <source>A résztvevők alapján:</source>
        <translation>Based on the participants:</translation>
    </message>
    <message>
        <source>Kattintás: hozzáadás · ×: nem illik ide</source>
        <translation>Click: add · ×: doesn’t fit</translation>
    </message>
    <message>
        <source>Miért ezek a javaslatok?</source>
        <translation>Why these suggestions?</translation>
    </message>
    <message>
        <source>Miért?</source>
        <translation>Why?</translation>
    </message>
</context>
<context>
    <name>TagWhyPopover</name>
    <message>
        <source>Közös résztvevő</source>
        <translation>Shared participant</translation>
    </message>
    <message>
        <source>Hasonló cím</source>
        <translation>Similar title</translation>
    </message>
    <message>
        <source>Közös kifejezések</source>
        <translation>Shared terms</translation>
    </message>
    <message>
        <source>Résztvevők címkéje</source>
        <translation>Participants&apos; tag</translation>
    </message>
    <message>
        <source>Miért ezek?</source>
        <translation>Why these?</translation>
    </message>
    <message>
        <source>Hozzáadás</source>
        <translation>Add</translation>
    </message>
    <message>
        <source>Nem illik ide</source>
        <translation>Doesn’t fit</translation>
    </message>
    <message>
        <source>Hasonló megbeszélés</source>
        <translation>Similar meeting</translation>
    </message>
    <message>
        <source>Az elutasított javaslatot ennél a megbeszélésnél nem ajánljuk újra.</source>
        <translation>A dismissed suggestion won’t be offered again for this meeting.</translation>
    </message>
</context>
<context>
    <name>TagsDetailPane</name>
    <message>
        <source>Még nincs címke</source>
        <translation>No tags yet</translation>
    </message>
    <message>
        <source>Címkét a megbeszélés fejlécében, a felvevőben vagy importkor adhatsz. Ha már van néhány, a rendszer a hasonló megbeszélésekre magától is javasol.</source>
        <translation>You can add tags in the meeting header, in the recorder or when importing. Once there are a few, Tanara also suggests them for similar meetings on its own.</translation>
    </message>
    <message>
        <source>A címke új neve</source>
        <translation>New tag name</translation>
    </message>
    <message>
        <source>Mentés</source>
        <translation>Save</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Enter: mentés · Esc: mégse. A régi névre a beviteli mező ezt a címkét ajánlja.</source>
        <translation>Enter: save · Esc: cancel. Typing the old name in the tag input will suggest this tag.</translation>
    </message>
    <message>
        <source>Átnevezés</source>
        <translation>Rename</translation>
    </message>
    <message>
        <source>Összevonás…</source>
        <translation>Merge…</translation>
    </message>
    <message>
        <source>Címke törlése</source>
        <translation>Delete tag</translation>
    </message>
    <message>
        <source>A kifejezéseket és az együtt járást a rendszer a címke használatából tanulja; a javaslatok indoklása ebből jön. A személyeken a címkét te teheted fel vagy veheted le.</source>
        <translation>The system learns the terms and co-occurrence from how the tag is used; the reasons for suggestions come from that. On people, you add or remove the tag yourself.</translation>
    </message>
    <message>
        <source>Jellemző kifejezések</source>
        <translation>Typical terms</translation>
    </message>
    <message>
        <source>Gyakran együtt</source>
        <translation>Often together</translation>
    </message>
    <message>
        <source>Megnyitás: #%1</source>
        <translation>Open: #%1</translation>
    </message>
    <message>
        <source>Megbeszélések</source>
        <translation>Meetings</translation>
    </message>
    <message>
        <source>Megnyitás a könyvtárban szűrőként</source>
        <translation>Open as a filter in the library</translation>
    </message>
</context>
<context>
    <name>TagsListPane</name>
    <message>
        <source>Címke keresése</source>
        <translation>Search tags</translation>
    </message>
    <message>
        <source>Keresés a címkék között</source>
        <translation>Search among tags</translation>
    </message>
    <message>
        <source>Rendezés: %1</source>
        <translation>Sort: %1</translation>
    </message>
    <message>
        <source>Legutóbb használt</source>
        <translation>Recently used</translation>
    </message>
    <message>
        <source>ABC</source>
        <translation>A–Z</translation>
    </message>
    <message>
        <source>Leggyakoribb</source>
        <translation>Most used</translation>
    </message>
    <message>
        <source>Itt jelennek meg a címkék, amint az első megbeszélésre felkerül egy.</source>
        <translation>Tags appear here as soon as the first meeting gets one.</translation>
    </message>
    <message>
        <source>Nincs ilyen címke.</source>
        <translation>No such tag.</translation>
    </message>
</context>
<context>
    <name>TagsMergeDialog</name>
    <message>
        <source>#%1 összevonása</source>
        <translation>Merge #%1</translation>
    </message>
    <message>
        <source>Ha ugyanazt jelenti két név. A megbeszélések egy címke alá kerülnek, a profilok összeadódnak.</source>
        <translation>For when two names mean the same thing. The meetings move under one tag and the profiles are combined.</translation>
    </message>
    <message>
        <source>Nincs hasonló nevű vagy gyakran együtt járó címke.</source>
        <translation>No tag with a similar name or that often goes together with it.</translation>
    </message>
    <message>
        <source>Megmaradó név</source>
        <translation>Name to keep</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Összevonás</source>
        <translation>Merge</translation>
    </message>
</context>
<context>
    <name>TagsViewModel</name>
    <message>
        <source>az</source>
        <comment>névelő magánhangzó előtt</comment>
        <translation>the</translation>
    </message>
    <message>
        <source>a</source>
        <comment>névelő mássalhangzó előtt</comment>
        <translation>the</translation>
    </message>
</context>
<context>
    <name>TagsWindow</name>
    <message>
        <source>Címkék</source>
        <translation>Tags</translation>
    </message>
    <message>
        <source>Akin már rajta van a címke, nem szerepel a listában.</source>
        <translation>People who already have the tag are not in the list.</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Törlés</source>
        <translation>Delete</translation>
    </message>
</context>
<context>
    <name>TaskStrip</name>
    <message>
        <source>Megszakítás…</source>
        <translation>Cancelling…</translation>
    </message>
    <message>
        <source>Megszakítás</source>
        <translation>Cancel</translation>
    </message>
</context>
<context>
    <name>TopicCard</name>
    <message>
        <source>Téma áthelyezése: %1</source>
        <translation>Move topic: %1</translation>
    </message>
    <message>
        <source>Húzd az átrendezéshez (Ctrl+↑ / Ctrl+↓)</source>
        <translation>Drag to reorder (Ctrl+↑ / Ctrl+↓)</translation>
    </message>
    <message>
        <source>Téma újraelemzése</source>
        <translation>Re-analyze topic</translation>
    </message>
    <message>
        <source>Téma elemzése</source>
        <translation>Analyze topic</translation>
    </message>
    <message>
        <source>Cím és leírás szerkesztése</source>
        <translation>Edit title and description</translation>
    </message>
    <message>
        <source>Téma törlése</source>
        <translation>Delete topic</translation>
    </message>
    <message>
        <source>kész</source>
        <translation>done</translation>
    </message>
    <message>
        <source>fut</source>
        <translation>running</translation>
    </message>
    <message>
        <source>sorban áll</source>
        <translation>queued</translation>
    </message>
    <message>
        <source>hibás</source>
        <translation>failed</translation>
    </message>
    <message>
        <source>vár</source>
        <translation>waiting</translation>
    </message>
    <message>
        <source>Kevesebb</source>
        <translation>Less</translation>
    </message>
    <message>
        <source>Teljes elemzés</source>
        <translation>Full analysis</translation>
    </message>
    <message>
        <source>Döntések</source>
        <translation>Decisions</translation>
    </message>
    <message>
        <source>Nyitott kérdések</source>
        <translation>Open questions</translation>
    </message>
    <message>
        <source>Teendők</source>
        <translation>Action items</translation>
    </message>
    <message>
        <source>elemzés…</source>
        <translation>analyzing…</translation>
    </message>
    <message>
        <source>Megszakítás</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Betöltés nagyobb kontextussal</source>
        <translation>Load with a larger context</translation>
    </message>
    <message>
        <source>Újra</source>
        <translation>Retry</translation>
    </message>
    <message>
        <source>A téma címe</source>
        <translation>Topic title</translation>
    </message>
    <message>
        <source>Rövid leírás: mire figyeljen az elemzés (elhagyható)</source>
        <translation>Short description: what the analysis should focus on (optional)</translation>
    </message>
    <message>
        <source>A téma leírása</source>
        <translation>Topic description</translation>
    </message>
    <message>
        <source>A meglévő elemzés megmarad; új szöveghez futtasd újra a témát.</source>
        <translation>The existing analysis is kept; rerun the topic to get new text.</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Mentés</source>
        <translation>Save</translation>
    </message>
</context>
<context>
    <name>TrackCatalog</name>
    <message>
        <source>Saját mikrofon</source>
        <translation>My microphone</translation>
    </message>
    <message>
        <source>Hívás hangja</source>
        <translation>Call audio</translation>
    </message>
    <message>
        <source>Rendszerhang</source>
        <translation>System audio</translation>
    </message>
    <message>
        <source>Hangsáv</source>
        <translation>Audio track</translation>
    </message>
</context>
<context>
    <name>TrackRow</name>
    <message>
        <source>A hangfájl hiányzik</source>
        <translation>The audio file is missing</translation>
    </message>
    <message>
        <source>hiányzik a fájl</source>
        <translation>file missing</translation>
    </message>
    <message>
        <source>eldobott · csendes</source>
        <translation>discarded · silent</translation>
    </message>
    <message>
        <source>hiányzik a hangfájl</source>
        <translation>audio file missing</translation>
    </message>
    <message>
        <source>nincs beszéd · kimaradt a lekeverésből</source>
        <translation>no speech · left out of the mix</translation>
    </message>
    <message>
        <source>kivetted a lekeverésből</source>
        <translation>you removed it from the mix</translation>
    </message>
    <message>
        <source>beszéd-ellenőrzés…</source>
        <translation>checking for speech…</translation>
    </message>
    <message>
        <source>beszéd %1%</source>
        <translation>speech %1%</translation>
    </message>
    <message>
        <source>A sáv hangfájlja a megbeszélés mappájába került.</source>
        <translation>The track&apos;s audio file was copied into the meeting folder.</translation>
    </message>
    <message>
        <source>Végleg törlöd a sávot?</source>
        <translation>Delete the track permanently?</translation>
    </message>
    <message>
        <source>A(z) „%1” sáv hangfájlja véglegesen törlődik a lemezről, és a sáv kikerül a megbeszélésből. Ez nem vonható vissza.</source>
        <translation>The audio file of the &quot;%1&quot; track is permanently deleted from the disk, and the track is removed from the meeting. This cannot be undone.</translation>
    </message>
    <message>
        <source>Végleges törlés</source>
        <translation>Delete permanently</translation>
    </message>
    <message>
        <source>Közben másik megbeszélésre váltottál, ezért semmi nem törlődött.</source>
        <translation>You switched to another meeting in the meantime, so nothing was deleted.</translation>
    </message>
    <message>
        <source>A sáv törölve.</source>
        <translation>Track deleted.</translation>
    </message>
    <message>
        <source>Szünet</source>
        <translation>Pause</translation>
    </message>
    <message>
        <source>Sáv meghallgatása (az első beszédtől)</source>
        <translation>Listen to the track (from the first speech)</translation>
    </message>
    <message>
        <source>Beemelem</source>
        <translation>Add back</translation>
    </message>
    <message>
        <source>A sáv visszakerül a lekeverésbe (utána újrakeverés)</source>
        <translation>The track goes back into the mix (then it is re-mixed)</translation>
    </message>
    <message>
        <source>A lekeverésben</source>
        <translation>In the mix</translation>
    </message>
    <message>
        <source>Sáv átnevezése</source>
        <translation>Rename track</translation>
    </message>
    <message>
        <source>A sáv neve</source>
        <translation>Track name</translation>
    </message>
    <message>
        <source>Enter: mentés · Esc: mégse · üresen: „%1”</source>
        <translation>Enter: save · Esc: cancel · empty: &quot;%1&quot;</translation>
    </message>
    <message>
        <source>Megkeresés…</source>
        <translation>Locate…</translation>
    </message>
    <message>
        <source>Visszaállítás</source>
        <translation>Restore</translation>
    </message>
    <message>
        <source>A sáv végleges törlése (a hangfájl is)</source>
        <translation>Delete the track permanently (the audio file too)</translation>
    </message>
</context>
<context>
    <name>TracksTab</name>
    <message>
        <source>A sáv hangfájlja a megbeszélés mappájába került.</source>
        <translation>The track&apos;s audio file was copied into the meeting folder.</translation>
    </message>
    <message>
        <source>Végleg törlöd az eldobott sávokat?</source>
        <translation>Permanently delete the discarded tracks?</translation>
    </message>
    <message numerus="yes">
        <source>%n eldobott sáv hangfájlja véglegesen törlődik a lemezről. Ez nem vonható vissza. Az aktív sávok és a lekeverés megmaradnak.</source>
        <translation>
            <numerusform>The audio file of %n discarded track will be permanently deleted from disk. This cannot be undone. The active tracks and the mixdown are kept.</numerusform>
            <numerusform>The audio files of %n discarded tracks will be permanently deleted from disk. This cannot be undone. The active tracks and the mixdown are kept.</numerusform>
        </translation>
    </message>
    <message>
        <source>Végleges törlés</source>
        <translation>Delete permanently</translation>
    </message>
    <message>
        <source>Közben másik megbeszélésre váltottál, ezért semmi nem törlődött.</source>
        <translation>You switched to another meeting in the meantime, so nothing was deleted.</translation>
    </message>
    <message numerus="yes">
        <source>%n eldobott sáv törölve.</source>
        <translation>
            <numerusform>%n discarded track deleted.</numerusform>
            <numerusform>%n discarded tracks deleted.</numerusform>
        </translation>
    </message>
    <message>
        <source>Ehhez a megbeszéléshez nem tartozik hangsáv.</source>
        <translation>This meeting has no audio tracks.</translation>
    </message>
    <message>
        <source>Lekeverés</source>
        <translation>Mixdown</translation>
    </message>
    <message>
        <source>elavult</source>
        <translation>outdated</translation>
    </message>
    <message>
        <source>nincs aktív sáv, amiből készülhetne</source>
        <translation>no active track to create it from</translation>
    </message>
    <message>
        <source>a sávok változtak azóta; a lejátszó még a régit szólaltatja meg</source>
        <translation>the tracks have changed since; the player still plays the old one</translation>
    </message>
    <message>
        <source>még nem készült el; ebből szól a lejátszó és ebből készül az átirat</source>
        <translation>not created yet; the player and the transcript both use it</translation>
    </message>
    <message numerus="yes">
        <source>%n aktív sávból; ebből szól a lejátszó és ebből készül az átirat</source>
        <translation>
            <numerusform>from %n active track; the player and the transcript both use it</numerusform>
            <numerusform>from %n active tracks; the player and the transcript both use it</numerusform>
        </translation>
    </message>
    <message>
        <source>Megszakítás…</source>
        <translation>Cancelling…</translation>
    </message>
    <message>
        <source>Megszakítás</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>az átírás részeként</source>
        <translation>as part of transcription</translation>
    </message>
    <message>
        <source>Lekeverés frissítése</source>
        <translation>Update mixdown</translation>
    </message>
    <message>
        <source>Lekeverés készítése</source>
        <translation>Create mixdown</translation>
    </message>
    <message>
        <source>Az eldobott sávok megmaradnak, amíg végleg nem törlöd őket.</source>
        <translation>Discarded tracks are kept until you delete them permanently.</translation>
    </message>
    <message>
        <source>Eldobott sávok végleges törlése</source>
        <translation>Permanently delete discarded tracks</translation>
    </message>
</context>
<context>
    <name>TranscribeStepPanel</name>
    <message>
        <source>fut</source>
        <translation>running</translation>
    </message>
    <message>
        <source>kész</source>
        <translation>done</translation>
    </message>
    <message>
        <source>nem sikerült</source>
        <translation>failed</translation>
    </message>
    <message>
        <source>kihagyva</source>
        <translation>skipped</translation>
    </message>
    <message>
        <source>vár</source>
        <translation>waiting</translation>
    </message>
    <message>
        <source>Megszakítás folyamatban; a felvétel és a sávok érintetlenek maradnak.</source>
        <translation>Cancelling; the recording and the tracks are left untouched.</translation>
    </message>
    <message>
        <source>Közben nyugodtan dolgozz tovább; szólunk, ha kész.</source>
        <translation>Feel free to keep working; we&apos;ll let you know when it&apos;s done.</translation>
    </message>
    <message>
        <source>Megszakítás</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Az átírás leáll; a felvétel és a sávok megmaradnak, később újraindítható</source>
        <translation>The transcription stops; the recording and the tracks are kept and it can be restarted later</translation>
    </message>
    <message>
        <source>Az átírás nem sikerült</source>
        <translation>Transcription failed</translation>
    </message>
    <message>
        <source>A felvétel és a sávok érintetlenek.</source>
        <translation>The recording and the tracks are untouched.</translation>
    </message>
    <message>
        <source>Újra</source>
        <translation>Retry</translation>
    </message>
    <message>
        <source>Átírás indítása</source>
        <translation>Start transcription</translation>
    </message>
    <message>
        <source>Szolgáltató: %1</source>
        <translation>Provider: %1</translation>
    </message>
</context>
<context>
    <name>TranscriptChangeBar</name>
    <message>
        <source>Visszavonás</source>
        <translation>Undo</translation>
    </message>
    <message>
        <source>A most készült hanglenyomat törlése (az elnevezés marad)</source>
        <translation>Remove the voiceprint just created (the name stays)</translation>
    </message>
    <message>
        <source>Hanglenyomat készítése</source>
        <translation>Create voiceprint</translation>
    </message>
    <message>
        <source>Még nincs hanglenyomata. Az itteni, hosszabb soraiból most készíthető — ebből ismeri fel a program a következő megbeszéléseken</source>
        <translation>No voiceprint yet. One can be created now from their longer lines here — this is how the app recognises them in later meetings</translation>
    </message>
    <message numerus="yes">
        <source>Hasonló %n sor is</source>
        <translation>
            <numerusform>%n similar line too</numerusform>
            <numerusform>%n similar lines too</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Még %n sor hasonlít erre a hangra: ezek is átkerülnek ide: %1</source>
        <translation>
            <numerusform>%n more line resembles this voice: it will move to %1 as well</numerusform>
            <numerusform>%n more lines resemble this voice: they will move to %1 as well</numerusform>
        </translation>
    </message>
    <message>
        <source>Elrejtem</source>
        <translation>Hide</translation>
    </message>
    <message>
        <source>Megmutatom</source>
        <translation>Show</translation>
    </message>
    <message>
        <source>A hasonló sorok kiemelése a sávokon és az áttekintőn</source>
        <translation>Highlight the similar lines on the lanes and the overview</translation>
    </message>
    <message>
        <source>A beszélő összes megmaradt sora is átkerül (előbb megerősítést kér): %1</source>
        <translation>Moves all remaining lines of the speaker as well (asks for confirmation first): %1</translation>
    </message>
    <message numerus="yes">
        <source>Még &lt;b&gt;%n sor&lt;/b&gt; hangja hasonlít a most megadott %1 sorhoz</source>
        <translation>
            <numerusform>&lt;b&gt;%n more line&lt;/b&gt; sounds like the %1 line you just set</numerusform>
            <numerusform>&lt;b&gt;%n more lines&lt;/b&gt; sound like the %1 line you just set</numerusform>
        </translation>
    </message>
    <message>
        <source>Megmutatva a sínen</source>
        <translation>Shown on the rail</translation>
    </message>
    <message>
        <source>Megmutatom a sínen</source>
        <translation>Show on the rail</translation>
    </message>
    <message>
        <source>A hasonló sorok kék kerettel a sínen és jellel a térképen</source>
        <translation>Similar lines get a blue frame on the rail and a marker on the map</translation>
    </message>
    <message>
        <source>Mind a %1 hozzá</source>
        <translation>All %1 to them</translation>
    </message>
    <message>
        <source>Mind %1 sorai közé kerülnek — egy lépésben visszavonható (Ctrl+Z)</source>
        <translation>Moves them among %1&apos;s lines — undoable in one step (Ctrl+Z)</translation>
    </message>
    <message>
        <source>A hanglenyomat most elkészül az itteni soraiból</source>
        <translation>The voiceprint will now be created from their lines here</translation>
    </message>
    <message>
        <source>Amint elég tiszta beszéd gyűlik össze (további sorok hozzá), a hanglenyomat elkészül</source>
        <translation>The voiceprint will be created as soon as enough clean speech has accumulated (with more lines added)</translation>
    </message>
    <message>
        <source>Átnézés</source>
        <translation>Review</translation>
    </message>
    <message>
        <source>Csak a két beszélő sorait mérem a megerősített soraik hangjához; a kétségeseket az Átnézendő szűrőbe teszem (visszavonható: Ctrl+Z)</source>
        <translation>I only compare the lines of the two speakers with the audio of their confirmed lines; I put the doubtful ones into the To review filter (undoable: Ctrl+Z)</translation>
    </message>
    <message>
        <source>A címke-döntés visszavonása</source>
        <translation>Undo the tag decision</translation>
    </message>
    <message>
        <source>AZ ELŐZŐ DÖNTÉSEK</source>
        <translation>EARLIER DECISIONS</translation>
    </message>
    <message>
        <source>Most nem</source>
        <translation>Not now</translation>
    </message>
    <message>
        <source>IMÉNT</source>
        <translation>JUST NOW</translation>
    </message>
    <message>
        <source>Még nincs döntés — itt látszik az utolsó, és innen visszavonható (Ctrl+Z).</source>
        <translation>No decision yet — the latest one shows here and can be undone from here (Ctrl+Z).</translation>
    </message>
    <message>
        <source>Visszavonás: %1</source>
        <translation>Undo: %1</translation>
    </message>
    <message>
        <source>Az előző döntések (legfeljebb 5)</source>
        <translation>Earlier decisions (up to 5)</translation>
    </message>
    <message>
        <source>Az ajánlatok bezárása (az „Imént” sor marad)</source>
        <translation>Close the offers (the “Just now” row stays)</translation>
    </message>
    <message>
        <source>Legyen nem minta</source>
        <translation>Make it not a sample</translation>
    </message>
    <message>
        <source>A sor marad a beszélőnél, de a hangja nem lesz minta (egy lépés, Ctrl+Z)</source>
        <translation>The line stays with the speaker, but its voice will not be a sample (one step, Ctrl+Z)</translation>
    </message>
    <message>
        <source>Ebben a munkamenetben erre a két beszélőre nem kérdezek rá újra</source>
        <translation>I won&apos;t ask about these two speakers again in this session</translation>
    </message>
</context>
<context>
    <name>TranscriptMarker</name>
    <message>
        <source>rövid</source>
        <translation>short</translation>
    </message>
    <message>
        <source>javítva</source>
        <translation>corrected</translation>
    </message>
    <message>
        <source>új személy</source>
        <translation>new person</translation>
    </message>
    <message>
        <source>egymásra beszéltek</source>
        <translation>cross-talk</translation>
    </message>
    <message>
        <source>nem azonosítható</source>
        <translation>unidentifiable</translation>
    </message>
    <message>
        <source>sáv</source>
        <translation>track</translation>
    </message>
    <message>
        <source>címke</source>
        <translation>tag</translation>
    </message>
    <message>
        <source>hang</source>
        <translation>voice</translation>
    </message>
    <message>
        <source>bizonytalan</source>
        <translation>uncertain</translation>
    </message>
    <message>
        <source>bizonytalan · %1</source>
        <translation>uncertain · %1</translation>
    </message>
</context>
<context>
    <name>TranscriptRow</name>
    <message numerus="yes">
        <source>··· %n biztos sor elrejtve</source>
        <translation>
            <numerusform>··· %n confident line hidden</numerusform>
            <numerusform>··· %n confident lines hidden</numerusform>
        </translation>
    </message>
    <message>
        <source>Más mondta? A kijelölt sorok beszélőjének javítása</source>
        <translation>Someone else said it? Fix the speaker of the selected lines</translation>
    </message>
    <message>
        <source>Más mondta? Ennek a sornak a beszélője javítható</source>
        <translation>Someone else said it? Fix the speaker of this line</translation>
    </message>
    <message>
        <source>Lejátszás innen</source>
        <translation>Play from here</translation>
    </message>
    <message>
        <source>Most vetted fel: a hozzá hasonló hangú sorok kék kerettel látszanak a sínen</source>
        <translation>You just added this: lines with a similar voice show with a blue frame on the rail</translation>
    </message>
    <message>
        <source>Kézzel javítottad a beszélőjét</source>
        <translation>You corrected its speaker manually</translation>
    </message>
    <message>
        <source>A sor a beszélője oldalával ellentétes sávon szólt (mikrofon / hívás hangja)</source>
        <translation>The line was on the track opposite its speaker&apos;s side (microphone / call audio)</translation>
    </message>
    <message>
        <source>Hangra kétes, és a beszélő címkéi nem illenek a megbeszéléshez</source>
        <translation>Doubtful by voice, and the speaker&apos;s tags don&apos;t fit the meeting</translation>
    </message>
    <message>
        <source>Hangra kétes, kié a sor</source>
        <translation>Doubtful by voice whose line it is</translation>
    </message>
    <message>
        <source>Nem azonosítható: a név marad, de a hangját semmire nem használom (minta, bizonyíték, átnézendő). Visszavonás: 0 újra, vagy Ctrl+Z</source>
        <translation>Unidentifiable: the name stays, but its voice is not used for anything (sample, evidence, review). Undo: 0 again, or Ctrl+Z</translation>
    </message>
    <message>
        <source>Megerősítetted: a beszélő rendben van</source>
        <translation>You confirmed it: the speaker is right</translation>
    </message>
    <message>
        <source>Túl rövid a hang-ellenőrzéshez: csak fülre dönthető el, kié</source>
        <translation>Too short for the voice check: only by ear can it be decided whose it is</translation>
    </message>
    <message>
        <source>megálló · itt állt meg</source>
        <translation>stop · stopped here</translation>
    </message>
    <message>
        <source>utólagos ablak · a billentyűk ide</source>
        <translation>grace window · keys apply here</translation>
    </message>
    <message>
        <source>megálló</source>
        <translation>stop</translation>
    </message>
    <message>
        <source>Hangra inkább %1 sorának tűnik</source>
        <translation>By voice it sounds more like %1</translation>
    </message>
    <message numerus="yes">
        <source>··· %n sor elrejtve</source>
        <translation>
            <numerusform>··· %n line hidden</numerusform>
            <numerusform>··· %n lines hidden</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>··· %n nem azonosítható sor elrejtve</source>
        <translation>
            <numerusform>··· %n unidentifiable line hidden</numerusform>
            <numerusform>··· %n unidentifiable lines hidden</numerusform>
        </translation>
    </message>
    <message>
        <source>Hangminta: a sor hangja tanítja %1 felismerését</source>
        <translation>Voice sample: the line&apos;s voice teaches the recognition of %1</translation>
    </message>
    <message>
        <source>Nem minta: egymásra beszéltek / zajos, a hangját nem használom</source>
        <translation>Not a sample: overlapping speech / noisy, its voice is not used</translation>
    </message>
    <message>
        <source>Nem minta: túl rövid ahhoz, hogy a hangja minta legyen</source>
        <translation>Not a sample: too short for its voice to be a sample</translation>
    </message>
    <message>
        <source>Ebben a sorban más is beszél egyszerre, ezért a hangját nem használom mintának (bizonytalanság-jelzés, hanglenyomat).</source>
        <translation>Someone else speaks at the same time in this line, so its voice is not used as a sample (uncertainty marks, voiceprints).</translation>
    </message>
    <message>
        <source>Megjelölted, hogy ezt a sort ne használjam hangmintának (bizonytalanság-jelzés, hanglenyomat).</source>
        <translation>You marked this line not to be used as a voice sample (uncertainty marks, voiceprints).</translation>
    </message>
    <message>
        <source>Mintának használható</source>
        <translation>Usable as a sample</translation>
    </message>
    <message>
        <source>A sor hangja mégis mehet mintának</source>
        <translation>Use this line’s voice as a sample after all</translation>
    </message>
</context>
<context>
    <name>TranscriptSelectionBar</name>
    <message numerus="yes">
        <source>%n sor kijelölve</source>
        <translation>
            <numerusform>%n line selected</numerusform>
            <numerusform>%n lines selected</numerusform>
        </translation>
    </message>
    <message>
        <source>Áthelyezés:</source>
        <translation>Move to:</translation>
    </message>
    <message>
        <source>Új személy…</source>
        <translation>New person…</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
</context>
<context>
    <name>TranscriptTab</name>
    <message>
        <source>Ehhez a megbeszéléshez még nincs átirat.</source>
        <translation>This meeting has no transcript yet.</translation>
    </message>
    <message>
        <source>Ez az átirat régebbi formátumú: olvasható és másolható, de a beszélők itt nem javíthatók. Az újra-átírás új (a szolgáltatónál díjköteles) átírást indít, és lecseréli a mostani átiratot.</source>
        <translation>This transcript is in an older format: you can read and copy it, but speakers cannot be corrected here. Re-transcribing starts a new transcription (billed by the provider) and replaces the current transcript.</translation>
    </message>
    <message>
        <source>Újra-átírás…</source>
        <translation>Re-transcribe…</translation>
    </message>
    <message>
        <source>Az átirat szövegfájlja (transcript.md) nem található.</source>
        <translation>The transcript text file (transcript.md) was not found.</translation>
    </message>
    <message>
        <source>Átnézendő: csak a kétes sorok, okok szerint</source>
        <translation>To review: only the doubtful lines, grouped by reason</translation>
    </message>
    <message>
        <source>Söprés: hallgasd végig; a billentyűk a futó (megálló) sorra vonatkoznak · 1–%1 beszélő · J: Jó így · 0: nem azonosítható · Shift: nem minta</source>
        <translation>Sweep: listen through; keys apply to the playing (stopped) line · 1–%1 speaker · J: looks right · 0: unidentifiable · Shift: not a sample</translation>
    </message>
    <message>
        <source>új oszlop: %1 · kék keret: hozzá hasonló sorok</source>
        <translation>new column: %1 · blue frame: similar lines</translation>
    </message>
    <message>
        <source>Shift: nem minta — a sor átkerül, de a hangja nem tanítja a felismerést</source>
        <translation>Shift: not a sample — the line moves, but its voice does not teach the recognition</translation>
    </message>
    <message>
        <source>másik oszlopra kattintás: áthelyezés · 1–%1: a kijelöltek áthelyezése · Shift: nem minta · 0: nem azonosítható</source>
        <translation>click another column: move · 1–%1: move the selected lines · Shift: not a sample · 0: unidentifiable</translation>
    </message>
    <message>
        <source> · nem minta</source>
        <translation> · not a sample</translation>
    </message>
    <message>
        <source>Nincs több megálló.</source>
        <translation>No more stops.</translation>
    </message>
    <message>
        <source>Nincs átnézendő sor.</source>
        <translation>No lines to review.</translation>
    </message>
    <message>
        <source>Söprés</source>
        <translation>Sweep</translation>
    </message>
    <message>
        <source>Hallgatás közben javítani: a lejátszás a kétes sorok végén megáll, a billentyűk a futó sorra vonatkoznak</source>
        <translation>Fix while listening: playback stops at the end of uncertain lines, keys apply to the playing line</translation>
    </message>
    <message>
        <source>Szűrő: %1 most lett mintái</source>
        <translation>Filter: the new samples of %1</translation>
    </message>
    <message>
        <source>Mind (Esc)</source>
        <translation>All (Esc)</translation>
    </message>
    <message numerus="yes">
        <source>%n nem azonosítható rejtve · mutasd</source>
        <translation>
            <numerusform>%n unidentifiable hidden · show</numerusform>
            <numerusform>%n unidentifiable hidden · show</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n nem azonosítható · elrejtés</source>
        <translation>
            <numerusform>%n unidentifiable · hide</numerusform>
            <numerusform>%n unidentifiable · hide</numerusform>
        </translation>
    </message>
    <message>
        <source>A „nem azonosítható” sorok a hang-elemzésből kimaradnak; itt csak a lista nézetét állítod</source>
        <translation>“Unidentifiable” lines are left out of the voice analysis; here you only change the list view</translation>
    </message>
    <message>
        <source> · marad</source>
        <translation> · stays</translation>
    </message>
    <message>
        <source>Fagyasztott ablak</source>
        <translation>Frozen window</translation>
    </message>
    <message numerus="yes">
        <source>mögötted: %n sor újra kétes</source>
        <translation>
            <numerusform>behind you: %n line uncertain again</numerusform>
            <numerusform>behind you: %n lines uncertain again</numerusform>
        </translation>
    </message>
    <message>
        <source>A háttér-elemzés ezeket a már elhagyott sorokat jelölte újra kétesnek — kattintásra a lista</source>
        <translation>The background analysis marked these already passed lines uncertain again — click for the list</translation>
    </message>
    <message>
        <source>Ha a háttér-elemzés egy már elhagyott sort újra kétesnek jelöl, itt gyűlik</source>
        <translation>If the background analysis marks an already passed line uncertain again, it is collected here</translation>
    </message>
    <message numerus="yes">
        <source>előtted: %n kétes</source>
        <translation>
            <numerusform>ahead: %n uncertain</numerusform>
            <numerusform>ahead: %n uncertain</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%1/%2. szakasz · %3 · %n kétes</source>
        <translation>
            <numerusform>section %1/%2 · %3 · %n uncertain</numerusform>
            <numerusform>section %1/%2 · %3 · %n uncertain</numerusform>
        </translation>
    </message>
    <message>
        <source>Folyamatos</source>
        <translation>Continuous</translation>
    </message>
    <message>
        <source>Kikapcsolva (alap): a kétes sor végén megáll. Bekapcsolva: nem áll meg; az imént hallott kétes sor még 3 mp-ig a billentyűk célpontja</source>
        <translation>Off (default): stops at the end of an uncertain line. On: does not stop; the uncertain line just heard stays the keys&apos; target for 3 s</translation>
    </message>
    <message>
        <source>Lassabban ([)</source>
        <translation>Slower ([)</translation>
    </message>
    <message>
        <source>tempó %1</source>
        <translation>speed %1</translation>
    </message>
    <message>
        <source>Gyorsabban (])</source>
        <translation>Faster (])</translation>
    </message>
    <message>
        <source>Névtelen résztvevő</source>
        <translation>Unnamed participant</translation>
    </message>
    <message>
        <source>Új névtelen résztvevő</source>
        <translation>New unnamed participant</translation>
    </message>
    <message>
        <source>Névtelen beszélő</source>
        <translation>Unnamed speaker</translation>
    </message>
    <message>
        <source>Újra kétes mögötted (a háttér-elemzés jelölte)</source>
        <translation>Uncertain again behind you (marked by the background analysis)</translation>
    </message>
    <message>
        <source>Összevonod a két beszélőt?</source>
        <translation>Merge the two speakers?</translation>
    </message>
    <message numerus="yes">
        <source>%1 %n sora összeolvad ezzel: %2 (%3). Visszavonható: Ctrl+Z.</source>
        <translation>
            <numerusform>%n line of %1 will be merged into %2 (%3). Undo: Ctrl+Z.</numerusform>
            <numerusform>%n lines of %1 will be merged into %2 (%3). Undo: Ctrl+Z.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor</source>
        <translation>
            <numerusform>%n line</numerusform>
            <numerusform>%n lines</numerusform>
        </translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Összevonás</source>
        <translation>Merge</translation>
    </message>
    <message>
        <source>Lejátszás innen</source>
        <translation>Play from here</translation>
    </message>
    <message>
        <source>Más mondta… (a kijelölt sorok)</source>
        <translation>Someone else said it… (the selected lines)</translation>
    </message>
    <message>
        <source>Más mondta… (ez a sor)</source>
        <translation>Someone else said it… (this line)</translation>
    </message>
    <message>
        <source>Jó így (a kijelölt sorok)</source>
        <translation>Looks right (the selected lines)</translation>
    </message>
    <message>
        <source>Jó így</source>
        <translation>Looks right</translation>
    </message>
    <message>
        <source>Újra azonosítható</source>
        <translation>Identifiable again</translation>
    </message>
    <message>
        <source>Nem azonosítható (a kijelölt sorok)</source>
        <translation>Unidentifiable (the selected lines)</translation>
    </message>
    <message>
        <source>Nem azonosítható</source>
        <translation>Unidentifiable</translation>
    </message>
    <message>
        <source>Jó így + hanglenyomat-minta ebből a sorból (%1 mp)</source>
        <translation>Fine as is + voice sample from this line (%1 s)</translation>
    </message>
    <message>
        <source>Hanglenyomat-minta ebből a sorból (%1 mp)</source>
        <translation>Voice sample from this line (%1 s)</translation>
    </message>
    <message>
        <source>Hanglenyomat-minta ebből a sorból</source>
        <translation>Voice sample from this line</translation>
    </message>
    <message>
        <source>Sor másolása</source>
        <translation>Copy line</translation>
    </message>
    <message>
        <source>Kijelölt sorok másolása (%1)</source>
        <translation>Copy selected lines (%1)</translation>
    </message>
    <message>
        <source>Teljes átirat másolása</source>
        <translation>Copy full transcript</translation>
    </message>
    <message>
        <source>Minden sor kijelölése</source>
        <translation>Select all lines</translation>
    </message>
</context>
<context>
    <name>TranscriptToolbar</name>
    <message>
        <source>Olvasás</source>
        <translation>Read</translation>
    </message>
    <message>
        <source>Javítás</source>
        <translation>Correct</translation>
    </message>
    <message>
        <source>Olvasás: tiszta lista (Ctrl+E)</source>
        <translation>Read: clean list (Ctrl+E)</translation>
    </message>
    <message numerus="yes">
        <source>%1 / %2 sor · %n csoport</source>
        <translation>
            <numerusform>%1 / %2 lines · %n group</numerusform>
            <numerusform>%1 / %2 lines · %n groups</numerusform>
        </translation>
    </message>
    <message>
        <source>%1 megszólalás · %2 beszélő</source>
        <translation>%1 utterances · %2 speakers</translation>
    </message>
    <message>
        <source>Hangelemzés… %1%</source>
        <translation>Analyzing voices… %1%</translation>
    </message>
    <message>
        <source>Hangelemzés nélkül</source>
        <translation>No voice analysis</translation>
    </message>
    <message>
        <source>Keresés az átiratban</source>
        <translation>Search the transcript</translation>
    </message>
    <message>
        <source>Nincs találat</source>
        <translation>No results</translation>
    </message>
    <message>
        <source>%1 / %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <source>Előző találat (Shift+Enter)</source>
        <translation>Previous match (Shift+Enter)</translation>
    </message>
    <message>
        <source>Következő találat (Enter)</source>
        <translation>Next match (Enter)</translation>
    </message>
    <message>
        <source>Nincs átnézendő sor. Kattints, és a megerősített és javított sorok hangja alapján újraellenőrzöm a többit.</source>
        <translation>Nothing to review. Click and the other lines are re-checked against the voice of the confirmed and corrected lines.</translation>
    </message>
    <message>
        <source>Nincs átnézendő sor. Újraellenőrzéshez: %1</source>
        <translation>Nothing to review. To re-check: %1</translation>
    </message>
    <message>
        <source>A kétes sorok okok szerint csoportosítva, csoportonként egy döntéssel (B: a következő)</source>
        <translation>Doubtful lines grouped by reason, one decision per group (B: next)</translation>
    </message>
    <message>
        <source>Átnézendő</source>
        <translation>To review</translation>
    </message>
    <message>
        <source>Átnézendő (%1)</source>
        <translation>To review (%1)</translation>
    </message>
    <message>
        <source>Javítás: beszélő-oszlopok, jelölők, soronkénti javítás (Ctrl+E)</source>
        <translation>Fix: speaker columns, markers, per-line fixes (Ctrl+E)</translation>
    </message>
    <message>
        <source>Átnézendő: a kétes sorok okok szerint csoportosítva, csoportonként egy döntéssel (B: a következő)</source>
        <translation>To review: doubtful lines grouped by reason, one decision per group (B: next)</translation>
    </message>
    <message>
        <source>Nincs átnézendő sor</source>
        <translation>Nothing to review</translation>
    </message>
    <message numerus="yes">
        <source>%n átnézendő sor</source>
        <translation>
            <numerusform>%n line to review</numerusform>
            <numerusform>%n lines to review</numerusform>
        </translation>
    </message>
    <message>
        <source>Következő átnézendő sor (B)</source>
        <translation>Next line to review (B)</translation>
    </message>
    <message>
        <source>Ki volt ott?</source>
        <translation>Who was there?</translation>
    </message>
    <message>
        <source>A résztvevők átnézése: kik voltak ott, és melyik beszélő kicsoda</source>
        <translation>Review the participants: who was there, and which speaker is who</translation>
    </message>
    <message>
        <source>Keresés bezárása</source>
        <translation>Close search</translation>
    </message>
    <message>
        <source>Keresés az átiratban (Ctrl+Shift+F)</source>
        <translation>Search the transcript (Ctrl+Shift+F)</translation>
    </message>
</context>
<context>
    <name>VoiceModelListModel</name>
    <message>
        <source>hiányzó felvétel (a megbeszélés már nincs meg)</source>
        <translation>missing recording (the meeting no longer exists)</translation>
    </message>
    <message>
        <source>hiányzó felvétel</source>
        <translation>missing recording</translation>
    </message>
    <message>
        <source>nem számolható (túl rövid hang)</source>
        <translation>cannot be computed (audio too short)</translation>
    </message>
    <message>
        <source>törölt megbeszélés</source>
        <translation>deleted meeting</translation>
    </message>
</context>
<context>
    <name>VoiceModelOnboardingRow</name>
    <message>
        <source>A hangmodell letöltve: a Tanara felismeri, ki beszél.</source>
        <translation>The voice model is downloaded: Tanara recognizes who is speaking.</translation>
    </message>
    <message>
        <source>Hangmodell a beszélők felismeréséhez</source>
        <translation>Voice model for recognizing speakers</translation>
    </message>
    <message>
        <source>Nélküle is minden működik, csak a beszélőket neked kell elnevezned. Licenc: Apache-2.0; később a Beállítások › Beszélők lapon is letöltheted.</source>
        <translation>Everything works without it, you just have to name the speakers yourself. License: Apache-2.0; you can also download it later under Settings › Speakers.</translation>
    </message>
    <message>
        <source>Hangmodell letöltése (%1)</source>
        <translation>Download voice model (%1)</translation>
    </message>
    <message>
        <source>Nincs internetkapcsolat.</source>
        <translation>No internet connection.</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
</context>
<context>
    <name>VoiceprintPanel</name>
    <message numerus="yes">
        <source>Elkészült %n sorból, %1 mp beszédből.</source>
        <translation>
            <numerusform>Created from %n line, %1 s of speech.</numerusform>
            <numerusform>Created from %n lines, %1 s of speech.</numerusform>
        </translation>
    </message>
    <message>
        <source>A most készült hanglenyomat törölve.</source>
        <translation>The voiceprint just created was removed.</translation>
    </message>
    <message>
        <source>Névtelen beszélő</source>
        <translation>Unnamed speaker</translation>
    </message>
    <message>
        <source>Van hanglenyomata</source>
        <translation>Has a voiceprint</translation>
    </message>
    <message>
        <source>Még nincs hanglenyomata</source>
        <translation>No voiceprint yet</translation>
    </message>
    <message>
        <source>Hanglenyomat csak elnevezett beszélőhöz készíthető. Előbb add meg, ki ő.</source>
        <translation>A voiceprint can only be created for a named speaker. First say who this is.</translation>
    </message>
    <message>
        <source>A megbeszélés lekevert hangja nem érhető el, ezért itt most nem készíthető hanglenyomat.</source>
        <translation>The meeting&apos;s mixdown is not available, so a voiceprint cannot be created here right now.</translation>
    </message>
    <message>
        <source>Nincs letöltve a hangmodell, ezért itt most nem készíthető hanglenyomat.</source>
        <translation>The voice model is not downloaded, so a voiceprint cannot be created here right now.</translation>
    </message>
    <message numerus="yes">
        <source>Ebből a megbeszélésből %n hosszabb sora használható fel (%1 mp beszéd).</source>
        <translation>
            <numerusform>%n longer line of theirs from this meeting can be used (%1 s of speech).</numerusform>
            <numerusform>%n longer lines of theirs from this meeting can be used (%1 s of speech).</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Ebből a megbeszélésből csak %n hosszabb sora használható (%1 mp beszéd).</source>
        <translation>
            <numerusform>Only %n longer line of theirs from this meeting can be used (%1 s of speech).</numerusform>
            <numerusform>Only %n longer lines of theirs from this meeting can be used (%1 s of speech).</numerusform>
        </translation>
    </message>
    <message>
        <source>Ebben a megbeszélésben nincs elég hosszú, jól használható sora.</source>
        <translation>They have no line in this meeting that is long and clear enough.</translation>
    </message>
    <message>
        <source>Még kb. %1 mp tiszta beszéd kellene: segít, ha több, legalább 3 másodperces sorát rendeled hozzá.</source>
        <translation>About %1 s more clear speech is needed: it helps to assign more of their lines that are at least 3 seconds long.</translation>
    </message>
    <message>
        <source>Új minta</source>
        <translation>New sample</translation>
    </message>
    <message>
        <source>Készítés</source>
        <translation>Create</translation>
    </message>
    <message>
        <source>Új minta készítése</source>
        <translation>Create new sample</translation>
    </message>
    <message>
        <source>Hanglenyomat készítése</source>
        <translation>Create voiceprint</translation>
    </message>
    <message>
        <source>Hanglenyomat készül a beszélő itteni, hosszabb soraiból — ebből ismeri fel a program a következő megbeszéléseken</source>
        <translation>A voiceprint is made from the speaker&apos;s longer lines here — this is how the app recognises them in later meetings</translation>
    </message>
    <message>
        <source>Visszavonás</source>
        <translation>Undo</translation>
    </message>
    <message>
        <source>A most készült hanglenyomat törlése</source>
        <translation>Remove the voiceprint just created</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <source>Tanara — Felvétel</source>
        <translation>Tanara — Recording</translation>
    </message>
    <message>
        <source>Tanara — Beállítások</source>
        <translation>Tanara — Settings</translation>
    </message>
    <message>
        <source>A Beállítások felülete nem tölthető be.</source>
        <translation>The Settings interface could not be loaded.</translation>
    </message>
    <message>
        <source>Felvevő megjelenítése</source>
        <translation>Show recorder</translation>
    </message>
    <message>
        <source>Felvétel leállítása</source>
        <translation>Stop recording</translation>
    </message>
    <message>
        <source>Tanara — felvétel fut</source>
        <translation>Tanara — recording</translation>
    </message>
    <message>
        <source>Tanara felvevő</source>
        <translation>Tanara Recorder</translation>
    </message>
    <message>
        <source>A felvevő felülete nem tölthető be.</source>
        <translation>The recorder interface could not be loaded.</translation>
    </message>
</context>
<context>
    <name>tanara::PeopleService</name>
    <message>
        <source>Névtelen %1</source>
        <translation>Anonymous %1</translation>
    </message>
</context>
<context>
    <name>tanara::SpeakerEditor</name>
    <message>
        <source>Új beszélő %1</source>
        <translation>New speaker %1</translation>
    </message>
</context>
<context>
    <name>tanara::app::AppController</name>
    <message>
        <source>%1 tokenes kontextus, egy szálon</source>
        <translation>%1-token context, single-threaded</translation>
    </message>
    <message>
        <source> · a szerveren más modell is betöltve: %1</source>
        <translation> · another model is also loaded on the server: %1</translation>
    </message>
    <message>
        <source>Modell betöltése</source>
        <translation>Loading model</translation>
    </message>
    <message>
        <source>Modell betöltése…</source>
        <translation>Loading model…</translation>
    </message>
    <message>
        <source>Modell betöltése (%1 tokenes kontextus)…</source>
        <translation>Loading model (%1-token context)…</translation>
    </message>
    <message>
        <source>Importálás</source>
        <translation>Import</translation>
    </message>
    <message>
        <source>Ismeretlen meeting: %1</source>
        <translation>Unknown meeting: %1</translation>
    </message>
    <message>
        <source>Leírás módosítva</source>
        <translation>Description changed</translation>
    </message>
    <message>
        <source>Hiányzik a(z) „%1” sáv hangfájlja — a meglévő lekeverés megmaradt. Keresd meg a fájlt a Sávok fülön, vagy dobd el a sávot, és keverd újra.</source>
        <translation>The audio file of track &quot;%1&quot; is missing — the existing mixdown was kept. Locate the file on the Tracks tab, or discard the track and mix down again.</translation>
    </message>
    <message>
        <source>Nincs aktív hangsáv a lekeveréshez.</source>
        <translation>No active audio track for mixdown.</translation>
    </message>
    <message>
        <source>Lekeverés</source>
        <translation>Mixdown</translation>
    </message>
    <message>
        <source>A lekeverés (ffmpeg) sikertelen.</source>
        <translation>Mixdown (ffmpeg) failed.</translation>
    </message>
    <message>
        <source>A lekeverés nem sikerült.</source>
        <translation>The mixdown failed.</translation>
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
        <source>A Tanara Cloudhoz frissítés kell (legalább %1).</source>
        <translation>Tanara Cloud requires an update (%1 or newer).</translation>
    </message>
    <message>
        <source>töltsd fel az egyenleged</source>
        <translation>top up your balance</translation>
    </message>
    <message>
        <source>Feltöltés</source>
        <translation>Upload</translation>
    </message>
    <message>
        <source>%1 perc, %2 sáv</source>
        <translation>%1 min, %2 tracks</translation>
    </message>
    <message>
        <source>Átírás</source>
        <translation>Transcription</translation>
    </message>
    <message>
        <source>Beszélők szétválasztása</source>
        <translation>Speaker separation</translation>
    </message>
    <message>
        <source>Résztvevők azonosítása</source>
        <translation>Identifying participants</translation>
    </message>
    <message>
        <source>Átírás folyamatban</source>
        <translation>Transcription in progress</translation>
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
        <source>sorban áll</source>
        <translation>queued</translation>
    </message>
    <message>
        <source>Az átírás üres eredményt adott (a szolgáltató nem talált beszédet a felvételen) — a korábbi átirat megmaradt.</source>
        <translation>Transcription returned an empty result (the provider found no speech in the recording) — the previous transcript was kept.</translation>
    </message>
    <message>
        <source>Az átírás üres eredményt adott: a szolgáltató nem talált beszédet a felvételen.</source>
        <translation>Transcription returned an empty result: the provider found no speech in the recording.</translation>
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
        <source>Összefoglalás a Tanara Cloudban…</source>
        <translation>Summarizing in Tanara Cloud…</translation>
    </message>
    <message>
        <source>Összefoglalás a helyi modellel…</source>
        <translation>Summarising with the local model…</translation>
    </message>
    <message>
        <source>Összefoglalás</source>
        <translation>Summarising</translation>
    </message>
    <message numerus="yes">
        <source>%n rész</source>
        <translation>
            <numerusform>%n part</numerusform>
            <numerusform>%n parts</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source> · %n korábbi futásból</source>
        <translation>
            <numerusform> · %n from an earlier run</numerusform>
            <numerusform> · %n from an earlier run</numerusform>
        </translation>
    </message>
    <message>
        <source>Jegyzetelés részenként</source>
        <translation>Notes part by part</translation>
    </message>
    <message>
        <source>Összegzés</source>
        <translation>Summing up</translation>
    </message>
    <message>
        <source>Összefoglaló készítése</source>
        <translation>Writing the summary</translation>
    </message>
    <message numerus="yes">
        <source>%n rész (a modell kontextusához igazítva)</source>
        <translation>
            <numerusform>%n part (fitted to the model&apos;s context)</numerusform>
            <numerusform>%n parts (fitted to the model&apos;s context)</numerusform>
        </translation>
    </message>
    <message>
        <source>%1/%2. rész</source>
        <translation>Part %1/%2</translation>
    </message>
    <message numerus="yes">
        <source>%n rész kész</source>
        <translation>
            <numerusform>%n part done</numerusform>
            <numerusform>%n parts done</numerusform>
        </translation>
    </message>
    <message>
        <source>Jegyzetelés: %1/%2. rész…</source>
        <translation>Taking notes: part %1/%2…</translation>
    </message>
    <message>
        <source>Összegzés a részjegyzetekből…</source>
        <translation>Summing up the part notes…</translation>
    </message>
    <message>
        <source>Összefoglaló hiba: %1</source>
        <translation>Summary error: %1</translation>
    </message>
    <message>
        <source>Témák kigyűjtése a Tanara Cloudban…</source>
        <translation>Collecting topics in Tanara Cloud…</translation>
    </message>
    <message>
        <source>Témák kigyűjtése a helyi modellel…</source>
        <translation>Extracting topics with the local model…</translation>
    </message>
    <message>
        <source>Témák javaslása</source>
        <translation>Suggesting topics</translation>
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
        <source>Témák elemzése</source>
        <translation>Analysing topics</translation>
    </message>
    <message>
        <source>%1 téma elemzése nem sikerült.</source>
        <translation>The analysis of %1 topic(s) failed.</translation>
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
        <source>Megszakítva</source>
        <translation>Cancelled</translation>
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
        <source>Összegzés készítése</source>
        <translation>Composing the digest</translation>
    </message>
    <message>
        <source>Komplex összefoglaló hiba: %1</source>
        <translation>Complex summary error: %1</translation>
    </message>
    <message>
        <source>Már fut egy importálás — várd meg, vagy szakítsd meg.</source>
        <translation>An import is already running — wait for it to finish or cancel it.</translation>
    </message>
    <message>
        <source>Nincs ilyen megbeszélés: %1</source>
        <translation>No such meeting: %1</translation>
    </message>
    <message>
        <source>Felvétel közben a megbeszélés nem exportálható.</source>
        <translation>The meeting cannot be exported while it is being recorded.</translation>
    </message>
    <message>
        <source>Már fut egy archívum-művelet — várd meg, vagy szakítsd meg.</source>
        <translation>An archive operation is already running — wait for it or cancel it.</translation>
    </message>
    <message>
        <source>Exportálás archívumba</source>
        <translation>Export to archive</translation>
    </message>
    <message>
        <source>Megszakítva.</source>
        <translation>Cancelled.</translation>
    </message>
    <message>
        <source>%1 / %2 beszélő</source>
        <translation>%1 / %2 speakers</translation>
    </message>
    <message>
        <source>Sáv beemelve</source>
        <translation>Track added back</translation>
    </message>
    <message>
        <source>Sáv kivéve</source>
        <translation>Track removed</translation>
    </message>
    <message>
        <source>Csak én beszéltem</source>
        <translation>Only I spoke</translation>
    </message>
    <message>
        <source>Résztvevők jóváhagyása</source>
        <translation>Approve participants</translation>
    </message>
    <message>
        <source>Résztvevők jóváhagyva</source>
        <translation>Participants approved</translation>
    </message>
    <message>
        <source>Jóváhagyás kihagyva</source>
        <translation>Approval skipped</translation>
    </message>
    <message>
        <source>Nem volt ott</source>
        <translation>Was not there</translation>
    </message>
    <message>
        <source>Nem volt ott: %1</source>
        <translation>Not there: %1</translation>
    </message>
    <message>
        <source>Résztvevő felvéve: %1</source>
        <translation>Participant added: %1</translation>
    </message>
    <message>
        <source>Résztvevő eltávolítva</source>
        <translation>Participant removed</translation>
    </message>
    <message>
        <source>Résztvevő eltávolítva: %1</source>
        <translation>Participant removed: %1</translation>
    </message>
</context>
<context>
    <name>tanara::editing::SpeakerEditor</name>
    <message>
        <source>Új beszélő %1</source>
        <translation>New speaker %1</translation>
    </message>
    <message numerus="yes">
        <source>%1: %n sor ide: %2</source>
        <translation>
            <numerusform>%1: %n line to %2</numerusform>
            <numerusform>%1: %n lines to %2</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor áthelyezése ide: %1 (nem hangminta)</source>
        <translation>
            <numerusform>Move %n line to %1 (not a voice sample)</numerusform>
            <numerusform>Move %n lines to %1 (not a voice sample)</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor áthelyezése ide: %1</source>
        <translation>
            <numerusform>Move %n line to %1</numerusform>
            <numerusform>Move %n lines to %1</numerusform>
        </translation>
    </message>
    <message>
        <source>Beszélő cseréje: %1 → %2</source>
        <translation>Change speaker: %1 → %2</translation>
    </message>
    <message>
        <source>Névtelenre állítás: %1</source>
        <translation>Make anonymous: %1</translation>
    </message>
    <message>
        <source>Összevonás: %1 → %2</source>
        <translation>Merge: %1 → %2</translation>
    </message>
    <message>
        <source>Résztvevő hozzáadása: %1</source>
        <translation>Add participant: %1</translation>
    </message>
    <message>
        <source>Résztvevő eltávolítása: %1</source>
        <translation>Remove participant: %1</translation>
    </message>
    <message numerus="yes">
        <source>%n sor megerősítése (nem hangminta)</source>
        <translation>
            <numerusform>Confirm %n line (not a voice sample)</numerusform>
            <numerusform>Confirm %n lines (not a voice sample)</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor megerősítése</source>
        <translation>
            <numerusform>Confirm %n line</numerusform>
            <numerusform>Confirm %n lines</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor: nem hangminta</source>
        <translation>
            <numerusform>%n line: not a voice sample</numerusform>
            <numerusform>%n lines: not a voice sample</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor: mintának használható</source>
        <translation>
            <numerusform>%n line: usable as a sample</numerusform>
            <numerusform>%n lines: usable as a sample</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor: nem azonosítható</source>
        <translation>
            <numerusform>%n line: not identifiable</numerusform>
            <numerusform>%n lines: not identifiable</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor: újra azonosítható</source>
        <translation>
            <numerusform>%n line: identifiable again</numerusform>
            <numerusform>%n lines: identifiable again</numerusform>
        </translation>
    </message>
    <message>
        <source>Ennek a megbeszélésnek nincs szerkeszthető átirata.</source>
        <translation>This meeting has no editable transcript.</translation>
    </message>
    <message>
        <source>Az újraellenőrzéshez nincs telepítve a hangmodell.</source>
        <translation>The voice model is not installed, so lines cannot be re-checked.</translation>
    </message>
    <message>
        <source>A sorok hang-elemzése még fut — a végén újraellenőrizheted a sorokat.</source>
        <translation>The voice analysis of the lines is still running — you can re-check them when it finishes.</translation>
    </message>
    <message>
        <source>A sorok hang-elemzése még nem készült el. Nyisd meg az Átirat fület, és várd meg a végét.</source>
        <translation>The voice analysis of the lines is not done yet. Open the Transcript tab and wait for it to finish.</translation>
    </message>
    <message>
        <source>Előbb erősíts meg vagy javíts legalább %1 sort egy beszélőnél („Jó így” vagy áthelyezés) — ezek hangjához mérem a többit.</source>
        <translation>First confirm or correct at least %1 lines of one speaker (“Fine as is” or move) — the rest is compared with their voice.</translation>
    </message>
    <message>
        <source>Beszélők újraellenőrzése</source>
        <translation>Re-check speakers</translation>
    </message>
    <message>
        <source>%1 sor</source>
        <translation>%1 lines</translation>
    </message>
    <message>
        <source>%1 itteni lenyomat</source>
        <translation>%1 voiceprint from this meeting</translation>
    </message>
    <message>
        <source>%1 korábbi lenyomat</source>
        <translation>%1 earlier voiceprint</translation>
    </message>
    <message>
        <source>Referencia: %1.</source>
        <translation>Reference: %1.</translation>
    </message>
    <message numerus="yes">
        <source>%n megerősített sor</source>
        <translation>
            <numerusform>%n confirmed line</numerusform>
            <numerusform>%n confirmed lines</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n tárolt minta</source>
        <translation>
            <numerusform>%n stored sample</numerusform>
            <numerusform>%n stored samples</numerusform>
        </translation>
    </message>
    <message>
        <source>(ebből %1 erről a megbeszélésről)</source>
        <translation>(%1 of them from this meeting)</translation>
    </message>
    <message>
        <source>(mind erről a megbeszélésről)</source>
        <translation>(all from this meeting)</translation>
    </message>
    <message>
        <source>(korábbi megbeszélésekből)</source>
        <translation>(from earlier meetings)</translation>
    </message>
    <message>
        <source>tárolt minta nélkül</source>
        <translation>without a stored sample</translation>
    </message>
    <message>
        <source>modell: %1</source>
        <translation>model: %1</translation>
    </message>
    <message>
        <source>modellek: %1</source>
        <translation>models: %1</translation>
    </message>
    <message>
        <source>Alapja: %1.</source>
        <translation>Based on: %1.</translation>
    </message>
    <message>
        <source>Figyelem: %1 és %2 hangja nagyon hasonló (%3), kettejük sorait érdemes átnézni.</source>
        <translation>Note: the voices of %1 and %2 are very similar (%3); it is worth reviewing both of their lines.</translation>
    </message>
    <message numerus="yes">
        <source>(További %n hasonló pár.)</source>
        <translation>
            <numerusform>(%n more similar pair.)</numerusform>
            <numerusform>(%n more similar pairs.)</numerusform>
        </translation>
    </message>
    <message>
        <source>Válassz két különböző beszélőt ebből a megbeszélésből.</source>
        <translation>Choose two different speakers from this meeting.</translation>
    </message>
    <message>
        <source>Az átnézéshez nincs telepítve a hangmodell.</source>
        <translation>The voice model needed for the review is not installed.</translation>
    </message>
    <message>
        <source>A sorok hang-elemzése még fut — a végén átnézheted a két beszélőt.</source>
        <translation>The voice analysis of the lines is still running — you can review the two speakers when it finishes.</translation>
    </message>
    <message>
        <source>%1 hangjához kevés a minta: legalább %2 elég hosszú, tiszta sora kell.</source>
        <translation>Not enough samples of %1&apos;s voice: at least %2 long enough, clean lines are needed.</translation>
    </message>
    <message>
        <source>Átnézés: %1 és %2</source>
        <translation>Review: %1 and %2</translation>
    </message>
    <message numerus="yes">
        <source>Javaslat elfogadása: %n sor ide: %1</source>
        <translation>
            <numerusform>Accept suggestion: %n line to %1</numerusform>
            <numerusform>Accept suggestion: %n lines to %1</numerusform>
        </translation>
    </message>
    <message>
        <source>Ismeretlen beszélő.</source>
        <translation>Unknown speaker.</translation>
    </message>
    <message>
        <source>Előbb nevezd el a beszélőt — névtelen beszélőhöz nem készül hanglenyomat.</source>
        <translation>Name the speaker first — a voiceprint cannot be created for an anonymous speaker.</translation>
    </message>
    <message>
        <source>Nincs elég hanganyag a lenyomathoz: még kb. %1 mp beszéd kellene (legalább 3 mp-es sorokból).</source>
        <translation>Not enough audio for a voiceprint: about %1 s more speech is needed (from lines of at least 3 s).</translation>
    </message>
    <message>
        <source>A hangmodell vagy a megbeszélés hangja nem érhető el.</source>
        <translation>The voice model or the meeting audio is not available.</translation>
    </message>
    <message>
        <source>Ez a sor bizonytalan: előbb erősítsd meg („Jó így”) vagy helyezd át, utána lehet minta.</source>
        <translation>This line is uncertain: confirm it (“Fine as is”) or move it first, then it can be a sample.</translation>
    </message>
    <message>
        <source>Ez a sor „nem azonosítható” jelölésű, ezért nem használható hangmintának.</source>
        <translation>This line is marked “not identifiable”, so it cannot be used as a voice sample.</translation>
    </message>
    <message>
        <source>Ebben a sorban egymásra beszéltek, ezért nem használható hangmintának.</source>
        <translation>People talk over each other in this line, so it cannot be a voice sample.</translation>
    </message>
    <message>
        <source>Ez a sor túl rövid mintának (legalább 3 mp beszéd kell).</source>
        <translation>This line is too short for a sample (at least 3 s of speech is needed).</translation>
    </message>
    <message>
        <source>Nincs olyan sor, amelyből minta készülhetne.</source>
        <translation>There is no line a sample could be made from.</translation>
    </message>
    <message>
        <source>Sáv-beosztás törlése: %1</source>
        <translation>Delete track assignment: %1</translation>
    </message>
    <message>
        <source>Sáv-beosztás: %1</source>
        <translation>Track assignment: %1</translation>
    </message>
    <message>
        <source>Szétválasztás</source>
        <translation>Split</translation>
    </message>
    <message>
        <source>Átnézendő</source>
        <translation>To review</translation>
    </message>
</context>
<context>
    <name>tanara::library::AudioImporter</name>
    <message>
        <source>Nincs importálható fájl.</source>
        <translation>No file to import.</translation>
    </message>
    <message>
        <source>Nem hozható létre mappa a felvételek helyén.</source>
        <translation>Could not create a folder in the recordings location.</translation>
    </message>
    <message>
        <source>Nem sikerült beolvasni: %1</source>
        <translation>Could not read: %1</translation>
    </message>
    <message>
        <source>Nem sikerült elmenteni az importált felvételt.</source>
        <translation>Could not save the imported recording.</translation>
    </message>
</context>
<context>
    <name>tanara::library::EmbeddingPreparer</name>
    <message>
        <source>A beágyazó szolgáltató nem hozható létre.</source>
        <translation>The embedding provider could not be created.</translation>
    </message>
    <message>
        <source>A beágyazási válasz hiányos.</source>
        <translation>The embedding response is incomplete.</translation>
    </message>
    <message>
        <source>A beágyazás nem menthető (%1).</source>
        <translation>The embedding could not be saved (%1).</translation>
    </message>
</context>
<context>
    <name>tanara::library::MeetingLibrary</name>
    <message>
        <source>Ma</source>
        <translation>Today</translation>
    </message>
    <message>
        <source>Tegnap</source>
        <translation>Yesterday</translation>
    </message>
    <message>
        <source>Ezen a héten</source>
        <translation>This week</translation>
    </message>
    <message>
        <source>Korábban</source>
        <translation>Earlier</translation>
    </message>
</context>
<context>
    <name>tanara::library::PeopleService</name>
    <message>
        <source>Névtelen %1</source>
        <translation>Anonymous %1</translation>
    </message>
    <message>
        <source>Adj meg egy nevet.</source>
        <translation>Enter a name.</translation>
    </message>
    <message>
        <source>Ilyen nevű személy már van.</source>
        <translation>A person with this name already exists.</translation>
    </message>
    <message>
        <source>Ez a személy már nem létezik.</source>
        <translation>This person no longer exists.</translation>
    </message>
    <message>
        <source>A név nem lehet üres.</source>
        <translation>The name cannot be empty.</translation>
    </message>
    <message>
        <source>Ilyen nevű személy már van. Ha ugyanaz az ember, használd az Összevonást.</source>
        <translation>A person with this name already exists. If it is the same human, use Merge.</translation>
    </message>
    <message>
        <source>Adj meg egy becenevet.</source>
        <translation>Enter an alias.</translation>
    </message>
    <message>
        <source>Ez a személy neve — becenévnek mást adj meg.</source>
        <translation>This is the person&apos;s name — enter something else as an alias.</translation>
    </message>
    <message>
        <source>Ez a becenév már szerepel.</source>
        <translation>This alias is already listed.</translation>
    </message>
    <message>
        <source>Ez a minta már nincs meg.</source>
        <translation>This sample no longer exists.</translation>
    </message>
    <message>
        <source>A minta már ennél a személynél van.</source>
        <translation>The sample already belongs to this person.</translation>
    </message>
    <message>
        <source>A hangmodell nincs telepítve, ezért nem készíthető hanglenyomat.</source>
        <translation>The voice model is not installed, so a voiceprint cannot be created.</translation>
    </message>
    <message>
        <source>Ezen a megbeszélésen nincs elég hosszú, biztos sora.</source>
        <translation>There is no long enough, confident line of theirs in this meeting.</translation>
    </message>
    <message>
        <source>Az összevonáshoz két különböző, létező személy kell.</source>
        <translation>Merging needs two different, existing people.</translation>
    </message>
    <message>
        <source>A saját személyed nem törölhető.</source>
        <translation>Your own person cannot be deleted.</translation>
    </message>
</context>
<context>
    <name>tanara::library::TagService</name>
    <message>
        <source>Címke létrehozása</source>
        <translation>Create tag</translation>
    </message>
    <message>
        <source>Címke átnevezése</source>
        <translation>Rename tag</translation>
    </message>
    <message>
        <source>Címkék összevonása</source>
        <translation>Merge tags</translation>
    </message>
    <message>
        <source>Címke törlése</source>
        <translation>Delete tag</translation>
    </message>
    <message>
        <source>Címke hozzáadása</source>
        <translation>Add tag</translation>
    </message>
    <message>
        <source>Címke levétele</source>
        <translation>Remove tag</translation>
    </message>
    <message>
        <source>Címkék módosítása</source>
        <translation>Change tags</translation>
    </message>
    <message numerus="yes">
        <source>„%1” hozzáadása %n megbeszéléshez</source>
        <translation>
            <numerusform>Add &quot;%1&quot; to %n meeting</numerusform>
            <numerusform>Add &quot;%1&quot; to %n meetings</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>„%1” levétele %n megbeszélésről</source>
        <translation>
            <numerusform>Remove &quot;%1&quot; from %n meeting</numerusform>
            <numerusform>Remove &quot;%1&quot; from %n meetings</numerusform>
        </translation>
    </message>
    <message>
        <source>Javaslat elutasítása</source>
        <translation>Dismiss suggestion</translation>
    </message>
    <message>
        <source>Elutasított javaslatok visszaállítása</source>
        <translation>Restore dismissed suggestions</translation>
    </message>
    <message>
        <source>Személy címkéinek módosítása</source>
        <translation>Change a person&apos;s tags</translation>
    </message>
    <message>
        <source>Címke a személyen</source>
        <translation>Tag on a person</translation>
    </message>
    <message>
        <source>Címke levétele a személyről</source>
        <translation>Remove tag from a person</translation>
    </message>
    <message>
        <source>Címke a személyeken: #%1</source>
        <translation>Tag on the people: #%1</translation>
    </message>
    <message>
        <source>Javaslat elutasítása: %1</source>
        <translation>Suggestion rejected: %1</translation>
    </message>
</context>
<context>
    <name>tanara::library::TrackCatalog</name>
    <message>
        <source>Nincs meeting-tár.</source>
        <translation>No meeting store.</translation>
    </message>
    <message>
        <source>Ismeretlen meeting: %1</source>
        <translation>Unknown meeting: %1</translation>
    </message>
    <message>
        <source>Ismeretlen sáv: %1</source>
        <translation>Unknown track: %1</translation>
    </message>
    <message>
        <source>A kiválasztott fájl nem olvasható: %1</source>
        <translation>The selected file cannot be read: %1</translation>
    </message>
    <message>
        <source>A kiválasztott fájl üres: %1</source>
        <translation>The selected file is empty: %1</translation>
    </message>
    <message>
        <source>A kiválasztott fájl nem hangfájl: %1</source>
        <translation>The selected file is not an audio file: %1</translation>
    </message>
    <message>
        <source>A lekeverés fájlja nem választható sávnak.</source>
        <translation>The mixdown file cannot be chosen as a track.</translation>
    </message>
    <message>
        <source>Ez a fájl már egy másik sávhoz tartozik.</source>
        <translation>This file already belongs to another track.</translation>
    </message>
    <message>
        <source>Nem sikerült a fájlt a megbeszélés mappájába másolni.</source>
        <translation>Could not copy the file into the meeting folder.</translation>
    </message>
</context>
<context>
    <name>tanara::library::WaveformService</name>
    <message>
        <source>A hangfájl nem található.</source>
        <translation>The audio file was not found.</translation>
    </message>
    <message>
        <source>Az ffmpeg nem indítható.</source>
        <translation>Could not start ffmpeg.</translation>
    </message>
    <message>
        <source>A hullámforma számítása nem sikerült.</source>
        <translation>Could not compute the waveform.</translation>
    </message>
</context>
<context>
    <name>tanara::processing::ComplexSummaryService</name>
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
    <name>tanara::processing::LlmTagSuggester</name>
    <message>
        <source>Nincs nyelvi modell beállítva.</source>
        <translation>No language model configured.</translation>
    </message>
</context>
<context>
    <name>tanara::processing::SummaryService</name>
    <message>
        <source>Nincs beállított LLM provider.</source>
        <translation>No LLM provider configured.</translation>
    </message>
    <message>
        <source>Az átirat üres — nincs mit összefoglalni.</source>
        <translation>The transcript is empty — there is nothing to summarise.</translation>
    </message>
    <message>
        <source>A provider nem adott vissza jobot.</source>
        <translation>The provider did not return a job.</translation>
    </message>
    <message>
        <source>A(z) %1/%2. rész jegyzete üres vagy nem értelmezhető.</source>
        <translation>The notes of part %1/%2 are empty or cannot be read.</translation>
    </message>
    <message>
        <source>Az összegzés válasza nem értelmezhető: %1. A részjegyzetek megmaradtak — újrapróbáláskor csak az összegzés fut le újra.</source>
        <translation>The summing-up response cannot be read: %1. The part notes are kept — a retry runs only the summing-up again.</translation>
    </message>
    <message>
        <source>Az összefoglaló válasza nem értelmezhető: %1. A jegyzet megmaradt — újrapróbáláskor csak az összegzés fut le újra.</source>
        <translation>The summary response cannot be read: %1. The notes are kept — a retry runs only the summing-up again.</translation>
    </message>
    <message>
        <source>Az összefoglaló válasza nem értelmezhető: %1.</source>
        <translation>The summary response cannot be read: %1.</translation>
    </message>
</context>
<context>
    <name>tanara::providers::OpenAiCompatibleEmbeddingJob</name>
    <message>
        <source>Hiányzik a beágyazó végpont címe.</source>
        <translation>The embedding endpoint address is missing.</translation>
    </message>
    <message>
        <source>Hálózati hiba: %1</source>
        <translation>Network error: %1</translation>
    </message>
    <message>
        <source>Beágyazási hiba (HTTP %1): %2</source>
        <translation>Embedding error (HTTP %1): %2</translation>
    </message>
    <message>
        <source>Beágyazási hiba (HTTP %1).</source>
        <translation>Embedding error (HTTP %1).</translation>
    </message>
    <message>
        <source>Érvénytelen beágyazási válasz (%1 vektor jött %2 szövegre).</source>
        <translation>Invalid embedding response (%1 vectors for %2 texts).</translation>
    </message>
    <message>
        <source>Érvénytelen beágyazási válasz (hiányzó vektor).</source>
        <translation>Invalid embedding response (missing vector).</translation>
    </message>
</context>
<context>
    <name>tanara::providers::OpenAiCompatibleJob</name>
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
        <source>A modell nem adott választ, csak „gondolkodott” (%1 karakter, finish=%2). Kapcsold ki a gondolkodást a modell beállításainál, vagy növeld a max. tokenszámot.</source>
        <translation>The model gave no answer, it only “reasoned” (%1 characters, finish=%2). Switch reasoning off in the model settings, or raise the max. tokens.</translation>
    </message>
    <message>
        <source>Üres LLM-válasz (finish=%1).</source>
        <translation>Empty LLM response (finish=%1).</translation>
    </message>
</context>
<context>
    <name>tanara::providers::SonioxJob</name>
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
    <name>tanara::providers::WhisperCompatJob</name>
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
    <name>tanara::recording::RecordingSession</name>
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
    <name>tanara_gui::QmlShellBridge</name>
    <message>
        <source>Hangfájl kiválasztása</source>
        <translation>Select audio file</translation>
    </message>
    <message>
        <source>Hangfájlok (*.ogg *.opus *.flac *.wav *.mp3 *.m4a *.aac);;Minden fájl (*)</source>
        <translation>Audio files (*.ogg *.opus *.flac *.wav *.mp3 *.m4a *.aac);;All files (*)</translation>
    </message>
    <message>
        <source>Importálandó hangfájlok kiválasztása</source>
        <translation>Choose audio files to import</translation>
    </message>
    <message>
        <source>Hang- és videófájlok (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.wma *.aiff *.aif *.amr *.mp4 *.mov *.mkv *.webm *.avi *.m4v *.3gp);;Minden fájl (*)</source>
        <translation>Audio and video files (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.wma *.aiff *.aif *.amr *.mp4 *.mov *.mkv *.webm *.avi *.m4v *.3gp);;All files (*)</translation>
    </message>
    <message>
        <source>Megbeszélés-archívum kiválasztása</source>
        <translation>Choose a meeting archive</translation>
    </message>
    <message>
        <source>Tanara-archívum (*.tanara.zip *.zip);;Minden fájl (*)</source>
        <translation>Tanara archive (*.tanara.zip *.zip);;All files (*)</translation>
    </message>
    <message>
        <source>Megbeszélés mappájának kiválasztása</source>
        <translation>Choose the meeting folder</translation>
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
        <translation>Identifying participants</translation>
    </message>
</context>
<context>
    <name>tanara_qml::BackfillSkipNote</name>
    <message>
        <source>Partnerdemó – adatimport</source>
        <translation>Partner demo – data import</translation>
    </message>
    <message numerus="yes">
        <source>%n minta nem pótolható az új modellel</source>
        <translation>
            <numerusform>%n sample cannot be backfilled with the new model</numerusform>
            <numerusform>%n samples cannot be backfilled with the new model</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n minta nem pótolható: %1</source>
        <translation>
            <numerusform>%n sample cannot be backfilled: %1</numerusform>
            <numerusform>%n samples cannot be backfilled: %1</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>tanara_qml::CandidateListModel</name>
    <message>
        <source>a sávja szerint nem ő</source>
        <translation>not them, per their track</translation>
    </message>
    <message>
        <source>most ő</source>
        <translation>currently them</translation>
    </message>
    <message numerus="yes">
        <source>%n sor itt</source>
        <translation>
            <numerusform>%n line here</numerusform>
            <numerusform>%n lines here</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélés</source>
        <translation>
            <numerusform>%n meeting</numerusform>
            <numerusform>%n meetings</numerusform>
        </translation>
    </message>
    <message>
        <source>„%1”</source>
        <translation>&quot;%1&quot;</translation>
    </message>
</context>
<context>
    <name>tanara_qml::LibraryListModel</name>
    <message>
        <source>Átirat: kész</source>
        <translation>Transcript: done</translation>
    </message>
    <message>
        <source>Átirat: készül…</source>
        <translation>Transcript: in progress…</translation>
    </message>
    <message>
        <source>Átirat: nem sikerült</source>
        <translation>Transcript: failed</translation>
    </message>
    <message>
        <source>Átirat: még nincs</source>
        <translation>Transcript: none yet</translation>
    </message>
    <message>
        <source>Összefoglaló: elavult (a beszélők változtak)</source>
        <translation>Summary: outdated (speakers changed)</translation>
    </message>
    <message>
        <source>Összefoglaló: kész</source>
        <translation>Summary: done</translation>
    </message>
    <message>
        <source>Összefoglaló: készül…</source>
        <translation>Summary: in progress…</translation>
    </message>
    <message>
        <source>Összefoglaló: nem sikerült</source>
        <translation>Summary: failed</translation>
    </message>
    <message>
        <source>Összefoglaló: még nincs</source>
        <translation>Summary: none yet</translation>
    </message>
    <message>
        <source>Résztvevők: azonosítva</source>
        <translation>Participants: identified</translation>
    </message>
    <message>
        <source>Résztvevők: azonosítás folyamatban…</source>
        <translation>Participants: identifying…</translation>
    </message>
    <message>
        <source>Résztvevők: nincs azonosítva</source>
        <translation>Participants: not identified</translation>
    </message>
    <message>
        <source>címben</source>
        <translation>in title</translation>
    </message>
    <message>
        <source>címkében</source>
        <translation>in tag</translation>
    </message>
    <message>
        <source>átiratban</source>
        <translation>in transcript</translation>
    </message>
    <message numerus="yes">
        <source>%n találat</source>
        <translation>
            <numerusform>%n result</numerusform>
            <numerusform>%n results</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélés a szűrő szerint</source>
        <translation>
            <numerusform>%n meeting matches the filter</numerusform>
            <numerusform>%n meetings match the filter</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>tanara_qml::LibraryPendingModel</name>
    <message>
        <source>Legutóbbi: %1</source>
        <translation>Latest: %1</translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélés átírásra vár</source>
        <translation>
            <numerusform>%n meeting awaiting transcription</numerusform>
            <numerusform>%n meetings awaiting transcription</numerusform>
        </translation>
    </message>
    <message>
        <source>Megnyitás</source>
        <translation>Open</translation>
    </message>
    <message>
        <source>Elavult összefoglaló</source>
        <translation>Outdated summary</translation>
    </message>
    <message>
        <source>a beszélők változtak</source>
        <translation>speakers changed</translation>
    </message>
    <message>
        <source>Frissítés</source>
        <translation>Update</translation>
    </message>
    <message>
        <source>Sikertelen átírás</source>
        <translation>Failed transcription</translation>
    </message>
    <message>
        <source>Megnézem</source>
        <translation>View</translation>
    </message>
</context>
<context>
    <name>tanara_qml::LibrarySelectionModel</name>
    <message>
        <source>mindegyiken rajta van</source>
        <translation>on all of them</translation>
    </message>
    <message>
        <source>csak egyiken</source>
        <translation>only on one</translation>
    </message>
    <message>
        <source>%1 megbeszélésen</source>
        <translation>on %1 meetings</translation>
    </message>
    <message>
        <source>kattintásra mindegyikre</source>
        <translation>click to add to all</translation>
    </message>
    <message numerus="yes">
        <source>Címke hozzáadása %n megbeszéléshez</source>
        <translation>
            <numerusform>Add tag to %n meeting</numerusform>
            <numerusform>Add tag to %n meetings</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Címke levétele %n megbeszélésről</source>
        <translation>
            <numerusform>Remove tag from %n meeting</numerusform>
            <numerusform>Remove tag from %n meetings</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>tanara_qml::MeetingTagsModel</name>
    <message>
        <source>%1 mellé gyakran</source>
        <translation>Often with %1</translation>
    </message>
    <message>
        <source>Az összefoglaló alapján</source>
        <translation>Based on the summary</translation>
    </message>
    <message>
        <source>A cím alapján</source>
        <translation>Based on the title</translation>
    </message>
    <message>
        <source>A résztvevők alapján</source>
        <translation>Based on the participants</translation>
    </message>
    <message>
        <source>Javasolt</source>
        <translation>Suggested</translation>
    </message>
    <message>
        <source>Címke hozzáadva: #%1</source>
        <translation>Tag added: #%1</translation>
    </message>
    <message>
        <source>Címke eltávolítva: #%1</source>
        <translation>Tag removed: #%1</translation>
    </message>
    <message>
        <source>Javaslat elfogadva: #%1</source>
        <translation>Suggestion accepted: #%1</translation>
    </message>
    <message>
        <source>Javaslat elutasítva: #%1</source>
        <translation>Suggestion dismissed: #%1</translation>
    </message>
    <message>
        <source>%1 javaslat elfogadva</source>
        <translation>%1 suggestions accepted</translation>
    </message>
    <message>
        <source>Közös résztvevő</source>
        <translation>Shared participant</translation>
    </message>
    <message>
        <source>Hasonló cím</source>
        <translation>Similar title</translation>
    </message>
    <message>
        <source>Résztvevők címkéje</source>
        <translation>Participants&apos; tag</translation>
    </message>
    <message>
        <source>Közös kifejezések</source>
        <translation>Shared terms</translation>
    </message>
    <message>
        <source> (mindkettőjükön)</source>
        <translation> (on both of them)</translation>
    </message>
    <message>
        <source> (mindhármukon)</source>
        <translation> (on all three)</translation>
    </message>
    <message numerus="yes">
        <source> (%n résztvevőn)</source>
        <translation>
            <numerusform> (on %n participant)</numerusform>
            <numerusform> (on %n participants)</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>tanara_qml::OnboardingViewModel</name>
    <message>
        <source>Átírás</source>
        <translation>Transcription</translation>
    </message>
    <message>
        <source>Összefoglaló</source>
        <translation>Summary</translation>
    </message>
    <message>
        <source>nincs bejelentkezve</source>
        <translation>not signed in</translation>
    </message>
    <message>
        <source>nincs kulcs</source>
        <translation>no key</translation>
    </message>
    <message>
        <source>hiányos beállítás</source>
        <translation>incomplete setup</translation>
    </message>
    <message>
        <source>Üdvözlés</source>
        <translation>Welcome</translation>
    </message>
    <message>
        <source>Te</source>
        <translation>You</translation>
    </message>
    <message>
        <source>Mappák</source>
        <translation>Folders</translation>
    </message>
    <message>
        <source>Szolgáltatások</source>
        <translation>Services</translation>
    </message>
    <message>
        <source>Hívásfigyelő</source>
        <translation>Call watcher</translation>
    </message>
    <message>
        <source>Kész</source>
        <translation>Done</translation>
    </message>
    <message>
        <source>Adj meg egy nevet — ezen a néven szerepelsz az átiratokban.</source>
        <translation>Enter a name — this is how you appear in transcripts.</translation>
    </message>
    <message>
        <source>Rendszer nyelve (magyar)</source>
        <translation>System language (Hungarian)</translation>
    </message>
    <message>
        <source>Rendszer nyelve (English)</source>
        <translation>System language (English)</translation>
    </message>
    <message>
        <source>Felvételek</source>
        <translation>Recordings</translation>
    </message>
    <message>
        <source>Hangsávok, megbeszélésenként egy mappa. Az új felvételek ide kerülnek.</source>
        <translation>Audio tracks, one folder per meeting. New recordings go here.</translation>
    </message>
    <message>
        <source>Jegyzetek</source>
        <translation>Notes</translation>
    </message>
    <message>
        <source>Az összefoglalók másolata sima Markdown-fájlként (pl. a jegyzettáradba).</source>
        <translation>Copies of the summaries as plain Markdown files (for example into your notes vault).</translation>
    </message>
    <message>
        <source>Felvételek mappája</source>
        <translation>Recordings folder</translation>
    </message>
    <message>
        <source>Jegyzetek mappája</source>
        <translation>Notes folder</translation>
    </message>
    <message>
        <source>Linuxon egy indítófájl kerül a ~/.config/autostart mappába; kikapcsoláskor törlődik.</source>
        <translation>On Linux a launcher file is placed in ~/.config/autostart; it is removed when switched off.</translation>
    </message>
    <message>
        <source>Windowson egy bejegyzés kerül a felhasználó indítási listájába (Run); kikapcsoláskor törlődik.</source>
        <translation>On Windows an entry is added to the user’s startup list (Run); it is removed when switched off.</translation>
    </message>
    <message>
        <source>Ezen a rendszeren az automatikus indítás még nem érhető el; a figyelőt kézzel indíthatod.</source>
        <translation>Automatic start is not available on this system yet; you can start the watcher by hand.</translation>
    </message>
    <message>
        <source>Most nem íródik bejegyzés (elkülönített adatmappával fut a Tanara).</source>
        <translation>No entry is written now (Tanara runs with a separate data folder).</translation>
    </message>
</context>
<context>
    <name>tanara_qml::OverviewViewModel</name>
    <message>
        <source>te</source>
        <translation>you</translation>
    </message>
    <message>
        <source>A Beállításokban megadott saját neved</source>
        <translation>Your own name entered in Settings</translation>
    </message>
    <message>
        <source>kézi</source>
        <translation>manual</translation>
    </message>
    <message>
        <source>Kézzel adtad hozzá</source>
        <translation>You added them manually</translation>
    </message>
    <message>
        <source>naptár</source>
        <translation>calendar</translation>
    </message>
    <message>
        <source>mikrofon</source>
        <translation>microphone</translation>
    </message>
    <message>
        <source>A saját mikrofon sávján beszél</source>
        <translation>Speaks on your own microphone track</translation>
    </message>
    <message>
        <source>hívás</source>
        <translation>call</translation>
    </message>
    <message>
        <source>A hívás hangján (távoli oldal) beszél</source>
        <translation>Speaks on the call audio (remote side)</translation>
    </message>
    <message>
        <source>Az átiratban nem találtuk a hangját: nincs beszélő a nevén</source>
        <translation>Their voice was not found in the transcript: no speaker carries their name</translation>
    </message>
    <message>
        <source>%1 (te)</source>
        <translation>%1 (you)</translation>
    </message>
    <message>
        <source>névtelen · elnevezhető</source>
        <translation>unnamed · can be named</translation>
    </message>
    <message>
        <source>Ismeretlen hang %1</source>
        <translation>Unknown voice %1</translation>
    </message>
    <message>
        <source>javasolt</source>
        <translation>suggested</translation>
    </message>
    <message>
        <source>a #%1 alapján · %2 / %3</source>
        <translation>based on #%1 · %2 / %3</translation>
    </message>
    <message>
        <source>csak én beszéltem</source>
        <translation>only I spoke</translation>
    </message>
    <message>
        <source>jóváhagyva %1-kor</source>
        <translation>approved at %1</translation>
    </message>
    <message>
        <source>jóváhagyva</source>
        <translation>approved</translation>
    </message>
    <message>
        <source>naptár · hang · címke · kézi</source>
        <translation>calendar · voice · tag · manual</translation>
    </message>
    <message>
        <source>névtelen</source>
        <translation>unnamed</translation>
    </message>
    <message numerus="yes">
        <source>%n résztvevő</source>
        <translation>
            <numerusform>%n participant</numerusform>
            <numerusform>%n participants</numerusform>
        </translation>
    </message>
    <message>
        <source>Felvétel</source>
        <translation>Recording</translation>
    </message>
    <message numerus="yes">
        <source>%1 · %n sáv</source>
        <translation>
            <numerusform>%1 · %n track</numerusform>
            <numerusform>%1 · %n tracks</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n aktív</source>
        <translation>
            <numerusform>%n active</numerusform>
            <numerusform>%n active</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n kimaradt, nincs rajta beszéd</source>
        <translation>
            <numerusform>%n skipped, no speech</numerusform>
            <numerusform>%n skipped, no speech</numerusform>
        </translation>
    </message>
    <message>
        <source>Sávok</source>
        <translation>Tracks</translation>
    </message>
    <message>
        <source>Lekeverés</source>
        <translation>Mixdown</translation>
    </message>
    <message>
        <source>fut · %1%</source>
        <translation>running · %1%</translation>
    </message>
    <message>
        <source>a sávok változtak azóta</source>
        <translation>the tracks have changed since</translation>
    </message>
    <message numerus="yes">
        <source>%n sávból</source>
        <translation>
            <numerusform>from %n track</numerusform>
            <numerusform>from %n tracks</numerusform>
        </translation>
    </message>
    <message>
        <source>%1 / %2 sáv</source>
        <translation>%1 / %2 tracks</translation>
    </message>
    <message>
        <source>fut</source>
        <translation>running</translation>
    </message>
    <message numerus="yes">
        <source>%n jelölt · jóváhagyásra vár</source>
        <translation>
            <numerusform>%n candidate · awaiting approval</numerusform>
            <numerusform>%n candidates · awaiting approval</numerusform>
        </translation>
    </message>
    <message>
        <source>nincs ismert hang</source>
        <translation>no known voice</translation>
    </message>
    <message>
        <source>nem futott</source>
        <translation>did not run</translation>
    </message>
    <message>
        <source>Hanglenyomat-elemzés</source>
        <translation>Voiceprint analysis</translation>
    </message>
    <message numerus="yes">
        <source>kb. %n perc</source>
        <translation>
            <numerusform>about %n minute</numerusform>
            <numerusform>about %n minutes</numerusform>
        </translation>
    </message>
    <message>
        <source>Átirat</source>
        <translation>Transcript</translation>
    </message>
    <message numerus="yes">
        <source>%n megszólalás</source>
        <translation>
            <numerusform>%n utterance</numerusform>
            <numerusform>%n utterances</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n beszélő</source>
        <translation>
            <numerusform>%n speaker</numerusform>
            <numerusform>%n speakers</numerusform>
        </translation>
    </message>
    <message>
        <source>kész</source>
        <translation>done</translation>
    </message>
    <message>
        <source>nem sikerült</source>
        <translation>failed</translation>
    </message>
    <message>
        <source>még nem indult</source>
        <translation>not started yet</translation>
    </message>
    <message>
        <source>Összefoglaló</source>
        <translation>Summary</translation>
    </message>
    <message>
        <source>elavult</source>
        <translation>outdated</translation>
    </message>
    <message>
        <source>még nem készült</source>
        <translation>not created yet</translation>
    </message>
    <message>
        <source>az átirat után</source>
        <translation>after the transcript</translation>
    </message>
    <message>
        <source>felvétel · lekeverés · átirat · összefoglaló</source>
        <translation>recording · mixdown · transcript · summary</translation>
    </message>
    <message>
        <source>HOSSZ</source>
        <translation>LENGTH</translation>
    </message>
    <message>
        <source>felvétel</source>
        <translation>recording</translation>
    </message>
    <message>
        <source>BESZÉD</source>
        <translation>SPEECH</translation>
    </message>
    <message numerus="yes">
        <source>%n sávon</source>
        <translation>
            <numerusform>on %n track</numerusform>
            <numerusform>on %n tracks</numerusform>
        </translation>
    </message>
    <message>
        <source>BESZÉLŐK</source>
        <translation>SPEAKERS</translation>
    </message>
    <message numerus="yes">
        <source>%n névtelen</source>
        <translation>
            <numerusform>%n unnamed</numerusform>
            <numerusform>%n unnamed</numerusform>
        </translation>
    </message>
    <message>
        <source>mind elnevezve</source>
        <translation>all named</translation>
    </message>
    <message>
        <source>MEGSZÓLALÁS</source>
        <translation>UTTERANCES</translation>
    </message>
    <message numerus="yes">
        <source>%n javítva</source>
        <translation>
            <numerusform>%n corrected</numerusform>
            <numerusform>%n corrected</numerusform>
        </translation>
    </message>
    <message>
        <source>ÁTNÉZENDŐ</source>
        <translation>TO REVIEW</translation>
    </message>
    <message>
        <source>az Átirat fülön</source>
        <translation>in the Transcript tab</translation>
    </message>
    <message>
        <source>ÖSSZEFOGLALÓ</source>
        <translation>SUMMARY</translation>
    </message>
    <message>
        <source>nincs</source>
        <translation>none</translation>
    </message>
    <message>
        <source>Közös címke: %1</source>
        <translation>Tag in common: %1</translation>
    </message>
    <message>
        <source>hang 84%</source>
        <translation>voice 84%</translation>
    </message>
    <message>
        <source>A hangja 84%-ban hasonlít a mentett mintáihoz (2 modell)</source>
        <translation>Their voice is 84% similar to their saved samples (2 models)</translation>
    </message>
    <message>
        <source>hang 79%</source>
        <translation>voice 79%</translation>
    </message>
    <message>
        <source>hang 71%</source>
        <translation>voice 71%</translation>
    </message>
    <message>
        <source>nem hallottuk</source>
        <translation>not heard</translation>
    </message>
    <message>
        <source>nincs hangmodell</source>
        <translation>no voice model</translation>
    </message>
    <message>
        <source>Beszélő 1</source>
        <translation>Speaker 1</translation>
    </message>
    <message>
        <source>Beszélő 2</source>
        <translation>Speaker 2</translation>
    </message>
    <message>
        <source>Beszélő 3</source>
        <translation>Speaker 3</translation>
    </message>
    <message numerus="yes">
        <source>%n sáv</source>
        <translation>
            <numerusform>%n track</numerusform>
            <numerusform>%n tracks</numerusform>
        </translation>
    </message>
    <message>
        <source>feltöltés</source>
        <translation>upload</translation>
    </message>
    <message>
        <source>sáv</source>
        <translation>track</translation>
    </message>
</context>
<context>
    <name>tanara_qml::ParticipantTrackBinder</name>
    <message>
        <source>Saját mikrofon</source>
        <translation>My microphone</translation>
    </message>
    <message>
        <source>Hívás hangja</source>
        <translation>Call audio</translation>
    </message>
    <message>
        <source>mikrofon</source>
        <translation>microphone</translation>
    </message>
    <message>
        <source>hívás</source>
        <translation>call</translation>
    </message>
</context>
<context>
    <name>tanara_qml::ParticipantsViewModel</name>
    <message>
        <source>most</source>
        <translation>now</translation>
    </message>
    <message numerus="yes">
        <source>%n perce</source>
        <translation>
            <numerusform>%n minute ago</numerusform>
            <numerusform>%n minutes ago</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n órája</source>
        <translation>
            <numerusform>%n hour ago</numerusform>
            <numerusform>%n hours ago</numerusform>
        </translation>
    </message>
    <message>
        <source>tegnap</source>
        <translation>yesterday</translation>
    </message>
    <message>
        <source>hanglenyomat-elemzés</source>
        <translation>voiceprint analysis</translation>
    </message>
    <message>
        <source>kézzel felvéve</source>
        <translation>added manually</translation>
    </message>
    <message>
        <source>A #%1 alapján:</source>
        <translation>Based on #%1:</translation>
    </message>
    <message>
        <source>%1 / %2 megbeszélésen</source>
        <translation>in %1 / %2 meetings</translation>
    </message>
    <message>
        <source>-sáv</source>
        <translation> track</translation>
    </message>
    <message>
        <source>átirat után</source>
        <translation>after transcript</translation>
    </message>
    <message>
        <source>Ismeretlen hang</source>
        <translation>Unknown voice</translation>
    </message>
    <message>
        <source>%1 (te)</source>
        <translation>%1 (you)</translation>
    </message>
    <message>
        <source>BIZTOS</source>
        <translation>CERTAIN</translation>
    </message>
    <message>
        <source>KÉTSÉGES</source>
        <translation>DOUBTFUL</translation>
    </message>
    <message>
        <source>MEGHÍVOTT, DE NEM HALLOTTUK</source>
        <translation>INVITED, BUT NOT HEARD</translation>
    </message>
    <message>
        <source>KÉZZEL FELVÉVE</source>
        <translation>ADDED MANUALLY</translation>
    </message>
    <message>
        <source>a hang mellett még egy forrás egyezik</source>
        <translation>another source matches besides the voice</translation>
    </message>
    <message>
        <source>amit valami cáfol, alapból nincs bejelölve</source>
        <translation>anything contradicted is unchecked by default</translation>
    </message>
    <message>
        <source>a hangját még nem hallottuk</source>
        <translation>their voice has not been heard yet</translation>
    </message>
    <message>
        <source>A hanglenyomat-elemzés fut; a jelöltek pár másodperc múlva itt lesznek.</source>
        <translation>Voiceprint analysis is running; the candidates will be here in a few seconds.</translation>
    </message>
    <message>
        <source>Az átirat ebből köti a beszélőket, amikor megérkezik; a kimaradók névtelenek maradnak.</source>
        <translation>The transcript uses this to match speakers when it arrives; anyone left out stays unnamed.</translation>
    </message>
    <message>
        <source>Minden nyers beszélő résztvevőhöz kötődik.</source>
        <translation>Every raw speaker is linked to a participant.</translation>
    </message>
    <message>
        <source>A %1. nyers beszélő senkihez sincs kötve, ezért névtelen marad.</source>
        <translation>Raw speaker %1 is not linked to anyone, so it stays unnamed.</translation>
    </message>
    <message>
        <source>„%1” senkihez sincs kötve, ezért névtelen marad.</source>
        <translation>&quot;%1&quot; is not linked to anyone, so it stays unnamed.</translation>
    </message>
    <message numerus="yes">
        <source>%n nyers beszélő senkihez sincs kötve (%1), ezért névtelenek maradnak.</source>
        <translation>
            <numerusform>%n raw speaker is not linked to anyone (%1), so it stays unnamed.</numerusform>
            <numerusform>%n raw speakers are not linked to anyone (%1), so they stay unnamed.</numerusform>
        </translation>
    </message>
    <message>
        <source>A címkék csak sorrendet adnak; a hang dönt.</source>
        <translation>Tags only set the order; the voice decides.</translation>
    </message>
</context>
<context>
    <name>tanara_qml::PeopleViewModel</name>
    <message>
        <source>%1 / %2 megbeszélés…</source>
        <translation>%1 / %2 meetings…</translation>
    </message>
    <message>
        <source>„%1”</source>
        <translation>&quot;%1&quot;</translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélés</source>
        <translation>
            <numerusform>%n meeting</numerusform>
            <numerusform>%n meetings</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n minta</source>
        <translation>
            <numerusform>%n sample</numerusform>
            <numerusform>%n samples</numerusform>
        </translation>
    </message>
    <message>
        <source>nincs hanglenyomat</source>
        <translation>no voiceprint</translation>
    </message>
    <message>
        <source>címke: #%1</source>
        <translation>tag: #%1</translation>
    </message>
    <message>
        <source>Te</source>
        <translation>You</translation>
    </message>
    <message>
        <source>Többiek</source>
        <translation>Others</translation>
    </message>
    <message>
        <source>Megbeszélések számolása…</source>
        <translation>Counting meetings…</translation>
    </message>
    <message>
        <source>Még egy megbeszélésen sem szerepel</source>
        <translation>Does not appear in any meeting yet</translation>
    </message>
    <message>
        <source>Utoljára: %1</source>
        <translation>Last seen: %1</translation>
    </message>
    <message>
        <source>%1 beszéd</source>
        <translation>%1 of speech</translation>
    </message>
    <message>
        <source>A megbeszélés lekevert hangja</source>
        <translation>The meeting&apos;s mixed-down audio</translation>
    </message>
    <message>
        <source>ismeretlen eszköz</source>
        <translation>unknown device</translation>
    </message>
    <message>
        <source>A megbeszélés már nincs meg</source>
        <translation>The meeting no longer exists</translation>
    </message>
    <message numerus="yes">
        <source>%n forrásból</source>
        <translation>
            <numerusform>from %n source</numerusform>
            <numerusform>from %n sources</numerusform>
        </translation>
    </message>
    <message>
        <source>%1/%2 modell</source>
        <translation>%1/%2 models</translation>
    </message>
    <message>
        <source>Megvan: %1</source>
        <translation>Present: %1</translation>
    </message>
    <message>
        <source>Hiányzik: %1</source>
        <translation>Missing: %1</translation>
    </message>
    <message>
        <source>A javaslathoz legalább két megbeszélés kell.</source>
        <translation>A suggestion needs at least two meetings.</translation>
    </message>
    <message>
        <source>%1 #%2 %3 megbeszéléséből %4 alkalommal ott volt.</source>
        <translation>%1 was there in %4 of the %3 meetings tagged #%2.</translation>
    </message>
    <message>
        <source>Címke hozzáadva: #%1</source>
        <translation>Tag added: #%1</translation>
    </message>
    <message>
        <source>Címke levéve: #%1</source>
        <translation>Tag removed: #%1</translation>
    </message>
    <message numerus="yes">
        <source>%n javasolt címke hozzáadva</source>
        <translation>
            <numerusform>%n suggested tag added</numerusform>
            <numerusform>%n suggested tags added</numerusform>
        </translation>
    </message>
    <message>
        <source>Javaslat elutasítva: #%1</source>
        <translation>Suggestion dismissed: #%1</translation>
    </message>
    <message numerus="yes">
        <source>Kézzel rendelted hozzá %n megbeszélés sorait, ezért eddig nem ismerjük fel magától. A biztos sorokból mintát vehetünk.</source>
        <translation>
            <numerusform>You assigned the lines of %n meeting by hand, so we do not recognise them automatically yet. We can take a sample from the confident lines.</numerusform>
            <numerusform>You assigned the lines of %n meetings by hand, so we do not recognise them automatically yet. We can take samples from the confident lines.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Minta %n megbeszélésből</source>
        <translation>
            <numerusform>Sample from %n meeting</numerusform>
            <numerusform>Samples from %n meetings</numerusform>
        </translation>
    </message>
    <message>
        <source>kb. %1 hang, a gépen marad</source>
        <translation>approx. %1 of audio, stays on this computer</translation>
    </message>
    <message numerus="yes">
        <source>%n perc</source>
        <translation>
            <numerusform>%n minute</numerusform>
            <numerusform>%n minutes</numerusform>
        </translation>
    </message>
    <message>
        <source>Még egy megbeszélésen sincsenek sorai. Ha az átiratban hozzárendeled a sorait, azokból mintát vehetünk.</source>
        <translation>They have no lines in any meeting yet. Once you assign their lines in a transcript, we can take a sample from them.</translation>
    </message>
    <message numerus="yes">
        <source>Kézzel rendelted hozzá %n megbeszélés sorait, de egyiken sincs elég hosszú, biztos sora egy mintához (megbeszélésenként legalább 15 másodpercnyi kell, 3 másodpercnél hosszabb sorokból).</source>
        <translation>
            <numerusform>You assigned the lines of %n meeting by hand, but it has no long enough, confident lines for a sample (at least 15 seconds per meeting are needed, from lines longer than 3 seconds).</numerusform>
            <numerusform>You assigned the lines of %n meetings by hand, but none of them has long enough, confident lines for a sample (at least 15 seconds per meeting are needed, from lines longer than 3 seconds).</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Kézzel rendelted hozzá %n megbeszélés sorait. A hangfelismerő modell nincs telepítve ezen a gépen, ezért most nem készíthető hanglenyomat.</source>
        <translation>
            <numerusform>You assigned the lines of %n meeting by hand. The voice recognition model is not installed on this computer, so a voiceprint cannot be created now.</numerusform>
            <numerusform>You assigned the lines of %n meetings by hand. The voice recognition model is not installed on this computer, so a voiceprint cannot be created now.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n másodperc</source>
        <translation>
            <numerusform>%n second</numerusform>
            <numerusform>%n seconds</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélésen nincs elég hosszú sor</source>
        <translation>
            <numerusform>%n meeting has no long enough line</numerusform>
            <numerusform>%n meetings have no long enough line</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n minta készült.</source>
        <translation>
            <numerusform>%n sample created.</numerusform>
            <numerusform>%n samples created.</numerusform>
        </translation>
    </message>
    <message>
        <source>Nem készült minta.</source>
        <translation>No sample was created.</translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélésből nem sikerült.</source>
        <translation>
            <numerusform>Failed for %n meeting.</numerusform>
            <numerusform>Failed for %n meetings.</numerusform>
        </translation>
    </message>
    <message>
        <source>Legutóbb</source>
        <translation>Most recent</translation>
    </message>
    <message>
        <source>Legtöbb megbeszélés</source>
        <translation>Most meetings</translation>
    </message>
    <message>
        <source>ABC</source>
        <translation>A–Z</translation>
    </message>
    <message numerus="yes">
        <source>%n találat</source>
        <translation>
            <numerusform>%n result</numerusform>
            <numerusform>%n results</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n személy</source>
        <translation>
            <numerusform>%n person</numerusform>
            <numerusform>%n people</numerusform>
        </translation>
    </message>
    <message>
        <source>Átnevezve: %1</source>
        <translation>Renamed: %1</translation>
    </message>
    <message>
        <source>Becenév törölve: %1</source>
        <translation>Alias removed: %1</translation>
    </message>
    <message>
        <source>kézzel beállítva</source>
        <translation>set manually</translation>
    </message>
    <message>
        <source>tanult</source>
        <translation>learned</translation>
    </message>
    <message>
        <source>Minta törölve</source>
        <translation>Sample deleted</translation>
    </message>
    <message>
        <source>Minta törölve: %1</source>
        <translation>Sample deleted: %1</translation>
    </message>
    <message>
        <source>Minta áthelyezve ide: %1</source>
        <translation>Sample moved to: %1</translation>
    </message>
    <message>
        <source>Új személy a mintából: %1</source>
        <translation>New person from the sample: %1</translation>
    </message>
    <message>
        <source>hang alapján hasonló (%1%)</source>
        <translation>similar by voice (%1%)</translation>
    </message>
    <message>
        <source>hang-egyezés: %1%</source>
        <translation>voice match: %1%</translation>
    </message>
    <message>
        <source>Eredmény: &lt;b&gt;%1&lt;/b&gt;, &lt;b&gt;%2&lt;/b&gt;. „%3” becenév lesz.</source>
        <translation>Result: &lt;b&gt;%1&lt;/b&gt;, &lt;b&gt;%2&lt;/b&gt;. &quot;%3&quot; becomes an alias.</translation>
    </message>
    <message numerus="yes">
        <source>%n érintett összefoglaló elavultnak jelölődik.</source>
        <translation>
            <numerusform>%n affected summary is marked outdated.</numerusform>
            <numerusform>%n affected summaries are marked outdated.</numerusform>
        </translation>
    </message>
    <message>
        <source>A címkék összeadódnak (%1).</source>
        <translation>Tags add up (%1).</translation>
    </message>
    <message>
        <source>Az összevonás nem vonható vissza.</source>
        <translation>A merge cannot be undone.</translation>
    </message>
    <message>
        <source>Összevonva: %1</source>
        <translation>Merged: %1</translation>
    </message>
    <message>
        <source>Egy megbeszélésen sem szerepel.</source>
        <translation>Does not appear in any meeting.</translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélésen névtelen beszélőként marad meg (pl. „%1”), a szöveg nem változik.</source>
        <translation>
            <numerusform>They remain an unnamed speaker in %n meeting (e.g. &quot;%1&quot;); the text does not change.</numerusform>
            <numerusform>They remain an unnamed speaker in %n meetings (e.g. &quot;%1&quot;); the text does not change.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélésen névtelen beszélőként marad meg, a szöveg nem változik.</source>
        <translation>
            <numerusform>They remain an unnamed speaker in %n meeting; the text does not change.</numerusform>
            <numerusform>They remain an unnamed speaker in %n meetings; the text does not change.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n hangmintája megmarad egy új, névtelen személynél, akit később átnevezhetsz vagy összevonhatsz.</source>
        <translation>
            <numerusform>Their %n voice sample is kept with a new, anonymous person that you can rename or merge later.</numerusform>
            <numerusform>Their %n voice samples are kept with a new, anonymous person that you can rename or merge later.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n hangmintája törlődik, így hang alapján többé nem ismerjük fel.</source>
        <translation>
            <numerusform>Their %n voice sample is deleted, so we will no longer recognise them by voice.</numerusform>
            <numerusform>Their %n voice samples are deleted, so we will no longer recognise them by voice.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n összefoglaló elavultnak jelölődik.</source>
        <translation>
            <numerusform>%n summary is marked outdated.</numerusform>
            <numerusform>%n summaries are marked outdated.</numerusform>
        </translation>
    </message>
    <message>
        <source>A törlés nem vonható vissza.</source>
        <translation>A deletion cannot be undone.</translation>
    </message>
    <message>
        <source>Törölve: %1.</source>
        <translation>Deleted: %1.</translation>
    </message>
    <message>
        <source>A mintái itt maradtak: %1</source>
        <translation>Their samples were kept here: %1</translation>
    </message>
    <message>
        <source>Törölve: %1</source>
        <translation>Deleted: %1</translation>
    </message>
    <message>
        <source>Visszavonva: %1</source>
        <translation>Undone: %1</translation>
    </message>
    <message>
        <source>A minta visszakerült.</source>
        <translation>The sample is back.</translation>
    </message>
    <message>
        <source>A minta visszakerült a korábbi helyére.</source>
        <translation>The sample is back where it was.</translation>
    </message>
    <message>
        <source>Becenév visszaállítva: %1</source>
        <translation>Alias restored: %1</translation>
    </message>
    <message>
        <source>Az átnevezés visszavonva.</source>
        <translation>The rename was undone.</translation>
    </message>
    <message>
        <source>A minta hangfájlja már nincs meg, ezért nem hallgatható meg.</source>
        <translation>The sample&apos;s audio file no longer exists, so it cannot be played.</translation>
    </message>
</context>
<context>
    <name>tanara_qml::PersonListModel</name>
    <message numerus="yes">
        <source>%n megbeszélés</source>
        <translation>
            <numerusform>%n meeting</numerusform>
            <numerusform>%n meetings</numerusform>
        </translation>
    </message>
    <message>
        <source>A #%1 alapján</source>
        <translation>Based on #%1</translation>
    </message>
    <message>
        <source>A címkék alapján</source>
        <translation>Based on tags</translation>
    </message>
</context>
<context>
    <name>tanara_qml::PlayerController</name>
    <message>
        <source>A hangfájl nem található: %1</source>
        <translation>Audio file not found: %1</translation>
    </message>
</context>
<context>
    <name>tanara_qml::PreTranscriptViewModel</name>
    <message>
        <source>MMM d. HH:mm</source>
        <translation>MMM d, HH:mm</translation>
    </message>
    <message>
        <source>hasonló cím: „%1”</source>
        <translation>similar title: &quot;%1&quot;</translation>
    </message>
    <message>
        <source>közös résztvevő: %1</source>
        <translation>shared participant: %1</translation>
    </message>
    <message>
        <source>közös kifejezések: %1</source>
        <translation>shared terms: %1</translation>
    </message>
    <message>
        <source>Javaslat elfogadva: #%1</source>
        <translation>Suggestion accepted: #%1</translation>
    </message>
    <message>
        <source>Javaslat elutasítva: #%1</source>
        <translation>Suggestion dismissed: #%1</translation>
    </message>
    <message>
        <source>Átírás folyamatban</source>
        <translation>Transcription in progress</translation>
    </message>
    <message>
        <source>%1 · saját kulcs</source>
        <translation>%1 · own key</translation>
    </message>
    <message>
        <source>Lekeverés</source>
        <translation>Mixdown</translation>
    </message>
    <message>
        <source>Feltöltés</source>
        <translation>Upload</translation>
    </message>
    <message>
        <source>%1 perc, %2 sáv</source>
        <translation>%1 min, %2 tracks</translation>
    </message>
    <message>
        <source>Átírás</source>
        <translation>Transcription</translation>
    </message>
    <message>
        <source>Beszélők szétválasztása</source>
        <translation>Speaker separation</translation>
    </message>
    <message>
        <source>Résztvevők azonosítása</source>
        <translation>Identifying participants</translation>
    </message>
    <message>
        <source>A szolgáltató nem fogadta el az API-kulcsot. Ellenőrizd vagy cseréld a kulcsot a Beállításokban.</source>
        <translation>The provider rejected the API key. Check or replace the key in Settings.</translation>
    </message>
    <message>
        <source>szept. 30. 16:42</source>
        <translation>Sep 30, 16:42</translation>
    </message>
    <message>
        <source>Kulcs módosítása</source>
        <translation>Change key</translation>
    </message>
    <message>
        <source>Nincs beállítva átíró szolgáltató</source>
        <translation>No transcription provider is set up</translation>
    </message>
    <message>
        <source>Hiányzik: API-kulcs (Soniox). Add meg a saját szolgáltatód adatait a Beállításokban.</source>
        <translation>Missing: API key (Soniox). Enter your own provider details in Settings.</translation>
    </message>
    <message>
        <source>Szolgáltató beállítása</source>
        <translation>Set up provider</translation>
    </message>
    <message>
        <source>A szolgáltató beállítása után indítható.</source>
        <translation>Can be started once a provider is set up.</translation>
    </message>
    <message>
        <source>Kontextus: „%1”</source>
        <translation>Context: &quot;%1&quot;</translation>
    </message>
    <message>
        <source>Szolgáltató: %1</source>
        <translation>Provider: %1</translation>
    </message>
</context>
<context>
    <name>tanara_qml::RecorderViewModel</name>
    <message>
        <source>Az utolsó rögzített sáv nem kapcsolható ki — a felvételt a Leállítás gombbal fejezheted be.</source>
        <translation>The last recorded track cannot be turned off — use Stop to end the recording.</translation>
    </message>
    <message>
        <source>Megbeszélés</source>
        <translation>Meeting</translation>
    </message>
    <message>
        <source>%1-hívás</source>
        <translation>%1 call</translation>
    </message>
    <message>
        <source>A(z) %1 eszköz eltűnt, a sávja lezárult, a többi sáv megy tovább.</source>
        <translation>The device %1 disappeared. Its track is closed; the other tracks continue.</translation>
    </message>
    <message>
        <source>A(z) %1 eszköz visszatért, a rögzítése folytatódik (új szakasz).</source>
        <translation>The device %1 is back; recording it again (new segment).</translation>
    </message>
    <message>
        <source>Úgy tűnik, a hívás véget ért. Magamtól nem állítom le.</source>
        <translation>The call seems to have ended. I won&apos;t stop on my own.</translation>
    </message>
    <message>
        <source>Úgy tűnik, véget ért: %1. Magamtól nem állítom le.</source>
        <translation>This seems to have ended: %1. I won&apos;t stop on my own.</translation>
    </message>
    <message numerus="yes">
        <source>Minden sávon %n perce csend van. Magamtól nem állítom le.</source>
        <translation>
            <numerusform>Every track has been silent for %n minute. I won&apos;t stop on my own.</numerusform>
            <numerusform>Every track has been silent for %n minutes. I won&apos;t stop on my own.</numerusform>
        </translation>
    </message>
    <message>
        <source>leválasztva</source>
        <translation>disconnected</translation>
    </message>
    <message numerus="yes">
        <source>%n perce nincs jel</source>
        <translation>
            <numerusform>no signal for %n minute</numerusform>
            <numerusform>no signal for %n minutes</numerusform>
        </translation>
    </message>
    <message>
        <source>nincs jel</source>
        <translation>no signal</translation>
    </message>
    <message>
        <source>jel van · nincs rögzítve</source>
        <translation>signal · not recorded</translation>
    </message>
    <message>
        <source>Nincs bekapcsolt forrás. Kapcsolj be legalább egy eszközt.</source>
        <translation>No source is turned on. Turn on at least one device.</translation>
    </message>
    <message>
        <source>Nem sikerült sávot indítani ezen az eszközön: %1</source>
        <translation>Could not start a track on this device: %1</translation>
    </message>
    <message>
        <source>Vége a megbeszélésnek?</source>
        <translation>Is the meeting over?</translation>
    </message>
    <message numerus="yes">
        <source>%n sáv</source>
        <translation>
            <numerusform>%n track</numerusform>
            <numerusform>%n tracks</numerusform>
        </translation>
    </message>
    <message>
        <source>Egyetlen sáv hangfájlja sem jött létre.</source>
        <translation>No audio file was created for any track.</translation>
    </message>
    <message numerus="yes">
        <source>%n sáv hangfájlja hiányzik.</source>
        <translation>
            <numerusform>The audio file of %n track is missing.</numerusform>
            <numerusform>The audio files of %n tracks are missing.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>A hívás sávján %n perce csend van. Magamtól nem állítom le.</source>
        <translation>
            <numerusform>The call track has been silent for %n minute. I won&apos;t stop on my own.</numerusform>
            <numerusform>The call track has been silent for %n minutes. I won&apos;t stop on my own.</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>tanara_qml::SettingsEmbeddingModel</name>
    <message>
        <source>Nincs (alap)</source>
        <translation>None (basic)</translation>
    </message>
    <message>
        <source>Helyi végpont</source>
        <translation>Local endpoint</translation>
    </message>
    <message>
        <source>Tanara Cloud · hamarosan</source>
        <translation>Tanara Cloud · coming soon</translation>
    </message>
    <message>
        <source>A Tanara Cloud hamarosan érkezik.</source>
        <translation>Tanara Cloud is coming soon.</translation>
    </message>
    <message>
        <source>A rokon témákat is felismeri, akkor is, ha más szavakkal beszéltetek róla. Nem kell hozzá helyi modell.</source>
        <translation>Also recognises related topics, even if you talked about them in other words. No local model needed.</translation>
    </message>
    <message>
        <source>A rokon témákat is felismeri, akkor is, ha más szavakkal beszéltetek róla. Beágyazó modell kell hozzá (pl. LM Studio). Alap szinten is működnek a javaslatok: közös résztvevők, nevek, hasonló cím.</source>
        <translation>Also recognises related topics, even if you talked about them in other words. Needs an embedding model (e.g. LM Studio). Suggestions also work at the basic level: shared participants, names, similar title.</translation>
    </message>
    <message>
        <source>A javaslatok alap szinten működnek: közös résztvevők, nevek, hasonló cím alapján. Nem kell hozzá modell, és semmi nem hagyja el a gépet.</source>
        <translation>Suggestions work at the basic level: based on shared participants, names and similar title. No model needed, and nothing leaves your computer.</translation>
    </message>
    <message>
        <source>Alap szint</source>
        <translation>Basic level</translation>
    </message>
    <message>
        <source>Kapcsolódva</source>
        <translation>Connected</translation>
    </message>
    <message>
        <source>Nem elérhető</source>
        <translation>Not available</translation>
    </message>
    <message>
        <source>Nem érhető el</source>
        <translation>Unreachable</translation>
    </message>
    <message>
        <source>Hiányos</source>
        <translation>Incomplete</translation>
    </message>
    <message>
        <source>Beállítva</source>
        <translation>Configured</translation>
    </message>
    <message>
        <source>%1 / %2 megbeszélés</source>
        <translation>%1 / %2 meetings</translation>
    </message>
    <message numerus="yes">
        <source>kb. %n perc van hátra</source>
        <translation>
            <numerusform>about %n minute left</numerusform>
            <numerusform>about %n minutes left</numerusform>
        </translation>
    </message>
    <message>
        <source>ismeretlen hiba</source>
        <translation>unknown error</translation>
    </message>
    <message>
        <source>Megállt %1 / %2-nél: %3</source>
        <translation>Stopped at %1 / %2: %3</translation>
    </message>
    <message>
        <source>Megállt %1 / %2-nál: %3</source>
        <translation>Stopped at %1 / %2: %3</translation>
    </message>
    <message numerus="yes">
        <source>Naprakész · %n megbeszélés</source>
        <translation>
            <numerusform>Up to date · %n meeting</numerusform>
            <numerusform>Up to date · %n meetings</numerusform>
        </translation>
    </message>
    <message>
        <source>Másik beágyazásra váltasz.</source>
        <translation>You’re switching to a different embedding.</translation>
    </message>
    <message>
        <source>Másik modellre váltasz.</source>
        <translation>You’re switching to a different model.</translation>
    </message>
    <message>
        <source>Mentés után a könyvtár előkészítése elölről indul. Addig a javaslatok az alap szinten működnek.</source>
        <translation>After saving, preparing the library starts over. Until then, suggestions work at the basic level.</translation>
    </message>
    <message>
        <source>Mentés után a könyvtár előkészítése elölről indul (%1, kb. %2). Addig a javaslatok az alap szinten működnek.</source>
        <translation>After saving, preparing the library starts over (%1, about %2). Until then, suggestions work at the basic level.</translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélés</source>
        <translation>
            <numerusform>%n meeting</numerusform>
            <numerusform>%n meetings</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n perc</source>
        <translation>
            <numerusform>%n minute</numerusform>
            <numerusform>%n minutes</numerusform>
        </translation>
    </message>
    <message>
        <source>a végpont nem válaszol. Fut a helyi szerver?</source>
        <translation>the endpoint is not responding. Is the local server running?</translation>
    </message>
</context>
<context>
    <name>tanara_qml::SettingsProviderModel</name>
    <message>
        <source>beszélőket elkülönít</source>
        <translation>separates speakers</translation>
    </message>
    <message>
        <source>nem kell (helyi végpont)</source>
        <translation>not needed (local endpoint)</translation>
    </message>
    <message>
        <source>illeszd be a kulcsot</source>
        <translation>paste the key</translation>
    </message>
    <message>
        <source>nem kötelező</source>
        <translation>optional</translation>
    </message>
    <message>
        <source>Automatikus (a feladathoz igazítva)</source>
        <translation>Automatic (fitted to the job)</translation>
    </message>
    <message>
        <source>%1 token</source>
        <translation>%1 tokens</translation>
    </message>
    <message>
        <source>A cím kötelező.</source>
        <translation>The address is required.</translation>
    </message>
    <message>
        <source>http:// vagy https:// kezdetű címet adj meg.</source>
        <translation>Enter an address that starts with http:// or https://.</translation>
    </message>
    <message>
        <source>%1 és %2 közötti szám kell.</source>
        <translation>Enter a number between %1 and %2.</translation>
    </message>
    <message>
        <source>Ez a mező kötelező.</source>
        <translation>This field is required.</translation>
    </message>
    <message>
        <source>Tesztelés…</source>
        <translation>Testing…</translation>
    </message>
    <message>
        <source>Kapcsolódva · %1 ms</source>
        <translation>Connected · %1 ms</translation>
    </message>
    <message>
        <source>Kapcsolódva</source>
        <translation>Connected</translation>
    </message>
    <message>
        <source>A kulcs nem jó</source>
        <translation>The key is wrong</translation>
    </message>
    <message>
        <source>Hibás cím</source>
        <translation>Invalid address</translation>
    </message>
    <message>
        <source>Nem API-cím</source>
        <translation>Not an API address</translation>
    </message>
    <message>
        <source>A szolgáltató hibát jelzett</source>
        <translation>The provider reported an error</translation>
    </message>
    <message>
        <source>Nem érhető el</source>
        <translation>Unreachable</translation>
    </message>
    <message>
        <source>A szolgáltató üres modell-listát adott.</source>
        <translation>The provider returned an empty model list.</translation>
    </message>
    <message>
        <source>Nem sikerült lekérni a modelleket: %1</source>
        <translation>Could not fetch the models: %1</translation>
    </message>
</context>
<context>
    <name>tanara_qml::SettingsViewModel</name>
    <message>
        <source>Adj meg egy nevet — ezen a néven szerepelsz az átiratokban.</source>
        <translation>Enter a name — you appear under this name in transcripts.</translation>
    </message>
    <message>
        <source>Válassz egy mappát.</source>
        <translation>Choose a folder.</translation>
    </message>
    <message>
        <source>Teljes elérési utat adj meg.</source>
        <translation>Enter a full path.</translation>
    </message>
    <message>
        <source>Legalább egy alkalmazás kell: üres listával a figyelő semmire nem jelez.</source>
        <translation>At least one app is needed: with an empty list the watcher never alerts you.</translation>
    </message>
    <message>
        <source>Nincs mentetlen változás</source>
        <translation>No unsaved changes</translation>
    </message>
    <message>
        <source>Modellváltás · mentéskor újraindul az előkészítés</source>
        <translation>Model change · preparation restarts on save</translation>
    </message>
    <message>
        <source>Új hangmodell · mentéskor indul a minták pótlása</source>
        <translation>New voice model · samples are recalculated on save</translation>
    </message>
    <message numerus="yes">
        <source>%n nem mentett változás</source>
        <translation>
            <numerusform>%n unsaved change</numerusform>
            <numerusform>%n unsaved changes</numerusform>
        </translation>
    </message>
    <message>
        <source>Az átíráshoz kell egy szolgáltató. Válassz egyet, add meg a kulcsot, és visszaviszünk a megbeszéléshez.</source>
        <translation>Transcription needs a provider. Choose one, enter the key, and we will take you back to the meeting.</translation>
    </message>
    <message>
        <source>Az összefoglalóhoz kell egy nyelvi modell. Állítsd be a végpontot, és visszaviszünk a megbeszéléshez.</source>
        <translation>The summary needs a language model. Set up the endpoint, and we will take you back to the meeting.</translation>
    </message>
    <message numerus="yes">
        <source>Hanglenyomat: %n minta,</source>
        <translation>
            <numerusform>Voiceprint: %n sample,</numerusform>
            <numerusform>Voiceprint: %n samples,</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélésből</source>
        <translation>
            <numerusform>from %n meeting</numerusform>
            <numerusform>from %n meetings</numerusform>
        </translation>
    </message>
    <message>
        <source>Ehhez a névhez még nincs hanglenyomat.</source>
        <translation>There is no voiceprint for this name yet.</translation>
    </message>
    <message>
        <source>Rendszer nyelve (magyar)</source>
        <translation>System language (magyar)</translation>
    </message>
    <message>
        <source>Rendszer nyelve (English)</source>
        <translation>System language (English)</translation>
    </message>
    <message>
        <source>Ez a mappa felhő-szinkronban lévőnek tűnik. A belső adatokat ne szinkronizáld: két gép egyszerre írná, és megsérülhet.</source>
        <translation>This folder looks like it is synced to the cloud. Do not sync the internal data: two computers would write it at the same time and it could get corrupted.</translation>
    </message>
    <message>
        <source>Felvételek</source>
        <translation>Recordings</translation>
    </message>
    <message>
        <source>Hangsávok, megbeszélésenként egy mappa. Az új felvételek ide kerülnek.</source>
        <translation>Audio tracks, one folder per meeting. New recordings go here.</translation>
    </message>
    <message>
        <source>Jegyzetek</source>
        <translation>Notes</translation>
    </message>
    <message>
        <source>Az összefoglalók másolata sima Markdown-fájlként (pl. a jegyzettáradba).</source>
        <translation>A copy of the summaries as plain Markdown files (e.g. for your notes vault).</translation>
    </message>
    <message>
        <source>Belső adatok</source>
        <translation>Internal data</translation>
    </message>
    <message>
        <source>még nincs ilyen mappa — mentéskor létrejön</source>
        <translation>this folder does not exist yet — it is created on save</translation>
    </message>
    <message numerus="yes">
        <source>%n megbeszélés</source>
        <translation>
            <numerusform>%n meeting</numerusform>
            <numerusform>%n meetings</numerusform>
        </translation>
    </message>
    <message>
        <source>Felvételek mappája</source>
        <translation>Recordings folder</translation>
    </message>
    <message>
        <source>Jegyzetek mappája</source>
        <translation>Notes folder</translation>
    </message>
    <message>
        <source>Belső adatok mappája</source>
        <translation>Internal data folder</translation>
    </message>
    <message>
        <source>Takarékos</source>
        <translation>Economy</translation>
    </message>
    <message>
        <source>Beszéd</source>
        <translation>Speech</translation>
    </message>
    <message>
        <source>Magas</source>
        <translation>High</translation>
    </message>
    <message>
        <source>Legjobb</source>
        <translation>Best</translation>
    </message>
    <message>
        <source>A legkisebb fájl; az átíráshoz még elég.</source>
        <translation>The smallest file; still enough for transcription.</translation>
    </message>
    <message>
        <source>Beszédhez elég.</source>
        <translation>Enough for speech.</translation>
    </message>
    <message>
        <source>Zajos teremhez, több beszélőhöz.</source>
        <translation>For a noisy room or several speakers.</translation>
    </message>
    <message>
        <source>A legjobb minőség, a legnagyobb fájl.</source>
        <translation>The best quality, the largest file.</translation>
    </message>
    <message>
        <source>%1 kbps Opus · kb. %2 MB sávonként 1,5 óra alatt.</source>
        <translation>%1 kbps Opus · about %2 MB per track for 1.5 hours.</translation>
    </message>
    <message>
        <source>Írd be a folyamat nevét vagy annak egy részét.</source>
        <translation>Type the process name or a part of it.</translation>
    </message>
    <message>
        <source>Legalább két karakter kell.</source>
        <translation>At least two characters are needed.</translation>
    </message>
    <message>
        <source>Ez már szerepel a listán.</source>
        <translation>This is already on the list.</translation>
    </message>
    <message>
        <source>Gyors összefoglaló</source>
        <translation>Quick summary</translation>
    </message>
    <message>
        <source>Rövid megbeszélésnél (kb. 22 percig) egyetlen hívás fut az „Egy lépésben” utasítással; hosszabbnál az átirat kb. 15 perces részeire egyenként „Részjegyzet” készül, majd az „Összefésülés” írja meg belőlük a vezetői összefoglalót (a memó a részjegyzetekből áll össze).</source>
        <translation>For a short meeting (up to about 22 minutes) one call runs with the “One step” instructions; for a longer one, “Part notes” are made for each part of about 15 minutes, then “Merge” writes the executive summary from them (the memo is built from the part notes).</translation>
    </message>
    <message>
        <source>Előbb a „Témajavaslat” gyűjti ki a megbeszélés témáit, majd a jóváhagyott témákra egyenként az „Elemzés” fut.</source>
        <translation>First “Topic suggestions” collects the topics of the meeting, then “Analysis” runs for each approved topic.</translation>
    </message>
    <message>
        <source>Egy lépésben</source>
        <translation>One step</translation>
    </message>
    <message>
        <source>Részjegyzet</source>
        <translation>Part notes</translation>
    </message>
    <message>
        <source>Összefésülés</source>
        <translation>Merging</translation>
    </message>
    <message>
        <source>Témajavaslat</source>
        <translation>Topic suggestion</translation>
    </message>
    <message>
        <source>Témánkénti elemzés</source>
        <translation>Topic-by-topic analysis</translation>
    </message>
    <message>
        <source>Elemzés</source>
        <translation>Analysis</translation>
    </message>
    <message>
        <source>A végpont nem válaszol. Fut a helyi szerver (pl. LM Studio), és jó a port?</source>
        <translation>The endpoint does not respond. Is the local server (e.g. LM Studio) running, and is the port right?</translation>
    </message>
    <message>
        <source>A kulcsok helye: a rendszer kulcstartója. A beállítás-fájlba nem kerülnek.</source>
        <translation>Keys are stored in the system keychain. They never go into the settings file.</translation>
    </message>
    <message>
        <source>A kulcsok helye: fájl a belső adatok mappájában, csak neked olvasható. A beállítás-fájlba nem kerülnek.</source>
        <translation>Keys are stored in a file in the internal data folder, readable only by you. They never go into the settings file.</translation>
    </message>
    <message>
        <source>A rendszer kulcstartója nem érhető el, ezért a kulcsok titkosítatlan fájlban vannak (csak a te felhasználód olvashatja).</source>
        <translation>The system keychain is not available, so the keys are in an unencrypted file (readable only by your user).</translation>
    </message>
    <message>
        <source>A beszélő-felismerés letöltött modelljei (.onnx). A váltás újraindítás után érvényes; a meglévő fájlokat nem költözteti át.</source>
        <translation>The downloaded speaker recognition models (.onnx). A change takes effect after a restart; existing files are not moved.</translation>
    </message>
    <message>
        <source>Hanglenyomatok, személyek, címkék és index. Ezt a mappát most a TANARA_HOME környezeti változó rögzíti.</source>
        <translation>Voiceprints, people, tags and the index. This folder is currently fixed by the TANARA_HOME environment variable.</translation>
    </message>
    <message>
        <source>Hanglenyomatok, személyek, címkék és index. Ne szinkronizáld felhőtárhellyel. A váltás újraindítás után érvényes; a meglévő adatokat nem költözteti át.</source>
        <translation>Voiceprints, people, tags and the index. Do not sync it with cloud storage. A change takes effect after a restart; existing data is not moved.</translation>
    </message>
    <message>
        <source>A TANARA_HOME be van állítva: amíg él, a homokozó saját mappája érvényes, nem ez.</source>
        <translation>TANARA_HOME is set: while it is, the sandbox&apos;s own folder is used, not this one.</translation>
    </message>
    <message>
        <source>Hangmodellek</source>
        <translation>Voice models</translation>
    </message>
</context>
<context>
    <name>tanara_qml::ShellActions</name>
    <message>
        <source>Felvétel kész: %1</source>
        <translation>Recording done: %1</translation>
    </message>
    <message>
        <source>Importálva: %1</source>
        <translation>Imported: %1</translation>
    </message>
    <message>
        <source>Elkészült az átirat: %1</source>
        <translation>Transcript ready: %1</translation>
    </message>
    <message>
        <source>Elkészült az összefoglaló: %1</source>
        <translation>Summary ready: %1</translation>
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
        <source>Exportálva: %1</source>
        <translation>Exported: %1</translation>
    </message>
    <message>
        <source>Az exportálás megszakítva.</source>
        <translation>Export cancelled.</translation>
    </message>
    <message>
        <source>Az exportálás nem sikerült: %1</source>
        <translation>Export failed: %1</translation>
    </message>
    <message>
        <source>Importálva archívumból: %1</source>
        <translation>Imported from archive: %1</translation>
    </message>
    <message>
        <source>A lekeverés nem sikerült.</source>
        <translation>The mixdown failed.</translation>
    </message>
    <message>
        <source>A Beállítások ebben a módban nem érhetők el.</source>
        <translation>Settings are not available in this mode.</translation>
    </message>
    <message>
        <source>A Személyek ebben a módban nem érhetők el.</source>
        <translation>People are not available in this mode.</translation>
    </message>
    <message>
        <source>A Címkék ebben a módban nem érhetők el.</source>
        <translation>Tags are not available in this mode.</translation>
    </message>
    <message>
        <source>Az Első lépések ebben a módban nem érhetők el.</source>
        <translation>Getting started is not available in this mode.</translation>
    </message>
    <message>
        <source>A felvevő ebben a módban nem érhető el.</source>
        <translation>The recorder is not available in this mode.</translation>
    </message>
    <message>
        <source>Exportálás archívumba</source>
        <translation>Export to archive</translation>
    </message>
    <message>
        <source>Tanara-archívum (*.tanara.zip)</source>
        <translation>Tanara archive (*.tanara.zip)</translation>
    </message>
    <message>
        <source>Archívum importálása: %1…</source>
        <translation>Importing archive: %1…</translation>
    </message>
    <message>
        <source>Mappa importálása: %1…</source>
        <translation>Importing folder: %1…</translation>
    </message>
    <message>
        <source>Nem indítható: %1</source>
        <translation>Cannot start: %1</translation>
    </message>
    <message>
        <source>A Tanara Cloud még nem érhető el: a Beállításokban saját kulcsot adj meg.</source>
        <translation>Tanara Cloud is not available yet: enter your own key in Settings.</translation>
    </message>
    <message numerus="yes">
        <source>A mostani átirat és a benne lévő %n kézi javítás elvész, az összefoglaló elavulttá válik.</source>
        <translation>
            <numerusform>The current transcript and its %n manual correction will be lost, and the summary becomes outdated.</numerusform>
            <numerusform>The current transcript and its %n manual corrections will be lost, and the summary becomes outdated.</numerusform>
        </translation>
    </message>
    <message>
        <source>A mostani átirat elvész, az összefoglaló elavulttá válik.</source>
        <translation>The current transcript will be lost, and the summary becomes outdated.</translation>
    </message>
    <message numerus="yes">
        <source>A beszélők elnevezése (%n beszélő) törlődik, mert az új átirat másképp oszthatja fel a beszélőket.</source>
        <translation>
            <numerusform>Speaker names (%n speaker) will be cleared, because the new transcript may split the speakers differently.</numerusform>
            <numerusform>Speaker names (%n speakers) will be cleared, because the new transcript may split the speakers differently.</numerusform>
        </translation>
    </message>
    <message>
        <source>A hanglenyomatokba tanított javítások megmaradnak.</source>
        <translation>Corrections already learned into voiceprints are kept.</translation>
    </message>
    <message>
        <source>Adj meg legalább egy témát.</source>
        <translation>Enter at least one topic.</translation>
    </message>
    <message>
        <source>A témához cím kell az elemzéshez.</source>
        <translation>The topic needs a title for analysis.</translation>
    </message>
    <message>
        <source>Ez a téma már nem létezik.</source>
        <translation>This topic no longer exists.</translation>
    </message>
    <message>
        <source>Mindenki azonosítva</source>
        <translation>Everyone identified</translation>
    </message>
    <message>
        <source>Újraellenőrizzem a sorokat a megerősített és javított sorok hangja alapján? A kétséges sorokat az Átnézendő szűrőbe teszem, hogy átnézhesd őket.</source>
        <translation>Shall I re-verify the lines against the audio of the confirmed and corrected lines? I put the doubtful lines into the To review filter so you can go through them.</translation>
    </message>
    <message numerus="yes">
        <source>%n kétséges sort jelöltem meg — az Átnézendő szűrőben találod.</source>
        <translation>
            <numerusform>I marked %n doubtful line — you will find it in the To review filter.</numerusform>
            <numerusform>I marked %n doubtful lines — you will find them in the To review filter.</numerusform>
        </translation>
    </message>
    <message>
        <source>Az azonosításhoz nincs telepítve a hangmodell.</source>
        <translation>The voice model needed for identification is not installed.</translation>
    </message>
    <message>
        <source>Nincs mit azonosítani: ebben a megbeszélésben nincs beszélő.</source>
        <translation>Nothing to identify: this meeting has no speakers.</translation>
    </message>
    <message>
        <source>Az átírás még fut — a végén magától azonosítja a résztvevőket.</source>
        <translation>Transcription is still running — it identifies the participants automatically when it finishes.</translation>
    </message>
    <message>
        <source>Az importálás nem sikerült: %1</source>
        <translation>Import failed: %1</translation>
    </message>
    <message>
        <source>Ebben a megbeszélésben már minden beszélőnek van neve. Újraellenőrizzem a sorokat a megerősített és javított sorok hangja alapján? A kétséges sorokat az Átnézendő szűrőbe teszem, hogy átnézhesd őket.</source>
        <translation>Every speaker in this meeting already has a name. Shall I re-verify the lines against the audio of the confirmed and corrected lines? I put the doubtful lines into the To review filter so you can go through them.</translation>
    </message>
    <message>
        <source>A névtelen beszélőkhöz nem találtam használható hangot, ezért nincs mit azonosítani.</source>
        <translation>No usable audio was found for the unnamed speakers, so there is nothing to identify.</translation>
    </message>
    <message>
        <source>Azonosítás megszakítva.</source>
        <translation>Identification cancelled.</translation>
    </message>
    <message>
        <source>Résztvevők: %1</source>
        <translation>Participants: %1</translation>
    </message>
    <message>
        <source>Beszélők újraellenőrzése</source>
        <translation>Re-check speakers</translation>
    </message>
    <message>
        <source>Ennek a megbeszélésnek nincs szerkeszthető átirata.</source>
        <translation>This meeting has no editable transcript.</translation>
    </message>
    <message>
        <source>Újraellenőrzés</source>
        <translation>Re-check</translation>
    </message>
    <message>
        <source>A megerősített sorok alapján nem találtam kétséges sort.</source>
        <translation>Based on the confirmed lines, no doubtful line was found.</translation>
    </message>
    <message>
        <source>Az azonosítás kész: egyik beszélő hangja sem ismert még. A neveket az átiratban adhatod meg.</source>
        <translation>Identification done: none of the speakers&apos; voices is known yet. You can enter names in the transcript.</translation>
    </message>
    <message>
        <source>Az azonosítás kész. Felismert résztvevők: %1.</source>
        <translation>Identification done. Recognized participants: %1.</translation>
    </message>
    <message>
        <source>Azonosítás megszakítva. A már megtalált nevek megmaradtak.</source>
        <translation>Identification cancelled. The names already found were kept.</translation>
    </message>
    <message>
        <source>Az ismétlés nem indult el, mert közben másik megbeszélésre váltottál: %1</source>
        <translation>The retry was not started because you switched to another meeting in the meantime: %1</translation>
    </message>
</context>
<context>
    <name>tanara_qml::ShellImportModel</name>
    <message>
        <source>mono</source>
        <translation>mono</translation>
    </message>
    <message>
        <source>sztereó</source>
        <translation>stereo</translation>
    </message>
    <message numerus="yes">
        <source>%n csatorna</source>
        <translation>
            <numerusform>%n channel</numerusform>
            <numerusform>%n channels</numerusform>
        </translation>
    </message>
    <message>
        <source>ismeretlen hossz</source>
        <translation>unknown length</translation>
    </message>
    <message>
        <source>videó hangja</source>
        <translation>audio from video</translation>
    </message>
    <message>
        <source>A bal és a jobb csatorna két külön sáv lesz.</source>
        <translation>The left and right channels become two separate tracks.</translation>
    </message>
    <message>
        <source>Akkor kapcsold be, ha a bal és a jobb csatornán más-más ember mikrofonja szól. Kikapcsolva a fájl egyetlen sztereó sáv marad.</source>
        <translation>Turn this on if the left and right channels carry different people&apos;s microphones. When off, the file stays a single stereo track.</translation>
    </message>
    <message>
        <source>Mind a(z) %1 csatorna külön sáv lesz.</source>
        <translation>Each of the %1 channels becomes a separate track.</translation>
    </message>
    <message>
        <source>Kikapcsolva a(z) %1 csatorna egyetlen sztereó sávba keveredik.</source>
        <translation>When off, the %1 channels are mixed into a single stereo track.</translation>
    </message>
    <message>
        <source>A fájlok adatainak beolvasása…</source>
        <translation>Reading file details…</translation>
    </message>
    <message>
        <source>Egyik fájl sem importálható.</source>
        <translation>None of the files can be imported.</translation>
    </message>
    <message numerus="yes">
        <source>%n sáv lesz belőle.</source>
        <translation>
            <numerusform>This will create %n track.</numerusform>
            <numerusform>This will create %n tracks.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n fájl kimarad.</source>
        <translation>
            <numerusform>%n file will be skipped.</numerusform>
            <numerusform>%n files will be skipped.</numerusform>
        </translation>
    </message>
    <message>
        <source>Így írd: 2026-03-05 14:30</source>
        <translation>Use this format: 2026-03-05 14:30</translation>
    </message>
    <message>
        <source>A fájlba írt készítési időből.</source>
        <translation>From the creation time stored in the file.</translation>
    </message>
    <message>
        <source>A fájl módosítási idejéből — ha máskor készült, írd át.</source>
        <translation>From the file&apos;s modification time — change it if it was recorded at another time.</translation>
    </message>
    <message>
        <source>Megszakítás…</source>
        <translation>Cancelling…</translation>
    </message>
    <message>
        <source>Előkészítés…</source>
        <translation>Preparing…</translation>
    </message>
    <message>
        <source>%1% · %2 / %3 fájl</source>
        <translation>%1% · %2 / %3 files</translation>
    </message>
    <message>
        <source>hasonló cím: „%1”</source>
        <translation>similar title: &quot;%1&quot;</translation>
    </message>
    <message>
        <source>közös résztvevő: %1</source>
        <translation>shared participant: %1</translation>
    </message>
    <message>
        <source>közös kifejezések: %1</source>
        <translation>shared terms: %1</translation>
    </message>
    <message>
        <source>Nem hang- vagy videófájl (nem olvasható be): %1</source>
        <translation>Not an audio or video file (cannot be read): %1</translation>
    </message>
    <message>
        <source>Nem sikerült beolvasni: %1</source>
        <translation>Could not read: %1</translation>
    </message>
</context>
<context>
    <name>tanara_qml::ShellMeetingModel</name>
    <message>
        <source>Átírás</source>
        <translation>Transcription</translation>
    </message>
    <message>
        <source>Összefoglaló készítése</source>
        <translation>Creating summary</translation>
    </message>
    <message>
        <source>Témák javaslása</source>
        <translation>Suggesting topics</translation>
    </message>
    <message>
        <source>Témánkénti elemzés</source>
        <translation>Topic-by-topic analysis</translation>
    </message>
    <message>
        <source>Lekeverés</source>
        <translation>Mixdown</translation>
    </message>
    <message>
        <source>Résztvevők azonosítása</source>
        <translation>Identifying participants</translation>
    </message>
    <message>
        <source>Importálás</source>
        <translation>Import</translation>
    </message>
    <message>
        <source>Exportálás archívumba</source>
        <translation>Export to archive</translation>
    </message>
    <message numerus="yes">
        <source>%n beszélő</source>
        <translation>
            <numerusform>%n speaker</numerusform>
            <numerusform>%n speakers</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sáv</source>
        <translation>
            <numerusform>%n track</numerusform>
            <numerusform>%n tracks</numerusform>
        </translation>
    </message>
    <message>
        <source>LM Studio · saját kulcs</source>
        <translation>LM Studio · own key</translation>
    </message>
    <message>
        <source>%1 / %2 beszélő</source>
        <translation>%1 / %2 speakers</translation>
    </message>
    <message>
        <source>%1 / %2 téma</source>
        <translation>%1 / %2 topics</translation>
    </message>
    <message>
        <source>kevesebb mint 1 perc van hátra</source>
        <translation>less than 1 minute left</translation>
    </message>
    <message numerus="yes">
        <source>kb. %n perc van hátra</source>
        <translation>
            <numerusform>about %n minute left</numerusform>
            <numerusform>about %n minutes left</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>tanara_qml::SummaryViewModel</name>
    <message>
        <source>Gyors összefoglaló</source>
        <translation>Quick summary</translation>
    </message>
    <message>
        <source>Témánkénti elemzés</source>
        <translation>Topic-by-topic analysis</translation>
    </message>
    <message>
        <source>Összefoglaló</source>
        <translation>Summary</translation>
    </message>
    <message>
        <source>Az átirat kész.</source>
        <translation>The transcript is ready.</translation>
    </message>
    <message>
        <source>Az átirat kész, %1 beszélő, %2 megszólalás.</source>
        <translation>The transcript is ready: %1 speakers, %2 utterances.</translation>
    </message>
    <message>
        <source>MMM d. HH:mm</source>
        <translation>MMM d, HH:mm</translation>
    </message>
    <message>
        <source>saját kulcs</source>
        <translation>own key</translation>
    </message>
    <message>
        <source>%1 · saját kulcs</source>
        <translation>%1 · own key</translation>
    </message>
    <message>
        <source>Nincs beállítva összefoglaló szolgáltató</source>
        <translation>No summary provider is set up</translation>
    </message>
    <message>
        <source>Hiányzik: Cím (URL) (OpenAI-kompatibilis). Add meg a saját szolgáltatód adatait a Beállításokban.</source>
        <translation>Missing: Address (URL) (OpenAI-compatible). Enter your own provider&apos;s details in Settings.</translation>
    </message>
    <message>
        <source>Szolgáltató beállítása</source>
        <translation>Set up provider</translation>
    </message>
    <message>
        <source>A szolgáltató beállítása után indítható.</source>
        <translation>Can be started once a provider is set up.</translation>
    </message>
    <message>
        <source>Összefoglaló készítése</source>
        <translation>Creating summary</translation>
    </message>
    <message>
        <source>Negyedéves partner-egyeztetés: támogatási igény, súgóoldalak, számlázás. A „Szúgó Pont” helyesen: SúgóPont.</source>
        <translation>Quarterly partner sync: support demand, help pages, billing. “Shoe Go Point” is really SúgóPont.</translation>
    </message>
    <message>
        <source>Negyedéves partner-egyeztetés: támogatási igény, súgóoldalak, számlázás.</source>
        <translation>Quarterly partner sync: support demand, help pages, billing.</translation>
    </message>
    <message>
        <source>Összefoglalás</source>
        <translation>Summarising</translation>
    </message>
    <message>
        <source>Jegyzetelés részenként</source>
        <translation>Notes part by part</translation>
    </message>
    <message numerus="yes">
        <source>%n rész kész</source>
        <translation>
            <numerusform>%n part done</numerusform>
            <numerusform>%n parts done</numerusform>
        </translation>
    </message>
    <message>
        <source>%1/%2. rész</source>
        <translation>Part %1/%2</translation>
    </message>
    <message numerus="yes">
        <source> · %n korábbi futásból</source>
        <translation>
            <numerusform> · %n from an earlier run</numerusform>
            <numerusform> · %n from an earlier run</numerusform>
        </translation>
    </message>
    <message>
        <source>Összegzés</source>
        <translation>Summing up</translation>
    </message>
    <message>
        <source>A szolgáltató nem válaszolt időben. Próbáld újra.</source>
        <translation>The provider did not respond in time. Try again.</translation>
    </message>
    <message>
        <source>A szolgáltatónál hiba történt. Próbáld újra később.</source>
        <translation>The provider reported an error. Try again later.</translation>
    </message>
    <message>
        <source>A modell 4096 tokenes kontextussal van betöltve, a kérés 6042 token volt — nem fér bele. A Tanara újra tudja tölteni az LM Studióban legalább 16 384 tokenes kontextussal, és újraindítja a feladatot.</source>
        <translation>The model is loaded with a 4096-token context, the request was 6042 tokens — it does not fit. Tanara can reload it in LM Studio with a context of at least 16,384 tokens and restart the job.</translation>
    </message>
    <message>
        <source>kérés 6042 token · kontextus 4096 token</source>
        <translation>request 6042 tokens · context 4096 tokens</translation>
    </message>
    <message>
        <source>A partnerek megerősítették, hogy a harmadik negyedévben a támogatási igény harmadával csökkent, főként az új súgóoldalak miatt. A súgót a többi termékre is kiterjesztik, de a számlázásnál előbb a folyamatot egyszerűsítik. A dokumentációs létszámbővítésről a költségvetés dönt.</source>
        <translation>The partners confirmed that support demand dropped by a third in the third quarter, mainly thanks to the new help pages. The help pages will be extended to the other products, but for billing the process is simplified first. The documentation headcount increase is decided in the budget.</translation>
    </message>
    <message>
        <source>[12:52] A súgóoldalakat a többi termékre is kiterjesztik.</source>
        <translation>[12:52] The help pages are extended to the other products.</translation>
    </message>
    <message>
        <source>[31:10] A számlázásnál előbb a folyamatot egyszerűsítik, utána a leírást.</source>
        <translation>[31:10] For billing, the process is simplified first, then the documentation.</translation>
    </message>
    <message>
        <source>[58:40] A dokumentációs létszámról a költségvetési tervezés dönt.</source>
        <translation>[58:40] Documentation headcount is decided during budget planning.</translation>
    </message>
    <message>
        <source>[43:30] Legyen-e nyomtatható változata a súgóoldalaknak?</source>
        <translation>[43:30] Should the help pages have a printable version?</translation>
    </message>
    <message>
        <source>[58:40] Bevonjanak-e átmenetileg külső szövegírót a dokumentációhoz?</source>
        <translation>[58:40] Should an outside writer help with the documentation for a while?</translation>
    </message>
    <message>
        <source>Mikorra frissül a partnerportál?</source>
        <translation>When will the partner portal be updated?</translation>
    </message>
    <message>
        <source>A súgóoldalak kiterjesztése a további termékekre: ütemterv</source>
        <translation>Extending the help pages to the other products: schedule</translation>
    </message>
    <message>
        <source>okt. 15.</source>
        <translation>Oct 15</translation>
    </message>
    <message>
        <source>Számlázási elakadási pontok összegyűjtése</source>
        <translation>Collect the points where people get stuck in billing</translation>
    </message>
    <message>
        <source>okt. 10.</source>
        <translation>Oct 10</translation>
    </message>
    <message>
        <source>Dokumentációs létszámigény a költségvetési tervbe</source>
        <translation>Add documentation headcount to the budget plan</translation>
    </message>
    <message>
        <source>okt. 20.</source>
        <translation>Oct 20</translation>
    </message>
    <message>
        <source>Új-ügyfél bontás elküldése a partnereknek</source>
        <translation>Send the new-customer breakdown to the partners</translation>
    </message>
    <message>
        <source>okt. 4.</source>
        <translation>Oct 4</translation>
    </message>
    <message>
        <source>Támogatási igény alakulása</source>
        <translation>Support demand trend</translation>
    </message>
    <message>
        <source>A jegyek száma harmadával csökkent, miközben az aktív felhasználók 12%-kal nőttek. A csökkenés fő oka a termékbe épített súgó.</source>
        <translation>Tickets dropped by a third while active users grew by 12%. The main reason for the drop is the help built into the product.</translation>
    </message>
    <message>
        <source>A súgóoldalakat a többi termékre is kiterjesztik.</source>
        <translation>The help pages are extended to the other products.</translation>
    </message>
    <message>
        <source>Legyen-e nyomtatható változata a súgóoldalaknak?</source>
        <translation>Should the help pages have a printable version?</translation>
    </message>
    <message>
        <source>Ütemterv a kiterjesztéshez</source>
        <translation>Schedule for the rollout</translation>
    </message>
    <message>
        <source>Számlázási folyamat</source>
        <translation>Billing process</translation>
    </message>
    <message>
        <source>A partnerek szerint a gond nem a leírás, hanem maga a folyamat: túl sok lépés, kevés visszajelzés.</source>
        <translation>According to the partners the problem is not the documentation but the process itself: too many steps, too little feedback.</translation>
    </message>
    <message>
        <source>Nyitás, napirend</source>
        <translation>Opening, agenda</translation>
    </message>
    <message>
        <source>A partnerek kérték, hogy a számlázás külön napirendi pont legyen.</source>
        <translation>The partners asked for billing to be a separate agenda item.</translation>
    </message>
    <message>
        <source>Támogatási jegyek alakulása</source>
        <translation>Support tickets</translation>
    </message>
    <message>
        <source>A jegyek száma harmadával csökkent az előző negyedévhez képest.</source>
        <translation>The number of tickets fell by a third compared with the previous quarter.</translation>
    </message>
    <message>
        <source>Az aktív felhasználók száma közben 12%-kal nőtt.</source>
        <translation>Meanwhile the number of active users grew by 12%.</translation>
    </message>
    <message>
        <source>A csökkenés nagyobb része a beállítási kérdéseknél jelentkezett.</source>
        <translation>Most of the drop came from setup questions.</translation>
    </message>
    <message>
        <source>A súgóoldalak hatása</source>
        <translation>Effect of the help pages</translation>
    </message>
    <message>
        <source>A partnerek ugyanezt tapasztalják a saját ügyfélszolgálatukon.</source>
        <translation>The partners see the same at their own support desks.</translation>
    </message>
    <message>
        <source>A súgót a többi termékre is kiterjesztik.</source>
        <translation>The help is extended to the other products.</translation>
    </message>
    <message>
        <source>A kiterjesztés sorrendje</source>
        <translation>Order of the rollout</translation>
    </message>
    <message>
        <source>Elsőként a riportmodul kap súgóoldalakat, utána az integrációk.</source>
        <translation>The reporting module gets help pages first, then the integrations.</translation>
    </message>
    <message>
        <source>Számlázási panaszok</source>
        <translation>Billing complaints</translation>
    </message>
    <message>
        <source>A számlázási jegyek száma nem csökkent; a panaszok fele a számla módosításáról szól.</source>
        <translation>Billing tickets did not decrease; half of the complaints are about changing an invoice.</translation>
    </message>
    <message>
        <source>Folyamat vagy dokumentáció</source>
        <translation>Process or documentation</translation>
    </message>
    <message>
        <source>Két partner a lépések számát tartja a fő gondnak.</source>
        <translation>Two partners see the number of steps as the main problem.</translation>
    </message>
    <message>
        <source>Abban maradtak, hogy előbb a folyamatot egyszerűsítik, utána a leírást.</source>
        <translation>They agreed to simplify the process first, then the documentation.</translation>
    </message>
    <message>
        <source>Elakadási pontok gyűjtése</source>
        <translation>Collecting the sticking points</translation>
    </message>
    <message>
        <source>A gyűjtéshez a jegyek címkéit és a munkamenet-felvételeket használják.</source>
        <translation>They use the ticket tags and the session recordings for this.</translation>
    </message>
    <message>
        <source>Új ügyfelek bevezetése</source>
        <translation>Onboarding new customers</translation>
    </message>
    <message>
        <source>Az új ügyfelek az első két hétben háromszor annyi jegyet nyitnak.</source>
        <translation>New customers open three times as many tickets in their first two weeks.</translation>
    </message>
    <message>
        <source>Bevezető levelek</source>
        <translation>Welcome emails</translation>
    </message>
    <message>
        <source>A bevezető levelek megnyitási aránya 40% alatt van.</source>
        <translation>The open rate of the welcome emails is below 40%.</translation>
    </message>
    <message>
        <source>Javaslat: a levelek a súgóoldalakra mutassanak, ne a dokumentációra.</source>
        <translation>Proposal: the emails should link to the help pages, not the documentation.</translation>
    </message>
    <message>
        <source>Partneri visszajelzések</source>
        <translation>Partner feedback</translation>
    </message>
    <message>
        <source>A partnerek a keresőt dicsérték, a nyomtatható változatot hiányolják.</source>
        <translation>The partners praised the search and miss a printable version.</translation>
    </message>
    <message>
        <source>Nyomtatható súgó</source>
        <translation>Printable help</translation>
    </message>
    <message>
        <source>Nem döntöttek; a nyomtatható változat igényét előbb felmérik.</source>
        <translation>No decision; they first find out how much a printable version is needed.</translation>
    </message>
    <message>
        <source>Dokumentációs kapacitás</source>
        <translation>Documentation capacity</translation>
    </message>
    <message>
        <source>A dokumentációs csapat két fővel dolgozik, ez a kiterjesztéshez kevés.</source>
        <translation>The documentation team has two people, which is not enough for the rollout.</translation>
    </message>
    <message>
        <source>Költségvetési keret</source>
        <translation>Budget</translation>
    </message>
    <message>
        <source>A létszámbővítés legkorábban a jövő évi tervben szerepelhet.</source>
        <translation>More staff can be in next year’s plan at the earliest.</translation>
    </message>
    <message>
        <source>Döntés a létszámról</source>
        <translation>Decision on staffing</translation>
    </message>
    <message>
        <source>A dokumentációs létszámról a költségvetési tervezés dönt.</source>
        <translation>Budget planning decides on documentation staffing.</translation>
    </message>
    <message>
        <source>Külső szövegíró</source>
        <translation>Outside writer</translation>
    </message>
    <message>
        <source>Átmeneti megoldásként felmerült egy külső szövegíró; nem döntöttek.</source>
        <translation>An outside writer came up as a temporary solution; no decision.</translation>
    </message>
    <message>
        <source>Mérőszámok</source>
        <translation>Metrics</translation>
    </message>
    <message>
        <source>A jegyszám mellett a megoldási időt is mérik; ez nem javult.</source>
        <translation>Besides the ticket count they also measure resolution time; it did not improve.</translation>
    </message>
    <message>
        <source>A következő negyedév céljai</source>
        <translation>Goals for next quarter</translation>
    </message>
    <message>
        <source>Cél a számlázási jegyek 20%-os csökkentése.</source>
        <translation>The goal is 20% fewer billing tickets.</translation>
    </message>
    <message>
        <source>Riportok a partnereknek</source>
        <translation>Reports for the partners</translation>
    </message>
    <message>
        <source>A partnerek havi bontású riportot kérnek a jegyekről.</source>
        <translation>The partners ask for a monthly ticket report.</translation>
    </message>
    <message>
        <source>A riport formája</source>
        <translation>Report format</translation>
    </message>
    <message>
        <source>A riport táblázatként megy ki, grafikon nélkül.</source>
        <translation>The report goes out as a table, without charts.</translation>
    </message>
    <message>
        <source>Partnerportál</source>
        <translation>Partner portal</translation>
    </message>
    <message>
        <source>A partnerportál frissítése a következő negyedévre csúszik.</source>
        <translation>The partner portal update slips to next quarter.</translation>
    </message>
    <message>
        <source>Kapcsolattartók</source>
        <translation>Contacts</translation>
    </message>
    <message>
        <source>Minden partnernél egy állandó kapcsolattartó lesz.</source>
        <translation>Each partner gets one permanent contact person.</translation>
    </message>
    <message>
        <source>Új-ügyfél bontás</source>
        <translation>New-customer breakdown</translation>
    </message>
    <message>
        <source>Nyitva maradt ügyek</source>
        <translation>Open items</translation>
    </message>
    <message>
        <source>Nyitva maradt a nyomtatható súgó és a külső szövegíró kérdése.</source>
        <translation>The printable help and the outside writer remain open.</translation>
    </message>
    <message>
        <source>Zárás</source>
        <translation>Closing</translation>
    </message>
    <message>
        <source>A következő partnertalálkozó januárban lesz.</source>
        <translation>The next partner meeting is in January.</translation>
    </message>
    <message>
        <source>Elhangzott érvek</source>
        <translation>Arguments raised</translation>
    </message>
    <message>
        <source>Nyitott kérdés</source>
        <translation>Open question</translation>
    </message>
    <message>
        <source>a kiemelt szakasz: %1</source>
        <translation>highlighted section: %1</translation>
    </message>
    <message>
        <source>kék: a kijelölt állítás forrása · halvány: az összes állításé</source>
        <translation>blue: the source of the selected claim · faint: the sources of all claims</translation>
    </message>
    <message>
        <source>kék: a javítás által érintett források</source>
        <translation>blue: the sources affected by the correction</translation>
    </message>
    <message>
        <source>halvány: az összes állítás forrása</source>
        <translation>faint: the sources of all claims</translation>
    </message>
    <message>
        <source>Memó</source>
        <translation>Memo</translation>
    </message>
    <message numerus="yes">
        <source>%n szakasz</source>
        <translation>
            <numerusform>%n section</numerusform>
            <numerusform>%n sections</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>azóta %n beszélő-javítás</source>
        <translation>
            <numerusform>%n speaker correction since</numerusform>
            <numerusform>%n speaker corrections since</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n megszólalásból</source>
        <translation>
            <numerusform>from %n utterance</numerusform>
            <numerusform>from %n utterances</numerusform>
        </translation>
    </message>
    <message>
        <source>Az első és legfontosabb: a támogatási jegyek száma harmadával csökkent, miközben az aktív felhasználók száma nagyjából tizenkét százalékkal nőtt.</source>
        <translation>First and most important: the number of support tickets fell by a third while the number of active users grew by about twelve percent.</translation>
    </message>
    <message>
        <source>Az összesre. Az újaknál még jobb az arány, de ott kicsi a minta.</source>
        <translation>On the total. The ratio is even better for new users, but the sample is small there.</translation>
    </message>
    <message>
        <source>A súgóoldalak júliusban mentek élesbe, és az első hat hétben a keresések kétharmada ott ért véget.</source>
        <translation>The help pages went live in July, and in the first six weeks two thirds of searches ended there.</translation>
    </message>
    <message>
        <source>Nálunk a beállítási kérdések szinte eltűntek, a számlázásiak viszont maradtak, főleg a Nordvik-ügyfeleknél.</source>
        <translation>On our side, setup questions have almost disappeared, but the billing ones remain, mainly with Nordvik customers.</translation>
    </message>
    <message>
        <source>Az első változatot megcsinálom a dokumentációs csapattal, október közepére.</source>
        <translation>I&apos;ll do the first version with the documentation team, by mid-October.</translation>
    </message>
    <message>
        <source>A negyedik negyedév elejére kell, különben a megújításnál újra előjön.</source>
        <translation>It&apos;s needed by the start of the fourth quarter, otherwise it will come up again at the renewal.</translation>
    </message>
    <message>
        <source>Akkor a súgóoldalakat a többi termékre is kiterjesztjük.</source>
        <translation>Then we&apos;ll extend the help pages to the other products too.</translation>
    </message>
    <message>
        <source>Én a számlázást javasolnám következő súgó-témának, ott a jegyek fele számlázási.</source>
        <translation>I&apos;d suggest billing as the next help topic; half the tickets there are about billing.</translation>
    </message>
    <message>
        <source>Vállalom, de két hét dokumentációs kapacitás kell hozzá.</source>
        <translation>I&apos;ll take it, but it needs two weeks of documentation capacity.</translation>
    </message>
    <message>
        <source>A számlázási kérdések felét a díjbekérők adják; ha azokat külön kezeljük, a maradék is eltűnhet.</source>
        <translation>Half of the billing questions come from proforma invoices; if we handle those separately, the rest may disappear too.</translation>
    </message>
    <message>
        <source>Ha a súgó nem lesz kész a negyedév elejére, a megújításnál újra előjön ugyanez.</source>
        <translation>If the help center is not ready by the start of the quarter, the same issue will come up again at renewal.</translation>
    </message>
    <message>
        <source>A Nordvik-tárgyalás pont akkor indul, addig kell valami kézzelfogható.</source>
        <translation>The Nordvik negotiation starts right then, so we need something tangible by that time.</translation>
    </message>
    <message>
        <source>A partnerportálon a leggyakoribb keresés a „számla letöltése”.</source>
        <translation>The most common search on the partner portal is &quot;download invoice&quot;.</translation>
    </message>
    <message>
        <source>És ki adja a dokumentációs kapacitást, ha közben a kiterjesztésen dolgoztok?</source>
        <translation>And who provides the documentation capacity if you are working on the extension in the meantime?</translation>
    </message>
    <message>
        <source>A díjbekérős kérdéseket érdemes lenne külön kezelni a súgóban.</source>
        <translation>It would be worth handling the proforma invoice questions separately in the help.</translation>
    </message>
    <message>
        <source>A nyomtatható változatot többen kérdezték, de nem tudom, megéri-e.</source>
        <translation>Several people asked about the printable version, but I am not sure it is worth it.</translation>
    </message>
    <message>
        <source>A létszámról a költségvetési tervezésnél döntünk.</source>
        <translation>We&apos;ll decide on headcount during budget planning.</translation>
    </message>
    <message>
        <source>A harmadik negyedévben a támogatási jegyek száma harmadával csökkent, miközben az aktív felhasználók száma 12%-kal nőtt.</source>
        <translation>In the third quarter the number of support tickets fell by a third while the number of active users grew by 12%.</translation>
    </message>
    <message>
        <source>A javulás fő oka a júliusban élesített súgóoldalak: az első hat hétben a keresések kétharmada ott ért véget.</source>
        <translation>The main reason for the improvement is the help pages that went live in July: in the first six weeks two thirds of searches ended there.</translation>
    </message>
    <message>
        <source>A partnerek oldalán a beállítási kérdések szinte eltűntek, a számlázási kérdések viszont maradtak, főleg a Nordvik-ügyfeleknél.</source>
        <translation>On the partners&apos; side, setup questions have almost disappeared, but billing questions remain, mainly with Nordvik customers.</translation>
    </message>
    <message>
        <source>A számlázási súgónak a negyedik negyedév elejére el kell készülnie, különben a megújításnál újra előjön a probléma.</source>
        <translation>The billing help must be ready by the start of the fourth quarter, otherwise the problem will come up again at the renewal.</translation>
    </message>
    <message>
        <source>A számlázásnál előbb a folyamatot egyszerűsítik, utána a leírást.</source>
        <translation>For billing, the process is simplified first, then the documentation.</translation>
    </message>
    <message>
        <source>Számlázási súgó első változata a dokumentációs csapattal</source>
        <translation>First version of the billing help with the documentation team</translation>
    </message>
    <message>
        <source>Díjbekérő-kérdések külön kezelése a súgóban</source>
        <translation>Handle proforma invoice questions separately in the help</translation>
    </message>
    <message>
        <source>okt. 22.</source>
        <translation>Oct 22</translation>
    </message>
    <message>
        <source>Ki adja a dokumentációs kapacitást, ha a csapat a negyedik negyedévben a súgó kiterjesztésén dolgozik?</source>
        <translation>Who provides the documentation capacity if the team works on extending the help center in the fourth quarter?</translation>
    </message>
    <message>
        <source>Megéri-e elkészíteni a súgó nyomtatható változatát?</source>
        <translation>Is it worth producing a printable version of the help center?</translation>
    </message>
    <message>
        <source>Nyitás, Q3 számok</source>
        <translation>Opening, Q3 numbers</translation>
    </message>
    <message>
        <source>Súgóoldalak</source>
        <translation>Help pages</translation>
    </message>
    <message>
        <source>A júliusban élesített súgóoldalak az első hat hétben a keresések kétharmadát lezárták.</source>
        <translation>The help pages that went live in July closed two thirds of searches in the first six weeks.</translation>
    </message>
    <message>
        <source>Számlázási súgó és a Nordvik-megújítás</source>
        <translation>Billing help and the Nordvik renewal</translation>
    </message>
    <message>
        <source>A partnerportál keresési adatai alapján a „számla letöltése” a leggyakoribb keresés.</source>
        <translation>According to the partner portal search data, &quot;download invoice&quot; is the most common search.</translation>
    </message>
    <message>
        <source>A negyedév számai jók, a számlázás külön napirendi pontot kapott.</source>
        <translation>The quarter&apos;s numbers are good; billing got a separate agenda item.</translation>
    </message>
    <message>
        <source>A díjbekérőkkel kapcsolatos kérdések a számlázási kérdések felét adják; külön kezelve a maradék is eltűnhet.</source>
        <translation>Questions about proforma invoices make up half of the billing questions; handled separately, the rest may disappear too.</translation>
    </message>
    <message>
        <source>Ha a súgó nem készül el a negyedév elejére, a megújítási tárgyalásokon újra előjön a probléma.</source>
        <translation>If the help isn&apos;t ready by the start of the quarter, the problem will come up again in the renewal talks.</translation>
    </message>
    <message>
        <source>A díjbekérős kérdéseket külön kezelik a súgóban.</source>
        <translation>Proforma invoice questions will be handled separately in the help.</translation>
    </message>
    <message>
        <source>Nordvik-megújítás</source>
        <translation>Nordvik renewal</translation>
    </message>
    <message>
        <source>A Nordvik-szerződés megújítása a számlázási súgón múlik.</source>
        <translation>Renewing the Nordvik contract depends on the billing help.</translation>
    </message>
    <message>
        <source>A tárgyalás a negyedik negyedév elején indul.</source>
        <translation>Negotiations start at the beginning of the fourth quarter.</translation>
    </message>
    <message>
        <source>Költségvetés</source>
        <translation>Budget</translation>
    </message>
    <message>
        <source>Lezárás, teendők</source>
        <translation>Wrap-up, action items</translation>
    </message>
</context>
<context>
    <name>tanara_qml::TagDemoBackend</name>
    <message>
        <source>Címke a személyen</source>
        <translation>Tag on a person</translation>
    </message>
    <message>
        <source>Címke levétele a személyről</source>
        <translation>Remove tag from a person</translation>
    </message>
    <message>
        <source>Javaslat elutasítása</source>
        <translation>Dismiss suggestion</translation>
    </message>
    <message>
        <source>Címke hozzáadva</source>
        <translation>Tag added</translation>
    </message>
    <message>
        <source>Címke eltávolítva</source>
        <translation>Tag removed</translation>
    </message>
    <message>
        <source>Javaslat elutasítva</source>
        <translation>Suggestion dismissed</translation>
    </message>
    <message>
        <source>Átnevezés</source>
        <translation>Rename</translation>
    </message>
    <message>
        <source>Összevonás</source>
        <translation>Merge</translation>
    </message>
    <message>
        <source>Címke törölve</source>
        <translation>Tag deleted</translation>
    </message>
</context>
<context>
    <name>tanara_qml::TagsViewModel</name>
    <message>
        <source>ABC</source>
        <translation>A–Z</translation>
    </message>
    <message>
        <source>Leggyakoribb</source>
        <translation>Most used</translation>
    </message>
    <message>
        <source>Legutóbb használt</source>
        <translation>Recently used</translation>
    </message>
    <message>
        <source>%1 / %2 címke</source>
        <translation>%1 / %2 tags</translation>
    </message>
    <message>
        <source>%1 címke</source>
        <translation>%1 tags</translation>
    </message>
    <message>
        <source>%1 megbeszélés · %2</source>
        <translation>%1 meetings · %2</translation>
    </message>
    <message>
        <source>Még senki. Ha %1 #%2 megbeszéléseken elnevezed a beszélőket, itt megjelennek.</source>
        <translation>Nobody yet. If you name the speakers in meetings tagged #%2, %1 will appear here.</translation>
    </message>
    <message>
        <source>%1 megbeszélés</source>
        <translation>%1 meetings</translation>
    </message>
    <message numerus="yes">
        <source>%n személy</source>
        <translation>
            <numerusform>%n person</numerusform>
            <numerusform>%n people</numerusform>
        </translation>
    </message>
    <message>
        <source>először: %1 · utoljára: %2</source>
        <translation>first: %1 · last: %2</translation>
    </message>
    <message>
        <source>%1 · legújabb elöl</source>
        <translation>%1 · newest first</translation>
    </message>
    <message>
        <source>A címke már nem létezik.</source>
        <translation>The tag no longer exists.</translation>
    </message>
    <message>
        <source>Adj meg egy nevet.</source>
        <translation>Enter a name.</translation>
    </message>
    <message>
        <source>Átnevezve: #%1 → #%2</source>
        <translation>Renamed: #%1 → #%2</translation>
    </message>
    <message>
        <source>Már van „%1” nevű címke. Ha ugyanazt jelentik, vond össze őket.</source>
        <translation>A tag named &quot;%1&quot; already exists. If they mean the same thing, merge them.</translation>
    </message>
    <message>
        <source>%1 megbeszélés · hasonló név</source>
        <translation>%1 meetings · similar name</translation>
    </message>
    <message>
        <source>%1 megbeszélés · %2× szerepeltek együtt</source>
        <translation>%1 meetings · together %2×</translation>
    </message>
    <message>
        <source>Eredmény: &lt;b&gt;%1 megbeszélés&lt;/b&gt; (%2 közös). A „%3” név megszűnik; ha valaki beírja, a mező %4 #%5 címkét ajánlja.</source>
        <translation>Result: &lt;b&gt;%1 meetings&lt;/b&gt; (%2 shared). The name &quot;%3&quot; goes away; if someone types it, the input suggests %4 #%5 tag.</translation>
    </message>
    <message numerus="yes">
        <source>A címke %n személyen is egyesül.</source>
        <translation>
            <numerusform>The tag is merged on %n person too.</numerusform>
            <numerusform>The tag is merged on %n people too.</numerusform>
        </translation>
    </message>
    <message>
        <source>Válassz egy másik címkét.</source>
        <translation>Choose a different tag.</translation>
    </message>
    <message>
        <source>Összevonva: #%1 → #%2</source>
        <translation>Merged: #%1 → #%2</translation>
    </message>
    <message>
        <source>Törlöd %1 #%2 címkét?</source>
        <translation>Delete %1 #%2 tag?</translation>
    </message>
    <message numerus="yes">
        <source>%1 megbeszélésről és %n személyről lekerül.</source>
        <translation>
            <numerusform>It is removed from %1 meetings and %n person.</numerusform>
            <numerusform>It is removed from %1 meetings and %n people.</numerusform>
        </translation>
    </message>
    <message>
        <source>%1 megbeszélésről lekerül.</source>
        <translation>It is removed from %1 meetings.</translation>
    </message>
    <message>
        <source>Maguk a megbeszélések, átiratok és összefoglalók megmaradnak. A címke profilja és az együtt járási adatai törlődnek.</source>
        <translation>The meetings, transcripts and summaries themselves remain. The tag&apos;s profile and co-occurrence data are deleted.</translation>
    </message>
    <message>
        <source>#%1 rákerült: %2</source>
        <translation>#%1 applied: %2</translation>
    </message>
    <message>
        <source>#%1 levéve: %2</source>
        <translation>#%1 removed: %2</translation>
    </message>
    <message>
        <source>Nem tartozik ide: %1</source>
        <translation>Doesn&apos;t belong here: %1</translation>
    </message>
    <message>
        <source>Címke törölve: #%1</source>
        <translation>Tag deleted: #%1</translation>
    </message>
</context>
<context>
    <name>tanara_qml::TopicListModel</name>
    <message numerus="yes">
        <source>%n téma</source>
        <translation>
            <numerusform>%n topic</numerusform>
            <numerusform>%n topics</numerusform>
        </translation>
    </message>
    <message>
        <source>%1 kész</source>
        <translation>%1 done</translation>
    </message>
    <message>
        <source>%1 fut</source>
        <translation>%1 running</translation>
    </message>
    <message>
        <source>%1 sorban áll</source>
        <translation>%1 queued</translation>
    </message>
    <message>
        <source>%1 hibás</source>
        <translation>%1 failed</translation>
    </message>
    <message>
        <source>%1 vár</source>
        <translation>%1 waiting</translation>
    </message>
    <message>
        <source>Támogatási igény alakulása</source>
        <translation>Support demand trend</translation>
    </message>
    <message>
        <source>Jegyszám, felhasználónkénti arány, okok</source>
        <translation>Ticket count, per-user ratio, causes</translation>
    </message>
    <message>
        <source>A jegyek száma harmadával csökkent, miközben az aktív felhasználók 12%-kal nőttek. A csökkenés fő oka a termékbe épített súgó; a partnerek ugyanezt tapasztalják.</source>
        <translation>Tickets dropped by a third while active users grew by 12%. The main reason for the drop is the help built into the product; the partners are seeing the same.</translation>
    </message>
    <message>
        <source>A súgóoldalakat a többi termékre is kiterjesztik.</source>
        <translation>The help pages are extended to the other products.</translation>
    </message>
    <message>
        <source>Legyen-e nyomtatható változata a súgóoldalaknak?</source>
        <translation>Should the help pages have a printable version?</translation>
    </message>
    <message>
        <source>Súgóoldalak kiterjesztése</source>
        <translation>Extending the help pages</translation>
    </message>
    <message>
        <source>Melyik termékre, milyen sorrendben</source>
        <translation>Which products, in what order</translation>
    </message>
    <message>
        <source>A beállítási oldalak mintáját viszik tovább. A számlázás előtt a folyamatot egyszerűsítik.</source>
        <translation>The pattern of the settings pages is carried over. For billing, the process is simplified first.</translation>
    </message>
    <message>
        <source>Számlázási folyamat</source>
        <translation>Billing process</translation>
    </message>
    <message>
        <source>Termék vagy dokumentáció a gond?</source>
        <translation>Is the problem the product or the documentation?</translation>
    </message>
    <message>
        <source>Dokumentációs kapacitás</source>
        <translation>Documentation capacity</translation>
    </message>
    <message>
        <source>Létszámigény és költségvetés</source>
        <translation>Headcount need and budget</translation>
    </message>
    <message>
        <source>A szolgáltató időtúllépés miatt megszakította.</source>
        <translation>The provider aborted it due to a timeout.</translation>
    </message>
    <message>
        <source>Következő negyedév mérőszámai</source>
        <translation>Next quarter&apos;s metrics</translation>
    </message>
    <message>
        <source>Mit és hogyan mérünk</source>
        <translation>What we measure and how</translation>
    </message>
</context>
<context>
    <name>tanara_qml::TrackListModel</name>
    <message>
        <source>%1. szakasz</source>
        <translation>segment %1</translation>
    </message>
    <message>
        <source>kezdete: %1</source>
        <translation>starts at %1</translation>
    </message>
    <message>
        <source>Közben másik megbeszélésre váltottál, ezért a fájl nem lett hozzárendelve.</source>
        <translation>You switched to another meeting in the meantime, so the file was not assigned.</translation>
    </message>
    <message>
        <source>A fájlt nem sikerült a megbeszéléshez rendelni.</source>
        <translation>Could not assign the file to the meeting.</translation>
    </message>
    <message>
        <source>Saját mikrofon</source>
        <translation>My microphone</translation>
    </message>
    <message>
        <source>Hívás hangja</source>
        <translation>Call audio</translation>
    </message>
    <message>
        <source>Rendszerhang</source>
        <translation>System audio</translation>
    </message>
    <message>
        <source>Második mikrofon</source>
        <translation>Second microphone</translation>
    </message>
</context>
<context>
    <name>tanara_qml::TranscriptEditorViewModel</name>
    <message>
        <source>Egyéb (%1)</source>
        <translation>Other (%1)</translation>
    </message>
    <message>
        <source>Újra: %1</source>
        <translation>Redo: %1</translation>
    </message>
    <message numerus="yes">
        <source>%n sor átkerült ide: %1</source>
        <translation>
            <numerusform>%n line moved to %1</numerusform>
            <numerusform>%n lines moved to %1</numerusform>
        </translation>
    </message>
    <message>
        <source>%1 és %2 hangja hasonló. Nézzem át kettejük sorait a megerősítettek alapján?</source>
        <translation>%1 and %2 sound similar. Shall I review their lines based on the confirmed ones?</translation>
    </message>
    <message>
        <source>A megerősített sorok alapján nem találtam kétséges sort %1 és %2 között.</source>
        <translation>Based on the confirmed lines, I found no doubtful line between %1 and %2.</translation>
    </message>
    <message>
        <source>Kevés megerősített sor (%1): az összes sorát vettem alapul.</source>
        <translation>Few confirmed lines (%1): I used all of their lines as the reference.</translation>
    </message>
    <message>
        <source>A két hang nagyon hasonló (%1), az eredmény bizonytalan — hallgass bele.</source>
        <translation>The two voices are very similar (%1), so the result is uncertain — listen to the lines.</translation>
    </message>
    <message>
        <source>Nincs letöltve a hangmodell, ezért a bizonytalan sorok jelölése, a hasonló sorok felajánlása és a kézi hanglenyomat most nem érhető el. A szerkesztés enélkül is működik.</source>
        <translation>The voice model is not downloaded, so marking uncertain lines, suggesting similar lines and manual voiceprints are not available right now. Editing works without them.</translation>
    </message>
    <message>
        <source>A megbeszélés lekevert hangja nem érhető el, ezért a bizonytalan sorok jelölése, a hasonló sorok felajánlása és a kézi hanglenyomat most nem érhető el. A szerkesztés enélkül is működik.</source>
        <translation>The meeting&apos;s mixdown audio is not available, so marking uncertain lines, suggesting similar lines and manual voiceprints are not available right now. Editing works without them.</translation>
    </message>
    <message numerus="yes">
        <source>Új személy: %1. %n sor átkerült hozzá.</source>
        <translation>
            <numerusform>New person: %1. %n line moved to them.</numerusform>
            <numerusform>New person: %1. %n lines moved to them.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor: nem minta (%1)</source>
        <translation>
            <numerusform>%n line: not a sample (%1)</numerusform>
            <numerusform>%n lines: not a sample (%1)</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor marad: %1</source>
        <translation>
            <numerusform>%n line stays: %1</numerusform>
            <numerusform>%n lines stay: %1</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor nem azonosítható</source>
        <translation>
            <numerusform>%n line unidentifiable</numerusform>
            <numerusform>%n lines unidentifiable</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor újra azonosítható</source>
        <translation>
            <numerusform>%n line identifiable again</numerusform>
            <numerusform>%n lines identifiable again</numerusform>
        </translation>
    </message>
    <message>
        <source>Jó így: %1</source>
        <translation>Looks right: %1</translation>
    </message>
    <message numerus="yes">
        <source>%n sor: Jó így (%1)</source>
        <translation>
            <numerusform>%n line: looks right (%1)</numerusform>
            <numerusform>%n lines: look right (%1)</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%1 mind az %n sora</source>
        <translation>
            <numerusform>All %n line of %1</numerusform>
            <numerusform>All %n lines of %1</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%1 mind a %n sora</source>
        <translation>
            <numerusform>All %n line of %1</numerusform>
            <numerusform>All %n lines of %1</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%1 mind az %n sora átkerült ide: %2</source>
        <translation>
            <numerusform>All %n line of %1 moved to %2</numerusform>
            <numerusform>All %n lines of %1 moved to %2</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%1 mind a %n sora átkerült ide: %2</source>
        <translation>
            <numerusform>All %n line of %1 moved to %2</numerusform>
            <numerusform>All %n lines of %1 moved to %2</numerusform>
        </translation>
    </message>
    <message>
        <source>A most készült hanglenyomat törölve: %1</source>
        <translation>The voiceprint just created was removed: %1</translation>
    </message>
    <message>
        <source>A sor nem minta</source>
        <translation>The line is not a sample</translation>
    </message>
    <message>
        <source>A sor mintának használható</source>
        <translation>The line can be used as a sample</translation>
    </message>
    <message numerus="yes">
        <source>%n megszólalás a vágólapra másolva.</source>
        <translation>
            <numerusform>%n utterance copied to the clipboard.</numerusform>
            <numerusform>%n utterances copied to the clipboard.</numerusform>
        </translation>
    </message>
    <message>
        <source>Előbb nevezd el a beszélőt.</source>
        <translation>Name the speaker first.</translation>
    </message>
    <message>
        <source>Egymásra beszéltek — nem tiszta minta.</source>
        <translation>Cross-talk — not a clean sample.</translation>
    </message>
    <message>
        <source>Túl rövid mintának (legalább 3 mp kell).</source>
        <translation>Too short for a sample (at least 3 s is needed).</translation>
    </message>
    <message>
        <source>Nincs hangmodell vagy lekevert hang.</source>
        <translation>No voice model or mixdown audio.</translation>
    </message>
    <message>
        <source>Hanglenyomat-minta készült: %1 (ebből a sorból, %2 mp).</source>
        <translation>Voice sample created: %1 (from this line, %2 s).</translation>
    </message>
    <message>
        <source>Hanglenyomat készült: %1 (%2 sorból, %3 mp beszédből).</source>
        <translation>Voiceprint created: %1 (from %2 lines, %3 s of speech).</translation>
    </message>
    <message>
        <source>a hívás hangján</source>
        <translation>on the call audio</translation>
    </message>
    <message>
        <source>a mikrofonon</source>
        <translation>on the microphone</translation>
    </message>
    <message>
        <source>hívás</source>
        <translation>call</translation>
    </message>
    <message>
        <source>mikrofon</source>
        <translation>microphone</translation>
    </message>
    <message>
        <source>Hang:</source>
        <translation>Voice:</translation>
    </message>
    <message>
        <source>%1, %2</source>
        <translation>%1, %2</translation>
    </message>
    <message>
        <source>Sáv:</source>
        <translation>Track:</translation>
    </message>
    <message>
        <source>ugyanaz a sáv</source>
        <translation>same track</translation>
    </message>
    <message>
        <source>beszél</source>
        <translation>speaks</translation>
    </message>
    <message>
        <source>%1 beszél</source>
        <translation>%1 speaks</translation>
    </message>
    <message>
        <source> (kézi sáv-beosztás)</source>
        <translation> (manual track assignment)</translation>
    </message>
    <message>
        <source>Címke:</source>
        <translation>Tag:</translation>
    </message>
    <message>
        <source>%1 mp</source>
        <translation>%1 s</translation>
    </message>
    <message>
        <source>%1 p %2 mp</source>
        <translation>%1 min %2 s</translation>
    </message>
    <message>
        <source>nem azonosítható</source>
        <translation>unidentifiable</translation>
    </message>
    <message>
        <source>újra azonosítható</source>
        <translation>identifiable again</translation>
    </message>
    <message>
        <source>nem minta</source>
        <translation>not a sample</translation>
    </message>
    <message>
        <source>minta lett</source>
        <translation>became a sample</translation>
    </message>
    <message>
        <source>mind minta lett</source>
        <translation>all became samples</translation>
    </message>
    <message numerus="yes">
        <source>%n minta lett</source>
        <translation>
            <numerusform>%n became a sample</numerusform>
            <numerusform>%n became samples</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n másik sor javaslata változott</source>
        <translation>
            <numerusform>the suggestion of %n other line changed</numerusform>
            <numerusform>the suggestions of %n other lines changed</numerusform>
        </translation>
    </message>
    <message>
        <source>Visszavonva: %1</source>
        <translation>Undone: %1</translation>
    </message>
    <message>
        <source>Ez a minta eltér %1 többi sorától (hang %2%). Biztos?</source>
        <translation>This sample differs from the other lines of %1 (voice %2%). Are you sure?</translation>
    </message>
    <message numerus="yes">
        <source>%n minta</source>
        <translation>
            <numerusform>%n sample</numerusform>
            <numerusform>%n samples</numerusform>
        </translation>
    </message>
    <message>
        <source>+%1 most</source>
        <translation>+%1 now</translation>
    </message>
    <message numerus="yes">
        <source>%n kétséges sor %1 és %2 között — az Átnézendő szűrőben.</source>
        <translation>
            <numerusform>%n doubtful line between %1 and %2 — in the To review filter.</numerusform>
            <numerusform>%n doubtful lines between %1 and %2 — in the To review filter.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n kétséges sort jelöltem meg — az Átnézendő szűrőben találod.</source>
        <translation>
            <numerusform>I marked %n doubtful line — you will find it in the To review filter.</numerusform>
            <numerusform>I marked %n doubtful lines — you will find them in the To review filter.</numerusform>
        </translation>
    </message>
    <message>
        <source>A megerősített sorok alapján nem találtam kétséges sort.</source>
        <translation>Based on the confirmed lines, no doubtful line was found.</translation>
    </message>
    <message>
        <source>Figyelem:</source>
        <translation>Attention:</translation>
    </message>
    <message>
        <source>%1 hangja nagyon hasonló (%2)</source>
        <translation>%1&apos;s voice is very similar (%2)</translation>
    </message>
    <message>
        <source>Sorok:</source>
        <translation>Lines:</translation>
    </message>
    <message>
        <source>Naptár:</source>
        <translation>Calendar:</translation>
    </message>
    <message>
        <source>Sáv-beosztás</source>
        <translation>Track assignment</translation>
    </message>
    <message>
        <source>Módosítás</source>
        <translation>Edit</translation>
    </message>
    <message>
        <source>Minták</source>
        <translation>Samples</translation>
    </message>
    <message>
        <source>Címkéi</source>
        <translation>Tags</translation>
    </message>
    <message>
        <source>Kettejük átnézése</source>
        <translation>Review the two</translation>
    </message>
    <message>
        <source>a sor %1 jött, %2 %3 beszél.</source>
        <translation>the line came %1, %2 speaks %3.</translation>
    </message>
    <message>
        <source>a megerősített sorai két különböző hangra esnek (%1)</source>
        <translation>its confirmed lines fall into two different voices (%1)</translation>
    </message>
    <message>
        <source>két hang</source>
        <translation>two voices</translation>
    </message>
    <message>
        <source>Szétválasztás</source>
        <translation>Split</translation>
    </message>
    <message>
        <source>Saját mikrofon</source>
        <translation>My microphone</translation>
    </message>
    <message>
        <source>Hívás hangja</source>
        <translation>Call audio</translation>
    </message>
    <message>
        <source>+ #%1</source>
        <translation>+ #%1</translation>
    </message>
    <message>
        <source>több sávon</source>
        <translation>on several tracks</translation>
    </message>
    <message>
        <source>kézi beosztás: %1</source>
        <translation>manual assignment: %1</translation>
    </message>
    <message>
        <source>tanult: jellemzően %1</source>
        <translation>learned: typically %1</translation>
    </message>
    <message>
        <source>a megerősített sorai alapján: %1</source>
        <translation>based on its confirmed lines: %1</translation>
    </message>
    <message>
        <source>te vagy: a saját mikrofonodon</source>
        <translation>that&apos;s you: on your own microphone</translation>
    </message>
    <message>
        <source>%1 sorai két különböző hangnak tűnnek</source>
        <translation>%1&apos;s lines seem to be two different voices</translation>
    </message>
    <message>
        <source>A hívás hangján érkezett, de %1 nevén van</source>
        <translation>It came in on the call audio, but it is under %1&apos;s name</translation>
    </message>
    <message>
        <source>A mikrofonon érkezett, de %1 nevén van</source>
        <translation>It came in on the microphone, but it is under %1&apos;s name</translation>
    </message>
    <message>
        <source>A másik sávon érkezett, de %1 nevén van</source>
        <translation>It came in on the other track, but it is under %1&apos;s name</translation>
    </message>
    <message>
        <source>új beszélő (hívás hangja)</source>
        <translation>new speaker (call audio)</translation>
    </message>
    <message>
        <source>új beszélő (mikrofon)</source>
        <translation>new speaker (microphone)</translation>
    </message>
    <message>
        <source>Nincs azonos sávon beszélő jelölt</source>
        <translation>No candidate speaks on the same track</translation>
    </message>
    <message>
        <source>Javasolt: %1</source>
        <translation>Suggested: %1</translation>
    </message>
    <message>
        <source>Hang alapján valószínűleg %1 mondta</source>
        <translation>By voice, it was probably %1</translation>
    </message>
    <message>
        <source>Most %1 nevén.</source>
        <translation>Currently under %1&apos;s name.</translation>
    </message>
    <message>
        <source>Rövid sorok: ezeken a hang nem segít</source>
        <translation>Short lines: voice doesn&apos;t help on these</translation>
    </message>
    <message>
        <source>%1 mp alattiak, csak fülre dönthetők; nem számítanak az átnézendők közé.</source>
        <translation>Shorter than %1 s, can be decided only by ear; they don&apos;t count as lines to review.</translation>
    </message>
    <message>
        <source>Hangra kétes sorok</source>
        <translation>Lines doubtful by voice</translation>
    </message>
    <message>
        <source>A hang alapján nem dönthető el biztosan, kié — egyenként érdemes meghallgatni.</source>
        <translation>It can&apos;t be decided with certainty by voice whose it is — worth listening to one by one.</translation>
    </message>
    <message>
        <source>Elég tiszta beszéd van a hanglenyomathoz (%1 mp).</source>
        <translation>There is enough clean speech for the voiceprint (%1 s).</translation>
    </message>
    <message>
        <source>Hanglenyomathoz még kb. %1 mp tiszta beszéd kell (most %2 mp).</source>
        <translation>The voiceprint needs about %1 s more clean speech (now %2 s).</translation>
    </message>
    <message>
        <source>Hanglenyomat készítése</source>
        <translation>Create voiceprint</translation>
    </message>
    <message>
        <source>Elkészül, ha elég lesz</source>
        <translation>Created when there is enough</translation>
    </message>
    <message>
        <source>Hanglenyomat, ha elég</source>
        <translation>Voiceprint, when enough</translation>
    </message>
    <message numerus="yes">
        <source>Szétválasztás: %n sor átkerült ide: %1</source>
        <translation>
            <numerusform>Split: %n line moved to %1</numerusform>
            <numerusform>Split: %n lines moved to %1</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n sor megerősítve</source>
        <translation>
            <numerusform>%n line confirmed</numerusform>
            <numerusform>%n lines confirmed</numerusform>
        </translation>
    </message>
    <message>
        <source>Mind az %1 → %2</source>
        <translation>All %1 → %2</translation>
    </message>
    <message>
        <source>Mind a %1 → %2</source>
        <translation>All %1 → %2</translation>
    </message>
</context>
<context>
    <name>tanara_qml::VoiceModelListModel</name>
    <message>
        <source>A csomagban · tartalék.</source>
        <translation>Bundled · fallback.</translation>
    </message>
    <message>
        <source>Tartalék: ezt használja, amíg a WeSpeaker nincs letöltve.</source>
        <translation>Fallback: used until WeSpeaker is downloaded.</translation>
    </message>
    <message>
        <source>Ajánlott alap: négy valódi megbeszélésen ez választotta szét legjobban a beszélőket.</source>
        <translation>Recommended default: on four real meetings it separated the speakers best.</translation>
    </message>
    <message>
        <source>Rövid sorokra; magyar beszéden nem ajánlott.</source>
        <translation>For short lines; not recommended for Hungarian speech.</translation>
    </message>
    <message>
        <source>a szerzők megnevezésével</source>
        <translation>with attribution to the authors</translation>
    </message>
    <message>
        <source>%1 dim.</source>
        <translation>%1 dims</translation>
    </message>
    <message>
        <source>A letöltés megszakadt: a kapcsolat 12 MB után megszűnt. Semmi nem került a gépre.</source>
        <translation>The download was interrupted: the connection dropped after 12 MB. Nothing was saved to your machine.</translation>
    </message>
    <message>
        <source>A fájl mérete eltér a várttól (%1 helyett %2) — töltsd le újra.</source>
        <translation>The file size differs from expected (%2 instead of %1) — download it again.</translation>
    </message>
    <message>
        <source>törölt megbeszélés</source>
        <translation>deleted meeting</translation>
    </message>
    <message>
        <source>Ismeretlen hangmodell.</source>
        <translation>Unknown voice model.</translation>
    </message>
    <message>
        <source>Letöltés vagy ellenőrzés közben nem törölhető.</source>
        <translation>Cannot be deleted while downloading or verifying.</translation>
    </message>
    <message>
        <source>Ennek a modellnek nincs letöltött fájlja; a csomagban lévőt nem lehet törölni.</source>
        <translation>This model has no downloaded file; the bundled one cannot be deleted.</translation>
    </message>
    <message>
        <source>Nem sikerült törölni: %1</source>
        <translation>Could not delete: %1</translation>
    </message>
    <message>
        <source>A letöltött fájl mérete nem egyezik a várttal, ezért nem mentettem el.</source>
        <translation>The downloaded file&apos;s size does not match the expected one, so it was not saved.</translation>
    </message>
    <message>
        <source>Nem tudtam menteni a modellt a belső adatok mappájába.</source>
        <translation>Could not save the model to the internal data folder.</translation>
    </message>
    <message>
        <source>A letöltés nem sikerült: %1</source>
        <translation>The download failed: %1</translation>
    </message>
    <message numerus="yes">
        <source>A meglévő %n minta újraszámolása az új modellel… (%1 / %2)</source>
        <translation>
            <numerusform>Recalculating the existing %n sample with the new model… (%1 / %2)</numerusform>
            <numerusform>Recalculating the existing %n samples with the new model… (%1 / %2)</numerusform>
        </translation>
    </message>
    <message>
        <source>A meglévő minták újraszámolása az új modellel… folyamatban</source>
        <translation>Recalculating the existing samples with the new model… in progress</translation>
    </message>
    <message>
        <source>Kész: a meglévő minták az új modellel is megvannak.</source>
        <translation>Done: the existing samples are now also available with the new model.</translation>
    </message>
    <message numerus="yes">
        <source>%n minta nem volt pótolható az új modellel.</source>
        <translation>
            <numerusform>%n sample could not be backfilled with the new model.</numerusform>
            <numerusform>%n samples could not be backfilled with the new model.</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>tanara_watcher::TrayWatcher</name>
    <message>
        <source>Elemző megnyitása</source>
        <translation>Open analyzer</translation>
    </message>
    <message>
        <source>Felvevő megjelenítése</source>
        <translation>Show recorder</translation>
    </message>
    <message>
        <source>Felvétel indítása</source>
        <translation>Start recording</translation>
    </message>
    <message>
        <source>Felvétel leállítása</source>
        <translation>Stop recording</translation>
    </message>
    <message>
        <source>Beállítások…</source>
        <translation>Settings…</translation>
    </message>
    <message>
        <source>Kilépés…</source>
        <translation>Quit…</translation>
    </message>
    <message>
        <source>Tanara — felvétel fut · %1</source>
        <translation>Tanara — recording · %1</translation>
    </message>
    <message>
        <source>Tanara — figyel · nincs hívás</source>
        <translation>Tanara — watching · no call</translation>
    </message>
    <message>
        <source>Felvétel · %1</source>
        <translation>Recording · %1</translation>
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
        <source>Tanara — kilépés</source>
        <translation>Tanara — Quit</translation>
    </message>
    <message>
        <source>A felvétel még fut.</source>
        <translation>The recording is still running.</translation>
    </message>
    <message>
        <source>A figyelő kilépése nem állítja le a felvételt: a felvevő tovább rögzít, de a tálca-ikon eltűnik. Mit tegyek?</source>
        <translation>Quitting the watcher does not stop the recording: the recorder keeps recording, but the tray icon disappears. What should I do?</translation>
    </message>
    <message>
        <source>Kilépés, a felvétel fusson</source>
        <translation>Quit, keep recording</translation>
    </message>
    <message>
        <source>Leállítás és kilépés</source>
        <translation>Stop and quit</translation>
    </message>
    <message>
        <source>Mégse</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>Elindítsam a felvételt?</source>
        <translation>Start recording?</translation>
    </message>
    <message>
        <source>%1 most ezen a kimeneten szól: %2.</source>
        <translation>%1 is now playing on this output: %2.</translation>
    </message>
    <message>
        <source>Hívást észleltem: %1</source>
        <translation>Call detected: %1</translation>
    </message>
    <message>
        <source>Felvevő megnyitása</source>
        <translation>Open recorder</translation>
    </message>
    <message>
        <source>Nem most</source>
        <translation>Not now</translation>
    </message>
    <message>
        <source>Kattints ide a felvevő megnyitásához.</source>
        <translation>Click here to open the recorder.</translation>
    </message>
    <message>
        <source>Tanara — hívás észlelve: %1</source>
        <translation>Tanara — call detected: %1</translation>
    </message>
</context>
</TS>

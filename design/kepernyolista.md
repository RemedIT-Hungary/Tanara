# Tanara — funkcionális képernyőlista

> Állapot: 2026-10-03. A **[MEGVAN]** jelű részek a mostani Widgets-felületet írják le (a `gui/` és `watcher/` kód alapján), a **[TERV]** jelűek az egyeztetett bővítést.
> Cél: a felület QML-re átültetésének és az átirat-szerkesztő bővítésének közös alapja. Ez funkcionális leírás, nem vizuális terv.

## 0. Az app részei

Három külön indítható felület van, közös `core` fölött:

| Rész | Bináris / mód | Feladat |
|---|---|---|
| Elemző (főablak) | `gui` | Megbeszélések könyvtára, átirat, összefoglaló, sávok, személyek, beállítások |
| Felvevő (lebegő ablak) | `gui` felvevő-módban | Felvétel indítása/leállítása; egyetlen példány futhat (lock-fájl) |
| Figyelő (tálca) | `tanara-watcher` | Aktív hívás észlelése, a rögzítés felajánlása |

## 1. Képernyők áttekintése

| # | Képernyő | Állapot | Honnan nyílik |
|---|---|---|---|
| K1 | Könyvtár (megbeszélés-lista) | MEGVAN | Főablak bal oldala |
| K2 | Megbeszélés fejléce | MEGVAN, bővül | Főablak jobb oldala, felül |
| K3 | Feldolgozási lépések (átirat előtt) | MEGVAN | Jobb oldal, ha még nincs átirat |
| K4 | Átirat-szerkesztő | **TERV** (a mostani átirat-nézetet váltja) | „Átirat" fül |
| K5 | Személyválasztó panel | **TERV** (a mostani menüt váltja) | K4 oszlopfejléc, `+` oszlop, K2 |
| K6 | Összefoglaló | MEGVAN, bővül | „Összefoglaló" fül |
| K7 | Témánkénti összefoglaló | MEGVAN | K6-ból |
| K8 | Sávok | MEGVAN | „Sávok" fül |
| K9 | Lejátszó sáv | MEGVAN | Jobb oldal alja |
| K10 | Személyek kezelése | MEGVAN, bővül | Fájl → Személyek… |
| K11 | Beállítások | MEGVAN | Fájl → Beállítások… |
| K12 | Felvevő | MEGVAN | „Új felvétel", tálca |
| K13 | Tálca-figyelő | MEGVAN | Háttérben fut |
| K14 | Első indítás (onboarding) | MEGVAN (QML) | Első indításkor magától; Fájl → Első lépések…; Beállítások › Általános |

## K1 — Könyvtár [MEGVAN]

A felvett megbeszélések listája, a legfrissebb felül.

- **Egy sor tartalma:** cím, dátum, hossz, három állapotjelző: átirat / összefoglaló / azonosítva.
- **Kijelölés:** betölti a megbeszélést a jobb oldalra.
- **Helyi menü:** Átnevezés…, Mappa megnyitása, Törlés… (megerősítéssel).
- **Fölötte:** „Új felvétel" gomb (a felvevőt külön ablakban nyitja).
- **Indításkor:** az árva (félbeszakadt) felvételek helyreállítása.

## K2 — Megbeszélés fejléce [MEGVAN, bővül]

- Cím, dátum, hossz.
- **Beszélők sor:** az azonosított nevek, illetve „N ismeretlen".
- **Résztvevők azonosítása:** hang-lenyomat alapú párosítás; megszakítható, bármikor újrafuttatható.
- **Újra-átírás…:** az átirat újrakészítése az aktuális STT-szolgáltatóval; a mostani átirat és a hozzárendelések elvesznek (megerősítést kér).
- **[TERV]** A beszélők sor a meeting **résztvevő-listáját** mutatja (lásd K4 adatmodell), nem csak a felismert címkéket.

## K3 — Feldolgozási lépések [MEGVAN]

Átirat nélküli megbeszélésnél a fülek helyett ez látszik.

1. **Felvéve** (kész).
2. **Átirat:** „Miről szólt a meeting?" mező (kontextus az STT-nek és az összefoglalónak), opcionális résztvevő-azonosítás, opcionális lekeverés, „Átírás indítása".
3. **Összefoglaló:** zárolva, amíg nincs átirat.

Ha a szolgáltató nincs beállítva, a lépés megmondja, mi hiányzik, és a Beállításokra visz.

## K4 — Átirat-szerkesztő [TERV]

### Mi a baj a mostanival

- A beszélő csak nyers címke szinten rendelhető névhez (`Meeting.speakerMap`: „Távoli 1" → név). Ha a diarizáció két embert egy címkébe mos, nem bontható szét.
- Új beszélő nem vehető fel, csak akit a rendszer elkülönített.
- A névadás automatikusan lenyomatot is tanít; kézzel nem irányítható, miből.

### Elrendezés

Sorok = megszólalások időrendben. Bal oldalt beszélőnként egy keskeny oszlop (sáv), jobb oldalt a szöveg. A megszólalás blokkja annak a beszélőnek az oszlopában áll, akihez tartozik, és pontosan olyan magas, mint a szövege.

```
 LG  FÁ  SzÁ  +  │
 ██              │ [00:06] És ő még kattint?
     ██          │ [00:07] Nem, csak felraktam a meetingre, hogy
     ██          │         tudjon róla. Mondta, hogy nem...
 ██              │ [00:13] Igen, ma... azért csodálkoztam
         ▒▒      │ [00:19] Aha.          ← bizonytalan
```

- **Oszlopfejléc:** szín + monogram, teljes név súgóbuborékban; rögzített (görgetéskor látszik).
- **Oszlopsorrend:** saját magam elöl, utána beszédidő szerint csökkenően.
- **Névtelen nyers címkék** („Távoli 2") is kapnak oszlopot, amíg nincs nevük.
- **Sok résztvevő:** a keveset beszélők oszlopa keskenyre csukható.
- **Áttekintő csík a lista fölött:** vízszintes idővonal beszélőnként (ki mikor beszélt); kattintásra odaugrik. Csak navigáció, szerkeszteni nem lehet rajta.

### Műveletek

| Művelet | Hatás |
|---|---|
| Kattintás egy sor másik oszlopába | A sor átkerül ahhoz a beszélőhöz |
| Függőleges húzás egy oszlopban | Az érintett sorok mind átkerülnek |
| Kattintás a blokkra vagy az időbélyegre | Lejátszás attól a ponttól |
| Fel / le | Sor léptetése |
| `1`–`9` | A kijelölt sor az n-edik oszlopba |
| Szóköz | Lejátszás / szünet |
| „Következő bizonytalan" | A következő alacsony megbízhatóságú sorra ugrik |
| `+` oszlop | Résztvevő hozzáadása (K5); üres oszlop nyílik |
| Visszavonás | Az utolsó átsorolás visszavonása |

**Oszlopfejléc menüje:**
- Személy cseréje (K5): a teljes oszlop másik személyhez kerül. Ez a mostani fejléc-szintű átnevezés.
- Meghallgatás: reprezentatív minta.
- Hanglenyomat készítése ebből a meetingből (lásd lent).
- Oszlop eltávolítása: csak üres oszlopnál.

### Bizonytalan sorok

Sraffozott blokk jelöli azt a sort, ahol a hozzárendelés gyenge (alacsony párosítási pontszám, vagy nagyon rövid megszólalás). A kézzel átsorolt vagy megerősített sor tömör színű lesz.

### Hasonló sorok felajánlása (félautomata szétbontás)

Kézi átsorolás után a rendszer megnézi ugyanannak a nyers címkének a többi sorát, és felajánlja azokat, amelyek hangra az új beszélőhöz állnak közelebb: „Még 14 sor hasonlít erre a hangra. Átrakjam?" A javaslat előnézetben jelölve látszik, elfogadható vagy elvethető.

Korlát: az 1–2 másodperces sorokból („Aha") nem lesz megbízható hasonlítás, azok kézi munkák maradnak.

### Kézi hanglenyomat

- Az oszlopfejlécből indul; a rendszer az adott személyhez rendelt sorok közül a hosszabbakat használja (irányérték: legalább 3 mp-es sorok, összesen 15–20 mp).
- Ha nincs elég anyag, megmondja, mennyi hiányzik.
- A fejléc jelzi, kinek van már lenyomata.
- A kézi átsorolás **nem** tanít automatikusan; lenyomat csak gombnyomásra készül.

### Adatmodell-változás

| Új elem | Mit tárol | Miért |
|---|---|---|
| Soronkénti felülírás | megszólalás kulcsa (sáv + kezdőidő) → személy | A nyers diarizáció érintetlen marad; a javítás visszavonható, és a résztvevő-azonosítás újrafuttatása nem törli |
| Résztvevő-lista | a meeting személyei, címkétől függetlenül | Így vehető fel az, akit a rendszer nem különített el |
| Elavult-jelző az összefoglalón | igaz, ha az összefoglaló óta változott a hozzárendelés | Lásd K6 |

A megjelenített név feloldási sorrendje: soronkénti felülírás → `speakerMap` → nyers címke.

Technikai nyitott pont: az `Utterance` most nem hordoz sáv-azonosítót (a tokenek igen), a kulcshoz ezt át kell vezetni. Az újra-átírás a felülírásokat is törli, mert a megszólalások határai megváltoznak.

### Amit szándékosan nem csinál

- **Soron belüli vágás nincs.** Ha egy sorban két ember beszél, a sor egyben marad. Teljes diarizációs szerkesztő nem cél; ha később gond lesz, újragondoljuk.
- A szöveg javítása (elírások) nem része ennek a körnek.

## K5 — Személyválasztó panel [TERV]

A mostani menü az összes ismert személyt listázza, kereső nélkül; 20 név fölött kezelhetetlen.

- Felugró panel **keresőmezővel**, a fókusz azonnal a mezőben van.
- Gépelésre szűr (ékezet- és kisbetű-független, névrészletre is).
- **Sorrend:** a meeting résztvevői → gyakran / nemrég használtak → mindenki ábécében.
- Enter az első találatot választja; fel/le léptet.
- Ha nincs találat: „Új személy: «beírt név»" sor.
- A lenyomattal rendelkezők jelölve.
- Ugyanez a panel szolgál a `+` oszlophoz, az oszlopfejléc cseréjéhez és K10 összevonásához.

## K6 — Összefoglaló [MEGVAN, bővül]

- **Gyors összefoglaló:** egy modell-hívás, vezetői összefoglaló + döntések + teendők.
- **Témánként:** több körös elemzés (K7).
- **Kontextus:** a meeting leírásának szerkesztése.
- **Újragenerálás.**
- Ha a szolgáltató nincs kész, a gomb megmondja, mi hiányzik.
- **[TERV] Elavult-jelzés:** ha az összefoglaló készítése óta változott a beszélő-hozzárendelés, a fülön és a nézet tetején jelzés áll „A beszélők változtak az összefoglaló óta" szöveggel és Újragenerálás gombbal. Automatikus újragenerálás nincs.

## K7 — Témánkénti összefoglaló [MEGVAN]

1. **Téma-kinyerés:** a modell témákat javasol (cím + rövid leírás).
2. **Szerkesztés:** témakártyák; cím és leírás átírható, téma törölhető, „Új téma" felvehető.
3. **Elemzés indítása:** témánként részletes elemzés, döntések, teendők; a kártya mutatja az állapotot.
4. A hibás téma a saját kártyáján újrafuttatható; a kész eredmények megmaradnak.
5. „Vissza az összefoglalóhoz".

## K8 — Sávok [MEGVAN]

A felvétel hangsávjai (mikrofon, rendszerhang, egyéb).

- A csendesnek ítélt sáv „eldobott" jelölést kap, de a fájl megmarad.
- Műveletek: Meghallgatás, Visszaállítás, Törlés (végleges, megerősítéssel), Lekeverés frissítése.
- Jelzi, ha a lekeverés elavult vagy éppen készül.

## K9 — Lejátszó sáv [MEGVAN]

Lejátszás / szünet, pozíció-csúszka, idő, hangerő. A lejátszás az átirattal szinkronban fut: az aktuális sor kiemelve, sorra kattintva odaugrik. A lekeverés állapota az állapotsorban látszik.

## K10 — Személyek kezelése [MEGVAN, bővül]

- Az ismert személyek listája (minden meetingre érvényes), saját magam megjelölve.
- Személyenként: mely meetingeken szerepel; hang-lenyomatai (eszköz, forrás), meghallgatás, lenyomat törlése.
- Műveletek: Átnevezés, Törlés, Összevonás… (megerősítéssel).
- **[TERV]** Keresőmező a lista fölött; az összevonás célját K5 panellel lehet választani.

## K11 — Beállítások [MEGVAN]

| Fül | Tartalom |
|---|---|
| Általános | Saját beszélő neve; felület nyelve (újraindítás után érvényes); felvételek, jegyzetek és metaadat mappája |
| Rögzítés | Automatikus rögzítés minden eszközről, vagy alapértelmezett eszközök kiválasztása; hangminőség (24–64 kbps); lekeverés automatikusan vagy kézzel |
| Figyelő | Hívás-észlelés be/ki; ellenőrzési gyakoriság; indulás bejelentkezéskor; rákérdezés a hívás végén; csend utáni rákérdezés; ismert hívás-appok |
| Külső szolgáltatások | STT és LLM szolgáltató, cím, kulcs, modell („Modellek lekérése") |
| Összefoglaló | Az összefoglaló nyelve; a három rendszer-prompt szerkesztése és visszaállítása |

## K12 — Felvevő [MEGVAN]

Lebegő, húzással mozgatható ablak; a főablaktól független folyamat.

- Cím (szerkeszthető), indítás / leállítás, eltelt idő.
- Hangforrások kiválasztása, forrásonként élő szintjelzővel.
- Tálcára küldhető; bezárás felvétel közben megerősítést kér.
- **Rákérdezések felvétel közben:** „Vége a meetingnek?", ha a hívás-app leállt vagy elengedte a mikrofont, illetve ha minden forrás a beállított ideig csendes. Magától nem állít le.
- Leállítás után kódolás, majd a megbeszélés megjelenik a könyvtárban.

## K13 — Tálca-figyelő [MEGVAN]

- Állapotok: figyel / hívás észlelve / felvétel folyamatban.
- Hívás észlelésekor értesítés, benne a rögzítés indítása.
- Menü: Rögzítés azonnali indítása, Rögzítő megnyitása…, Elemző megnyitása, Kilépés.
- Ha már fut felvétel, újat nem indít.

## K14 — Első indítás [MEGVAN (QML)]

„Első lépések” ablak (`gui/qml/OnboardingWindow.qml`, `OnboardingViewModel`, `OnboardingWindowHost`). Designer-csomag nem volt: a Beállítások-ablak és az üres könyvtár vizuális nyelvét követi. Nem modális, 720 × 640, első indításkor a főablak fölött középen.

- **Varázsló hat lépéssel** (bal oldalt a lépések a Beállítások navigációjának elemeivel): Üdvözlés → Te (saját név az OS-fiókból kitöltve, nyelv, téma) → Mappák (felvételek, jegyzetek; „alapértelmezett” jelölés, tallózás, visszaállítás) → Szolgáltatások (saját kulcsos út magyarázata, állapot-sor: „Átírás: nincs kulcs”, „Összefoglaló: LM Studio · gemma-4-12b”; „Beállítás most” → Beállítások › Szolgáltatások; „Tanara Cloud: hamarosan”) → Hívásfigyelő (indítás bejelentkezéskor) → Kész.
- **Semmi sem kötelező:** minden döntési lépésnek van „Kihagyom” gombja, a „Később” / × bármikor bezár. A „Tovább” azonnal menti az adott lépést (különbség-mentés, mint a Beállításokban).
- **Egyszer magától:** az `onboardingDone` jelző (settings.json) a bezáráskor igazra áll. Kézzel bármikor újranyitható (Fájl → Első lépések…, Beállítások › Általános hivatkozás).
- Élő cloud-módban a K-01 módválasztó (Widgets) változatlanul előtte jön; ha a Cloudot választották, a Szolgáltatások lépés a saját kulcsos magyarázat helyett ezt mondja.

## Megvalósítási sorrend (javaslat)

1. Adatmodell: soronkénti felülírás, résztvevő-lista, elavult-jelző (`core`, tesztekkel).
2. K4 QML-ben, `QQuickWidget`-ként a mostani főablak „Átirat" fülén; K5 vele együtt.
3. Kézi hanglenyomat és a hasonló sorok felajánlása.
4. A többi képernyő átültetése QML-re.

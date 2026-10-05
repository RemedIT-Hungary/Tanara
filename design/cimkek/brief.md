# Tanara — design brief: címkék

> Állapot: 2026-10-05 · RemedIT Hungary Kft.
> Kinek szól: a felületet tervező designernek. Leírja, mi a címke, hogyan működik, hol jelenhet meg a mai felületen, és mit kell megtervezni.
> Kapcsolódó: [`../design-brief.md`](../design-brief.md) (az app és az alapelvek), `../handoff*/` (a jóváhagyott vizuális nyelv és a meglévő képernyők), `screenshots/` (a mai állapot képei, kitalált mintaadattal, világos és sötét témában).

## 1. Mit kérünk

A megbeszélésekre **címkéket** lehessen tenni, és a rendszer **javasoljon** címkéket. A feladat kettős:

1. **Megtalálni a címkék helyét** a meglévő képernyőkön (könyvtár, megbeszélés-fejléc, felvevő, átirat előtti lépések, összefoglaló, import).
2. **Megtervezni az új elemeket:** címke-chip, címke-beviteli mező, javaslat-sor, címkék kezelése, a kapcsolódó beállítások.

A vizuális nyelv adott (a „Nyomat” irány, a meglévő tokenekkel és vezérlőkkel). Új szín vagy vezérlő csak akkor kell, ha a meglévőkből nem rakható ki.

## 2. Miért kell

- A megbeszélések ma csak címmel, dátummal és résztvevőkkel különböztethetők meg. Nincs mód arra, hogy az összetartozók (ugyanaz az ügyfél, ugyanaz a projekt, ugyanaz a téma) együtt legyenek.
- Teljes projektkezelést nem akarunk bevezetni. A címke ennek a könnyű változata: a felhasználó szabadon ad nevet, nincs előre rögzített szerkezet.
- A címkék később más funkciókat is egyszerűsítenek (9. fejezet).

**Példa:** a felhasználó a „MuseumPlus” címkét használja egy termékre. Ha egy megbeszélésen rajta van, az valószínűleg a „MÉM-MDK” projekthez tartozik, tehát érdemes azt a címkét is felajánlani, és később az ott használt neveket és szakszavakat is.

## 3. Fogalmak

| Fogalom | Jelentés |
|---|---|
| Címke | Rövid, szabad szöveges jelölő egy megbeszélésen. Egy megbeszélésen 0–10 lehet, a teljes készlet 50–200 címkére nőhet. |
| Címkekészlet | Az összes eddig használt címke. Bevitelkor ebből ajánl a mező. |
| Javasolt címke | A rendszer ajánlja, de **nincs a megbeszélésen**, amíg a felhasználó el nem fogadja. |
| Indoklás | Egy javaslat rövid magyarázata: miért ajánlja a rendszer (közös résztvevő, közös kifejezések, hasonló megbeszélések). |
| Együtt járó címkék | Címkék, amelyek a múltban többnyire együtt szerepeltek. Ha az egyik felkerül, a többit felajánljuk. |
| Címke-profil | Amit a rendszer egy címkéről a használatából megtanult: jellemző résztvevők, jellemző kifejezések, példa-megbeszélések. |

## 4. Hogyan működik (amit a tervhez tudni kell)

### 4.1 Kézi címkézés

- Címkét bármikor lehet adni és levenni: felvétel előtt, **felvétel közben**, és utólag bármikor.
- A beviteli mező gépelés közben a címkekészletből ajánl. Ha nincs ilyen, új címke jön létre.
- Ha a begépelt név nagyon hasonlít egy meglévőre („Museum Plus” és „MuseumPlus”), a mező a meglévőt ajánlja fel, de az új létrehozása is lehetséges marad.
- A címkéknek nincs típusa (nincs külön „ügyfél” és „projekt” címke) és nincs hierarchiája.

### 4.2 Javaslatok

A címke jelentését nem a neve adja, hanem az, hogy mely megbeszélésekre került rá. A rendszer három forrásból javasol:

| Forrás | Mikor érhető el | Mit lát a felhasználó |
|---|---|---|
| Hasonló korábbi megbeszélések | az átírás után, azonnal | javasolt címkék indoklással („közös résztvevő: Kovács Anna · közös kifejezések: Nordvik, ütemterv”) |
| Együtt járó címkék | amint egy címke felkerül | „Ezzel együtt szokott szerepelni: …” |
| Nyelvi modell | az összefoglalás után, ha a felhasználó bekapcsolta | javasolt meglévő címkék, és legfeljebb 1–2 **új** címke ötlete |

Szabályok, amelyeket a tervnek tartania kell:

1. **Javaslat sosem kerül fel magától.** Mindig a felhasználó fogadja el vagy utasítja el.
2. **A javasolt címke látványosan más**, mint a már felrakott. Nem téveszthető össze.
3. **Az elutasítás megmarad:** ugyanazt a címkét a rendszer ugyanarra a megbeszélésre nem ajánlja újra.
4. **A nyelvi modell által kitalált új címke megkülönböztethető** a meglévő készletből ajánlottól, mert az új névvel a készlet bővül.
5. **A javaslat nem akadály és nem tolakodó:** nem ugrik fel, nem kér választ, a megbeszélés enélkül is teljes.

### 4.3 Két minőségi szint

A javaslatok kétféleképpen készülhetnek, a felhasználó választ a Beállításokban:

| | Alap | Beágyazó modellel |
|---|---|---|
| Kell hozzá | semmi, mindig működik, hálózat nélkül is | beágyazó modell: helyi (pl. LM Studio) vagy Tanara Cloud |
| Mit ért | közös résztvevők, közös nevek és szakszavak, hasonló cím | ezeken felül a rokon témát is, akkor is, ha más szavakkal beszéltek róla |
| Előkészítés | pillanatok | megbeszélésenként feldolgozás; a meglévő könyvtárra egyszeri, percekig tartó futás |

A felületen a két szint **ugyanúgy néz ki**. A különbség a javaslatok minősége, és az, hogy a beágyazó modellel előkészítő futás jár (folyamatjelzés, megszakítás, hiba). Az alap szint nem „lebutított” állapot: nem kell figyelmeztetéssel jelölni.

## 5. Hol jelenhet meg a mai felületen

Minden képernyőhöz van kép a `screenshots/` mappában, `-light` és `-dark` változatban.

| Kép | Mai állapot | Kérdés a tervhez |
|---|---|---|
| `01-foablak-atirat` | Főablak: bal oldalt könyvtár, jobb oldalt a megbeszélés fejléce (cím, dátum, hossz, beszélők száma) és a fülek. | Hol látszanak a megbeszélés címkéi, és hol lehet hozzáadni? A fejléc ma egy cím- és egy adatsorból áll. |
| `02-konyvtar-szurok` | A könyvtár szűrői chipként a kereső alatt: állapot („Nincs összefoglaló”) és személy. A listasorban cím, dátum, hossz és állapot-ikonok. | Címke-szűrő több címkére is. Megjelenjenek-e a címkék a listasorban, és ha igen, hány fér el a 276 px széles oszlopban? |
| `03-konyvtar-kereses` | Keresés címben és átiratban, találat-részlettel. | A kereső a címkékben is keres. Hogyan látszik, hogy a találat címke miatt van? |
| `04-atnevezes` | Megbeszélés átnevezése párbeszédablakban. | Itt is szerkeszthető legyen a címke, vagy elég a fejléc? |
| `05-atirat-elott`, `06-atirat-elott-megjegyzes` | Átirat előtti lépések. Az 1. lépés („Miről szólt a megbeszélés?”) egy megjegyzés-mező, fölötte korábbi megbeszélések megjegyzései kártyaként. | A címke természetes helye lehet az 1. lépés. A kártyás megjegyzés-javaslat később a közös címkék alapján jön (9. fejezet), a kettő együtt legyen érthető. |
| `07-osszefoglalo-kesz`, `08-osszefoglalo-megjegyzes` | Összefoglaló fül: bal oldalt a szöveg, jobb oszlopban résztvevők, a futás adatai és a megjegyzés-blokk. | A nyelvi modell címkejavaslata az összefoglalás után érkezik. Hol jelenik meg úgy, hogy észrevehető, de nem tolakodó? |
| `09-atirat-ful` | Átirat fül a beszélő-áttekintéssel. | Valószínűleg nem kell ide címke; a fejléc közös. |
| `10-felvevo-cim`, `11-felvevo-felvetel`, `12-felvevo-kinyitva` | Felvevő (420 px széles lebegő ablak): cím, indítás / leállítás, eszközlista. Összecsukható és „pirula” méretre kicsinyíthető. | Címke megadása indítás előtt és **felvétel közben**, a lehető legkevesebb helyen. A felvevő hívás közben látszik, a bevitel legyen gyors és billentyűzetről is menjen. |
| `13-import` | Hangfájl importálása: fájlok, cím, dátum. | Címke megadása importkor. |
| `14-beallitasok-szolgaltatasok` | Beállítások, Szolgáltatások oldal: átíró és nyelvi modell kártyák. | Harmadik szerep: beágyazó modell (nincs / helyi / Tanara Cloud), a meglévő kártyák mintájára. |
| `15-nincs-kijeloles` | Főablak kijelölt megbeszélés nélkül. | Lehet-e innen elérni a címkék kezelését? |

A bal alsó sarokban ma két belépő van: „Személyek” és „Beállítások”. A címkék kezelésének belépője ide illhet, de ez tervezői döntés.

## 6. Megtervezendő elemek és állapotok

### C01 — Címke-chip

- Felrakott címke, javasolt címke, a nyelvi modell által kitalált új címke.
- Eltávolítás, kattintás (szűrés a könyvtárban erre a címkére).
- Hosszú név levágása; sok címke egy sorban („+3”).
- **Megkülönböztethető legyen** a személy-chiptől (színes, monogrammal) és az állapot-chiptől („elavult”, „bizonytalan”, szűrők). A személyek színkódja foglalt, a címkék ne versenyezzenek vele.
- Kérdés: legyen-e a címkéknek saját színe? Ha igen, ki adja (a felhasználó vagy a rendszer)?

### C02 — Címke-beviteli mező

- Üres állapot: a legutóbb használt címkék.
- Gépelés közben: találatok a készletből (ékezet és kis-nagybetű nem számít), kiemelt egyezéssel.
- Nincs találat: „Új címke: …”.
- Nagyon hasonló név már létezik: a meglévő felajánlása.
- Billentyűzet: Enter hozzáad, Backspace az utolsót törli, nyilak a listában.
- Kompakt változat a felvevőhöz.

### C03 — Címkék a megbeszélésen

- Nincs címke, 1–3 címke, sok címke.
- Hozzáadás és eltávolítás helyben.
- A megbeszélésnek van javaslata (lásd C04).

### C04 — Javaslatok

- Javaslat-sor indoklással; elfogadás és elutasítás egyenként, és „mind elfogadása”.
- Együtt járó címkék felajánlása egy címke hozzáadása után.
- A nyelvi modell javaslata az összefoglalás után, benne új címke ötlete.
- Javaslatok számolása folyamatban; nincs javaslat (ilyenkor semmi ne látszódjon, vagy csak a hozzáadás).
- Az indoklás részletei: mely korábbi megbeszélésekhez hasonlít (kattintható).

### C05 — Könyvtár

- Címkék a listasorban (ha a terv szerint megjelennek).
- Szűrés egy vagy több címkére a meglévő szűrők mellett; „címke nélküli” szűrő.
- Keresési találat címke alapján.
- Kérdés: lehessen-e több megbeszélést kijelölni és egyszerre címkézni? Ma a könyvtárban nincs többes kijelölés.

### C06 — Felvevő

- Címke megadása indítás előtt, felvétel közben, összecsukott állapotban.
- A „pirula” méretben valószínűleg nem kell.

### C07 — Átirat előtti 1. lépés és import

- Címke megadása a megjegyzés mellett.
- Importnál a párbeszédablakban.

### C08 — Címkék kezelése

- Lista: név, hány megbeszélésen szerepel, mikor használták utoljára; keresés és rendezés.
- Átnevezés, két címke összevonása, törlés (megerősítéssel, a megbeszélések megmaradnak).
- Egy címke részletei: a megbeszélései, a címke-profil (jellemző résztvevők és kifejezések), a vele együtt járó címkék.
- Üres állapot: még nincs címke.
- A Személyek ablak (`../handoff-people/`) felépítése jó kiindulás lehet.

### C09 — Beállítások

- Beágyazó modell szerep a Szolgáltatások oldalon: nincs / helyi / Tanara Cloud, kapcsolat tesztelése.
- A könyvtár előkészítése beágyazáshoz: folyamat („23 / 40 megbeszélés”), megszakítás, hiba, „naprakész”.
- Modellváltás: az előkészítés újraindul; erről a felhasználó előre tudjon.
- Kapcsolók: javaslatok be / ki; a nyelvi modell címkejavaslata az összefoglalás után be / ki.
- Tanara Cloud esetén jelezni kell, hogy az átirat szövege a felhőbe megy (ugyanúgy, mint a felhős összefoglalásnál).

## 7. Keretek

- Qt Quick / QML, a meglévő `Theme` tokenekkel és `T*` vezérlőkkel; világos és sötét téma.
- Magyar és angol felirat; a magyar jellemzően hosszabb.
- A felvevő 420 px széles, a könyvtár-oszlop 276 px, az Összefoglaló jobb oszlopa 250 px.
- Minden művelet billentyűzetről is elérhető.
- A javaslatok késve érkezhetnek (átírás, előkészítés vagy összefoglalás után): a felület nem ugorhat el alattuk.

## 8. Amit nem kérünk

- Címke-típusok, hierarchia, projekt- vagy ügyfél-nyilvántartás.
- Megosztás, több felhasználó.
- Jelentés szerinti keresés a könyvtárban (a beágyazó modell később lehetővé teszi, de külön kör).

## 9. Később jön, de legyen helye

Ezeket most nem kell megtervezni, de a terv ne zárja ki őket:

- **Megjegyzés-javaslat címkék alapján:** az átirat előtti lépésben és az Összefoglaló fülön a korábbi megjegyzések a közös címkéjű megbeszélésekből jönnek, nem a hasonló című megbeszélésekből.
- **Címkéhez kötött megjegyzés:** egy címkéhez nevek, szakszavak és ismert félrehallások tartoznak, amelyek automatikusan bekerülnek az átírás és az összefoglalás kontextusába.
- **Előtöltés a felvevőben:** címke alapján cím-javaslat, illetve cím alapján címke-javaslat, még a felvétel indítása előtt.

## 10. Amit várunk

A korábbi csomagokkal megegyező formában (lásd `../handoff-people/`):

- a fenti elemek és állapotok terve (C01–C09), mindkét témában;
- annak megjelölése, hogy a meglévő képernyőkön pontosan hova kerülnek;
- új tokenek, ha kellenek (például címke-színek);
- a nyitott kérdésekre (C01 szín, C05 többes kijelölés, a kezelő belépője) javaslat, indoklással.

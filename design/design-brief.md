# Tanara — design brief

> Állapot: 2026-10-03 · RemedIT Hungary Kft.
> Kinek szól: a felület újratervezését végző designernek. Leírja, mi az app, mit csinál ma, hogyan néz ki ma, és mit kell megtervezni.
> Kapcsolódó: [`kepernyolista.md`](kepernyolista.md) (képernyőnkénti funkciólista, K1–K14), `screenshots/` (a mai állapot képei).

## 1. Mi a Tanara

Asztali alkalmazás, amely online megbeszéléseket **felvesz, átír és összefoglal**. Minden hangforrást külön sávra rögzít (a saját mikrofon és a hívás hangja külön marad), felismeri a visszatérő beszélőket a hangjuk alapján, és a végén átiratot meg összefoglalót ad, sima fájlokként a hangfelvétel mellett.

- **Local-first:** a felvétel, a beszélő-felismerés és az adatok a felhasználó gépén maradnak. Csak az átírás és az összefoglalás megy külső szolgáltatóhoz, amelyet a felhasználó választ.
- **Nyílt forráskódú** (MIT), Linuxon és Windowson fut.
- **Kinek:** aki sok online megbeszélésen vesz részt, és utólag vissza akarja keresni, ki mit mondott és miben maradtak. Erős a magyar és más kis nyelvek átírásában.

## 2. Mit kérünk

A mai felület gyári Qt-vezérlőkből áll, működik, de elavult kinézetű és helyenként nehezen olvasható. A cél egy egységes, mai megjelenés és néhány képernyő érdemi újragondolása.

**Sorrend:**

1. **Átirat-szerkesztő** (új, a legfontosabb): lásd 7. fejezet.
2. **Főablak:** könyvtár + megbeszélés-nézet (átirat, összefoglaló, sávok).
3. **Felvevő** (kis lebegő ablak) és a **tálca-ikon** állapotai.
4. **Személyválasztó** és **Személyek kezelése**.
5. **Beállítások**.
6. **Első indítás** (ma nincs).

**Amit várunk:**

- Vizuális nyelv: színek, tipográfia, térközök, ikonkészlet, vezérlők (gomb, mező, fül, lista, chip, menü, párbeszédablak).
- Világos és sötét téma.
- A fenti képernyők terve, az állapotaikkal együtt (9. fejezet).
- A beszélők színkódolása: legalább 10 megkülönböztethető szín mindkét témában.

## 3. Keretek

| Téma | Megkötés |
|---|---|
| Technológia | Qt Quick / QML. Egyedi megjelenés szabadon tervezhető, de nem webes felület. |
| Platform | Linux és Windows asztali gép. macOS és mobil nem cél. |
| Ablakok | Átméretezhető főablak (a mai alapméret kb. 1280×820); kis, mindig látható lebegő felvevő; tálca-ikon menüvel. |
| Nyelv | Magyar és angol. A feliratok hossza eltér, a magyar jellemzően hosszabb. |
| Ikonok | Ma emojik szolgálnak ikonként (🔴 👥 ⚙ 🎧), ezek platformonként másképp néznek ki. Nyílt licencű ikonkészlet kell helyettük. |
| Betűtípus | Szabadon terjeszthető (nyílt licencű) legyen. |
| Hosszú tartalom | Egy átirat több száz sor; egy megbeszélésen 2–10+ beszélő; a személylista 20+ név és nő. |

## 4. Alapelvek, amelyeket a tervnek tartania kell

1. **A tartalom lépései egymásra épülnek:** felvétel → átirat → összefoglaló. Átirat nélkül nincs összefoglaló; ha hiányzik egy beállítás, a felület megmondja, mi kell, és odavisz.
2. **A beszélők azonosítása sosem akadály.** Az átirat névtelen beszélőkkel („Távoli 1") is elkészül; a névadás utólagos, opcionális, és a következő megbeszélésre tanítja a rendszert.
3. **Saját kulcs és hosztolt szolgáltatás egyenrangú.** A „hozom a saját szolgáltatómat" út végig teljes értékű; a tervezett Tanara Cloud bejelentkezés kényelmi alternatíva, nem kötelező.
4. **A felvétel külön felület.** A főablak a visszanézésé; a felvevő kicsi, nem vesz el helyet, és felvétel közben is elfér a hívás mellett.
5. **A felvétel biztonsága az első.** Felvétel közben semmi nem záródhat be megerősítés nélkül, és a rendszer magától nem állít le felvételt, csak rákérdez.

## 5. A fő folyamat

```
Hívás indul → a tálca-figyelő észleli → értesítés → Felvevő (indítás / leállítás)
   → a megbeszélés megjelenik a Könyvtárban
   → „Miről szólt?" + Átírás indítása → Átirat
   → beszélők ellenőrzése, javítása (átirat-szerkesztő)
   → Gyors összefoglaló vagy Témánkénti elemzés → Összefoglaló
```

A felvétel a főablakból kézzel is indítható. Az átírás és az összefoglalás percekig tarthat; közben az app használható marad.

## 6. Fogalmak

| Fogalom | Jelentés |
|---|---|
| Megbeszélés | Egy felvétel a hozzá tartozó sávokkal, átirattal, összefoglalóval |
| Sáv | Egy hangforrás felvétele (mikrofon, rendszerhang). A csendes sávot a rendszer „eldobottnak" jelöli, de megtartja |
| Lekeverés | A sávok egy fájlba keverve, csak visszahallgatáshoz |
| Átirat | Időbélyeges, beszélőnként tagolt szöveg |
| Megszólalás | Az átirat egy sora: egy beszélő egy összefüggő mondanivalója |
| Beszélő | Akit a rendszer egy megbeszélésen elkülönített; lehet névtelen |
| Személy | Névvel ismert ember, minden megbeszélésre érvényes |
| Hanglenyomat | Egy személy hangjának mintája, amelyből a rendszer később felismeri |
| Résztvevők azonosítása | A beszélők párosítása az ismert személyekkel hang alapján |
| Szolgáltató | Az átírást (STT) vagy az összefoglalást (LLM) végző külső szolgáltatás |

## 7. Az átirat-szerkesztő (új képernyő)

**A gond ma:** ha a rendszer valakinek a hangját rossz beszélőhöz köti, nem lehet soronként javítani; új beszélőt sem lehet felvenni. Egy tízfős megbeszélés így kétszemélyes beszélgetésnek látszik.

**A megoldás váza:** a szöveg mellett beszélőnként egy keskeny oszlop áll, mint a videószerkesztők sávjai. Minden megszólalás blokkja annak az oszlopában van, aki mondta, és olyan magas, mint a szövege. Másik oszlopba kattintva a sor átkerül ahhoz a beszélőhöz.

```
 LG  FÁ  SzÁ  +  │
 ██              │ [00:06] És ő még kattint?
     ██          │ [00:07] Nem, csak felraktam a meetingre, hogy
     ██          │         tudjon róla. Mondta, hogy nem...
 ██              │ [00:13] Igen, ma... azért csodálkoztam
         ▒▒      │ [00:19] Aha.          ← bizonytalan
```

**Amit meg kell tervezni:**

- Oszlopfejléc: szín, monogram vagy név, van-e hanglenyomata; görgetéskor is látszik.
- Blokk állapotai: rendes, bizonytalan (a rendszer nem biztos benne), kézzel javított, éppen lejátszott, kijelölt, húzás közbeni előnézet.
- 2 beszélő és 10+ beszélő esete; a keveset beszélők oszlopa összecsukható.
- `+` oszlop: új résztvevő felvétele a személyválasztóval.
- Áttekintő csík a lista fölött: vízszintes idővonal, ki mikor beszélt; kattintásra odaugrik.
- Javaslat-sáv: „Még 14 sor hasonlít erre a hangra. Átrakjam?" elfogadással és elvetéssel.
- Visszavonás.

Részletes működés: `kepernyolista.md`, K4 és K5. Soron belüli vágás nincs, egy sor mindig egy beszélőé.

## 8. A mai felület képernyőnként

A képek egy valódi példányról készültek, sötét témában. **Valódi neveket és beszélgetés-részleteket tartalmaznak: nem adhatók tovább nyilvánosan, és a tervekben kitalált mintaadat szerepeljen.**

### Főablak — könyvtár és megbeszélés

![Főablak, nincs kijelölés](screenshots/01-konyvtar-nincs-kijeloles.png)
![Átirat](screenshots/02-atirat.png)

Bal oldalt a megbeszélések listája, jobb oldalt a kijelölt megbeszélés három füllel (Átirat, Összefoglaló, Sávok), alul a lejátszó.

Ismert gondok:
- A lista állapotjelzői („○ átirat ○ össz ○ azonosítva") szövegesek és halványak, ránézésre nem olvasható le, mi készült el.
- Nincs keresés és szűrés a listában.
- A dátum és a hossz a cím mellett jobbra szinte láthatatlan (túl alacsony kontraszt).
- A beszélők kétszer szerepelnek: a fejlécben és az átirat fölötti chipekben.
- Az „Emberek" és „Beállítások" a gombsoron és a Fájl menüben is megvan.
- Ha nincs kijelölt megbeszélés, a jobb oldal üres, útmutatás nélkül.

### Beszélő hozzárendelése

![Beszélő-menü](screenshots/03-beszelo-menu.png)

A beszélő nevére kattintva menü nyílik az összes ismert személlyel. Kereső nincs, a lista a képernyő aljáig ér; ezt váltja a személyválasztó panel.

### Összefoglaló

![Összefoglaló](screenshots/04-osszefoglalo.png)
![Üres állapot](screenshots/05-osszefoglalo-ures.png)
![Témaszerkesztő váza](screenshots/05b-temaszerkeszto-ures.png)

Kétféle összefoglaló készül: gyors (egy lépés) és témánkénti (a modell témákat javasol, a felhasználó szerkeszti őket, majd témánként elemzés készül). A tartalom: vezetői összefoglaló, döntések, teendők felelőssel, résztvevők. A harmadik kép a témaszerkesztő üres váza; kitöltve témakártyák listája, kártyánként címmel, rövid leírással, állapottal és az elemzés eredményével.

Új elem a tervben: **elavult-jelzés**, ha az összefoglaló készítése óta megváltoztak a beszélők.

### Sávok

![Sávok](screenshots/06-savok.png)

A felvétel hangsávjai; meghallgatás, visszaállítás, végleges törlés, lekeverés frissítése. Gond: a sávok az eszköz nyers rendszernevével szerepelnek („Monitor of …"), ami a felhasználónak keveset mond.

### Lépések átirat előtt

![Lépések](screenshots/07-lepesek-atirat-elott.png)

Átirat nélküli megbeszélésnél a fülek helyett ez a kártya látszik: kontextus megadása, opcionális résztvevő-azonosítás és lekeverés, átírás indítása. Gond: a zárolt harmadik lépés felirata sötét témában nem olvasható (csak a lakat látszik), és a három lépés nem áll össze folyamattá.

### Menük

![Fájl menü](screenshots/08-menu-fajl.png)
![Nézet menü](screenshots/08-menu-nezet.png)

### Beállítások

![Általános](screenshots/09-beallitasok-1-altalanos.png)
![Rögzítés](screenshots/09-beallitasok-2-rogzites.png)
![Figyelő](screenshots/09-beallitasok-3-figyelo.png)
![Külső szolgáltatások](screenshots/09-beallitasok-4-szolgaltatasok.png)
![Összefoglaló](screenshots/09-beallitasok-5-osszefoglalo.png)

Öt fül. A „Külső szolgáltatások" mezői a választott szolgáltatótól függően változnak (cím, kulcs, modell), ezt a tervnek rugalmasan kell kezelnie.

### Személyek kezelése

![Személyek](screenshots/10-szemelyek.png)

Személyek listája; személyenként a megbeszélései és a hanglenyomatai. Átnevezés, törlés, összevonás. Gond: kereső nincs, a jobb oldali listák levágják a hosszú sorokat.

### Felvevő

![Felvevő](screenshots/11-felvevo.png)
![Felvevő, hangforrások](screenshots/12-felvevo-hangforrasok.png)

Kis lebegő ablak: cím, indítás/leállítás, eltelt idő, a hangforrások kiválasztása élő szintjelzővel. Gond: a hosszú eszköznevek le vannak vágva; a csoportfejlécek alig válnak el a soroktól.

Felvétel közben megerősítő kérdések jöhetnek („Vége a meetingnek?"), ma rendszer-párbeszédablakként.

### Tálca-figyelő

Nincs képe. Tálca-ikon három állapottal (figyel / hívás észlelve / felvétel folyamatban), értesítés hívás észlelésekor, és menü: Rögzítés azonnali indítása, Rögzítő megnyitása, Elemző megnyitása, Kilépés.

## 9. Megtervezendő állapotok

- **Üres:** nincs még megbeszélés; nincs kijelölés; nincs átirat; nincs összefoglaló; nincs hangeszköz.
- **Folyamatban:** átírás, összefoglalás, témánkénti elemzés (témánként külön állapot), lekeverés (százalékos), résztvevő-azonosítás (megszakítható).
- **Hiányzó beállítás:** a lépés megmondja, mi kell, és a megfelelő beállításra visz.
- **Hiba:** sikertelen átírás vagy összefoglalás, újrapróbálási lehetőséggel; hiányzó hangfájl.
- **Megerősítés:** törlés, újra-átírás (elveszik a mostani átirat), bezárás felvétel közben, „vége a meetingnek?".
- **Felvétel:** készenlét, rögzítés, leállítás, kódolás.

## 10. Tervezett, még nem létező funkciók

Ezekhez ma nincs felület; a tervben legyen helyük.

- **Első indítás:** saját név, mappák, szolgáltató kiválasztása (saját kulcs vagy Tanara Cloud).
- **Tanara Cloud:** bejelentkezés, kreditegyenleg, költségbecslés a feldolgozás előtt, minőségi szint választása (Gyors / Pontos).
- **Keresés a könyvtárban** és a személylistákban.

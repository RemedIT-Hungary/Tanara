# Tanara — design brief: elemző, beszélő-azonosítás, hangmodellek, személy-címkék

> Állapot: 2026-10-10 · RemedIT Hungary Kft.
> Kinek szól: a felületet tervező designernek. Leírja, mit tud ma az elemző (átirat-szerkesztő), hogyan működik mögötte a beszélő-azonosítás, mi készült el a motorban felület nélkül, és mit kell megtervezni.
> Kapcsolódó: [`leltar.md`](leltar.md) (a MAI felület tételes leltára képernyőnként: minden gomb, menüpont, állapot és felirat, fájl:sor hivatkozással — ez a tényforrás), `screenshots/` (a mai állapot képei kitalált mintaadattal, világos és sötét témában), [`../design-brief.md`](../design-brief.md) (az app és az alapelvek), [`../cimkek/brief.md`](../cimkek/brief.md) (a címkerendszer), `../handoff*/` (a jóváhagyott vizuális nyelv és vezérlők).

## 1. Mit kérünk

Négy, egymásra épülő feladat. A vizuális nyelv adott (a „Nyomat" irány, a meglévő tokenek és `T*` vezérlők); új szín vagy vezérlő csak akkor kell, ha a meglévőkből nem rakható ki.

1. **Elemző (átirat-szerkesztő) UX-átnézése.** A funkciók megvannak és működnek, de egy hosszú, sok beszélős megbeszélésen a javítás káosszá válik (lásd 2. fejezet). Kell egy olyan munkafolyamat, amelyben a felhasználó **tömegesen**, biztos sorrendben halad, és mindig látja, mire támaszkodik a gép.
2. **Hangmodellek a Beállításokban.** A motor már több beszélő-felismerő modellt tud párhuzamosan használni, letölteni, ki-be kapcsolni; ennek még nincs felülete. Kell a lista, a letöltés folyamata, a licenc és forrás megjelenítése, és a háttérben futó újraszámolás jelzése.
3. **Sáv-oldal ellenőrzés az elemzőben.** A motor már tudja, hogy egy megszólalás a saját mikrofonon vagy a hívás hangján (loopback) érkezett-e; ebből derül ki, ha az átirat a „másik oldali" embert jelöli. Kell a jelölés, a magyarázat, a kézi sáv-beosztás és a tömeges átnézés.
4. **Címkék a személyeken.** Ugyanazok a címkék, mint a megbeszéléseken, személyekre is. A címke-átfedés a beszélő-azonosítás egyik bemenete lesz: aki korábban ott volt ugyanannál az ügyfélnél, nagyobb eséllyel beszél most is.

## 2. Miért — mi romlik el ma

Egy valódi, 131 perces, 6 fős megbeszélés elemzése (a számok a mai motorból jönnek, az átirat tartalma nem):

| Jelenség | Szám | Következmény a felületen |
|---|---|---|
| Az átíró (Soniox) 6 emberhez csak **3 nyers beszélőt** adott | „Beszélő 1" = a felhasználó + 130 más sor; „Beszélő 3" = három ember | A felhasználó 368 sort helyezett át kézzel, egyesével. Nincs tömeges eszköz. |
| **Rövid sorok**: 1,5 mp alatt nincs hang-beágyazás | 426 sor az 1298-ból | A gép nem segít rajtuk, de a felület nem mutatja, melyek ezek. A felhasználó ugyanúgy vadászik rájuk. |
| **Szennyezett mag**: a „Gábor" címke alá sok „Árpád"-sor került a tömeges áthelyezésnél | a két ember hangja a gépnek 0,92-ben „azonos", pedig külön-külön 0,58 | Az újraellenőrzés a rossz magot erősíti; minden kör rosszabb javaslatot ad. A felhasználó egyre kevésbé hatékony, és nem tudja, miért. |
| **Sáv-oldal ellentmondás**: a felhasználóra írt sorok közül sok a hívás hangján érkezett | 368 sorból 69 nem a mikrofonról jött | Ez a nyers „Beszélő 1" keveredésének maradéka; a hangmodell nem látja, a sáv-energia igen. |

Tanulság: a **hang** csak az egyik bizonyíték. A **sáv** (honnan jött a hang), a **címke** (ki szokott itt lenni), a **megerősített sorok** és a **kézi beosztás** együtt adnak megbízható javaslatot, és a felületnek meg kell mutatnia, melyik bizonyíték alapján mond valamit a gép.

## 3. Fogalmak

| Fogalom | Jelentés |
|---|---|
| Nyers beszélő | Az átíró által adott névtelen címke („Beszélő 1"). Gyakran kevesebb van belőle, mint ahány ember beszélt. |
| Személy | Névvel ellátott, megbeszéléseken átívelő résztvevő (Személyek ablak). Lehet hanglenyomata, aliasa, jegyzete, és mostantól **címkéi**. |
| Hanglenyomat (minta) | Egy személy hangjának rögzített jellemzője, egy vagy több megbeszélés soraiból. Egy személynek több mintája lehet (más mikrofon, más akusztika). Mostantól **modellenként** készül: egy minta = ugyanaz a hangrészlet minden bekapcsolt modellel. A felületen egy minta **egy tétel** marad, nem modellenként több. |
| Hangmodell | A beszélő-felismerő ML-modell (ONNX-fájl). Ma három ismert: CAM++ (alap, a Windows-csomagban benne), WeSpeaker ResNet34-LM, ERes2NetV2. Letölthető, ki-be kapcsolható. Több bekapcsolt modell **fúzióban** dolgozik: a hasonlóság a modellek átlaga. |
| Beágyazás | A megbeszélés minden legalább 1,5 mp-es sorának hang-jellemzője, a gép ebből számol hasonlóságot. Háttérben készül, a megbeszélés mappájában tárolódik. |
| Mag | Egy személy megerősített/javított, nem zajos soraiból képzett átlag-hang. Az újraellenőrzés ehhez méri a többi sort. |
| Bizonytalan | Olyan sor, ahol a hang nem illik a jelölt beszélőhöz; a „Bizonytalan" szűrőben jelenik meg, soronként dönthető. |
| Jó így / Jó így, de nem minta | Megerősítés: a sor beszélője rendben. A „nem minta" változat (zajos: egymásra beszéltek) kizárja a sort a magból és a lenyomatból. |
| Újraellenőrzés | A megerősített sorok magjához méri a többi sort: az egész megbeszélésre, vagy **párban** két összekeverhető beszélő közt. |
| Sáv | Egy hangforrás külön fájlja: mikrofon-sáv (helyi ember) vagy loopback-sáv (a hívás hangja, a távoli emberek). |
| Sor oldala | A sor energiája szerint: **helyi** (mikrofonon szólt), **távoli** (a hívás hangján), **vegyes** (egymásra beszéltek vagy visszhang), **ismeretlen** (nincs két sáv). |
| Személy oldala | Kézi beosztásból (mely sávokra tette a felhasználó), ebből hiányában a megerősített sorai többségéből, hiányában az alapértékből (a saját név helyi; tanult alapérték személyenként). |
| Sáv-ellentmondás | A sor oldala nem egyezik a beszélő oldalával. Minimum bizonytalan, saját magyarázattal. |
| Személy-címke | Ugyanaz a címke, mint a megbeszéléseken (ügyfél, projekt, téma), személyre téve. Egy személynek 0–10 címkéje lehet. |

## 4. Hogyan működik ma (amit a tervhez tudni kell)

A tételes leírás a [`leltar.md`](leltar.md)-ban van; itt csak az összefüggések.

### 4.1 Elemző (átirat fül)

- **Felépítés:** eszköztár (keresés, „Bizonytalan" szűrő számmal, sáv-nézet, visszavonás), beszélő-áttekintő csík (beszélőnként egy sor, a sorai színnel), **beszélő-sín** (oszloponként egy beszélő; kattintás vagy 1–9 billentyű sorol át), sorok (idő, szöveg, jelölők), lejátszó sáv alul.
- **Soronkénti jelölők:** `bizonytalan` (tooltip: „Hangra inkább X sorának tűnik"), `javítva`, `egymásra beszéltek`. A „megerősítve" és az „újraellenőrizve" állapot a motorban létezik, de a soron nem látszik.
- **Bizonytalan szűrő:** a bizonytalan sorok, soronként beágyazott gombokkal: `Meghallgatom` · `Jó így` · `Jó így, de nem minta` · `<Név> mondta` · `Más mondta…`. B / Shift+B lépked.
- **„Ki mondta?" felugró** (soron vagy beszélőn): más személy, új személy, névtelen; egész beszélőnél **összevonás** (megerősítő ablak számokkal), `A sorok kerüljenek ki X hanglenyomatából (téves felismerés)`, **`Átnézés másik beszélővel…`** (páros újraellenőrzés jelöltlistával), és hanglenyomat-minta a sorból (`Hanglenyomat-minta ebből a sorból (N mp)`, bizonytalan soron `Jó így + …`).
- **Változás-sáv** (sötét sáv a lista alatt, 12 mp): mit csináltál, `Visszavonás`, `Hanglenyomat készítése` (ha egy egész beszélő nevet kapott), és a **páros ajánlat**: „X és Y hangja hasonló. Nézzem át kettejük sorait?" `Átnézés` / `Most nem`.
- **Újraellenőrzés az egész megbeszélésre:** a „Bizonytalan" chipről (ha 0 bizonytalan), az üres szűrő gombjáról, a fejléc menüjéből. Eredmény toast: „N kétséges sort jelöltem meg — a Bizonytalan szűrőben találod." Feltétel: legalább 3 megerősített sor.
- **Hanglenyomat panel:** van / nincs / névtelen; `Hanglenyomat készítése` / `Új minta`; mennyi tiszta beszéd kell még. Lenyomat **csak kifejezett kattintásra** készül.
- **Hiányzó modell:** „Hangelemzés nélkül" csík + magyarázat; a szerkesztés enélkül is működik.
- **Mi fut a háttérben:** beágyazás (egy dekódolás, kötegelt eredmények, megszakítható), átirat-fájl mentése, hullámformák, személy-statisztika, címke-profilok. A GUI-szál nem blokkol.

### 4.2 Személyek ablak

Lista (te / többiek / betű), részletek: név, alias, jegyzet, **minta-lista** (forrás-megbeszélés, hossz, mintánkénti menü: meghallgatás, áthelyezés, törlés), `Minta N megbeszélésből` terv, összevonás, törlés, visszavonás-toast. A mintákat a motor ma modellenként tárolja, de a lista **egy tételt** mutat mintánként (testvér-csoport).

### 4.3 Beállítások

Öt lap: Általános (saját név + saját hanglenyomat állapota, nyelv, téma, mappák), Rögzítés (eszközök), Hívásfigyelő, Szolgáltatások (átírás, összefoglaló, beágyazás-modell a címkékhez, Tanara Cloud), Összefoglaló. Piszkozat → Mentés, mentetlen-változás párbeszéd. **Hangmodell-beállítás nincs.**

### 4.4 Címkék

Megbeszélésen chip-sor a fejlécben, javaslatok indoklással („Miért?"), Címkék ablak (átnevezés, összevonás, tanult profil: jellemző résztvevők, kifejezések). **Személyen még nincs címke.**

## 5. Ami a motorban kész, felület nélkül

| Funkció | Motor / CLI | Felületi állapot ma |
|---|---|---|
| Hangmodellek listája, be/ki, letöltés | `tanara-cli voice-models [list\|enable\|disable\|fetch <id>]`; nyilvántartás: azonosító, név, fájl, dimenzió, licenc, forrás, letöltési URL, méret | nincs; a hiányzó modell csak hibaszövegként jelenik meg |
| Több modell fúziója | automatikus, ha több modell be van kapcsolva | láthatatlan |
| Lenyomatok modellenként + **háttér-pótlás** (új modell bekapcsolásakor a régi minták újraszámolása a felvételből) | `backfillVoiceprints`, fut induláskor és modell-váltáskor | nincs jelzés, hogy fut, mennyi van hátra, mi maradt ki (hiányzó felvétel) |
| Modell-összevetés egy megbeszélésen | `tanara-cli voice-eval` | nem kell a felületre (fejlesztői eszköz) |
| **Sáv-oldal elemzés**: sor oldala, személy oldala, ellentmondások, nyers címkék oldal-megoszlása | `tanara-cli track-sides` | nincs |
| Újraellenőrzés (egész, páros) | motor | **van** (4.1) |
| „megerősítve" / „újraellenőrizve" sor-állapot | motor | nincs jelölő |
| Rövid (beágyazás nélküli) sorok | motor tudja | nincs jelölő |

## 6. Mit kell megtervezni

### E01 — Beállítások › Hangmodellek (új szakasz vagy lap)

- **Lista** modellenként: név, rövid leírás (mire jó: pl. „alap, a csomagban", „jobb elválasztás", „rövid sorokra"), **licenc** (SPDX) és **forrás** link, méret, állapot: `nincs letöltve` · `letöltés… (MB / MB, mégse)` · `letöltve` · `hiba` · `ellenőrzés` (fájl-méret egyezés). Kapcsoló: **bekapcsolva**.
- **Letöltés előtt** a licenc és a forrás látható legyen (a motor is kiírja); CC-BY modellnél az attribúció ténye. Nincs külön jogi szöveg-fal: egy sor + link.
- **Fúzió magyarázata** egy mondatban: „Több bekapcsolt modell együtt dönt; a lassabb modellek lassítják az elemzést."
- **Háttér-pótlás állapota**: „A meglévő N minta újraszámolása az új modellel… (k / N)"; ha kész: mennyi minta nem volt pótolható (hiányzó felvétel) és hol. Ez a Személyek ablakban is látszódjon (lásd E02).
- **Ajánlott alapállapot** jelzése (ma: CAM++ + WeSpeaker). ERes2NetV2 letölthető, de nem ajánlott (magyar beszéden gyengébb).
- **Állapotok:** nincs modell egyáltalán (a hangelemzés kikapcsolt állapotának magyarázata + „Letöltés" CTA); offline (letöltés nem indul); folyamatban; sikertelen (újra); minden kész.
- **Hol:** javaslat: a „Szolgáltatások" lap mellé új lap **„Beszélők"** (hangmodellek + a saját név/hanglenyomat szakasz ide költözhet az Általánosból) — vagy a Rögzítés lap alja. A designer dönt, de a saját hanglenyomat és a modellek egy helyen legyenek.

### E02 — Személyek: minták modellenként, oldal, címkék

- **Minta-tétel:** egy minta egy sor marad; **apró modell-jelölés** (pl. „2/2 modell" vagy pöttyök), tooltipben melyik modellel van és melyikkel nem (hiányzó felvétel). Egy modell hiánya nem hiba, csak jelzés.
- **Háttér-pótlás** jelzése a részletek fejlécében, amíg fut.
- **Személy oldala / sáv-beosztás** alapértéke: `jellemzően helyi` / `jellemzően távoli` (tanult), átírható. Ez megbeszélésenként felülírható az elemzőben (E03).
- **Címkék a személyen** (lásd E04): címke-chip-sor a részletek fejléce alatt, ugyanaz a beviteli mező, mint a megbeszélésnél. Tanult javaslat: „Ezeken a címkéken szokott részt venni: …" (a megbeszélései címkéiből), elfogadható egy kattintással.

### E03 — Elemző: bizonyíték-alapú, tömeges javítás

Ez a legfontosabb rész. Alapelv: **a gép megmondja, mire támaszkodik, és csoportban kínálja a javításokat**, nem soronként.

1. **Sor-jelölők bővítése.** A mai három mellé: `megerősítve` (halk pipa), `rövid` (nincs hang-elemzés: a gép ezen nem tud segíteni, csak a fül), **`sáv-ellentmondás`** (saját ikon; tooltip: „A sávok szerint a hívás hangján érkezett, X viszont a te oldaladon van"). A `bizonytalan` tooltipje mondja meg az okot: hang / sáv / címke.
2. **„Ki mondta?" felugró – jelöltek rangsora indoklással.** Minden jelölt mellett a bizonyíték: `hang 82%` · `ugyanaz a sáv` · `közös címke: MuseumPlus` · `ezen a megbeszélésen már 41 sor`. A tiltott oldali jelölt (más sávon van) lejjebb, halványan, nem eltüntetve.
3. **Kézi sáv-beosztás.** A résztvevő-felugróban (és a Sávok fülön a sáv mellett) sáv-chipek: a személy melyik sávon beszél. Több sáv egy személynél és több személy egy sávon megengedett. Üres = nincs megkötés. Alapérték: a saját név a mikrofonra, a többiek a hívás hangjára; a tanult alapérték felülírja.
4. **Tömeges átnézés mód** (új nézet a Bizonytalan szűrő helyett vagy mellett): a gép **csoportokba** rendezi a javaslatait, csoportonként egy döntéssel:
   - „Sávok szerint a hívás hangján érkezett, de X-nek (helyi) van írva — 69 sor. Javasolt: Y (hang 78%)." `Mind átsorolom` · `Átnézem egyenként` · `Kihagyom`
   - „A megerősített magokhoz képest valószínűleg Z mondta — 15 sor." …
   - „Rövid sorok, amin nem tudok segíteni — 426 sor." (csak tájékoztatás, szűrhető)
   Minden csoportban a sorok meghallgathatók, és a csoport-döntés visszavonható egy lépésben.
5. **Szennyezett-mag figyelmeztetés.** Ha egy személy megerősített sorai két jól elváló hangra esnek: sáv a lista fölött: „X sorai két különböző hangnak tűnnek. Szétválasszam? (57 / 86 sor)" `Szétválasztás` → a két felet külön csoportként mutatja, a másodikra jelöltet ajánl. Ez szakítja meg az önerősítő kört.
6. **Nyers beszélő kettéosztása az első azonosításnál.** Ha egy nyers címke kétoldalú (a mikrofonon ÉS a hívás hangján is), a „Résztvevők azonosítása" eredménye eleve két részt mutat: „Beszélő 1 · mikrofon (90 sor) → te" és „Beszélő 1 · hívás hangja (396 sor) → javasolt: Gábor". A felhasználó a két felet külön nevezi el.
7. **Újraellenőrzés visszajelzése.** Mire támaszkodott: hány megerősített sor, hány tárolt minta (ebből hány erről a megbeszélésről), mely modellekkel; figyelmeztetés, ha két hang nagyon hasonló.
8. **Beszélő-sín és áttekintő:** a sáv-oldal is látszódjon (pl. a beszélő fejlécén egy apró „mikrofon / hívás" jel), hogy a keveredés ránézésre feltűnjön.

### E04 — Személy-címkék és a címke-átfedés mint bizonyíték

- **Ugyanaz a címkekészlet.** Nincs külön személy-címke típus; a Címkék ablak részletében a címke **személyeit** is listázza („Ezeken a megbeszéléseken · Ezek a személyek").
- **Hol tehető fel:** Személyek ablak részletei; a „Ki mondta?" felugróban a jelölt mellett (gyors „+címke"); a Résztvevők azonosítása után a változás-sávban („X-et most először láttad a MuseumPlus címkén. Tegyem rá?").
- **Hogyan használja a gép:** a megbeszélés címkéi ∩ a személy címkéi → előny a rangsorban és a „Résztvevők azonosítása" jelöltjei közt; a „Miért?" indoklásban a közös címke neve. Címke nélküli személyt nem büntet.
- **Könyvtár-szűrő:** a címke-szűrő a személyeken át is találhat („ahol MuseumPlus-os ember beszélt"), ez opcionális, a designer dönt, kell-e.

## 7. Állapotok, amelyeket a tervnek le kell fednie

| Terület | Állapotok |
|---|---|
| Hangmodellek (E01) | nincs letöltött modell · egy modell (alap) · több bekapcsolt · letöltés fut / megszakítva / hiba / offline · pótlás fut / kész / részben (hiányzó felvétel) |
| Minta (E02) | minden modellel · hiányzó modell · pótlás alatt · zajos sorból nem készül (hiba) |
| Sor (E03) | normál · megerősítve · javítva · bizonytalan (ok: hang / sáv / címke) · egymásra beszéltek · rövid · sáv-ellentmondás · újraellenőrzés alatt (fut a háttér) |
| Sáv-oldal | két sáv van (aktív) · egy sáv (inaktív, csendben) · sáv hiányzik (törölt fájl) · kézi beosztás van / nincs |
| Tömeges átnézés | üres („nincs javaslat") · csoportok · csoport végrehajtva + visszavonás · részben kihagyva |
| Címke a személyen | nincs · van · javasolt (tanult, elfogadásra vár) |

## 8. Keretek

- Qt Quick / QML, a meglévő `Theme` tokenek és `T*` vezérlők; világos és sötét téma; magyar és angol (a magyar hosszabb).
- Egy átirat 300–1300 sor, 2–10 beszélő; a tömeges nézetben egy csoport 1–400 sor lehet.
- A hanganalízis háttérben fut; a felület soha nem blokkol, de meg kell mutatnia, mi fut és mi nem áll még rendelkezésre.
- Lenyomat és átsorolás **mindig visszavonható**, egy lépésben csoportra is.
- A felhasználó sosem kényszerül letöltésre: modell nélkül az elemző szerkesztés-módban működik.

## 9. Nyitott kérdések a designernek

1. A hangmodellek helye: külön „Beszélők" lap (saját név + hanglenyomat + modellek) vagy a Rögzítés lap alja?
2. A tömeges átnézés külön nézet (a lista helyett) vagy a Bizonytalan szűrő kibővítése csoportfejlécekkel?
3. Sáv-ellentmondás: önálló jelölő vagy a `bizonytalan` egy ok-variánsa (ikon + tooltip)?
4. Kézi sáv-beosztás helye: a résztvevő-felugró vagy a Sávok fül (vagy mindkettő, egy forrásból)?
5. Személy-címkék a könyvtár-szűrőben: kell-e most?
6. A „Beszélő 1 · mikrofon / hívás hangja" kettéosztás: automatikus a Résztvevők azonosításakor, vagy felajánlott lépés?

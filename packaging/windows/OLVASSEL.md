# Tanara — tesztelői csomag (Windows 11, x64)

A Tanara meetingeket rögzít (mikrofon + a gép hangja), Soniox-szal leírja, és egy
LLM-mel összefoglalót készít. Ez a csomag önálló: nem kell hozzá Qt, fejlesztőeszköz
vagy rendszergazdai jog.

## 1. Telepítés

1. Csomagold ki a zipet egy tetszőleges mappába, például `C:\Tanara`.
   (Ne a `C:\Program Files` alá — oda a felhasználó nem írhat.)
2. Indítsd el a `tanara.exe`-t.
3. Ha a SmartScreen figyelmeztet („A Windows megvédte a számítógépet”), kattints a
   „További információ” → „Futtatás mindenképp” gombra. A csomag nincs aláírva.

A mappában minden a helyén van: `ffmpeg.exe` és `ffprobe.exe` (felvétel, importálás,
hang-dekódolás),
`onnxruntime.dll` és `models\campplus_sv_zh_en_16k.onnx` (beszélő-felismerés),
valamint a Qt-könyvtárak. Ne mozgass ki belőle fájlt.

## 2. Első indítás — beállítások

Nyisd meg a **Beállítások** → **Szolgáltatások** oldalt. A **Saját kulcs** mód
legyen kiválasztva (ez az alapértelmezés). Az **Átírás** és az **Összefoglaló**
résznél add meg a következőket.

**Átírás (STT) — Soniox**
- *API-kulcs*: a Soniox-kulcsot Ádámtól kapod meg, külön csatornán. Ide másold be.
- *Cím (URL)*: maradhat az alapértelmezett (`https://api.soniox.com/v1`).
- A *Kapcsolat tesztelése* gomb ellenőrzi a kulcsot.

**Összefoglaló (LLM) — OpenAI-kompatibilis végpont**
- *Cím (URL)*: a helyi LM Studio címe (alapértelmezés: `http://localhost:1234/v1`),
  vagy az a cím, amit Ádámtól kapsz.
- *Modell*: válaszd ki a listából (a lista a szerverről töltődik be).
- *API-kulcs*: csak akkor kell, ha a végpont kéri. LM Studióhoz üresen hagyható.

**Tanara Cloud**: hamarosan, most nem használható. A beállításokban megjelenő
Tanara Cloud / „hamarosan” részt hagyd figyelmen kívül.

A beszélő-felismerés modellje a csomagban van, ehhez nincs teendő.

## 3. Hol vannak az adatok?

| Mi | Hol |
|---|---|
| Beállítások, kulcsok, személyek, hang-lenyomatok, naplók | `%USERPROFILE%\.tanara\` (pl. `C:\Users\<név>\.tanara\`) |
| Felvételek | `%USERPROFILE%\Tanara\recordings\` |
| Átiratok, összefoglalók (Markdown) | `%USERPROFILE%\Tanara\notes\` |

A felvételek és a jegyzetek mappája a Beállításokban átállítható.
Ha a gépről mindent törölni akarsz: zárd be a Tanarát, és töröld a fenti mappákat
és a kicsomagolt `C:\Tanara` mappát.

## 4. Tálca-figyelő (`tanara-watcher.exe`)

A `tanara-watcher.exe` a rendszertálcán fut, és onnan egy kattintással indítható a
felvétel. Kézzel indítsd el a mappából. A Beállítások → Hívásfigyelő →
„Induljon el a bejelentkezéskor” kapcsoló Windowson még nem hoz létre indítási
bejegyzést. Ha bejelentkezéskor is el kell indulnia, tegyél parancsikont a
`tanara-watcher.exe`-ről ide: `Win+R` → `shell:startup`.

## 5. Ismert korlátok Windowson

- **Hívás-felismerés nincs.** Az automatikus meeting-észlelés (Teams/Zoom/Meet
  indulásakor értesítés) jelenleg csak Linuxon működik. Windowson a felvételt
  kézzel kell indítani (főablak vagy tálca-figyelő).
- **A gép hangja WASAPI loopbackkel jön.** A lejátszó eszközök „loopback”
  felvevőként jelennek meg az eszközlistában. Fejhallgatóval is működik, de csak
  azt a kimenetet rögzíti, amelyiket kiválasztod — ha a hívás közben eszközt
  váltasz (pl. Bluetooth-headset be), ellenőrizd a kiválasztást.
- A felvétel alatt ne altasd a gépet.

## 6. Hibabejelentés

Küldd el:
1. Rövid leírás: mit csináltál, mit vártál, mi történt (képernyőkép, ha van).
2. A naplókat: `%USERPROFILE%\.tanara\logs\tanara.log` és `tanara-error.log`.
3. A verziót, parancssorból: `tanara-cli.exe --version`.

A naplók nem tartalmaznak API-kulcsot. A hangfelvételt csak akkor küldd el, ha
a hiba azzal függ össze, és a résztvevők hozzájárultak.

## Hasznos parancsok (opcionális)

Parancssorból, a Tanara mappájában:

```
tanara-cli.exe --version      verzió
tanara-cli.exe devices        a felismert hangeszközök (mikrofonok, loopback)
```

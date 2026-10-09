# Tanara — külső komponensek leltára (third-party inventory)

Gépi forrása: `inventory.json` (ugyanebben a mappában). Ez a jegyzet abból generálódik; a GUI „Felhasznált szoftverek" oldala
később szintén ebből a JSON-ból készülhet. A `THIRD_PARTY_NOTICES.md` kézzel írt, de ezzel szinkronban tartott. Felmérés dátuma: 2026-10-09,
alap: `main` f905ca0; Windows-csomag: Tanara-0.5.11-win64.zip (csak listázva). Javítások: `chore/third-party-notices` ág (2026-10-09):
LGPL-only FFmpeg-build, licencszövegek (`packaging/licenses/`), újraírt NOTICES, telepítő-`LicenseFile`.

Jelmagyarázat: **Attribúció** = a licenc előír-e megjelölést/licencszöveget, és teljesül-e (a javított csomag/dokumentáció szerint; a Windows-csomag
tényleges tartalma a következő csomag-építés után ellenőrizendő). „inference” a JSON-ban = az agent következtetése, nem olvasott tény.

## Vendorozott forrás (belefordítva)

| Komponens | Verzió | Licenc | Szállítva | Attribúció | Mire használjuk |
|---|---|---|---|---|---|
| dr_wav / dr_flac / dr_mp3 (embedded in miniaudio) | version of embedded copies not stated in grep; bundled with miniaudio 0.11.25 | MIT-0 OR Unlicense | linux-build, windows-zip, windows-installer | nem szükséges | WAV/FLAC/MP3 dekódolás a miniaudio-n belül. |
| kaldi-native-fbank | v1.22.3 (2025-10-09), commit b09e686fe2084732ddd30d1ef80acfc0f13eaf01; csrc files byte-identical to the tag | Apache-2.0 | linux-build, windows-zip, windows-installer | rendben | Kaldi-kompatibilis fbank jellemzők a beszélő-embedding (voice-ID) előfeldolgozásához. |
| KISS FFT (float build) | 131.1.0 (matches upstream tags 131.1.0/131.2.0 for the compiled files); recorded in third_party/kissfft/VENDORED.txt | BSD-3-Clause | linux-build (system lib, dynamically linked - not shipped by Tanara), windows-zip, windows-installer | rendben | Gyors Fourier-transzformáció a kaldi-native-fbank-hoz (Windowson vendorelt forrásból, Linuxon rendszercsomagból). |
| miniaudio | 0.11.25 (2026-03-04) - MA_VERSION_* in header | MIT-0 OR Unlicense | linux-build, windows-zip, windows-installer | nem szükséges | Hangrögzítés és lejátszás (WASAPI/PipeWire/Pulse/ALSA stb. backendek). |
| miniz | 3.1.2 (release 2026-07-01); recorded in third_party/miniz/VENDORED.txt; Tanara vendoring commit c87136a | MIT | linux-build, windows-zip, windows-installer | rendben | ZIP írás/olvasás a megbeszélés-archívumhoz (*.tanara.zip). |

## Linkelt könyvtár

| Komponens | Verzió | Licenc | Szállítva | Attribúció | Mire használjuk |
|---|---|---|---|---|---|
| ONNX Runtime | Windows: 1.20.1 (README/packaging docs; onnxruntime.dll timestamp 2024-11-19); Linux dev: 1.22.2 (Fedora onnxruntime-devel, ORT_API_VERSION 22) | MIT | linux-build (system lib, not shipped), windows-zip, windows-installer | rendben | A beszélő-embedding (CAM++) ONNX modell futtatása. |
| POSIX Threads / libm / libdl (Linux system libraries) | glibc of the build host | LGPL-2.1-or-later (glibc) | linux-build (system, not shipped) | nem szükséges | Szálak, matematika, dinamikus betöltés (miniaudio dlopen). |
| Qt 6 (Core, Gui, Network, Sql, Widgets, Multimedia, MultimediaWidgets, Qml, Quick, QuickControls2, Svg, DBus) | 6.11.2 on Linux dev (Fedora qt6-qtbase 6.11.2); 6.11.1 mingw_64 documented for Windows (packaging/windows/README.md; DLL timestamps 2026-05-07/08) | LGPL-3.0-only (Qt 6.11 open-source edition is LGPL-3.0-only OR GPL-3.0-only WITH Qt-GPL-exception-1.0; some add-on modules GPL-only) | linux-build (system Qt, not shipped), windows-zip, windows-installer | rendben | Felhasználói felület (Widgets + Qt Quick/QML), hálózat, SQLite, médialejátszás, SVG, fordítás. |
| Windows SDK system libraries (ole32, oleaut32, uuid, winmm, avrt, ksuser, secur32, plus Qt-implied d3d11, user32 ...) | Windows 10+ (installer MinGW MinVersion=10.0) | LicenseRef-Microsoft-Windows | none (OS-provided) | nem szükséges | WASAPI/COM audio, Schannel TLS, multimédia időzítők - rendszer-API-k. |

## Szállított bináris (Windows-csomag)

| Komponens | Verzió | Licenc | Szállítva | Attribúció | Mire használjuk |
|---|---|---|---|---|---|
| D3Dcompiler_47.dll | 47 (file date 2014-03-11) | LicenseRef-Microsoft-Redistributable | windows-zip, windows-installer | rendben | Qt shader-fordító függősége (windeployqt másolja); Tanara közvetlenül nem használja. |
| FFmpeg command-line tools: ffmpeg.exe and ffprobe.exe (Windows static build) | FFmpeg n9.0.2-17-g2a571b6068 (release/9.0, commit 2a571b606854520cf89804d8030c8b328e621689), BtbN autobuild-2026-09-30-13-08, file ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0.zip, SHA256 6b264b9e6019103f601d98c292bd332fd87acf1c5e941ddff4fb71760fe63432 | LGPL-3.0-or-later (build configured with --enable-version3, no --enable-gpl/--enable-nonfree; includes libopus, libmp3lame; excludes x264/x265) | windows-zip, windows-installer | rendben | Hangfelvétel kódolás/dekódolás, importálás, hullámforma, 16 kHz PCM kinyerés - QProcess-szel indított külső program. |
| FFmpeg libraries bundled with Qt Multimedia (avcodec-61, avformat-61, avutil-59, swresample-5, swscale-8) | FFmpeg 7.1.x (inference from DLL sonames avcodec-61/avformat-61/avutil-59; Qt 6.11 ships its own LGPL build) | LGPL-2.1-or-later (inference: Qt builds its FFmpeg without GPL components) | windows-zip, windows-installer | rendben | A Qt Multimedia FFmpeg-backendje (lejátszás) használja; Tanara közvetlenül nem hívja. |
| Microsoft Visual C++ runtime: msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll | 14.x (file dates 2025-11-21; copied from C:\Windows\System32 per packaging/windows/README.md) | LicenseRef-Microsoft-VC-Redistributable | windows-zip, windows-installer | rendben | Az MSVC-vel fordított onnxruntime.dll futtatókörnyezete (tiszta gépen nem biztos, hogy van). |
| MinGW-w64 GCC runtime DLLs: libstdc++-6.dll, libgcc_s_seh-1.dll | GCC 13.1 (Qt-bundled mingw1310_64; DLL timestamps 2023-05-24) | GPL-3.0-or-later WITH GCC-exception-3.1 | windows-zip, windows-installer | rendben | A MinGW-vel fordított exe-k C++ futtatókörnyezete. |
| opengl32sw.dll (Mesa llvmpipe software OpenGL) | Mesa version unknown (file date 2022-11-28; Qt-supplied) | MIT (Mesa) AND others incl. LLVM Apache-2.0 WITH LLVM-exception (inference) | windows-zip, windows-installer | rendben | Szoftveres OpenGL tartalék (windeployqt másolja), ha nincs GPU-driver. |
| Third-party code bundled inside Qt 6.11 DLLs (PCRE2, double-conversion, zlib, libpng, libjpeg, FreeType, HarfBuzz, md4c, tinycbor, forkfd, Mesa etc.) | bundled by Qt 6.11.x (exact versions in Qt's 'Licenses Used in Qt' docs) | MIXED (BSD-2/3-Clause, MIT, Zlib, libpng, FTL OR GPL-2.0, IJG, Apache-2.0 ... per component) | windows-zip, windows-installer | rendben | A Qt DLL-ekbe fordított harmadik féltől származó kódok. |
| winpthreads: libwinpthread-1.dll (mingw-w64) | bundled with Qt mingw1310_64 (GCC 13.1 toolchain; DLL 2023-05-24) | MIT (mingw-w64 winpthreads: MIT-style with some BSD-3 parts; inference) | windows-zip, windows-installer | rendben | POSIX szálak a MinGW futtatókörnyezetben. |

## Beágyazott eszköz (betű, ikon, spec)

| Komponens | Verzió | Licenc | Szállítva | Attribúció | Mire használjuk |
|---|---|---|---|---|---|
| docs/cloud-gateway-api.yaml (OpenAPI spec) | — | MIT | none | nem szükséges | A Tanara Cloud átjáró saját API-leírása. |
| IBM Plex Sans / IBM Plex Mono (7 TTF) | release/commit not recorded (fetch-assets.sh uses IBM/plex master ref); files Sans Regular/Medium/SemiBold/Bold + Mono Regular/Medium/SemiBold | OFL-1.1 | linux-build, windows-zip, windows-installer | rendben | A felület betűtípusai. |
| Lucide icons (lucide-static) | 0.460.0 (gui/qml/fetch-assets.sh LUCIDE_VERSION); 81 SVGs in gui/qml/icons (incl. a few not in the fetch list: arrow-down-to-line, cable, chevrons-down, chevrons-up, hash, maximize-2, minimize-2, pin, tag) | ISC (portions MIT from Feather) | linux-build, windows-zip, windows-installer | rendben | Felület-ikonok (SVG), futásidőben színezve. |

## ML-modell

| Komponens | Verzió | Licenc | Szállítva | Attribúció | Mire használjuk |
|---|---|---|---|---|---|
| BGE-M3 embedding model (BAAI/bge-m3) | default id 'text-embedding-bge-m3' (BuiltinProviders.cpp:201) | MIT | none | nem szükséges | Többnyelvű szöveg-beágyazás a címkékhez / hasonlósághoz, OpenAI-kompatibilis végponton át; nem szállítjuk. |
| CAM++ speaker-embedding model (3D-Speaker, ONNX export via sherpa-onnx) | file campplus_sv_zh_en_16k.onnx (upstream name 3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx); no hash/version pinned | Apache-2.0 | windows-zip, windows-installer | rendben | Beszélő-lenyomat (embedding) számítása a beszélők automatikus felismeréséhez; helyben fut ONNX Runtime-mal. |
| ERes2NetV2 speaker model, 3D-Speaker | 3dspeaker_speech_eres2netv2_sv_zh-cn_16k-common.onnx (71,441,526 B), sherpa-onnx release speaker-recongition-models | Apache-2.0 | none | rendben | Alternatív beszélő-embedding modell; a felhasználó tölti le (voice-models fetch), nem része a csomagnak. |
| Google Gemma (google/gemma-4-12b; prompt-eval also tests google/gemma-4-12b-qat) | default model id 'google/gemma-4-12b' (core/src/provider/BuiltinProviders.cpp:78); GGUF/quant chosen by user in LM Studio | LicenseRef-Gemma-Terms-of-Use | none | nem szükséges | Alapértelmezett helyi LLM az összefoglalókhoz és címkékhez (LM Studio-n át); a Tanara nem szállítja a súlyokat. |
| sherpa-onnx (k2-fsa) — source of the ONNX CAM++ export | — | Apache-2.0 | none | nem szükséges | Innen töltődik le az ONNX-ra exportált CAM++ modell (release 'speaker-recongition-models'); a sherpa-onnx kódot nem használjuk. A feature-kinyerés kaldi-native-fbank-kal (A rész) egyezik az export tanító-előfeldolgozásával. |
| WeSpeaker ResNet34-LM (VoxCeleb) | wespeaker_en_voxceleb_resnet34_LM.onnx (26,530,550 B), sherpa-onnx release speaker-recongition-models | CC-BY-4.0 | none | rendben | Alternatív beszélő-embedding modell (256 dim.); a felhasználó tölti le, nem része a csomagnak. |

## Futásidejű külső program / rendszer-API

| Komponens | Verzió | Licenc | Szállítva | Attribúció | Mire használjuk |
|---|---|---|---|---|---|
| FFmpeg (ffmpeg / ffprobe executables) | FFmpeg n9.0.2-17-g2a571b6068 (release/9.0, commit 2a571b606854520cf89804d8030c8b328e621689), BtbN autobuild-2026-09-30-13-08, file ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0.zip, SHA256 6b264b9e6019103f601d98c292bd332fd87acf1c5e941ddff4fb71760fe63432 | LGPL-3.0-or-later (build configured with --enable-version3, no --enable-gpl/--enable-nonfree; includes libopus, libmp3lame; excludes x264/x265) | windows-zip, windows-installer | rendben | Hangfelvétel kódolása (libopus), lekeverés (libmp3lame, loudnorm), importált hangfájlok dekódolása/kódolása, hullámforma és beszélő-szegmensek dekódolása; az ffprobe a hangfájlok adatait olvassa. |
| freedesktop.org Notifications (D-Bus), XDG autostart, QSystemTrayIcon/StatusNotifier | — | N/A (service terms / OS API — no software license applies) | none | nem szükséges | Értesítések (org.freedesktop.Notifications), induláskori automatikus indítás (~/.config/autostart/tanara-watcher.desktop), tálcaikon. |
| PipeWire (pw-dump) | — | MIT | none | nem szükséges | Linuxon a pw-dump JSON-kimenetéből derül ki, melyik alkalmazás használja a mikrofont (hívásfelismerés) és melyek a lejátszó-eszközök (lejátszás-irányítás). |
| PipeWire / PulseAudio / ALSA (via miniaudio backends) | — | MIT (PipeWire) / LGPL-2.1+ (PulseAudio client lib) / LGPL-2.1+ (ALSA lib) | none | nem szükséges | Linuxon a miniaudio ezeken a rendszer-hangszervereken át vesz fel és játszik le; a miniaudio dinamikusan tölti be őket (dlopen). |
| Windows APIs: WASAPI loopback, CapabilityAccessManager ConsentStore registry, HKCU Run key, process snapshot, WebView2 parent-chain detection | — | N/A (service terms / OS API — no software license applies) | none | nem szükséges | WASAPI loopback a gép hangjához (miniaudio), a mikrofonhasználat felismerése a ConsentStore registryből, a 'Tanara Watcher' bejelentkezéskori indítása HKCU\...\Run kulcson át, az msedgewebview2 szülő-lánc feloldása. |

## Hálózati szolgáltatás / külső név

| Komponens | Verzió | Licenc | Szállítva | Attribúció | Mire használjuk |
|---|---|---|---|---|---|
| Call/meeting app names (Google Meet, Microsoft Teams, Zoom, Webex, Slack, Discord, Skype) and browsers (Chrome, Edge, Firefox, Brave, Opera, Vivaldi) | — | N/A (service terms / OS API — no software license applies) | windows-zip, windows-installer, linux-build | nem szükséges | Csak név-egyezés: a hívásfigyelő ezeknek a programoknak/böngészőknek a mikrofonhasználatát ismeri fel; semmilyen API-hívás nem megy hozzájuk. |
| OpenAI-compatible LLM/STT/embedding endpoints (LM Studio, Ollama, OpenAI, Whisper-compatible servers) | — | N/A (service terms / OS API — no software license applies) | none | nem szükséges | Összefoglaló, címkézés és beágyazás OpenAI-kompatibilis /v1 végpontokon (alapért.: LM Studio http://localhost:1234/v1), opcionálisan whisper-kompatibilis STT (http://localhost:8000/v1 vagy api.openai.com). |
| Soniox speech-to-text API | — | N/A (service terms / OS API — no software license applies) | none | nem szükséges | Felhőalapú, aszinkron beszéd-szöveg átírás (alapértelmezett STT; a felhasználó saját kulcsával). |
| Tanara Cloud (RemedIT Hungary Kft.) | — | N/A (service terms / OS API — no software license applies) | windows-zip, windows-installer, linux-build | nem szükséges | A RemedIT saját átírás/összefoglaló/beágyazás átjárója (api.tanara.remedit.hu), bejelentkezéssel; opcionális, jelenleg 'hamarosan'. |
| Telemetry / update checks (none found) | — | N/A (service terms / OS API — no software license applies) | none | nem szükséges | Nincs: a kódban (core/gui/cli/watcher) nincs frissítés-ellenőrzés vagy telemetria-végpont. |
| Third-party names in UI text (Soniox, LM Studio, OpenAI, Ollama, Teams, Zoom, Google Meet, Gemma) | — | N/A (service terms / OS API — no software license applies) | windows-zip, windows-installer, linux-build | nem szükséges | Felhasználói szövegekben és beállítás-súgókban termékek megnevezése (nominatív használat). |

## Build- és csomagoló-eszköz

| Komponens | Verzió | Licenc | Szállítva | Attribúció | Mire használjuk |
|---|---|---|---|---|---|
| CMake and Ninja | CMake >=3.21 required (4.3.0 on Linux dev; Qt-bundled CMake_64 on Windows); Ninja 1.13.2 (Linux) | BSD-3-Clause (CMake); Apache-2.0 (Ninja) | none | nem szükséges | Build rendszer. |
| GCC (Linux) and MinGW-w64 GCC 13.1 (Windows, Qt-bundled mingw1310_64) | GCC 16.2.1 (Fedora dev); MinGW GCC 13.1 (Windows) | GPL-3.0-or-later WITH GCC-exception-3.1 | none | nem szükséges | Fordító. |
| Inno Setup 6 | 6.x (winget JRSoftware.InnoSetup; exact version not recorded) | LicenseRef-InnoSetup (Inno Setup License, permissive) | windows-installer (the setup stub is embedded in the installer) | nem szükséges | A Windows-telepítő (Tanara-x.y.z-win64-setup.exe) készítése. |
| Node.js (dev-only scripts: tools/prompt-eval, tests/mock-gateway) | not recorded; scripts use only Node built-ins (node:fs, http, https, child_process, crypto, os, url, path) - no npm dependencies | MIT (Node.js) | none | nem szükséges | Prompt-kiértékelő eszköz és a cloud-integrációs tesztek mock-gateway-e. |
| pkg-config (kissfft-float lookup, Linux) | n/a | GPL-2.0-or-later | none | nem szükséges | Linuxon a kissfft megkeresése. |
| Qt build tools: windeployqt, lupdate, lrelease, moc/rcc/uic, qmltyperegistrar, qmlcachegen | Qt 6.11.x | LGPL-3.0-only (Qt 6.11 open-source edition is LGPL-3.0-only OR GPL-3.0-only WITH Qt-GPL-exception-1.0; some add-on modules GPL-only) | none | nem szükséges | Telepítő-mappa összerakása, fordítások (.ts -> .qm), MOC/RCC. |

## Összesítés

- 43 tétel; 19 tételnél kötelező az attribúció, ebből 19 teljesül.
- A Windows-csomag a javítás után tartalmazza: `LICENSE`, `THIRD_PARTY_NOTICES.md`, `licenses\` (lásd `packaging/windows/README.md`); a telepítőnek van `LicenseFile`-ja.
- Hangmodellek: a CAM++ a Windows-csomagban van (Apache-2.0); a WeSpeaker ResNet34-LM (CC-BY-4.0) és az ERes2NetV2 (Apache-2.0) a `tanara-cli voice-models fetch` paranccsal tölthető le, nincs csomagolva.

## Nyitott kérdések

- A pinelt FFmpeg-zip (`ffmpeg-n9.0.2-17-g2a571b6068-win64-lgpl-9.0.zip`) tartalmát a BtbN build-scriptekből ellenőriztük, de a zipet nem töltöttük le: az első csomag-építésnél futtasd a `ffmpeg.exe -version` és `-encoders` ellenőrzést (lásd a packaging README-t).
- A Qt-vel szállított FFmpeg-DLL-ek pontos patch-verziója nincs ellenőrizve (avcodec-61 = FFmpeg 7.1-széria).
- A VC++ DLL-ek hivatalos redistributable-ből jönnek-e a következő csomagban (a README lépése ezt írja elő); jogi felülvizsgálat nem történt.
- D3Dcompiler_47 / opengl32sw / qmltooling / nem használt Quick-stílusok / qoffscreen kihagyható-e a csomagból.
- A hangmodellek tanító-adat feltételei (VoxCeleb, 3D-Speaker adat) nincsenek vizsgálva; csak a modell-szintű licenc.
- Soniox: kéri-e a Tanara a feltöltött hang törlését (az async tár 30 nap után törlődik a dokumentáció szerint)?
- Tanara Cloud: végleges adatvédelmi/feltételek oldal (a CloudTypes.h-beli URL feltételezés); szolgál-e majd Gemmát (hosztolt terjesztés a Gemma-feltételek szerint)?
- A hanglenyomat biometrikus személyes adat — a GDPR-szöveg nem e leltár része.
- A forrás-ajánlat (FFmpeg/BtbN) a NOTICES-ban linkekkel teljesül; ha a BtbN-repó vagy a release eltűnik, a RemedIT-nek saját tükörre/írásos ajánlatra van szüksége (tulajdonosi döntés).
- Linux: nincs szállított csomag; ha AppImage/Flatpak/RPM jön, a Windows-lista érvényes rá is.

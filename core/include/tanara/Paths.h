#pragma once
//
// Tanara — útvonal-segédek: a metaadat-mappa EGYETLEN igazságforrása.
//
// Alapértelmezés: ~/.tanara. A TANARA_HOME környezeti változó felülírja — ilyenkor az app
// SEMMIT nem olvas/ír a ~/.tanara alatt (settings, kulcsok, személyek, lenyomatok, logok,
// promptok, modellek, state.json, lock-fájlok, index.db). Ez teszi lehetővé a biztonságos
// tesztelést mintaadaton: TANARA_HOME=/tmp/sandbox/home tanara
//
// SZABÁLY: sehol máshol ne számolj `homePath() + ".tanara"`-t — mindig ezt használd.
//
#include <QString>

namespace tanara {
namespace paths {

// A TANARA_HOME értéke abszolút, "~"-kifejtett úttá oldva; üres, ha nincs beállítva.
QString homeOverride();

// Az alapértelmezett metaadat-mappa: TANARA_HOME, különben ~/.tanara.
QString defaultMetadataDir();

// "~" / "~/x" kifejtése a felhasználó home-jára (más út változatlan).
QString expandHome(const QString& path);

// Egy (beállításból jövő, akár üres vagy "~"-os) metaadat-mappa feloldása:
//  - ha TANARA_HOME be van állítva → MINDIG az (a settings.json-beli érték nem térítheti el);
//  - üres → defaultMetadataDir(); egyébként a "~"-kifejtett érték.
QString resolveMetadataDir(const QString& configured);

// Fájl/almappa a metaadat-mappában (pl. metadataFile("people.json")).
QString metadataFile(const QString& relative, const QString& configuredDir = QString());

// TANARA_HOME mellett a felvételek / jegyzetek ALAPÉRTELMEZETT helye is a sandboxba kerül
// (<TANARA_HOME>/recordings, <TANARA_HOME>/notes), különben ~/Tanara/recordings|notes.
// (A settings.json-ban explicit megadott mappát nem írja felül.)
QString defaultAudioDir();
QString defaultNotesDir();

// Az ALAP beszélő-embedding modell (CAM++) fájlneve és feloldása — a VoiceModelRegistry
// szabályát használja (ott a többi modellé is). Sorrend:
//  1) <metaDir>/models/<név>  — a felhasználó saját (letöltött) modellje;
//  2) <appDir>/models/<név>   — az alkalmazás mellé csomagolt modell (Windows-zip/telepítő);
//  3) egyik sincs → az 1) útja (a „várt hely”, hibaüzenetekhez / letöltési célnak).
// Az appDir üres is lehet (pl. QCoreApplication nélkül) — ilyenkor a 2) kimarad.
QString voiceModelFileName();
QString resolveVoiceModelPath(const QString& metaDir, const QString& appDir);

} // namespace paths
} // namespace tanara

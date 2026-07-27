#pragma once
//
// Tanara — a beépített LLM-promptok EGY helye + fájl-override.
//
// Prompt-azonosítók: "simple" (sima összefoglaló) | "topic" (téma-kinyerés) |
// "analysis" (témánkénti elemzés) | "reduce" (záró vezetői összefoglaló).
//
// Feloldási sorrend egy promptra:
//   1. a Beállításokban tárolt user-override (az AppController kezeli, nem itt),
//   2. <metadataDir>/prompts/<id>.md — kézzel hangolható, ÚJRAFORDÍTÁS NÉLKÜL,
//   3. a kódba égetett beépített default (promptBuiltin).
//
#include <QString>

namespace tanara {

// A kódba égetett default a megadott id-hoz (ismeretlen id → üres string).
QString promptBuiltin(const QString& id);

// A fájl-override útja: <metadataDir>/prompts/<id>.md ("~"-t kifejti;
// metadataDir üres → ~/.tanara).
QString promptFilePath(const QString& id, const QString& metadataDir = QString());

// Fájl-override, ha létezik és nem üres, különben a beépített default.
QString promptDefault(const QString& id, const QString& metadataDir = QString());

} // namespace tanara

#pragma once
//
// Tanara — a beépített LLM-promptok EGY helye + fájl-override.
//
// Prompt-azonosítók:
//   gyors összefoglaló (SummaryService): "notes" (részenkénti jegyzet) | "merge" (a
//     részjegyzetek összegzése) | "single" (egy részből álló megbeszélés: jegyzet + összegzés
//     egy hívásban; a régi "simple" azonosító ennek álneve),
//   témánkénti elemzés: "topic" (téma-kinyerés) | "analysis" (témánkénti elemzés) |
//     "reduce" (záró vezetői összefoglaló),
//   címkejavaslat az összefoglaló után: "tags" (LlmTagSuggester; sima „pick: / new:” sorok).
// A beépített promptok angolok; a kimenet nyelvét a {{LANGUAGE}} (angol nyelvnév) ill. a
// régi {{NYELV}} (a beállítás szövege) változó adja.
//
// Feloldási sorrend egy promptra:
//   1. a Beállításokban tárolt user-override (az AppController kezeli, nem itt),
//   2. <metadataDir>/prompts/<id>.md — kézzel hangolható, ÚJRAFORDÍTÁS NÉLKÜL,
//   3. a kódba égetett beépített default (promptBuiltin).
//
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

// A kódba égetett default a megadott id-hoz (ismeretlen id → üres string).
QString promptBuiltin(const QString& id);

// A fájl-override útja: <metadataDir>/prompts/<id>.md ("~"-t kifejti;
// metadataDir üres → ~/.tanara).
QString promptFilePath(const QString& id, const QString& metadataDir = QString());

// Fájl-override, ha létezik és nem üres, különben a beépített default.
QString promptDefault(const QString& id, const QString& metadataDir = QString());

// Az összefoglaló CÉLNYELVÉNEK alkalmazása egy rendszer-promptra:
//  - a {{LANGUAGE}} placeholdert az angol nyelvnévre (summaryLanguageName), a {{NYELV}}-et a
//    beállítás szövegére cseréli,
//  - placeholder nélküli (saját) promptnál nem-magyar célnyelv esetén direktívát fűz hozzá.
// language üres → "magyar". A ## Döntések / ## Teendők szakaszcímek magyarok maradnak.
QString applySummaryLanguage(QString prompt, const QString& language);

// A célnyelv (a felhasználó szabad szövege, pl. "magyar", "német") angol neve az angol
// promptokhoz: a gyakori nyelveket leképezi (magyar → Hungarian), minden mást változatlanul ad.
// Üres → "Hungarian".
QString summaryLanguageName(const QString& language);

// Rövid nyelvi emlékeztető, amit az átirat / jegyzet UTÁN fűzünk a felhasználói üzenethez
// (a modell ezt olvassa utoljára — a Gemma angol prompttal hajlamos angolul válaszolni).
QString languageReminder(const QString& language);

// A promptokban a KÓD által behelyettesített változók (a Beállítások jelmagyarázata és a
// szerkesztő kiemelése ebből dolgozik — csak az szerepel itt, amit tényleg cserélünk).
struct PromptVariable {
    QString token;         // pl. "{{NYELV}}"
    QString description;   // mire cserélődik (a UI nyelvén)
};
QVector<PromptVariable> promptVariables();

// Egy prompt KIMENETÉNEK alakja, ahogy a feldolgozó kód várja (nem szerkeszthető; a
// Beállítások „Kimeneti séma” ablaka mutatja). id: simple | topic | analysis | reduce.
struct PromptOutputFormat {
    QString kind;          // "json" | "markdown" | "text"
    QString summary;       // egysoros: „execSummary, decisions[], actionItems[], participants[]”
    QString body;          // a teljes leírás (séma / minta), egyenközű betűvel megjelenítve
};
PromptOutputFormat promptOutputFormat(const QString& id);

} // namespace tanara

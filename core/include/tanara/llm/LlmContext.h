#pragma once
//
// Az LLM kontextus-méretének kezelése — TISZTA (I/O-mentes) építőkövek:
//  - token-becslés karakterekből és egy hívás kontextus-igénye (bemenet + kimeneti keret),
//  - a szabványos kontextus-lépcsők (8k / 16k / 20k / 32k …),
//  - a „nem fér a kontextusba” hiba felismerése a szerver válaszából (LM Studio / llama.cpp
//    exceed_context_size_error, OpenAI context_length_exceeded, vLLM / szöveges alakok),
//  - az LM Studio natív API-jának (GET /api/v1/models) értelmezése: a beállított modell
//    betöltött példányai (kontextus, párhuzamosság) és a modell maximuma,
//  - a betöltés előtti döntés (marad / betöltés / újratöltés).
// A hálózati részt (lekérdezés, betöltés) az LlmServer.h osztályai végzik.
//
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {
namespace llmctx {

// Karakter / token arány a becsléshez. Magyar átiratokon (Gemma / Qwen tokenizálóval) mérve
// kb. 2,5 karakter jut egy tokenre; az angol promptokra ez felülbecsül (ott ~4), ami a
// kontextus-igénynél a biztonságos irány.
constexpr double kCharsPerToken = 2.5;
// A chat-sablon (szerep-jelölők, rendszer-fejléc) tokenjei hívásonként.
constexpr int kTemplateOverheadTokens = 256;
// Biztonsági ráhagyás a bemenet becslésére (+10%).
constexpr double kSafetyFactor = 1.10;
// Egy rész legalább ennyi token átiratot kell vigyen, hogy a részenkénti jegyzetelésnek
// értelme legyen (~2–3 perc beszéd); ennél kisebb kontextusnál nem bontunk tovább.
constexpr int kMinPartTokens = 1000;

// Becsült tokenszám karakterszámból (felfelé kerekítve).
int estimateTokens(qint64 chars);
// Egy hívás kontextus-igénye: a bemenet (rendszer-prompt + felhasználói üzenet) becsült
// tokenjei a sablonnal és a ráhagyással, plusz a kimeneti keret (max_tokens).
int callContextNeed(qint64 inputChars, int outputTokens);
// A callContextNeed() megfordítása: adott kontextus mellett legfeljebb hány karakter átirat
// fér egy hívásba (a prompt és a kimeneti keret mellett). -1, ha kMinPartTokens sem fér.
int partBudgetChars(int contextTokens, qint64 promptChars, int outputTokens);
// Egy tipikus összefoglaló-rész (~15 perc magyar beszéd + jegyzet-prompt + 3000 token
// jegyzet) kontextus-igénye — a kapcsolat-teszt figyelmeztetéséhez.
int typicalSummaryPartNeed();

// A szabványos kontextus-lépcsők, növekvő sorrendben.
QVector<int> contextSteps();
// A legkisebb lépcső, ami lefedi a need-et; maxContext > 0 esetén legfeljebb annyi. Ha a need
// minden lépcsőnél nagyobb, a need 1024-re felkerekítve (szintén a maximumig).
int contextStepFor(int need, int maxContext = 0);

// A „nem fér a kontextusba” hiba adatai.
struct ContextOverflow {
    bool matched = false;
    int promptTokens = -1;      // a kérés tokenjei (ha a szerver megmondta)
    int contextTokens = -1;     // a betöltött kontextus (n_ctx / maximum context length)
    QString code;               // exceed_context_size_error | context_length_exceeded | …
};
// Felismerés a hibatörzsből és/vagy a hibaszövegből. A törzs lehet LM Studio / llama.cpp
// ({"error":{"type":"exceed_context_size_error","n_prompt_tokens":…,"n_ctx":…}}), a szövegbe
// ágyazott ugyanilyen JSON („Engine protocol predict request returned 400: {…}”), OpenAI
// ({"error":{"code":"context_length_exceeded","message":"… maximum context length is 8192
// tokens. However, your messages resulted in 9000 tokens …"}}) vagy vLLM-szerű szöveg.
ContextOverflow parseContextOverflow(const QByteArray& body, const QString& text = QString());

// ---- LM Studio natív API ----------------------------------------------------------------

struct LlmInstanceInfo {
    QString id;                 // a példány azonosítója (pl. "qwen3.8-27b" vagy "qwen3.8-27b:2")
    int contextLength = -1;     // -1 = ismeretlen
    int parallel = -1;          // -1 = ismeretlen (régebbi szerver: 1-nek vesszük)
};

struct LlmServerInfo {
    enum class Kind { Unknown, LmStudio };
    Kind kind = Kind::Unknown;  // Unknown: nincs natív API (llama.cpp, Ollama, vLLM, felhő …)
    QString model;              // a beállított modell (amire a lekérdezés vonatkozott)
    bool modelListed = false;   // a beállított modell szerepel a szerver listájában
    QString modelKey;           // a lista kulcsa (betöltéshez ezt küldjük)
    int maxContext = -1;        // a modell max_context_length-je
    QVector<LlmInstanceInfo> instances;   // a beállított modell betöltött példányai
    QStringList otherLoaded;    // MÁS betöltött modellek kulcsai (LLM-ek)
    bool isLmStudio() const { return kind == Kind::LmStudio; }
    bool loaded() const { return !instances.isEmpty(); }
};

// A natív API gyökere a beállított (OpenAI-kompatibilis) címből: a záró "/v1" (és "/")
// levágva — "http://localhost:1234/v1" → "http://localhost:1234".
QString nativeApiRoot(const QString& baseUrl);
// A GET /api/v1/models válasz értelmezése a beállított modellre. Nem értelmezhető törzs →
// Kind::Unknown.
LlmServerInfo parseLmStudioModels(const QByteArray& json, const QString& model);

// A betöltés előtti döntés.
struct PreloadDecision {
    enum class Action {
        Skip,      // nincs mit tenni / nem tudunk (nem LM Studio, a modell nincs a listán)
        None,      // a betöltött példány megfelel
        Load,      // nincs betöltve → betöltés
        Reload,    // be van töltve, de kicsi a kontextus vagy parallel > 1 → ki + be
    };
    Action action = Action::Skip;
    int contextLength = 0;      // a kérendő kontextus (Load / Reload)
    QStringList unloadIds;      // Reload: a beállított modell lecserélendő példányai
    // A kérendő kontextus kisebb a becsült igénynél (rögzített beállítás vagy a modell
    // maximuma miatt) — a futás ettől még indul; az összefoglaló kisebb részekre bont.
    bool contextInsufficient = false;
    QString reason;             // gépi ok: not-lmstudio | model-unlisted | ok | not-loaded |
                                //          context-small | parallel
};
// need: a feladat becsült igénye; setting: ProviderConfig::contextLength (0 = automatikus);
// floor: a felhasználó által kért minimum („Betöltés nagyobb kontextussal”), 0 = nincs.
PreloadDecision decidePreload(const LlmServerInfo& info, int need, int setting, int floor = 0);

} // namespace llmctx
} // namespace tanara

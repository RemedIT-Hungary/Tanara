#pragma once
//
// Bukott háttér-feladat → megjeleníthető hiba (emberi magyarázat + technikai sor).
// Tiszta függvények (nincs állapot, nincs I/O) — az AppController a szolgáltató utolsó
// sikertelen HTTP-válaszából (ProviderConfig::onExchange) és a nyers hibaszövegből hívja.
//
#include "tanara/jobs/JobTypes.h"
#include "tanara/llm/LlmContext.h"

namespace tanara {

// Egy sikertelen HTTP-váltás technikai sora: „HTTP 401 · invalid_api_key · <üzenet>”.
// Hálózati hibánál (nincs HTTP-válasz): „hálózat · <Qt hibaszöveg>”. A törzsből a Soniox
// ({error_type, message}), az OpenAI-kompatibilis ({error:{code|type, message}}) és a
// Tanara-gateway ({error:{code, message, request_id}}) alakot is felismeri.
QString httpFailureDetail(const HttpExchange& ex);

// A nyers hibaszövegből és (ha van) az utolsó sikertelen HTTP-váltásból megjeleníthető hiba.
//  - message: emberi magyarázat a státuszkód alapján (401/403 kulcs, 402 egyenleg, 429 keret,
//    5xx szolgáltató-hiba, hálózati hiba …); ismeretlen esetben a nyers szöveg.
//  - detail: httpFailureDetail(), vagy — HTTP-váltás nélkül — üres (ha a message már a nyers
//    szöveg), ill. a nyers szöveg (ha a message általános).
//  - fixActionHint: "settings:stt" / "settings:llm" kulcs- és címhibáknál, különben üres.
// A „nem fér a modell kontextusába” hiba javításához ismert körülmények (az AppController
// tölti a feladat becsült igényéből és a szerver-próbából).
struct ContextFailureHint {
    int  recommendedContext = -1;   // a feladathoz ajánlott kontextus (token); -1 = nem tudjuk
    bool lmStudio = false;          // a szerver LM Studio → a Tanara maga újratöltheti a modellt
    bool cloud = false;             // Tanara Cloud: nincs mit betölteni, csak a magyarázat
};

// A kontextus-hiba javító tippjei (JobError::fixActionHint):
//  "llm:reload-context:<N>"   — LM Studio: újratöltés legalább N tokenes kontextussal + újra,
//  "settings:llm-context:<N>" — más szerver: a modellt ott kell nagyobb kontextussal betölteni
//                               (a Beállítások LLM-kártyája); N lehet hiányzó.
// A tippből a token-szám (0, ha nincs), ill. hogy újratöltés-e.
int contextFixTokens(const QString& fixActionHint);
bool isReloadContextHint(const QString& fixActionHint);

JobError describeJobFailure(JobKind kind, const QString& rawMessage,
                            const HttpExchange* failedExchange = nullptr,
                            const ContextFailureHint* context = nullptr);

// A kontextus-túllépés emberi leírása: „A modell 4096 tokenes kontextussal van betöltve, a
// kérés 6042 token volt — nem fér bele.” + mit tegyen a felhasználó; a technikai sor JSON
// nélkül („HTTP 400 · exceed_context_size_error · kérés 6042 token · kontextus 4096 token”).
// httpStatus: 0, ha nincs HTTP-válasz-adat.
JobError describeContextOverflow(JobKind kind, const llmctx::ContextOverflow& overflow,
                                 const ContextFailureHint& hint, int httpStatus = 0);

// Tanara Cloud strukturált hibából (a gateway üzenete már a felhasználó nyelvén van).
JobError describeCloudFailure(JobKind kind, const CloudError& error);

} // namespace tanara

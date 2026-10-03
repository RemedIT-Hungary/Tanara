#pragma once
//
// Bukott háttér-feladat → megjeleníthető hiba (emberi magyarázat + technikai sor).
// Tiszta függvények (nincs állapot, nincs I/O) — az AppController a szolgáltató utolsó
// sikertelen HTTP-válaszából (ProviderConfig::onExchange) és a nyers hibaszövegből hívja.
//
#include "tanara/jobs/JobTypes.h"

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
JobError describeJobFailure(JobKind kind, const QString& rawMessage,
                            const HttpExchange* failedExchange = nullptr);

// Tanara Cloud strukturált hibából (a gateway üzenete már a felhasználó nyelvén van).
JobError describeCloudFailure(JobKind kind, const CloudError& error);

} // namespace tanara

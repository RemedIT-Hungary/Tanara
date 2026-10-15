#pragma once
//
// Per-capability processing route: WHERE a capability runs (S3). One route per capability, persisted as
// the "routes" settings section (app::AppSettings):
//
//   "routes": { "stt":         { "route": "ask",      "local": "whisper-compat", "external": "soniox" },
//               "diarization": { "route": "local" },
//               "voice":       { "route": "local" },
//               "llm":         { "route": "local",    "local": "openai-compat" },
//               "embedding":   { "route": "basic",    "local": "openai-compat-embedding" } }
//
//  - Stt / Llm / Embedding: Local and External both run the capability's selected provider
//    (ProviderSettings::<cap>.selectedId); the provider's KIND (providerKindOf) decides which segment it
//    belongs to. "local"/"external" remember the last provider of that kind, so switching back restores it.
//    For Stt / Llm the selected id is ALWAYS an own (non-cloud) provider: Cloud is the route, not the id.
//    Embedding has a distinct cloud provider id and no Ask: its route follows its selected id
//    ("" = Basic, the local endpoint = Local, the Tanara Cloud endpoint = Cloud).
//  - Diarization: Local = the local diarizer (pyannote-like models in Tanara), External = the STT provider
//    separates the speakers (Soniox). Cloud and Ask are not offered yet.
//  - Voice: Local only (the voiceprint never leaves the machine).
//  - Ask: decided per run (app::Workflows' askRoute hook); allowed only when both a non-cloud provider and
//    the live Tanara Cloud exist for the capability.
#include "tanara/providers/core/ProviderDescriptor.h"
#include "tanara/providers/core/ProviderSettings.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace tanara::providers {

enum class Capability { Stt, Diarization, Voice, Llm, Embedding };
enum class Route { Local, External, Cloud, Ask, Basic };   // Basic: embedding only (no embedding provider)

// The provider id of the Tanara Cloud STT + chat adapters (equal to cloud::ProviderId).
inline const QString CloudProviderId = QStringLiteral("tanara-cloud");

// Stable machine names ("stt", "diarization", "voice", "llm", "embedding"; "local", "external", "cloud",
// "ask", "basic"): JSON, the GUI, the CLI.
QString capabilityKey(Capability cap);
std::optional<Capability> capabilityFromKey(const QString& key);
QString routeKey(Route route);
std::optional<Route> routeFromKey(const QString& key);
QString providerKindKey(ProviderKind kind);
QList<Capability> allCapabilities();

// The kind of a built-in provider id (the same value its descriptor carries): the Tanara Cloud ids ->
// Cloud, Soniox -> External, everything else (whisper-compat, openai-compat, the local embedding
// endpoint, unknown ids) -> Local.
ProviderKind providerKindOf(const QString& providerId);
// The non-cloud route of a provider kind (Cloud -> nullopt).
std::optional<Route> routeOfKind(ProviderKind kind);

// One capability's route + the remembered provider per kind.
struct CapabilityRoute {
    Route   route = Route::Local;
    QString lastLocalId;
    QString lastExternalId;

    QJsonObject toJson() const;
    static CapabilityRoute fromJson(const QJsonObject& o, Route fallback);
    bool operator==(const CapabilityRoute&) const = default;
};

// What a route can be set to for one capability (built from the provider registry + the cloud mode).
struct RouteContext {
    QStringList localIds;       // registered providers of kind Local (sorted)
    QStringList externalIds;    // registered providers of kind External (sorted)
    QString     cloudId;        // the capability's Tanara Cloud provider id ("" = none registered)
    bool        cloudLive = false;
    bool hasCloud() const { return cloudLive && !cloudId.isEmpty(); }
};

// Why a route is not allowed (empty = allowed): "voice_local_only" | "embedding_no_ask" |
// "not_embedding" (Basic outside embedding) | "no_local" | "no_external" | "cloud_unavailable" |
// "ask_unavailable" (needs both a non-cloud provider and the live cloud) | "diarization_later".
QString routeBlocker(Capability cap, Route route, const RouteContext& ctx);

struct RouteSettings {
    CapabilityRoute stt         {Route::Local};
    CapabilityRoute diarization {Route::External};
    CapabilityRoute voice       {Route::Local};
    CapabilityRoute llm         {Route::Local};
    CapabilityRoute embedding   {Route::Basic};
    // false: the section was missing from settings.json (written before S3) -> app::AppSettings converts.
    bool present = true;

    CapabilityRoute& at(Capability cap);
    const CapabilityRoute& at(Capability cap) const;
    Route route(Capability cap) const { return at(cap).route; }

    // The non-cloud route the capability falls back to (Ask unanswered, Cloud not live): the selected
    // provider's kind (Stt / Llm), External for diarization, Local for voice, Basic / Local for embedding.
    Route fallbackRoute(Capability cap, const ProviderSettings& providers) const;
    // The provider id that runs the capability on a (resolved) route: Cloud -> the cloud id, Local /
    // External -> the selected provider if it is of that kind, else the remembered one ("" = none).
    QString providerFor(Capability cap, Route resolved, const ProviderSettings& providers) const;
    // A copy of the provider settings whose stt / llm selection is the provider of the given resolved routes
    // (the readiness check and the provider factories read the selected id).
    ProviderSettings effective(const ProviderSettings& providers, Route stt, Route llm) const;

    // Sets a route after routeBlocker() allowed it; keeps the provider selection consistent (Local /
    // External select the remembered provider of that kind, else the first registered one; Cloud and Ask
    // keep the own provider). false (nothing changes): not allowed.
    bool set(Capability cap, Route route, const RouteContext& ctx, ProviderSettings& providers);

    // Brings the routes in line with the provider selection (after a provider was picked directly, or a
    // cloud id was written into a selected id the pre-S3 way): a cloud id selected for stt / llm becomes
    // route Cloud with the remembered own provider selected; a Local / External route follows the selected
    // provider's kind; the remembered ids are updated; embedding follows its selected id; diarization keeps
    // Local / External, voice is Local.
    void normalize(ProviderSettings& providers);

    // The routes of settings written before S3 (or a fresh install): stt / llm from the selected
    // provider's kind (a selected cloud id -> Cloud), diarization Local iff the old
    // processing.localDiarization switch was on, voice Local, embedding from its selected id (none ->
    // Basic). cloudLive: stt / llm that are not on Cloud default to Ask (Tanara Cloud and an own provider
    // are both there). The providers' cloud ids are replaced by the own provider (see normalize).
    static RouteSettings derive(ProviderSettings& providers, bool localDiarization, bool cloudLive);

    QJsonObject toJson() const;
    static RouteSettings fromJson(const QJsonObject& o);
};

} // namespace tanara::providers

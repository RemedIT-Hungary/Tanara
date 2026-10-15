#include "tanara/providers/core/Routes.h"
#include "tanara/providers/embedding/ITextEmbedder.h"

namespace tanara::providers {

namespace {

bool hasOwnProvider(Capability cap)
{
    return cap == Capability::Stt || cap == Capability::Llm || cap == Capability::Embedding;
}

CapabilityProviders* providersOf(Capability cap, ProviderSettings& p)
{
    switch (cap) {
    case Capability::Stt:       return &p.stt;
    case Capability::Llm:       return &p.llm;
    case Capability::Embedding: return &p.embedding;
    default:                    return nullptr;
    }
}

const CapabilityProviders* providersOf(Capability cap, const ProviderSettings& p)
{
    return providersOf(cap, const_cast<ProviderSettings&>(p));
}

// The capability's own (non-cloud) provider for a selected cloud id: the remembered one, else the first
// non-cloud config, else the built-in default.
QString ownProviderFor(Capability cap, const CapabilityRoute& r, const CapabilityProviders& p)
{
    if (!r.lastExternalId.isEmpty()) return r.lastExternalId;
    if (!r.lastLocalId.isEmpty()) return r.lastLocalId;
    for (auto it = p.configs.constBegin(); it != p.configs.constEnd(); ++it)
        if (providerKindOf(it.key()) != ProviderKind::Cloud) return it.key();
    return cap == Capability::Stt ? QStringLiteral("soniox") : QStringLiteral("openai-compat");
}

void remember(CapabilityRoute& r, const QString& id)
{
    if (id.isEmpty()) return;
    const ProviderKind k = providerKindOf(id);
    if (k == ProviderKind::Local) r.lastLocalId = id;
    else if (k == ProviderKind::External) r.lastExternalId = id;
}

} // namespace

QString capabilityKey(Capability cap)
{
    switch (cap) {
    case Capability::Stt:         return QStringLiteral("stt");
    case Capability::Diarization: return QStringLiteral("diarization");
    case Capability::Voice:       return QStringLiteral("voice");
    case Capability::Llm:         return QStringLiteral("llm");
    case Capability::Embedding:   return QStringLiteral("embedding");
    }
    return {};
}

std::optional<Capability> capabilityFromKey(const QString& key)
{
    for (Capability c : allCapabilities())
        if (capabilityKey(c) == key) return c;
    return std::nullopt;
}

QString routeKey(Route route)
{
    switch (route) {
    case Route::Local:    return QStringLiteral("local");
    case Route::External: return QStringLiteral("external");
    case Route::Cloud:    return QStringLiteral("cloud");
    case Route::Ask:      return QStringLiteral("ask");
    case Route::Basic:    return QStringLiteral("basic");
    }
    return {};
}

std::optional<Route> routeFromKey(const QString& key)
{
    for (Route r : {Route::Local, Route::External, Route::Cloud, Route::Ask, Route::Basic})
        if (routeKey(r) == key) return r;
    return std::nullopt;
}

QString providerKindKey(ProviderKind kind)
{
    switch (kind) {
    case ProviderKind::Local:    return QStringLiteral("local");
    case ProviderKind::External: return QStringLiteral("external");
    case ProviderKind::Cloud:    return QStringLiteral("cloud");
    }
    return {};
}

QList<Capability> allCapabilities()
{
    return {Capability::Stt, Capability::Diarization, Capability::Voice, Capability::Llm, Capability::Embedding};
}

ProviderKind providerKindOf(const QString& providerId)
{
    if (providerId == CloudProviderId || providerId == embeddingproviders::CloudId) return ProviderKind::Cloud;
    if (providerId == QLatin1String("soniox")) return ProviderKind::External;
    return ProviderKind::Local;
}

std::optional<Route> routeOfKind(ProviderKind kind)
{
    if (kind == ProviderKind::Local) return Route::Local;
    if (kind == ProviderKind::External) return Route::External;
    return std::nullopt;
}

// ---- CapabilityRoute -------------------------------------------------------------------------------------

QJsonObject CapabilityRoute::toJson() const
{
    QJsonObject o{{QStringLiteral("route"), routeKey(route)}};
    if (!lastLocalId.isEmpty()) o.insert(QStringLiteral("local"), lastLocalId);
    if (!lastExternalId.isEmpty()) o.insert(QStringLiteral("external"), lastExternalId);
    return o;
}

CapabilityRoute CapabilityRoute::fromJson(const QJsonObject& o, Route fallback)
{
    CapabilityRoute r;
    r.route = routeFromKey(o.value(QStringLiteral("route")).toString()).value_or(fallback);
    r.lastLocalId = o.value(QStringLiteral("local")).toString();
    r.lastExternalId = o.value(QStringLiteral("external")).toString();
    return r;
}

// ---- rules -----------------------------------------------------------------------------------------------

QString routeBlocker(Capability cap, Route route, const RouteContext& ctx)
{
    if (cap == Capability::Voice)
        return route == Route::Local ? QString() : QStringLiteral("voice_local_only");
    if (route == Route::Basic)
        return cap == Capability::Embedding ? QString() : QStringLiteral("not_embedding");
    if (cap == Capability::Embedding && route == Route::Ask) return QStringLiteral("embedding_no_ask");
    if (cap == Capability::Diarization)
        return route == Route::Local || route == Route::External ? QString() : QStringLiteral("diarization_later");
    switch (route) {
    case Route::Local:    return ctx.localIds.isEmpty() ? QStringLiteral("no_local") : QString();
    case Route::External: return ctx.externalIds.isEmpty() ? QStringLiteral("no_external") : QString();
    case Route::Cloud:    return ctx.hasCloud() ? QString() : QStringLiteral("cloud_unavailable");
    case Route::Ask:
        return ctx.hasCloud() && !(ctx.localIds.isEmpty() && ctx.externalIds.isEmpty())
            ? QString() : QStringLiteral("ask_unavailable");
    case Route::Basic:    break;
    }
    return {};
}

// ---- RouteSettings ---------------------------------------------------------------------------------------

CapabilityRoute& RouteSettings::at(Capability cap)
{
    switch (cap) {
    case Capability::Stt:         return stt;
    case Capability::Diarization: return diarization;
    case Capability::Voice:       return voice;
    case Capability::Llm:         return llm;
    case Capability::Embedding:   return embedding;
    }
    return stt;
}

const CapabilityRoute& RouteSettings::at(Capability cap) const
{
    return const_cast<RouteSettings*>(this)->at(cap);
}

Route RouteSettings::fallbackRoute(Capability cap, const ProviderSettings& providers) const
{
    switch (cap) {
    case Capability::Diarization: return route(cap) == Route::Local ? Route::Local : Route::External;
    case Capability::Voice:       return Route::Local;
    case Capability::Embedding:
        return providers.embedding.selectedId.isEmpty() ? Route::Basic
             : routeOfKind(providerKindOf(providers.embedding.selectedId)).value_or(Route::Basic);
    default: break;
    }
    const CapabilityProviders* p = providersOf(cap, providers);
    return routeOfKind(providerKindOf(p->selectedId)).value_or(Route::Local);
}

QString RouteSettings::providerFor(Capability cap, Route resolved, const ProviderSettings& providers) const
{
    if (!hasOwnProvider(cap)) return {};
    const CapabilityProviders* p = providersOf(cap, providers);
    const CapabilityRoute& r = at(cap);
    if (resolved == Route::Ask) resolved = fallbackRoute(cap, providers);
    switch (resolved) {
    case Route::Cloud:
        return cap == Capability::Embedding ? embeddingproviders::CloudId : CloudProviderId;
    case Route::Local:
        return providerKindOf(p->selectedId) == ProviderKind::Local && !p->selectedId.isEmpty()
            ? p->selectedId : r.lastLocalId;
    case Route::External:
        return providerKindOf(p->selectedId) == ProviderKind::External ? p->selectedId : r.lastExternalId;
    case Route::Basic:
    case Route::Ask:
        break;
    }
    return {};
}

ProviderSettings RouteSettings::effective(const ProviderSettings& providers, Route sttRoute, Route llmRoute) const
{
    ProviderSettings out = providers;
    // The provider of the resolved route, or EMPTY when that kind has no provider at all (never set up):
    // an empty selection is refused by the readiness check. Keeping the other kind's provider here would
    // silently run a Local choice on the External provider (found by S5).
    out.stt.selectedId = providerFor(Capability::Stt, sttRoute, providers);
    out.llm.selectedId = providerFor(Capability::Llm, llmRoute, providers);
    return out;
}

bool RouteSettings::set(Capability cap, Route r, const RouteContext& ctx, ProviderSettings& providers)
{
    if (!routeBlocker(cap, r, ctx).isEmpty()) return false;
    CapabilityRoute& cr = at(cap);
    CapabilityProviders* p = providersOf(cap, providers);
    if (p && (r == Route::Local || r == Route::External)) {
        const QStringList& ids = r == Route::Local ? ctx.localIds : ctx.externalIds;
        QString id = providerFor(cap, r, providers);
        if (id.isEmpty() || (!ids.isEmpty() && !ids.contains(id))) id = ids.value(0);
        if (id.isEmpty()) return false;
        p->selectedId = id;
        remember(cr, id);
    } else if (p && cap == Capability::Embedding) {
        if (r == Route::Basic) p->selectedId.clear();
        else if (r == Route::Cloud) p->selectedId = ctx.cloudId;
    }
    cr.route = r;
    return true;
}

void RouteSettings::normalize(ProviderSettings& providers)
{
    for (Capability cap : {Capability::Stt, Capability::Llm}) {
        CapabilityRoute& r = at(cap);
        CapabilityProviders& p = *providersOf(cap, providers);
        if (providerKindOf(p.selectedId) == ProviderKind::Cloud) {
            // The pre-S3 way of choosing the cloud: the id. Now the route says it; the own provider stays selected.
            r.route = Route::Cloud;
            p.selectedId = ownProviderFor(cap, r, p);
            if (!p.configs.contains(p.selectedId)) p.configs[p.selectedId].type = p.selectedId;
        }
        remember(r, p.selectedId);
        const Route kindRoute = routeOfKind(providerKindOf(p.selectedId)).value_or(Route::Local);
        if (r.route == Route::Local || r.route == Route::External || r.route == Route::Basic) r.route = kindRoute;
    }
    {
        CapabilityRoute& r = embedding;
        remember(r, providers.embedding.selectedId);
        r.route = fallbackRoute(Capability::Embedding, providers);
        if (providerKindOf(providers.embedding.selectedId) == ProviderKind::Cloud && !providers.embedding.selectedId.isEmpty())
            r.route = Route::Cloud;
    }
    if (diarization.route != Route::Local) diarization.route = Route::External;
    voice.route = Route::Local;
}

RouteSettings RouteSettings::derive(ProviderSettings& providers, bool localDiarization, bool cloudLive)
{
    RouteSettings r;
    r.stt.route = Route::External;   // placeholders: normalize() follows the selected providers' kinds
    r.llm.route = Route::Local;
    r.diarization.route = localDiarization ? Route::Local : Route::External;
    r.normalize(providers);
    if (cloudLive)
        for (Capability cap : {Capability::Stt, Capability::Llm})
            if (r.at(cap).route != Route::Cloud) r.at(cap).route = Route::Ask;
    return r;
}

QJsonObject RouteSettings::toJson() const
{
    QJsonObject o;
    for (Capability cap : allCapabilities()) o.insert(capabilityKey(cap), at(cap).toJson());
    return o;
}

RouteSettings RouteSettings::fromJson(const QJsonObject& o)
{
    RouteSettings s;
    s.present = !o.isEmpty();
    for (Capability cap : allCapabilities())
        s.at(cap) = CapabilityRoute::fromJson(o.value(capabilityKey(cap)).toObject(), s.at(cap).route);
    return s;
}

} // namespace tanara::providers

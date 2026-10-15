#include "tanara/app/RouteChoices.h"

#include "tanara/library/jobs/JobStats.h"
#include "tanara/providers/core/ProviderRegistry.h"

#include <QCoreApplication>

namespace tanara::app {

namespace {

providers::CapabilityKind kindOf(providers::Capability cap)
{
    switch (cap) {
    case providers::Capability::Stt:       return providers::CapabilityKind::Transcription;
    case providers::Capability::Llm:       return providers::CapabilityKind::Chat;
    case providers::Capability::Embedding: return providers::CapabilityKind::TextEmbedding;
    case providers::Capability::Voice:     return providers::CapabilityKind::VoiceEmbedding;
    case providers::Capability::Diarization: break;
    }
    return providers::CapabilityKind::Diarization;
}

// "Whisper (OpenAI-kompatibilis)" -> "Whisper": the menu row is short; the details live in the Settings.
QString shortProviderName(const QString& name)
{
    const int paren = name.indexOf(QLatin1String(" ("));
    return paren > 0 ? name.left(paren) : name;
}

} // namespace

QString cloudTierName(const QString& tier)
{
    return tier == QLatin1String("fast") ? QCoreApplication::translate("tanara::app::RouteChoices", "Gyors")
                                         : QCoreApplication::translate("tanara::app::RouteChoices", "Pontos");
}

providers::RouteContext routeContextOf(providers::Capability cap, const providers::ProviderRegistry& registry,
                                       bool cloudLive)
{
    providers::RouteContext ctx;
    ctx.cloudLive = cloudLive;
    for (const providers::ProviderDescriptor& d : registry.descriptors(kindOf(cap))) {
        if (d.kind == providers::ProviderKind::Local) ctx.localIds << d.id;
        else if (d.kind == providers::ProviderKind::External) ctx.externalIds << d.id;
        else ctx.cloudId = d.id;
    }
    // The cloud adapters are registered only in live mode; teaser builds still show the "coming soon" row.
    if (ctx.cloudId.isEmpty() && (cap == providers::Capability::Stt || cap == providers::Capability::Llm))
        ctx.cloudId = providers::CloudProviderId;
    ctx.localIds.sort();
    ctx.externalIds.sort();
    return ctx;
}

library::RouteChoices buildRouteChoices(providers::Capability cap, const RouteChoiceInputs& in)
{
    library::RouteChoices out;
    if (!in.registry || (cap != providers::Capability::Stt && cap != providers::Capability::Llm))
        return out;
    out.available = true;
    out.cloudLive = in.cloudLive;
    out.confirmCloud = in.settings.cloud.estimateBeforeRun;
    providers::ProviderSettings providers = in.settings.providers;
    providers::RouteSettings routes = in.settings.routes;
    routes.normalize(providers);
    out.stored = routes.route(cap);
    const providers::RouteContext ctx = routeContextOf(cap, *in.registry, in.cloudLive);
    const providers::WorkflowStep step = cap == providers::Capability::Stt ? providers::WorkflowStep::Transcribe
                                                                            : providers::WorkflowStep::Summarize;
    const QString tier = cap == providers::Capability::Stt ? in.settings.cloud.sttTier : in.settings.cloud.llmTier;

    for (providers::Route r : {providers::Route::Local, providers::Route::External, providers::Route::Cloud}) {
        library::RouteChoiceRow row;
        row.route = r;
        row.blocker = providers::routeBlocker(cap, r, ctx);
        // A capability without a provider of that kind has no row (e.g. no external LLM provider).
        if (row.blocker == QLatin1String("no_local") || row.blocker == QLatin1String("no_external"))
            continue;
        if (r == providers::Route::Cloud) {
            row.provider = cloudTierName(tier);
        } else {
            QString id = routes.providerFor(cap, r, providers);
            // No provider of this kind was ever set up: the row names the first registered one (the one a
            // default write would select) but cannot run (a run on it would keep the other provider).
            const bool unset = id.isEmpty();
            if (unset) id = (r == providers::Route::Local ? ctx.localIds : ctx.externalIds).value(0);
            row.provider = shortProviderName(in.registry->descriptor(kindOf(cap), id).displayName);
            if (row.provider.isEmpty()) row.provider = id;
            if (unset && row.blocker.isEmpty()) {
                row.blocker = QStringLiteral("not_configured");
                row.detail = QCoreApplication::translate("tanara::app::RouteChoices",
                                                         "Még nincs beállítva a Beállításokban.");
            }
            // The transcription's speed of earlier runs with the same provider / model (TranscriptionService's key).
            const providers::CapabilityProviders& cp = cap == providers::Capability::Stt ? providers.stt : providers.llm;
            if (cap == providers::Capability::Stt && in.stats && in.meeting.durationMs > 0)
                row.etaSeconds = in.stats->estimateSec(QStringLiteral("transcribe/%1/%2").arg(id, cp.configs.value(id).model),
                                                       double(in.meeting.durationMs) / 1000.0);
        }
        // An allowed route whose provider misses a setting / key / sign-in: not runnable now (the readiness text).
        if (row.blocker.isEmpty() && !in.meeting.id.isEmpty()) {
            const providers::Route sttR = cap == providers::Capability::Stt ? r : routes.fallbackRoute(providers::Capability::Stt, providers);
            const providers::Route llmR = cap == providers::Capability::Llm ? r : routes.fallbackRoute(providers::Capability::Llm, providers);
            const providers::ReadinessModel model(routes.effective(providers, sttR, llmR), in.hasSecret, in.registry);
            const providers::ReadinessResult res = model.check(step, in.meeting);
            if (!res.runnable && res.blockerKind != providers::BlockerKind::MeetingState) {
                row.blocker = QStringLiteral("not_configured");
                row.detail = res.detail;
            }
        }
        out.rows << row;
    }
    return out;
}

} // namespace tanara::app

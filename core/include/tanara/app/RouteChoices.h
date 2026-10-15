#pragma once
//
// app::buildRouteChoices (S5): the routes a run of a capability (stt / llm) can take on a meeting, for the
// split button's menu (library::RouteChoices): one row per route the capability offers (Local, External,
// Cloud), each with its provider's name, its blocker (providers::routeBlocker, or "not_configured" with the
// readiness text when the route's provider misses a setting / key) and the run's estimated length from the
// earlier runs (library::JobStats; the transcription's key, -1 when unknown).
//
// Pure over its inputs (no controller): the GUI's real facade gathers them.
#include "tanara/app/AppSettings.h"
#include "tanara/domain/Meeting.h"
#include "tanara/library/IOverviewFacade.h"
#include "tanara/providers/core/ReadinessModel.h"
#include "tanara/providers/core/Routes.h"

namespace tanara::providers { class ProviderRegistry; }
namespace tanara::library { class JobStats; }

namespace tanara::app {

struct RouteChoiceInputs {
    AppSettings settings;
    const providers::ProviderRegistry* registry = nullptr;
    providers::ReadinessModel::SecretProbe hasSecret;
    const library::JobStats* stats = nullptr;   // may be null: no time estimate
    Meeting meeting;                            // the meeting of the run (its length: the estimate)
    bool cloudLive = false;
};

// What a capability's route can be set to (the registry's providers by kind + the cloud mode).
providers::RouteContext routeContextOf(providers::Capability cap, const providers::ProviderRegistry& registry,
                                       bool cloudLive);

library::RouteChoices buildRouteChoices(providers::Capability cap, const RouteChoiceInputs& in);

// "fast" -> "Gyors", "accurate" -> "Pontos" (the Tanara Cloud tier's display name).
QString cloudTierName(const QString& tier);

} // namespace tanara::app

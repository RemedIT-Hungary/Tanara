#include "tanara/cloud/CloudMode.h"
#include "tanara/cloud/CloudTypes.h"

namespace tanara::cloud {

CloudMode CloudMode::resolve(const CloudSettings& s, const QString& envMode, const QString& envUrl,
                             bool clientCompiled, bool teaserCompiled)
{
    const QString mode = envMode.trimmed().toLower();
    const bool off = mode == QLatin1String("off");
    const bool wantLive = mode == QLatin1String("live") || (mode.isEmpty() && s.enabled);
    CloudMode m;
    m.live   = !off && clientCompiled && wantLive;
    m.teaser = !off && !m.live && teaserCompiled;
    const QString url = envUrl.trimmed();
    m.baseUrl = !url.isEmpty() ? url : s.baseUrl;
    return m;
}

CloudMode CloudMode::fromEnvironment(const CloudSettings& s)
{
    return resolve(s, qEnvironmentVariable("TANARA_CLOUD"), qEnvironmentVariable("TANARA_CLOUD_URL"),
                   clientCompiled(), teaserCompiled());
}

bool CloudMode::selects(providers::WorkflowStep step, const providers::RouteSettings& routes) const
{
    if (step == providers::WorkflowStep::Transcribe) return selects(routes.route(providers::Capability::Stt));
    if (step == providers::WorkflowStep::Summarize)  return selects(routes.route(providers::Capability::Llm));
    return false;
}

} // namespace tanara::cloud

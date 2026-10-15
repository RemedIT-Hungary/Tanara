#include "tanara/app/AppSettings.h"

namespace tanara::app {

QJsonObject UiSettings::toJson() const
{
    return QJsonObject{{QStringLiteral("language"), language},
                       {QStringLiteral("onboardingDone"), onboardingDone},
                       {QStringLiteral("reviewAutoPlay"), reviewAutoPlay}};
}

UiSettings UiSettings::fromJson(const QJsonObject& o)
{
    UiSettings s;
    s.language = o.value(QStringLiteral("language")).toString(s.language);
    s.onboardingDone = o.value(QStringLiteral("onboardingDone")).toBool(s.onboardingDone);
    s.reviewAutoPlay = o.value(QStringLiteral("reviewAutoPlay")).toBool(s.reviewAutoPlay);
    return s;
}

QStringList AppSettings::sectionNames()
{
    return {QStringLiteral("paths"), QStringLiteral("recording"), QStringLiteral("media"), QStringLiteral("watcher"),
            QStringLiteral("processing"), QStringLiteral("providers"), QStringLiteral("routes"), QStringLiteral("cloud"),
            QStringLiteral("ui"), QStringLiteral("secrets")};
}

QJsonObject AppSettings::sectionJson(const QString& name) const
{
    if (name == QLatin1String("paths"))      return paths.toJson();
    if (name == QLatin1String("recording"))  return recording.toJson();
    if (name == QLatin1String("media"))      return media.toJson();
    if (name == QLatin1String("watcher"))    return watcher.toJson();
    if (name == QLatin1String("processing")) return processing.toJson();
    if (name == QLatin1String("providers"))  return providers.toJson();
    if (name == QLatin1String("routes"))     return routes.toJson();
    if (name == QLatin1String("cloud"))      return cloud.toJson();
    if (name == QLatin1String("ui"))         return ui.toJson();
    if (name == QLatin1String("secrets"))    return secrets.toJson();
    return {};
}

void AppSettings::setSectionJson(const QString& name, const QJsonObject& o)
{
    if (name == QLatin1String("paths"))           paths = paths::PathSettings::fromJson(o);
    else if (name == QLatin1String("recording"))  recording = recording::RecordingSettings::fromJson(o);
    else if (name == QLatin1String("media"))      media = media::MediaSettings::fromJson(o);
    else if (name == QLatin1String("watcher"))    watcher = watcher::WatcherSettings::fromJson(o);
    else if (name == QLatin1String("processing")) processing = processing::ProcessingSettings::fromJson(o);
    else if (name == QLatin1String("providers"))  providers = providers::ProviderSettings::fromJson(o);
    else if (name == QLatin1String("routes"))     routes = providers::RouteSettings::fromJson(o);
    else if (name == QLatin1String("cloud"))      cloud = cloud::CloudSettings::fromJson(o);
    else if (name == QLatin1String("ui"))         ui = UiSettings::fromJson(o);
    else if (name == QLatin1String("secrets"))    secrets = SecretSettings::fromJson(o);
}

} // namespace tanara::app

namespace tanara::app {

QJsonObject AppSettings::toJson() const
{
    QJsonObject o;
    for (const QString& name : sectionNames()) o.insert(name, sectionJson(name));
    return o;
}

AppSettings AppSettings::fromJson(const QJsonObject& o)
{
    AppSettings s;
    for (const QString& name : sectionNames()) s.setSectionJson(name, o.value(name).toObject());
    s.finishRoutes(o.value(QStringLiteral("processing")).toObject(), /*cloudLive*/ false);
    return s;
}

void AppSettings::finishRoutes(const QJsonObject& rawProcessing, bool cloudLive)
{
    if (!routes.present)
        routes = providers::RouteSettings::derive(
            providers, rawProcessing.value(QStringLiteral("localDiarization")).toBool(true), cloudLive);
    routes.normalize(providers);
}

} // namespace tanara::app

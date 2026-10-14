#include "TrackActivitySource.h"

#include "demo/TrackActivityDemo.h"

namespace tanara_qml {

TrackActivityDemoSource::TrackActivityDemoSource(const QString& scenario, QObject* parent)
    : TrackActivitySource(parent), m_loading(scenario == QLatin1String("loading"))
{
}

tanara::Meeting TrackActivityDemoSource::meeting(const QString&) const
{
    return tanara_demo::trackActivityMeeting();
}

tanara::MeetingActivity TrackActivityDemoSource::activity(const QString&)
{
    return m_loading ? tanara::MeetingActivity{} : tanara_demo::trackActivityFrames();
}

bool TrackActivityDemoSource::computing(const QString&) const
{
    return m_loading;
}

} // namespace tanara_qml

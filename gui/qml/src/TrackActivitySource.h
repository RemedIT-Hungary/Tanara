#pragma once
//
// TrackActivitySource: where TrackActivityModel gets a meeting's tracks and their speech-activity
// frames from. The real source wraps the core (AppContext.cpp, until the activity computation
// becomes a service of its own); the demo source serves the fictional frames of tanara_demo.
//
#include "tanara/domain/Activity.h"
#include "tanara/domain/Meeting.h"

#include <QObject>
#include <QString>

namespace tanara_qml {

class TrackActivitySource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // The meeting (its tracks and length); an empty id / unknown meeting -> an empty Meeting.
    virtual tanara::Meeting meeting(const QString& meetingId) const = 0;
    // The tracks' activity; computed in the background if it is not there yet (activityReady follows).
    virtual tanara::MeetingActivity activity(const QString& meetingId) = 0;
    virtual bool computing(const QString& meetingId) const = 0;

signals:
    void activityReady(const QString& meetingId);
    // A track was added / located / deleted: the fingerprint changes, the activity is recomputed.
    void tracksChanged(const QString& meetingId);
};

// The fictional source (tanara_demo's frames). scenario "loading": the activity is still being
// computed (no tracks, computing()); anything else: the finished sample.
class TrackActivityDemoSource : public TrackActivitySource {
    Q_OBJECT
public:
    explicit TrackActivityDemoSource(const QString& scenario, QObject* parent = nullptr);

    tanara::Meeting meeting(const QString& meetingId) const override;
    tanara::MeetingActivity activity(const QString& meetingId) override;
    bool computing(const QString& meetingId) const override;

private:
    bool m_loading;
};

} // namespace tanara_qml

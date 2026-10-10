#include "tanara/edit/TrackSpeech.h"

#include "tanara/audio/TrackCatalog.h"

#include <QHash>

#include <cmath>

namespace tanara::trackspeech {

bool isSpeechFrame(const TrackActivity& t, int frame)
{
    if (frame < 0 || frame >= t.dbAboveFloor.size()) return false;
    const float d = t.dbAboveFloor.at(frame);
    if (std::isnan(d)) return false;
    return d >= kSpeechAboveFloorDb && (t.floorDb + d) >= kSpeechMinAbsDb;
}

double speechRatio(const TrackActivity& t)
{
    int covered = 0, speech = 0;
    for (int i = 0; i < t.dbAboveFloor.size(); ++i) {
        if (std::isnan(t.dbAboveFloor.at(i))) continue;
        ++covered;
        if (isSpeechFrame(t, i)) ++speech;
    }
    return covered > 0 ? double(speech) / covered : -1.0;
}

bool needsSpeechCheck(const Meeting& m)
{
    for (const Track& t : m.tracks)
        if (t.included() && t.speechRatio < 0.0) return true;
    return false;
}

SpeechCheckResult applySpeechRatios(Meeting& m, const MeetingActivity& activity)
{
    SpeechCheckResult r;
    QHash<QString, double> ratioByLeader;
    for (const TrackActivity& ta : activity.tracks) ratioByLeader.insert(ta.trackId, speechRatio(ta));

    // Sávonként a logikai sáv (vezető szakasz) aránya.
    const QVector<tracknames::SegmentInfo> seg = tracknames::segments(m.tracks);
    QVector<double> ratio(m.tracks.size(), -1.0);
    for (int i = 0; i < m.tracks.size(); ++i) {
        const int lead = seg.value(i).leader >= 0 ? seg.value(i).leader : i;
        ratio[i] = ratioByLeader.value(m.tracks.at(lead).id, -1.0);
    }

    // Kiesne-e minden bevont sáv? Akkor senki sem esik ki.
    bool anySpeech = false;
    for (int i = 0; i < m.tracks.size(); ++i) {
        const Track& t = m.tracks.at(i);
        if (!t.included()) continue;
        const double v = t.speechRatio >= 0.0 ? t.speechRatio : ratio.at(i);
        if (v < 0.0 || v >= kMinSpeechRatio) anySpeech = true;
    }

    for (int i = 0; i < m.tracks.size(); ++i) {
        Track& t = m.tracks[i];
        if (!t.included() || t.speechRatio >= 0.0 || ratio.at(i) < 0.0) continue;
        t.speechRatio = ratio.at(i);
        r.measured << t.id;
        if (anySpeech && t.speechRatio < kMinSpeechRatio) {
            t.active = false;
            t.excludedReason = kNoSpeech;
            r.excluded << t.id;
        }
    }
    return r;
}

} // namespace tanara::trackspeech

#include "demo/TrackActivityDemo.h"

namespace tanara_demo {

namespace {
constexpr qint64 kDurationMs = (30 * 60 + 34) * 1000;
constexpr int kFrameMs = 500;
constexpr float kSpeechDb = 30.0f;   // a speaking frame (well above any speech threshold)
constexpr float kQuietDb = 0.0f;     // a quiet frame (below the activity threshold)
} // namespace

tanara::Meeting trackActivityMeeting()
{
    tanara::Meeting m;
    m.id = QStringLiteral("demo");
    m.durationMs = kDurationMs;
    for (const char* id : {"mic", "call", "system", "mic2"}) {
        tanara::Track t;
        t.id = QString::fromLatin1(id);
        m.tracks.append(t);
    }
    return m;
}

tanara::MeetingActivity trackActivityFrames()
{
    const int n = int(kDurationMs / kFrameMs);
    tanara::MeetingActivity act;
    // A deterministic xorshift speech pattern: talk = the share of time spoken.
    auto make = [&](const char* id, int seed, double talk) {
        tanara::TrackActivity t;
        t.trackId = QString::fromLatin1(id);
        t.frameMs = kFrameMs;
        t.originMs = 0;
        t.dbAboveFloor.resize(n);
        quint32 x = 2463534242u + quint32(seed) * 7919u;
        bool speaking = false;
        int left = 0;
        for (int i = 0; i < n; ++i) {
            if (left-- <= 0) {
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                speaking = talk > 0 && (x % 1000) / 1000.0 < talk;
                left = 4 + int((x >> 10) % (speaking ? 40 : 30));
            }
            t.dbAboveFloor[i] = speaking ? kSpeechDb : kQuietDb;
        }
        act.tracks.append(t);
    };
    make("mic", 1, 0.38);
    make("call", 2, 0.61);
    make("system", 3, 0.0);
    make("mic2", 4, 0.2);
    return act;
}

} // namespace tanara_demo

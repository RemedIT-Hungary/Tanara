#include "demo/TracksDemoFacade.h"

#include "demo/LibraryDemoData.h"

#include <cmath>

using namespace tanara;
using tanara::library::TrackListFixture;

namespace tanara_demo {

namespace {

// Fictional waveform levels (deterministic per seed).
QList<qreal> demoPeaks(int seed, qreal level)
{
    QList<qreal> out;
    quint32 x = 2463534242u + quint32(seed) * 7919u;
    qreal env = 0.4;
    for (int i = 0; i < 160; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        const qreal r = (x % 1000) / 1000.0;
        if (i % 7 == 0) env = 0.03 + 0.97 * std::pow(((x >> 8) % 1000) / 1000.0, 2.0);
        out.append(level * env * (0.35 + 0.65 * r));
    }
    return out;
}

} // namespace

TracksDemoFacade::TracksDemoFacade(const QString& designState, QObject* parent) : ITracksFacade(parent)
{
    const QString st = designState.isEmpty() ? QStringLiteral("default") : designState;
    const bool loading = st == QLatin1String("loading");
    // Overview: own mic + call + non-speech system audio; "overviewTwo": only the two speech tracks.
    const bool overview = st.startsWith(QLatin1String("overview"));
    m_fixture.meetingDurationMs = (30 * 60 + 34) * 1000;

    auto make = [&](const char* id, library::TrackRole role, const QString& name, const QString& raw,
                    const QString& file, bool active, bool missing, int color, qreal level) {
        TrackListFixture::Row r;
        r.view.track.id = QString::fromLatin1(id);
        r.view.track.file = file;
        r.view.track.active = active;
        r.view.role = role;
        r.view.displayName = r.view.friendlyName = name;
        r.view.rawDeviceName = raw;
        r.view.fileMissing = missing;
        r.view.absolutePath = QStringLiteral("/demo/") + file;
        r.colorIndex = color;
        if (missing) {
            r.peaksState = QStringLiteral("none");
        } else if (loading) {
            r.peaksState = QStringLiteral("loading");
        } else {
            r.levels = demoPeaks(color + 1, level);
            r.peaksState = QStringLiteral("ready");
        }
        return r;
    };
    QVector<TrackListFixture::Row>& rows = m_fixture.rows;
    rows << make("mic", library::TrackRole::OwnMic, tr("Saját mikrofon"),
                 QStringLiteral("Jabra Evolve2 65 · alsa_input.usb-GN_Netcom_Jabra_Evolve2_65-00.mono-fallback"),
                 QStringLiteral("track-01.ogg"), true, false, 0, 0.8)
         << make("call", library::TrackRole::CallAudio, tr("Hívás hangja"),
                 QStringLiteral("Microsoft Teams · Monitor of Built-in Audio Analog Stereo"),
                 QStringLiteral("track-02.ogg"), true, false, 1, 0.9);
    rows.last().view.track.startOffsetMs = 2 * 60 * 1000 + 30 * 1000;   // enabled later
    rows[0].view.track.kind = TrackKind::Mic;
    rows[1].view.track.kind = TrackKind::Loopback;
    if (overview) {
        const bool two = st == QLatin1String("overviewTwo");
        rows[0].view.track.speechRatio = two ? 0.44 : 0.38;
        rows[1].view.track.speechRatio = two ? 0.52 : 0.61;
        rows[1].view.track.startOffsetMs = 0;
        if (!two) {
            rows << make("system", library::TrackRole::SystemAudio, tr("Rendszerhang"),
                         QStringLiteral("Monitor of HDMI / DisplayPort 1 Output"),
                         QStringLiteral("track-03.ogg"), false, false, 2, 0.004);
            rows.last().view.track.kind = TrackKind::Loopback;
            rows.last().view.track.speechRatio = 0.0;
            rows.last().view.track.excludedReason = QStringLiteral("noSpeech");
        }
    } else if (st != QLatin1String("idle")) {
        rows << make("system", library::TrackRole::SystemAudio, tr("Rendszerhang"),
                     QStringLiteral("Monitor of HDMI / DisplayPort 1 Output"),
                     QStringLiteral("track-03.ogg"), false, false, 2, 0.004)
             << make("mic2", library::TrackRole::OwnMic, tr("Második mikrofon"),
                     QStringLiteral("USB PnP Audio Device"),
                     QStringLiteral("track-04.flac"), true, true, 3, 0.0);
    }

    if (st == QLatin1String("idle") || overview) {
        m_fixture.mixdownState = QStringLiteral("ready");
        m_fixture.mixdownLevels = demoPeaks(9, 0.85);
    } else if (loading) {
        m_fixture.mixdownState = QStringLiteral("stale");
        m_fixture.mixdownPeaksLoading = true;
    } else {
        m_fixture.mixdownState = QStringLiteral("running");
        m_fixture.mixdownPercent = 64;
        m_fixture.mixdownCancellable = true;
    }
}

TrackListFixture::Row* TracksDemoFacade::row(const QString& trackId)
{
    for (TrackListFixture::Row& r : m_fixture.rows)
        if (r.view.track.id == trackId) return &r;
    return nullptr;
}

Meeting TracksDemoFacade::meeting(const QString& meetingId) const
{
    // The player: the fictional library's meeting, with its length (no audio file).
    Meeting m;
    if (const DemoMeeting* d = find(meetingId)) {
        m.id = meetingId;
        m.durationMs = d->entry.durationMs;
    }
    return m;
}

bool TracksDemoFacade::renameTrack(const QString& meetingId, const QString& trackId, const QString& name)
{
    TrackListFixture::Row* r = row(trackId);
    if (!r) return false;
    const QString n = name.trimmed();
    r->view.renamed = !n.isEmpty() && n != r->view.friendlyName;
    r->view.displayName = r->view.renamed ? n : r->view.friendlyName;
    emit tracksChanged(meetingId);
    return true;
}

void TracksDemoFacade::restoreTrack(const QString& meetingId, const QString& trackId)
{
    if (TrackListFixture::Row* r = row(trackId)) {
        r->view.track.active = true;
        emit tracksChanged(meetingId);
    }
}

void TracksDemoFacade::setTrackIncluded(const QString& meetingId, const QString& trackId, bool included)
{
    if (TrackListFixture::Row* r = row(trackId)) {
        r->view.track.excludedReason = included ? QString() : QStringLiteral("manual");
        r->view.track.active = included || r->view.track.active;
        emit tracksChanged(meetingId);
    }
}

int TracksDemoFacade::deleteDroppedTracks(const QString& meetingId)
{
    const auto before = m_fixture.rows.size();
    m_fixture.rows.removeIf([](const TrackListFixture::Row& r) { return !r.view.track.active; });
    const int n = int(before - m_fixture.rows.size());
    if (n > 0) emit tracksChanged(meetingId);
    return n;
}

void TracksDemoFacade::deleteTrack(const QString& meetingId, const QString& trackId)
{
    const auto before = m_fixture.rows.size();
    m_fixture.rows.removeIf([&trackId](const TrackListFixture::Row& r) { return r.view.track.id == trackId; });
    if (m_fixture.rows.size() != before) emit tracksChanged(meetingId);
}

} // namespace tanara_demo

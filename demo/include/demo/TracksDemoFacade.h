#pragma once
//
// TracksDemoFacade: the fictional implementation of tanara::library::ITracksFacade for --demo,
// --gallery, --qml-shot and the tests. No audio file exists: the Tracks tab gets a ready-made track
// list of a design state (fixture()), and the player a meeting record of the fictional library
// (LibraryDemoData) with its length only. The track operations (rename, restore, include / exclude,
// delete) change the in-memory list and announce it (tracksChanged); the rest do nothing.
//
// Design states: "default" (the design sketch: the mixdown is running) | "loading" (computing
// peaks) | "idle" (all done) | "overview" / "overviewTwo" (Overview > Tracks: with / without the
// non-speech system audio).
#include "tanara/library/tracks/ITracksFacade.h"

namespace tanara_demo {

class TracksDemoFacade : public tanara::library::ITracksFacade {
    Q_OBJECT
public:
    explicit TracksDemoFacade(const QString& designState, QObject* parent = nullptr);

    bool live() const override { return false; }
    std::optional<tanara::library::TrackListFixture> fixture(const QString&) const override { return m_fixture; }

    tanara::Meeting meeting(const QString& meetingId) const override;
    tanara::library::MixdownJob mixdownJob(const QString&) const override { return {}; }
    void requestWaveforms(const QString&) override {}
    void cancelWaveforms(const QString&) override {}
    bool renameTrack(const QString& meetingId, const QString& trackId, const QString& name) override;
    void restoreTrack(const QString& meetingId, const QString& trackId) override;
    void setTrackIncluded(const QString& meetingId, const QString& trackId, bool included) override;
    bool relocateTrack(const QString&, const QString&, const QString&, QString*) override { return true; }
    int deleteDroppedTracks(const QString& meetingId) override;
    void deleteTrack(const QString& meetingId, const QString& trackId) override;
    void regenerateMixdown(const QString&) override {}

private:
    tanara::library::TrackListFixture::Row* row(const QString& trackId);

    tanara::library::TrackListFixture m_fixture;
};

} // namespace tanara_demo

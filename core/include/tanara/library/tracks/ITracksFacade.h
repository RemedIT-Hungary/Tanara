#pragma once
//
// ITracksFacade: what the Tracks tab (TrackListModel) and the player (PlayerController) need from
// the core about a meeting's audio: the meeting record (tracks, folder, mixdown), the tracks ready
// for display, the mixdown job, the waveform peaks, and the track operations (rename, restore,
// include / exclude, locate, delete, remix).
//
// The real implementation composes the meeting repository, TrackCatalog, WaveformService and the
// job tracker (gui/qml/src/TracksServicesFacade); the fictional one (demo/TracksDemoFacade) serves
// --demo, --gallery, --qml-shot and the tests: it keeps a ready-made track list of a design state
// (fixture()) in memory, and its operations change only that. It is a QObject only because it
// carries signals.
#include "tanara/domain/Meeting.h"
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/library/tracks/WaveformService.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

#include <optional>

namespace tanara::library {

// The mixdown job of a meeting.
struct MixdownJob {
    bool running = false;
    int percent = -1;
    bool cancellable = false;   // a standalone mixdown (not part of a transcription)
    bool cancelling = false;
};

// A fictional facade's ready-made track list (one design state).
struct TrackListFixture {
    struct Row {
        TrackView view;
        QList<qreal> levels;                     // the waveform's levels (0..1)
        QString peaksState = QStringLiteral("none");   // none | loading | ready | failed
        int colorIndex = 0;
    };
    qint64 meetingDurationMs = 0;
    QVector<Row> rows;
    QString mixdownState = QStringLiteral("none");     // none | ready | stale | running
    int mixdownPercent = -1;
    bool mixdownCancellable = false;
    QList<qreal> mixdownLevels;
    bool mixdownPeaksLoading = false;
};

class ITracksFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ITracksFacade() override = default;

    // False: fictional data (no audio files; the player plays silence over the meeting's length).
    virtual bool live() const { return true; }
    // The fictional facade's track list of a meeting; the real facade has none.
    virtual std::optional<TrackListFixture> fixture(const QString& meetingId) const
    {
        Q_UNUSED(meetingId);
        return std::nullopt;
    }

    // The meeting record (empty id: no such meeting).
    virtual Meeting meeting(const QString& meetingId) const = 0;
    virtual MixdownJob mixdownJob(const QString& meetingId) const = 0;

    // Waveform peaks of the tracks and the mixdown: peaksReady / peaksFailed. The service does not
    // duplicate a running request; cached ones come immediately.
    virtual void requestWaveforms(const QString& meetingId) = 0;
    virtual void cancelWaveforms(const QString& meetingId) = 0;

    // ---- operations (tracksChanged / mixdownUpdated follow) ----
    virtual bool renameTrack(const QString& meetingId, const QString& trackId, const QString& name) = 0;
    virtual void restoreTrack(const QString& meetingId, const QString& trackId) = 0;
    virtual void setTrackIncluded(const QString& meetingId, const QString& trackId, bool included) = 0;
    // Copies the chosen file into the meeting's folder. false (+ *error): failed.
    virtual bool relocateTrack(const QString& meetingId, const QString& trackId, const QString& filePath,
                               QString* error) = 0;
    // Permanently deletes every dropped track (the files too); how many.
    virtual int deleteDroppedTracks(const QString& meetingId) = 0;
    virtual void deleteTrack(const QString& meetingId, const QString& trackId) = 0;
    virtual void regenerateMixdown(const QString& meetingId) = 0;

signals:
    void tracksChanged(QString meetingId);
    // The user-given device names changed (the tracks' names follow).
    void trackNamesChanged();
    void mixdownUpdated(QString meetingId, bool ok);
    void mixdownProgress(QString meetingId, int percent);
    // The meeting's running jobs changed (e.g. a mixdown started / ended).
    void jobsChanged(QString meetingId);
    void peaksReady(QString meetingId, QString trackId, const tanara::library::TrackPeaks& peaks);
    void peaksFailed(QString meetingId, QString trackId);
};

} // namespace tanara::library

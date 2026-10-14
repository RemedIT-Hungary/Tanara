#pragma once
//
// PlayerController: player of the selected meeting's mixdown (gui/qml/CONTRACT.md).
//
// One instance lives, owned by Main.qml; content components get it as the `player` property.
//  - Setting meetingId loads the meeting's audio (the mixdown; if none, the largest active
//    track) and stops playback. The audio engine is created LAZILY (on first playback), so
//    startup does not trigger Qt Multimedia's device probing.
//  - positionMs updates ~25x/s during playback (for the transcript highlight).
//  - playRange() plays one utterance, then pauses.
//  - playFile() previews another file (a track); meanwhile the mixdown's position and
//    `playing` stay untouched (the preview state: previewPath / previewPlaying /
//    previewPositionMs / previewDurationMs). stopPreview() returns to the mixdown.
//    The preview plays the track file BY ITSELF (file time, from 0): a later-starting track
//    (Track::startOffsetMs) previews from the track's own start, meeting time is irrelevant.
//  - playTrack() / toggleTrack() listen to a TRACK in MEETING time (the speech strip's seek):
//    the file of the logical track (a device's segments) that covers the requested time plays,
//    file time = meeting time - Track::startOffsetMs; at the segment end the next segment
//    follows. This is a preview too (previewPath), plus previewTrackId (the leading segment's
//    id) and previewMeetingMs. The track preview can be paused (toggleTrack): the position
//    stays, the source is not emptied.
//
#include "PlayerBackend.h"

#include "tanara/domain/Meeting.h"
#include "tanara/library/tracks/ITracksFacade.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <functional>


namespace tanara_qml {

class PlayerController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(int positionMs READ positionMs NOTIFY positionMsChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY durationMsChanged)
    Q_PROPERTY(qreal rate READ rate WRITE setRate NOTIFY rateChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString previewPath READ previewPath NOTIFY previewPathChanged)
    // Convenience state beyond the contract, for the preview (Tracks tab).
    Q_PROPERTY(bool previewPlaying READ previewPlaying NOTIFY previewPlayingChanged)
    Q_PROPERTY(int previewPositionMs READ previewPositionMs NOTIFY previewPositionMsChanged)
    Q_PROPERTY(int previewDurationMs READ previewDurationMs NOTIFY previewDurationMsChanged)
    // Absolute path of the audio file being played (mixdown or fallback track); empty if none.
    Q_PROPERTY(QString audioPath READ audioPath NOTIFY availableChanged)
    // The track preview (playTrack): the logical track's leading id (empty = no track plays) and
    // the playhead in MEETING time.
    Q_PROPERTY(QString previewTrackId READ previewTrackId NOTIFY previewPathChanged)
    Q_PROPERTY(int previewMeetingMs READ previewMeetingMs NOTIFY previewPositionMsChanged)

public:
    // A track's point in meeting time: which segment file (index into meeting.tracks) and where
    // within it. index -1: no such track. In a gap before the requested time / between segments,
    // the next segment's start; after the last segment, the last segment (towards its end).
    struct TrackSeek {
        int index = -1;
        QString leaderId;
        qint64 fileMs = 0;
        qint64 offsetMs = 0;    // the segment's Track::startOffsetMs
    };
    static TrackSeek resolveTrackSeek(const QVector<tanara::Track>& tracks, const QString& trackId,
                                      qint64 meetingMs);

    using BackendFactory = std::function<PlayerBackend*(QObject* parent)>;
    // The real audio engine's factory (set by main.cpp). Empty -> the built-in silent engine.
    static void setBackendFactory(BackendFactory factory);
    // A standalone audio engine from the same factory (the silent engine without a factory), for
    // another window's own playback (People: listening to a voice sample). The caller owns it (parent).
    static PlayerBackend* createBackend(QObject* parent);

    explicit PlayerController(QObject* parent = nullptr);
    ~PlayerController() override;

    // The meeting source defaults to AppContext::services()'s tracks facade; injectable in tests.
    void setFacade(tanara::library::ITracksFacade* facade);

    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    bool available() const { return m_available; }
    bool playing() const { return m_playing; }
    int positionMs() const { return int(m_positionMs); }
    int durationMs() const { return int(m_durationMs); }
    qreal rate() const { return m_rate; }
    void setRate(qreal rate);
    qreal volume() const { return m_volume; }
    void setVolume(qreal volume);
    QString previewPath() const { return m_previewPath; }
    bool previewPlaying() const { return m_previewPlaying; }
    int previewPositionMs() const { return int(m_previewPositionMs); }
    int previewDurationMs() const { return int(m_previewDurationMs); }
    QString audioPath() const { return m_mixPath; }
    QString previewTrackId() const { return m_previewTrackId; }
    int previewMeetingMs() const { return int(m_previewOffsetMs + m_previewPositionMs); }

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(int ms);
    Q_INVOKABLE void playRange(int startMs, int endMs);
    // Plays a row with kPreRollMs lead-in and kPostRollMs tail, limited to the neighbor rows'
    // bounds: minStartMs = the previous row's end, maxEndMs = the next row's start (< 0 = no
    // limit). The run is never shorter than the [startMs, endMs] range.
    Q_INVOKABLE void playRange(int startMs, int endMs, int minStartMs, int maxEndMs);
    static constexpr int kPreRollMs = 150;
    static constexpr int kPostRollMs = 100;
    Q_INVOKABLE void playFile(const QString& absolutePath);
    Q_INVOKABLE void stopPreview();
    // Listen to a track in meeting time: meetingMs < 0 -> where it left off (same track),
    // otherwise the track's start. The mixdown's position and `playing` stay untouched.
    Q_INVOKABLE void playTrack(const QString& trackId, int meetingMs = -1);
    // Play / pause on the track (pause keeps the source and the position).
    Q_INVOKABLE void toggleTrack(const QString& trackId);
    // Re-resolves the source (e.g. the mixdown became ready) keeping the position.
    Q_INVOKABLE void reload();

signals:
    void meetingIdChanged();
    void availableChanged();
    void playingChanged();
    void positionMsChanged();
    void durationMsChanged();
    void rateChanged();
    void volumeChanged();
    void previewPathChanged();
    void previewPlayingChanged();
    void previewPositionMsChanged();
    void previewDurationMsChanged();
    void errorOccurred(const QString& message);

private:
    void attach();
    void resolveSource();
    void ensureBackend();
    void loadMix();
    void startMix();
    void setPosition(qint64 ms);
    void setPlaying(bool playing);
    void onTick();
    void onBackendPlaying(bool playing);
    void onBackendDuration(qint64 ms);
    void onBackendFinished();

    void resolveServices();
    void attach(tanara::library::ITracksFacade* facade);
    bool live() const;

    QPointer<tanara::library::ITracksFacade> m_facade;
    QList<QMetaObject::Connection> m_connections;
    bool m_explicitFacade = false;
    PlayerBackend* m_backend = nullptr;
    QTimer m_tick;

    QString m_meetingId;
    QString m_mixPath;          // the meeting's audio (empty: demo / none)
    bool m_mixLoaded = false;   // the engine currently holds the meeting's audio (not a preview)
    bool m_available = false;
    bool m_playing = false;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    bool m_ended = false;       // reached the end of the source (the engine may report 0 then)
    qint64 m_rangeEndMs = -1;   // end of playRange; -1 = continuous playback
    qreal m_rate = 1.0;
    qreal m_volume = 1.0;

    void loadPreview(const QString& absolutePath, qint64 fileMs);
    QString m_previewTrackId;   // for a track preview, the logical track's leading id
    QString m_previewSegmentId; // the id of the segment now playing
    qint64 m_previewOffsetMs = 0;
    QString m_previewPath;
    bool m_previewPlaying = false;
    qint64 m_previewPositionMs = 0;
    qint64 m_previewDurationMs = 0;
};

} // namespace tanara_qml

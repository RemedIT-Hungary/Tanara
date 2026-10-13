#pragma once
//
// Tanara QML: model of the Tracks tab: the meeting's audio tracks ready for display
// (friendly name, raw device name, state: active / dropped / missing file), per track the
// cached waveform peaks, and the mixdown state (ready / stale / running %).
//
// Sources: TrackCatalog (names, rename, locate, delete dropped), WaveformService (peaks:
// async, requested by requestWaveforms() when the tab first shows), AppController's
// restoreTrack / regenerateMixdown, the MeetingJobTracker (whether a mixdown runs).
// Updates per row on the core's signals (with an unchanged track order only dataChanged goes out).
//
// Without a controller or with App.demo, fictional sample data; demoState: "default" (the
// design sketch: the mixdown is running) | "loading" (computing peaks) | "idle" (all done).
//
#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/library/tracks/WaveformService.h"

#include <QAbstractListModel>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara {
namespace app { class AppController; }
}

namespace tanara_qml {

class TrackListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controllerObject WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    Q_PROPERTY(bool demo READ demo NOTIFY changed)

    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY changed)
    Q_PROPERTY(int droppedCount READ droppedCount NOTIFY changed)
    // Number of tracks going into the mixdown / left out for no speech (Overview > Tracks).
    Q_PROPERTY(int includedCount READ includedCount NOTIFY changed)
    Q_PROPERTY(int excludedCount READ excludedCount NOTIFY changed)
    // The loudest track's peak: the waveforms' common scale.
    Q_PROPERTY(qreal peakReference READ peakReference NOTIFY peaksChanged)

    // ---- mixdown ----
    // "none" (not yet) | "ready" | "stale" (tracks changed since) | "running"
    Q_PROPERTY(QString mixdownState READ mixdownState NOTIFY changed)
    Q_PROPERTY(int mixdownPercent READ mixdownPercent NOTIFY mixdownPercentChanged)
    // A standalone mixdown is running -> cancelable (shell.cancelJob(..., JobKinds.Mixdown)); if
    // it runs as part of the transcription, it can only be cancelled with the transcription.
    Q_PROPERTY(bool mixdownCancellable READ mixdownCancellable NOTIFY changed)
    Q_PROPERTY(bool mixdownCancelling READ mixdownCancelling NOTIFY changed)
    Q_PROPERTY(QString mixdownDurationText READ mixdownDurationText NOTIFY peaksChanged)
    Q_PROPERTY(QList<qreal> mixdownPeaks READ mixdownPeaks NOTIFY peaksChanged)
    Q_PROPERTY(bool mixdownPeaksLoading READ mixdownPeaksLoading NOTIFY peaksChanged)

public:
    enum Role {
        TrackIdRole = Qt::UserRole + 1,
        DisplayNameRole,      // what the UI shows (the user's name or the friendly name)
        FriendlyNameRole,     // the name derived from the role (undoing a rename)
        RenamedRole,
        RawNameRole,          // raw device name · file name
        IconNameRole,         // mic | monitor-speaker | speaker | audio-lines
        ActiveRole,           // false = dropped (silent)
        MissingRole,          // the audio file is missing
        PathRole,             // absolute path (for playback)
        DurationTextRole,
        PeaksRole,            // QList<qreal>
        PeaksStateRole,       // none | loading | ready | failed
        ColorIndexRole,       // the waveform's color (speaker palette)
        // The track's place on the meeting timeline (0..1): the waveform starts at waveStart of the
        // row's width (a later-enabled track / second segment) and is waveSpan long (the file's
        // length relative to the meeting).
        WaveStartRole,
        WaveSpanRole,
        SegmentRole,          // the device segment's number (1..), 0 = a single file
        StartOffsetRole,      // ms
        // Speech check (Track::speechRatio): 0..1, -1 = not measured yet.
        SpeechRatioRole,
        // Why it was left out of the mixdown: "" | "noSpeech" | "manual" (Track::excludedReason).
        ExcludedReasonRole,
        IncludedRole,         // is in the mixdown (Track::included())
        KindRole,             // "mic" | "loopback" | "other" (which side's audio)
        // The id of the leading segment of the logical track (the device's segments): the key of the
        // speech strip, track playback (PlayerController::previewTrackId) and the speaker -> track binding.
        GroupIdRole,
    };

    // The waveform's place on the meeting timeline: {start, length} as 0..1 fractions. Unknown
    // file length (fileDurationMs < 0) -> the track runs from its start to the meeting's end;
    // unknown meeting length -> {0, 1}.
    static QPair<qreal, qreal> waveExtent(qint64 startOffsetMs, qint64 fileDurationMs,
                                          qint64 meetingDurationMs);

    explicit TrackListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QObject* controllerObject() const;
    void setController(QObject* controller);
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    bool demo() const;

    int count() const { return int(m_rows.size()); }
    int activeCount() const;
    int droppedCount() const;
    int includedCount() const;
    int excludedCount() const;
    qreal peakReference() const;

    QString mixdownState() const { return m_mixdownState; }
    int mixdownPercent() const { return m_mixdownPercent; }
    bool mixdownCancellable() const { return m_mixdownCancellable; }
    bool mixdownCancelling() const { return m_mixdownCancelling; }
    QString mixdownDurationText() const;
    QList<qreal> mixdownPeaks() const { return m_mixdownPeaks; }
    bool mixdownPeaksLoading() const { return m_mixdownPeaksLoading; }

    // Requests the peaks for the still-missing tracks (to call when the tab first shows).
    Q_INVOKABLE void requestWaveforms();
    // Rename; an empty name -> back to the friendly name. True if it changed.
    Q_INVOKABLE bool rename(int row, const QString& name);
    // Restores a dropped track to active.
    Q_INVOKABLE void restore(int row);
    // "Include" / toggle: the track back into the mixdown, or a manual exclusion
    // (AppController::includeTrack / excludeTrack; then a remix).
    Q_INVOKABLE void setIncluded(int row, bool included);
    // Locates a missing file: the chosen file is copied into the meeting's folder. An empty
    // return = success; otherwise the human error message.
    QString relocate(int row, const QString& filePath);
    // Permanently deletes EVERY dropped track (confirmation is the caller's job). Returns how many were deleted.
    int deleteDropped();
    // QML calls these. The confirm / file-picker dialog runs a nested event loop, during which
    // the selection may switch to another meeting (e.g. a recording ends): so the caller notes
    // the meeting BEFORE the dialog (and the track's id, not its row) and passes it back here.
    // If the model switched to another meeting meanwhile, NOTHING happens (-1, or an error message).
    // A track's drawing scale: its own top level, but at least 30% of the common scale, so a
    // quiet track (e.g. a rarely speaking own mic) stays readable, yet a silent one is not
    // amplified into noise.
    Q_INVOKABLE qreal rowReference(int row) const;
    Q_INVOKABLE QString trackIdAt(int row) const;
    Q_INVOKABLE QString relocateTrack(const QString& meetingId, const QString& trackId,
                                      const QString& filePath);
    Q_INVOKABLE int deleteDroppedIn(const QString& meetingId);
    // Permanently deletes ONE track (the audio file too; AppController::deleteTrack). The
    // confirmation is the caller's job; the meeting check as in deleteDroppedIn. True: deleted.
    Q_INVOKABLE bool deleteTrackIn(const QString& meetingId, const QString& trackId);
    // Makes the mixdown (again) from the active tracks.
    Q_INVOKABLE void refreshMixdown();

signals:
    void controllerChanged();
    void meetingIdChanged();
    void demoStateChanged();
    void mixdownPercentChanged();
    void peaksChanged();
    void changed();

private:
    struct Row {
        tanara::library::TrackView view;
        QString groupId;
        QList<qreal> peaks;
        QString peaksState = QStringLiteral("none");
        qint64 durationMs = -1;
        int colorIndex = 0;
    };

    tanara::app::AppController* app() const;
    void connectController();
    void reload();
    void reloadMixdown();
    void loadDemo();
    void applyPeaks(const QString& trackId, const tanara::library::TrackPeaks& peaks, bool failed);
    static QList<qreal> toList(const QVector<float>& v);
    static QList<qreal> levelsOf(const tanara::library::TrackPeaks& peaks);
    static QString iconFor(tanara::library::TrackRole role);

    QPointer<QObject> m_injected;
    QPointer<tanara::app::AppController> m_connected;
    QList<QMetaObject::Connection> m_connections;
    QMap<QString, QString> m_deviceNames;   // last seen state of the device renames

    QString m_meetingId;
    QString m_demoState;
    QVector<Row> m_rows;
    qint64 m_meetingDurationMs = 0;
    bool m_waveformsRequested = false;

    QString m_mixdownState = QStringLiteral("none");
    int m_mixdownPercent = -1;
    bool m_mixdownCancellable = false;
    bool m_mixdownCancelling = false;
    QString m_mixdownPath;
    qint64 m_mixdownDurationMs = -1;
    QList<qreal> m_mixdownPeaks;
    bool m_mixdownPeaksLoading = false;
};

} // namespace tanara_qml

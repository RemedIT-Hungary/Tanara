#pragma once
//
// Tanara QML: the tracks' speech activity for the speech strip (TrackActivityStrip): from the
// core AppController::trackActivity() frames (50 ms, dB above the track's noise floor), one
// frame-state row per track: no data / silence / speech. Speech = the frame is at most 10 dB
// below the track's speech level (sides::speechLevelDb + sides::kSpeechDb), the same yardstick
// as the track-side analysis.
//
// One instance per meeting; TrackActivityItem reads it from C++ (no QML array copy, so
// 2 hours x 50 ms = 144,000 frames is fast too). While the core is still computing: state
// "loading", refreshes on the ready signal (trackActivityReady). The track id may be any
// segment's (we map to the leading one).
//
// Without a controller / with App.demo deterministic sample data (for TrackListModel's demo
// tracks: mic, call, system, mic2); demoState "loading" -> the computing state.
//
#include "tanara/edit/TrackActivity.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class TrackActivityModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controllerObject WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    // "none" (no meeting / track) | "loading" (the core is computing) | "ready"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // The common timeline's length (ms): the meeting's length, but at least the longest track's end.
    Q_PROPERTY(int durationMs READ durationMs NOTIFY changed)
    // Grows on every data change (the painter ties its repaint to this).
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    // Frame states.
    enum Frame : qint8 { NoData = -1, Quiet = 0, Speech = 1 };

    struct Lane {
        QString trackId;            // the leading segment's id
        qint64 originMs = 0;
        int frameMs = 50;
        QVector<qint8> frames;      // Frame values
        int speechFrames = 0;
    };

    explicit TrackActivityModel(QObject* parent = nullptr);

    QObject* controllerObject() const;
    void setController(QObject* controller);
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    QString state() const { return m_state; }
    int durationMs() const { return int(m_durationMs); }
    int revision() const { return m_revision; }

    // The track's (any segment's id) data; nullptr if none.
    const Lane* lane(const QString& trackId) const;

    Q_INVOKABLE bool hasTrack(const QString& trackId) const { return lane(trackId) != nullptr; }
    // The start of the first speech frame from fromMs (meeting time); -1 if there are no more.
    Q_INVOKABLE int nextSpeechMs(const QString& trackId, int fromMs = 0) const;
    // The share of speech frames among the covered frames (0..1); -1 if no data.
    Q_INVOKABLE qreal speechShare(const QString& trackId) const;
    Q_INVOKABLE void refresh() { reload(); }

    // The core's frames -> strip states (pure function; for tests too).
    static Lane laneFrom(const tanara::TrackActivity& t);

signals:
    void controllerChanged();
    void meetingIdChanged();
    void demoStateChanged();
    void changed();

private:
    tanara::AppController* app() const;
    void connectController();
    void reload();
    void loadDemo();
    void publish(const QString& state);

    QPointer<QObject> m_injected;
    QPointer<tanara::AppController> m_connected;
    QList<QMetaObject::Connection> m_connections;

    QString m_meetingId;
    QString m_demoState;
    QString m_state = QStringLiteral("none");
    qint64 m_durationMs = 0;
    int m_revision = 0;
    QVector<Lane> m_lanes;
    QHash<QString, int> m_laneOf;   // any segment id -> m_lanes index
};

} // namespace tanara_qml

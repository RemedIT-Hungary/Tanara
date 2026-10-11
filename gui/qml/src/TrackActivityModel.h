#pragma once
//
// Tanara QML — a sávok beszéd-aktivitása a beszéd-csíkhoz (TrackActivityStrip): a core
// AppController::trackActivity() kereteiből (50 ms, a sáv zajpadlója feletti dB) sávonként
// egy keret-állapot sor: nincs adat / csend / beszéd. Beszéd = a keret legfeljebb 10 dB-lel a
// sáv beszédszintje alatt (sides::speechLevelDb + sides::kSpeechDb), ugyanaz a mérce, mint a
// sáv-oldal elemzésé.
//
// Egy példány egy megbeszélésre; a TrackActivityItem C++-ból olvassa (nincs QML-tömb másolás,
// így 2 óra × 50 ms = 144 000 keret is gyors). Ha a core még számol: state "loading", a kész
// jelre (trackActivityReady) frissül. A sáv-id bármelyik szakaszé lehet (a vezetőre képezzük).
//
// Controller nélkül / App.demo mellett determinisztikus mintaadat (a TrackListModel demó-
// sávjaihoz: mic, call, system, mic2); demoState "loading" → számolás állapot.
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
    // "none" (nincs megbeszélés / sáv) | "loading" (a core számol) | "ready"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // A közös idővonal hossza (ms): a megbeszélés hossza, de legalább a leghosszabb sáv vége.
    Q_PROPERTY(int durationMs READ durationMs NOTIFY changed)
    // Minden adatváltozáskor nő (a rajzoló ehhez köti az újrarajzolást).
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    // Keret-állapotok.
    enum Frame : qint8 { NoData = -1, Quiet = 0, Speech = 1 };

    struct Lane {
        QString trackId;            // a vezető szakasz id-ja
        qint64 originMs = 0;
        int frameMs = 50;
        QVector<qint8> frames;      // Frame értékek
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

    // A sáv (bármelyik szakaszának id-ja) adatai; nullptr, ha nincs.
    const Lane* lane(const QString& trackId) const;

    Q_INVOKABLE bool hasTrack(const QString& trackId) const { return lane(trackId) != nullptr; }
    // Az első beszédes keret kezdete fromMs-től (megbeszélés-idő); -1, ha nincs több.
    Q_INVOKABLE int nextSpeechMs(const QString& trackId, int fromMs = 0) const;
    // A beszédes keretek aránya a lefedett keretekhez (0..1); -1, ha nincs adat.
    Q_INVOKABLE qreal speechShare(const QString& trackId) const;
    Q_INVOKABLE void refresh() { reload(); }

    // A core keretei → csík-állapotok (tiszta függvény; a tesztekhez is).
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
    QHash<QString, int> m_laneOf;   // bármely szakasz-id → m_lanes index
};

} // namespace tanara_qml

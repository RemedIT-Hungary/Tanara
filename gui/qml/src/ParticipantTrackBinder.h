#pragma once
//
// Tanara QML — beszélő → sáv kötés az Áttekintés KIK VOLTAK OTT soraiban (sáv-chipek).
// Egy megbeszélésre egy példány; soronként chips(id) adja a chipeket, toggle(id, trackId) vált.
// Az adat a core-é: AppController::participantTracks / setParticipantTracks — átirat előtt a
// Participant::tracks, átirat után a szerkesztő kézi sáv-beosztása (SpeakerOverlay::speakerTracks,
// a „Miért ő?" MELYIK SÁVON BESZÉL? chipjeivel közös). A kézi kötés nélkül a következtetett oldal
// sávjai látszanak (halványan).
//
// Chip: { trackId, name, icon ("mic" | "phone" | "audio-lines"), on, manual }.
// Kattintás: a mostani halmaz ⊕ a sáv (kézi kötés lesz belőle); ha a kézi halmaz üres maradna →
// a kézi kötés törlődik (vissza a következtetésre); a következtetett egyetlen sáv kattintása a
// kötés megerősítése.
//
// Controller nélkül / App.demo mellett mintaadat (a demó résztvevő-id-khoz: p1 = mikrofon,
// a többi = hívás; p3 kézzel mindkettőre kötve), a váltás memóriában.
//
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class ParticipantTrackBinder : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controllerObject WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    // Minden változáskor nő: a QML a chips(id) hívást ehhez köti.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // Van-e legalább két választható sáv (különben a chipeknek nincs értelme).
    Q_PROPERTY(bool available READ available NOTIFY changed)

public:
    explicit ParticipantTrackBinder(QObject* parent = nullptr);

    QObject* controllerObject() const;
    void setController(QObject* controller);
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    int revision() const { return m_revision; }
    bool available() const;

    // id: résztvevő-id vagy beszélő-kulcs (OverviewViewModel.participants[i].id). A javasolt
    // („suggest:") sorokra üres.
    Q_INVOKABLE QVariantList chips(const QString& id) const;
    Q_INVOKABLE bool toggle(const QString& id, const QString& trackId);
    // A kézi kötés törlése (vissza a következtetett oldalra).
    Q_INVOKABLE bool reset(const QString& id);

signals:
    void controllerChanged();
    void meetingIdChanged();
    void changed();

private:
    struct TrackChoice {
        QString id;
        QString name;
        QString icon;
    };

    tanara::AppController* app() const;
    void connectController();
    void reload();
    bool setTracks(const QString& id, const QStringList& trackIds);
    // A kötés (demóban a memóriából).
    QStringList binding(const QString& id, bool* manual) const;

    QPointer<QObject> m_injected;
    QPointer<tanara::AppController> m_connected;
    QList<QMetaObject::Connection> m_connections;

    QString m_meetingId;
    int m_revision = 0;
    QVector<TrackChoice> m_tracks;
    QHash<QString, QStringList> m_demoManual;
};

} // namespace tanara_qml

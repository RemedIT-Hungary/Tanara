#pragma once
//
// Tanara QML: speaker -> track binding in the Overview's WHO WAS THERE rows (track chips).
// One instance per meeting; per row chips(id) gives the chips, toggle(id, trackId) toggles.
// The data is the core's (IParticipantsFacade::participantTracks / setParticipantTracks): before a
// transcript Participant::tracks, after it the editor's manual track assignment
// (SpeakerOverlay::speakerTracks, shared with the "Why this one?" WHICH TRACK DOES THE
// SPEAKER USE? chips). Without a manual binding the inferred side's tracks show (dimmed).
//
// Chip: { trackId, name, shortName, icon ("mic" | "phone" | "audio-lines"), on, manual }.
// Click: the current set XOR the track (it becomes a manual binding); if the manual set would
// stay empty -> the manual binding is cleared (back to inference); clicking the single
// inferred track confirms the binding.
//
// On the fictional facade (demo) sample data (for the demo participant ids: p1 =
// microphone, the others = call; p3 manually bound to both), toggling in memory.
//
#include "UiServices.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara::editing { class IParticipantsFacade; }

namespace tanara_qml {

class ParticipantTrackBinder : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    // Grows on every change: QML binds the chips(id) call to it.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // Whether there are at least two selectable tracks (otherwise the chips make no sense).
    Q_PROPERTY(bool available READ available NOTIFY changed)

public:
    // QML: the services of the moment (follows App.controller / App.demo).
    explicit ParticipantTrackBinder(QObject* parent = nullptr);
    // The participants facade of the given services.
    explicit ParticipantTrackBinder(const UiServices& services, QObject* parent = nullptr);
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    int revision() const { return m_revision; }
    bool available() const;

    // id: participant id or speaker key (OverviewViewModel.participants[i].id). Empty for the
    // suggested ("suggest:") rows.
    Q_INVOKABLE QVariantList chips(const QString& id) const;
    Q_INVOKABLE bool toggle(const QString& id, const QString& trackId);
    // Clears the manual binding (back to the inferred side).
    Q_INVOKABLE bool reset(const QString& id);

signals:
    void meetingIdChanged();
    void changed();

private:
    struct TrackChoice {
        QString id;
        QString name;
        QString icon;
    };

    void resolveServices();
    void useServices(const UiServices& services);
    void reload();
    bool setTracks(const QString& id, const QStringList& trackIds);
    QStringList binding(const QString& id, bool* manual) const;

    bool m_explicitServices = false;
    tanara::editing::IParticipantsFacade* m_facade = nullptr;   // shared (real) or ours (fictional)
    QList<QMetaObject::Connection> m_connections;

    QString m_meetingId;
    int m_revision = 0;
    QVector<TrackChoice> m_tracks;
};

} // namespace tanara_qml

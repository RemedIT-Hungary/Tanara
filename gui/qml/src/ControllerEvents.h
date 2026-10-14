#pragma once
//
// ControllerEvents: the notifications the people / participants facades forward, as ONE object.
// Today the AppController is the hub that merges several services' signals (the people service,
// the participant service, the voice analysis, the transcript editors ...); AppContext connects it
// into one of these per controller, so the facades (and so the view models) never see the controller.
// It goes away when the services' own signals are complete (P12f).
//
#include <QObject>
#include <QString>

namespace tanara_qml {

class ControllerEvents : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    void peopleChanged();
    void voiceprintsChanged();
    void voiceModelsChanged();
    void voiceprintBackfillFinished();
    void participantsChanged(QString meetingId);
    void participantAnalysisStarted(QString meetingId);
    void participantAnalysisFinished(QString meetingId);
    void tracksChanged(QString meetingId);
    void speakerMapChanged(QString meetingId);
};

} // namespace tanara_qml

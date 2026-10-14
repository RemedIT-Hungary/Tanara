#pragma once
//
// IOverviewFacade: what the Overview tab's view model needs for one meeting: the finished
// snapshot of what it shows (who was there, the processing steps, the DATA tiles) and the few
// actions on it. The real implementation (gui/qml/src/ControllerFacades) builds the snapshot from
// the application's services (one shared instance); the fictional one (demo/OverviewDemoFacade)
// serves one design state ("overviewDone", "overviewProcessing", ... : its scenario; a new
// instance per view model, so its edits stay with that view model) for --demo, --gallery,
// --qml-shot and the tests.
//
// The rows are the QML contract's variant maps (see OverviewViewModel's property docs).
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace tanara::library {

struct OverviewSnapshot {
    bool hasTranscript = false;
    bool transcribing = false;
    bool allDone = false;
    QString title;
    QString contextNote;
    // "list" | "empty" | "nobody"
    QString peopleState = QStringLiteral("empty");
    QVariantList participants;
    QVariantList anonymousSpeakers;
    QString peopleHint;
    bool approved = false;
    bool approvalPending = false;
    bool analysisRunning = false;
    int candidateCount = 0;
    bool canReanalyze = false;
    QVariantMap sideWho;
    QVariantList steps;
    QVariantList stats;
    QString stepsSummary;
};

class IOverviewFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IOverviewFacade() override = default;

    // What the tab shows for this meeting. (A fictional facade keeps its edits: the added /
    // removed / named participants and the typed description stay in its snapshot.)
    virtual OverviewSnapshot snapshot(const QString& meetingId) = 0;

    // "+ Participant" / "Add": returns the participant's id ("" = error).
    virtual QString addParticipant(const QString& meetingId, const QString& name) = 0;
    // x: the participant's permanent removal (for a tag suggestion, id "suggest:<person>": its
    // lasting rejection for this meeting).
    virtual void removeParticipant(const QString& meetingId, const QString& id) = 0;
    // "Only I spoke". False: no own name is set.
    virtual bool setSolo(const QString& meetingId) = 0;
    // "Re-analyze" before a transcript: the audio analysis again.
    virtual bool reanalyze(const QString& meetingId) = 0;
    // Names an anonymous row in place (only the fictional facade can: true); false: the editor's
    // reassignSpeaker does it on real data.
    virtual bool nameSpeakerInPlace(const QString& meetingId, const QString& speakerKey, const QString& name) = 0;
    virtual void setContextNote(const QString& meetingId, const QString& note) = 0;

signals:
    // The snapshot of this meeting may have changed (the real facade: the core's signals; the
    // fictional one: after its own edits).
    void changed(QString meetingId);
};

} // namespace tanara::library

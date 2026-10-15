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
#include "tanara/providers/core/ReadinessModel.h"
#include "tanara/providers/core/Routes.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>

namespace tanara::library {

// S5: one route of a capability that a run can take (the split button's menu rows).
struct RouteChoiceRow {
    providers::Route route = providers::Route::Local;
    // The provider's display name on that route: "Whisper", "Soniox", the Tanara Cloud tier ("Pontos").
    QString provider;
    // Why the route cannot run now: a providers::routeBlocker code ("cloud_unavailable", ...), or
    // "not_configured" (the route's provider misses a setting / key: `detail` says what); "" = allowed.
    QString blocker;
    QString detail;           // the translated detail of "not_configured" (the readiness text)
    int etaSeconds = -1;      // the run's estimated length from earlier runs (library::JobStats); -1 = unknown
};

// S5: the routes of a capability for a run on a meeting.
struct RouteChoices {
    bool available = false;   // false: the facade knows no routes (a test double): no split button
    providers::Route stored = providers::Route::Local;   // the setting (may be Ask)
    bool cloudLive = false;
    // A Cloud run needs the cost estimate's confirmation (cloud.estimateBeforeRun): the main part then
    // opens the menu (the row shows the estimate) instead of starting.
    bool confirmCloud = true;
    QVector<RouteChoiceRow> rows;   // Local, External, Cloud (only the routes the capability offers)
};

// S5: the cost estimate of a Tanara Cloud run (display-ready money texts).
struct CloudRunEstimate {
    bool ok = false;          // false: no estimate (error says why)
    QString cost;             // "0,12 €"
    QString balance;          // "8,40 €"
    // "enough" | "littleReserve" (can start, little margin) | "notEnough" (blocked: top-up)
    QString level;
    int etaSeconds = -1;
    bool offline = false;     // the server was not reached (no internet): the local route still works
    QString error;            // translated; "" when ok
};

struct OverviewSnapshot {
    bool hasTranscript = false;
    bool transcribing = false;
    bool allDone = false;
    QString title;
    QString contextNote;
    // "list" | "empty" | "nobody"
    QString peopleState = QStringLiteral("empty");
    QVariantList participants;
    QString peopleHint;
    bool approved = false;
    bool approvalPending = false;
    // The "Who was there?" decision: "" (nothing decided, nothing to decide yet) | "pending" (the analysis
    // is done, no decision) | "approved" | "skipped" | "solo" ("Only I spoke").
    QString decision;
    // "Approve · N": the named rows the approval takes (sure by default, or confirmed "Was there"); after
    // an approval the approved ones.
    int approveCount = 0;
    // After the transcript: the anonymous speakers that have no line any more (hidden from the rows).
    int hiddenSpeakers = 0;
    // The user's own name (the "This is me" row of the naming panel); empty: not set.
    QString selfName;
    bool analysisRunning = false;
    int candidateCount = 0;
    bool canReanalyze = false;
    QVariantMap sideWho;
    QVariantList steps;
    // D5: where the transcript's speakers come from and the manual "Beszélők felismerése" (empty: nothing to say,
    // e.g. no transcript): { source: "provider" | "local" | "none", line (the "Beszélők: ..." text),
    // fixTarget ("speakers" | ""), canDiarize, hasNames (a run resets the names), speakerCount (the remembered
    // "How many speakers?", 0 = automatic), running (the diarize stage runs now), failed, problem (the failure's
    // human detail) }.
    QVariantMap speakers;
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
    // "Beszélők felismerése": the local diarization on the meeting's existing transcript (speakerCount > 0: "How many
    // speakers?", 0 = automatic), then the identification. false: not started (+ *whyNot, a code). The default
    // does nothing (test doubles).
    virtual bool diarizeSpeakers(const QString& meetingId, int speakerCount, QString* whyNot = nullptr)
    {
        Q_UNUSED(meetingId); Q_UNUSED(speakerCount);
        if (whyNot) *whyNot = QStringLiteral("unsupported");
        return false;
    }
    // "Re-analyze" before a transcript: the audio analysis again.
    virtual bool reanalyze(const QString& meetingId) = 0;
    virtual void setContextNote(const QString& meetingId, const QString& note) = 0;
    // Applies a "Who was there?" decision ("approved" | "skipped") in place: only the fictional facade
    // can (true). On real data the participants view model decides (approve / skip, with the learning
    // question) and the core's signal refreshes the snapshot.
    virtual bool decideInPlace(const QString& meetingId, const QString& decision)
    {
        Q_UNUSED(meetingId); Q_UNUSED(decision);
        return false;
    }

    // ---- the "Who was there?" rows' operations (the participant service; each one undo step) ----
    // `id`: the row's id (a participant, or after the transcript a speaker key). false: nothing changed.
    // The defaults do nothing (test doubles).
    // Names the row (a known person by id or name, else a new one), before the transcript too.
    virtual bool nameParticipant(const QString& meetingId, const QString& id, const QString& personIdOrName)
    {
        Q_UNUSED(meetingId); Q_UNUSED(id); Q_UNUSED(personIdOrName);
        return false;
    }
    // "Same voice as ...": the row `fromId` joins `intoId` (meeting-local).
    virtual bool mergeParticipantInto(const QString& meetingId, const QString& fromId, const QString& intoId)
    {
        Q_UNUSED(meetingId); Q_UNUSED(fromId); Q_UNUSED(intoId);
        return false;
    }
    // "Was there" (true) / its undo (false).
    virtual bool confirmParticipant(const QString& meetingId, const QString& id, bool confirmed)
    {
        Q_UNUSED(meetingId); Q_UNUSED(id); Q_UNUSED(confirmed);
        return false;
    }
    // "This is me". false + whyNot (translated) when no own name is set.
    virtual bool setSelfParticipant(const QString& meetingId, const QString& id, QString* whyNot = nullptr)
    {
        Q_UNUSED(meetingId); Q_UNUSED(id); Q_UNUSED(whyNot);
        return false;
    }

    // ---- S5: the per-run route (the split button) ----
    // The routes of the step's capability (stt / llm) for a run on the meeting. The default: none (no split).
    virtual RouteChoices routeChoices(providers::Capability cap, const QString& meetingId) const
    {
        Q_UNUSED(cap); Q_UNUSED(meetingId);
        return {};
    }
    // The cost estimate of the step on the Tanara Cloud (only asked in live cloud mode); `done` is called
    // once, possibly later (on the main thread). The default answers "no estimate".
    virtual void estimateCloud(providers::WorkflowStep step, const QString& meetingId,
                               std::function<void(const CloudRunEstimate&)> done)
    {
        Q_UNUSED(step); Q_UNUSED(meetingId);
        if (done) done(CloudRunEstimate{});
    }
    // "Ez legyen az alapértelmezett": writes the capability's route setting. false: not allowed / not saved.
    virtual bool setDefaultRoute(providers::Capability cap, providers::Route route)
    {
        Q_UNUSED(cap); Q_UNUSED(route);
        return false;
    }

signals:
    // The snapshot of this meeting may have changed (the real facade: the core's signals; the
    // fictional one: after its own edits).
    void changed(QString meetingId);
};

} // namespace tanara::library

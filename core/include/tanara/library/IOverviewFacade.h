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

signals:
    // The snapshot of this meeting may have changed (the real facade: the core's signals; the
    // fictional one: after its own edits).
    void changed(QString meetingId);
};

} // namespace tanara::library

#pragma once
//
// OverviewDemoFacade: the fictional tanara::library::IOverviewFacade for --demo, --gallery,
// --qml-shot and the tests. One instance serves one design state of the Overview tab
// ("overviewProcessing", "overviewDone", "overviewEmpty", "overviewNobody", "overviewRecorded",
// "overviewRecordedError", "overviewTranscribing"; empty -> "overviewDone") and keeps its edits.
// The "Who was there?" card's states (before the transcript, the transcription running):
// overviewProcessing (the decision pending), overviewNaming / overviewSameVoice / overviewWhichVoice
// (the same rows; the card opens its naming panel / "Same voice as..." / "Which voice is it?"),
// overviewVoiceless (+ a manual row without a voice), overviewThreeTracks (the track drop-down),
// overviewApproved (named, confirmed, merged, every named row bound to a track by hand),
// overviewSkipped, overviewSolo, overviewNoVoiceModel (no voice model, a manual row).
// D5, the finished meeting's transcript step: overviewSpeakersLocal | overviewSpeakersProvider |
// overviewSpeakersOff (the setting is off) | overviewSpeakersNoModel | overviewDiarizing (62 %) |
// overviewDiarizeFailed | overviewDiarizeAsk (the "Beszélők felismerése" dialog open, names + "2" remembered).
// The summary step on a finished transcript: overviewSummaryReady | overviewSummaryBlocked (no provider) |
// overviewSummaryRunning (40 %) | overviewSummaryFailed (the local server is down) | overviewSummaryStale.
// S5, recorded (like overviewRecorded) with the split button's menu open: overviewRouteAsk (stored Ask, the
// live cloud's estimate) | overviewRouteSoon (stored Local, Tanara Cloud "coming soon").
#include "tanara/library/IOverviewFacade.h"

#include <QHash>

namespace tanara_demo {

class OverviewDemoFacade : public tanara::library::IOverviewFacade {
    Q_OBJECT
public:
    explicit OverviewDemoFacade(const QString& scenario = {}, QObject* parent = nullptr);

    QString scenario() const { return m_scenario; }

    tanara::library::OverviewSnapshot snapshot(const QString& meetingId) override;
    QString addParticipant(const QString& meetingId, const QString& name) override;
    void removeParticipant(const QString& meetingId, const QString& id) override;
    // "Only I spoke" / the card's decision, in place (decision "solo" / "approved" / "skipped").
    bool setSolo(const QString& meetingId) override;
    bool decideInPlace(const QString& meetingId, const QString& decision) override;
    bool reanalyze(const QString&) override { return true; }
    // The manual "Beszélők felismerése", in place: the transcript's speakers become "local" (no job runs).
    bool diarizeSpeakers(const QString& meetingId, int speakerCount, QString* whyNot = nullptr) override;
    void setContextNote(const QString& meetingId, const QString& note) override;
    // The row operations in place (the rows of this instance's snapshot).
    bool nameParticipant(const QString& meetingId, const QString& id, const QString& personIdOrName) override;
    bool mergeParticipantInto(const QString& meetingId, const QString& fromId, const QString& intoId) override;
    bool confirmParticipant(const QString& meetingId, const QString& id, bool confirmed) override;
    bool setSelfParticipant(const QString& meetingId, const QString& id, QString* whyNot = nullptr) override;
    // S5: the split button. Stored Local (Whisper; Soniox external), Tanara Cloud "coming soon"; in
    // overviewRouteAsk stored Ask with the live cloud and a fixed estimate (0,12 € · about 1 minute).
    // setDefaultRoute changes the stored route in place.
    tanara::library::RouteChoices routeChoices(tanara::providers::Capability cap, const QString& meetingId) const override;
    void estimateCloud(tanara::providers::WorkflowStep step, const QString& meetingId,
                       std::function<void(const tanara::library::CloudRunEstimate&)> done) override;
    bool setDefaultRoute(tanara::providers::Capability cap, tanara::providers::Route route) override;

    // "Varga Árpád" -> "VÁ", "Remote 1" -> "R1" ("Kovács Lilla (you)" -> "KL").
    static QString monogramOf(const QString& name);

private:
    tanara::library::OverviewSnapshot build() const;
    void applyScenarioEdits();
    // D5: the finished meeting's transcript step with the speakers' source / the manual action.
    void applySpeakers(const QString& scenario, QVariantList& steps, tanara::library::OverviewSnapshot& snapshot) const;
    void applySummaryStep(const QString& scenario, QVariantList& steps, tanara::library::OverviewSnapshot& snapshot) const;
    // The index of the row with this id (or speaker key); -1 if none.
    int rowIndex(const QString& id) const;
    // After an edit: "Approve · N" recomputed, the change signal.
    void touch(const QString& meetingId);

    QString m_scenario;
    tanara::library::OverviewSnapshot m_snapshot;
    QHash<int, tanara::providers::Route> m_storedRoutes;   // int(Capability) -> the edited default
};

} // namespace tanara_demo

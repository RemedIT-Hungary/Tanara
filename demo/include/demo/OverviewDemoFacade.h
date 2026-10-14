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
#include "tanara/library/IOverviewFacade.h"

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
    bool nameSpeakerInPlace(const QString& meetingId, const QString& speakerKey, const QString& name) override;
    void setContextNote(const QString& meetingId, const QString& note) override;
    // The row operations in place (the rows of this instance's snapshot).
    bool nameParticipant(const QString& meetingId, const QString& id, const QString& personIdOrName) override;
    bool mergeParticipantInto(const QString& meetingId, const QString& fromId, const QString& intoId) override;
    bool confirmParticipant(const QString& meetingId, const QString& id, bool confirmed) override;
    bool setSelfParticipant(const QString& meetingId, const QString& id, QString* whyNot = nullptr) override;

    // "Varga Árpád" -> "VÁ", "Remote 1" -> "R1" ("Kovács Lilla (you)" -> "KL").
    static QString monogramOf(const QString& name);

private:
    tanara::library::OverviewSnapshot build() const;
    void applyScenarioEdits();
    // The index of the row with this id (or speaker key); -1 if none.
    int rowIndex(const QString& id) const;
    // After an edit: "Approve · N" recomputed, the change signal.
    void touch(const QString& meetingId);

    QString m_scenario;
    tanara::library::OverviewSnapshot m_snapshot;
};

} // namespace tanara_demo

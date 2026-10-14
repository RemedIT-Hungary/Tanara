#pragma once
//
// OverviewDemoFacade: the fictional tanara::library::IOverviewFacade for --demo, --gallery,
// --qml-shot and the tests. One instance serves one design state of the Overview tab
// ("overviewProcessing", "overviewDone", "overviewEmpty", "overviewNobody", "overviewRecorded",
// "overviewRecordedError", "overviewTranscribing"; empty -> "overviewDone") and keeps its edits.
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
    bool setSolo(const QString&) override { return true; }
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
    // The index of the row with this id (or speaker key); -1 if none.
    int rowIndex(const QString& id) const;

    QString m_scenario;
    tanara::library::OverviewSnapshot m_snapshot;
};

} // namespace tanara_demo

#pragma once
//
// TranscriptEditorDemoFacade: the transcript editor's FICTIONAL facade (App.demo, --qml-shot,
// --demo, the tests): a meeting built in a temporary folder, with a REAL editing session
// (SpeakerEditSession + RecheckService) on it. So every demo action (move, merge, undo, suggestion,
// filter) runs on the same code as live; only the "voice" is fake: the utterance embedder knows from
// the script whose voice speaks. It never touches the user's data (~/.tanara, ~/Tanara). One instance
// per view model and variant (its edits stay with that view model).
//
// Variants (TranscriptTab.demoVariant):
//   ""         4 speakers, long paragraphs, uncertain rows, rows suitable for a suggestion
//   "two"      two-speaker interview
//   "many"     11 speakers (collapsed "+N" column, "Other (N)" row)
//   "long"     the first utterance is a very long monologue
//   "novoice"  no voice model (no uncertainty / suggestion / manual voiceprint)
//   "v3"       the handoff's names (Kovács Lilla, Fehér Gábor, Varga Árpád...): mic and
//              call tracks with fictional activity, #Nordvik tag, rows for "To review" groups
//   "none"     no transcript
//
#include "tanara/domain/Meeting.h"
#include "tanara/editing/ITranscriptEditorFacade.h"
#include "tanara/editing/session/SpeakerEditTypes.h"

#include <QHash>
#include <QString>
#include <QTemporaryDir>
#include <QVector>
#include <memory>

namespace tanara {
namespace library { class MeetingRepository; }
namespace library { class PeopleStore; }
namespace library { class VoiceprintStore; }
namespace editing { class RecheckService; class SpeakerEditSession; }
}

namespace tanara_demo {

class TranscriptEditorDemoFacade : public tanara::editing::ITranscriptEditorFacade {
    Q_OBJECT
public:
    explicit TranscriptEditorDemoFacade(const QString& variant, QObject* parent = nullptr);
    ~TranscriptEditorDemoFacade() override;

    // ---- ITranscriptEditorFacade ----
    bool live() const override { return false; }
    Session open(const QString& meetingId) override;
    QVector<tanara::Track> tracks(const QString&) const override { return m_tracks; }
    // The demo never remembers the rail (every design state starts from the hidden rail).
    bool railVisible(const QString&) const override { return false; }
    void setRailVisible(const QString&, bool) override {}
    // The person pickers' list (fictional names; the meeting's participants are in it too).
    QVector<tanara::editing::PersonInfo> people() const override;
    // The fictional tag ids' names (for the evidence chips); empty for an unknown one.
    QString tagName(const QString& id) const override { return m_tagNames.value(id); }
    // The own person (in the v3 variant Kovács Lilla); null elsewhere.
    tanara::PersonId selfId() const override { return m_selfId; }
    // The fictional person tags (source of the candidate ranking); the quick "+ #Tag" writes here.
    bool addPersonTag(const tanara::PersonId& person, const QString& tagId) override;
    // Only in the "newPersonTag" design state: "<name> is a new person. Add the #Nordvik tag?".
    QVector<tanara::library::PersonTagQuestion> newPersonTagQuestions(const QString& meetingId,
                                                                      const tanara::PersonId& person,
                                                                      const QString& name) override;
    void answerTagQuestion(const tanara::library::PersonTagQuestion&, bool) override {}
    void undoTagAnswer() override {}
    bool reviewAutoPlay() const override { return m_reviewAutoPlay; }
    void setReviewAutoPlay(bool on) override { m_reviewAutoPlay = on; }

    // Preparing the v3 demo states knowing the script (confirmed cores, an "overlapping speech"
    // row, two foreign-voice confirmed rows for the contaminated core, the "fixed" row). Does
    // nothing to the meeting in another variant. Returns: true if the variant is v3.
    bool stage(const QString& state) override;
    // For the "suggestion" state: this row (raw "Remote 1", by voice Fehér Ádám) must be moved
    // to Fehér Ádám. Empty if the variant has none.
    QString suggestionSeedUtteranceId() const override { return m_seedId; }
    QString suggestionTargetKey() const override { return m_seedTarget; }

    // ---- for the tests ----
    tanara::editing::SpeakerEditSession* session() const { return m_session.get(); }
    tanara::editing::RecheckService* recheck() const { return m_recheck.get(); }
    QStringList personTags(const tanara::PersonId& person) const { return m_personTags->value(person); }
    QString selfName() const { return m_selfName; }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<tanara::library::MeetingRepository> m_store;
    std::unique_ptr<tanara::library::PeopleStore> m_people;
    std::unique_ptr<tanara::library::VoiceprintStore> m_prints;
    std::unique_ptr<tanara::editing::SpeakerEditSession> m_session;
    std::unique_ptr<tanara::editing::RecheckService> m_recheck;
    QString m_seedId;
    QString m_seedTarget;
    QHash<QString, QString> m_tagNames;
    std::shared_ptr<QHash<tanara::PersonId, QStringList>> m_personTags =
        std::make_shared<QHash<tanara::PersonId, QStringList>>();
    QString m_selfName;
    tanara::PersonId m_selfId;
    QVector<tanara::Track> m_tracks;
    bool m_v3 = false;
    bool m_tagQuestion = false;     // the "newPersonTag" design state is staged
    bool m_reviewAutoPlay = true;
    QVector<int> m_lineSpeaker;     // script row -> speaker (cast index)
    QVector<int> m_lineVoice;       // -> whose voice speaks
    QVector<qint64> m_lineDur;
};

} // namespace tanara_demo

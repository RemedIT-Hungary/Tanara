#pragma once
//
// TranscriptDemoSession: the transcript editor's FICTIONAL sample data (App.demo, --qml-shot,
// --demo): a meeting built in a temporary folder, with a REAL SpeakerEditor on it. So every
// demo action (move, merge, undo, suggestion, filter) runs on the same code as live; only the
// "voice" is fake: the utterance embedder knows from the script whose voice speaks. It never
// touches the user's data (~/.tanara, ~/Tanara).
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
#include "tanara/domain/Types.h"
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
namespace editing { class SpeakerEditor; }
}

namespace tanara_qml {

class TranscriptDemoSession {
public:
    explicit TranscriptDemoSession(const QString& variant);
    ~TranscriptDemoSession();

    tanara::editing::SpeakerEditor* editor() const { return m_editor.get(); }
    // The person pickers' list (fictional names; the meeting's participants are in it too).
    QVector<tanara::editing::PersonInfo> people() const;
    // For the "suggestion" state: this row (raw "Remote 1", by voice Fehér Ádám) must be moved
    // to Fehér Ádám. Empty if the variant has none.
    QString suggestionSeedUtteranceId() const { return m_seedId; }
    QString suggestionTargetKey() const { return m_seedTarget; }
    // The fictional tag ids' names (for the evidence chips); empty for an unknown one.
    QString tagName(const QString& id) const { return m_tagNames.value(id); }
    // The fictional person tags (source of the candidate ranking); the quick "+ #Tag" writes here.
    QStringList personTags(const tanara::PersonId& person) const { return m_personTags->value(person); }
    bool addPersonTag(const tanara::PersonId& person, const QString& tagId);
    // The own person (in the v3 variant Kovács Lilla); empty / null elsewhere.
    QString selfName() const { return m_selfName; }
    tanara::PersonId selfId() const { return m_selfId; }
    // Preparing the v3 demo states knowing the script (confirmed cores, an "overlapping speech"
    // row, two foreign-voice confirmed rows for the contaminated core, the "fixed" row). Does
    // nothing in another variant. Returns: true if the variant is v3.
    bool stageV3(const QString& state);
    // The fictional meeting's tracks (for the "Which track does the speaker use?" chips).
    QVector<tanara::Track> tracks() const { return m_tracks; }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<tanara::library::MeetingRepository> m_store;
    std::unique_ptr<tanara::library::PeopleStore> m_people;
    std::unique_ptr<tanara::library::VoiceprintStore> m_prints;
    std::unique_ptr<tanara::editing::SpeakerEditor> m_editor;
    QString m_seedId;
    QString m_seedTarget;
    QHash<QString, QString> m_tagNames;
    std::shared_ptr<QHash<tanara::PersonId, QStringList>> m_personTags =
        std::make_shared<QHash<tanara::PersonId, QStringList>>();
    QString m_selfName;
    tanara::PersonId m_selfId;
    QVector<tanara::Track> m_tracks;
    bool m_v3 = false;
    QVector<int> m_lineSpeaker;     // script row -> speaker (cast index)
    QVector<int> m_lineVoice;       // -> whose voice speaks
    QVector<qint64> m_lineDur;
};

} // namespace tanara_qml

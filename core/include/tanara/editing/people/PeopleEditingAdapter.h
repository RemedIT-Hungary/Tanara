#pragma once
//
// PeopleEditingAdapter: the editing module's implementation of the two library ports of the People
// window's service (cleanup plan T 1.2, library <- editing inversion):
//  - library::VoiceprintMaterialSource: voiceprint material / creation from a meeting through a temporary
//    SpeakerEditSession session (the undo stack of an editor open in the main window is not touched; the
//    voiceprint goes into the shared store);
//  - library::PersonReferenceEditor: the speaker overlay, the summary stale marks and transcript.md
//    (editing::speakeredit).
// A plain object (no QObject); app::Services owns it and hands it to library::PeopleService.
#include "tanara/library/people/PersonReferenceEditor.h"
#include "tanara/library/people/VoiceprintMaterialSource.h"

#include <QStringList>

#include <functional>

namespace tanara::library {
class MeetingRepository;
class PeopleStore;
class VoiceprintStore;
}

namespace tanara::editing {

class PeopleEditingAdapter : public library::VoiceprintMaterialSource, public library::PersonReferenceEditor {
public:
    struct Deps {
        library::MeetingRepository* store = nullptr;
        library::PeopleStore*       people = nullptr;
        library::VoiceprintStore*   voiceprints = nullptr;
        // The utterance embedder of the voice models in use (empty: no usable model).
        std::function<EmbedderFactory()> embedderFactory;
        // The editor's model list (the models in use; failing that, the enabled ones).
        std::function<QStringList()> editorModelIds;
    };

    explicit PeopleEditingAdapter(const Deps& deps);
    ~PeopleEditingAdapter() override;

    // ---- library::VoiceprintMaterialSource ----
    bool canCreate() const override;
    qint64 bestUsableMs(const QString& meetingId, const PersonId& person) override;
    VoiceprintResult createVoiceprint(const QString& meetingId, const PersonId& person, bool* noMaterial) override;
    void setEmbedderFactory(EmbedderFactory factory) override;

    // ---- library::PersonReferenceEditor ----
    QStringList markSummaryStaleKeys(const Meeting& m, const QStringList& speakerKeys) override;
    bool unmarkSummaryStale(const QString& meetingFolder, const QStringList& speakerKeys) override;
    bool isParticipantKey(const QString& key) const override;
    bool overlayRefersTo(const QString& meetingFolder, const PersonId& person) const override;
    bool removePersonFromOverlay(const QString& meetingFolder, const PersonId& person) override;
    OverlayPersonRefs replacePersonInOverlay(const QString& meetingFolder, const PersonId& from,
                                             const PersonId& to) override;
    bool restorePersonInOverlay(const QString& meetingFolder, const OverlayPersonRefs& refs,
                                const PersonId& current, const PersonId& original) override;
    void rewriteTranscriptNames(const Meeting& m, const PersonNames& names) override;

private:
    EmbedderFactory currentFactory() const;
    Deps m_deps;
    EmbedderFactory m_injected;
    bool m_factoryInjected = false;
};

} // namespace tanara::editing

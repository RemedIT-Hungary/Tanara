#pragma once
//
// VoiceprintMaterialSource: the port through which the People window's service (library::PeopleService)
// gets voiceprint material from a meeting: how much sure, long speech a person has there, and a
// voiceprint made from it. The transcript editor knows this (editing::SpeakerEditor's
// voiceprintMaterial / createVoiceprint), so the editing module implements the port
// (editing::PeopleEditingAdapter) and app::Services wires it in: the dependency inversion that keeps
// library free of editing (cleanup plan T 1.2).
#include "tanara/domain/Person.h"
#include "tanara/domain/Voice.h"
#include "tanara/providers/voice/IUtteranceEmbedder.h"

#include <QString>

#include <functional>
#include <memory>

namespace tanara::library {

class VoiceprintMaterialSource {
public:
    // The utterance embedder of the voice models (the providers port); only for the test seam below,
    // library code never calls it.
    using EmbedderFactory = providers::UtteranceEmbedderFactory;

    virtual ~VoiceprintMaterialSource() = default;

    // Whether a voiceprint can be made at all (a usable voice model / an injected embedder).
    virtual bool canCreate() const = 0;
    // The person's best sufficient material in the meeting (the largest usable length among their speakers
    // whose material is sufficient); 0 = none. Computed without the embedder (the plan is a quick estimate).
    virtual qint64 bestUsableMs(const QString& meetingId, const PersonId& person) = 0;
    // One voiceprint from the person's best sufficient speaker in the meeting (with the embedder), into the
    // shared voiceprint store. *noMaterial = true (and an empty result) if no speaker has enough.
    virtual VoiceprintResult createVoiceprint(const QString& meetingId, const PersonId& person,
                                              bool* noMaterial) = 0;
    // Test seam: the embedder used instead of the voice models (and the editor's default model list).
    virtual void setEmbedderFactory(EmbedderFactory factory) = 0;
};

} // namespace tanara::library

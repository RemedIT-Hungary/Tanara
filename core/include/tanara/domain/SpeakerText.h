#pragma once
//
// Domain: the display-time rendering of a speaker label (SpeakerKeys.h). Every consumer that SHOWS a
// speaker (editor, overview, review, participants, people, transcript.md, CLI) goes through this; a raw
// key follows the active UI language ("Beszélő 2" / "Speaker 2"), so stored data never freezes a language.
// The LLM input does not use this (it follows the summary language: processing/summary/PromptLibrary.h).
#include <QString>

namespace tanara {

// A raw speaker key -> "Beszélő N" (translated); a raw participant id with a side ("speaker:1@mic") keeps
// its suffix; anything else (a person's name, a track label) is returned unchanged.
QString speakerLabelText(const QString& label);

} // namespace tanara

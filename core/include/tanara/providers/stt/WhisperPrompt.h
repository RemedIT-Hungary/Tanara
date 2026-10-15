#pragma once
//
// The Whisper "prompt" built from the meeting context (pure, no I/O). Whisper reads at most ~224
// tokens of the prompt and drops its BEGINNING, so the note goes first and the terms (participant
// names, jargon) last: if something is cut, it is the start of the note, never a name.
//
// Token estimate: conservative ~2.5 characters per token (Hungarian text with a multilingual BPE
// tokenizer); the real count is usually lower, so a prompt within the estimate fits.
#include <QString>
#include <QStringList>

namespace tanara::providers {

// Whisper's prompt budget (tokens): half of its 448-token decoder context.
constexpr int kWhisperPromptTokenBudget = 224;

// Conservative token estimate of a text (ceil(chars / 2.5)); 0 for an empty text.
int estimateWhisperTokens(const QString& text);

struct WhisperPromptFit {
    QString prompt;              // what the model gets: the kept end of the note, then the terms
    int estimatedTokens = 0;     // estimateWhisperTokens(prompt) (<= budget)
    int fullTokens = 0;          // the estimate of the untruncated prompt (note + terms)
    int truncatedChars = 0;      // characters dropped (from the start of the note, then whole leading terms)
    QString droppedNote;         // the dropped start of the note ("" when nothing was cut)
    QString keptNote;            // the note's part in the prompt
    QStringList keptTerms;       // the terms in the prompt (all of them unless the terms alone exceed the budget)
};

// Builds the prompt "<note>. <term>, <term>, ..." within the budget: drops from the START of the
// note at a word boundary; if the terms alone do not fit, the note goes entirely and whole terms are
// dropped from the front (a term is never cut in half). Empty / blank inputs give an empty prompt.
WhisperPromptFit fitToBudget(const QString& note, const QStringList& terms,
                             int budget = kWhisperPromptTokenBudget);

} // namespace tanara::providers

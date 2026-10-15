#pragma once
//
// Domain: the language-neutral key of a RAW speaker (an unidentified speaker of the diarization):
// "speaker:N" (N from 1). Persisted data (transcript.tokens.json / segments.json, meeting.json speakerMap,
// transcript.speakers.json, participants' rawSpeakerIds, summary source snapshots) carries only the key;
// the label shown to the user ("Beszélő 2" / "Speaker 2") is rendered at DISPLAY time in the UI language
// (SpeakerText.h), the label the LLM sees in the prompt language (processing/summary/PromptLibrary.h).
//
// Old data stored the Hungarian label itself ("Beszélő 2", a bare "Beszélő" for a non-diarized token);
// the readers normalise it to the key on load (normalizeSpeakerLabel), so no migration pass is needed.
#include <QString>
#include <QStringList>

#include <optional>

namespace tanara::speakerkeys {

inline constexpr QLatin1String kRawSpeakerPrefix{"speaker:"};

// "speaker:<n>" (n is 1-based).
QString rawSpeakerKey(int n);
// The key of a provider's speaker id: a number -> rawSpeakerKey(n); empty -> rawSpeakerKey(1) (a provider
// without diarization); anything else -> "speaker:<id>" verbatim.
QString rawSpeakerKeyForId(const QString& providerId);
// Whether `label` is a raw speaker key ("speaker:" + a non-empty id).
bool isRawSpeakerKey(const QString& label);
// The number of a raw speaker key ("speaker:2" -> 2); nullopt for anything else (incl. a non-numeric id).
std::optional<int> rawSpeakerIndex(const QString& label);
// The id part of a raw speaker key ("speaker:2" -> "2"); empty for anything else.
QString rawSpeakerId(const QString& label);

// Read-side normalisation of a stored speaker label: the legacy Hungarian raw label "Beszélő N" (and the
// bare "Beszélő") -> "speaker:N" ("speaker:1"); a raw participant id with a side suffix keeps it
// ("Beszélő 1@mic" -> "speaker:1@mic"). Anything else (a key, a person's name, a track label) unchanged.
QString normalizeSpeakerLabel(const QString& label);
QStringList normalizeSpeakerLabels(const QStringList& labels);
// The fixed (untranslated) label of a raw key in Hungarian ("Beszélő N") or English ("Speaker N"): the
// LLM's transcript labels (processing/summary/PromptLibrary.h promptSpeakerLabel) and matching what the LLM
// wrote back (e.g. an action item's owner). Anything that is not a raw key is returned unchanged.
QString fixedSpeakerLabel(const QString& label, bool hungarian);
// The inverse for comparisons with fingerprints computed from old data: "speaker:N" -> "Beszélő N".
QString legacySpeakerLabel(const QString& label);

} // namespace tanara::speakerkeys

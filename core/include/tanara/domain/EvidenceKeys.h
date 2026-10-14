#pragma once
//
// Domain: the language-neutral ids of an Evidence (Participant.h). An Evidence that is PERSISTED (a
// participant's evidence in meeting.json / participants.analysis.json) carries only a key + params; the
// short chip text and the tooltip are rendered at DISPLAY time (EvidenceText.h) in the active language.
// Evidence made and shown within one run (the candidate ranker) may leave the key empty and fill `text`.
#include <QLatin1String>

namespace tanara::evidencekeys {

// ---- keys (Evidence::key) --------------------------------------------------------------------------
// Voice evidence.
inline constexpr QLatin1String kVoiceScore       {"voice.score"};        // named speaker heard; Evidence::value = cosine score
inline constexpr QLatin1String kVoiceUnknown     {"voice.unknown"};      // an unnamed voice heard
inline constexpr QLatin1String kVoiceNotHeard    {"voice.notHeard"};     // has voiceprints, but was not heard here
inline constexpr QLatin1String kVoiceNoVoiceprint{"voice.noVoiceprint"}; // named, not heard, no voiceprint to look for
// Track side evidence; params: kParamSides (QStringList of "mic" / "loopback", mic first).
inline constexpr QLatin1String kSideHeard        {"side.heard"};         // the side(s) the voice was heard on
inline constexpr QLatin1String kSideCallOnly     {"side.callOnly"};      // the user's own voice only on the call track
inline constexpr QLatin1String kManualTracks     {"manual.tracks"};      // the sides of the manual track binding
// Manual participant.
inline constexpr QLatin1String kManualAdded      {"manual.added"};       // added by hand
// Tag evidence (library::TagEvidence); params: kParamChip, kParamPerson, kParamItems, kParamElsewhere.
inline constexpr QLatin1String kTagMatch         {"tag.match"};          // the meeting's tags support the person
inline constexpr QLatin1String kTagOther         {"tag.other"};          // the person is elsewhere (contradiction)
// A text-only evidence read from an old file whose text matches no known key; params: kParamText (shown as is).
inline constexpr QLatin1String kLegacy           {"legacy"};

// ---- params (Evidence::params) ------------------------------------------------------------------------
inline constexpr QLatin1String kParamSides       {"sides"};      // QStringList: "mic" | "loopback"
inline constexpr QLatin1String kParamChip        {"chip"};       // QString: "#Nordvik" / "#Nordvik +1" (tag names are data)
inline constexpr QLatin1String kParamPerson      {"person"};     // QString: the person's name (manual tag text)
inline constexpr QLatin1String kParamItems       {"items"};      // QVariantList of {name, shared, total, manual}
inline constexpr QLatin1String kParamElsewhere   {"elsewhere"};  // QStringList: the person's other tag names
inline constexpr QLatin1String kParamText        {"text"};       // QString: verbatim text of a legacy evidence

} // namespace tanara::evidencekeys

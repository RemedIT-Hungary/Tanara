#pragma once
//
// Text helpers shared by every module (foundation): accent folding, search folding, slugs and
// name normalization. Pure functions, no I/O.
//
// The variants below are DELIBERATELY different (each keeps the behavior of the place it came from);
// do not merge them:
//  - fold / foldQuery / find / snippet: the library search; length-preserving (positions map back
//    onto the original text for highlighting), lowercase.
//  - foldForSearch: the people search; NFD, combining marks dropped, case-folded; NOT length-preserving.
//  - asciiSlug: ASCII-only file-name part (compatibility decomposition, marks dropped, lowercase,
//    hyphenated, trimmed, optional length limit) - imported track files.
//  - slug: letters and digits kept as they are (accents included), everything else '-', no
//    lowercasing - export file names.
//  - normalizeName: the display form of a user-typed name (NFC, whitespace simplified).
//
// Library search: accent- and case-insensitive matching ("odon" <-> "Odon" with accents).
// Folding is 1:1 in length PER CHARACTER: a position found in the folded text is directly the
// position in the original text, so the match can be highlighted in the original (accented) text.
// For this the input must first be normalized to NFC (normalize()).
#include <QString>
#include <QStringList>

namespace tanara {
namespace text {

// NFC normalization ("o + combining diaeresis" pairs fuse into one character).
QString normalize(const QString& s);

// Accent removal + lowercasing, keeping the length (for NFC input).
QString fold(const QString& nfc);

// Convenience: normalize + fold + simplified, for a search term.
QString foldQuery(const QString& query);

struct Range {
    int start = -1;
    int length = 0;
    bool isValid() const { return start >= 0 && length > 0; }
};

// The first occurrence of the (already folded) query in the (folded) text. For a multi-word
// phrase it first looks for the whole phrase; if not found but EVERY word is present, it returns
// the first occurrence of the first word. No match -> an invalid Range.
Range find(const QString& foldedText, const QString& foldedQuery);

// A snippet of the original text around the match (aligned to word boundaries, marked with an
// ellipsis). matchInSnippet: the match position WITHIN the snippet (for highlighting).
QString snippet(const QString& text, const Range& match, Range* matchInSnippet,
                int before = 32, int after = 72);

// People search: lowercase (case-folded), without accents (o-double-acute -> o, e-acute -> e ...).
// Canonical decomposition (NFD), then every combining mark is dropped. Not length-preserving.
QString foldForSearch(const QString& text);

// ASCII file-name part: compatibility decomposition (NFKD) with the non-spacing marks dropped,
// every run of non-[A-Za-z0-9] characters -> '-', leading/trailing '-' trimmed, lowercase, at most
// maxLength characters (< 0 = no limit). Empty result -> fallback.
QString asciiSlug(const QString& in, int maxLength, const QString& fallback);

// Slug that keeps letters and digits as they are (accents and case included): every other character
// -> '-', runs of '-' collapsed. Empty result -> fallback.
QString slug(const QString& in, const QString& fallback);

// Display form of a typed name: NFC, trimmed, inner whitespace collapsed to one space.
QString normalizeName(const QString& name);

} // namespace text
} // namespace tanara

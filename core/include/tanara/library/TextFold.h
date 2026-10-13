#pragma once
//
// Accent- and case-insensitive text matching for the library search ("odon" <-> "Odon" with accents).
//
// Folding is 1:1 in length PER CHARACTER: a position found in the folded text is directly the
// position in the original text, so the match can be highlighted in the original (accented) text.
// For this the input must first be normalized to NFC (normalize()).
#include <QString>
#include <QStringList>

namespace tanara {
namespace textfold {

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

} // namespace textfold
} // namespace tanara

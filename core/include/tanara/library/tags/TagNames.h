#pragma once
//
// Normalizing and comparing tag names (UI-independent, pure functions).
//
//  - normalizeTagName: the displayed form: NFC, trimmed, inner whitespace collapsed, without
//    leading "#" characters.
//  - tagKey: the equality key: accent- and case-insensitive, without spaces and punctuation
//    ("Museum Plus" and "MuseumPlus" have the same key).
//  - nearDuplicate: the same key, or a small typo (Damerau-Levenshtein <= 1 on keys of length
//    5+, <= 2 on keys of length 10+).
#include <QString>

namespace tanara::library {

QString normalizeTagName(const QString& name);
QString tagKey(const QString& name);
bool nearDuplicate(const QString& a, const QString& b);

// Damerau-Levenshtein distance with optimal alignment (OSA). With maxDistance >= 0 the
// computation stops early and returns maxDistance + 1 if the distance is surely greater.
int damerauLevenshtein(const QString& a, const QString& b, int maxDistance = -1);

} // namespace tanara::library
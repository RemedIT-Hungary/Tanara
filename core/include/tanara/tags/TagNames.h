#pragma once
//
// Címkenevek normalizálása és összehasonlítása (UI-független, tiszta függvények).
//
//  - normalizeTagName: a megjelenített alak — NFC, vágva, a belső szóközök egyre vonva, a
//    vezető „#” jelek nélkül.
//  - tagKey: az egyenlőség kulcsa — ékezet- és kisbetű-független, szóköz és írásjel nélkül
//    („Museum Plus” és „MuseumPlus” kulcsa azonos).
//  - nearDuplicate: azonos kulcs, vagy kis elírás (Damerau–Levenshtein ≤ 1 az 5+ hosszú,
//    ≤ 2 a 10+ hosszú kulcsokon).
//
#include <QString>

namespace tanara {

QString normalizeTagName(const QString& name);
QString tagKey(const QString& name);
bool nearDuplicate(const QString& a, const QString& b);

// Optimális igazítású (OSA) Damerau–Levenshtein távolság. maxDistance >= 0 esetén a
// számolás korán megáll, és maxDistance + 1-et ad, ha a távolság biztosan nagyobb.
int damerauLevenshtein(const QString& a, const QString& b, int maxDistance = -1);

} // namespace tanara

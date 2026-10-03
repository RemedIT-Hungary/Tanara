#pragma once
//
// Ékezet- és kisbetű-független szöveg-illesztés a könyvtár-kereséshez („odon” ⟷ „Ödön”).
//
// A hajtogatás (fold) KARAKTERENKÉNT 1:1 hosszú: a hajtogatott szövegben talált pozíció
// közvetlenül az eredeti szöveg pozíciója — így a találat kiemelhető az eredeti (ékezetes)
// szövegben. Ehhez a bemenetet előbb NFC-re kell normalizálni (normalize()).
//
#include <QString>
#include <QStringList>

namespace tanara {
namespace textfold {

// NFC-normalizálás (az „o + kombináló trém” párok egy karakterré olvadnak).
QString normalize(const QString& s);

// Ékezet-levétel + kisbetűsítés, a hossz megtartásával (NFC bemenetre).
QString fold(const QString& nfc);

// Kényelmi: normalize + fold + simplified — keresőkifejezéshez.
QString foldQuery(const QString& query);

struct Range {
    int start = -1;
    int length = 0;
    bool isValid() const { return start >= 0 && length > 0; }
};

// A (már hajtogatott) query első előfordulása a (hajtogatott) szövegben. Több szavas
// kifejezésnél előbb a teljes kifejezést keresi; ha az nincs meg, de MINDEN szó szerepel,
// az első szó első előfordulását adja. Nincs találat → érvénytelen Range.
Range find(const QString& foldedText, const QString& foldedQuery);

// Kivonat az eredeti szövegből a találat körül (szóhatárra igazítva, „…” jelöléssel).
// matchInSnippet: a találat helye a kivonaton BELÜL (kiemeléshez).
QString snippet(const QString& text, const Range& match, Range* matchInSnippet,
                int before = 32, int after = 72);

} // namespace textfold
} // namespace tanara

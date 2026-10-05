#pragma once
//
// Címkenév-illesztés a beviteli mezőhöz (C02) — tiszta függvények, állapot nélkül.
//
// A core (tanara/tags/TagNames.h) ugyanezt a szabályt valósítja meg a tároláshoz; ez a
// nézet-oldali változat a beviteli lista sorrendjét és kiemelését adja, így a TagInputModel
// bármelyik backenddel (demó / controller) ugyanúgy viselkedik:
//  - üres mező → a legutóbb használt címkék;
//  - gépelés → ékezet- és kisbetű-független találatok: előbb a név eleje, aztán szókezdet, aztán
//    bárhol; azonos csoporton belül a gyakoribb előbb; a végén „Új címke: „…”” — kivéve, ha a
//    beírt név (ékezet / kisbetű nélkül) pontosan megvan;
//  - nagyon hasonló név (ugyanaz a kulcs szóközök / írásjelek nélkül, vagy Damerau-Levenshtein
//    ≤ 1 a ≥ 5 hosszú kulcsokon, ≤ 2 a ≥ 10 hosszúakon) → „HASONLÓ MÁR VAN”: a meglévő elöl
//    (kijelölve), alul „Mégis új: „…””.
//
#include "TagBackend.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara_qml::tagmatch {

// Megjelenítéshez: NFC, két szélén vágva, a belső szóközök egyre.
QString normalizeName(const QString& name);
// Kereséshez: NFC + ékezet nélkül + kisbetűs; a hossz megegyezik az NFC alakéval (kiemeléshez).
QString foldName(const QString& name);
// Egyezés-kulcs: foldName, szóközök és írásjelek nélkül („Museum Plus” → „museumplus”).
QString key(const QString& name);
// Optimal string alignment (Damerau-Levenshtein, szomszédos csere = 1 lépés).
int editDistance(const QString& a, const QString& b);
// Ugyanaz a kulcs, vagy kis szerkesztési távolság (≥ 5 hosszú kulcson ≤ 1, ≥ 10-en ≤ 2).
bool nearDuplicate(const QString& a, const QString& b);

struct InputRow {
    QString kind;               // "recent" | "match" | "new" | "nearDuplicate" | "forceNew"
    QString id;
    QString name;               // new / forceNew: a beírt (normalizált) név
    int count = 0;
    int matchStart = -1;        // a kiemelés a name-ben (match / nearDuplicate)
    int matchLen = 0;
};

// A felugró lista sorai megjelenítési sorrendben. limit: legfeljebb ennyi meglévő címke
// (a „new” / „forceNew” sor ezen felül). excludeIds: a megbeszélésen már rajta lévők.
QVector<InputRow> inputRows(const QVector<TagItem>& all, const QVector<TagItem>& recent,
                            const QString& typed, int limit, const QStringList& excludeIds = {});

} // namespace tanara_qml::tagmatch

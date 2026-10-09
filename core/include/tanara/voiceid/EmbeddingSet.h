#pragma once
//
// EmbeddingSet — egy hangszelet (vagy lenyomat) beágyazásai modellenként, és a fúzió.
// A modell-sorrend mindenhol determinisztikus: a QMap kulcs szerint rendez.
//
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

using EmbeddingSet = QMap<QString /*modelId*/, QVector<float>>;

namespace fusion {

// Egy közös vektor: minden kért modell L2-normalizált vektora egymás után fűzve (a modellIds
// ábécérendjében), mindegyik 1/sqrt(k)-val szorozva. Így két fúziós vektor cosine-ja = a
// modellenkénti cosine-ok ÁTLAGA. Üres, ha bármely kért modellhez nincs (nem üres) vektor,
// vagy a modellIds üres.
QVector<float> fuse(const EmbeddingSet& set, const QStringList& modelIds);

// A modellenkénti cosine-ok átlaga a KÖZÖS (mindkét oldalon nem üres) kért modelleken.
// Nincs közös modell → -1. used: hány modell vett részt (opcionális).
double averageCosine(const EmbeddingSet& a, const EmbeddingSet& b, const QStringList& modelIds,
                     int* used = nullptr);

} // namespace fusion
} // namespace tanara

#pragma once
//
// Bizonyíték-modell (v3 design, 13. döntés): minden javaslat megmutatja, MIÉRT (hang %, sáv-oldal,
// címke, sorok száma itt, hasonló hang…), és minden ok megmondja, HOL javítható (fixTarget).
// Sima, header-only adat — a jelölt-rangsor (CandidateRanker.h), az Átnézendő csoportok
// (ReviewGroups.h) és a résztvevő-jóváhagyás is ezt használja.
//
#include <QMetaType>
#include <QString>
#include <QVector>

namespace tanara {

enum class EvidenceKind { Voice, Side, Tag, LineCount, Similarity, Calendar /*később*/, Manual };
enum class Polarity { Support, Contradict, Neutral };

struct Evidence {
    EvidenceKind kind = EvidenceKind::Voice;
    Polarity polarity = Polarity::Neutral;
    double value = 0.0;
    QString text;       // magyar, rövid, pl. "hang 82%"
    QString detail;
    QString fixTarget;  // "tracks" | "samples" | "tags" | "pair:<key>" | ""
};

struct Candidate {
    QString speakerKey;         // a meeting beszélője; üres = a meetingen kívüli ismert személy
    QString personName;
    double score = 0.0;
    QVector<Evidence> evidence;
    bool otherSide = false;     // a jelölt a másik sávon beszél (lejjebb sorolva, nem eltüntetve)
    int linesHere = 0;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::Evidence)
Q_DECLARE_METATYPE(tanara::Candidate)

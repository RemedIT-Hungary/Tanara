#pragma once
//
// Tanara — bizonyíték-modell: miért javasol a gép egy beszélőt / résztvevőt, és hol javítható
// az ok. Sima value-típusok (QObject nélkül). Lásd A-szelet: CandidateRanker.
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
    QString text;        // magyar, rövid, pl. "hang 82%"
    QString detail;
    QString fixTarget;   // "tracks" | "samples" | "tags" | "pair:<key>" | ""
};

struct Candidate {
    QString speakerKey;
    QString personName;
    double score = 0.0;
    QVector<Evidence> evidence;
    bool otherSide = false;
    int linesHere = 0;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::Evidence)
Q_DECLARE_METATYPE(tanara::Candidate)

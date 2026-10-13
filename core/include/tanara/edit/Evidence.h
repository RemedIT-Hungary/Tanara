#pragma once
//
// Evidence model: every suggestion shows WHY (voice %, track side, tag, number of lines here, a similar
// voice...), and every reason tells WHERE it can be fixed (fixTarget).
// Plain, header-only data; the candidate ranking (CandidateRanker.h), the "To review" groups
// (ReviewGroups.h) and the participant approval use it too.
#include <QMetaType>
#include <QString>
#include <QVector>

namespace tanara {

enum class EvidenceKind { Voice, Side, Tag, LineCount, Similarity, Calendar /* later */, Manual };
enum class Polarity { Support, Contradict, Neutral };

struct Evidence {
    EvidenceKind kind = EvidenceKind::Voice;
    Polarity polarity = Polarity::Neutral;
    double value = 0.0;
    QString text;       // Hungarian, short, e.g. "voice 82%"
    QString detail;
    QString fixTarget;  // "tracks" | "samples" | "tags" | "pair:<key>" | ""
};

struct Candidate {
    QString speakerKey;         // the meeting's speaker; empty = a known person outside the meeting
    QString personName;
    double score = 0.0;
    QVector<Evidence> evidence;
    bool otherSide = false;     // the candidate speaks on the other track (ranked lower, not removed)
    // Excluded by the manual track assignment (SideAnalysis.h sides::vetoes): without a score (0), at the END
    // of the list; never a suggestion, but selectable by hand. Always otherSide too.
    bool vetoed = false;
    int linesHere = 0;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::Evidence)
Q_DECLARE_METATYPE(tanara::Candidate)

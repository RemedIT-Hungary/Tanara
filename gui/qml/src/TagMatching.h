#pragma once
//
// Tag-name matching for the input field: pure functions, stateless.
//
// The core (tanara/tags/TagNames.h) implements the same rule for storage; this view-side
// version gives the input list's order and highlighting, so TagInputModel behaves the same
// with any backend (demo / controller):
//  - empty field -> the recently used tags;
//  - typing -> accent- and case-insensitive hits: name start first, then word start, then
//    anywhere; within a group the more frequent first; at the end "New tag: "..."", unless
//    the typed name (without accents / case) exists exactly;
//  - a very similar name (same key without spaces / punctuation, or Damerau-Levenshtein
//    <= 1 on keys of length >= 5, <= 2 on those of length >= 10) -> "SIMILAR ALREADY EXISTS":
//    the existing one first (selected), at the bottom "Create new anyway: "..."".
//
#include "TagBackend.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara_qml::tagmatch {

// For display: NFC, trimmed at both ends, inner spaces collapsed to one.
QString normalizeName(const QString& name);
// For searching: NFC + accent-free + lowercase; the length equals the NFC form's (for highlighting).
QString foldName(const QString& name);
// Match key: foldName without spaces and punctuation ("Archive Plus" -> "archiveplus").
QString key(const QString& name);
// Optimal string alignment (Damerau-Levenshtein, an adjacent swap = 1 step).
int editDistance(const QString& a, const QString& b);
// Same key, or a small edit distance (<= 1 on keys of length >= 5, <= 2 on >= 10).
bool nearDuplicate(const QString& a, const QString& b);

struct InputRow {
    QString kind;               // "recent" | "match" | "new" | "nearDuplicate" | "forceNew"
    QString id;
    QString name;               // new / forceNew: the typed (normalized) name
    int count = 0;
    int matchStart = -1;        // the highlight in name (match / nearDuplicate)
    int matchLen = 0;
};

// The popup list's rows in display order. limit: at most this many existing tags (the "new" /
// "forceNew" row is on top of that). excludeIds: those already on the meeting.
QVector<InputRow> inputRows(const QVector<TagItem>& all, const QVector<TagItem>& recent,
                            const QString& typed, int limit, const QStringList& excludeIds = {});

} // namespace tanara_qml::tagmatch

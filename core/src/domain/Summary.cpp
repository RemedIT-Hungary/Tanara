#include "tanara/domain/Summary.h"
#include "tanara/domain/SpeakerKeys.h"
#include "tanara/domain/SpeakerText.h"

#include "tanara/foundation/text/Text.h"

#include <QHash>
#include <QPair>
#include <QSet>

#include <algorithm>

namespace tanara {
namespace summarysrc {

QVector<SourceLine> linesFromSegments(const QVector<Utterance>& segments)
{
    QVector<SourceLine> out;
    QHash<QString, int> seen;
    out.reserve(segments.size());
    for (const Utterance& u : segments) {
        SourceLine l;
        const QString base = QStringLiteral("u%1").arg(u.startMs);
        const int n = ++seen[base];
        l.id = n == 1 ? base : QStringLiteral("%1-%2").arg(base).arg(n);
        l.startMs = u.startMs;
        l.endMs = u.endMs;
        l.speakerKey = u.speaker;
        l.speakerName = u.speaker;
        out.append(l);
    }
    return out;
}

void applyTargetedStaleness(QVector<SummaryStatement>& statements,
                            const QMap<QString, SourceSpeaker>& then,
                            const QHash<QString, SourceSpeaker>& now,
                            const QStringList& changedKeys, SummaryStaleInfo& info)
{
    // Targeted only if at least one statement has a source (otherwise the whole-document flag is the fallback).
    info.targeted = std::any_of(statements.cbegin(), statements.cend(),
                                [](const SummaryStatement& st) { return !st.sourceSpans.isEmpty(); });
    info.affectedStatements = info.affectedTodos = info.ownerChanges = 0;
    info.affectedStatementIds.clear();
    info.affectedUtteranceIds.clear();
    const QSet<QString> changed(changedKeys.cbegin(), changedKeys.cend());

    for (SummaryStatement& st : statements) {
        st.staleBecause.clear();
        st.ownerStaleBecause.clear();
        if (changed.isEmpty()) continue;
        QVector<QPair<QString, QString>> moves;     // (then, now) names
        for (const SourceSpan& sp : std::as_const(st.sourceSpans)) {
            for (const QString& id : sp.utteranceIds) {
                const auto t = then.constFind(id);
                const auto n = now.constFind(id);
                if (t == then.constEnd() || n == now.constEnd()) continue;   // regenerated transcript
                if (t->name == n->name) continue;
                if (!changed.contains(t->key) && !changed.contains(n->key)) continue;
                // The names are language-neutral (a raw key for an unnamed speaker): rendered for display here.
                const QString reason = QStringLiteral("%1 → %2?").arg(speakerLabelText(t->name), speakerLabelText(n->name));
                if (!st.staleBecause.contains(reason)) st.staleBecause << reason;
                moves.append({t->name, n->name});
                if (!info.affectedUtteranceIds.contains(id)) info.affectedUtteranceIds << id;
            }
        }
        if (st.staleBecause.isEmpty()) continue;
        info.affectedStatementIds << st.id;
        if (st.kind == StatementKind::Todo) {
            ++info.affectedTodos;
            const QString owner = text::foldQuery(st.owner).trimmed();
            if (!owner.isEmpty()) {
                for (const auto& mv : std::as_const(moves)) {
                    // The LLM wrote an unnamed speaker's owner in the summary language ("Beszélő 2" / "Speaker 2").
                    const QStringList forms{mv.first, speakerLabelText(mv.first),
                                            speakerkeys::fixedSpeakerLabel(mv.first, true),
                                            speakerkeys::fixedSpeakerLabel(mv.first, false)};
                    const bool same = std::any_of(forms.cbegin(), forms.cend(), [&owner](const QString& f) {
                        return text::foldQuery(f).trimmed() == owner;
                    });
                    if (same) {
                        st.ownerStaleBecause = QStringLiteral("%1 → %2?").arg(st.owner, speakerLabelText(mv.second));
                        ++info.ownerChanges;
                        break;
                    }
                }
            }
        } else {
            ++info.affectedStatements;
        }
    }
}

} // namespace summarysrc
} // namespace tanara

#include "tanara/providers/stt/WhisperPrompt.h"

namespace tanara::providers {

namespace {

const QLatin1String kNoteSep(". ");
const QLatin1String kTermSep(", ");

// The longest text (in characters) whose estimate fits into `tokens`: floor(tokens * 2.5).
qsizetype charsFor(int tokens) { return tokens <= 0 ? 0 : (qsizetype(tokens) * 5) / 2; }

QString joinPrompt(const QString& note, const QStringList& terms)
{
    const QString t = terms.join(kTermSep);
    if (note.isEmpty()) return t;
    if (t.isEmpty()) return note;
    // A note that already ends a sentence gets a space only (no ".." before the terms).
    static const QString enders = QStringLiteral(".!?;:…");
    return enders.contains(note.back()) ? note + QLatin1Char(' ') + t : note + kNoteSep + t;
}

} // namespace

int estimateWhisperTokens(const QString& text)
{
    // ceil(n / 2.5) = ceil(2n / 5)
    return int((qsizetype(text.size()) * 2 + 4) / 5);
}

WhisperPromptFit fitToBudget(const QString& note, const QStringList& terms, int budget)
{
    WhisperPromptFit out;
    const QString n = note.simplified();
    QStringList ts;
    for (const QString& t : terms) {
        const QString s = t.simplified();
        if (!s.isEmpty() && !ts.contains(s)) ts << s;
    }
    const QString full = joinPrompt(n, ts);
    out.fullTokens = estimateWhisperTokens(full);
    const qsizetype maxChars = charsFor(budget);
    if (full.size() <= maxChars) {
        out.prompt = full;
        out.keptNote = n;
        out.keptTerms = ts;
        out.estimatedTokens = out.fullTokens;
        return out;
    }

    const QString termText = ts.join(kTermSep);
    if (termText.size() > maxChars) {
        // The terms alone overflow: no note, whole terms dropped from the front.
        while (!ts.isEmpty() && ts.join(kTermSep).size() > maxChars) ts.removeFirst();
        out.keptTerms = ts;
        out.droppedNote = n;
        out.prompt = ts.join(kTermSep);
    } else {
        // Room for the note's end: what is left after the terms and the (longest) separator.
        const qsizetype room = termText.isEmpty() ? maxChars : maxChars - termText.size() - kNoteSep.size();
        QString kept;
        if (room > 0) {
            qsizetype start = n.size() - room;
            // Start at a word: skip the rest of a cut word (a word at the cut is dropped whole).
            if (start > 0 && !n.at(start - 1).isSpace()) {
                while (start < n.size() && !n.at(start).isSpace()) ++start;
            }
            while (start < n.size() && n.at(start).isSpace()) ++start;
            kept = n.mid(start);
        }
        out.keptNote = kept;
        out.droppedNote = n.left(n.size() - kept.size()).trimmed();
        out.keptTerms = ts;
        out.prompt = joinPrompt(kept, ts);
    }
    out.truncatedChars = int(full.size() - out.prompt.size());
    out.estimatedTokens = estimateWhisperTokens(out.prompt);
    return out;
}

} // namespace tanara::providers

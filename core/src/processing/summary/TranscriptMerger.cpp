#include "tanara/processing/summary/TranscriptMerger.h"
#include "tanara/foundation/time/TimeFormat.h"
#include "tanara/processing/stt/SonioxTiming.h"

#include <QString>
#include <QStringList>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>

namespace tanara::processing {

MergedTranscript mergeTranscripts(const QVector<TrackTranscript>& tracks) {
    MergedTranscript merged;
    if (!tracks.isEmpty())
        merged.language = tracks.first().language;

    for (const TrackTranscript& tr : tracks) {
        merged.emissionTiming = merged.emissionTiming || tr.emissionTiming;
        for (TranscriptToken tok : tr.tokens) {
            tok.trackId = tr.trackId;
            // A token's speaker is the track's fixed speaker, unless the token already brought a speaker with it
            // (in case of diarization).
            if (tok.speaker.isEmpty())
                tok.speaker = tr.speakerLabel;
            merged.tokens.append(tok);
        }
    }

    std::stable_sort(merged.tokens.begin(), merged.tokens.end(),
                     [](const TranscriptToken& a, const TranscriptToken& b) {
                         return a.startMs < b.startMs;
                     });

    return merged;
}

namespace {
QString formatTimestamp(qint64 ms) {
    return timefmt::timestamp(ms);
}

} // namespace

QString renderUtterancesMarkdown(const QVector<Utterance>& utterances) {
    // Format: `[mm:ss]` **Speaker** text, timestamp FIRST (searchable by time).
    QStringList paragraphs;
    for (const Utterance& u : utterances)
        paragraphs << QStringLiteral("`[%1]` **%2** %3")
                          .arg(formatTimestamp(u.startMs), u.speaker, u.text);
    return paragraphs.join(QStringLiteral("\n\n"));
}

} // namespace tanara::processing

// MergedTranscript is a domain type (domain/Transcript.h); its members are implemented here with
// the merge algorithm, hence in namespace tanara.
namespace tanara {

QVector<Utterance> MergedTranscript::segments() const {
    // Soniox subword tokens are concatenated DIRECTLY (the leading space marks the word boundary).
    // We arrange into pause-based (1.5 s) blocks per speaker, then by time.
    const qint64 GAP_MS = 1500;

    QHash<QString, QVector<const TranscriptToken*>> bySpeaker;
    QStringList speakerOrder;
    for (const TranscriptToken& t : tokens) {
        if (!bySpeaker.contains(t.speaker)) speakerOrder << t.speaker;
        bySpeaker[t.speaker].append(&t);
    }

    QVector<Utterance> utts;
    for (const QString& spk : speakerOrder) {
        Utterance cur; bool have = false;
        for (const TranscriptToken* t : bySpeaker[spk]) {
            if (!have || (t->startMs - cur.endMs) > GAP_MS) {
                if (have) utts.append(cur);
                cur = Utterance{t->startMs, t->endMs, spk, t->text};
                have = true;
            } else {
                cur.text += t->text;
                cur.endMs = t->endMs;
            }
        }
        if (have) utts.append(cur);
    }

    static const QRegularExpression multiSpace(QStringLiteral("[ \\t]{2,}"));
    QVector<Utterance> out;
    for (Utterance u : utts) {
        u.text.replace(multiSpace, QStringLiteral(" "));
        u.text = u.text.trimmed();
        if (!u.text.isEmpty()) out.append(u);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Utterance& a, const Utterance& b) { return a.startMs < b.startMs; });
    // Emission-moment timing (Soniox): grouping used the ORIGINAL token times (so lines neither merge nor
    // split); now the finished lines get the pre-/post-roll.
    if (emissionTiming) processing::sonioxtiming::padUtterances(out);
    return out;
}

QString MergedTranscript::renderMarkdown() const {
    return processing::renderUtterancesMarkdown(segments());
}

} // namespace tanara

#include "tanara/processing/summary/TranscriptMerger.h"
#include "tanara/domain/SpeakerText.h"
#include "tanara/foundation/time/TimeFormat.h"
#include "tanara/processing/stt/SonioxTiming.h"

#include <QString>
#include <QStringList>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
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
    // Format: `[mm:ss]` **Speaker** text, timestamp FIRST (searchable by time). A raw speaker key still on a
    // line is rendered in the UI language (transcript.md); the LLM input arrives with its labels already in
    // the summary language (applyPromptSpeakerLabels), which this leaves unchanged.
    QStringList paragraphs;
    for (const Utterance& u : utterances)
        paragraphs << QStringLiteral("`[%1]` **%2** %3")
                          .arg(formatTimestamp(u.startMs), speakerLabelText(u.speaker), u.text);
    return paragraphs.join(QStringLiteral("\n\n"));
}

} // namespace tanara::processing

// MergedTranscript is a domain type (domain/Transcript.h); its members are implemented here with
// the merge algorithm, hence in namespace tanara.
namespace tanara {

namespace {

QString lineText(const MergedTranscript& mt, const QVector<int>& group)
{
    static const QRegularExpression multiSpace(QStringLiteral("[ \\t]{2,}"));
    QString text;
    for (int i : group) text += mt.tokens.at(i).text;
    text.replace(multiSpace, QStringLiteral(" "));
    return text.trimmed();
}

} // namespace

QVector<QVector<int>> MergedTranscript::lineTokenGroups() const {
    // Soniox subword tokens are concatenated DIRECTLY (the leading space marks the word boundary).
    // We arrange into pause-based (1.5 s) blocks per speaker, then by time. A side cut breaks a block too.
    const qint64 GAP_MS = 1500;
    const QSet<qint64> cuts(sideCuts.cbegin(), sideCuts.cend());

    QHash<QString, QVector<int>> bySpeaker;
    QStringList speakerOrder;
    for (int i = 0; i < tokens.size(); ++i) {
        const QString& spk = tokens.at(i).speaker;
        if (!bySpeaker.contains(spk)) speakerOrder << spk;
        bySpeaker[spk].append(i);
    }

    QVector<QVector<int>> groups;
    for (const QString& spk : std::as_const(speakerOrder)) {
        QVector<int> cur;
        qint64 curEnd = 0;
        for (int i : std::as_const(bySpeaker[spk])) {
            const TranscriptToken& t = tokens.at(i);
            if (!cur.isEmpty() && ((t.startMs - curEnd) > GAP_MS || cuts.contains(t.startMs))) {
                groups.append(cur);
                cur.clear();
            }
            cur.append(i);
            curEnd = t.endMs;
        }
        if (!cur.isEmpty()) groups.append(cur);
    }

    QVector<QVector<int>> out;
    for (const QVector<int>& g : std::as_const(groups))
        if (!lineText(*this, g).isEmpty()) out.append(g);
    std::stable_sort(out.begin(), out.end(), [this](const QVector<int>& a, const QVector<int>& b) {
        return tokens.at(a.first()).startMs < tokens.at(b.first()).startMs;
    });
    return out;
}

QVector<Utterance> MergedTranscript::segments() const {
    QVector<Utterance> out;
    for (const QVector<int>& g : lineTokenGroups()) {
        const TranscriptToken& first = tokens.at(g.first());
        out.append(Utterance{first.startMs, tokens.at(g.last()).endMs, first.speaker, lineText(*this, g)});
    }
    // Emission-moment timing (Soniox): grouping used the ORIGINAL token times (so lines neither merge nor
    // split); now the finished lines get the pre-/post-roll.
    if (emissionTiming) processing::sonioxtiming::padUtterances(out);
    return out;
}

QString MergedTranscript::renderMarkdown() const {
    return processing::renderUtterancesMarkdown(segments());
}

} // namespace tanara

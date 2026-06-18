#include "tanara/TranscriptMerger.h"

#include <QString>
#include <QStringList>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>

namespace tanara {

MergedTranscript mergeTranscripts(const QVector<TrackTranscript>& tracks) {
    MergedTranscript merged;
    if (!tracks.isEmpty())
        merged.language = tracks.first().language;

    for (const TrackTranscript& tr : tracks) {
        for (TranscriptToken tok : tr.tokens) {
            tok.trackId = tr.trackId;
            // A token beszélője a sáv fix beszélője, kivéve ha a token már
            // hozott magával speaker-t (diarizáció esetén).
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
    const qint64 totalSec = ms / 1000;
    const qint64 mm = totalSec / 60;
    const qint64 ss = totalSec % 60;
    return QStringLiteral("%1:%2")
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'));
}

} // namespace

QVector<Utterance> MergedTranscript::segments() const {
    // A Soniox subword-tokeneket KÖZVETLENÜL fűzzük (a vezető szóköz a szóhatár).
    // Beszélőnként, szünet-alapú (1.5s) blokkokba rendezünk, majd idő szerint.
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
    return out;
}

QString MergedTranscript::renderMarkdown() const {
    // Formátum: `[mm:ss]` **Beszélő** szöveg — időbélyeg ELŐL (időre kereshető).
    QStringList paragraphs;
    for (const Utterance& u : segments())
        paragraphs << QStringLiteral("`[%1]` **%2** %3")
                          .arg(formatTimestamp(u.startMs), u.speaker, u.text);
    return paragraphs.join(QStringLiteral("\n\n"));
}

} // namespace tanara

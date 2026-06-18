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

QString MergedTranscript::renderMarkdown() const {
    // A Soniox subword-tokeneket ad, ahol a szóhatárt a vezető szóköz jelzi → a
    // tokeneket KÖZVETLENÜL fűzzük össze (nem teszünk közéjük szóközt). Az átfedő
    // beszéd token-szintű keveredése helyett beszélőnként, szünet-alapú blokkokba
    // (utterance) rendezünk, majd a blokkokat idő szerint sorba tesszük.
    const qint64 GAP_MS = 1500;

    struct Utt { QString spk; qint64 startMs; qint64 endMs; QString text; };

    QHash<QString, QVector<const TranscriptToken*>> bySpeaker;
    QStringList speakerOrder;
    for (const TranscriptToken& t : tokens) {
        if (!bySpeaker.contains(t.speaker)) speakerOrder << t.speaker;
        bySpeaker[t.speaker].append(&t);
    }

    QVector<Utt> utts;
    for (const QString& spk : speakerOrder) {
        Utt cur; bool have = false;
        for (const TranscriptToken* t : bySpeaker[spk]) {
            if (!have || (t->startMs - cur.endMs) > GAP_MS) {
                if (have) utts.append(cur);
                cur = Utt{spk, t->startMs, t->endMs, t->text};
                have = true;
            } else {
                cur.text += t->text;
                cur.endMs = t->endMs;
            }
        }
        if (have) utts.append(cur);
    }

    std::stable_sort(utts.begin(), utts.end(),
                     [](const Utt& a, const Utt& b) { return a.startMs < b.startMs; });

    static const QRegularExpression multiSpace(QStringLiteral("[ \\t]{2,}"));
    QStringList paragraphs;
    for (const Utt& u : utts) {
        QString body = u.text;
        body.replace(multiSpace, QStringLiteral(" "));
        body = body.trimmed();
        if (body.isEmpty()) continue;
        paragraphs << QStringLiteral("**%1** [%2] %3")
                          .arg(u.spk, formatTimestamp(u.startMs), body);
    }
    return paragraphs.join(QStringLiteral("\n\n"));
}

} // namespace tanara

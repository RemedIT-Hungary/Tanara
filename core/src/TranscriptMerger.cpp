#include "tanara/TranscriptMerger.h"

#include <QString>
#include <QStringList>
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

// A tokenek összeillesztése egy bekezdés szövegévé: szóköz a tokenek között,
// de írásjel elé nem teszünk szóközt.
QString joinTokens(const QStringList& parts) {
    QString out;
    for (const QString& raw : parts) {
        const QString p = raw;
        if (p.isEmpty())
            continue;
        if (!out.isEmpty()) {
            const QChar c = p.at(0);
            const bool isPunct = (c == QLatin1Char('.') || c == QLatin1Char(',') ||
                                  c == QLatin1Char('!') || c == QLatin1Char('?') ||
                                  c == QLatin1Char(':') || c == QLatin1Char(';'));
            if (!isPunct && !out.endsWith(QLatin1Char(' ')))
                out += QLatin1Char(' ');
        }
        out += p;
    }
    return out.trimmed();
}
} // namespace

QString MergedTranscript::renderMarkdown() const {
    QStringList paragraphs;

    int i = 0;
    const int n = tokens.size();
    while (i < n) {
        const QString speaker = tokens[i].speaker;
        const qint64 startMs = tokens[i].startMs;

        QStringList parts;
        int j = i;
        while (j < n && tokens[j].speaker == speaker) {
            parts << tokens[j].text;
            ++j;
        }

        const QString body = joinTokens(parts);
        paragraphs << QStringLiteral("**%1** [%2] %3")
                          .arg(speaker, formatTimestamp(startMs), body);
        i = j;
    }

    return paragraphs.join(QStringLiteral("\n\n"));
}

} // namespace tanara

// library::MeetingLayout: every meeting-folder file name in one place.
//  - every name is non-empty, relative and unique (no two files collide);
//  - path() joins, an empty folder gives an empty path;
//  - mixdownOf() falls back to the default name;
//  - the derived names keep their family (embedding caches end in the common suffix, the
//    transcript files of a backup are the transcript names).
#include <QtTest>

#include "tanara/library/MeetingLayout.h"

using namespace tanara;
using library::MeetingLayout;

class MeetingLayoutTest : public QObject {
    Q_OBJECT
private slots:
    void namesAreUniqueAndRelative()
    {
        const QStringList names = MeetingLayout::allNames();
        QVERIFY(names.size() >= 20);
        QSet<QString> seen;
        for (const QString& n : names) {
            QVERIFY2(!n.isEmpty(), "empty name");
            QVERIFY2(!n.contains(QLatin1Char('/')) && !n.contains(QLatin1Char('\\')), qPrintable(n));
            QVERIFY2(!seen.contains(n), qPrintable(QStringLiteral("duplicate: ") + n));
            seen.insert(n);
        }
    }

    void knownNames()
    {
        // The on-disk format: a change here is a format change (no backward compatibility, but deliberate).
        QCOMPARE(MeetingLayout::meetingJson(), QStringLiteral("meeting.json"));
        QCOMPARE(MeetingLayout::tokensJson(), QStringLiteral("transcript.tokens.json"));
        QCOMPARE(MeetingLayout::segmentsJson(), QStringLiteral("transcript.segments.json"));
        QCOMPARE(MeetingLayout::transcriptMd(), QStringLiteral("transcript.md"));
        QCOMPARE(MeetingLayout::overlayJson(), QStringLiteral("transcript.speakers.json"));
        QCOMPARE(MeetingLayout::summaryMd(), QStringLiteral("summary.md"));
        QCOMPARE(MeetingLayout::summaryJson(), QStringLiteral("summary.json"));
        QCOMPARE(MeetingLayout::topicsJson(), QStringLiteral("summary.topics.json"));
        QCOMPARE(MeetingLayout::analysesJson(), QStringLiteral("summary.analyses.json"));
        QCOMPARE(MeetingLayout::processingJson(), QStringLiteral("processing.json"));
        QCOMPARE(MeetingLayout::mixdownFile(), QStringLiteral("mixdown.mp3"));
        QCOMPARE(MeetingLayout::trackFilePattern(), QStringLiteral("track_*.ogg"));
    }

    void families()
    {
        QVERIFY(MeetingLayout::utteranceEmbeddings().endsWith(MeetingLayout::embeddingCacheSuffix()));
        QVERIFY(MeetingLayout::textEmbeddings().endsWith(MeetingLayout::embeddingCacheSuffix()));
        QVERIFY(MeetingLayout::trackFilePattern().startsWith(MeetingLayout::trackFilePrefix()));
        const QStringList t = MeetingLayout::transcriptFiles();
        QCOMPARE(t.size(), 4);
        QVERIFY(t.contains(MeetingLayout::tokensJson()));
        QVERIFY(t.contains(MeetingLayout::overlayJson()));
        QCOMPARE(MeetingLayout::waveformCache(QStringLiteral("/m/track_a.ogg")), QStringLiteral("/m/track_a.ogg.peaks.json"));
    }

    void paths()
    {
        QCOMPARE(MeetingLayout::path(QStringLiteral("/r/2026-01-01_1200_x_ab"), MeetingLayout::meetingJson()),
                 QStringLiteral("/r/2026-01-01_1200_x_ab/meeting.json"));
        QVERIFY(MeetingLayout::path(QString(), MeetingLayout::meetingJson()).isEmpty());
        Meeting m;
        QCOMPARE(MeetingLayout::mixdownOf(m), MeetingLayout::mixdownFile());
        m.mixdownFile = QStringLiteral("own.mp3");
        QCOMPARE(MeetingLayout::mixdownOf(m), QStringLiteral("own.mp3"));
    }
};

QTEST_GUILESS_MAIN(MeetingLayoutTest)
#include "test_meeting_layout.moc"

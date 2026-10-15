// The library repositories, write -> read round trips for every file type, in a temporary folder:
//  - TranscriptRepository: tokens (with emission timing), segments, transcript.md (LF, size cap),
//    speakerCount (cache + rewrite detection + invalidate);
//  - SummaryRepository: summary.json + summary.md, topics, analyses (upsert by topicId, a deleted
//    folder is not recreated), renderComplexMarkdown / openQuestionsOf, the note copy;
//  - UiStateRepository: the rail flag per meeting (only the switched-on ones stored);
//  - MeetingRepository: meeting.json without "folder", the reader fills the folder in.
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/library/MeetingLayout.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/TranscriptRepository.h"
#include "tanara/library/store/UiStateRepository.h"
#include "tanara/library/summary/SummaryRepository.h"

using namespace tanara;
using library::MeetingLayout;
using library::SummaryRepository;
using library::TranscriptRepository;

namespace {
QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

MergedTranscript sampleTranscript()
{
    MergedTranscript mt;
    mt.language = QStringLiteral("hu");
    mt.emissionTiming = true;
    auto tok = [](const char* text, const char* spk, qint64 s, qint64 e, const char* track) {
        TranscriptToken t;
        t.text = QString::fromUtf8(text); t.speaker = QString::fromUtf8(spk);
        t.startMs = s; t.endMs = e; t.confidence = 0.75; t.trackId = QString::fromUtf8(track);
        return t;
    };
    mt.tokens << tok("Szia", "speaker:1", 0, 400, "mixdown") << tok(" mindenki.", "speaker:1", 400, 900, "mixdown")
              << tok("Helló", "speaker:2", 1500, 1900, "mixdown");
    return mt;
}
} // namespace

class RepositoriesTest : public QObject {
    Q_OBJECT
private slots:
    void transcriptRoundTrip()
    {
        QTemporaryDir dir;
        const QString folder = dir.path();
        QVERIFY(!TranscriptRepository::hasTokens(folder));
        QVERIFY(TranscriptRepository::readTokens(folder).tokens.isEmpty());

        const MergedTranscript mt = sampleTranscript();
        QVERIFY(TranscriptRepository::writeTokens(folder, mt));
        QVERIFY(TranscriptRepository::hasTokens(folder));
        const MergedTranscript back = TranscriptRepository::readTokens(folder);
        QCOMPARE(back.language, mt.language);
        QCOMPARE(back.emissionTiming, true);
        QCOMPARE(back.tokens.size(), mt.tokens.size());
        for (int i = 0; i < mt.tokens.size(); ++i) {
            QCOMPARE(back.tokens.at(i).text, mt.tokens.at(i).text);
            QCOMPARE(back.tokens.at(i).speaker, mt.tokens.at(i).speaker);
            QCOMPARE(back.tokens.at(i).startMs, mt.tokens.at(i).startMs);
            QCOMPARE(back.tokens.at(i).endMs, mt.tokens.at(i).endMs);
            QCOMPARE(back.tokens.at(i).trackId, mt.tokens.at(i).trackId);
        }

        const QVector<Utterance> segs = mt.segments();
        QVERIFY(TranscriptRepository::writeSegments(folder, segs));
        const QVector<Utterance> segsBack = TranscriptRepository::readSegments(folder);
        QCOMPARE(segsBack.size(), segs.size());
        QCOMPARE(segsBack.first().speaker, segs.first().speaker);
        QCOMPARE(segsBack.last().text, segs.last().text);
        QCOMPARE(segsBack.last().startMs, segs.last().startMs);

        const QString md = QStringLiteral("# Átirat\n\nelső sor\nmásodik sor\n");
        QVERIFY(TranscriptRepository::writeMarkdown(folder, md));
        QCOMPARE(TranscriptRepository::readMarkdown(folder), md);
        QVERIFY(!readAll(TranscriptRepository::markdownPath(folder)).contains('\r'));   // LF only
        QCOMPARE(TranscriptRepository::readMarkdown(folder, 4), QStringLiteral("# Á"));    // byte cap (Á = 2 bytes)

        QCOMPARE(TranscriptRepository::tokensPath(folder), MeetingLayout::path(folder, MeetingLayout::tokensJson()));
    }

    void speakerCountCache()
    {
        QTemporaryDir dir;
        const QString folder = dir.path();
        library::TranscriptRepository repo;
        QCOMPARE(repo.speakerCount(folder), 0);
        QVERIFY(TranscriptRepository::writeSegments(folder, sampleTranscript().segments()));
        QCOMPARE(repo.speakerCount(folder), 2);
        // A rewrite with a different size is noticed even without an invalidate.
        MergedTranscript three = sampleTranscript();
        TranscriptToken t; t.text = QStringLiteral("Én is itt vagyok, hosszabb szöveggel."); t.speaker = QStringLiteral("speaker:3");
        t.startMs = 3000; t.endMs = 4000;
        three.tokens << t;
        QVERIFY(TranscriptRepository::writeSegments(folder, three.segments()));
        QCOMPARE(repo.speakerCount(folder), 3);
        repo.invalidate(folder);
        QCOMPARE(repo.speakerCount(folder), 3);
        // The {"segments": [...]} wrapper is accepted too.
        QFile f(TranscriptRepository::segmentsPath(folder));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"segments":[{"speaker":"A","text":"x"},{"speaker":"A","text":"y"}]})");
        f.close();
        repo.invalidate();
        QCOMPARE(repo.speakerCount(folder), 1);
    }

    void summaryRoundTrip()
    {
        QTemporaryDir dir;
        const QString folder = dir.path();
        library::SummaryDocument doc;
        doc.exists = true;
        doc.summary.execSummary = QStringLiteral("Rövid összefoglaló.");
        doc.summary.decisions << QStringLiteral("Döntés egy");
        doc.summary.actionItems << ActionItem{QStringLiteral("Teendő"), QStringLiteral("Anna"), QStringLiteral("péntek")};
        doc.meta.mode = library::SummaryMode::Quick;
        doc.meta.providerId = QStringLiteral("openai-compat");
        doc.meta.model = QStringLiteral("m1");
        doc.meta.createdAt = QDateTime::fromString(QStringLiteral("2026-10-14T10:00:00"), Qt::ISODate);
        doc.markdown = doc.summary.renderMarkdown();
        QVERIFY(SummaryRepository::writeMarkdown(folder, doc.markdown));
        QVERIFY(SummaryRepository::save(folder, doc));
        const library::SummaryDocument back = SummaryRepository::load(folder);
        QVERIFY(back.exists);
        QCOMPARE(back.summary.execSummary, doc.summary.execSummary);
        QCOMPARE(back.summary.decisions, doc.summary.decisions);
        QCOMPARE(back.summary.actionItems.size(), 1);
        QCOMPARE(back.summary.actionItems.first().owner, QStringLiteral("Anna"));
        QCOMPARE(back.meta.mode, library::SummaryMode::Quick);
        QCOMPARE(back.meta.model, QStringLiteral("m1"));
        QCOMPARE(back.markdown, doc.markdown);
    }

    void topicsAndAnalyses()
    {
        QTemporaryDir dir;
        const QString folder = dir.path();
        QVERIFY(SummaryRepository::readTopics(folder).isEmpty());
        QVector<SummaryTopic> topics{{QStringLiteral("t1"), QStringLiteral("Első"), QStringLiteral("a")},
                                     {QStringLiteral("t2"), QStringLiteral("  "), QStringLiteral("cím nélkül")},
                                     {QStringLiteral("t3"), QStringLiteral("Harmadik"), QString()}};
        QVERIFY(SummaryRepository::writeTopics(folder, topics));
        const QVector<SummaryTopic> tb = SummaryRepository::readTopics(folder);
        QCOMPARE(tb.size(), 2);   // a topic without a title is dropped on reading
        QCOMPARE(tb.at(1).id, QStringLiteral("t3"));

        TopicAnalysis a;
        a.topicId = QStringLiteral("t1"); a.title = QStringLiteral("Első"); a.detail = QStringLiteral("Részletek.");
        a.decisions << QStringLiteral("D1");
        a.actionItems << ActionItem{QStringLiteral("Tedd meg"), QString(), QString()};
        QVERIFY(SummaryRepository::upsertAnalysis(folder, a));
        a.detail = QStringLiteral("Új részletek.");
        QVERIFY(SummaryRepository::upsertAnalysis(folder, a));   // same topic: replaced
        TopicAnalysis b; b.topicId = QStringLiteral("t3"); b.title = QStringLiteral("Harmadik");
        QVERIFY(SummaryRepository::upsertAnalysis(folder, b));
        const QVector<TopicAnalysis> ab = SummaryRepository::readAnalyses(folder);
        QCOMPARE(ab.size(), 2);
        QCOMPARE(ab.first().detail, QStringLiteral("Új részletek."));
        QCOMPARE(ab.first().actionItems.first().text, QStringLiteral("Tedd meg"));
        // The meeting was deleted meanwhile: nothing is recreated.
        QVERIFY(!SummaryRepository::upsertAnalysis(dir.filePath(QStringLiteral("gone")), a));
        QVERIFY(!QDir(dir.filePath(QStringLiteral("gone"))).exists());
    }

    void complexMarkdown()
    {
        TopicAnalysis a; a.title = QStringLiteral("Költségvetés"); a.openQuestions << QStringLiteral("K1") << QStringLiteral("K2");
        TopicAnalysis b; b.title = QStringLiteral("Ütemterv"); b.openQuestions << QStringLiteral("K2");
        for (int i = 0; i < 8; ++i) b.openQuestions << QStringLiteral("X%1").arg(i);
        const QStringList open = SummaryRepository::openQuestionsOf({a, b});
        QCOMPARE(open.size(), 5);   // unique, capped
        QCOMPARE(open.at(1), QStringLiteral("K2"));
        const QString md = SummaryRepository::renderComplexMarkdown(
            QStringLiteral("Összegzés."), {ActionItem{QStringLiteral("Tétel"), QStringLiteral("Béla"), QStringLiteral("holnap")}}, {a, b});
        QVERIFY(md.startsWith(QStringLiteral("## Vezetői összefoglaló\n\nÖsszegzés.")));
        QVERIFY(md.contains(QStringLiteral("- [ ] Tétel — Béla (holnap)\n")));
        QVERIFY(md.contains(QStringLiteral("### 1. Költségvetés")));
        QVERIFY(md.contains(QStringLiteral("### 2. Ütemterv")));
        // It parses back as a per-topic summary.
        const library::SummaryDocument doc = SummaryRepository::parseMarkdown(md);
        QCOMPARE(doc.topics.size(), 2);
    }

    void noteCopy()
    {
        QTemporaryDir dir;
        const QString notes = dir.filePath(QStringLiteral("notes/sub"));
        QVERIFY(SummaryRepository::writeNoteCopy(notes, QStringLiteral("2026-10-14 x.md"), QStringLiteral("a\nb\n")));
        QCOMPARE(readAll(QDir(notes).filePath(QStringLiteral("2026-10-14 x.md"))), QByteArray("a\nb\n"));
    }

    void uiState()
    {
        QTemporaryDir dir;
        library::UiStateRepository none;
        QVERIFY(!none.railVisible(QStringLiteral("m1")));
        QVERIFY(!none.setRailVisible(QStringLiteral("m1"), true));

        const QString path = dir.filePath(QStringLiteral("state/ui-transcript.json"));
        library::UiStateRepository repo(path);
        QVERIFY(!repo.railVisible(QStringLiteral("m1")));
        QVERIFY(repo.setRailVisible(QStringLiteral("m1"), true));
        QVERIFY(repo.setRailVisible(QStringLiteral("m2"), true));
        QVERIFY(library::UiStateRepository(path).railVisible(QStringLiteral("m1")));
        QVERIFY(repo.setRailVisible(QStringLiteral("m1"), false));
        QVERIFY(!repo.railVisible(QStringLiteral("m1")));
        const QJsonObject rail = QJsonDocument::fromJson(readAll(path)).object().value(QStringLiteral("rail")).toObject();
        QCOMPARE(rail.keys(), QStringList{QStringLiteral("m2")});   // only the switched-on ones
    }

    void meetingFileHasNoFolder()
    {
        QTemporaryDir dir;
        Meeting m;
        m.id = QStringLiteral("abc12345");
        m.title = QStringLiteral("Teszt");
        m.folder = dir.filePath(QStringLiteral("2026-10-14_1000_Teszt_abc12345"));
        m.startedAt = QDateTime::fromString(QStringLiteral("2026-10-14T10:00:00"), Qt::ISODate);
        QVERIFY(library::MeetingRepository::writeMeetingFile(m));
        const QJsonObject o = QJsonDocument::fromJson(readAll(MeetingLayout::path(m.folder, MeetingLayout::meetingJson()))).object();
        QCOMPARE(o.value(QStringLiteral("id")).toString(), m.id);
        QVERIFY(!o.contains(QStringLiteral("folder")));
        // Moved by hand: the reader takes the folder from where the file is.
        const QString moved = dir.filePath(QStringLiteral("elsewhere"));
        QVERIFY(QDir().rename(m.folder, moved));
        const Meeting back = library::MeetingRepository::readMeetingFolder(moved);
        QCOMPARE(back.id, m.id);
        QCOMPARE(back.folder, moved);
        QVERIFY(library::MeetingRepository::readMeetingFolder(dir.filePath(QStringLiteral("nope"))).id.isEmpty());
    }
};

QTEST_GUILESS_MAIN(RepositoriesTest)
#include "test_repositories.moc"

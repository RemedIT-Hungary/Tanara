// SummaryRepository: structured access to the summary: summary.json (new summaries, with
// metadata) and reading back the old markdown-only summaries.
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/domain/Types.h"

using namespace tanara;

class SummaryStoreTest : public QObject {
    Q_OBJECT
private slots:
    void quickMarkdownRoundTrip();
    void topicsMarkdownParsed();
    void tolerantParsing();
    void emptyMarkdown();
    void loadNothing();
    void loadLegacyMarkdownOnly();
    void saveAndLoadJsonWithMeta();
    void editedMarkdownWins();
    void memoAndOpenQuestionsRoundTrip();
    void oldJsonWithoutNewFieldsLoads();
    void topicOpenQuestionsParsed();

private:
    static void write(const QString& path, const QString& text) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
    }
    static Summary sample() {
        Summary s;
        s.execSummary = QStringLiteral("A csapat áttekintette a pilot állását.\n\nA következő lépés az éles indulás.");
        s.decisions = {QStringLiteral("Az indulás október 15-én lesz."),
                       QStringLiteral("A régi rendszer párhuzamosan fut — két hétig.")};
        s.actionItems = {{QStringLiteral("Árajánlat elküldése"), QStringLiteral("Ödön"), QStringLiteral("péntek")},
                         {QStringLiteral("Szerződés átnézése"), QStringLiteral("Kovács Lilla"), QString()},
                         {QStringLiteral("Tesztkörnyezet frissítése"), QString(), QStringLiteral("2026-10-10")},
                         {QStringLiteral("Dokumentáció pótlása"), QString(), QString()}};
        s.participants = {QStringLiteral("Ödön"), QStringLiteral("Kovács Lilla"), QStringLiteral("Ádám")};
        return s;
    }
};

void SummaryStoreTest::quickMarkdownRoundTrip()
{
    // The output of Summary::renderMarkdown can be read back without loss.
    const Summary s = sample();
    const library::SummaryDocument doc = library::SummaryRepository::parseMarkdown(s.renderMarkdown());
    QVERIFY(doc.exists);
    QVERIFY(doc.fromMarkdown);
    QCOMPARE(doc.meta.mode, library::SummaryMode::Quick);
    QCOMPARE(doc.summary.execSummary, s.execSummary);
    QCOMPARE(doc.summary.decisions, s.decisions);
    QCOMPARE(doc.summary.participants, s.participants);
    QCOMPARE(doc.summary.actionItems.size(), s.actionItems.size());
    for (int i = 0; i < s.actionItems.size(); ++i) {
        QCOMPARE(doc.summary.actionItems[i].text, s.actionItems[i].text);
        QCOMPARE(doc.summary.actionItems[i].owner, s.actionItems[i].owner);
        QCOMPARE(doc.summary.actionItems[i].due, s.actionItems[i].due);
    }
    QVERIFY(doc.topics.isEmpty());
    // And back: the recovered structure gives the same markdown.
    QCOMPARE(doc.summary.renderMarkdown(), s.renderMarkdown());
}

void SummaryStoreTest::topicsMarkdownParsed()
{
    // The complex (per-topic) summary's format, as AppController writes it.
    TopicAnalysis a1;
    a1.title = QStringLiteral("Pilot állapota");
    a1.detail = QStringLiteral("A pilot három helyszínen fut.");
    a1.decisions = {QStringLiteral("Negyedik helyszín novemberben.")};
    a1.actionItems = {{QStringLiteral("Helyszín-lista véglegesítése"), QStringLiteral("Lilla"), QStringLiteral("okt. 20.")}};
    TopicAnalysis a2;
    a2.title = QStringLiteral("Árazás");
    a2.detail = QStringLiteral("Két csomag marad.\n\nA részletek a következő körben.");
    a2.decisions = {QStringLiteral("Az alapcsomag ára nem változik."), QStringLiteral("Negyedik helyszín novemberben.")};

    QString md = QStringLiteral("## Vezetői összefoglaló\n\nRöviden: jól haladunk.\n\n"
                                "## Teendők (összevont)\n\n- [ ] Helyszín-lista véglegesítése — Lilla (okt. 20.)\n\n"
                                "## Témák\n\n");
    md += QStringLiteral("### 1. %1\n\n").arg(a1.title) + a1.renderMarkdown();
    md += QStringLiteral("### 2. %1\n\n").arg(a2.title) + a2.renderMarkdown();

    const library::SummaryDocument doc = library::SummaryRepository::parseMarkdown(md);
    QCOMPARE(doc.meta.mode, library::SummaryMode::Topics);
    QCOMPARE(doc.summary.execSummary, QStringLiteral("Röviden: jól haladunk."));
    QCOMPARE(doc.summary.actionItems.size(), 1);
    QCOMPARE(doc.summary.actionItems[0].owner, QStringLiteral("Lilla"));
    QCOMPARE(doc.summary.actionItems[0].due, QStringLiteral("okt. 20."));
    QCOMPARE(doc.topics.size(), 2);
    QCOMPARE(doc.topics[0].title, a1.title);                 // the ordinal is dropped
    QCOMPARE(doc.topics[0].detail, a1.detail);
    QCOMPARE(doc.topics[0].decisions, a1.decisions);
    QCOMPARE(doc.topics[0].actionItems.size(), 1);
    QCOMPARE(doc.topics[0].actionItems[0].text, QStringLiteral("Helyszín-lista véglegesítése"));
    QCOMPARE(doc.topics[1].title, a2.title);
    QCOMPARE(doc.topics[1].detail, a2.detail);
    QCOMPARE(doc.topics[1].decisions, a2.decisions);
    QVERIFY(doc.topics[1].actionItems.isEmpty());
    // The top-level decision list is the union of the topics' decisions (without repeats).
    QCOMPARE(doc.summary.decisions,
             QStringList({"Negyedik helyszín novemberben.", "Az alapcsomag ára nem változik."}));
}

void SummaryStoreTest::tolerantParsing()
{
    // Hand-edited / differently shaped markdown: another heading level, unaccented and
    // upper-case titles, a * bullet, a ticked action item, an unknown section, intro text.
    const QString md = QStringLiteral(
        "Bevezető megjegyzés a fájl elején.\r\n"
        "\r\n"
        "# VEZETOI OSSZEFOGLALO\r\n"
        "Első bekezdés,\r\nkét sorban.\r\n"
        "\r\n"
        "## Dontesek ##\r\n"
        "* Első döntés\r\n"
        "* Második döntés, ami\r\n  a következő sorban folytatódik\r\n"
        "\r\n"
        "## Feladatok\r\n"
        "- [x] Kész feladat — Ödön\r\n"
        "1. Számozott feladat (holnap)\r\n"
        "ez egy elkóborolt folytatás\r\n"
        "\r\n"
        "## Megjegyzések\r\n"
        "Ez egy ismeretlen szakasz.\r\n"
        "\r\n"
        "## Résztvevők\r\n"
        "- Ödön\r\n"
        "- Lilla, Ádám\r\n");
    const library::SummaryDocument doc = library::SummaryRepository::parseMarkdown(md);
    QVERIFY(doc.exists);
    QVERIFY(doc.summary.execSummary.startsWith(QStringLiteral("Bevezető megjegyzés a fájl elején.")));
    QVERIFY(doc.summary.execSummary.contains(QStringLiteral("Első bekezdés,\nkét sorban.")));
    // The unknown section's text is not lost.
    QVERIFY(doc.summary.execSummary.contains(QStringLiteral("Megjegyzések")));
    QVERIFY(doc.summary.execSummary.contains(QStringLiteral("Ez egy ismeretlen szakasz.")));
    QCOMPARE(doc.summary.decisions,
             QStringList({"Első döntés", "Második döntés, ami a következő sorban folytatódik"}));
    QCOMPARE(doc.summary.actionItems.size(), 2);
    QCOMPARE(doc.summary.actionItems[0].text, QStringLiteral("Kész feladat"));
    QCOMPARE(doc.summary.actionItems[0].owner, QStringLiteral("Ödön"));
    QCOMPARE(doc.summary.actionItems[1].text, QStringLiteral("Számozott feladat ez egy elkóborolt folytatás"));
    QCOMPARE(doc.summary.actionItems[1].due, QStringLiteral("holnap"));
    QCOMPARE(doc.summary.participants, QStringList({"Ödön", "Lilla", "Ádám"}));
}

void SummaryStoreTest::emptyMarkdown()
{
    const library::SummaryDocument doc = library::SummaryRepository::parseMarkdown(QStringLiteral("  \n\n"));
    QVERIFY(!doc.exists);
    QVERIFY(doc.summary.execSummary.isEmpty());
}

void SummaryStoreTest::loadNothing()
{
    QTemporaryDir dir;
    QVERIFY(!library::SummaryRepository::load(dir.path()).exists);
    QVERIFY(!library::SummaryRepository::load(QString()).exists);
    QVERIFY(!library::SummaryRepository::save(dir.filePath("nincs-ilyen-mappa"), library::SummaryDocument{}));
}

void SummaryStoreTest::loadLegacyMarkdownOnly()
{
    // Old meeting: only summary.md exists.
    QTemporaryDir dir;
    const Summary s = sample();
    write(library::SummaryRepository::markdownPath(dir.path()), s.renderMarkdown());
    const library::SummaryDocument doc = library::SummaryRepository::load(dir.path());
    QVERIFY(doc.exists);
    QVERIFY(doc.fromMarkdown);
    QCOMPARE(doc.summary.decisions, s.decisions);
    QCOMPARE(doc.summary.actionItems.size(), 4);
    QCOMPARE(doc.markdown, s.renderMarkdown());
    // Metadata: what can honestly be known: the time is the file's, the provider/model unknown.
    QCOMPARE(doc.meta.mode, library::SummaryMode::Quick);
    QVERIFY(doc.meta.createdAt.isValid());
    QVERIFY(qAbs(doc.meta.createdAt.secsTo(QDateTime::currentDateTime())) < 60);
    QVERIFY(doc.meta.providerId.isEmpty());
    QVERIFY(doc.meta.model.isEmpty());
}

void SummaryStoreTest::saveAndLoadJsonWithMeta()
{
    QTemporaryDir dir;
    library::SummaryDocument doc;
    doc.exists = true;
    doc.summary = sample();
    // An action item that the markdown parser would misread (a parenthesis at the end): the json keeps it.
    doc.summary.actionItems.append({QStringLiteral("Riport (heti)"), QString(), QString()});
    TopicAnalysis a;
    a.topicId = QStringLiteral("t-1");
    a.title = QStringLiteral("Pilot");
    a.detail = QStringLiteral("Részletek.");
    a.decisions = {QStringLiteral("d1")};
    a.actionItems = {{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}};
    doc.topics = {a};
    doc.meta.createdAt = QDateTime(QDate(2026, 10, 1), QTime(14, 5, 9));
    doc.meta.providerId = QStringLiteral("openai-compat");
    doc.meta.model = QStringLiteral("google/gemma-4-12b");
    doc.meta.mode = library::SummaryMode::Topics;

    write(library::SummaryRepository::markdownPath(dir.path()), doc.summary.renderMarkdown());
    QVERIFY(library::SummaryRepository::save(dir.path(), doc));
    QVERIFY(QFile::exists(library::SummaryRepository::jsonPath(dir.path())));

    const library::SummaryDocument back = library::SummaryRepository::load(dir.path());
    QVERIFY(back.exists);
    QVERIFY(!back.fromMarkdown);
    QCOMPARE(back.meta.createdAt, doc.meta.createdAt);
    QCOMPARE(back.meta.providerId, QStringLiteral("openai-compat"));
    QCOMPARE(back.meta.model, QStringLiteral("google/gemma-4-12b"));
    QCOMPARE(back.meta.mode, library::SummaryMode::Topics);
    QCOMPARE(back.summary.execSummary, doc.summary.execSummary);
    QCOMPARE(back.summary.decisions, doc.summary.decisions);
    QCOMPARE(back.summary.participants, doc.summary.participants);
    QCOMPARE(back.summary.actionItems.size(), 5);
    QCOMPARE(back.summary.actionItems.last().text, QStringLiteral("Riport (heti)"));
    QVERIFY(back.summary.actionItems.last().due.isEmpty());
    QCOMPARE(back.topics.size(), 1);
    QCOMPARE(back.topics[0].topicId, QStringLiteral("t-1"));
    QCOMPARE(back.topics[0].actionItems[0].owner, QStringLiteral("y"));
    QCOMPARE(back.markdown, doc.summary.renderMarkdown());

    QCOMPARE(library::SummaryRepository::modeFromString(library::SummaryRepository::modeToString(library::SummaryMode::Quick)), library::SummaryMode::Quick);
    QCOMPARE(library::SummaryRepository::modeFromString(QStringLiteral("ismeretlen")), library::SummaryMode::Unknown);
}

void SummaryStoreTest::editedMarkdownWins()
{
    QTemporaryDir dir;
    library::SummaryDocument doc;
    doc.exists = true;
    doc.summary = sample();
    doc.meta.createdAt = QDateTime(QDate(2026, 9, 1), QTime(10, 0));
    doc.meta.providerId = QStringLiteral("openai-compat");
    doc.meta.model = QStringLiteral("m");
    doc.meta.mode = library::SummaryMode::Quick;
    write(library::SummaryRepository::markdownPath(dir.path()), doc.summary.renderMarkdown());
    QVERIFY(library::SummaryRepository::save(dir.path(), doc));

    // The user later rewrites summary.md by hand (materially newer than the json).
    Summary edited = sample();
    edited.decisions = {QStringLiteral("Kézzel átírt döntés.")};
    write(library::SummaryRepository::markdownPath(dir.path()), edited.renderMarkdown());
    QFile md(library::SummaryRepository::markdownPath(dir.path()));
    QVERIFY(md.open(QIODevice::ReadWrite));
    QVERIFY(md.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
    md.close();

    const library::SummaryDocument back = library::SummaryRepository::load(dir.path());
    QVERIFY(back.fromMarkdown);
    QCOMPARE(back.summary.decisions, QStringList({"Kézzel átírt döntés."}));
    QCOMPARE(back.meta.model, QStringLiteral("m"));            // the creation data stays
    QCOMPARE(back.meta.createdAt, doc.meta.createdAt);
}

void SummaryStoreTest::memoAndOpenQuestionsRoundTrip()
{
    Summary s = sample();
    s.openQuestions = {QStringLiteral("Kell-e külön tesztszerver?"), QStringLiteral("Ki hagyja jóvá a keretet?")};
    s.memo = {MemoSection{QStringLiteral("Pilot állása"), 0, 9 * 60000 + 5000,
                          {QStringLiteral("Két helyszínen fut."), QStringLiteral("A hibák 80%-a javítva.")}},
              MemoSection{QStringLiteral("Éles indulás (ütemezés)"), 95 * 60000, 101 * 60000 + 30000,
                          {QStringLiteral("Október 15.")}}};

    // Markdown to structure to markdown: without loss.
    const QString md = s.renderMarkdown();
    const library::SummaryDocument doc = library::SummaryRepository::parseMarkdown(md);
    QCOMPARE(doc.summary.openQuestions, s.openQuestions);
    QCOMPARE(doc.summary.memo.size(), 2);
    QCOMPARE(doc.summary.memo[0].title, QStringLiteral("Pilot állása"));
    QCOMPARE(doc.summary.memo[0].startMs, 0);
    QCOMPARE(doc.summary.memo[0].endMs, 9 * 60000 + 5000);
    QCOMPARE(doc.summary.memo[0].points, s.memo[0].points);
    QCOMPARE(doc.summary.memo[1].title, QStringLiteral("Éles indulás (ütemezés)"));   // the parenthesis in the title stays
    QCOMPARE(doc.summary.memo[1].startMs, 95 * 60000);
    QCOMPARE(doc.summary.participants, s.participants);   // the memo does not mix into the participants
    QCOMPARE(doc.summary.renderMarkdown(), md);

    // summary.json: the same, with the exact times.
    QTemporaryDir dir;
    library::SummaryDocument out;
    out.exists = true;
    out.summary = s;
    out.meta.mode = library::SummaryMode::Quick;
    out.meta.createdAt = QDateTime(QDate(2026, 10, 4), QTime(12, 0));
    write(library::SummaryRepository::markdownPath(dir.path()), md);
    QVERIFY(library::SummaryRepository::save(dir.path(), out));
    const library::SummaryDocument back = library::SummaryRepository::load(dir.path());
    QVERIFY(!back.fromMarkdown);
    QCOMPARE(back.summary.openQuestions, s.openQuestions);
    QCOMPARE(back.summary.memo.size(), 2);
    QCOMPARE(back.summary.memo[1].endMs, 101 * 60000 + 30000);
    QCOMPARE(back.summary.memo[1].points, s.memo[1].points);
    QFile jf(library::SummaryRepository::jsonPath(dir.path()));
    QVERIFY(jf.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(jf.readAll()).object().value("version").toInt(), 4);
}

void SummaryStoreTest::oldJsonWithoutNewFieldsLoads()
{
    // An earlier build's summary.json (version 1, without openQuestions and memo).
    QTemporaryDir dir;
    write(library::SummaryRepository::jsonPath(dir.path()), QStringLiteral(
        R"({"version":1,"mode":"quick","createdAt":"2026-09-01T10:00:00","providerId":"openai-compat",)"
        R"("model":"m","summary":{"execSummary":"Régi.","decisions":["d"],)"
        R"("actionItems":[{"text":"t","owner":"o","due":""}],"participants":["Ádám"]}})"));
    const library::SummaryDocument doc = library::SummaryRepository::load(dir.path());
    QVERIFY(doc.exists);
    QVERIFY(!doc.fromMarkdown);
    QCOMPARE(doc.summary.execSummary, QStringLiteral("Régi."));
    QCOMPARE(doc.summary.actionItems.size(), 1);
    QVERIFY(doc.summary.openQuestions.isEmpty());
    QVERIFY(doc.summary.memo.isEmpty());
    // The old markdown without open questions and memo loads too (the sample is like that).
    QCOMPARE(library::SummaryRepository::parseMarkdown(sample().renderMarkdown()).summary.memo.size(), 0);
}

void SummaryStoreTest::topicOpenQuestionsParsed()
{
    TopicAnalysis a;
    a.title = QStringLiteral("Keret");
    a.detail = QStringLiteral("A keretről szó esett.");
    a.decisions = {QStringLiteral("Marad 12 millió.")};
    a.openQuestions = {QStringLiteral("Lesz-e pótkeret?")};
    a.actionItems = {{QStringLiteral("Kalkuláció"), QStringLiteral("Ödön"), QString()}};
    const QString md = QStringLiteral("## Vezetői összefoglaló\n\nX\n\n## Témák\n\n### 1. Keret\n\n") + a.renderMarkdown();
    const library::SummaryDocument doc = library::SummaryRepository::parseMarkdown(md);
    QCOMPARE(doc.meta.mode, library::SummaryMode::Topics);
    QCOMPARE(doc.topics.size(), 1);
    QCOMPARE(doc.topics[0].decisions, a.decisions);
    QCOMPARE(doc.topics[0].openQuestions, a.openQuestions);
    QCOMPARE(doc.topics[0].actionItems.size(), 1);
    QCOMPARE(doc.summary.openQuestions, a.openQuestions);   // in per-topic mode the union of the topics
}

QTEST_GUILESS_MAIN(SummaryStoreTest)
#include "test_summary_store.moc"

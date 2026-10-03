//
// SummaryStore — az összefoglaló strukturált elérése: summary.json (új összefoglalók,
// metaadattal) és a régi, csak-markdown összefoglalók visszaolvasása.
//
#include <QtTest>
#include <QTemporaryDir>

#include "tanara/summary/SummaryStore.h"
#include "tanara/Types.h"

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
    // A Summary::renderMarkdown kimenete veszteség nélkül visszaolvasható.
    const Summary s = sample();
    const SummaryDocument doc = summarystore::parseMarkdown(s.renderMarkdown());
    QVERIFY(doc.exists);
    QVERIFY(doc.fromMarkdown);
    QCOMPARE(doc.meta.mode, SummaryMode::Quick);
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
    // És vissza: a visszanyert struktúra ugyanazt a markdownt adja.
    QCOMPARE(doc.summary.renderMarkdown(), s.renderMarkdown());
}

void SummaryStoreTest::topicsMarkdownParsed()
{
    // A komplex (témánkénti) összefoglaló formátuma, ahogy az AppController írja.
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

    const SummaryDocument doc = summarystore::parseMarkdown(md);
    QCOMPARE(doc.meta.mode, SummaryMode::Topics);
    QCOMPARE(doc.summary.execSummary, QStringLiteral("Röviden: jól haladunk."));
    QCOMPARE(doc.summary.actionItems.size(), 1);
    QCOMPARE(doc.summary.actionItems[0].owner, QStringLiteral("Lilla"));
    QCOMPARE(doc.summary.actionItems[0].due, QStringLiteral("okt. 20."));
    QCOMPARE(doc.topics.size(), 2);
    QCOMPARE(doc.topics[0].title, a1.title);                 // a sorszám lekerül
    QCOMPARE(doc.topics[0].detail, a1.detail);
    QCOMPARE(doc.topics[0].decisions, a1.decisions);
    QCOMPARE(doc.topics[0].actionItems.size(), 1);
    QCOMPARE(doc.topics[0].actionItems[0].text, QStringLiteral("Helyszín-lista véglegesítése"));
    QCOMPARE(doc.topics[1].title, a2.title);
    QCOMPARE(doc.topics[1].detail, a2.detail);
    QCOMPARE(doc.topics[1].decisions, a2.decisions);
    QVERIFY(doc.topics[1].actionItems.isEmpty());
    // A felső szintű döntés-lista a témák döntéseinek uniója (ismétlés nélkül).
    QCOMPARE(doc.summary.decisions,
             QStringList({"Negyedik helyszín novemberben.", "Az alapcsomag ára nem változik."}));
}

void SummaryStoreTest::tolerantParsing()
{
    // Kézzel szerkesztett / eltérő formájú markdown: más címsor-szint, ékezet nélküli és
    // nagybetűs címek, * bullet, kipipált teendő, ismeretlen szakasz, bevezető szöveg.
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
    const SummaryDocument doc = summarystore::parseMarkdown(md);
    QVERIFY(doc.exists);
    QVERIFY(doc.summary.execSummary.startsWith(QStringLiteral("Bevezető megjegyzés a fájl elején.")));
    QVERIFY(doc.summary.execSummary.contains(QStringLiteral("Első bekezdés,\nkét sorban.")));
    // Az ismeretlen szakasz szövege nem vész el.
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
    const SummaryDocument doc = summarystore::parseMarkdown(QStringLiteral("  \n\n"));
    QVERIFY(!doc.exists);
    QVERIFY(doc.summary.execSummary.isEmpty());
}

void SummaryStoreTest::loadNothing()
{
    QTemporaryDir dir;
    QVERIFY(!summarystore::load(dir.path()).exists);
    QVERIFY(!summarystore::load(QString()).exists);
    QVERIFY(!summarystore::save(dir.filePath("nincs-ilyen-mappa"), SummaryDocument{}));
}

void SummaryStoreTest::loadLegacyMarkdownOnly()
{
    // Régi meeting: csak summary.md van.
    QTemporaryDir dir;
    const Summary s = sample();
    write(summarystore::markdownPath(dir.path()), s.renderMarkdown());
    const SummaryDocument doc = summarystore::load(dir.path());
    QVERIFY(doc.exists);
    QVERIFY(doc.fromMarkdown);
    QCOMPARE(doc.summary.decisions, s.decisions);
    QCOMPARE(doc.summary.actionItems.size(), 4);
    QCOMPARE(doc.markdown, s.renderMarkdown());
    // Metaadat: amit őszintén tudni lehet — az idő a fájlé, a szolgáltató/modell ismeretlen.
    QCOMPARE(doc.meta.mode, SummaryMode::Quick);
    QVERIFY(doc.meta.createdAt.isValid());
    QVERIFY(qAbs(doc.meta.createdAt.secsTo(QDateTime::currentDateTime())) < 60);
    QVERIFY(doc.meta.providerId.isEmpty());
    QVERIFY(doc.meta.model.isEmpty());
}

void SummaryStoreTest::saveAndLoadJsonWithMeta()
{
    QTemporaryDir dir;
    SummaryDocument doc;
    doc.exists = true;
    doc.summary = sample();
    // Olyan teendő, amit a markdown-értelmező félreolvasna (zárójel a végén) — a json őrzi.
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
    doc.meta.mode = SummaryMode::Topics;

    write(summarystore::markdownPath(dir.path()), doc.summary.renderMarkdown());
    QVERIFY(summarystore::save(dir.path(), doc));
    QVERIFY(QFile::exists(summarystore::jsonPath(dir.path())));

    const SummaryDocument back = summarystore::load(dir.path());
    QVERIFY(back.exists);
    QVERIFY(!back.fromMarkdown);
    QCOMPARE(back.meta.createdAt, doc.meta.createdAt);
    QCOMPARE(back.meta.providerId, QStringLiteral("openai-compat"));
    QCOMPARE(back.meta.model, QStringLiteral("google/gemma-4-12b"));
    QCOMPARE(back.meta.mode, SummaryMode::Topics);
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

    QCOMPARE(summarystore::modeFromString(summarystore::modeToString(SummaryMode::Quick)), SummaryMode::Quick);
    QCOMPARE(summarystore::modeFromString(QStringLiteral("ismeretlen")), SummaryMode::Unknown);
}

void SummaryStoreTest::editedMarkdownWins()
{
    QTemporaryDir dir;
    SummaryDocument doc;
    doc.exists = true;
    doc.summary = sample();
    doc.meta.createdAt = QDateTime(QDate(2026, 9, 1), QTime(10, 0));
    doc.meta.providerId = QStringLiteral("openai-compat");
    doc.meta.model = QStringLiteral("m");
    doc.meta.mode = SummaryMode::Quick;
    write(summarystore::markdownPath(dir.path()), doc.summary.renderMarkdown());
    QVERIFY(summarystore::save(dir.path(), doc));

    // A felhasználó később kézzel átírja a summary.md-t (érdemben újabb a json-nál).
    Summary edited = sample();
    edited.decisions = {QStringLiteral("Kézzel átírt döntés.")};
    write(summarystore::markdownPath(dir.path()), edited.renderMarkdown());
    QFile md(summarystore::markdownPath(dir.path()));
    QVERIFY(md.open(QIODevice::ReadWrite));
    QVERIFY(md.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
    md.close();

    const SummaryDocument back = summarystore::load(dir.path());
    QVERIFY(back.fromMarkdown);
    QCOMPARE(back.summary.decisions, QStringList({"Kézzel átírt döntés."}));
    QCOMPARE(back.meta.model, QStringLiteral("m"));            // a keletkezés adatai megmaradnak
    QCOMPARE(back.meta.createdAt, doc.meta.createdAt);
}

QTEST_GUILESS_MAIN(SummaryStoreTest)
#include "test_summary_store.moc"

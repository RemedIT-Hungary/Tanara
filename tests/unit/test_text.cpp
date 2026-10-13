// foundation/text: the fold / slug / name helpers. Each variant keeps the behavior of the place it
// came from (library search, people search, imported track file names, export file names, tag names),
// so the cases below pin the differences on purpose.
#include <QtTest>

#include "tanara/foundation/text/Text.h"
#include "tanara/tags/TagNames.h"

using namespace tanara;

class TextTest : public QObject {
    Q_OBJECT
private slots:
    // Library search: length-preserving, lowercase, NFC first.
    void fold_keepsLength()
    {
        const QString in = QStringLiteral("Szűcs ÖDÖN");
        const QString f = text::fold(text::normalize(in));
        QCOMPARE(f, QStringLiteral("szucs odon"));
        QCOMPARE(f.size(), in.size());
        // A decomposed input becomes one character per letter after normalize().
        QCOMPARE(text::fold(text::normalize(QStringLiteral("Ö"))), QStringLiteral("o"));
        // Not case-FOLDED: the sharp s stays one character.
        QCOMPARE(text::fold(QStringLiteral("Straße")), QStringLiteral("straße"));
    }
    void foldQuery_simplifies() { QCOMPARE(text::foldQuery(QStringLiteral("  Ödön   Béla ")), QStringLiteral("odon bela")); }
    void find_phraseAndWords()
    {
        const QString t = text::fold(text::normalize(QStringLiteral("Kovács Lilla és Szűcs Ödön")));
        text::Range r = text::find(t, text::foldQuery(QStringLiteral("szucs odon")));
        QCOMPARE(r.start, 16);
        QCOMPARE(r.length, 10);
        r = text::find(t, text::foldQuery(QStringLiteral("odon kovacs")));   // every word, not as a phrase
        QCOMPARE(r.start, 22);
        QCOMPARE(r.length, 4);
        QVERIFY(!text::find(t, text::foldQuery(QStringLiteral("odon peter"))).isValid());
    }

    // People search: NFD + marks dropped + case-folded (simple folding; not length-preserving).
    void foldForSearch_caseFolds()
    {
        QCOMPARE(text::foldForSearch(QStringLiteral("Ödön ŐRÚT")), QStringLiteral("odon orut"));
        // A decomposed input loses its marks here; fold() (made for NFC input) keeps the length instead.
        QCOMPARE(text::foldForSearch(QStringLiteral("e\u0301")), QStringLiteral("e"));
        QCOMPARE(text::fold(QStringLiteral("e\u0301")).size(), 2);
        QCOMPARE(text::foldForSearch(QStringLiteral("Ö")), QStringLiteral("o"));
    }

    // Imported track files: ASCII only, lowercase, trimmed, limited, own fallback.
    void asciiSlug_importTracks()
    {
        QCOMPARE(text::asciiSlug(QStringLiteral("Ádám Béla – hívás"), 48, QStringLiteral("sav")),
                 QStringLiteral("adam-bela-hivas"));
        QCOMPARE(text::asciiSlug(QStringLiteral("  --Zoom (1)!  "), 48, QStringLiteral("sav")),
                 QStringLiteral("zoom-1"));
        QCOMPARE(text::asciiSlug(QStringLiteral("ﬁle"), 48, QStringLiteral("sav")), QStringLiteral("file"));
        QCOMPARE(text::asciiSlug(QStringLiteral("!!!"), 48, QStringLiteral("sav")), QStringLiteral("sav"));
        QCOMPARE(text::asciiSlug(QString(60, QLatin1Char('A')), 48, QStringLiteral("sav")).size(), 48);
        QCOMPARE(text::asciiSlug(QString(60, QLatin1Char('A')), -1, QStringLiteral("x")).size(), 60);
    }

    // Export file names: letters and digits kept as they are (accents, case), the rest '-'.
    void slug_keepsLetters()
    {
        QCOMPARE(text::slug(QStringLiteral("Ádám Béla: Q&A"), QStringLiteral("meeting")),
                 QStringLiteral("Ádám-Béla-Q-A"));
        QCOMPARE(text::slug(QStringLiteral(" x "), QStringLiteral("meeting")), QStringLiteral("-x-"));
        QCOMPARE(text::slug(QString(), QStringLiteral("meeting")), QStringLiteral("meeting"));
    }

    // Typed names (tags): NFC, whitespace simplified; the tag name also drops leading '#'.
    void normalizeName_andTagName()
    {
        QCOMPARE(text::normalizeName(QStringLiteral("  Museum \t  Plus ")), QStringLiteral("Museum Plus"));
        QCOMPARE(text::normalizeName(QStringLiteral("Ödön")), QStringLiteral("Ödön"));
        QCOMPARE(text::normalizeName(QStringLiteral("#tag")), QStringLiteral("#tag"));
        QCOMPARE(normalizeTagName(QStringLiteral(" # #  Museum   Plus ")), QStringLiteral("Museum Plus"));
        QCOMPARE(tagKey(QStringLiteral("Museum Plus")), tagKey(QStringLiteral("MuseumPlus")));
        QCOMPARE(tagKey(QStringLiteral("Ügyfél-találkozó")), QStringLiteral("ugyfeltalalkozo"));
    }
};

QTEST_GUILESS_MAIN(TextTest)
#include "test_text.moc"

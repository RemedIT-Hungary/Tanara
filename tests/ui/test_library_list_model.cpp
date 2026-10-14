// LibraryListModel + LibraryPendingModel: the view models of the library sidebar.
//  - in demo mode (without a controller) the made-up sample library: sections, accent-insensitive
//    search, snippet with the hit, filters, empty library;
//  - with a real AppController in an ISOLATED TANARA_HOME (QTemporaryDir): incremental updates
//    on core signals (insert / one row changes / delete, without a full reset).
#include "AppContext.h"
#include "LibraryDemoData.h"
#include "LibraryListModel.h"
#include "LibraryPendingModel.h"

#include "tanara/app/AppController.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

class TestLibraryListModel : public QObject {
    Q_OBJECT

    static QString text(const LibraryListModel& m, int row, int role)
    {
        return m.data(m.index(row), role).toString();
    }
    static QStringList list(const LibraryListModel& m, int row, int role)
    {
        return m.data(m.index(row), role).toStringList();
    }
    static int rowOf(const LibraryListModel& m, const QString& title)
    {
        for (int i = 0; i < m.count(); ++i)
            if (text(m, i, LibraryListModel::TitleRole) == title) return i;
        return -1;
    }
    static QStringList titles(const LibraryListModel& m)
    {
        QStringList out;
        for (int i = 0; i < m.count(); ++i)
            out << text(m, i, LibraryListModel::TitleRole);
        return out;
    }

    tanara::Meeting addMeeting(tanara::app::AppController& app, const QString& title, bool transcript,
                               const QString& said = QString())
    {
        tanara::Meeting m = app.store()->createMeeting(title);
        tanara::Track t;
        t.id = QStringLiteral("mic");
        t.file = QStringLiteral("track_mic.wav");
        t.active = true;
        m.tracks = {t};
        m.durationMs = 90 * 1000;
        if (transcript) {
            QFile seg(QDir(m.folder).filePath(QStringLiteral("transcript.segments.json")));
            if (seg.open(QIODevice::WriteOnly))
                seg.write(QJsonDocument(QJsonArray{QJsonObject{
                    {"startMs", 1000}, {"endMs", 4000}, {"speaker", "Beszélő 1"}, {"text", said}}}).toJson());
            m.hasTranscript = true;
        }
        app.store()->saveMeeting(m);
        return m;
    }

private slots:
    void init()
    {
        AppContext::instance()->setController(nullptr);
        AppContext::instance()->setDemo(true);
        demo::resetTags();
    }

    void demoLibraryHasSectionsAndStates()
    {
        LibraryListModel m;
        QVERIFY(m.count() >= 10);
        QCOMPARE(m.totalCount(), m.count());
        QVERIFY(!m.filtered());
        // Newest first, in the order of the design sample; the sections are relative to the demo's "today".
        QCOMPARE(text(m, 0, LibraryListModel::TitleRole), QStringLiteral("Termékcsapat heti egyeztetés"));
        QCOMPARE(text(m, 0, LibraryListModel::SectionRole), QStringLiteral("Ma"));
        QCOMPARE(text(m, 0, LibraryListModel::MetaRole), QStringLiteral("okt. 2. · 30 p"));
        QCOMPARE(text(m, 2, LibraryListModel::SectionRole), QStringLiteral("Tegnap"));
        QCOMPARE(text(m, 2, LibraryListModel::MetaRole), QStringLiteral("okt. 1. · 1 ó 16 p"));
        // Status icons: done / stale / failed / missing.
        QCOMPARE(text(m, 0, LibraryListModel::TranscriptStateRole), QStringLiteral("done"));
        QCOMPARE(text(m, 1, LibraryListModel::TranscriptStateRole), QStringLiteral("missing"));
        QCOMPARE(text(m, 2, LibraryListModel::SummaryStateRole), QStringLiteral("stale"));
        QCOMPARE(text(m, 2, LibraryListModel::IdentifyStateRole), QStringLiteral("done"));
        QCOMPARE(text(m, 3, LibraryListModel::TranscriptStateRole), QStringLiteral("error"));
        QVERIFY(!text(m, 3, LibraryListModel::TranscriptTipRole).isEmpty());
        // Without a search the title comes in one piece (no highlight, no snippet).
        QCOMPARE(text(m, 0, LibraryListModel::TitleMatchRole), QString());
        QCOMPARE(text(m, 0, LibraryListModel::TitleAfterRole), text(m, 0, LibraryListModel::TitleRole));
        QVERIFY(!m.data(m.index(0), LibraryListModel::HasSnippetRole).toBool());
    }

    void demoSearchIsAccentInsensitiveAndHighlights()
    {
        LibraryListModel m;
        m.setSearchText(QStringLiteral("DEMO"));   // also finds "demo"
        m.refreshNow();
        QVERIFY(m.filtered());
        QCOMPARE(m.count(), 3);
        QVERIFY(m.resultText().startsWith(QStringLiteral("3 ")));
        for (int i = 0; i < m.count(); ++i) {
            QVERIFY(m.data(m.index(i), LibraryListModel::HasSnippetRole).toBool());
            QCOMPARE(text(m, i, LibraryListModel::SnippetMatchRole), QStringLiteral("demó"));
            // The hit shows in the narrow sidebar too: only a few words remain before it.
            QVERIFY(text(m, i, LibraryListModel::SnippetBeforeRole).size() <= 20);
            // A filtered list has no date section.
            QCOMPARE(text(m, i, LibraryListModel::SectionRole), QString());
        }
        // Title hit in three pieces.
        m.setSearchText(QStringLiteral("partner"));
        m.refreshNow();
        QCOMPARE(m.count(), 1);
        QCOMPARE(text(m, 0, LibraryListModel::TitleBeforeRole), QStringLiteral("Negyedéves "));
        QCOMPARE(text(m, 0, LibraryListModel::TitleMatchRole), QStringLiteral("partner"));
        QCOMPARE(text(m, 0, LibraryListModel::TitleAfterRole), QStringLiteral("találkozó"));

        m.setSearchText(QStringLiteral("nincs ilyen szó"));
        m.refreshNow();
        QCOMPARE(m.count(), 0);
        QVERIFY(m.totalCount() > 0);   // the library is not empty, there is just no hit
        m.clearFilters();
        QVERIFY(!m.filtered());
        QCOMPARE(m.count(), m.totalCount());
    }

    void demoFiltersCombine()
    {
        LibraryListModel m;
        const int all = m.count();
        m.setNoTranscript(true);
        QCOMPARE(titles(m), (QStringList{QStringLiteral("Ügyféltámogatás átadás"),
                                         QStringLiteral("Termékbemutató, 2. kör")}));
        m.setNoTranscript(false);
        m.setNoSummary(true);
        const int withoutSummary = m.count();
        QVERIFY(withoutSummary > 0 && withoutSummary < all);
        m.addPerson(QStringLiteral("Varga Nóra"));
        QVERIFY(m.count() > 0 && m.count() < withoutSummary);
        QCOMPARE(m.people(), QStringList{QStringLiteral("Varga Nóra")});
        m.removePerson(QStringLiteral("Varga Nóra"));
        QCOMPARE(m.count(), withoutSummary);

        // The person list comes by frequency, with a stable color index.
        const QVariantList people = m.peopleOptions();
        QVERIFY(people.size() >= 4);
        QVERIFY(people.at(0).toMap().value("count").toInt() >= people.at(1).toMap().value("count").toInt());
        QCOMPARE(m.personColorIndex(people.at(1).toMap().value("name").toString()), 1);
    }

    // ---- tags ----
    void tagLineFitsAboutTwentyFourCharacters()
    {
        QCOMPARE(LibraryListModel::tagLine({}), QStringList());
        QCOMPARE(LibraryListModel::tagLine({QStringLiteral("Nordvik")}), QStringList{QStringLiteral("#Nordvik")});
        // The design's examples: what does not fit becomes "+N".
        QCOMPARE(LibraryListModel::tagLine({QStringLiteral("Nordvik"), QStringLiteral("Partnerek"),
                                            QStringLiteral("Q4 tervezés")}),
                 (QStringList{QStringLiteral("#Nordvik"), QStringLiteral("#Partnerek"), QStringLiteral("+1")}));
        QCOMPARE(LibraryListModel::tagLine({QStringLiteral("ArchivePlus"), QStringLiteral("MÉM-MDK"),
                                            QStringLiteral("Gyűjteménykezelés")}),
                 (QStringList{QStringLiteral("#ArchivePlus"), QStringLiteral("#MÉM-MDK"), QStringLiteral("+1")}));
        // The first tag always shows, however long it is.
        QCOMPARE(LibraryListModel::tagLine({QStringLiteral("Gyűjteménykezelési munkacsoport 2026"),
                                            QStringLiteral("Belső")}),
                 (QStringList{QStringLiteral("#Gyűjteménykezelési munkacsoport 2026"), QStringLiteral("+1")}));
    }

    void demoRowsCarryTags()
    {
        LibraryListModel m;
        const int bemutato = rowOf(m, QStringLiteral("Termékbemutató, 2. kör"));
        QVERIFY(bemutato >= 0);
        QCOMPARE(list(m, bemutato, LibraryListModel::TagIdsRole),
                 (QStringList{QStringLiteral("t-muzeo"), QStringLiteral("t-mem"), QStringLiteral("t-gyujtemeny")}));
        QCOMPARE(list(m, bemutato, LibraryListModel::TagNamesRole).first(), QStringLiteral("Muzeo"));
        QCOMPARE(list(m, bemutato, LibraryListModel::TagLineRole).last(), QStringLiteral("+1"));
        // A row without tags: an empty third line.
        const int arazas = rowOf(m, QStringLiteral("Árazás egyeztetés"));
        QVERIFY(list(m, arazas, LibraryListModel::TagLineRole).isEmpty());
        QVERIFY(!m.data(m.index(arazas), LibraryListModel::TagMatchRole).toBool());
    }

    void demoTagFiltersAnyAllUntagged()
    {
        LibraryListModel m;
        QSignalSpy chips(&m, &LibraryListModel::tagFilterItemsChanged);
        m.addTag(QStringLiteral("t-nordvik"));
        QVERIFY(m.filtered());
        QCOMPARE(chips.count(), 1);
        QCOMPARE(titles(m), (QStringList{QStringLiteral("Ügyféltámogatás átadás"),
                                         QStringLiteral("Negyedéves partnertalálkozó"),
                                         QStringLiteral("Nordvik heti meeting")}));
        QCOMPARE(m.resultText(), QStringLiteral("3 megbeszélés a szűrő szerint"));
        // Any (OR): Partners adds nothing new; All (AND): only the partner meeting.
        m.addTag(QStringLiteral("t-partnerek"));
        QCOMPARE(m.count(), 3);
        const QVariantList items = m.tagFilterItems();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(1).toMap().value("name").toString(), QStringLiteral("Partnerek"));
        m.setTagsAll(true);
        QCOMPARE(titles(m), QStringList{QStringLiteral("Negyedéves partnertalálkozó")});
        // Untagged meetings are combined with the tags by OR.
        m.setTagsAll(false);
        m.removeTag(QStringLiteral("t-partnerek"));
        m.toggleTag(QStringLiteral("t-nordvik"));
        QVERIFY(m.tags().isEmpty());
        m.setUntagged(true);
        QCOMPARE(titles(m), (QStringList{QStringLiteral("Árazás egyeztetés"),
                                         QStringLiteral("Vezetői egyeztetés")}));
        m.addTag(QStringLiteral("t-belso"));
        QCOMPARE(m.count(), 5);
        // "Clear filters" also removes the tags.
        m.clearFilters();
        QVERIFY(!m.filtered());
        QVERIFY(m.tags().isEmpty());
        QVERIFY(!m.untagged());
        QCOMPARE(m.count(), m.totalCount());
    }

    void demoTagOptionsAndUntaggedCount()
    {
        LibraryListModel m;
        QCOMPARE(m.untaggedCount(), 2);
        const QVariantList opts = m.tagOptions();
        QCOMPARE(opts.size(), 12);                       // the full set; unused ones with 0
        // By frequency, ties alphabetical.
        QCOMPARE(opts.at(0).toMap().value("name").toString(), QStringLiteral("Belső"));
        QCOMPARE(opts.at(0).toMap().value("count").toInt(), 3);
        QCOMPARE(opts.last().toMap().value("count").toInt(), 0);
        // The selected ones first.
        m.addTag(QStringLiteral("t-partnerek"));
        QCOMPARE(m.tagOptions().at(0).toMap().value("id").toString(), QStringLiteral("t-partnerek"));
        // Search in the popover: accent- and case-insensitive substring.
        const QVariantList nord = m.tagOptionsMatching(QStringLiteral("NORD"));
        QCOMPARE(nord.size(), 2);
        QCOMPARE(m.tagOptionsMatching(QStringLiteral("gyujtemeny")).size(), 1);
        QCOMPARE(m.tagName(QStringLiteral("t-q4")), QStringLiteral("Q4 tervezés"));
    }

    void demoSearchFindsTagNames()
    {
        LibraryListModel m;
        m.setSearchText(QStringLiteral("nordvik"));
        m.refreshNow();
        QCOMPARE(m.count(), 4);
        QCOMPARE(m.resultText(), QStringLiteral("4 találat · címben, címkében, átiratban"));
        // Found only because of the tag: no snippet, the tag hit is the row's third line.
        const int tamogatas = rowOf(m, QStringLiteral("Ügyféltámogatás átadás"));
        QVERIFY(m.data(m.index(tamogatas), LibraryListModel::TagMatchRole).toBool());
        QCOMPARE(text(m, tamogatas, LibraryListModel::TagMatchNameRole), QStringLiteral("Nordvik"));
        QVERIFY(!m.data(m.index(tamogatas), LibraryListModel::HasSnippetRole).toBool());
        // Transcript hit: snippet, without a tag hit.
        const int arazas = rowOf(m, QStringLiteral("Árazás egyeztetés"));
        QVERIFY(m.data(m.index(arazas), LibraryListModel::HasSnippetRole).toBool());
        QVERIFY(!m.data(m.index(arazas), LibraryListModel::TagMatchRole).toBool());
        // Title hit (its tag matches too).
        const int heti = rowOf(m, QStringLiteral("Nordvik heti meeting"));
        QCOMPARE(text(m, heti, LibraryListModel::TitleMatchRole), QStringLiteral("Nordvik"));
        // Only the places that actually occur go into the counter row.
        m.setSearchText(QStringLiteral("muzeo"));
        m.refreshNow();
        QCOMPARE(m.resultText(), QStringLiteral("1 találat · címkében"));
    }

    void forceEmptyGivesEmptyLibrary()
    {
        LibraryListModel m;
        QSignalSpy counts(&m, &LibraryListModel::countChanged);
        m.setForceEmpty(true);
        QCOMPARE(m.count(), 0);
        QCOMPARE(m.totalCount(), 0);
        QVERIFY(counts.count() >= 1);
        QVERIFY(m.peopleOptions().isEmpty());
    }

    void noControllerAndNoDemoIsEmpty()
    {
        AppContext::instance()->setDemo(false);
        LibraryListModel m;
        QCOMPARE(m.count(), 0);
        LibraryPendingModel p;
        QVERIFY(p.items().isEmpty());
    }

    void pendingItemsFromDemo()
    {
        LibraryPendingModel p;
        const QVariantList items = p.items();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(0).toMap().value("kind").toString(), QStringLiteral("awaiting"));
        QVERIFY(items.at(0).toMap().value("title").toString().startsWith(QStringLiteral("1 ")));
        QCOMPARE(items.at(1).toMap().value("kind").toString(), QStringLiteral("stale"));
        QCOMPARE(items.at(1).toMap().value("tab").toInt(), 2);   // the Executive summary tab
        QCOMPARE(items.at(2).toMap().value("kind").toString(), QStringLiteral("failed"));
        QCOMPARE(items.at(2).toMap().value("tone").toString(), QStringLiteral("danger"));
    }

    void pendingItemsGroupAndLimit()
    {
        QVector<tanara::library::PendingItem> in;
        for (int i = 0; i < 5; ++i) {
            tanara::library::PendingItem a;
            a.kind = tanara::library::PendingKind::AwaitingTranscription;
            a.meetingId = QStringLiteral("a%1").arg(i);
            a.title = QStringLiteral("Váró %1").arg(i);
            in << a;
            tanara::library::PendingItem f;
            f.kind = tanara::library::PendingKind::TranscriptionFailed;
            f.meetingId = QStringLiteral("f%1").arg(i);
            f.title = QStringLiteral("Bukott %1").arg(i);
            f.error.message = QStringLiteral("Hiba");
            in << f;
        }
        const QVariantList out = LibraryPendingModel::build(in);
        // Meetings awaiting transcription in ONE row (opens the newest), at most 3 of the failed ones.
        QCOMPARE(out.size(), 4);
        QCOMPARE(out.at(0).toMap().value("meetingId").toString(), QStringLiteral("a0"));
        QVERIFY(out.at(0).toMap().value("title").toString().startsWith(QStringLiteral("5 ")));
        QCOMPARE(out.at(1).toMap().value("subtitle").toString(), QStringLiteral("Bukott 0 · Hiba"));
        QVERIFY(LibraryPendingModel::build({}).isEmpty());
    }

    // ---- real controller, isolated TANARA_HOME ----
    void realLibraryUpdatesIncrementally()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("TANARA_HOME", home.path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        AppContext::instance()->setDemo(false);
        {
            tanara::app::AppController app;
            QVERIFY(app.store()->audioDir().startsWith(home.path()));
            const tanara::Meeting first = addMeeting(app, QStringLiteral("Első megbeszélés"), true,
                                                     QStringLiteral("Az árajánlatot Ödön küldi el holnap."));

            LibraryListModel m;
            m.setController(&app);
            LibraryPendingModel pending;
            pending.setController(&app);
            QCOMPARE(m.count(), 1);
            QCOMPARE(m.totalCount(), 1);
            QCOMPARE(text(m, 0, LibraryListModel::TranscriptStateRole), QStringLiteral("done"));
            QVERIFY(pending.items().isEmpty());

            QSignalSpy resets(&m, &QAbstractItemModel::modelReset);
            QSignalSpy inserts(&m, &QAbstractItemModel::rowsInserted);
            QSignalSpy removes(&m, &QAbstractItemModel::rowsRemoved);
            QSignalSpy changes(&m, &QAbstractItemModel::dataChanged);

            // New recording: one row inserted (no reset), and it appears in "These are waiting for you".
            const tanara::Meeting second = addMeeting(app, QStringLiteral("Második megbeszélés"), false);
            QTRY_COMPARE(m.count(), 2);
            QCOMPARE(inserts.count(), 1);
            QCOMPARE(resets.count(), 0);
            QCOMPARE(m.indexOfMeeting(second.id), 0);   // newest first
            QCOMPARE(text(m, 0, LibraryListModel::TranscriptStateRole), QStringLiteral("missing"));
            QTRY_COMPARE(pending.items().size(), 1);
            QCOMPARE(pending.items().at(0).toMap().value("meetingId").toString(), second.id);

            // Rename: dataChanged only for that row.
            changes.clear();
            app.renameMeeting(first.id, QStringLiteral("Átnevezett megbeszélés"));
            QTRY_COMPARE(m.titleOf(first.id), QStringLiteral("Átnevezett megbeszélés"));
            QCOMPARE(resets.count(), 0);
            QVERIFY(changes.count() >= 1);
            QCOMPARE(changes.last().at(0).toModelIndex().row(), m.indexOfMeeting(first.id));

            // Search in the transcript text, accent-insensitive (a query without accents matches the accented name).
            m.setSearchText(QStringLiteral("odon"));
            m.refreshNow();
            QCOMPARE(m.count(), 1);
            QCOMPARE(m.meetingIdAt(0), first.id);
            QCOMPARE(text(m, 0, LibraryListModel::SnippetMatchRole), QStringLiteral("Ödön"));
            QCOMPARE(m.data(m.index(0), LibraryListModel::SnippetMsRole).toInt(), 1000);
            m.clearFilters();
            QCOMPARE(m.count(), 2);

            // Delete: one row removed.
            resets.clear();
            removes.clear();
            app.deleteMeeting(second.id);
            QTRY_COMPARE(m.count(), 1);
            QCOMPARE(removes.count(), 1);
            QCOMPARE(resets.count(), 0);
            QTRY_VERIFY(pending.items().isEmpty());
        }
        qunsetenv("TANARA_HOME");
    }

    void realLibraryTagsFilterAndSearch()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("TANARA_HOME", home.path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        AppContext::instance()->setDemo(false);
        {
            tanara::app::AppController app;
            const tanara::Meeting a = addMeeting(app, QStringLiteral("Heti egyeztetés"), true,
                                                 QStringLiteral("A határidőt egy héttel később tartjuk."));
            const tanara::Meeting b = addMeeting(app, QStringLiteral("Ügyfélhívás"), false);
            const tanara::Meeting c = addMeeting(app, QStringLiteral("Tervezés"), false);
            tanara::library::TagService* tags = app.tags();
            const tanara::Tag alfa = tags->addTag(a.id, QStringLiteral("Alfa Projekt"));
            tags->addTag(b.id, QStringLiteral("Alfa Projekt"));
            const tanara::Tag beta = tags->addTag(b.id, QStringLiteral("Béta"));

            LibraryListModel m;
            m.setController(&app);
            QTRY_COMPARE(m.count(), 3);
            const int rowB = m.indexOfMeeting(b.id);
            QCOMPARE(list(m, rowB, LibraryListModel::TagNamesRole),
                     (QStringList{QStringLiteral("Alfa Projekt"), QStringLiteral("Béta")}));
            QCOMPARE(m.untaggedCount(), 1);
            QCOMPARE(m.tagOptions().at(0).toMap().value("name").toString(), QStringLiteral("Alfa Projekt"));
            QCOMPARE(m.tagOptions().at(0).toMap().value("count").toInt(), 2);
            QCOMPARE(m.tagName(beta.id), QStringLiteral("Béta"));

            m.setTags({alfa.id, beta.id});
            QCOMPARE(m.count(), 2);
            m.setTagsAll(true);
            QCOMPARE(m.count(), 1);
            QCOMPARE(m.meetingIdAt(0), b.id);
            m.clearFilters();
            m.setUntagged(true);
            QCOMPARE(m.count(), 1);
            QCOMPARE(m.meetingIdAt(0), c.id);
            m.clearFilters();

            // Search in the tag name (accent-insensitive): the row comes as a tag hit.
            m.setSearchText(QStringLiteral("beta"));
            m.refreshNow();
            QCOMPARE(m.count(), 1);
            QVERIFY(m.data(m.index(0), LibraryListModel::TagMatchRole).toBool());
            QCOMPARE(text(m, 0, LibraryListModel::TagMatchNameRole), QStringLiteral("Béta"));
            QCOMPARE(m.resultText(), QStringLiteral("1 találat · címkében"));
            m.clearFilters();

            // A meeting's tag changes: the row updates (without a reset).
            QSignalSpy resets(&m, &QAbstractItemModel::modelReset);
            tags->addTag(c.id, QStringLiteral("Béta"));
            QTRY_COMPARE(list(m, m.indexOfMeeting(c.id), LibraryListModel::TagNamesRole),
                         QStringList{QStringLiteral("Béta")});
            QCOMPARE(resets.count(), 0);
            // Rename: the rows' tag names too.
            tags->rename(beta.id, QStringLiteral("Gamma"));
            QTRY_COMPARE(list(m, m.indexOfMeeting(c.id), LibraryListModel::TagNamesRole),
                         QStringList{QStringLiteral("Gamma")});
        }
        qunsetenv("TANARA_HOME");
    }

    void manyMeetingsStayFast()
    {
        // A few hundred meetings: the query + building the model and a search stay fast.
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("TANARA_HOME", home.path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        AppContext::instance()->setDemo(false);
        {
            tanara::app::AppController app;
            for (int i = 0; i < 300; ++i)
                addMeeting(app, QStringLiteral("Megbeszélés %1").arg(i), i % 2 == 0,
                           QStringLiteral("A %1. alkalommal a költségvetésről beszéltünk.").arg(i));
            LibraryListModel m;
            m.setController(&app);
            QCOMPARE(m.count(), 300);
            QElapsedTimer t;
            t.start();
            m.setSearchText(QStringLiteral("koltsegvetes"));
            m.refreshNow();                    // the first search loads the transcripts
            QCOMPARE(m.count(), 150);
            m.setSearchText(QStringLiteral("koltsegvetesrol"));
            m.refreshNow();
            QCOMPARE(m.count(), 150);
            m.clearFilters();
            QCOMPARE(m.count(), 300);
            QVERIFY2(t.elapsed() < 3000, qPrintable(QString::number(t.elapsed())));
        }
        qunsetenv("TANARA_HOME");
    }
};

QTEST_MAIN(TestLibraryListModel)
#include "test_library_list_model.moc"

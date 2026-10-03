// LibraryListModel + LibraryPendingModel — a könyvtár-oldalsáv nézetmodelljei.
//  - demó-módban (controller nélkül) a kitalált mintakönyvtár: szekciók, keresés ékezet
//    nélkül, kivonat a találattal, szűrők, üres könyvtár;
//  - valódi AppControllerrel, IZOLÁLT TANARA_HOME-ban (QTemporaryDir): a core jeleire
//    inkrementális frissítés (beszúrás / egy sor változása / törlés — teljes reset nélkül).
#include "AppContext.h"
#include "LibraryListModel.h"
#include "LibraryPendingModel.h"

#include "tanara/AppController.h"
#include "tanara/store/MeetingStore.h"

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
    static QStringList titles(const LibraryListModel& m)
    {
        QStringList out;
        for (int i = 0; i < m.count(); ++i)
            out << text(m, i, LibraryListModel::TitleRole);
        return out;
    }

    tanara::Meeting addMeeting(tanara::AppController& app, const QString& title, bool transcript,
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
    }

    void demoLibraryHasSectionsAndStates()
    {
        LibraryListModel m;
        QVERIFY(m.count() >= 10);
        QCOMPARE(m.totalCount(), m.count());
        QVERIFY(!m.filtered());
        // Legújabb elöl, a design-minta sorrendjében; a szekciók a demó „mai napjához” mértek.
        QCOMPARE(text(m, 0, LibraryListModel::TitleRole), QStringLiteral("Termékcsapat heti egyeztetés"));
        QCOMPARE(text(m, 0, LibraryListModel::SectionRole), QStringLiteral("Ma"));
        QCOMPARE(text(m, 0, LibraryListModel::MetaRole), QStringLiteral("okt. 2. · 30 p"));
        QCOMPARE(text(m, 2, LibraryListModel::SectionRole), QStringLiteral("Tegnap"));
        QCOMPARE(text(m, 2, LibraryListModel::MetaRole), QStringLiteral("okt. 1. · 1 ó 16 p"));
        // Állapot-ikonok: kész / elavult / hibás / hiányzó.
        QCOMPARE(text(m, 0, LibraryListModel::TranscriptStateRole), QStringLiteral("done"));
        QCOMPARE(text(m, 1, LibraryListModel::TranscriptStateRole), QStringLiteral("missing"));
        QCOMPARE(text(m, 2, LibraryListModel::SummaryStateRole), QStringLiteral("stale"));
        QCOMPARE(text(m, 2, LibraryListModel::IdentifyStateRole), QStringLiteral("done"));
        QCOMPARE(text(m, 3, LibraryListModel::TranscriptStateRole), QStringLiteral("error"));
        QVERIFY(!text(m, 3, LibraryListModel::TranscriptTipRole).isEmpty());
        // Keresés nélkül a cím egy darabban jön (nincs kiemelés, nincs kivonat).
        QCOMPARE(text(m, 0, LibraryListModel::TitleMatchRole), QString());
        QCOMPARE(text(m, 0, LibraryListModel::TitleAfterRole), text(m, 0, LibraryListModel::TitleRole));
        QVERIFY(!m.data(m.index(0), LibraryListModel::HasSnippetRole).toBool());
    }

    void demoSearchIsAccentInsensitiveAndHighlights()
    {
        LibraryListModel m;
        m.setSearchText(QStringLiteral("DEMO"));   // „demó”-t is megtalálja
        m.refreshNow();
        QVERIFY(m.filtered());
        QCOMPARE(m.count(), 3);
        QVERIFY(m.resultText().startsWith(QStringLiteral("3 ")));
        for (int i = 0; i < m.count(); ++i) {
            QVERIFY(m.data(m.index(i), LibraryListModel::HasSnippetRole).toBool());
            QCOMPARE(text(m, i, LibraryListModel::SnippetMatchRole), QStringLiteral("demó"));
            // A találat a keskeny oldalsávban is látszik: előtte legfeljebb pár szó marad.
            QVERIFY(text(m, i, LibraryListModel::SnippetBeforeRole).size() <= 20);
            // Szűrt listában nincs dátum-szekció.
            QCOMPARE(text(m, i, LibraryListModel::SectionRole), QString());
        }
        // Cím-találat három darabban.
        m.setSearchText(QStringLiteral("partner"));
        m.refreshNow();
        QCOMPARE(m.count(), 1);
        QCOMPARE(text(m, 0, LibraryListModel::TitleBeforeRole), QStringLiteral("Negyedéves "));
        QCOMPARE(text(m, 0, LibraryListModel::TitleMatchRole), QStringLiteral("partner"));
        QCOMPARE(text(m, 0, LibraryListModel::TitleAfterRole), QStringLiteral("találkozó"));

        m.setSearchText(QStringLiteral("nincs ilyen szó"));
        m.refreshNow();
        QCOMPARE(m.count(), 0);
        QVERIFY(m.totalCount() > 0);   // a könyvtár nem üres, csak nincs találat
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

        // A személy-lista gyakoriság szerint jön, stabil színindexszel.
        const QVariantList people = m.peopleOptions();
        QVERIFY(people.size() >= 4);
        QVERIFY(people.at(0).toMap().value("count").toInt() >= people.at(1).toMap().value("count").toInt());
        QCOMPARE(m.personColorIndex(people.at(1).toMap().value("name").toString()), 1);
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
        QCOMPARE(items.at(1).toMap().value("tab").toInt(), 1);
        QCOMPARE(items.at(2).toMap().value("kind").toString(), QStringLiteral("failed"));
        QCOMPARE(items.at(2).toMap().value("tone").toString(), QStringLiteral("danger"));
    }

    void pendingItemsGroupAndLimit()
    {
        QVector<tanara::PendingItem> in;
        for (int i = 0; i < 5; ++i) {
            tanara::PendingItem a;
            a.kind = tanara::PendingKind::AwaitingTranscription;
            a.meetingId = QStringLiteral("a%1").arg(i);
            a.title = QStringLiteral("Váró %1").arg(i);
            in << a;
            tanara::PendingItem f;
            f.kind = tanara::PendingKind::TranscriptionFailed;
            f.meetingId = QStringLiteral("f%1").arg(i);
            f.title = QStringLiteral("Bukott %1").arg(i);
            f.error.message = QStringLiteral("Hiba");
            in << f;
        }
        const QVariantList out = LibraryPendingModel::build(in);
        // Az átírásra várók EGY sorban (a legfrissebbet nyitja), a bukottakból legfeljebb 3.
        QCOMPARE(out.size(), 4);
        QCOMPARE(out.at(0).toMap().value("meetingId").toString(), QStringLiteral("a0"));
        QVERIFY(out.at(0).toMap().value("title").toString().startsWith(QStringLiteral("5 ")));
        QCOMPARE(out.at(1).toMap().value("subtitle").toString(), QStringLiteral("Bukott 0 · Hiba"));
        QVERIFY(LibraryPendingModel::build({}).isEmpty());
    }

    // ---- valódi controller, izolált TANARA_HOME ----
    void realLibraryUpdatesIncrementally()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("TANARA_HOME", home.path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        AppContext::instance()->setDemo(false);
        {
            tanara::AppController app;
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

            // Új felvétel → egy sor beszúrása (nincs reset), és megjelenik az „Ezek várnak rád”-ban.
            const tanara::Meeting second = addMeeting(app, QStringLiteral("Második megbeszélés"), false);
            QTRY_COMPARE(m.count(), 2);
            QCOMPARE(inserts.count(), 1);
            QCOMPARE(resets.count(), 0);
            QCOMPARE(m.indexOfMeeting(second.id), 0);   // legújabb elöl
            QCOMPARE(text(m, 0, LibraryListModel::TranscriptStateRole), QStringLiteral("missing"));
            QTRY_COMPARE(pending.items().size(), 1);
            QCOMPARE(pending.items().at(0).toMap().value("meetingId").toString(), second.id);

            // Átnevezés → csak annak a sornak a dataChanged-je.
            changes.clear();
            app.renameMeeting(first.id, QStringLiteral("Átnevezett megbeszélés"));
            QTRY_COMPARE(m.titleOf(first.id), QStringLiteral("Átnevezett megbeszélés"));
            QCOMPARE(resets.count(), 0);
            QVERIFY(changes.count() >= 1);
            QCOMPARE(changes.last().at(0).toModelIndex().row(), m.indexOfMeeting(first.id));

            // Keresés az átirat szövegében, ékezet nélkül („odon” → „Ödön”).
            m.setSearchText(QStringLiteral("odon"));
            m.refreshNow();
            QCOMPARE(m.count(), 1);
            QCOMPARE(m.meetingIdAt(0), first.id);
            QCOMPARE(text(m, 0, LibraryListModel::SnippetMatchRole), QStringLiteral("Ödön"));
            QCOMPARE(m.data(m.index(0), LibraryListModel::SnippetMsRole).toInt(), 1000);
            m.clearFilters();
            QCOMPARE(m.count(), 2);

            // Törlés → egy sor eltávolítása.
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

    void manyMeetingsStayFast()
    {
        // Pár száz megbeszélés: a lekérdezés + a modell felépítése és egy keresés gyors marad.
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("TANARA_HOME", home.path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        AppContext::instance()->setDemo(false);
        {
            tanara::AppController app;
            for (int i = 0; i < 300; ++i)
                addMeeting(app, QStringLiteral("Megbeszélés %1").arg(i), i % 2 == 0,
                           QStringLiteral("A %1. alkalommal a költségvetésről beszéltünk.").arg(i));
            LibraryListModel m;
            m.setController(&app);
            QCOMPARE(m.count(), 300);
            QElapsedTimer t;
            t.start();
            m.setSearchText(QStringLiteral("koltsegvetes"));
            m.refreshNow();                    // az első keresés tölti be az átiratokat
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

//
// MeetingLibrary + TextFold — a könyvtár-oldalsáv lekérdezései: dátum-szekciók, ékezet- és
// kisbetű-független teljes szöveges keresés (cím + átirat) kivonattal és találat-hellyel,
// szűrők, „Ezek várnak rád”, inkrementális jelek, gyorsítótár-érvénytelenítés.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/TextFold.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/TagService.h"

using namespace tanara;

class LibraryTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void foldKeepsLengthAndStripsAccents();
    void findPhraseAndWords();
    void snippetAroundMatch();

    void sections();
    void listNewestFirstWithSections();
    void searchTitleAccentInsensitive();
    void searchTranscriptSnippet();
    void searchNoHit();
    void filters();
    void people();
    void pendingItems();
    void incrementalSignals();
    void transcriptChangeInvalidatesCache();
    void manyMeetingsStayResponsive();
    void warmUpLoadsInBackground();
    void tagsInEntriesAndFilter();
    void searchInTagNames();
    void tagOptionsAndUntagged();

private:
    struct Seg { qint64 ms; QString speaker; QString text; };
    Meeting add(const QString& title, const QDateTime& when, const QVector<Seg>& segs = {},
                bool summary = false, const QMap<QString, QString>& speakers = {});
    void writeSegments(const Meeting& m, const QVector<Seg>& segs);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<MeetingStore> m_store;
    const QDateTime m_now{QDate(2026, 10, 1), QTime(15, 0)};   // csütörtök
};

void LibraryTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<MeetingStore>(m_dir->filePath("rec"), m_dir->filePath("meta"));
}

void LibraryTest::cleanup()
{
    m_store.reset();
    m_dir.reset();
}

void LibraryTest::writeSegments(const Meeting& m, const QVector<Seg>& segs)
{
    QJsonArray arr;
    for (const Seg& s : segs) {
        QJsonObject o;
        o["startMs"] = double(s.ms);
        o["endMs"] = double(s.ms + 1000);
        o["speaker"] = s.speaker;
        o["text"] = s.text;
        arr.append(o);
    }
    QFile f(QDir(m.folder).filePath("transcript.segments.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(arr).toJson());
}

Meeting LibraryTest::add(const QString& title, const QDateTime& when, const QVector<Seg>& segs,
                         bool summary, const QMap<QString, QString>& speakers)
{
    Meeting m = m_store->createMeeting(title);
    m.startedAt = when;
    m.durationMs = 30 * 60 * 1000;
    m.hasTranscript = !segs.isEmpty();
    m.hasSummary = summary;
    m.speakerMap = speakers;
    Track mic;
    mic.id = "mic"; mic.kind = TrackKind::Mic; mic.file = "track_mic.ogg";
    mic.speakerLabel = "Ádám"; mic.fixedSpeaker = true;
    m.tracks.append(mic);
    if (!segs.isEmpty()) writeSegments(m, segs);
    m_store->saveMeeting(m);
    return m;
}

// ---- TextFold ---------------------------------------------------------------------------

void LibraryTest::foldKeepsLengthAndStripsAccents()
{
    const QString src = QStringLiteral("Ödön ÁRVÍZTŰRŐ tükörfúrógép — Ǘ ß 😀");
    const QString folded = textfold::fold(textfold::normalize(src));
    QCOMPARE(folded.size(), textfold::normalize(src).size());   // 1:1 hossz → a pozíciók egyeznek
    QVERIFY(folded.startsWith(QStringLiteral("odon arvizturo tukorfurogep")));
    QVERIFY(folded.contains(QStringLiteral(" u ")));             // Ǘ → u (többszörös ékezet)
    // Felbontott (NFD) bemenet: a normalize olvasztja össze, így is illeszkedik.
    const QString nfd = QStringLiteral("Ödön");
    QCOMPARE(textfold::fold(textfold::normalize(nfd)), QStringLiteral("odon"));
    QCOMPARE(textfold::foldQuery(QStringLiteral("  ÖDÖN   úr ")), QStringLiteral("odon ur"));
}

void LibraryTest::findPhraseAndWords()
{
    const QString text = textfold::fold(QStringLiteral("Az árajánlatot Ödön küldi el pénteken."));
    textfold::Range r = textfold::find(text, textfold::foldQuery("odon"));
    QVERIFY(r.isValid());
    QCOMPARE(r.start, 15);
    QCOMPARE(r.length, 4);
    // Több szó: előbb a teljes kifejezés …
    r = textfold::find(text, textfold::foldQuery("Ödön küldi"));
    QCOMPARE(r.start, 15);
    QCOMPARE(r.length, 10);
    // … ha az nincs, de minden szó megvan: az első szó helye.
    r = textfold::find(text, textfold::foldQuery("pénteken árajánlat"));
    QCOMPARE(r.start, int(text.indexOf("penteken")));
    QCOMPARE(r.length, 8);
    QVERIFY(!textfold::find(text, textfold::foldQuery("ödön szerdán")).isValid());
    QVERIFY(!textfold::find(text, QString()).isValid());
}

void LibraryTest::snippetAroundMatch()
{
    const QString text = QStringLiteral(
        "Először átnéztük a múlt heti feladatokat, aztán rátértünk a lényegre.\n"
        "Az árajánlatot Ödön küldi el pénteken, a szerződést pedig Lilla nézi át hétfőn, "
        "utána egyeztetünk a megrendelővel a részletekről és a határidőkről.");
    const textfold::Range hit = textfold::find(textfold::fold(text), textfold::foldQuery("odon"));
    QVERIFY(hit.isValid());
    textfold::Range in;
    const QString sn = textfold::snippet(text, hit, &in);
    QVERIFY(in.isValid());
    QCOMPARE(sn.mid(in.start, in.length), QStringLiteral("Ödön"));   // az EREDETI (ékezetes) szöveg
    QVERIFY(sn.startsWith(QChar(0x2026)));
    QVERIFY(sn.endsWith(QChar(0x2026)));
    QVERIFY(!sn.contains(QLatin1Char('\n')));
    QVERIFY(sn.size() < 140);
    // Rövid szöveg: nincs csonkolás, nincs „…”.
    const QString shortText = QStringLiteral("Ödön itt van.");
    const QString sn2 = textfold::snippet(shortText, {0, 4}, &in);
    QCOMPARE(sn2, shortText);
    QCOMPARE(in.start, 0);
}

// ---- MeetingLibrary ---------------------------------------------------------------------

void LibraryTest::sections()
{
    const QDate today(2026, 10, 1);   // csütörtök
    auto sec = [&](const QDate& d) { return MeetingLibrary::sectionFor(QDateTime(d, QTime(10, 0)), today); };
    QCOMPARE(sec(QDate(2026, 10, 1)), DateSection::Today);
    QCOMPARE(sec(QDate(2026, 9, 30)), DateSection::Yesterday);
    QCOMPARE(sec(QDate(2026, 9, 29)), DateSection::ThisWeek);   // kedd
    QCOMPARE(sec(QDate(2026, 9, 28)), DateSection::ThisWeek);   // hétfő
    QCOMPARE(sec(QDate(2026, 9, 27)), DateSection::Earlier);    // előző vasárnap
    QCOMPARE(sec(QDate(2025, 1, 1)), DateSection::Earlier);
    // Hétfőn a „tegnap” (vasárnap) tegnap marad, az „ezen a héten” üres.
    QCOMPARE(MeetingLibrary::sectionFor(QDateTime(QDate(2026, 9, 27), QTime(9, 0)), QDate(2026, 9, 28)),
             DateSection::Yesterday);
    QCOMPARE(MeetingLibrary::sectionKey(DateSection::ThisWeek), QStringLiteral("thisWeek"));
    QCOMPARE(MeetingLibrary::sectionTitle(DateSection::Today), QStringLiteral("Ma"));
}

void LibraryTest::listNewestFirstWithSections()
{
    add("Régi", QDateTime(QDate(2026, 8, 3), QTime(9, 0)));
    add("Mai", QDateTime(QDate(2026, 10, 1), QTime(9, 0)));
    add("Tegnapi", QDateTime(QDate(2026, 9, 30), QTime(9, 0)));
    add("Keddi", QDateTime(QDate(2026, 9, 29), QTime(9, 0)));
    MeetingLibrary lib(m_store.get());
    const LibraryResult r = lib.query({}, m_now);
    QCOMPARE(r.count(), 4);
    QCOMPARE(r.totalMeetings, 4);
    QStringList titles;
    for (const LibraryEntry& e : r.entries) titles << e.title;
    QCOMPARE(titles, QStringList({"Mai", "Tegnapi", "Keddi", "Régi"}));
    QCOMPARE(r.entries.at(0).section, DateSection::Today);
    QCOMPARE(r.entries.at(1).section, DateSection::Yesterday);
    QCOMPARE(r.entries.at(2).section, DateSection::ThisWeek);
    QCOMPARE(r.entries.at(3).section, DateSection::Earlier);
    QCOMPARE(r.entries.at(0).durationMs, qint64(30 * 60 * 1000));
    QVERIFY(!r.entries.at(0).titleMatch.isValid());
    QVERIFY(r.entries.at(0).snippet.isEmpty());
}

void LibraryTest::searchTitleAccentInsensitive()
{
    add("Ödön heti egyeztetése", m_now.addDays(-1));
    add("Tervezés", m_now.addDays(-2));
    MeetingLibrary lib(m_store.get());
    LibraryQuery q;
    q.text = "odon";
    const LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.totalMeetings, 2);
    const LibraryEntry& e = r.entries.first();
    QVERIFY(e.titleMatch.isValid());
    QCOMPARE(e.title.mid(e.titleMatch.start, e.titleMatch.length), QStringLiteral("Ödön"));
    QVERIFY(e.snippet.isEmpty());    // átiratban nincs találat
    q.text = "ÖDÖN HETI";
    QCOMPARE(lib.query(q, m_now).count(), 1);
}

void LibraryTest::searchTranscriptSnippet()
{
    add("Projekt indító", m_now.addDays(-3),
        {{0, "Beszélő 1", "Sziasztok, kezdjük el."},
         {65000, "Beszélő 2", "Az árajánlatot Ödön küldi el pénteken a megrendelőnek."},
         {90000, "Beszélő 1", "Rendben, köszönöm."}},
        false, {{"Beszélő 2", "Kovács Lilla"}});
    add("Másik", m_now.addDays(-4), {{0, "Beszélő 1", "Semmi érdekes."}});
    MeetingLibrary lib(m_store.get());
    LibraryQuery q;
    q.text = "odon kuldi";
    const LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    const LibraryEntry& e = r.entries.first();
    QCOMPARE(e.title, QStringLiteral("Projekt indító"));
    QVERIFY(!e.titleMatch.isValid());
    QVERIFY(e.snippetMatch.isValid());
    QCOMPARE(e.snippet.mid(e.snippetMatch.start, e.snippetMatch.length), QStringLiteral("Ödön küldi"));
    QCOMPARE(e.snippetMs, qint64(65000));                      // a megszólalás kezdete (ugráshoz)
    QCOMPARE(e.snippetSpeaker, QStringLiteral("Kovács Lilla")); // a megjelenített (leképezett) név
    QVERIFY(lib.isWarm());
}

void LibraryTest::searchNoHit()
{
    add("Valami", m_now, {{0, "Beszélő 1", "Szöveg."}});
    MeetingLibrary lib(m_store.get());
    LibraryQuery q;
    q.text = "nincsilyen";
    const LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 0);
    QCOMPARE(r.totalMeetings, 1);
}

void LibraryTest::filters()
{
    add("Nincs átirat", m_now.addDays(-1));
    add("Csak átirat", m_now.addDays(-2), {{0, "Beszélő 1", "Hello."}}, false, {{"Beszélő 1", "Ödön"}});
    add("Minden megvan", m_now.addDays(-3), {{0, "Beszélő 1", "Hello."}}, true, {{"Beszélő 1", "Lilla"}});
    MeetingLibrary lib(m_store.get());

    LibraryQuery q;
    QVERIFY(q.isEmpty());
    q.noTranscript = true;
    QVERIFY(!q.isEmpty());
    LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.entries.first().title, QStringLiteral("Nincs átirat"));

    q = {};
    q.noSummary = true;
    r = lib.query(q, m_now);
    QCOMPARE(r.count(), 2);

    q = {};
    q.people = {QStringLiteral("odon")};                // ékezet nélkül is
    r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.entries.first().title, QStringLiteral("Csak átirat"));
    QVERIFY(r.entries.first().participants.contains(QStringLiteral("Ödön")));

    q.people = {QStringLiteral("Ádám")};                // a saját mikrofon-sáv beszélője mindenhol ott van
    QCOMPARE(lib.query(q, m_now).count(), 3);
    q.people = {QStringLiteral("Ádám"), QStringLiteral("Lilla")};   // mindkettő kell
    QCOMPARE(lib.query(q, m_now).count(), 1);

    // Szűrő + szöveg együtt.
    q = {};
    q.noSummary = true;
    q.text = "hello";
    r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.entries.first().title, QStringLiteral("Csak átirat"));
}

void LibraryTest::people()
{
    add("A", m_now, {{0, "Beszélő 1", "x"}}, false, {{"Beszélő 1", "Lilla"}});
    add("B", m_now.addDays(-1), {{0, "Beszélő 1", "x"}}, false, {{"Beszélő 1", "Lilla"}, {"Beszélő 2", "Ödön"}});
    MeetingLibrary lib(m_store.get());
    const QVector<PersonPresence> p = lib.people();
    QCOMPARE(p.size(), 3);
    QCOMPARE(p.at(0).meetingCount, 2);                  // Ádám és Lilla 2-2, Ödön 1
    QCOMPARE(p.at(2).name, QStringLiteral("Ödön"));
    QCOMPARE(p.at(2).meetingCount, 1);
}

void LibraryTest::pendingItems()
{
    const Meeting waiting = add("Átírásra vár", m_now.addDays(-1));
    const Meeting failed = add("Bukott", m_now.addDays(-2));
    const Meeting running = add("Épp fut", m_now.addDays(-3));
    add("Kész", m_now.addDays(-4), {{0, "Beszélő 1", "x"}});

    MeetingJobTracker tracker(m_store.get());
    MeetingLibrary lib(m_store.get(), &tracker);
    QSignalSpy pendingSpy(&lib, &MeetingLibrary::pendingItemsChanged);

    JobError e;
    e.kind = JobKind::Transcribe;
    e.message = "A szolgáltató nem fogadta el az API-kulcsot.";
    e.detail = "HTTP 401 · invalid_api_key";
    tracker.recordError(failed.id, e);
    tracker.begin(running.id, JobKind::Transcribe, "Átírás");
    QVERIFY(pendingSpy.count() >= 2);

    const QVector<PendingItem> items = lib.pendingItems();
    QCOMPARE(items.size(), 2);                           // a futó és a kész nem „vár rád”
    QCOMPARE(items.at(0).meetingId, waiting.id);
    QCOMPARE(items.at(0).kind, PendingKind::AwaitingTranscription);
    QCOMPARE(items.at(1).meetingId, failed.id);
    QCOMPARE(items.at(1).kind, PendingKind::TranscriptionFailed);
    QCOMPARE(items.at(1).error.detail, QStringLiteral("HTTP 401 · invalid_api_key"));
    QCOMPARE(lib.pendingItems(1).size(), 1);

    // A lista-bejegyzés állapota a trackerből jön.
    QCOMPARE(lib.entry(failed.id, m_now).state.transcriptState, StepState::Failed);
    QCOMPARE(lib.entry(running.id, m_now).state.transcriptState, StepState::Running);

    tracker.cancelled(running.id, JobKind::Transcribe);
    QCOMPARE(lib.pendingItems().size(), 3);
}

void LibraryTest::incrementalSignals()
{
    MeetingLibrary lib(m_store.get());
    QCOMPARE(lib.query({}, m_now).count(), 0);           // betöltve (üres)
    QSignalSpy added(&lib, &MeetingLibrary::meetingAdded);
    QSignalSpy changed(&lib, &MeetingLibrary::meetingChanged);
    QSignalSpy removed(&lib, &MeetingLibrary::meetingRemoved);

    Meeting m = m_store->createMeeting("Új");
    QCOMPARE(added.count(), 1);                          // egy új meeting → pontosan egy „added”
    QCOMPARE(added.at(0).at(0).toString(), m.id);
    QCOMPARE(lib.meetingCount(), 1);

    m.title = "Átnevezve";
    m_store->saveMeeting(m);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(lib.entry(m.id, m_now).title, QStringLiteral("Átnevezve"));
    QCOMPARE(lib.meeting(m.id).title, QStringLiteral("Átnevezve"));

    // Régebbi meeting érkezik (pl. helyreállított): a rendezés megmarad.
    Meeting old = m_store->createMeeting("Régebbi");
    old.startedAt = m.startedAt.addDays(-10);
    m_store->saveMeeting(old);
    const LibraryResult r = lib.query({}, QDateTime::currentDateTime());
    QCOMPARE(r.entries.at(0).id, m.id);
    QCOMPARE(r.entries.at(1).id, old.id);

    QVERIFY(m_store->deleteMeeting(m.id));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(lib.meetingCount(), 1);
    QVERIFY(lib.entry(m.id).id.isEmpty());
}

void LibraryTest::transcriptChangeInvalidatesCache()
{
    Meeting m = add("Változó", m_now, {{0, "Beszélő 1", "Az első változat szövege."}});
    MeetingLibrary lib(m_store.get());
    LibraryQuery q;
    q.text = "masodik";
    QCOMPARE(lib.query(q, m_now).count(), 0);
    q.text = "elso";
    QCOMPARE(lib.query(q, m_now).count(), 1);

    // Újra-átírás: a fájl változik, majd a store jelez (saveMeeting) → a szöveg újratöltődik.
    QTest::qWait(20);
    writeSegments(m, {{0, "Beszélő 1", "A második, hosszabb változat szövege került a helyére."}});
    m_store->saveMeeting(m);
    q.text = "masodik";
    QCOMPARE(lib.query(q, m_now).count(), 1);
    q.text = "elso";
    QCOMPARE(lib.query(q, m_now).count(), 0);

    // Csak a beszélő-hozzárendelés változik: a szöveg-cache marad, a név frissül.
    m.speakerMap.insert("Beszélő 1", "Ödön");
    m_store->saveMeeting(m);
    q.text = "masodik";
    QCOMPARE(lib.query(q, m_now).entries.first().snippetSpeaker, QStringLiteral("Ödön"));

    // Kézi érvénytelenítés (pl. külső folyamat írt a mappába).
    lib.invalidate();
    QCOMPARE(lib.query(q, m_now).count(), 1);
}

void LibraryTest::manyMeetingsStayResponsive()
{
    // 300 meeting, egyenként ~200 megszólalással (~20 kB szöveg).
    QVector<Seg> segs;
    for (int i = 0; i < 200; ++i)
        segs.append({i * 5000, QStringLiteral("Beszélő %1").arg(i % 4 + 1),
                     QStringLiteral("Ez a %1. megszólalás, amelyben a csapat a szokásos heti teendőket beszéli meg részletesen.").arg(i)});
    QString needleId;
    for (int i = 0; i < 300; ++i) {
        QVector<Seg> s = segs;
        if (i == 137) s[150].text = QStringLiteral("Itt hangzik el a ritka szó: tűzoltófecskendő.");
        const Meeting m = add(QStringLiteral("Meeting %1").arg(i), m_now.addDays(-i), s);
        if (i == 137) needleId = m.id;
    }
    MeetingLibrary lib(m_store.get());
    QElapsedTimer clock;
    clock.start();
    QCOMPARE(lib.query({}, m_now).count(), 300);
    const qint64 listMs = clock.restart();

    LibraryQuery q;
    q.text = "tuzoltofecskendo";
    LibraryResult r = lib.query(q, m_now);                // első keresés: betölti az átiratokat
    const qint64 coldMs = clock.restart();
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.entries.first().id, needleId);
    QCOMPARE(r.entries.first().snippetMs, qint64(150 * 5000));

    r = lib.query(q, m_now);                              // meleg keresés: memóriából
    const qint64 warmMs = clock.restart();
    QCOMPARE(r.count(), 1);
    q.text = "heti teendoket";                            // mindenhol találat
    QCOMPARE(lib.query(q, m_now).count(), 300);
    const qint64 allHitsMs = clock.elapsed();
    qInfo("lista: %lld ms, hideg keresés: %lld ms, meleg keresés: %lld ms, 300 találat: %lld ms",
          listMs, coldMs, warmMs, allHitsMs);
    QVERIFY2(warmMs < 150, "a meleg keresés nem maradt reszponzív");
    QVERIFY2(allHitsMs < 300, "a sok találatos keresés nem maradt reszponzív");
}

void LibraryTest::warmUpLoadsInBackground()
{
    for (int i = 0; i < 40; ++i)
        add(QStringLiteral("M%1").arg(i), m_now.addDays(-i), {{0, "Beszélő 1", QStringLiteral("szöveg %1").arg(i)}});
    MeetingLibrary lib(m_store.get());
    QVERIFY(!lib.isWarm());
    QSignalSpy warmed(&lib, &MeetingLibrary::warmedUp);
    lib.warmUp();
    QVERIFY(!lib.isWarm() || warmed.count() == 0);       // nem a hívásban tölt (nem blokkol)
    QVERIFY(warmed.wait(5000));
    QVERIFY(lib.isWarm());
}

// ---- címkék ---------------------------------------------------------------------------------

void LibraryTest::tagsInEntriesAndFilter()
{
    const Meeting a = add("Nordvik heti", m_now.addDays(-1));
    const Meeting b = add("Partner-egyeztetés", m_now.addDays(-2));
    const Meeting c = add("Címke nélkül", m_now.addDays(-3));
    TagService tags(m_store.get(), m_dir->filePath("meta/tags.json"));
    MeetingLibrary lib(m_store.get());
    lib.setTagService(&tags);
    const Tag nord = tags.addTag(a.id, QStringLiteral("Nordvik"));
    const Tag part = tags.addTag(a.id, QStringLiteral("Partnerek"));
    tags.addTag(b.id, part.id);

    LibraryEntry e = lib.entry(a.id, m_now);
    QCOMPARE(e.tagIds, (QStringList{nord.id, part.id}));
    QCOMPARE(e.tagNames, (QStringList{"Nordvik", "Partnerek"}));
    QVERIFY(lib.entry(c.id, m_now).tagIds.isEmpty());

    LibraryQuery q;
    q.tags = {nord.id, part.id};                       // Bármelyik (VAGY)
    QVERIFY(!q.isEmpty());
    QCOMPARE(lib.query(q, m_now).count(), 2);
    q.tagsAll = true;                                  // Mindegyik (ÉS)
    LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.entries.first().id, a.id);
    q = {};
    q.untagged = true;
    QVERIFY(!q.isEmpty());
    r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.entries.first().id, c.id);
    q.tags = {nord.id};                                // címke nélküli VAGY Nordvik
    QCOMPARE(lib.query(q, m_now).count(), 2);
    // Törölt címke: kikerül a bejegyzésből, a szűrőben nem számít.
    QSignalSpy tc(&lib, &MeetingLibrary::tagsChanged);
    tags.remove(nord.id);
    QVERIFY(tc.count() >= 1);
    QCOMPARE(lib.entry(a.id, m_now).tagNames, QStringList{"Partnerek"});
}

void LibraryTest::searchInTagNames()
{
    const Meeting a = add("Heti egyeztetés", m_now.addDays(-1), {{0, "Beszélő 1", "A raktárról beszéltünk."}});
    add("Másik", m_now.addDays(-2), {{0, "Beszélő 1", "Semmi köze."}});
    TagService tags(m_store.get(), m_dir->filePath("meta/tags.json"));
    MeetingLibrary lib(m_store.get());
    lib.setTagService(&tags);
    tags.addTag(a.id, QStringLiteral("Ügyfél-Nordvik"));

    LibraryQuery q;
    q.text = QStringLiteral("ugyfel");                 // ékezet- és kisbetű-független
    LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QVERIFY(r.entries.first().tagMatch);
    QCOMPARE(r.entries.first().tagMatchName, QStringLiteral("Ügyfél-Nordvik"));
    QVERIFY(!r.entries.first().titleMatch.isValid());
    // Átirat-találat: nem címke-találat.
    q.text = QStringLiteral("raktar");
    r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QVERIFY(!r.entries.first().tagMatch);
}

void LibraryTest::tagOptionsAndUntagged()
{
    const Meeting a = add("A", m_now.addDays(-1));
    const Meeting b = add("B", m_now.addDays(-5));
    add("C", m_now.addDays(-6));
    TagService tags(m_store.get(), m_dir->filePath("meta/tags.json"));
    MeetingLibrary lib(m_store.get());
    QVERIFY(lib.tagOptions().isEmpty());               // TagService nélkül nincs címke
    lib.setTagService(&tags);
    const Tag x = tags.addTag(a.id, QStringLiteral("Zebra"));
    tags.addTag(b.id, x.id);
    tags.addTag(b.id, QStringLiteral("Alfa"));
    tags.create(QStringLiteral("Használatlan"));
    const QVector<TagUsage> opts = lib.tagOptions();
    QCOMPARE(opts.size(), 3);
    QCOMPARE(opts.at(0).tag.name, QStringLiteral("Zebra"));   // gyakoriság szerint
    QCOMPARE(opts.at(0).meetingCount, 2);
    QCOMPARE(opts.at(1).tag.name, QStringLiteral("Alfa"));    // azonos számnál ABC
    QCOMPARE(opts.at(2).meetingCount, 0);
    QCOMPARE(opts.at(0).lastUsedAt, a.startedAt);
    QCOMPARE(lib.untaggedCount(), 1);
}

QTEST_GUILESS_MAIN(LibraryTest)
#include "test_library.moc"

// MeetingLibrary + text folding (foundation/text): the library sidebar's queries: date sections, accent- and
// case-insensitive full-text search (title + transcript) with snippet and hit position,
// filters, "These are waiting for you", incremental signals, cache invalidation.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/foundation/text/Text.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"

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

    // A person by name (the test's people.json; created if missing).
    PersonId pid(const QString& name) { return m_people->ensure(name); }
    struct Seg { qint64 ms; QString speaker; QString text; };
    Meeting add(const QString& title, const QDateTime& when, const QVector<Seg>& segs = {},
                bool summary = false, const QMap<QString, QString>& speakers = {});
    void writeSegments(const Meeting& m, const QVector<Seg>& segs);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<library::MeetingRepository> m_store;
    std::unique_ptr<library::PeopleStore> m_people;
    const QDateTime m_now{QDate(2026, 10, 1), QTime(15, 0)};   // Thursday
};

void LibraryTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<library::MeetingRepository>(m_dir->filePath("rec"), m_dir->filePath("meta"));
    m_people = std::make_unique<library::PeopleStore>(m_dir->filePath("meta/people.json"));
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
    for (auto it = speakers.constBegin(); it != speakers.constEnd(); ++it) m.speakerMap.insert(it.key(), pid(it.value()));
    Track mic;
    mic.id = "mic"; mic.kind = TrackKind::Mic; mic.file = "track_mic.ogg";
    mic.speakerLabel = "Ádám"; mic.fixedSpeaker = true;
    m.speakerMap.insert(mic.speakerLabel, pid("Ádám"));   // the own track's label is the own person
    m.tracks.append(mic);
    if (!segs.isEmpty()) writeSegments(m, segs);
    m_store->saveMeeting(m);
    return m;
}

// ---- text folding (foundation/text) -----------------------------------------------------------

void LibraryTest::foldKeepsLengthAndStripsAccents()
{
    const QString src = QStringLiteral("Ödön ÁRVÍZTŰRŐ tükörfúrógép — Ǘ ß 😀");
    const QString folded = text::fold(text::normalize(src));
    QCOMPARE(folded.size(), text::normalize(src).size());   // 1:1 length, so the positions match
    QVERIFY(folded.startsWith(QStringLiteral("odon arvizturo tukorfurogep")));
    QVERIFY(folded.contains(QStringLiteral(" u ")));             // U-umlaut to u (multiple accents)
    // Decomposed (NFD) input: normalize merges it, so it matches that way too.
    const QString nfd = QStringLiteral("Ödön");
    QCOMPARE(text::fold(text::normalize(nfd)), QStringLiteral("odon"));
    QCOMPARE(text::foldQuery(QStringLiteral("  ÖDÖN   úr ")), QStringLiteral("odon ur"));
}

void LibraryTest::findPhraseAndWords()
{
    const QString text = text::fold(QStringLiteral("Az árajánlatot Ödön küldi el pénteken."));
    text::Range r = text::find(text, text::foldQuery("odon"));
    QVERIFY(r.isValid());
    QCOMPARE(r.start, 15);
    QCOMPARE(r.length, 4);
    // Several words: first the whole phrase ...
    r = text::find(text, text::foldQuery("Ödön küldi"));
    QCOMPARE(r.start, 15);
    QCOMPARE(r.length, 10);
    // ... if that is absent but every word is present: the first word's position.
    r = text::find(text, text::foldQuery("pénteken árajánlat"));
    QCOMPARE(r.start, int(text.indexOf("penteken")));
    QCOMPARE(r.length, 8);
    QVERIFY(!text::find(text, text::foldQuery("ödön szerdán")).isValid());
    QVERIFY(!text::find(text, QString()).isValid());
}

void LibraryTest::snippetAroundMatch()
{
    const QString text = QStringLiteral(
        "Először átnéztük a múlt heti feladatokat, aztán rátértünk a lényegre.\n"
        "Az árajánlatot Ödön küldi el pénteken, a szerződést pedig Lilla nézi át hétfőn, "
        "utána egyeztetünk a megrendelővel a részletekről és a határidőkről.");
    const text::Range hit = text::find(text::fold(text), text::foldQuery("odon"));
    QVERIFY(hit.isValid());
    text::Range in;
    const QString sn = text::snippet(text, hit, &in);
    QVERIFY(in.isValid());
    QCOMPARE(sn.mid(in.start, in.length), QStringLiteral("Ödön"));   // the ORIGINAL (accented) text
    QVERIFY(sn.startsWith(QChar(0x2026)));
    QVERIFY(sn.endsWith(QChar(0x2026)));
    QVERIFY(!sn.contains(QLatin1Char('\n')));
    QVERIFY(sn.size() < 140);
    // Short text: no truncation, no "...".
    const QString shortText = QStringLiteral("Ödön itt van.");
    const QString sn2 = text::snippet(shortText, {0, 4}, &in);
    QCOMPARE(sn2, shortText);
    QCOMPARE(in.start, 0);
}

// ---- MeetingLibrary ---------------------------------------------------------------------

void LibraryTest::sections()
{
    const QDate today(2026, 10, 1);   // Thursday
    auto sec = [&](const QDate& d) { return library::MeetingLibrary::sectionFor(QDateTime(d, QTime(10, 0)), today); };
    QCOMPARE(sec(QDate(2026, 10, 1)), library::DateSection::Today);
    QCOMPARE(sec(QDate(2026, 9, 30)), library::DateSection::Yesterday);
    QCOMPARE(sec(QDate(2026, 9, 29)), library::DateSection::ThisWeek);   // Tuesday
    QCOMPARE(sec(QDate(2026, 9, 28)), library::DateSection::ThisWeek);   // Monday
    QCOMPARE(sec(QDate(2026, 9, 27)), library::DateSection::Earlier);    // the previous Sunday
    QCOMPARE(sec(QDate(2025, 1, 1)), library::DateSection::Earlier);
    // On Monday "yesterday" (Sunday) stays yesterday, and "this week" is empty.
    QCOMPARE(library::MeetingLibrary::sectionFor(QDateTime(QDate(2026, 9, 27), QTime(9, 0)), QDate(2026, 9, 28)),
             library::DateSection::Yesterday);
    QCOMPARE(library::MeetingLibrary::sectionKey(library::DateSection::ThisWeek), QStringLiteral("thisWeek"));
    QCOMPARE(library::MeetingLibrary::sectionTitle(library::DateSection::Today), QStringLiteral("Ma"));
}

void LibraryTest::listNewestFirstWithSections()
{
    add("Régi", QDateTime(QDate(2026, 8, 3), QTime(9, 0)));
    add("Mai", QDateTime(QDate(2026, 10, 1), QTime(9, 0)));
    add("Tegnapi", QDateTime(QDate(2026, 9, 30), QTime(9, 0)));
    add("Keddi", QDateTime(QDate(2026, 9, 29), QTime(9, 0)));
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    const library::LibraryResult r = lib.query({}, m_now);
    QCOMPARE(r.count(), 4);
    QCOMPARE(r.totalMeetings, 4);
    QStringList titles;
    for (const library::LibraryEntry& e : r.entries) titles << e.title;
    QCOMPARE(titles, QStringList({"Mai", "Tegnapi", "Keddi", "Régi"}));
    QCOMPARE(r.entries.at(0).section, library::DateSection::Today);
    QCOMPARE(r.entries.at(1).section, library::DateSection::Yesterday);
    QCOMPARE(r.entries.at(2).section, library::DateSection::ThisWeek);
    QCOMPARE(r.entries.at(3).section, library::DateSection::Earlier);
    QCOMPARE(r.entries.at(0).durationMs, qint64(30 * 60 * 1000));
    QVERIFY(!r.entries.at(0).titleMatch.isValid());
    QVERIFY(r.entries.at(0).snippet.isEmpty());
}

void LibraryTest::searchTitleAccentInsensitive()
{
    add("Ödön heti egyeztetése", m_now.addDays(-1));
    add("Tervezés", m_now.addDays(-2));
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    library::LibraryQuery q;
    q.text = "odon";
    const library::LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.totalMeetings, 2);
    const library::LibraryEntry& e = r.entries.first();
    QVERIFY(e.titleMatch.isValid());
    QCOMPARE(e.title.mid(e.titleMatch.start, e.titleMatch.length), QStringLiteral("Ödön"));
    QVERIFY(e.snippet.isEmpty());    // no hit in the transcript
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
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    library::LibraryQuery q;
    q.text = "odon kuldi";
    const library::LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    const library::LibraryEntry& e = r.entries.first();
    QCOMPARE(e.title, QStringLiteral("Projekt indító"));
    QVERIFY(!e.titleMatch.isValid());
    QVERIFY(e.snippetMatch.isValid());
    QCOMPARE(e.snippet.mid(e.snippetMatch.start, e.snippetMatch.length), QStringLiteral("Ödön küldi"));
    QCOMPARE(e.snippetMs, qint64(65000));                      // the utterance's start (for the jump)
    QCOMPARE(e.snippetSpeaker, QStringLiteral("Kovács Lilla")); // the displayed (mapped) name
    QVERIFY(lib.isWarm());
}

void LibraryTest::searchNoHit()
{
    add("Valami", m_now, {{0, "Beszélő 1", "Szöveg."}});
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    library::LibraryQuery q;
    q.text = "nincsilyen";
    const library::LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 0);
    QCOMPARE(r.totalMeetings, 1);
}

void LibraryTest::filters()
{
    add("Nincs átirat", m_now.addDays(-1));
    add("Csak átirat", m_now.addDays(-2), {{0, "Beszélő 1", "Hello."}}, false, {{"Beszélő 1", "Ödön"}});
    add("Minden megvan", m_now.addDays(-3), {{0, "Beszélő 1", "Hello."}}, true, {{"Beszélő 1", "Lilla"}});
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());

    library::LibraryQuery q;
    QVERIFY(q.isEmpty());
    q.noTranscript = true;
    QVERIFY(!q.isEmpty());
    library::LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.entries.first().title, QStringLiteral("Nincs átirat"));

    q = {};
    q.noSummary = true;
    r = lib.query(q, m_now);
    QCOMPARE(r.count(), 2);

    q = {};
    q.people = {pid(QStringLiteral("Ödön"))};
    r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.entries.first().title, QStringLiteral("Csak átirat"));
    QVERIFY(r.entries.first().participants.contains(QStringLiteral("Ödön")));

    q.people = {pid(QStringLiteral("Ádám"))};          // the own-microphone track's speaker is on all of them
    QCOMPARE(lib.query(q, m_now).count(), 3);
    q.people = {pid(QStringLiteral("Ádám")), pid(QStringLiteral("Lilla"))};   // both are required
    QCOMPARE(lib.query(q, m_now).count(), 1);

    // Filter + text together.
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
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    const QVector<library::PersonPresence> p = lib.people();
    QCOMPARE(p.size(), 3);
    QCOMPARE(p.at(0).meetingCount, 2);                  // two each for the own person and Lilla, 1 for Ödön
    QCOMPARE(p.at(2).name, QStringLiteral("Ödön"));
    QCOMPARE(p.at(2).meetingCount, 1);
}

void LibraryTest::pendingItems()
{
    const Meeting waiting = add("Átírásra vár", m_now.addDays(-1));
    const Meeting failed = add("Bukott", m_now.addDays(-2));
    const Meeting running = add("Épp fut", m_now.addDays(-3));
    add("Kész", m_now.addDays(-4), {{0, "Beszélő 1", "x"}});

    library::MeetingJobTracker tracker(m_store.get());
    library::MeetingLibrary lib(m_store.get(), &tracker);
    lib.setPeopleStore(m_people.get());
    QSignalSpy pendingSpy(&lib, &library::MeetingLibrary::pendingItemsChanged);

    JobError e;
    e.kind = JobKind::Transcribe;
    e.message = "A szolgáltató nem fogadta el az API-kulcsot.";
    e.detail = "HTTP 401 · invalid_api_key";
    tracker.recordError(failed.id, e);
    tracker.begin(running.id, JobKind::Transcribe, "Átírás");
    QVERIFY(pendingSpy.count() >= 2);

    const QVector<library::PendingItem> items = lib.pendingItems();
    QCOMPARE(items.size(), 2);                           // the running and the finished one are not "waiting for you"
    QCOMPARE(items.at(0).meetingId, waiting.id);
    QCOMPARE(items.at(0).kind, library::PendingKind::AwaitingTranscription);
    QCOMPARE(items.at(1).meetingId, failed.id);
    QCOMPARE(items.at(1).kind, library::PendingKind::TranscriptionFailed);
    QCOMPARE(items.at(1).error.detail, QStringLiteral("HTTP 401 · invalid_api_key"));
    QCOMPARE(lib.pendingItems(1).size(), 1);

    // The list entry's state comes from the tracker.
    QCOMPARE(lib.entry(failed.id, m_now).state.transcriptState, StepState::Failed);
    QCOMPARE(lib.entry(running.id, m_now).state.transcriptState, StepState::Running);

    tracker.cancelled(running.id, JobKind::Transcribe);
    QCOMPARE(lib.pendingItems().size(), 3);
}

void LibraryTest::incrementalSignals()
{
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    QCOMPARE(lib.query({}, m_now).count(), 0);           // loaded (empty)
    QSignalSpy added(&lib, &library::MeetingLibrary::meetingAdded);
    QSignalSpy changed(&lib, &library::MeetingLibrary::meetingChanged);
    QSignalSpy removed(&lib, &library::MeetingLibrary::meetingRemoved);

    Meeting m = m_store->createMeeting("Új");
    QCOMPARE(added.count(), 1);                          // one new meeting: exactly one "added"
    QCOMPARE(added.at(0).at(0).toString(), m.id);
    QCOMPARE(lib.meetingCount(), 1);

    m.title = "Átnevezve";
    m_store->saveMeeting(m);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(lib.entry(m.id, m_now).title, QStringLiteral("Átnevezve"));
    QCOMPARE(lib.meeting(m.id).title, QStringLiteral("Átnevezve"));

    // An older meeting arrives (e.g. recovered): the ordering stays.
    Meeting old = m_store->createMeeting("Régebbi");
    old.startedAt = m.startedAt.addDays(-10);
    m_store->saveMeeting(old);
    const library::LibraryResult r = lib.query({}, QDateTime::currentDateTime());
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
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    library::LibraryQuery q;
    q.text = "masodik";
    QCOMPARE(lib.query(q, m_now).count(), 0);
    q.text = "elso";
    QCOMPARE(lib.query(q, m_now).count(), 1);

    // Re-transcription: the file changes, then the store signals (saveMeeting), so the text reloads.
    QTest::qWait(20);
    writeSegments(m, {{0, "Beszélő 1", "A második, hosszabb változat szövege került a helyére."}});
    m_store->saveMeeting(m);
    q.text = "masodik";
    QCOMPARE(lib.query(q, m_now).count(), 1);
    q.text = "elso";
    QCOMPARE(lib.query(q, m_now).count(), 0);

    // Only the speaker assignment changes: the text cache stays, the name refreshes.
    m.speakerMap.insert("Beszélő 1", pid("Ödön"));
    m_store->saveMeeting(m);
    q.text = "masodik";
    QCOMPARE(lib.query(q, m_now).entries.first().snippetSpeaker, QStringLiteral("Ödön"));

    // Manual invalidation (e.g. an external process wrote into the folder).
    lib.invalidate();
    QCOMPARE(lib.query(q, m_now).count(), 1);
}

void LibraryTest::manyMeetingsStayResponsive()
{
    // 300 meetings, ~200 utterances each (~20 kB of text).
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
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    QElapsedTimer clock;
    clock.start();
    QCOMPARE(lib.query({}, m_now).count(), 300);
    const qint64 listMs = clock.restart();

    library::LibraryQuery q;
    q.text = "tuzoltofecskendo";
    library::LibraryResult r = lib.query(q, m_now);                // first search: loads the transcripts
    const qint64 coldMs = clock.restart();
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.entries.first().id, needleId);
    QCOMPARE(r.entries.first().snippetMs, qint64(150 * 5000));

    r = lib.query(q, m_now);                              // warm search: from memory
    const qint64 warmMs = clock.restart();
    QCOMPARE(r.count(), 1);
    q.text = "heti teendoket";                            // a hit everywhere
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
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    QVERIFY(!lib.isWarm());
    QSignalSpy warmed(&lib, &library::MeetingLibrary::warmedUp);
    lib.warmUp();
    QVERIFY(!lib.isWarm() || warmed.count() == 0);       // does not load in the call (does not block)
    QVERIFY(warmed.wait(5000));
    QVERIFY(lib.isWarm());
}

// ---- tags ----

void LibraryTest::tagsInEntriesAndFilter()
{
    const Meeting a = add("Nordvik heti", m_now.addDays(-1));
    const Meeting b = add("Partner-egyeztetés", m_now.addDays(-2));
    const Meeting c = add("Címke nélkül", m_now.addDays(-3));
    library::TagService tags(m_store.get(), m_dir->filePath("meta/tags.json"));
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    lib.setTagService(&tags);
    const Tag nord = tags.addTag(a.id, QStringLiteral("Nordvik"));
    const Tag part = tags.addTag(a.id, QStringLiteral("Partnerek"));
    tags.addTag(b.id, part.id);

    library::LibraryEntry e = lib.entry(a.id, m_now);
    QCOMPARE(e.tagIds, (QStringList{nord.id, part.id}));
    QCOMPARE(e.tagNames, (QStringList{"Nordvik", "Partnerek"}));
    QVERIFY(lib.entry(c.id, m_now).tagIds.isEmpty());

    library::LibraryQuery q;
    q.tags = {nord.id, part.id};                       // Any (OR)
    QVERIFY(!q.isEmpty());
    QCOMPARE(lib.query(q, m_now).count(), 2);
    q.tagsAll = true;                                  // All (AND)
    library::LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.entries.first().id, a.id);
    q = {};
    q.untagged = true;
    QVERIFY(!q.isEmpty());
    r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QCOMPARE(r.entries.first().id, c.id);
    q.tags = {nord.id};                                // untagged OR Nordvik
    QCOMPARE(lib.query(q, m_now).count(), 2);
    // A deleted tag: removed from the entry, not counted in the filter.
    QSignalSpy tc(&lib, &library::MeetingLibrary::tagsChanged);
    tags.remove(nord.id);
    QVERIFY(tc.count() >= 1);
    QCOMPARE(lib.entry(a.id, m_now).tagNames, QStringList{"Partnerek"});
}

void LibraryTest::searchInTagNames()
{
    const Meeting a = add("Heti egyeztetés", m_now.addDays(-1), {{0, "Beszélő 1", "A raktárról beszéltünk."}});
    add("Másik", m_now.addDays(-2), {{0, "Beszélő 1", "Semmi köze."}});
    library::TagService tags(m_store.get(), m_dir->filePath("meta/tags.json"));
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    lib.setTagService(&tags);
    tags.addTag(a.id, QStringLiteral("Ügyfél-Nordvik"));

    library::LibraryQuery q;
    q.text = QStringLiteral("ugyfel");                 // accent- and case-insensitive
    library::LibraryResult r = lib.query(q, m_now);
    QCOMPARE(r.count(), 1);
    QVERIFY(r.entries.first().tagMatch);
    QCOMPARE(r.entries.first().tagMatchName, QStringLiteral("Ügyfél-Nordvik"));
    QVERIFY(!r.entries.first().titleMatch.isValid());
    // Transcript hit: not a tag hit.
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
    library::TagService tags(m_store.get(), m_dir->filePath("meta/tags.json"));
    library::MeetingLibrary lib(m_store.get());
    lib.setPeopleStore(m_people.get());
    QVERIFY(lib.tagOptions().isEmpty());               // no tags without TagService
    lib.setTagService(&tags);
    const Tag x = tags.addTag(a.id, QStringLiteral("Zebra"));
    tags.addTag(b.id, x.id);
    tags.addTag(b.id, QStringLiteral("Alfa"));
    tags.create(QStringLiteral("Használatlan"));
    const QVector<TagUsage> opts = lib.tagOptions();
    QCOMPARE(opts.size(), 3);
    QCOMPARE(opts.at(0).tag.name, QStringLiteral("Zebra"));   // by frequency
    QCOMPARE(opts.at(0).meetingCount, 2);
    QCOMPARE(opts.at(1).tag.name, QStringLiteral("Alfa"));    // ties alphabetical
    QCOMPARE(opts.at(2).meetingCount, 0);
    QCOMPARE(opts.at(0).lastUsedAt, a.startedAt);
    QCOMPARE(lib.untaggedCount(), 1);
}

QTEST_GUILESS_MAIN(LibraryTest)
#include "test_library.moc"

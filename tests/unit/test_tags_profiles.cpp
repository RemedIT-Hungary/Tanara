// MeetingProfiles: classic similarity: suffix stripping, filler words, mishearing merge,
// the similarity order on a made-up library of six meetings, participants' rarity weight, the
// profile.json cache and the transcript change.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>

#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/MeetingProfiles.h"
#include "tags_fixture.h"

using namespace tanara;

class TagsProfilesTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void suffixStripping();
    void termsSkipNoise();
    void similarityOrdering();
    void participantRarity();
    void mishearingMerge();
    void profileCacheAndRebuild();
    void draftByTitle();
    void parallelReadsWhileRebuilding();

private:
    void buildAll(library::MeetingProfiles& p);
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<library::MeetingRepository> m_store;
    tagsfixture::Library m_lib;
};

void TagsProfilesTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<library::MeetingRepository>(m_dir->filePath("rec"), m_dir->filePath("meta"));
    m_lib = tagsfixture::buildLibrary(*m_store);
}

void TagsProfilesTest::cleanup()
{
    m_store.reset();
    m_dir.reset();
}

void TagsProfilesTest::buildAll(library::MeetingProfiles& p)
{
    QSignalSpy idle(&p, &library::MeetingProfiles::idle);
    p.ensureBuilt();
    if (!p.isIdle()) QVERIFY(idle.wait(10000));
    QVERIFY(p.isIdle());
}

void TagsProfilesTest::suffixStripping()
{
    QCOMPARE(library::tagtext::stem(QStringLiteral("projektben")), QStringLiteral("projekt"));
    QCOMPARE(library::tagtext::stem(QStringLiteral("nordviknak")), QStringLiteral("nordvik"));
    QCOMPARE(library::tagtext::stem(QStringLiteral("utemtervet")), QStringLiteral("utemterv"));
    QCOMPARE(library::tagtext::stem(QStringLiteral("raktarbol")), QStringLiteral("raktar"));   // folded from "-bol"
    QCOMPARE(library::tagtext::stem(QStringLiteral("hazban")), QStringLiteral("hazban"));     // the stem < 4 would remain
    QCOMPARE(library::tagtext::stem(QStringLiteral("kamionokat")), QStringLiteral("kamiono")); // once, the longest
}

void TagsProfilesTest::termsSkipNoise()
{
    const auto t = library::tagtext::terms(QStringLiteral("Szóval hogy a 2026-os Nordvik raktárban 12 raklap van, ugye?"));
    QStringList stems;
    for (const auto& p : t) stems << p.first;
    QVERIFY(stems.contains(QStringLiteral("nordvi")) || stems.contains(QStringLiteral("nordvik")));
    QVERIFY(stems.contains(QStringLiteral("raktar")));
    QVERIFY(!stems.contains(QStringLiteral("szoval")));   // filler word
    QVERIFY(!stems.contains(QStringLiteral("hogy")));
    QVERIFY(!stems.contains(QStringLiteral("ugye")));
    for (const QString& s : stems) QVERIFY2(!s.contains(QRegularExpression("\\d")), qPrintable(s));
    // The original word form stays for display.
    QCOMPARE(t.at(0).second, QStringLiteral("Nordvik"));
}

void TagsProfilesTest::similarityOrdering()
{
    library::MeetingProfiles p(m_store.get());
    buildAll(p);
    QVERIFY(p.isBuilt(m_lib.nordvik1));

    QVector<SimilarHit> hits = p.similar(m_lib.nordvik1);
    QVERIFY(!hits.isEmpty());
    QCOMPARE(hits.first().meetingId, m_lib.nordvik2);
    // Reasoning: common participant + common phrases (in displayed form) + a similar title.
    bool person = false, terms = false, title = false;
    for (const SuggestionReason& r : hits.first().reasons) {
        if (r.kind == ReasonKind::Participant) { person = true; QVERIFY(r.values.contains("Kovács Anna")); QVERIFY(!r.values.contains("Ádám")); }
        if (r.kind == ReasonKind::Terms) { terms = true; QVERIFY(r.values.size() <= 3); }
        if (r.kind == ReasonKind::Title) title = true;
    }
    QVERIFY(person && terms && title);

    QCOMPARE(p.similar(m_lib.budget1).first().meetingId, m_lib.budget2);
    QCOMPARE(p.similar(m_lib.museum2).first().meetingId, m_lib.museum1);
    // Descending score, itself not in it.
    hits = p.similar(m_lib.museum1);
    for (int i = 1; i < hits.size(); ++i) QVERIFY(hits.at(i - 1).score >= hits.at(i).score);
    for (const SimilarHit& h : hits) QVERIFY(h.meetingId != m_lib.museum1);
    // The meeting of another topic is much weaker (only the "Ádám" present everywhere is shared).
    double budgetScore = 0;
    for (const SimilarHit& h : p.similar(m_lib.nordvik1, -1))
        if (h.meetingId == m_lib.budget1) budgetScore = h.score;
    QVERIFY(budgetScore < hits.first().score / 2);
}

void TagsProfilesTest::participantRarity()
{
    library::MeetingProfiles p(m_store.get());
    QVERIFY(p.participantWeight("Ádám") < 0.01);             // on 6/6 meetings
    QVERIFY(qAbs(p.participantWeight("Kovács Anna") - (1.0 - (2.0 / 6.0) / 0.6)) < 1e-9);
    QVERIFY(p.participantWeight("Fehér Bence") > p.participantWeight("Kovács Anna"));
}

void TagsProfilesTest::mishearingMerge()
{
    library::MeetingProfiles p(m_store.get());
    buildAll(p);
    // "RemedIT" (museum1) and the misheard "Remedi" (museum2) are one phrase: shared, shown in
    // the most frequent original spelling.
    const QStringList shared = p.sharedTerms(m_lib.museum1, m_lib.museum2, 20);
    QVERIFY2(shared.contains(QStringLiteral("RemedIT")), qPrintable(shared.join(", ")));
    QVERIFY(!shared.contains(QStringLiteral("Remedi")));
    QVERIFY(p.termsOf(m_lib.museum1, 60).contains(QStringLiteral("MuseumPlus")));
}

void TagsProfilesTest::profileCacheAndRebuild()
{
    const Meeting m = m_store->load(m_lib.budget1);
    const QString cache = QDir(m.folder).filePath("profile.json");
    {
        library::MeetingProfiles p(m_store.get());
        buildAll(p);
        QVERIFY(QFile::exists(cache));
    }
    // New instance: built from the cache (the same result).
    library::MeetingProfiles p(m_store.get());
    buildAll(p);
    QCOMPARE(p.similar(m_lib.budget1).first().meetingId, m_lib.budget2);

    // The transcript changes (another topic): the profile is rebuilt.
    QTest::qWait(20);   // different modification time
    tagsfixture::writeSegments(m.folder, { "A MuseumPlus gyűjteményi rekordok leltári mezői.",
                                           "A MuseumPlus adatbázis migrációja." });
    QSignalSpy ready(&p, &library::MeetingProfiles::profileReady);
    p.ensureBuilt();
    QTRY_VERIFY(p.isIdle());
    QVERIFY(ready.count() >= 1);
    QVERIFY(p.termsOf(m_lib.budget1, 60).contains(QStringLiteral("MuseumPlus")));
}

void TagsProfilesTest::draftByTitle()
{
    library::MeetingProfiles p(m_store.get());
    buildAll(p);
    const QVector<SimilarHit> hits = p.similarToDraft(QStringLiteral("Nordvik heti egyeztetés"), {});
    QVERIFY(!hits.isEmpty());
    QVERIFY(hits.first().meetingId == m_lib.nordvik1 || hits.first().meetingId == m_lib.nordvik2);
}

void TagsProfilesTest::parallelReadsWhileRebuilding()
{
    // Thread-safety smoke test: four threads read continuously while the main thread has the
    // rows (meeting.json change) and profiles (transcript change) rebuilt.
    library::MeetingProfiles p(m_store.get());
    buildAll(p);
    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    std::atomic<int> missing{0};
    std::vector<std::unique_ptr<QThread>> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back(QThread::create([&] {
            while (!stop) {
                if (p.similar(m_lib.nordvik1, 5).isEmpty()) ++missing;
                if (!p.folders().contains(m_lib.budget2)) ++missing;
                p.sharedTerms(m_lib.museum1, m_lib.museum2, 5);
                p.topTerms({ m_lib.budget1, m_lib.budget2 }, 10);
                p.similarToDraft(QStringLiteral("Nordvik heti egyeztetés"), {});
                p.participantWeight(QStringLiteral("Kovács Anna"));
                p.isBuilt(m_lib.budget1);
                ++reads;
            }
        }));
        readers.back()->start();
    }
    for (int round = 0; round < 8; ++round) {
        Meeting m = m_store->load(m_lib.museum2);
        m.title = QStringLiteral("Remedi átadás %1").arg(round);
        m_store->saveMeeting(m);          // meetingUpdated: the row refreshes on the background thread
        p.invalidate(m_lib.budget1);      // the profile is rebuilt
        p.ensureBuilt();
        QTRY_VERIFY_WITH_TIMEOUT(p.isIdle(), 10000);
        // The main thread's read does not stall either (it at most gets the earlier snapshot).
        QVERIFY(!p.similar(m_lib.nordvik1).isEmpty());
    }
    QTRY_VERIFY_WITH_TIMEOUT(reads.load() > 40, 10000);
    stop = true;
    for (auto& t : readers) QVERIFY(t->wait(10000));
    QCOMPARE(missing.load(), 0);
    QVERIFY(p.isBuilt(m_lib.budget1));
    QCOMPARE(p.similar(m_lib.nordvik1).first().meetingId, m_lib.nordvik2);
    QCOMPARE(p.similar(m_lib.budget1).first().meetingId, m_lib.budget2);
}

QTEST_GUILESS_MAIN(TagsProfilesTest)
#include "test_tags_profiles.moc"

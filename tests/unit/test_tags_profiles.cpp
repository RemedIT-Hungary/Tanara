//
// MeetingProfiles — klasszikus hasonlóság: toldalék-levágás, töltelékszavak, félrehallás-
// összevonás, a hasonlóság sorrendje egy hat megbeszélésből álló kitalált könyvtáron, a
// résztvevők ritkasági súlya, a profile.json gyorsítótár és az átirat-változás.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/store/MeetingStore.h"
#include "tanara/tags/MeetingProfiles.h"
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

private:
    void buildAll(MeetingProfiles& p);
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<MeetingStore> m_store;
    tagsfixture::Library m_lib;
};

void TagsProfilesTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<MeetingStore>(m_dir->filePath("rec"), m_dir->filePath("meta"));
    m_lib = tagsfixture::buildLibrary(*m_store);
}

void TagsProfilesTest::cleanup()
{
    m_store.reset();
    m_dir.reset();
}

void TagsProfilesTest::buildAll(MeetingProfiles& p)
{
    QSignalSpy idle(&p, &MeetingProfiles::idle);
    p.ensureBuilt();
    if (!p.isIdle()) QVERIFY(idle.wait(10000));
    QVERIFY(p.isIdle());
}

void TagsProfilesTest::suffixStripping()
{
    QCOMPARE(tagtext::stem(QStringLiteral("projektben")), QStringLiteral("projekt"));
    QCOMPARE(tagtext::stem(QStringLiteral("nordviknak")), QStringLiteral("nordvik"));
    QCOMPARE(tagtext::stem(QStringLiteral("utemtervet")), QStringLiteral("utemterv"));
    QCOMPARE(tagtext::stem(QStringLiteral("raktarbol")), QStringLiteral("raktar"));   // -ből hajtogatva
    QCOMPARE(tagtext::stem(QStringLiteral("hazban")), QStringLiteral("hazban"));     // a tő < 4 maradna
    QCOMPARE(tagtext::stem(QStringLiteral("kamionokat")), QStringLiteral("kamiono")); // egyszer, a leghosszabb
}

void TagsProfilesTest::termsSkipNoise()
{
    const auto t = tagtext::terms(QStringLiteral("Szóval hogy a 2026-os Nordvik raktárban 12 raklap van, ugye?"));
    QStringList stems;
    for (const auto& p : t) stems << p.first;
    QVERIFY(stems.contains(QStringLiteral("nordvi")) || stems.contains(QStringLiteral("nordvik")));
    QVERIFY(stems.contains(QStringLiteral("raktar")));
    QVERIFY(!stems.contains(QStringLiteral("szoval")));   // töltelékszó
    QVERIFY(!stems.contains(QStringLiteral("hogy")));
    QVERIFY(!stems.contains(QStringLiteral("ugye")));
    for (const QString& s : stems) QVERIFY2(!s.contains(QRegularExpression("\\d")), qPrintable(s));
    // Az eredeti szóalak megmarad a megjelenítéshez.
    QCOMPARE(t.at(0).second, QStringLiteral("Nordvik"));
}

void TagsProfilesTest::similarityOrdering()
{
    MeetingProfiles p(m_store.get());
    buildAll(p);
    QVERIFY(p.isBuilt(m_lib.nordvik1));

    QVector<SimilarHit> hits = p.similar(m_lib.nordvik1);
    QVERIFY(!hits.isEmpty());
    QCOMPARE(hits.first().meetingId, m_lib.nordvik2);
    // Indoklás: közös résztvevő + közös kifejezések (megjelenített alakban) + hasonló cím.
    bool person = false, terms = false, title = false;
    for (const SuggestionReason& r : hits.first().reasons) {
        if (r.kind == ReasonKind::Participant) { person = true; QVERIFY(r.values.contains("Kovács Anna")); QVERIFY(!r.values.contains("Ádám")); }
        if (r.kind == ReasonKind::Terms) { terms = true; QVERIFY(r.values.size() <= 3); }
        if (r.kind == ReasonKind::Title) title = true;
    }
    QVERIFY(person && terms && title);

    QCOMPARE(p.similar(m_lib.budget1).first().meetingId, m_lib.budget2);
    QCOMPARE(p.similar(m_lib.museum2).first().meetingId, m_lib.museum1);
    // Csökkenő pontszám, önmaga nincs benne.
    hits = p.similar(m_lib.museum1);
    for (int i = 1; i < hits.size(); ++i) QVERIFY(hits.at(i - 1).score >= hits.at(i).score);
    for (const SimilarHit& h : hits) QVERIFY(h.meetingId != m_lib.museum1);
    // A más témájú megbeszélés sokkal gyengébb (csak a mindenhol jelen lévő „Ádám” közös).
    double budgetScore = 0;
    for (const SimilarHit& h : p.similar(m_lib.nordvik1, -1))
        if (h.meetingId == m_lib.budget1) budgetScore = h.score;
    QVERIFY(budgetScore < hits.first().score / 2);
}

void TagsProfilesTest::participantRarity()
{
    MeetingProfiles p(m_store.get());
    QVERIFY(p.participantWeight("Ádám") < 0.01);             // 6/6 megbeszélésen
    QVERIFY(qAbs(p.participantWeight("Kovács Anna") - (1.0 - (2.0 / 6.0) / 0.6)) < 1e-9);
    QVERIFY(p.participantWeight("Fehér Bence") > p.participantWeight("Kovács Anna"));
}

void TagsProfilesTest::mishearingMerge()
{
    MeetingProfiles p(m_store.get());
    buildAll(p);
    // „RemedIT” (museum1) és a félrehallott „Remedi” (museum2) egy kifejezés: közös, és a
    // leggyakoribb eredeti írásmóddal jelenik meg.
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
        MeetingProfiles p(m_store.get());
        buildAll(p);
        QVERIFY(QFile::exists(cache));
    }
    // Új példány: a gyorsítótárból épül (ugyanaz az eredmény).
    MeetingProfiles p(m_store.get());
    buildAll(p);
    QCOMPARE(p.similar(m_lib.budget1).first().meetingId, m_lib.budget2);

    // Az átirat változik (más téma) → a profil újraépül.
    QTest::qWait(20);   // eltérő módosítási idő
    tagsfixture::writeSegments(m.folder, { "A MuseumPlus gyűjteményi rekordok leltári mezői.",
                                           "A MuseumPlus adatbázis migrációja." });
    QSignalSpy ready(&p, &MeetingProfiles::profileReady);
    p.ensureBuilt();
    QTRY_VERIFY(p.isIdle());
    QVERIFY(ready.count() >= 1);
    QVERIFY(p.termsOf(m_lib.budget1, 60).contains(QStringLiteral("MuseumPlus")));
}

void TagsProfilesTest::draftByTitle()
{
    MeetingProfiles p(m_store.get());
    buildAll(p);
    const QVector<SimilarHit> hits = p.similarToDraft(QStringLiteral("Nordvik heti egyeztetés"), {});
    QVERIFY(!hits.isEmpty());
    QVERIFY(hits.first().meetingId == m_lib.nordvik1 || hits.first().meetingId == m_lib.nordvik2);
}

QTEST_GUILESS_MAIN(TagsProfilesTest)
#include "test_tags_profiles.moc"

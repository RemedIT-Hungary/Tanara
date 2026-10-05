//
// TagSuggester — szavazás a hasonló megbeszélések címkéivel (a mindenhol ott lévő címke
// csillapítva), a felrakott / elutasított címkék szűrése, együtt járó címkék, és a beágyazás-
// alapú szomszédok kölcsönös rang-fúziója (RRF) egy kézzel írt (ál-)indexszel.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "tanara/embedding/EmbeddingIndex.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/MeetingProfiles.h"
#include "tanara/tags/TagService.h"
#include "tanara/tags/TagSuggester.h"
#include "tags_fixture.h"

using namespace tanara;

class TagsSuggesterTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void votingFromNeighbours();
    void globalTagDamped();
    void appliedAndRejectedFiltered();
    void cooccurrence();
    void rrfPureFunction();
    void rrfWithFakeIndex();
    void draftSuggestions();

private:
    static QStringList names(const QVector<TagSuggestion>& list);
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<MeetingStore> m_store;
    std::unique_ptr<TagService> m_tags;
    std::unique_ptr<MeetingProfiles> m_profiles;
    tagsfixture::Library m_lib;
};

QStringList TagsSuggesterTest::names(const QVector<TagSuggestion>& list)
{
    QStringList out;
    for (const TagSuggestion& s : list) out << s.name;
    return out;
}

void TagsSuggesterTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<MeetingStore>(m_dir->filePath("rec"), m_dir->filePath("meta"));
    m_lib = tagsfixture::buildLibrary(*m_store);
    m_tags = std::make_unique<TagService>(m_store.get(), m_dir->filePath("meta/tags.json"));
    m_profiles = std::make_unique<MeetingProfiles>(m_store.get());
    m_tags->setProfiles(m_profiles.get());
    QSignalSpy idle(m_profiles.get(), &MeetingProfiles::idle);
    m_profiles->ensureBuilt();
    if (!m_profiles->isIdle()) QVERIFY(idle.wait(10000));
}

void TagsSuggesterTest::cleanup()
{
    m_profiles.reset();
    m_tags.reset();
    m_store.reset();
    m_dir.reset();
}

void TagsSuggesterTest::votingFromNeighbours()
{
    m_tags->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"));
    m_tags->addTag(m_lib.nordvik1, QStringLiteral("Logisztika"));
    m_tags->addTag(m_lib.budget1, QStringLiteral("Belső"));
    m_tags->addTag(m_lib.museum1, QStringLiteral("MuseumPlus"));

    const TagSuggester sg(m_tags.get(), m_profiles.get());
    const QVector<TagSuggestion> list = sg.suggest(m_lib.nordvik2);
    const QStringList n = names(list);
    QVERIFY2(n.contains("Nordvik") && n.contains("Logisztika"), qPrintable(n.join(", ")));
    QVERIFY(!n.contains("Belső"));
    QVERIFY(!n.contains("MuseumPlus"));
    QVERIFY(list.size() <= TagSuggester::kMaxSuggestions);
    const TagSuggestion& top = list.first();
    QCOMPARE(top.source, SuggestionSource::Similar);
    QCOMPARE(top.score, 1.0);
    QVERIFY(!top.isNew);
    // Indoklás + a szavazó megbeszélés linkje (cím, dátum).
    QVERIFY(!top.reasons.isEmpty());
    QCOMPARE(top.similarMeetings.size(), 1);
    QCOMPARE(top.similarMeetings.first().meetingId, m_lib.nordvik1);
    QCOMPARE(top.similarMeetings.first().title, QStringLiteral("Nordvik ütemterv egyeztetés"));
    QVERIFY(top.similarMeetings.first().startedAt.isValid());

    QCOMPARE(names(sg.suggest(m_lib.museum2)), QStringList{"MuseumPlus"});
}

void TagsSuggesterTest::globalTagDamped()
{
    // „Heti” mindenhol ott van (5/6), „Nordvik” csak a hasonló megbeszélésen.
    const Tag heti = m_tags->create(QStringLiteral("Heti"));
    for (const QString& id : { m_lib.nordvik1, m_lib.budget1, m_lib.budget2, m_lib.museum1, m_lib.museum2 })
        m_tags->addTag(id, heti.id);
    m_tags->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"));
    const TagSuggester sg(m_tags.get(), m_profiles.get());
    const QStringList n = names(sg.suggest(m_lib.nordvik2));
    QVERIFY2(!n.isEmpty() && n.first() == "Nordvik", qPrintable(n.join(", ")));
}

void TagsSuggesterTest::appliedAndRejectedFiltered()
{
    const Tag nordvik = m_tags->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"));
    const Tag log = m_tags->addTag(m_lib.nordvik1, QStringLiteral("Logisztika"));
    m_tags->addTag(m_lib.nordvik2, nordvik.id);              // már rajta
    m_tags->reject(m_lib.nordvik2, TagSuggestion{log.id, log.name});
    const TagSuggester sg(m_tags.get(), m_profiles.get());
    QVERIFY(sg.suggest(m_lib.nordvik2).isEmpty());
    // Más meetingre az elutasítás nem hat.
    QVERIFY(names(sg.suggest(m_lib.nordvik1)).isEmpty());   // a szomszéd (nordvik2) címkéje rajta van
}

void TagsSuggesterTest::cooccurrence()
{
    const Tag a = m_tags->create(QStringLiteral("Partnerek"));
    const Tag b = m_tags->create(QStringLiteral("Szerződés"));
    const Tag c = m_tags->create(QStringLiteral("Egyszeri"));
    for (const QString& id : { m_lib.nordvik1, m_lib.budget1, m_lib.museum1 }) m_tags->addTag(id, a.id);
    m_tags->addTag(m_lib.nordvik1, b.id);
    m_tags->addTag(m_lib.budget1, b.id);
    m_tags->addTag(m_lib.nordvik1, c.id);

    m_tags->addTag(m_lib.museum2, a.id);   // most került fel
    const TagSuggester sg(m_tags.get(), m_profiles.get());
    QVector<TagSuggestion> co = sg.cooccur(m_lib.museum2, a.id);
    QCOMPARE(names(co), QStringList{"Szerződés"});           // 2/3 ≥ 50 %; az „Egyszeri” 1/3
    QCOMPARE(co.first().source, SuggestionSource::Cooccur);
    QCOMPARE(co.first().baseTagId, a.id);
    QVERIFY(co.first().similarMeetings.first().durationMs > 0);
    QCOMPARE(co.first().similarMeetings.size(), 2);
    // Már rajta / elutasítva → nincs.
    m_tags->reject(m_lib.museum2, co.first());
    QVERIFY(sg.cooccur(m_lib.museum2, a.id).isEmpty());
    // Kevés minta (< 2 megbeszélés) → nincs javaslat.
    QVERIFY(sg.cooccur(m_lib.museum2, c.id).isEmpty());
}

void TagsSuggesterTest::rrfPureFunction()
{
    const QVector<SimilarHit> a{ {"x", 0.9, {}}, {"y", 0.5, {}}, {"z", 0.1, {}} };
    const QVector<SimilarHit> b{ {"z", 0.99, {{ReasonKind::Terms, {"t"}}}}, {"y", 0.8, {}} };
    const QVector<SimilarHit> f = TagSuggester::fuseRrf(a, b);
    QCOMPARE(f.size(), 3);
    // z: 1/63 + 1/61 (≈ 0.032266) > y: 1/62 + 1/62 (≈ 0.032258) > x: 1/61
    QCOMPARE(f.at(0).meetingId, QStringLiteral("z"));
    QCOMPARE(f.at(1).meetingId, QStringLiteral("y"));
    QCOMPARE(f.at(2).meetingId, QStringLiteral("x"));
    QVERIFY(qAbs(f.at(1).score - 2.0 / 62.0) < 1e-12);
    QCOMPARE(f.at(0).reasons.size(), 1);   // az indoklás a beágyazási forrásból is átjön
}

void TagsSuggesterTest::rrfWithFakeIndex()
{
    m_tags->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"));
    m_tags->addTag(m_lib.museum1, QStringLiteral("MuseumPlus"));
    const TagSuggester classic(m_tags.get(), m_profiles.get());
    QVERIFY(!names(classic.suggest(m_lib.nordvik2)).contains("MuseumPlus"));

    // Ál-index: a beágyazás szerint a nordvik2 a museum1-hez áll a legközelebb.
    EmbeddingIndex index(m_store.get());
    index.setModel(QStringLiteral("fake-embed"));
    auto put = [&](const QString& id, QVector<float> v) {
        EmbeddingChunk c; c.startMs = 0; c.endMs = 1000; c.vector = v;
        QVERIFY(index.store(id, QStringLiteral("fake-embed"), { c }));
    };
    put(m_lib.nordvik2, { 1.0f, 0.0f, 0.0f });
    put(m_lib.museum1,  { 0.95f, 0.05f, 0.0f });
    put(m_lib.nordvik1, { 0.6f, 0.4f, 0.0f });
    put(m_lib.budget1,  { 0.0f, 0.0f, 1.0f });
    QVERIFY(index.has(m_lib.nordvik2));
    const QVector<SimilarHit> emb = index.similar(m_lib.nordvik2);
    QCOMPARE(emb.first().meetingId, m_lib.museum1);

    const TagSuggester fused(m_tags.get(), m_profiles.get(), &index);
    const QStringList n = names(fused.suggest(m_lib.nordvik2));
    QVERIFY2(n.contains("MuseumPlus") && n.contains("Nordvik"), qPrintable(n.join(", ")));
    // Másik modell indexe nem számít (nincs fúzió).
    index.setModel(QStringLiteral("masik-modell"));
    QVERIFY(!index.has(m_lib.nordvik2));
    QVERIFY(!names(fused.suggest(m_lib.nordvik2)).contains("MuseumPlus"));
}

void TagsSuggesterTest::draftSuggestions()
{
    m_tags->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"));
    m_tags->addTag(m_lib.nordvik2, QStringLiteral("Nordvik"));
    const TagSuggester sg(m_tags.get(), m_profiles.get());
    QCOMPARE(names(sg.suggestForDraft(QStringLiteral("Nordvik heti egyeztetés"))), QStringList{"Nordvik"});
    QVERIFY(sg.suggestForDraft(QStringLiteral("Teams-hívás")).isEmpty());
}

QTEST_GUILESS_MAIN(TagsSuggesterTest)
#include "test_tags_suggester.moc"

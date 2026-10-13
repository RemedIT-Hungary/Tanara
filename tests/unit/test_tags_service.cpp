// TagService + TagNames: the tag set and the per-meeting tags: key equality, very similar
// names, input rows, rename / merge / delete, undo (groups), rejected suggestions,
// persistence (tags.json + meeting.json "tags"). Made-up names and titles.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "tanara/library/store/MeetingStore.h"
#include "tanara/library/tags/TagNames.h"
#include "tanara/library/tags/TagService.h"

using namespace tanara;

class TagsServiceTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void names();
    void createAndKeyEquality();
    void renameRules();
    void persistence();
    void meetingTags();
    void inputRowsRecentAndMatches();
    void inputRowsNearDuplicate();
    void mergeRetagsAndOffersKept();
    void removeKeepsOtherTags();
    void undoSingleAndGroups();
    void undoCap();
    void rejected();
    void usageAndProfile();

private:
    QString addMeeting(const QString& title, const QDateTime& when, const QStringList& speakers = {});
    std::unique_ptr<library::TagService> makeService();

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<library::MeetingStore> m_store;
};

void TagsServiceTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<library::MeetingStore>(m_dir->filePath("rec"), m_dir->filePath("meta"));
}

void TagsServiceTest::cleanup()
{
    m_store.reset();
    m_dir.reset();
}

QString TagsServiceTest::addMeeting(const QString& title, const QDateTime& when, const QStringList& speakers)
{
    Meeting m = m_store->createMeeting(title);
    m.startedAt = when;
    m.durationMs = 52 * 60 * 1000;
    for (int i = 0; i < speakers.size(); ++i)
        m.speakerMap.insert(QStringLiteral("Beszélő %1").arg(i + 1), speakers.at(i));
    m_store->saveMeeting(m);
    return m.id;
}

std::unique_ptr<library::TagService> TagsServiceTest::makeService()
{
    return std::make_unique<library::TagService>(m_store.get(), m_dir->filePath("meta/tags.json"));
}

void TagsServiceTest::names()
{
    QCOMPARE(library::normalizeTagName(QStringLiteral("  #Museum   Plus ")), QStringLiteral("Museum Plus"));
    QCOMPARE(library::tagKey(QStringLiteral("Museum Plus")), library::tagKey(QStringLiteral("museumplus")));
    QCOMPARE(library::tagKey(QStringLiteral("MÉM-MDK")), QStringLiteral("memmdk"));
    QVERIFY(library::nearDuplicate(QStringLiteral("Museum Plus"), QStringLiteral("MuseumPlus")));
    QVERIFY(library::nearDuplicate(QStringLiteral("Nordvik"), QStringLiteral("Nordvk")));        // 6+ letters: 1 difference
    QVERIFY(library::nearDuplicate(QStringLiteral("Nordvik"), QStringLiteral("Nordvki")));       // letter swap
    QVERIFY(!library::nearDuplicate(QStringLiteral("Nordvik"), QStringLiteral("Nordak")));       // 2 differences
    QVERIFY(!library::nearDuplicate(QStringLiteral("Alfa"), QStringLiteral("Alfi")));           // short key
    QVERIFY(library::nearDuplicate(QStringLiteral("Partnerprogram"), QStringLiteral("Partnerprogrm1")));   // 10+: 2
    QVERIFY(!library::nearDuplicate(QStringLiteral("Partnerprogram"), QStringLiteral("Partnerpgrm123")));
    QCOMPARE(library::damerauLevenshtein(QStringLiteral("abcd"), QStringLiteral("abdc")), 1);
}

void TagsServiceTest::createAndKeyEquality()
{
    auto svc = makeService();
    QSignalSpy changed(svc.get(), &library::TagService::tagsChanged);
    const Tag a = svc->create(QStringLiteral("Museum Plus"));
    QVERIFY(a.isValid());
    QVERIFY(!a.id.contains(QLatin1Char('{')));
    QCOMPARE(changed.count(), 1);
    const Tag b = svc->create(QStringLiteral("museumplus"));
    QCOMPARE(b.id, a.id);                              // the same key: the existing one
    QCOMPARE(svc->all().size(), 1);
    QCOMPARE(svc->byName(QStringLiteral("MUSEUM-PLUS")).id, a.id);
    QVERIFY(!svc->create(QStringLiteral("  # ")).isValid());
    QCOMPARE(svc->tag(a.id).name, QStringLiteral("Museum Plus"));
    QVERIFY(!svc->tag(QStringLiteral("nincs")).isValid());
    // Very similar names: the exact key no, the small typo yes.
    svc->create(QStringLiteral("Nordvik"));
    const QVector<Tag> sim = svc->similarNames(QStringLiteral("Nordvk"));
    QCOMPARE(sim.size(), 1);
    QCOMPARE(sim.first().name, QStringLiteral("Nordvik"));
    QVERIFY(svc->similarNames(QStringLiteral("Nordvik")).isEmpty());
}

void TagsServiceTest::renameRules()
{
    auto svc = makeService();
    const Tag a = svc->create(QStringLiteral("Nordvik"));
    const Tag b = svc->create(QStringLiteral("Partnerek"));
    QVERIFY(!svc->rename(a.id, QStringLiteral("partnerek")));   // the key belongs to another
    QVERIFY(!svc->rename(a.id, QStringLiteral("  ")));
    QVERIFY(svc->rename(a.id, QStringLiteral("Nordvik Kft")));
    QCOMPARE(svc->tag(a.id).name, QStringLiteral("Nordvik Kft"));
    QVERIFY(svc->rename(b.id, QStringLiteral("PARTNEREK")));     // its own key, another spelling
    QCOMPARE(svc->tag(b.id).name, QStringLiteral("PARTNEREK"));
}

void TagsServiceTest::persistence()
{
    const QString mid = addMeeting("Heti egyeztetés", QDateTime(QDate(2026, 9, 1), QTime(10, 0)));
    QString id;
    {
        auto svc = makeService();
        id = svc->addTag(mid, QStringLiteral("Nordvik")).id;
        QVERIFY(!id.isEmpty());
    }
    // shape of tags.json
    QFile f(m_dir->filePath("meta/tags.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(root.value("version").toInt(), 1);
    QCOMPARE(root.value("tags").toArray().size(), 1);
    QCOMPARE(root.value("tags").toArray().at(0).toObject().value("name").toString(), QStringLiteral("Nordvik"));
    // meeting.json "tags"
    const Meeting m = m_store->load(mid);
    QCOMPARE(m.tagIds, QStringList{id});
    QFile mj(QDir(m.folder).filePath("meeting.json"));
    QVERIFY(mj.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(mj.readAll()).object().value("tags").toArray().at(0).toString(), id);
    // A new instance sees the same.
    auto again = makeService();
    QCOMPARE(again->tagsOf(mid), QStringList{id});
    QCOMPARE(again->tag(id).name, QStringLiteral("Nordvik"));
}

void TagsServiceTest::meetingTags()
{
    const QString mid = addMeeting("Ügyfél-hívás", QDateTime::currentDateTime());
    auto svc = makeService();
    QSignalSpy mt(svc.get(), &library::TagService::meetingTagsChanged);
    const Tag a = svc->addTag(mid, QStringLiteral("Nordvik"));
    const Tag b = svc->addTag(mid, QStringLiteral("Partnerek"));
    QCOMPARE(svc->tagsOf(mid), (QStringList{a.id, b.id}));
    QCOMPARE(mt.count(), 2);
    QCOMPARE(svc->addTag(mid, a.id).id, a.id);        // already on: no change
    QCOMPARE(mt.count(), 2);
    QCOMPARE(svc->addTag(mid, QStringLiteral("nordvik")).id, a.id);   // by name too
    QCOMPARE(svc->tagsOf(mid).size(), 2);
    svc->removeTag(mid, a.id);
    QCOMPARE(svc->tagsOf(mid), QStringList{b.id});
    svc->setTags(mid, {a.id, QStringLiteral("ismeretlen"), b.id, a.id});
    QCOMPARE(svc->tagsOf(mid), (QStringList{a.id, b.id}));
    QVERIFY(!svc->addTag(QStringLiteral("nincs-ilyen-meeting"), QStringLiteral("X")).isValid());
    QCOMPARE(svc->meetingCount(a.id), 1);
}

void TagsServiceTest::inputRowsRecentAndMatches()
{
    auto svc = makeService();
    const QString m1 = addMeeting("A", QDateTime(QDate(2026, 9, 1), QTime(9, 0)));
    const QString m2 = addMeeting("B", QDateTime(QDate(2026, 9, 2), QTime(9, 0)));
    const Tag nord = svc->addTag(m1, QStringLiteral("Nordvik"));
    svc->addTag(m2, QStringLiteral("Nordvik"));
    const Tag ut = svc->addTag(m2, QStringLiteral("Ütemterv"));
    const Tag ugy = svc->create(QStringLiteral("Ügyfél-nordvik"));

    // Empty field: the most recently used.
    QVector<TagInputRow> rows = svc->inputRows(QString());
    QVERIFY(!rows.isEmpty());
    for (const TagInputRow& r : rows) QCOMPARE(r.kind, TagInputRow::Recent);

    // Accent- and case-insensitive match, prefix first, "New tag" at the end.
    rows = svc->inputRows(QStringLiteral("nord"));
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.at(0).kind, TagInputRow::Match);
    QCOMPARE(rows.at(0).tag.id, nord.id);
    QCOMPARE(rows.at(0).matchStart, 0);
    QCOMPARE(rows.at(0).matchLen, 4);
    QCOMPARE(rows.at(0).meetingCount, 2);
    QCOMPARE(rows.at(1).tag.id, ugy.id);               // substring (a word start after the hyphen)
    QCOMPARE(rows.at(1).matchStart, 7);
    QCOMPARE(rows.at(2).kind, TagInputRow::New);
    QCOMPARE(rows.at(2).tag.name, QStringLiteral("nord"));

    rows = svc->inputRows(QStringLiteral("utem"));
    QCOMPARE(rows.first().tag.id, ut.id);

    // Exact key match: no "New tag" row.
    rows = svc->inputRows(QStringLiteral("NORDVIK"));
    QCOMPARE(rows.first().tag.id, nord.id);
    for (const TagInputRow& r : rows) QVERIFY(r.kind != TagInputRow::New && r.kind != TagInputRow::ForceNew);

    // No match: only "New tag" (selected).
    rows = svc->inputRows(QStringLiteral("Zöldfa"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().kind, TagInputRow::New);
    QCOMPARE(library::TagService::preferredRow(rows), 0);
}

void TagsServiceTest::inputRowsNearDuplicate()
{
    auto svc = makeService();
    const Tag mp = svc->create(QStringLiteral("Museum Plus"));
    // "MuseumPlus" has the same key: the existing one, without a new one.
    QVector<TagInputRow> rows = svc->inputRows(QStringLiteral("MuseumPlus"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().tag.id, mp.id);
    // A small typo gives SIMILAR EXISTS (selected) + "Create anyway".
    rows = svc->inputRows(QStringLiteral("Museum Plsu"));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).kind, TagInputRow::NearDuplicate);
    QCOMPARE(rows.at(0).tag.id, mp.id);
    QCOMPARE(rows.at(1).kind, TagInputRow::ForceNew);
    QCOMPARE(rows.at(1).tag.name, QStringLiteral("Museum Plsu"));
    QCOMPARE(library::TagService::preferredRow(rows), 0);
}

void TagsServiceTest::mergeRetagsAndOffersKept()
{
    auto svc = makeService();
    const QString m1 = addMeeting("A", QDateTime(QDate(2026, 9, 1), QTime(9, 0)));
    const QString m2 = addMeeting("B", QDateTime(QDate(2026, 9, 2), QTime(9, 0)));
    const Tag from = svc->addTag(m1, QStringLiteral("Nordvik Zrt"));
    const Tag other = svc->addTag(m1, QStringLiteral("Partnerek"));
    const Tag keep = svc->addTag(m2, QStringLiteral("Nordvik"));
    svc->addTag(m2, from.id);
    svc->reject(m2, TagSuggestion{from.id, from.name});

    svc->merge(from.id, keep.id);
    QVERIFY(!svc->tag(from.id).isValid());
    QCOMPARE(svc->tagsOf(m1), (QStringList{keep.id, other.id}));   // in the old one's place
    QCOMPARE(svc->tagsOf(m2), QStringList{keep.id});                // no duplicate
    QVERIFY(svc->isRejected(m2, keep.id));                          // the rejection carried over
    // Typing the old name offers the kept one (without a new tag).
    const QVector<TagInputRow> rows = svc->inputRows(QStringLiteral("Nordvik Zrt"));
    QCOMPARE(rows.first().tag.id, keep.id);
    for (const TagInputRow& r : rows) QVERIFY(r.kind != TagInputRow::New);
    QCOMPARE(svc->create(QStringLiteral("nordvik zrt")).id, keep.id);
}

void TagsServiceTest::removeKeepsOtherTags()
{
    auto svc = makeService();
    const QString m1 = addMeeting("A", QDateTime::currentDateTime());
    const Tag a = svc->addTag(m1, QStringLiteral("Nordvik"));
    const Tag b = svc->addTag(m1, QStringLiteral("Partnerek"));
    svc->reject(m1, TagSuggestion{a.id, a.name});
    svc->remove(a.id);
    QCOMPARE(svc->tagsOf(m1), QStringList{b.id});
    QCOMPARE(svc->rejectedCount(), 0);
    QVERIFY(!svc->tag(a.id).isValid());
    QVERIFY(m_store->load(m1).id == m1);   // the meeting stays
}

void TagsServiceTest::undoSingleAndGroups()
{
    auto svc = makeService();
    const QString m1 = addMeeting("A", QDateTime::currentDateTime());
    const QString m2 = addMeeting("B", QDateTime::currentDateTime());
    const QString m3 = addMeeting("C", QDateTime::currentDateTime());
    QVERIFY(!svc->canUndo());

    const Tag a = svc->addTag(m1, QStringLiteral("Nordvik"));
    QVERIFY(svc->canUndo());
    QVERIFY(!svc->undoLabel().isEmpty());
    svc->undo();                                  // the apply AND the create undone
    QVERIFY(svc->tagsOf(m1).isEmpty());
    QVERIFY(!svc->tag(a.id).isValid());

    // Bulk add: one step.
    const Tag b = svc->create(QStringLiteral("Partnerek"));
    svc->bulkAdd({m1, m2, m3}, b.id);
    QCOMPARE(svc->meetingCount(b.id), 3);
    QVERIFY(svc->undoLabel().contains(QStringLiteral("Partnerek")));
    svc->undo();
    QCOMPARE(svc->meetingCount(b.id), 0);
    QVERIFY(svc->tag(b.id).isValid());            // the create was a separate step

    // Own group: two changes in one step.
    svc->beginGroup(QStringLiteral("Javaslatok elfogadása"));
    svc->addTag(m1, b.id);
    svc->addTag(m1, QStringLiteral("Ütemterv"));
    svc->endGroup();
    QCOMPARE(svc->undoLabel(), QStringLiteral("Javaslatok elfogadása"));
    QCOMPARE(svc->tagsOf(m1).size(), 2);
    svc->undo();
    QVERIFY(svc->tagsOf(m1).isEmpty());
    QVERIFY(!svc->byName(QStringLiteral("Ütemterv")).isValid());

    // Undoing the merge: the old tag and the assignments come back.
    const Tag x = svc->addTag(m1, QStringLiteral("Alfa projekt"));
    const Tag y = svc->addTag(m2, QStringLiteral("Alfa"));
    svc->merge(x.id, y.id);
    svc->undo();
    QVERIFY(svc->tag(x.id).isValid());
    QCOMPARE(svc->tagsOf(m1), QStringList{x.id});
    QCOMPARE(svc->tagsOf(m2), QStringList{y.id});
}

void TagsServiceTest::undoCap()
{
    auto svc = makeService();
    for (int i = 0; i < 60; ++i) svc->create(QStringLiteral("Címke %1").arg(i));
    int n = 0;
    while (svc->canUndo()) { svc->undo(); ++n; }
    QCOMPARE(n, 50);
    QCOMPARE(svc->all().size(), 10);
}

void TagsServiceTest::rejected()
{
    const QString m1 = addMeeting("A", QDateTime::currentDateTime());
    const QString m2 = addMeeting("B", QDateTime::currentDateTime());
    {
        auto svc = makeService();
        QSignalSpy rc(svc.get(), &library::TagService::rejectedChanged);
        const Tag a = svc->create(QStringLiteral("Nordvik"));
        svc->reject(m1, TagSuggestion{a.id, a.name});
        TagSuggestion llmNew;
        llmNew.name = QStringLiteral("Árajánlat 2027");
        llmNew.isNew = true;
        svc->reject(m1, llmNew);
        QCOMPARE(rc.count(), 2);
        // The rejection is an undoable step (undo = taking it back), in a group too.
        svc->beginGroup(QStringLiteral("Nem illik ide"));
        svc->reject(m2, TagSuggestion{a.id, a.name});
        svc->endGroup();
        QCOMPARE(svc->undoLabel(), QStringLiteral("Nem illik ide"));
        QVERIFY(svc->isRejected(m2, a.id));
        svc->undo();
        QVERIFY(!svc->isRejected(m2, a.id));
        QVERIFY(svc->isRejected(m1, a.id));
        QVERIFY(svc->isRejected(m1, QStringLiteral("nordvik")));          // by name too
        QVERIFY(svc->isRejected(m1, QStringLiteral("arajanlat 2027")));   // the key of the new name idea
        QVERIFY(!svc->isRejected(m2, a.id));                              // only for that meeting
        QCOMPARE(svc->rejectedCount(), 2);
        svc->reject(m1, TagSuggestion{a.id, a.name});                     // repeat: no new entry
        QCOMPARE(svc->rejectedCount(), 2);
        // Applying lifts the rejection.
        svc->addTag(m1, a.id);
        QVERIFY(!svc->isRejected(m1, a.id));
        QCOMPARE(svc->rejectedCount(), 1);
    }
    auto svc = makeService();                                            // persistent
    QCOMPARE(svc->rejectedCount(), 1);
    svc->clearRejected();
    QCOMPARE(svc->rejectedCount(), 0);
    svc->undo();
    QCOMPARE(svc->rejectedCount(), 1);
}

void TagsServiceTest::usageAndProfile()
{
    auto svc = makeService();
    const QString m1 = addMeeting("Nordvik heti", QDateTime(QDate(2026, 8, 1), QTime(9, 0)), {"Kovács Anna"});
    const QString m2 = addMeeting("Nordvik ütemterv", QDateTime(QDate(2026, 9, 1), QTime(9, 0)), {"Kovács Anna", "Fehér Bence"});
    const QString m3 = addMeeting("Belső", QDateTime(QDate(2026, 9, 5), QTime(9, 0)));
    const Tag n = svc->addTag(m1, QStringLiteral("Nordvik"));
    svc->addTag(m2, n.id);
    const Tag p = svc->addTag(m2, QStringLiteral("Partnerek"));
    svc->addTag(m3, QStringLiteral("Belső"));

    const QVector<TagUsage> most = svc->all(library::TagService::Sort::MostUsed);
    QCOMPARE(most.first().tag.id, n.id);
    QCOMPARE(most.first().meetingCount, 2);
    QCOMPARE(most.first().firstUsedAt.date(), QDate(2026, 8, 1));
    const QVector<TagUsage> alpha = svc->all(library::TagService::Sort::Alpha);
    QCOMPARE(alpha.first().tag.name, QStringLiteral("Belső"));

    const TagProfile prof = svc->profile(n.id);
    QCOMPARE(prof.meetingCount, 2);
    QCOMPARE(prof.meetings.first().meetingId, m2);       // newest first
    QCOMPARE(prof.topParticipants.first(), qMakePair(QStringLiteral("Kovács Anna"), 2));
    QCOMPARE(prof.cooccurring.size(), 1);
    QCOMPARE(prof.cooccurring.first(), qMakePair(p.id, 1));
    QVERIFY(svc->profileLine(n.id).contains(QStringLiteral("Kovács Anna")));
    QCOMPARE(svc->meetingsWith(n.id), (QStringList{m2, m1}));
    QCOMPARE(svc->meetingRef(m1).title, QStringLiteral("Nordvik heti"));
    QCOMPARE(svc->meetingRef(m1).durationMs, qint64(52 * 60 * 1000));
    QCOMPARE(prof.meetings.first().durationMs, qint64(52 * 60 * 1000));
}

QTEST_GUILESS_MAIN(TagsServiceTest)
#include "test_tags_service.moc"

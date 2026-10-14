#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/domain/Voice.h"

#include <cmath>

using namespace tanara;

namespace {
// The tests' people: the name stands in as the PersonId (voiceprints.json v3 is keyed by PersonId).
PersonId P(const QString& name) { return PersonId::fromString(name); }
}

class VoiceprintTest : public QObject {
    Q_OBJECT
private slots:
    void cosine_basic();
    void l2normalize_unit();
    void addAndMatch();
    void multiplePrintsTakesMax();
    void roundTripPersist();
    void renameMergeRemove();
    void oldFormatIsReadOnly();
    void modelField_defaultAndPersist();
    void multiModel_maxPerModelThenMean();
    void multiModel_noCommonModel();
    void printsMissingModel_listsSampleRefs();
    void siblingGroups_andSamplesMissingModel();
    void sourceRefs_persist();

private:
    static Voiceprint mp(const QString& model, const QVector<float>& e, const QString& ref = QString()) {
        Voiceprint p;
        p.model = model;
        p.embedding = e;
        p.sampleRef = ref;
        return p;
    }
    static Voiceprint vp(const QVector<float>& e) {
        Voiceprint p;
        p.embedding = e;
        return p;
    }
};

void VoiceprintTest::cosine_basic() {
    const QVector<float> a{1, 0, 0};
    const QVector<float> b{1, 0, 0};
    const QVector<float> c{0, 1, 0};
    QVERIFY(qFuzzyCompare(library::VoiceprintStore::cosineSimilarity(a, b) + 1.0, 2.0)); // ~1.0
    QVERIFY(qAbs(library::VoiceprintStore::cosineSimilarity(a, c)) < 1e-6);             // orthogonal
    // different dimension gives 0
    QCOMPARE(library::VoiceprintStore::cosineSimilarity(a, QVector<float>{1, 0}), 0.0);
}

void VoiceprintTest::l2normalize_unit() {
    const QVector<float> n = library::VoiceprintStore::l2normalize(QVector<float>{3, 4});
    double len = std::sqrt(double(n[0]) * n[0] + double(n[1]) * n[1]);
    QVERIFY(qAbs(len - 1.0) < 1e-6);
}

void VoiceprintTest::addAndMatch() {
    QTemporaryDir dir;
    library::VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    store.addPrint(P(QStringLiteral("Béla")), vp({1, 0, 0}));
    store.addPrint(P(QStringLiteral("Dompa")), vp({0, 1, 0}));

    const VoiceMatch m = store.bestMatch({0.9f, 0.1f, 0.0f});
    QCOMPARE(m.personId, P(QStringLiteral("Béla")));
    QVERIFY(m.score > 0.9);

    QCOMPARE(store.people().size(), 2);
    QCOMPARE(store.totalPrintCount(), 2);

    // an empty DB / an empty embedding: no hit
    QVERIFY(store.bestMatch(QVector<float>{}).score < 0.0);
}

void VoiceprintTest::multiplePrintsTakesMax() {
    QTemporaryDir dir;
    library::VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    // Béla with two microphones: a "bad" and a "good" voiceprint.
    store.addPrint(P(QStringLiteral("Béla")), vp({0, 0, 1}));
    store.addPrint(P(QStringLiteral("Béla")), vp({1, 0, 0}));
    QCOMPARE(store.printCount(P(QStringLiteral("Béla"))), 2);

    // The query is close to {1,0,0}, so the MAX (the good voiceprint) decides.
    const VoiceMatch m = store.bestMatch({1, 0, 0});
    QCOMPARE(m.personId, P(QStringLiteral("Béla")));
    QVERIFY(m.score > 0.99);
}

void VoiceprintTest::roundTripPersist() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("vp.json"));
    {
        library::VoiceprintStore store(path);
        Voiceprint p = vp({0.6f, 0.8f, 0.0f});
        p.sourceMeetingId = QStringLiteral("m1");
        p.sourceTrack = QStringLiteral("loopback");
        p.device = QStringLiteral("Sennheiser");
        store.addPrint(P(QStringLiteral("Béla")), p);
    }
    // A new store on the same file.
    library::VoiceprintStore reloaded(path);
    QCOMPARE(reloaded.people(), PersonIds{P(QStringLiteral("Béla"))});
    const QVector<Voiceprint> prints = reloaded.printsFor(P(QStringLiteral("Béla")));
    QCOMPARE(prints.size(), 1);
    QVERIFY(!prints[0].id.isEmpty());                      // an id was generated
    QCOMPARE(prints[0].dim, 3);
    QCOMPARE(prints[0].device, QStringLiteral("Sennheiser"));
    // Stored L2-normalized: length 1.
    double len = 0; for (float x : prints[0].embedding) len += double(x) * x;
    QVERIFY(qAbs(std::sqrt(len) - 1.0) < 1e-5);
}

void VoiceprintTest::renameMergeRemove() {
    QTemporaryDir dir;
    library::VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    store.addPrint(P(QStringLiteral("Távoli 1")), vp({1, 0, 0}));
    store.addPrint(P(QStringLiteral("Béla")), vp({0, 1, 0}));

    // merge: the voiceprints move by id (a rename needs nothing here: the id stays).
    const QStringList moved = store.merge(P(QStringLiteral("Távoli 1")), P(QStringLiteral("Béla")));
    QCOMPARE(moved.size(), 1);
    QCOMPARE(store.people(), PersonIds{P(QStringLiteral("Béla"))});
    QCOMPARE(store.printCount(P(QStringLiteral("Béla"))), 2);

    // merge into a person without voiceprints: they just move.
    store.addPrint(P(QStringLiteral("Dompa")), vp({0, 0, 1}));
    store.merge(P(QStringLiteral("Dompa")), P(QStringLiteral("Béla")));
    QCOMPARE(store.people(), PersonIds{P(QStringLiteral("Béla"))});
    QCOMPARE(store.printCount(P(QStringLiteral("Béla"))), 3);

    // removePerson
    store.removePerson(P(QStringLiteral("Béla")));
    QVERIFY(store.people().isEmpty());
    QCOMPARE(store.totalPrintCount(), 0);
    // On disk: version 3, keyed by PersonId.
    QFile f(store.filePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(root.value("version").toInt(), 3);
    QVERIFY(root.value("people").isObject());
}

void VoiceprintTest::oldFormatIsReadOnly() {
    // An old, name-keyed voiceprints.json is not read and never overwritten (migrate person-ids converts it).
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("vp.json"));
    const QByteArray old = R"({"people":[{"name":"Béla","prints":[{"id":"p1","embedding":[1,0,0],"dim":3}]}]})";
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(old);
    }
    library::VoiceprintStore store(path);
    QVERIFY(store.legacy());
    QVERIFY(store.people().isEmpty());
    store.addPrint(P(QStringLiteral("Béla")), vp({1, 0, 0}));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), old);
}

void VoiceprintTest::modelField_defaultAndPersist() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("vp.json"));
    {
        // A voiceprint without a model field gives "campplus".
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"version":3,"people":{"Béla":[{"id":"p1","embedding":[1,0,0],"dim":3}]}})");
    }
    library::VoiceprintStore store(path);
    QCOMPARE(store.printsFor(P(QStringLiteral("Béla"))).first().model, QStringLiteral("campplus"));
    Voiceprint p = mp(QStringLiteral("other"), {0, 1, 0, 0});
    store.addPrint(P(QStringLiteral("Béla")), p);
    Voiceprint empty = mp(QString(), {0, 0, 1});
    store.addPrint(P(QStringLiteral("Béla")), empty);   // an empty model gives the default model

    library::VoiceprintStore reloaded(path);
    const QVector<Voiceprint> prints = reloaded.printsFor(P(QStringLiteral("Béla")));
    QCOMPARE(prints.size(), 3);
    QCOMPARE(prints[0].model, QStringLiteral("campplus"));
    QCOMPARE(prints[1].model, QStringLiteral("other"));
    QCOMPARE(prints[2].model, QStringLiteral("campplus"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.readAll().contains("\"model\": \"other\""));
}

void VoiceprintTest::multiModel_maxPerModelThenMean() {
    QTemporaryDir dir;
    library::VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    // Anna: two voiceprints in m1 (the better counts), one in m2.
    store.addPrint(P(QStringLiteral("Anna")), mp("m1", {0, 1}));
    store.addPrint(P(QStringLiteral("Anna")), mp("m1", {1, 0}));
    store.addPrint(P(QStringLiteral("Anna")), mp("m2", {0.6f, 0.8f}));
    // Béla: has a voiceprint only in m2.
    store.addPrint(P(QStringLiteral("Béla")), mp("m2", {1, 0}));

    EmbeddingSet q;
    q["m1"] = {1, 0};
    q["m2"] = {1, 0};
    const QVector<VoiceMatch> ranked = store.rankedMatches(q, {"m2", "m1"});
    QCOMPARE(ranked.size(), 2);
    // Béla: only m2 is common, so 1.0; Anna: (max(0,1) + 0.6) / 2 = 0.8.
    QCOMPARE(ranked[0].personId, P(QStringLiteral("Béla")));
    QVERIFY(qAbs(ranked[0].score - 1.0) < 1e-6);
    QCOMPARE(ranked[1].personId, P(QStringLiteral("Anna")));
    QVERIFY(qAbs(ranked[1].score - 0.8) < 1e-6);
    QCOMPARE(store.bestMatch(q, {"m1", "m2"}).personId, P(QStringLiteral("Béla")));

    // Asked with m1 only, Béla does not take part (-1), Anna 1.0.
    const QVector<VoiceMatch> onlyM1 = store.rankedMatches(q, {"m1"});
    QCOMPARE(onlyM1[0].personId, P(QStringLiteral("Anna")));
    QVERIFY(qAbs(onlyM1[0].score - 1.0) < 1e-6);
    QCOMPARE(onlyM1[1].score, -1.0);

    // The old single-vector form = {"campplus" to v}: here nobody has a campplus voiceprint.
    QCOMPARE(store.bestMatch(QVector<float>{1, 0}).score, -1.0);
    store.addPrint(P(QStringLiteral("Cili")), vp({1, 0}));
    QCOMPARE(store.bestMatch(QVector<float>{1, 0}).personId, P(QStringLiteral("Cili")));
}

void VoiceprintTest::multiModel_noCommonModel() {
    QTemporaryDir dir;
    library::VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    store.addPrint(P(QStringLiteral("Anna")), mp("m1", {1, 0}));
    EmbeddingSet q;
    q["m2"] = {1, 0};
    const VoiceMatch m = store.bestMatch(q, {"m1", "m2"});
    QVERIFY(m.personId.isNull());
    QCOMPARE(m.score, -1.0);
    QVERIFY(store.rankedMatches(EmbeddingSet(), {"m1"}).isEmpty());
    QVERIFY(store.bestMatch(q, {}).personId.isNull());
}

void VoiceprintTest::printsMissingModel_listsSampleRefs() {
    QTemporaryDir dir;
    library::VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    store.addPrint(P(QStringLiteral("Anna")), mp("campplus", {1, 0}, QStringLiteral("m/b.ogg#10-20")));
    store.addPrint(P(QStringLiteral("Anna")), mp("other", {1, 0}, QStringLiteral("m/b.ogg#10-20")));
    store.addPrint(P(QStringLiteral("Anna")), mp("campplus", {0, 1}, QStringLiteral("m/a.ogg#0-5")));
    store.addPrint(P(QStringLiteral("Anna")), mp("campplus", {0, 1}));   // without a sampleRef: cannot be backfilled
    QCOMPARE(store.printsMissingModel(P(QStringLiteral("Anna")), QStringLiteral("other")),
             QStringList{QStringLiteral("m/a.ogg#0-5")});
    QVERIFY(store.printsMissingModel(P(QStringLiteral("Anna")), QStringLiteral("campplus")).isEmpty());
    QCOMPARE(store.printsMissingModel(P(QStringLiteral("Anna")), QStringLiteral("third")),
             QStringList({QStringLiteral("m/a.ogg#0-5"), QStringLiteral("m/b.ogg#10-20")}));
    QVERIFY(store.printsMissingModel(P(QStringLiteral("Senki")), QStringLiteral("other")).isEmpty());
}

void VoiceprintTest::siblingGroups_andSamplesMissingModel() {
    QTemporaryDir dir;
    library::VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    auto withMeta = [](Voiceprint p, const QString& meeting, const QString& created) {
        p.sourceMeetingId = meeting;
        p.createdAt = created;
        return p;
    };
    const QString ref = QStringLiteral("mixdown.mp3#10-20");
    // The same sample with two models (siblings), and the same file name + segment from ANOTHER meeting.
    store.addPrint(P("Anna"), withMeta(mp("campplus", {1, 0}, ref), "m1", "t1"));
    store.addPrint(P("Anna"), withMeta(mp("wespeaker", {1, 0, 0}, ref), "m1", "t1"));
    store.addPrint(P("Anna"), withMeta(mp("campplus", {0, 1}, ref), "m2", "t1"));
    // The same key, the same model: two separate samples (not siblings).
    store.addPrint(P("Anna"), withMeta(mp("campplus", {0.6f, 0.8f}, ref), "m1", "t1"));
    store.addPrint(P("Anna"), mp("campplus", {0, 1}));   // without a sampleRef: its own group

    const auto groups = library::VoiceprintStore::siblingGroups(store.printsFor(P("Anna")));
    QCOMPARE(groups.size(), 4);
    QCOMPARE(groups[0].size(), 2);
    QCOMPARE(groups[0][0].model, QStringLiteral("campplus"));
    QCOMPARE(groups[0][1].model, QStringLiteral("wespeaker"));
    QCOMPARE(store.sampleCount(P("Anna")), 4);

    // A sampleRef-level list would mix the two meetings; the sibling-exact one does not.
    QVERIFY(store.printsMissingModel(P("Anna"), "wespeaker").isEmpty());
    const QVector<Voiceprint> missing = store.samplesMissingModel(P("Anna"), "wespeaker");
    QCOMPARE(missing.size(), 2);                   // m2 and the second campplus with the same key
    for (const Voiceprint& p : missing) QCOMPARE(p.model, QStringLiteral("campplus"));
    QCOMPARE(store.samplesMissingModel(P("Anna"), "campplus").size(), 0);

    const QVector<Voiceprint> sibs = store.siblingsOf(groups[0][1].id);
    QCOMPARE(sibs.size(), 2);
    PersonId owner;
    QCOMPARE(store.siblingsOf(groups[2].first().id, &owner).size(), 1);
    QCOMPARE(owner, P("Anna"));
    QVERIFY(store.siblingsOf("nincs").isEmpty());
}

void VoiceprintTest::sourceRefs_persist() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("vp.json"));
    {
        library::VoiceprintStore store(path);
        Voiceprint p = mp("campplus", {1, 0}, QStringLiteral("mixdown.mp3#0-9000"));
        p.sourceRefs = {QStringLiteral("mixdown.mp3#0-9000"), QStringLiteral("mixdown.mp3#12000-15000")};
        store.addPrint(P("Anna"), p);
        store.addPrint(P("Béla"), mp("campplus", {0, 1}, QStringLiteral("x.ogg#1-2")));
    }
    library::VoiceprintStore again(path);
    QCOMPARE(again.printsFor(P("Anna")).first().sourceRefs,
             QStringList({QStringLiteral("mixdown.mp3#0-9000"), QStringLiteral("mixdown.mp3#12000-15000")}));
    QVERIFY(again.printsFor(P("Béla")).first().sourceRefs.isEmpty());
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray raw = f.readAll();
    QCOMPARE(raw.count("sourceRefs"), 1);           // not written out when empty
}

QTEST_GUILESS_MAIN(VoiceprintTest)
#include "test_voiceprint.moc"

#include <QtTest>
#include <QTemporaryDir>
#include "tanara/store/VoiceprintStore.h"
#include "tanara/Types.h"

#include <cmath>

using namespace tanara;

class VoiceprintTest : public QObject {
    Q_OBJECT
private slots:
    void cosine_basic();
    void l2normalize_unit();
    void addAndMatch();
    void multiplePrintsTakesMax();
    void roundTripPersist();
    void renameMergeRemove();
    void modelField_defaultAndPersist();
    void multiModel_maxPerModelThenMean();
    void multiModel_noCommonModel();
    void printsMissingModel_listsSampleRefs();

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
    QVERIFY(qFuzzyCompare(VoiceprintStore::cosineSimilarity(a, b) + 1.0, 2.0)); // ~1.0
    QVERIFY(qAbs(VoiceprintStore::cosineSimilarity(a, c)) < 1e-6);             // ortogonális
    // eltérő dimenzió → 0
    QCOMPARE(VoiceprintStore::cosineSimilarity(a, QVector<float>{1, 0}), 0.0);
}

void VoiceprintTest::l2normalize_unit() {
    const QVector<float> n = VoiceprintStore::l2normalize(QVector<float>{3, 4});
    double len = std::sqrt(double(n[0]) * n[0] + double(n[1]) * n[1]);
    QVERIFY(qAbs(len - 1.0) < 1e-6);
}

void VoiceprintTest::addAndMatch() {
    QTemporaryDir dir;
    VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    store.addPrint(QStringLiteral("Béla"), vp({1, 0, 0}));
    store.addPrint(QStringLiteral("Dompa"), vp({0, 1, 0}));

    const VoiceMatch m = store.bestMatch({0.9f, 0.1f, 0.0f});
    QCOMPARE(m.name, QStringLiteral("Béla"));
    QVERIFY(m.score > 0.9);

    QCOMPARE(store.people().size(), 2);
    QCOMPARE(store.totalPrintCount(), 2);

    // üres DB / üres embedding → nincs találat
    QVERIFY(store.bestMatch(QVector<float>{}).score < 0.0);
}

void VoiceprintTest::multiplePrintsTakesMax() {
    QTemporaryDir dir;
    VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    // Béla két mikrofonnal: egy "rossz" és egy "jó" lenyomat.
    store.addPrint(QStringLiteral("Béla"), vp({0, 0, 1}));
    store.addPrint(QStringLiteral("Béla"), vp({1, 0, 0}));
    QCOMPARE(store.printCount(QStringLiteral("Béla")), 2);

    // A lekérdezés az {1,0,0}-hoz közeli → a MAX (jó lenyomat) dönt.
    const VoiceMatch m = store.bestMatch({1, 0, 0});
    QCOMPARE(m.name, QStringLiteral("Béla"));
    QVERIFY(m.score > 0.99);
}

void VoiceprintTest::roundTripPersist() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("vp.json"));
    {
        VoiceprintStore store(path);
        Voiceprint p = vp({0.6f, 0.8f, 0.0f});
        p.sourceMeetingId = QStringLiteral("m1");
        p.sourceTrack = QStringLiteral("loopback");
        p.device = QStringLiteral("Sennheiser");
        store.addPrint(QStringLiteral("Béla"), p);
    }
    // Új store ugyanarról a fájlról.
    VoiceprintStore reloaded(path);
    QCOMPARE(reloaded.people(), QStringList{QStringLiteral("Béla")});
    const QVector<Voiceprint> prints = reloaded.printsFor(QStringLiteral("Béla"));
    QCOMPARE(prints.size(), 1);
    QVERIFY(!prints[0].id.isEmpty());                      // id generálódott
    QCOMPARE(prints[0].dim, 3);
    QCOMPARE(prints[0].device, QStringLiteral("Sennheiser"));
    // L2-normalizálva tárolt → hossz 1.
    double len = 0; for (float x : prints[0].embedding) len += double(x) * x;
    QVERIFY(qAbs(std::sqrt(len) - 1.0) < 1e-5);
}

void VoiceprintTest::renameMergeRemove() {
    QTemporaryDir dir;
    VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    store.addPrint(QStringLiteral("Távoli 1"), vp({1, 0, 0}));
    store.addPrint(QStringLiteral("Béla"), vp({0, 1, 0}));

    // rename → "Béla" már létezik → egyesítés (2 lenyomat).
    store.renamePerson(QStringLiteral("Távoli 1"), QStringLiteral("Béla"));
    QCOMPARE(store.people(), QStringList{QStringLiteral("Béla")});
    QCOMPARE(store.printCount(QStringLiteral("Béla")), 2);

    // merge: hozzunk létre egy másikat, majd olvasszuk Bélába.
    store.addPrint(QStringLiteral("Dompa"), vp({0, 0, 1}));
    store.merge(QStringLiteral("Dompa"), QStringLiteral("Béla"));
    QCOMPARE(store.people(), QStringList{QStringLiteral("Béla")});
    QCOMPARE(store.printCount(QStringLiteral("Béla")), 3);

    // removePerson
    store.removePerson(QStringLiteral("Béla"));
    QVERIFY(store.people().isEmpty());
    QCOMPARE(store.totalPrintCount(), 0);
}

void VoiceprintTest::modelField_defaultAndPersist() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("vp.json"));
    {
        // Régi (modell-mező nélküli) fájl → "campplus".
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"people":[{"name":"Béla","prints":[{"id":"p1","embedding":[1,0,0],"dim":3}]}]})");
    }
    VoiceprintStore store(path);
    QCOMPARE(store.printsFor(QStringLiteral("Béla")).first().model, QStringLiteral("campplus"));
    Voiceprint p = mp(QStringLiteral("other"), {0, 1, 0, 0});
    store.addPrint(QStringLiteral("Béla"), p);
    Voiceprint empty = mp(QString(), {0, 0, 1});
    store.addPrint(QStringLiteral("Béla"), empty);   // üres modell → alapmodell

    VoiceprintStore reloaded(path);
    const QVector<Voiceprint> prints = reloaded.printsFor(QStringLiteral("Béla"));
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
    VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    // Anna: m1-ben két lenyomat (a jobbik számít), m2-ben egy.
    store.addPrint(QStringLiteral("Anna"), mp("m1", {0, 1}));
    store.addPrint(QStringLiteral("Anna"), mp("m1", {1, 0}));
    store.addPrint(QStringLiteral("Anna"), mp("m2", {0.6f, 0.8f}));
    // Béla: csak m2-ben van lenyomata.
    store.addPrint(QStringLiteral("Béla"), mp("m2", {1, 0}));

    EmbeddingSet q;
    q["m1"] = {1, 0};
    q["m2"] = {1, 0};
    const QVector<VoiceMatch> ranked = store.rankedMatches(q, {"m2", "m1"});
    QCOMPARE(ranked.size(), 2);
    // Béla: csak m2 közös → 1.0; Anna: (max(0,1) + 0.6) / 2 = 0.8.
    QCOMPARE(ranked[0].name, QStringLiteral("Béla"));
    QVERIFY(qAbs(ranked[0].score - 1.0) < 1e-6);
    QCOMPARE(ranked[1].name, QStringLiteral("Anna"));
    QVERIFY(qAbs(ranked[1].score - 0.8) < 1e-6);
    QCOMPARE(store.bestMatch(q, {"m1", "m2"}).name, QStringLiteral("Béla"));

    // Csak m1-gyel kérdezve Béla nem vesz részt (-1), Anna 1.0.
    const QVector<VoiceMatch> onlyM1 = store.rankedMatches(q, {"m1"});
    QCOMPARE(onlyM1[0].name, QStringLiteral("Anna"));
    QVERIFY(qAbs(onlyM1[0].score - 1.0) < 1e-6);
    QCOMPARE(onlyM1[1].score, -1.0);

    // A régi egyvektoros forma = {"campplus" → v}: itt senkinek nincs campplus-lenyomata.
    QCOMPARE(store.bestMatch(QVector<float>{1, 0}).score, -1.0);
    store.addPrint(QStringLiteral("Cili"), vp({1, 0}));
    QCOMPARE(store.bestMatch(QVector<float>{1, 0}).name, QStringLiteral("Cili"));
}

void VoiceprintTest::multiModel_noCommonModel() {
    QTemporaryDir dir;
    VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    store.addPrint(QStringLiteral("Anna"), mp("m1", {1, 0}));
    EmbeddingSet q;
    q["m2"] = {1, 0};
    const VoiceMatch m = store.bestMatch(q, {"m1", "m2"});
    QVERIFY(m.name.isEmpty());
    QCOMPARE(m.score, -1.0);
    QVERIFY(store.rankedMatches(EmbeddingSet(), {"m1"}).isEmpty());
    QVERIFY(store.bestMatch(q, {}).name.isEmpty());
}

void VoiceprintTest::printsMissingModel_listsSampleRefs() {
    QTemporaryDir dir;
    VoiceprintStore store(dir.filePath(QStringLiteral("vp.json")));
    store.addPrint(QStringLiteral("Anna"), mp("campplus", {1, 0}, QStringLiteral("m/b.ogg#10-20")));
    store.addPrint(QStringLiteral("Anna"), mp("other", {1, 0}, QStringLiteral("m/b.ogg#10-20")));
    store.addPrint(QStringLiteral("Anna"), mp("campplus", {0, 1}, QStringLiteral("m/a.ogg#0-5")));
    store.addPrint(QStringLiteral("Anna"), mp("campplus", {0, 1}));   // sampleRef nélkül: nem pótolható
    QCOMPARE(store.printsMissingModel(QStringLiteral("anna"), QStringLiteral("other")),
             QStringList{QStringLiteral("m/a.ogg#0-5")});
    QVERIFY(store.printsMissingModel(QStringLiteral("Anna"), QStringLiteral("campplus")).isEmpty());
    QCOMPARE(store.printsMissingModel(QStringLiteral("Anna"), QStringLiteral("third")),
             QStringList({QStringLiteral("m/a.ogg#0-5"), QStringLiteral("m/b.ogg#10-20")}));
    QVERIFY(store.printsMissingModel(QStringLiteral("Senki"), QStringLiteral("other")).isEmpty());
}

QTEST_GUILESS_MAIN(VoiceprintTest)
#include "test_voiceprint.moc"

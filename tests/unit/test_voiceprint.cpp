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

private:
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

QTEST_GUILESS_MAIN(VoiceprintTest)
#include "test_voiceprint.moc"

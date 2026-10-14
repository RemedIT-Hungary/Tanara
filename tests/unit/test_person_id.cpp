// PersonId (P11): the stable person identifier. Format of generate() (UUID v4, lower-case,
// hyphenated, no braces), uniqueness, string round trip, hashing, null semantics, the people.json
// v3 round trip (the id survives save + reload, a rename touches only the name), and the
// name-to-id boundary (PeopleStore::choiceFor).
#include "tanara/domain/Person.h"
#include "tanara/library/store/PeopleStore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

using namespace tanara;

class PersonIdTest : public QObject {
    Q_OBJECT
private slots:
    void generateFormatAndUniqueness()
    {
        static const QRegularExpression uuid(
            QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));
        QSet<QString> seen;
        for (int i = 0; i < 1000; ++i) {
            const PersonId id = PersonId::generate();
            QVERIFY(!id.isNull());
            QVERIFY2(uuid.match(id.toString()).hasMatch(), qPrintable(id.toString()));
            seen.insert(id.toString());
        }
        QCOMPARE(seen.size(), 1000);
    }

    void roundTripAndNull()
    {
        const PersonId id = PersonId::generate();
        QCOMPARE(PersonId::fromString(id.toString()), id);
        QCOMPARE(PersonId::fromString(QStringLiteral("  ") + id.toString() + QStringLiteral(" ")), id);
        QVERIFY(PersonId().isNull());
        QVERIFY(PersonId::fromString(QStringLiteral("   ")).isNull());
        QVERIFY(PersonId() == PersonId::fromString(QString()));
        QVERIFY(id != PersonId::generate());
        // Hash / map keys.
        QHash<PersonId, int> h;
        h.insert(id, 1);
        QCOMPARE(h.value(PersonId::fromString(id.toString())), 1);
        // Display names.
        const PersonNames names{{id, QStringLiteral("Anna")}};
        QCOMPARE(personName(names, id), QStringLiteral("Anna"));
        QCOMPARE(personName(names, PersonId::generate(), QStringLiteral("Beszélő 1")), QStringLiteral("Beszélő 1"));
        QCOMPARE(personName(names, PersonId(), QStringLiteral("x")), QStringLiteral("x"));
        // QVariant (QML / signals).
        const QVariant v = QVariant::fromValue(id);
        QCOMPARE(v.value<PersonId>(), id);
    }

    void storeKeepsIdAcrossSaveAndRename()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("people.json"));
        PersonId anna;
        {
            library::PeopleStore store(path);
            anna = store.create(QStringLiteral("Kovács Anna"));
            QVERIFY(!anna.isNull());
            QVERIFY(store.rename(anna, QStringLiteral("Szabó Anna")));
        }
        library::PeopleStore again(path);
        QVERIFY(again.contains(anna));
        QCOMPARE(again.displayName(anna), QStringLiteral("Szabó Anna"));
        QCOMPARE(again.idForName(QStringLiteral("szabó anna")), anna);     // case-insensitive lookup
        // The file: version 3, keyed by the id.
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 3);
        QCOMPARE(root.value(QStringLiteral("people")).toObject().value(anna.toString()).toObject()
                     .value(QStringLiteral("name")).toString(), QStringLiteral("Szabó Anna"));
    }

    void choiceForBoundary()
    {
        QTemporaryDir dir;
        library::PeopleStore store(dir.filePath(QStringLiteral("people.json")));
        const PersonId anna = store.create(QStringLiteral("Anna"));
        // A known id -> that person; a known name -> the person; an unknown name -> to be created.
        QCOMPARE(store.choiceFor(anna.toString()).id, anna);
        QCOMPARE(store.choiceFor(QStringLiteral("anna")).id, anna);
        const PersonChoice fresh = store.choiceFor(QStringLiteral("Béla"));
        QVERIFY(fresh.id.isNull());
        QCOMPARE(fresh.newName, QStringLiteral("Béla"));
        QVERIFY(store.choiceFor(QString()).isAnonymous());
        // Two people with the same name are allowed (the id tells them apart).
        const PersonId anna2 = store.create(QStringLiteral("Anna"));
        QVERIFY(!anna2.isNull());
        QVERIFY(anna2 != anna);
        QCOMPARE(store.findByName(QStringLiteral("Anna")).size(), 2);
    }
};

QTEST_GUILESS_MAIN(PersonIdTest)
#include "test_person_id.moc"

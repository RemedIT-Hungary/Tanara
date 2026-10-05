// A címke-vezérlők QML-oldala, valódi ablak nélkül (offscreen + szoftveres renderer):
//  - a Címkék ablak minden demó-állapota (T10–T13, rename) mindkét témában QML-figyelmeztetés
//    nélkül töltődik be, és a T11 / T12 párbeszéde nyitva van;
//  - a galéria „Címkék” szakasza (section="tags") és az önálló komponensek (TagChip, TagInput,
//    TagField, TagRow a „Miért?” panellel) adat nélkül is figyelmeztetés nélkül rajzolódnak;
//  - a TagRow a modelljén át működik: választás a beviteli mezőből → felkerül, Backspace az üres
//    mezőben → az utolsó lekerül, a jelek kimennek.
#include "AppContext.h"
#include "QmlApp.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

class TestTagsRender : public QObject {
    Q_OBJECT

    static QStringList render(const QString& page, const QVariantMap& props, const QSize& size, int waitMs = 120)
    {
        QStringList warnings;
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&warnings](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        if (!loadPage(engine, page, props, size)) {
            warnings << QStringLiteral("nem tölthető be: %1").arg(page);
            return warnings;
        }
        QTest::qWait(waitMs);
        return warnings;
    }

private slots:
    void initTestCase() { AppContext::instance()->setDemo(true); }
    void cleanupTestCase() { AppContext::instance()->setThemeMode(QStringLiteral("light")); }

    void tagsWindowStates_data()
    {
        QTest::addColumn<QString>("theme");
        QTest::addColumn<QString>("state");
        for (const char* theme : {"light", "dark"})
            for (const char* state : {"T10", "T11", "T12", "T13", "rename"})
                QTest::addRow("%s-%s", state, theme) << QString::fromLatin1(theme) << QString::fromLatin1(state);
    }
    void tagsWindowStates()
    {
        QFETCH(QString, theme);
        QFETCH(QString, state);
        AppContext::instance()->setThemeMode(theme);
        QQmlEngine engine;
        setupEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "TagsWindow");
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{"demoState", state}}));
        auto* win = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(win);
        win->show();
        QTest::qWait(120);
        auto* merge = win->property("mergeDialog").value<QObject*>();
        auto* del = win->property("deleteDialog").value<QObject*>();
        QCOMPARE(merge->property("visible").toBool(), state == QLatin1String("T11"));
        QCOMPARE(del->property("visible").toBool(), state == QLatin1String("T12"));
        if (state == QLatin1String("T11"))
            QCOMPARE(merge->property("chosen").toString(), QStringLiteral("t-nordvikas"));
        if (state == QLatin1String("T12"))
            QCOMPARE(del->property("title").toString(), QStringLiteral("Törlöd a #Nordvik címkét?"));
        if (state == QLatin1String("rename"))
            QVERIFY(win->property("detailPane").value<QObject*>()->property("renaming").toBool());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void gallerySectionAndComponents_data()
    {
        QTest::addColumn<QString>("theme");
        QTest::newRow("light") << QStringLiteral("light");
        QTest::newRow("dark") << QStringLiteral("dark");
    }
    void gallerySectionAndComponents()
    {
        QFETCH(QString, theme);
        AppContext::instance()->setThemeMode(theme);
        QStringList warnings = render("Gallery", {{"section", "tags"}}, QSize(1280, 1100));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
        // Önállóan, adat nélkül.
        for (const QString& page : {QStringLiteral("TagChip"), QStringLiteral("TagInput"), QStringLiteral("TagField"),
                                    QStringLiteral("TagRow")}) {
            warnings = render(page, {}, QSize(700, 400));
            QVERIFY2(warnings.isEmpty(), qPrintable(page + ": " + warnings.join(QLatin1Char('\n'))));
        }
        warnings = render("TagRow", {{"demoState", "why"}, {"demoWhyOpen", true}}, QSize(1000, 600));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
        warnings = render("TagChip", {{"kind", "llmNew"}, {"text", "Ügyfélsiker"}, {"compact", true}}, QSize(300, 60));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
        warnings = render("TagChip", {{"kind", "partial"}, {"text", "Partnerek"}, {"countText", "1/3"}}, QSize(300, 60));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void tagRowAppliesThroughItsModel()
    {
        QQmlEngine engine;
        setupEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) warnings << e.toString();
        });
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", "TagRow");
        QVERIFY2(!comp.isError(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> row(comp.createWithInitialProperties({{"demoState", "few"}, {"width", 900}}));
        QVERIFY(row);
        QCOMPARE(row->property("tags").toList().size(), 2);
        QSignalSpy added(row.get(), SIGNAL(addRequested(QString, bool)));
        QSignalSpy removed(row.get(), SIGNAL(removeRequested(QString)));

        QVERIFY(QMetaObject::invokeMethod(row.get(), "startAdd"));
        QVERIFY(row->property("adding").toBool());
        auto* input = row->property("input").value<QObject*>();
        QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "tagChosen", Q_ARG(QString, QStringLiteral("MuseumPlus")), Q_ARG(bool, false)));
        QCOMPARE(added.count(), 1);
        QCOMPARE(row->property("tags").toList().size(), 3);
        // Kézi hozzáadás után együtt járó javaslatok.
        QCOMPARE(row->property("suggestionLabel").toString(), QStringLiteral("MuseumPlus mellé gyakran"));

        QVERIFY(QMetaObject::invokeMethod(input, "backspaceOnEmpty"));
        QCOMPARE(removed.count(), 1);
        QCOMPARE(removed.at(0).at(0).toString(), QStringLiteral("t-museumplus"));
        QCOMPARE(row->property("tags").toList().size(), 2);

        QVERIFY(QMetaObject::invokeMethod(input, "closed"));
        QVERIFY(!row->property("adding").toBool());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestTagsRender)
#include "test_tags_render.moc"

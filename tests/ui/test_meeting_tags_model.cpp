// MeetingTagsModel — a fejléc címkesora (C03/C04) és a „Miért?” panel adatai.
//  - controller nélkül a T01 sorai (none … llm, why), a feliratok a forrás szerint;
//  - elfogadás / elutasítás / „Mind” / eltávolítás: egy-egy visszavonható lépés a
//    lépés nevével (toast), a visszavonás után a javaslat visszajön;
//  - kézi hozzáadás után együtt járó javaslat („MuseumPlus mellé gyakran”);
//  - a varrat: ál-backenddel a hívások (forrás: kézi / javaslat / nyelvi modell), az elutasítottak
//    és a már felrakottak kiszűrése.
#include "AppContext.h"
#include "MeetingTagsModel.h"
#include "test_tag_backend.h"

#include <QSignalSpy>
#include <QtTest>

using namespace tanara_qml;

namespace {

QStringList tagNames(const MeetingTagsModel& m)
{
    QStringList out;
    for (const QVariant& v : m.tags()) out << v.toMap().value("name").toString();
    return out;
}
QStringList suggestionNames(const MeetingTagsModel& m)
{
    QStringList out;
    for (const QVariant& v : m.suggestions()) out << v.toMap().value("name").toString();
    return out;
}

} // namespace

class TestMeetingTagsModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { AppContext::instance()->setDemo(true); }

    void demoStates()
    {
        MeetingTagsModel m;
        m.setDemoState("none");
        QVERIFY(m.tags().isEmpty());
        QVERIFY(m.suggestions().isEmpty());
        QCOMPARE(m.suggestionLabel(), QString());
        QVERIFY(!m.computing());

        m.setDemoState("few");
        QCOMPARE(tagNames(m), QStringList({"Nordvik", "Partnerek"}));

        m.setDemoState("many");
        QCOMPARE(m.tags().size(), 10);

        m.setDemoState("computing");
        QVERIFY(m.computing());
        QCOMPARE(tagNames(m), QStringList({"Nordvik"}));

        m.setDemoState("similar");
        QVERIFY(!m.computing());
        QCOMPARE(suggestionNames(m), QStringList({"Partnerek", "Q4 tervezés"}));
        QCOMPARE(m.suggestionLabel(), QStringLiteral("Javasolt"));
        QCOMPARE(m.suggestionSource(), QStringLiteral("similar"));

        m.setDemoState("cooccur");
        QCOMPARE(tagNames(m), QStringList({"MuseumPlus"}));
        QCOMPARE(suggestionNames(m), QStringList({"MÉM-MDK", "Gyűjteménykezelés"}));
        QCOMPARE(m.suggestionLabel(), QStringLiteral("MuseumPlus mellé gyakran"));

        m.setDemoState("llm");
        QCOMPARE(m.suggestionLabel(), QStringLiteral("Az összefoglaló alapján"));
        QCOMPARE(m.suggestions().at(1).toMap().value("isNew").toBool(), true);
        QCOMPARE(m.suggestions().at(0).toMap().value("isNew").toBool(), false);
        QVERIFY(!m.canUndo());
    }

    void acceptAndUndo()
    {
        MeetingTagsModel m;
        m.setDemoState("similar");
        QSignalSpy toast(&m, &MeetingTagsModel::toast);
        m.accept(0);
        QCOMPARE(tagNames(m), QStringList({"Nordvik", "Partnerek"}));
        QCOMPARE(suggestionNames(m), QStringList({"Q4 tervezés"}));
        QVERIFY(m.canUndo());
        QCOMPARE(m.undoLabel(), QStringLiteral("Javaslat elfogadva: #Partnerek"));
        QCOMPARE(toast.count(), 1);
        QCOMPARE(toast.at(0).at(0).toString(), QStringLiteral("Javaslat elfogadva: #Partnerek"));
        QCOMPARE(toast.at(0).at(1).toBool(), true);
        m.undo();
        QCOMPARE(tagNames(m), QStringList({"Nordvik"}));
        QCOMPARE(suggestionNames(m), QStringList({"Partnerek", "Q4 tervezés"}));
        QVERIFY(!m.canUndo());
    }

    void rejectAndUndo()
    {
        MeetingTagsModel m;
        m.setDemoState("similar");
        m.reject(1);
        QCOMPARE(suggestionNames(m), QStringList({"Partnerek"}));
        QCOMPARE(tagNames(m), QStringList({"Nordvik"}));
        QCOMPARE(m.undoLabel(), QStringLiteral("Javaslat elutasítva: #Q4 tervezés"));
        m.reject(0);
        QVERIFY(m.suggestions().isEmpty());
        QCOMPARE(m.suggestionLabel(), QString());       // javaslat nélkül nincs felirat
        m.undo();
        QCOMPARE(suggestionNames(m), QStringList({"Partnerek"}));
        m.undo();
        QCOMPARE(suggestionNames(m), QStringList({"Partnerek", "Q4 tervezés"}));
        // Érvénytelen index: semmi.
        m.reject(7);
        m.accept(-1);
        QVERIFY(!m.canUndo());
    }

    void acceptAllIsOneStep()
    {
        MeetingTagsModel m;
        m.setDemoState("llm");
        m.acceptAll();
        QCOMPARE(tagNames(m), QStringList({"Nordvik", "Termék", "Ügyfélsiker"}));
        QVERIFY(m.suggestions().isEmpty());
        QCOMPARE(m.undoLabel(), QStringLiteral("2 javaslat elfogadva"));
        m.undo();
        QCOMPARE(tagNames(m), QStringList({"Nordvik"}));
        QCOMPARE(suggestionNames(m), QStringList({"Termék", "Ügyfélsiker"}));
        QVERIFY(!m.canUndo());
    }

    void manualAddAsksForCooccurring()
    {
        MeetingTagsModel m;
        m.setDemoState("few");
        const QString id = m.add(QStringLiteral("museumplus"));      // név szerint, kisbetűvel is
        QCOMPARE(id, QStringLiteral("t-museumplus"));
        QCOMPARE(tagNames(m), QStringList({"Nordvik", "Partnerek", "MuseumPlus"}));
        QCOMPARE(m.suggestionLabel(), QStringLiteral("MuseumPlus mellé gyakran"));
        QCOMPARE(suggestionNames(m), QStringList({"MÉM-MDK", "Gyűjteménykezelés"}));
        QCOMPARE(m.undoLabel(), QStringLiteral("Címke hozzáadva: #MuseumPlus"));

        // Ami már rajta van, nem kerül fel újra; új név létrejön.
        QCOMPARE(m.add(QStringLiteral("Nordvik")), QStringLiteral("t-nordvik"));
        QCOMPARE(m.tags().size(), 3);
        m.add(QStringLiteral("  Pilot   2027 "));
        QCOMPARE(tagNames(m).last(), QStringLiteral("Pilot 2027"));
        QVERIFY(m.add(QStringLiteral("   ")).isEmpty());

        m.remove(QStringLiteral("t-nordvik"));
        QCOMPARE(tagNames(m), QStringList({"Partnerek", "MuseumPlus", "Pilot 2027"}));
        QCOMPARE(m.undoLabel(), QStringLiteral("Címke eltávolítva: #Nordvik"));
        m.undo();
        QCOMPARE(tagNames(m).first(), QStringLiteral("Nordvik"));
    }

    void whyDataHasReasonsAndLinks()
    {
        MeetingTagsModel m;
        m.setDemoState("why");
        const QVariantList why = m.whyData();
        QCOMPARE(why.size(), 2);
        const QVariantMap first = why.at(0).toMap();
        QCOMPARE(first.value("name").toString(), QStringLiteral("Nordvik"));
        const QVariantList reasons = first.value("reasons").toList();
        QCOMPARE(reasons.size(), 2);
        QCOMPARE(reasons.at(0).toMap().value("label").toString(), QStringLiteral("Közös résztvevő"));
        QCOMPARE(reasons.at(0).toMap().value("text").toString(), QStringLiteral("Varga Nóra, Fehér Ádám"));
        QCOMPARE(reasons.at(1).toMap().value("label").toString(), QStringLiteral("Közös kifejezések"));
        const QVariantList links = first.value("similarMeetings").toList();
        QCOMPARE(links.size(), 2);
        QCOMPARE(links.at(0).toMap().value("title").toString(), QStringLiteral("Nordvik heti meeting"));
        QVERIFY(links.at(0).toMap().value("dateText").toString().contains(QLatin1String("29")));
        QCOMPARE(why.at(1).toMap().value("reasons").toList().at(0).toMap().value("label").toString(),
                 QStringLiteral("Hasonló cím"));
    }

    void separateMeetingsAreIndependent()
    {
        MeetingTagsModel a, b;
        a.setMeetingId("m-a");
        b.setMeetingId("m-b");
        a.add("Nordvik");
        QCOMPARE(a.tags().size(), 1);
        QCOMPARE(b.tags().size(), 0);
    }

    // ---- a varrat: ál-backend a controller helyén ----
    void backendSeam()
    {
        FakeTagBackend backend;
        backend.addItem("x", "Xenon", 3);
        backend.addItem("y", "Ypszilon", 5);
        backend.addItem("z", "Zéta", 1);
        backend.onMeeting["m1"] = {"x"};
        TagSuggestionState s;
        s.source = "llm";
        s.items = {{"x", "Xenon", false, "llm", 0.9, {}, {}},          // már rajta van → kiesik
                   {"y", "Ypszilon", false, "llm", 0.8, {}, {}},
                   {"", "Omega", true, "llm", 0.5, {}, {}},
                   {"z", "Zéta", false, "llm", 0.4, {}, {}}};
        backend.pending["m1"] = s;
        backend.rejected.insert("m1|z");                               // elutasított → kiesik

        MeetingTagsModel m;
        m.setBackend(&backend);
        m.setMeetingId("m1");
        QCOMPARE(tagNames(m), QStringList({"Xenon"}));
        QCOMPARE(suggestionNames(m), QStringList({"Ypszilon", "Omega"}));
        QCOMPARE(m.suggestionSource(), QStringLiteral("llm"));

        m.accept(0);
        QVERIFY(backend.calls.contains("add:m1:y:llm"));
        QCOMPARE(backend.groups.last(), QStringLiteral("Javaslat elfogadva: #Ypszilon"));
        QCOMPARE(suggestionNames(m), QStringList({"Omega"}));

        m.reject(0);
        QVERIFY(backend.calls.contains("reject:m1:Omega"));
        QVERIFY(m.suggestions().isEmpty());

        m.add("Zéta");
        QVERIFY(backend.calls.contains("add:m1:Zéta:manual"));
        QVERIFY(backend.calls.contains("cooccur:m1:z"));

        m.remove("x");
        QVERIFY(backend.calls.contains("remove:m1:x"));
        m.requestSuggestions();
        QVERIFY(backend.calls.contains("request:m1"));
        m.undo();
        QVERIFY(backend.calls.contains("undo"));

        // Számolás jelzése.
        TagSuggestionState busy;
        busy.computing = true;
        backend.pending["m1"] = busy;
        emit backend.suggestionsChanged("m1");
        QVERIFY(m.computing());
        // Másik megbeszélés jele nem érinti.
        QSignalSpy spy(&m, &MeetingTagsModel::suggestionsChanged);
        emit backend.suggestionsChanged("m2");
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestMeetingTagsModel)
#include "test_meeting_tags_model.moc"

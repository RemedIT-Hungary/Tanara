// MeetingTagsModel: the header's tag row and the data of the "Why?" panel.
//  - without a controller the sample rows (none ... llm, why), the labels by source;
//  - accept / reject / "All" / remove: one undoable step each, named after the step (toast),
//    and after undo the suggestion returns;
//  - after a manual add, a co-occurring suggestion ("often goes with Muzeo");
//  - the seam: with a fake backend the calls (source: manual / suggestion / language model),
//    filtering out the rejected and the already applied.
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
        QCOMPARE(tagNames(m), QStringList({"Muzeo"}));
        QCOMPARE(suggestionNames(m), QStringList({"MÉM-MDK", "Gyűjteménykezelés"}));
        QCOMPARE(m.suggestionLabel(), QStringLiteral("Muzeo mellé gyakran"));

        m.setDemoState("llm");
        QCOMPARE(m.suggestionLabel(), QStringLiteral("Az összefoglaló alapján"));
        QCOMPARE(m.suggestions().at(1).toMap().value("isNew").toBool(), true);
        QCOMPARE(m.suggestions().at(0).toMap().value("isNew").toBool(), false);
        QVERIFY(!m.canUndo());
    }

    // Before transcription, with no regular suggestion, the title-based fallback (draftTitle)
    // suggests; with a regular suggestion it does not; a rejected / applied one drops out, and
    // can be accepted.
    void draftTitleFallback()
    {
        MeetingTagsModel m;
        m.setDemoState("none");
        QVERIFY(m.suggestions().isEmpty());
        QSignalSpy spy(&m, &MeetingTagsModel::suggestionsChanged);
        m.setDraftTitle(QStringLiteral("Ügyféltámogatás heti"));
        QVERIFY(spy.count() >= 1);
        QCOMPARE(suggestionNames(m), QStringList({"Ügyféltámogatás"}));
        QCOMPARE(m.suggestionSource(), QStringLiteral("title"));
        QCOMPARE(m.suggestionLabel(), QStringLiteral("A cím alapján"));
        m.reject(0);
        QVERIFY(m.suggestions().isEmpty());
        m.undo();
        QCOMPARE(suggestionNames(m), QStringList({"Ügyféltámogatás"}));
        m.accept(0);
        QVERIFY(tagNames(m).contains(QStringLiteral("Ügyféltámogatás")));
        QVERIFY(m.suggestions().isEmpty());              // an applied tag does not come back as a suggestion
        m.undo();
        m.setDraftTitle(QString());
        QVERIFY(m.suggestions().isEmpty());
        // With a regular suggestion the fallback stays out.
        m.setDemoState("similar");
        m.setDraftTitle(QStringLiteral("Ügyféltámogatás heti"));
        QCOMPARE(m.suggestionSource(), QStringLiteral("similar"));
        QVERIFY(!suggestionNames(m).contains(QStringLiteral("Ügyféltámogatás")));
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
        QCOMPARE(m.suggestionLabel(), QString());       // no suggestion, no label
        m.undo();
        QCOMPARE(suggestionNames(m), QStringList({"Partnerek"}));
        m.undo();
        QCOMPARE(suggestionNames(m), QStringList({"Partnerek", "Q4 tervezés"}));
        // Invalid index: nothing.
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
        const QString id = m.add(QStringLiteral("muzeo"));      // by name, lowercase too
        QCOMPARE(id, QStringLiteral("t-muzeo"));
        QCOMPARE(tagNames(m), QStringList({"Nordvik", "Partnerek", "Muzeo"}));
        QCOMPARE(m.suggestionLabel(), QStringLiteral("Muzeo mellé gyakran"));
        QCOMPARE(suggestionNames(m), QStringList({"MÉM-MDK", "Gyűjteménykezelés"}));
        QCOMPARE(m.undoLabel(), QStringLiteral("Címke hozzáadva: #Muzeo"));

        // What is already on is not added again; a new name is created.
        QCOMPARE(m.add(QStringLiteral("Nordvik")), QStringLiteral("t-nordvik"));
        QCOMPARE(m.tags().size(), 3);
        m.add(QStringLiteral("  Pilot   2027 "));
        QCOMPARE(tagNames(m).last(), QStringLiteral("Pilot 2027"));
        QVERIFY(m.add(QStringLiteral("   ")).isEmpty());

        m.remove(QStringLiteral("t-nordvik"));
        QCOMPARE(tagNames(m), QStringList({"Partnerek", "Muzeo", "Pilot 2027"}));
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

    void peopleSourceDemo()
    {
        MeetingTagsModel m;
        m.setDemoState("peopleWhy");
        QCOMPARE(m.suggestionSource(), QStringLiteral("people"));
        QCOMPARE(m.suggestionLabel(), QStringLiteral("A résztvevők alapján"));
        const QVariantList why = m.whyData();
        QCOMPARE(why.size(), 3);
        const QVariantMap r0 = why.at(0).toMap().value("reasons").toList().at(0).toMap();
        QCOMPARE(r0.value("label").toString(), QStringLiteral("Résztvevők címkéje"));
        QCOMPARE(r0.value("text").toString(), QStringLiteral("Fehér Gábor, Varga Árpád (mindkettőjükön)"));
        const QVariantMap r1 = why.at(1).toMap().value("reasons").toList().at(0).toMap();
        QCOMPARE(r1.value("text").toString(), QStringLiteral("Fehér Gábor"));
        QCOMPARE(why.at(2).toMap().value("source").toString(), QStringLiteral("similar"));
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

    // ---- the seam: a fake backend in place of the controller ----
    void backendSeam()
    {
        FakeTagBackend backend;
        backend.addItem("x", "Xenon", 3);
        backend.addItem("y", "Ypszilon", 5);
        backend.addItem("z", "Zéta", 1);
        backend.onMeeting["m1"] = {"x"};
        TagSuggestionState s;
        s.source = "llm";
        s.items = {{"x", "Xenon", false, "llm", 0.9, {}, {}},          // already applied: dropped
                   {"y", "Ypszilon", false, "llm", 0.8, {}, {}},
                   {"", "Omega", true, "llm", 0.5, {}, {}},
                   {"z", "Zéta", false, "llm", 0.4, {}, {}}};
        backend.pending["m1"] = s;
        backend.rejected.insert("m1|z");                               // rejected: dropped

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

        // Signals the count.
        TagSuggestionState busy;
        busy.computing = true;
        backend.pending["m1"] = busy;
        emit backend.suggestionsChanged("m1");
        QVERIFY(m.computing());
        // Another meeting's signal does not affect it.
        QSignalSpy spy(&m, &MeetingTagsModel::suggestionsChanged);
        emit backend.suggestionsChanged("m2");
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestMeetingTagsModel)
#include "test_meeting_tags_model.moc"

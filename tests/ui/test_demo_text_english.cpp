// The English version of the demo content (DemoText): with an English demo language the made-up
// meeting's title, the transcript's first line, the library's first title and the participants'
// names are English, with no accented Hungarian word; with Hungarian they stay the Hungarian demo
// texts. The structure of the demo states (line count, timing, speaker-key order) is the same in both languages.
#include "AppContext.h"
#include "DemoText.h"
#include "LibraryDemoData.h"
#include "OverviewViewModel.h"
#include "TranscriptDemoSession.h"

#include "tanara/editing/session/SpeakerEditor.h"

#include <QRegularExpression>
#include <QtTest>

using namespace tanara_qml;

namespace {

// A Hungarian accented letter (cannot appear in the English demo text).
bool hasHungarianAccent(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("[áéíóöőúüűÁÉÍÓÖŐÚÜŰ]"));
    return re.match(s).hasMatch();
}

struct Shape {
    int lines = 0;
    QVector<qint64> starts;
    QStringList rawOrder;
};

Shape shapeOf(const TranscriptDemoSession& s)
{
    Shape out;
    tanara::editing::SpeakerEditor* ed = s.editor();
    out.lines = ed->utteranceCount();
    for (int i = 0; i < out.lines; ++i) {
        const tanara::editing::EditorUtterance u = ed->utteranceAt(i);
        out.starts << u.startMs;
        if (!out.rawOrder.contains(u.rawLabel)) out.rawOrder << u.rawLabel;
    }
    return out;
}

} // namespace

class TestDemoTextEnglish : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { demotext::setLanguageOverride(QString()); }

    void switchAndNames()
    {
        demotext::setLanguageOverride(QStringLiteral("hu"));
        QVERIFY(!demotext::english());
        QCOMPARE(demoText("Negyedéves partnertalálkozó", "Quarterly partner review"),
                 QStringLiteral("Negyedéves partnertalálkozó"));
        QCOMPARE(demoName("Kovács Lilla"), QStringLiteral("Kovács Lilla"));
        QCOMPARE(demoRaw(QStringLiteral("Távoli 1")), QStringLiteral("Távoli 1"));

        demotext::setLanguageOverride(QStringLiteral("en"));
        QVERIFY(demotext::english());
        QCOMPARE(demoText("Negyedéves partnertalálkozó", "Quarterly partner review"),
                 QStringLiteral("Quarterly partner review"));
        QCOMPARE(demoName("Kovács Lilla"), QStringLiteral("Emma Carter"));
        QCOMPARE(demoName(QStringLiteral("Ismeretlen Név")), QStringLiteral("Ismeretlen Név"));
        QCOMPARE(demoRaw(QStringLiteral("Távoli 1")), QStringLiteral("Remote 1"));
        QCOMPARE(demoRaw(QStringLiteral("Beszélő 2@mic")), QStringLiteral("Speaker 2@mic"));
    }

    void libraryIsEnglish()
    {
        demotext::setLanguageOverride(QStringLiteral("en"));
        const auto& all = demo::meetings();
        QVERIFY(!all.isEmpty());
        const QString first = all.first().entry.title;
        QCOMPARE(first, QStringLiteral("Product team weekly sync"));
        for (const demo::DemoMeeting& d : all) {
            QVERIFY2(!hasHungarianAccent(d.entry.title), qPrintable(d.entry.title));
            QVERIFY2(!hasHungarianAccent(d.text), qPrintable(d.text));
            for (const QString& p : d.entry.participants) QVERIFY2(!hasHungarianAccent(p), qPrintable(p));
        }
        QCOMPARE(demo::entry(demo::defaultMeetingId()).title, QStringLiteral("Quarterly partner review"));
        for (const QString& tag : demo::entry(demo::defaultMeetingId()).tagNames)
            QVERIFY2(!hasHungarianAccent(tag), qPrintable(tag));

        // In Hungarian it is the usual library (the per-language cache does not get mixed).
        demotext::setLanguageOverride(QStringLiteral("hu"));
        QCOMPARE(demo::meetings().first().entry.title, QStringLiteral("Termékcsapat heti egyeztetés"));
        QCOMPARE(demo::entry(demo::defaultMeetingId()).title, QStringLiteral("Negyedéves partnertalálkozó"));
    }

    void transcriptIsEnglishWithSameShape()
    {
        demotext::setLanguageOverride(QStringLiteral("hu"));
        TranscriptDemoSession hu(QStringLiteral("v3"));
        const Shape huShape = shapeOf(hu);
        QCOMPARE(hu.selfName(), QStringLiteral("Kovács Lilla"));

        demotext::setLanguageOverride(QStringLiteral("en"));
        TranscriptDemoSession en(QStringLiteral("v3"));
        const Shape enShape = shapeOf(en);
        QCOMPARE(en.selfName(), QStringLiteral("Emma Carter"));

        // The same number of lines, in the same places, with the same speaker order (only the labels are English).
        QCOMPARE(enShape.lines, huShape.lines);
        QCOMPARE(enShape.starts, huShape.starts);
        QCOMPARE(enShape.rawOrder.size(), huShape.rawOrder.size());
        for (int i = 0; i < huShape.rawOrder.size(); ++i) {
            demotext::setLanguageOverride(QStringLiteral("en"));
            QCOMPARE(enShape.rawOrder.at(i), demoRaw(huShape.rawOrder.at(i)));
        }

        const QString firstLine = en.editor()->utteranceAt(0).text;
        QVERIFY(firstLine.startsWith(QStringLiteral("Yes, and on top of that")));
        for (int i = 0; i < enShape.lines; ++i) {
            const QString t = en.editor()->utteranceAt(i).text;
            QVERIFY2(!hasHungarianAccent(t), qPrintable(QStringLiteral("%1: %2").arg(i).arg(t)));
        }
        for (const tanara::editing::EditorSpeaker& sp : en.editor()->speakers())
            QVERIFY2(!hasHungarianAccent(sp.displayName), qPrintable(sp.displayName));
    }

    void overviewTitleIsEnglish()
    {
        demotext::setLanguageOverride(QStringLiteral("en"));
        AppContext::instance()->setDemo(true);
        OverviewViewModel vm;
        vm.setDemoState(QStringLiteral("overviewDone"));
        QCOMPARE(vm.title(), QStringLiteral("Quarterly partner review"));
        QVERIFY(!hasHungarianAccent(vm.title()));
        bool self = false;
        for (const QVariant& v : vm.participants()) {
            const QString name = v.toMap().value(QStringLiteral("name")).toString();
            QVERIFY2(!hasHungarianAccent(name), qPrintable(name));
            self = self || name == QStringLiteral("Emma Carter (you)");
        }
        QVERIFY(self);
        AppContext::instance()->setDemo(false);
    }
};

QTEST_MAIN(TestDemoTextEnglish)
#include "test_demo_text_english.moc"

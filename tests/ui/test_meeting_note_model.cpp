// MeetingNoteModel — a megbeszélés-megjegyzés szerkesztése az Összefoglaló fülön és az átirat
// előtti nézetben, izolált TANARA_HOME-on, ál-LLM-szerverrel (valódi szolgáltató-hívás nincs):
// mentés a saját megbeszélésbe kijelölés-váltáskor is, sablon-javaslatok, csere / hozzáfűzés,
// „a megjegyzés azóta változott” jelzés, észlelt hívás. Minden név és szó kitalált.
#include "JobTestSupport.h"
#include "MeetingNoteModel.h"
#include "PreTranscriptViewModel.h"
#include "SummaryViewModel.h"

#include "tanara/summary/SummaryStore.h"

#include <QSignalSpy>
#include <QtTest>

using namespace tanara;
using tanara_qml::MeetingNoteModel;
using tanara_qml::PreTranscriptViewModel;
using tanara_qml::SummaryViewModel;

namespace {

const char* kQuickJson =
    "{\"execSummary\":\"Rövid egyeztetés a Nordvik bevezetéséről.\","
    "\"decisions\":[],\"actionItems\":[],\"participants\":[]}";

void setNote(jobtest::Sandbox& sb, const QString& id, const QString& note, const QDateTime& when)
{
    Meeting m = sb.app->store()->load(id);
    m.contextNote = note;
    m.startedAt = when;
    sb.app->store()->saveMeeting(m);
}

QString pick(MeetingNoteModel* note, int index)
{
    return note->suggestions().value(index).toMap().value(QStringLiteral("note")).toString();
}

} // namespace

class TestMeetingNoteModel : public QObject {
    Q_OBJECT
private slots:
    void demoContent()
    {
        SummaryViewModel vm;
        QVERIFY(vm.demo());
        QVERIFY(!vm.note()->note().isEmpty());
        QVERIFY(!vm.noteChangedSinceSummary());
        vm.setDemoState(QStringLiteral("noteChanged"));
        QVERIFY(vm.noteChangedSinceSummary());
        vm.setDemoState(QStringLiteral("noteOpen"));
        QVERIFY(vm.noteOpen());
        QCOMPARE(vm.note()->suggestions().size(), 2);
        vm.setDemoState(QStringLiteral("emptyNote"));
        QCOMPARE(vm.view(), QStringLiteral("empty"));
        QVERIFY(vm.note()->note().isEmpty());
        QCOMPARE(vm.note()->detectedCallApp(), QStringLiteral("Microsoft Teams"));

        PreTranscriptViewModel pre;
        pre.setDemoState(QStringLiteral("note"));
        QCOMPARE(pre.note()->suggestions().size(), 2);
        QCOMPARE(pre.note()->detectedCallApp(), QStringLiteral("Microsoft Teams"));
        // Demóban is bemásolható (csak a memóriában), és a bemásolt eltűnik a javaslatok közül.
        pre.note()->applySuggestion(pick(pre.note(), 0), QStringLiteral("replace"));
        QVERIFY(pre.contextNote().contains(QStringLiteral("Nordvik")));
        QCOMPARE(pre.note()->suggestions().size(), 1);
    }

    void summaryNoteEditBoundToItsMeeting()
    {
        jobtest::Sandbox sb;
        const Meeting a = sb.transcribed(QStringLiteral("Nordvik heti meeting"));
        const Meeting b = sb.transcribed(QStringLiteral("Lantos árazás"));

        SummaryViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(a.id);
        MeetingNoteModel* note = vm.note();
        QCOMPARE(note->meetingId(), a.id);

        // Mentés: a meeting.json-ba (a piszkozat csak a commitkor íródik ki).
        note->draft(QStringLiteral("A „Nordwig” helyesen: Nordvik."));
        QVERIFY(sb.app->store()->load(a.id).contextNote.isEmpty());
        note->commitDraft();
        QCOMPARE(sb.app->store()->load(a.id).contextNote, QStringLiteral("A „Nordwig” helyesen: Nordvik."));

        // Kijelölés-váltás a késleltetett mentés ELŐTT: a piszkozat a SAJÁT megbeszélésébe kerül.
        vm.setNoteOpen(true);
        note->draft(QStringLiteral("A „pixel tár” helyesen: PixelTár."));
        vm.setMeetingId(b.id);
        QCOMPARE(sb.app->store()->load(a.id).contextNote, QStringLiteral("A „pixel tár” helyesen: PixelTár."));
        QVERIFY(sb.app->store()->load(b.id).contextNote.isEmpty());
        QVERIFY(note->note().isEmpty());                     // már a b megjegyzése látszik
        QVERIFY(!vm.noteOpen());                             // meeting-váltáskor a blokk bezárul
        note->commitDraft();                                 // nincs függő piszkozat → semmi
        QVERIFY(sb.app->store()->load(b.id).contextNote.isEmpty());

        // Másik nézetből (pl. átirat előtti nézet) mentett megjegyzés itt is megjelenik.
        sb.app->setMeetingContextNote(b.id, QStringLiteral("Lantos: Qvarko-modul."));
        QCOMPARE(note->note(), QStringLiteral("Lantos: Qvarko-modul."));
    }

    void suggestionsReplaceAndAppend()
    {
        jobtest::Sandbox sb;
        const Meeting older = sb.recording(QStringLiteral("Nordvik heti meeting"), 1);
        const Meeting newer = sb.recording(QStringLiteral("nordvik HETI meeting #13"), 1);
        const Meeting other = sb.recording(QStringLiteral("Lantos árazás"), 1);
        const Meeting self = sb.recording(QStringLiteral("Nordvik heti meeting #14"), 1);
        const QDateTime base(QDate(2026, 9, 1), QTime(10, 0));
        setNote(sb, older.id, QStringLiteral("Résztvevők: Kovács Anna."), base);
        setNote(sb, newer.id, QStringLiteral("A „Nordwig” helyesen: Nordvik."), base.addDays(7));
        setNote(sb, other.id, QStringLiteral("Más téma."), base.addDays(8));
        setNote(sb, self.id, QString(), base.addDays(14));

        PreTranscriptViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(self.id);
        MeetingNoteModel* note = vm.note();
        QVariantList s = note->suggestions();
        QCOMPARE(s.size(), 2);                                // a nem rokon cím kimarad
        QCOMPARE(s.at(0).toMap().value("meetingId").toString(), newer.id);   // legújabb elöl
        QCOMPARE(s.at(0).toMap().value("title").toString(), QStringLiteral("nordvik HETI meeting #13"));
        QVERIFY(!s.at(0).toMap().value("dateText").toString().isEmpty());
        QCOMPARE(s.at(1).toMap().value("meetingId").toString(), older.id);
        QVERIFY(note->note().isEmpty());                      // semmi nem töltődik ki magától

        // Üres mezőbe: egyszerű bemásolás, azonnal mentve; a bemásolt eltűnik a javaslatok közül.
        QSignalSpy changed(&vm, &PreTranscriptViewModel::contextNoteChanged);
        note->applySuggestion(pick(note, 0), QStringLiteral("replace"));
        QCOMPARE(sb.app->store()->load(self.id).contextNote, QStringLiteral("A „Nordwig” helyesen: Nordvik."));
        QVERIFY(changed.count() >= 1);
        QCOMPARE(note->suggestions().size(), 1);
        QCOMPARE(note->suggestions().at(0).toMap().value("meetingId").toString(), older.id);

        // Hozzáfűzés: a meglévő után, új sorba — egy közben gépelt piszkozat is a meglévő része.
        note->draft(QStringLiteral("A „Nordwig” helyesen: Nordvik. Téma: bevezetés."));
        note->applySuggestion(pick(note, 0), QStringLiteral("append"));
        QCOMPARE(sb.app->store()->load(self.id).contextNote,
                 QStringLiteral("A „Nordwig” helyesen: Nordvik. Téma: bevezetés.\nRésztvevők: Kovács Anna."));
        QVERIFY(note->suggestions().size() == 2);             // a mostani már egyikkel sem azonos

        // Csere: a régi szöveg helyére.
        QCOMPARE(pick(note, 1), QStringLiteral("Résztvevők: Kovács Anna."));
        note->applySuggestion(pick(note, 1), QStringLiteral("replace"));
        QCOMPARE(sb.app->store()->load(self.id).contextNote, QStringLiteral("Résztvevők: Kovács Anna."));
        note->applySuggestion(QStringLiteral("   "), QStringLiteral("replace"));   // üres → semmi
        QCOMPARE(sb.app->store()->load(self.id).contextNote, QStringLiteral("Résztvevők: Kovács Anna."));
    }

    void detectedCallShownNotSent()
    {
        jobtest::Sandbox sb;
        Meeting m = sb.recording(QStringLiteral("Teams-hívás · okt. 3. 14:02"), 1);
        sb.app->setMeetingDetectedCall(m.id, QStringLiteral("Microsoft Teams"));
        // Régi build automatikus mondata a mezőben: üres megjegyzésnek látszik.
        m = sb.app->store()->load(m.id);
        m.contextNote = QStringLiteral("Automatikusan észlelt hívás: Microsoft Teams");
        sb.app->store()->saveMeeting(m);

        PreTranscriptViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(m.id);
        QVERIFY(vm.contextNote().isEmpty());
        QCOMPARE(vm.note()->detectedCallApp(), QStringLiteral("Microsoft Teams"));
        QVERIFY(!vm.footerLine().contains(QStringLiteral("észlelt")));
    }

    void changedSinceSummaryHint()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.transcribed(QStringLiteral("Nordvik heti meeting"));
        sb.app->setMeetingContextNote(m.id, QStringLiteral("A „Nordwig” helyesen: Nordvik."));

        SummaryViewModel vm;
        vm.setController(sb.app.get());
        vm.setMeetingId(m.id);
        QCOMPARE(vm.view(), QStringLiteral("empty"));
        QVERIFY(!vm.noteChangedSinceSummary());

        // A gyors összefoglaló a megjegyzést kontextusként kapja, és a summary.json megjegyzi.
        QByteArray sent;
        sb.http->handler = [&sent](const jobtest::FakeRequest& r) -> jobtest::FakeReply {
            if (!r.path.endsWith("/chat/completions")) return {404, "{}"};
            sent += r.body;
            return {200, jobtest::chat(QString::fromUtf8(kQuickJson))};
        };
        QSignalSpy arrived(&vm, &SummaryViewModel::summaryArrived);
        sb.app->summarizeMeeting(m.id);
        QVERIFY(arrived.wait(15000));
        QVERIFY(QString::fromUtf8(sent).contains(QStringLiteral("Nordwig")));
        const SummaryDocument doc = sb.app->summaryDocument(m.id);
        QVERIFY(doc.meta.contextNoteKnown);
        QCOMPARE(doc.meta.contextNote, QStringLiteral("A „Nordwig” helyesen: Nordvik."));
        QCOMPARE(vm.view(), QStringLiteral("summary"));
        QVERIFY(!vm.noteChangedSinceSummary());

        // Változás után szelíd jelzés — de NEM elavult (sem itt, sem a könyvtárban).
        QSignalSpy hint(&vm, &SummaryViewModel::noteHintChanged);
        vm.note()->setNote(QStringLiteral("A „Nordwig” helyesen: Nordvik. A „kvarc modul” helyesen: Qvarko-modul."));
        QVERIFY(hint.count() >= 1);
        QVERIFY(vm.noteChangedSinceSummary());
        QVERIFY(!vm.stale());
        QVERIFY(!sb.app->summaryStale(sb.app->store()->load(m.id)).stale);
        // Csak szóköz-eltérés: nem változás.
        vm.note()->setNote(QStringLiteral("  A „Nordwig”  helyesen: Nordvik. "));
        QVERIFY(!vm.noteChangedSinceSummary());

        // Régi összefoglaló (a summary.json nem tudja, mivel készült): nincs jelzés.
        SummaryDocument old = doc;
        old.meta.contextNoteKnown = false;
        QVERIFY(summarystore::save(sb.app->store()->load(m.id).folder, old));
        vm.refresh();
        vm.note()->setNote(QStringLiteral("Egészen más."));
        QVERIFY(!vm.noteChangedSinceSummary());
    }
};

QTEST_MAIN(TestMeetingNoteModel)
#include "test_meeting_note_model.moc"

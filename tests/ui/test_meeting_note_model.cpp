// MeetingNoteModel: editing the meeting note on the Summary tab and in the pre-transcript view,
// on an isolated TANARA_HOME with a fake LLM server (no real provider call): saving into its own
// meeting even on a selection change, template suggestions, replace / append, the "the note
// changed since" indicator, detected call. All names and words are made up.
#include "AppContext.h"
#include "JobTestSupport.h"
#include "MeetingNoteModel.h"
#include "PreTranscriptViewModel.h"
#include "SummaryViewModel.h"

#include "tanara/library/summary/SummaryRepository.h"

#include <QSignalSpy>
#include <QtTest>

using namespace tanara;
using tanara_qml::MeetingNoteModel;
using tanara_qml::PreTranscriptViewModel;
using tanara_qml::SummaryViewModel;

namespace {
// The real summary facade of a sandbox controller (what the hosts hand to the view model).
tanara::processing::ISummaryFacade* summaryFacade(tanara::app::AppController* c)
{
    return tanara_qml::AppContext::instance()->servicesFor(c).summary(QString(), nullptr);
}
} // namespace

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
        // In demo mode it can be pasted too (memory only), and the pasted one disappears from the suggestions.
        pre.note()->applySuggestion(pick(pre.note(), 0), QStringLiteral("replace"));
        QVERIFY(pre.contextNote().contains(QStringLiteral("Nordvik")));
        QCOMPARE(pre.note()->suggestions().size(), 1);
    }

    void summaryNoteEditBoundToItsMeeting()
    {
        jobtest::Sandbox sb;
        const Meeting a = sb.transcribed(QStringLiteral("Nordvik heti meeting"));
        const Meeting b = sb.transcribed(QStringLiteral("Lantos árazás"));

        SummaryViewModel vm(summaryFacade(sb.app.get()));
        vm.setMeetingId(a.id);
        MeetingNoteModel* note = vm.note();
        QCOMPARE(note->meetingId(), a.id);

        // Save: into meeting.json (the draft is written only on commit).
        note->draft(QStringLiteral("A „Nordwig” helyesen: Nordvik."));
        QVERIFY(sb.app->store()->load(a.id).contextNote.isEmpty());
        note->commitDraft();
        QCOMPARE(sb.app->store()->load(a.id).contextNote, QStringLiteral("A „Nordwig” helyesen: Nordvik."));

        // Selection change BEFORE the delayed save: the draft goes into its OWN meeting.
        vm.setNoteOpen(true);
        note->draft(QStringLiteral("A „pixel tár” helyesen: PixelTár."));
        vm.setMeetingId(b.id);
        QCOMPARE(sb.app->store()->load(a.id).contextNote, QStringLiteral("A „pixel tár” helyesen: PixelTár."));
        QVERIFY(sb.app->store()->load(b.id).contextNote.isEmpty());
        QVERIFY(note->note().isEmpty());                     // b's note is already shown
        QVERIFY(!vm.noteOpen());                             // the block closes on a meeting change
        note->commitDraft();                                 // no pending draft, so nothing
        QVERIFY(sb.app->store()->load(b.id).contextNote.isEmpty());

        // A note saved from another view (e.g. the pre-transcript view) shows here too.
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
        QCOMPARE(s.size(), 2);                                // the unrelated title is left out
        QCOMPARE(s.at(0).toMap().value("meetingId").toString(), newer.id);   // newest first
        QCOMPARE(s.at(0).toMap().value("title").toString(), QStringLiteral("nordvik HETI meeting #13"));
        QVERIFY(!s.at(0).toMap().value("dateText").toString().isEmpty());
        QCOMPARE(s.at(1).toMap().value("meetingId").toString(), older.id);
        QVERIFY(note->note().isEmpty());                      // nothing fills in by itself

        // Into an empty field: a simple paste, saved at once; the pasted one disappears from the suggestions.
        QSignalSpy changed(&vm, &PreTranscriptViewModel::contextNoteChanged);
        note->applySuggestion(pick(note, 0), QStringLiteral("replace"));
        QCOMPARE(sb.app->store()->load(self.id).contextNote, QStringLiteral("A „Nordwig” helyesen: Nordvik."));
        QVERIFY(changed.count() >= 1);
        QCOMPARE(note->suggestions().size(), 1);
        QCOMPARE(note->suggestions().at(0).toMap().value("meetingId").toString(), older.id);

        // Append: after the existing one, on a new line; a draft typed meanwhile is part of the existing one too.
        note->draft(QStringLiteral("A „Nordwig” helyesen: Nordvik. Téma: bevezetés."));
        note->applySuggestion(pick(note, 0), QStringLiteral("append"));
        QCOMPARE(sb.app->store()->load(self.id).contextNote,
                 QStringLiteral("A „Nordwig” helyesen: Nordvik. Téma: bevezetés.\nRésztvevők: Kovács Anna."));
        QVERIFY(note->suggestions().size() == 2);             // the current one is no longer identical to either

        // Replace: in place of the old text.
        QCOMPARE(pick(note, 1), QStringLiteral("Résztvevők: Kovács Anna."));
        note->applySuggestion(pick(note, 1), QStringLiteral("replace"));
        QCOMPARE(sb.app->store()->load(self.id).contextNote, QStringLiteral("Résztvevők: Kovács Anna."));
        note->applySuggestion(QStringLiteral("   "), QStringLiteral("replace"));   // empty: nothing
        QCOMPARE(sb.app->store()->load(self.id).contextNote, QStringLiteral("Résztvevők: Kovács Anna."));
    }

    void detectedCallShownNotSent()
    {
        jobtest::Sandbox sb;
        Meeting m = sb.recording(QStringLiteral("Teams-hívás · okt. 3. 14:02"), 1);
        sb.app->setMeetingDetectedCall(m.id, QStringLiteral("Microsoft Teams"));
        // An old build's automatic sentence in the field: it counts as an empty note.
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

        SummaryViewModel vm(summaryFacade(sb.app.get()));
        vm.setMeetingId(m.id);
        QCOMPARE(vm.view(), QStringLiteral("empty"));
        QVERIFY(!vm.noteChangedSinceSummary());

        // The quick summary gets the note as context, and summary.json remembers it.
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
        const library::SummaryDocument doc = sb.app->summaryDocument(m.id);
        QVERIFY(doc.meta.contextNoteKnown);
        QCOMPARE(doc.meta.contextNote, QStringLiteral("A „Nordwig” helyesen: Nordvik."));
        QCOMPARE(vm.view(), QStringLiteral("summary"));
        QVERIFY(!vm.noteChangedSinceSummary());

        // After a change a gentle indicator, but NOT stale (neither here nor in the library).
        QSignalSpy hint(&vm, &SummaryViewModel::noteHintChanged);
        vm.note()->setNote(QStringLiteral("A „Nordwig” helyesen: Nordvik. A „kvarc modul” helyesen: Qvarko-modul."));
        QVERIFY(hint.count() >= 1);
        QVERIFY(vm.noteChangedSinceSummary());
        QVERIFY(!vm.stale());
        QVERIFY(!sb.app->summaryStale(sb.app->store()->load(m.id)).stale);
        // Whitespace-only difference: not a change.
        vm.note()->setNote(QStringLiteral("  A „Nordwig”  helyesen: Nordvik. "));
        QVERIFY(!vm.noteChangedSinceSummary());

        // Old summary (summary.json does not know what it was made with): no indicator.
        library::SummaryDocument old = doc;
        old.meta.contextNoteKnown = false;
        QVERIFY(library::SummaryRepository::save(sb.app->store()->load(m.id).folder, old));
        vm.refresh();
        vm.note()->setNote(QStringLiteral("Egészen más."));
        QVERIFY(!vm.noteChangedSinceSummary());
    }
};

QTEST_MAIN(TestMeetingNoteModel)
#include "test_meeting_note_model.moc"

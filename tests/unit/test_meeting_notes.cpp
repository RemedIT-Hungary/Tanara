// The meeting note's rules (tanara/library/MeetingNotes.h): title similarity (accents, date,
// number), template suggestions (order, exclusions), interpreting the watcher's old automatic
// sentence (on load, in summary.json), the context sent to the transcriber and the library's
// cached suggestion query. All names and words are made up.
#include <QtTest>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/MeetingNotes.h"
#include "tanara/library/store/JsonSerialization.h"
#include "tanara/library/store/MeetingStore.h"
#include "tanara/providers/stt/ISttProvider.h"
#include "tanara/library/summary/SummaryStore.h"

using namespace tanara;
using namespace tanara::library::meetingnotes;

namespace {

Meeting meeting(const QString& id, const QString& title, const QDateTime& when, const QString& note)
{
    Meeting m;
    m.id = id;
    m.title = title;
    m.startedAt = when;
    m.contextNote = note;
    return m;
}

QDateTime day(int d) { return QDateTime(QDate(2026, 9, d), QTime(10, 0)); }

} // namespace

class MeetingNotesTest : public QObject {
    Q_OBJECT
private slots:
    void titleWordsFoldAndIgnoreDatesNumbers()
    {
        QCOMPARE(library::meetingnotes::titleWords(QStringLiteral("Nordvik HETI meeting · 2026. szept. 29. 14:02")),
                 (QStringList{"nordvik", "heti", "meeting"}));
        // Accent- and case-insensitive; a word containing a number (ordinal, version) is left out.
        QCOMPARE(library::meetingnotes::titleWords(QStringLiteral("Örvényes ÜGYFÉL-egyeztetés #12 v2")),
                 (QStringList{"orvenyes", "ugyfel", "egyeztetes"}));
        QCOMPARE(library::meetingnotes::titleWords(QStringLiteral("Weekly sync – Monday, Oct 6")),
                 (QStringList{"weekly", "sync"}));
        QVERIFY(library::meetingnotes::titleWords(QStringLiteral("2026-10-03 14:00")).isEmpty());
    }

    void titleSimilarityRules()
    {
        // A recurring meeting: the date / ordinal does not matter.
        QVERIFY(library::meetingnotes::titlesSimilar(QStringLiteral("Nordvik heti meeting"),
                              QStringLiteral("Nordvik heti meeting 2026. okt. 6.")));
        QVERIFY(library::meetingnotes::titlesSimilar(QStringLiteral("Nordvik heti meeting #14"),
                              QStringLiteral("nordvik HETI Meeting #13")));
        // A title written without accents is related too.
        QVERIFY(library::meetingnotes::titlesSimilar(QStringLiteral("Qvarkó ütemezés"), QStringLiteral("Qvarko utemezes")));
        // A title with one more word is still related (3/4 shared), one shared word between two different topics is not.
        QVERIFY(library::meetingnotes::titlesSimilar(QStringLiteral("Nordvik heti meeting"),
                              QStringLiteral("Nordvik heti meeting demó")));
        QVERIFY(!library::meetingnotes::titlesSimilar(QStringLiteral("Nordvik bevezetés"), QStringLiteral("Nordvik árazás")));
        // Only weak words in common: another project's weekly meeting is not related.
        QVERIFY(!library::meetingnotes::titlesSimilar(QStringLiteral("Nordvik heti meeting"),
                               QStringLiteral("Lantos heti meeting")));
        // Automatic call names ("Teams call ...") do not make anything related.
        QVERIFY(!library::meetingnotes::titlesSimilar(QStringLiteral("Teams-hívás · okt. 3. 14:02"),
                               QStringLiteral("Teams-hívás · szept. 30. 9:15")));
        QCOMPARE(library::meetingnotes::titleSimilarity({}, {QStringLiteral("nordvik")}), 0.0);
    }

    void suggestionsOrderAndExclusions()
    {
        const QVector<Meeting> all = {
            meeting("self", "Nordvik heti meeting", day(29), "Mostani megjegyzés"),
            meeting("a", "Nordvik heti meeting", day(22), "A „Nordwig” helyesen: Nordvik."),
            meeting("b", "Nordvik heti meeting", day(15), "  "),                       // empty
            meeting("c", "Nordvik heti meeting", day(8), "Mostani   megjegyzés"),      // = the current one
            meeting("d", "Nordvik heti meeting – 2026. szept. 1.", day(1), "Régebbi: PixelTár."),
            meeting("e", "Lantos árazás", day(28), "Más téma."),                     // unrelated title
            meeting("f", "Nordvik heti meeting", day(25), "A „Nordwig” helyesen: Nordvik."), // repeat
            meeting("g", "Nordvik heti meeting", day(27), "Automatikusan észlelt hívás: Zoom"),
            meeting("h", "Nordvik heti meeting", day(2), "Még régebbi."),
        };
        const QVector<library::meetingnotes::NoteSuggestion> s =
            library::meetingnotes::suggestNotes(all, "self", "Nordvik heti meeting", "Mostani megjegyzés", 3);
        QCOMPARE(s.size(), 3);
        // Newest first; identical text only once (with the newest source); the own one, empty, the
        // same as the current one, the automatic one and the unrelated one are left out.
        QCOMPARE(s.at(0).meetingId, QStringLiteral("f"));
        QCOMPARE(s.at(1).meetingId, QStringLiteral("h"));
        QCOMPARE(s.at(2).meetingId, QStringLiteral("d"));
        QCOMPARE(s.at(0).note, QStringLiteral("A „Nordwig” helyesen: Nordvik."));

        // No comparable word in the title: no suggestion.
        QVERIFY(library::meetingnotes::suggestNotes(all, "self", "Teams-hívás · okt. 3.", QString()).isEmpty());
        QVERIFY(library::meetingnotes::suggestNotes(all, "self", "Nordvik heti meeting", QString(), 0).isEmpty());
    }

    void autoCallNoteRecognised()
    {
        QString app;
        QVERIFY(library::meetingnotes::parseAutoCallNote(QStringLiteral("Automatikusan észlelt hívás: Microsoft Teams"), &app));
        QCOMPARE(app, QStringLiteral("Microsoft Teams"));
        QVERIFY(library::meetingnotes::parseAutoCallNote(QStringLiteral("  Automatically detected call: Zoom  "), &app));
        QCOMPARE(app, QStringLiteral("Zoom"));
        // The user's own text is not that.
        QVERIFY(!library::meetingnotes::parseAutoCallNote(QStringLiteral("Automatikusan észlelt hívás: Teams\nA „Nordwig” helyesen: Nordvik.")));
        QVERIFY(!library::meetingnotes::parseAutoCallNote(QStringLiteral("Teams-hívás a Nordvikkal")));
        QVERIFY(!library::meetingnotes::parseAutoCallNote(QStringLiteral("Automatikusan észlelt hívás:")));
    }

    void legacyNoteInterpretedOnLoadAndRewritten()
    {
        QTemporaryDir dir;
        library::MeetingStore store(dir.filePath("rec"), dir.filePath("meta"));
        Meeting m = store.createMeeting(QStringLiteral("Teams-hívás · okt. 3. 14:02"));
        m.contextNote = QStringLiteral("Automatikusan észlelt hívás: Microsoft Teams");
        store.saveMeeting(m);
        // We write the old form straight into meeting.json (as if an old build had written it).
        const QString path = QDir(m.folder).filePath(QStringLiteral("meeting.json"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
            o.insert(QStringLiteral("contextNote"), QStringLiteral("Automatikusan észlelt hívás: Microsoft Teams"));
            o.remove(QStringLiteral("detectedCallApp"));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QJsonDocument(o).toJson());
        }
        Meeting back = store.load(m.id);
        QVERIFY(back.contextNote.isEmpty());
        QCOMPARE(back.detectedCallApp, QStringLiteral("Microsoft Teams"));

        // The next save already writes the clean form.
        store.saveMeeting(back);
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(!o.contains(QStringLiteral("contextNote")));
        QCOMPARE(o.value(QStringLiteral("detectedCallApp")).toString(), QStringLiteral("Microsoft Teams"));

        // The user's own note is untouched; the English form is recognized too.
        Meeting u;
        u.contextNote = QStringLiteral("Automatically detected call: Zoom");
        u.detectedCallApp = QStringLiteral("Slack");
        QVERIFY(library::meetingnotes::interpretLegacyNote(u));
        QVERIFY(u.contextNote.isEmpty());
        QCOMPARE(u.detectedCallApp, QStringLiteral("Slack"));   // the existing field wins
        Meeting own;
        own.contextNote = QStringLiteral("A „pixel tár” helyesen: PixelTár.");
        QVERIFY(!library::meetingnotes::interpretLegacyNote(own));
        QCOMPARE(library::meetingFromJson(library::toJson(own)).contextNote, own.contextNote);
    }

    void sttContextCarriesNoteNotDetectedCall()
    {
        Meeting m;
        m.title = QStringLiteral("Nordvik heti meeting");
        m.contextNote = QStringLiteral("  A „Nordwig” helyesen: Nordvik.  ");
        m.detectedCallApp = QStringLiteral("Microsoft Teams");
        Track mic; mic.speakerLabel = QStringLiteral("Kovács Anna"); mic.active = true;
        Track off; off.speakerLabel = QStringLiteral("Eldobott"); off.active = false;
        m.tracks = {mic, off};

        providers::SttRequest req;
        library::meetingnotes::fillSttContext(req, m);
        QCOMPARE(req.context, QStringLiteral("A „Nordwig” helyesen: Nordvik."));
        QCOMPARE(req.contextGeneral.value(QStringLiteral("Megbeszélés")), QStringLiteral("Nordvik heti meeting"));
        QCOMPARE(req.contextTerms, QStringList{QStringLiteral("Kovács Anna")});
        for (const QString& v : req.contextGeneral)
            QVERIFY(!v.contains(QStringLiteral("Teams")));
        QVERIFY(!req.contextTerms.contains(QStringLiteral("Microsoft Teams")));

        // The automatic sentence (if it somehow stayed in the field) never goes to the transcriber.
        m.contextNote = QStringLiteral("Automatikusan észlelt hívás: Microsoft Teams");
        library::meetingnotes::fillSttContext(req, m);
        QVERIFY(req.context.isEmpty());
        // Not even when loaded from an old meeting.json.
        Meeting legacy = library::meetingFromJson(library::toJson(m));
        QVERIFY(library::meetingnotes::sttContextText(legacy).isEmpty());
    }

    void summaryMetaRemembersNote()
    {
        library::SummaryDocument doc;
        doc.exists = true;
        doc.meta.contextNote = QStringLiteral("A „Nordwig” helyesen: Nordvik.");
        doc.meta.contextNoteKnown = true;
        library::SummaryDocument back = library::summarystore::fromJson(library::summarystore::toJson(doc));
        QVERIFY(back.meta.contextNoteKnown);
        QCOMPARE(back.meta.contextNote, doc.meta.contextNote);
        // Old summary.json: no key, so unknown (no "changed since" indicator).
        QJsonObject o = library::summarystore::toJson(doc);
        o.remove(QStringLiteral("contextNote"));
        QVERIFY(!library::summarystore::fromJson(o).meta.contextNoteKnown);
        // A summary made with an empty note: known, empty.
        doc.meta.contextNote.clear();
        back = library::summarystore::fromJson(library::summarystore::toJson(doc));
        QVERIFY(back.meta.contextNoteKnown);
        QVERIFY(back.meta.contextNote.isEmpty());
    }

    void promptsApplyContextCorrections()
    {
        for (const QString& id : {QStringLiteral("single"), QStringLiteral("notes"), QStringLiteral("merge"),
                                  QStringLiteral("topic"), QStringLiteral("analysis"), QStringLiteral("reduce")}) {
            const QString p = processing::promptBuiltin(id);
            QVERIFY2(p.contains(QStringLiteral("use the corrected form")), qPrintable(id));
            QVERIFY2(!p.contains(QLatin1Char('%')), qPrintable(id));   // no placeholder remained
        }
    }

    void librarySuggestionsCachedAndCheap()
    {
        QTemporaryDir dir;
        library::MeetingStore store(dir.filePath("rec"), dir.filePath("meta"));
        auto add = [&](const QString& title, int dayOfMonth, const QString& note) {
            Meeting m = store.createMeeting(title);
            m.startedAt = day(dayOfMonth);
            m.contextNote = note;
            store.saveMeeting(m);
            return m;
        };
        const Meeting older = add(QStringLiteral("Nordvik heti meeting"), 1, QStringLiteral("Régi: PixelTár."));
        const Meeting newer = add(QStringLiteral("Nordvik heti meeting"), 15, QStringLiteral("A „Nordwig” helyesen: Nordvik."));
        // A few hundred other meetings (the similarity computation stays fast).
        for (int i = 0; i < 300; ++i)
            add(QStringLiteral("Lantos projekt %1. egyeztetés témája %2")
                    .arg(i).arg(i % 7 ? QStringLiteral("árazás") : QStringLiteral("bevezetés")),
                1 + i % 28, i % 3 ? QStringLiteral("Megjegyzés %1").arg(i) : QString());
        const Meeting self = add(QStringLiteral("Nordvik heti meeting 2026. szept. 29."), 29, QString());

        library::MeetingLibrary lib(&store);
        QVector<library::meetingnotes::NoteSuggestion> s = lib.noteSuggestions(self.id, QString());
        QCOMPARE(s.size(), 2);
        QCOMPARE(s.at(0).meetingId, newer.id);
        QCOMPARE(s.at(1).meetingId, older.id);

        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 50; ++i)
            s = lib.noteSuggestions(self.id, QString());
        QVERIFY2(t.elapsed() < 2000, qPrintable(QString::number(t.elapsed())));   // typically < 1 ms per call

        // After the note changes the library refreshes (on the store's signal).
        Meeting n = store.load(newer.id);
        n.contextNote.clear();
        store.saveMeeting(n);
        s = lib.noteSuggestions(self.id, QString());
        QCOMPARE(s.size(), 1);
        QCOMPARE(s.at(0).meetingId, older.id);
        // What is identical to the current one is not a suggestion.
        QVERIFY(lib.noteSuggestions(self.id, QStringLiteral("Régi:  PixelTár.")).isEmpty());
        QVERIFY(lib.noteSuggestions(QStringLiteral("nincs-ilyen"), QString()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(MeetingNotesTest)
#include "test_meeting_notes.moc"

//
// A megbeszélés-megjegyzés szabályai (tanara/library/MeetingNotes.h): cím-hasonlóság
// (ékezet, dátum, szám), sablon-javaslatok (sorrend, kizárások), a figyelő régi automatikus
// mondatának értelmezése (betöltéskor, summary.json-ban), az átírónak küldött kontextus és a
// könyvtár gyorsítótáras javaslat-lekérdezése. Minden név és szó kitalált.
//
#include <QtTest>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "tanara/PromptLibrary.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/MeetingNotes.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/stt/ISttProvider.h"
#include "tanara/summary/SummaryStore.h"

using namespace tanara;
using namespace tanara::meetingnotes;

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
        QCOMPARE(titleWords(QStringLiteral("Nordvik HETI meeting · 2026. szept. 29. 14:02")),
                 (QStringList{"nordvik", "heti", "meeting"}));
        // Ékezet- és kisbetű-független; számot tartalmazó szó (sorszám, verzió) kimarad.
        QCOMPARE(titleWords(QStringLiteral("Örvényes ÜGYFÉL-egyeztetés #12 v2")),
                 (QStringList{"orvenyes", "ugyfel", "egyeztetes"}));
        QCOMPARE(titleWords(QStringLiteral("Weekly sync – Monday, Oct 6")),
                 (QStringList{"weekly", "sync"}));
        QVERIFY(titleWords(QStringLiteral("2026-10-03 14:00")).isEmpty());
    }

    void titleSimilarityRules()
    {
        // Ismétlődő megbeszélés: a dátum / sorszám nem számít.
        QVERIFY(titlesSimilar(QStringLiteral("Nordvik heti meeting"),
                              QStringLiteral("Nordvik heti meeting 2026. okt. 6.")));
        QVERIFY(titlesSimilar(QStringLiteral("Nordvik heti meeting #14"),
                              QStringLiteral("nordvik HETI Meeting #13")));
        // Ékezet nélkül írt cím is rokon.
        QVERIFY(titlesSimilar(QStringLiteral("Qvarkó ütemezés"), QStringLiteral("Qvarko utemezes")));
        // Egy szóval bővebb cím még rokon (3/4 közös), egy közös szó két különböző témánál nem.
        QVERIFY(titlesSimilar(QStringLiteral("Nordvik heti meeting"),
                              QStringLiteral("Nordvik heti meeting demó")));
        QVERIFY(!titlesSimilar(QStringLiteral("Nordvik bevezetés"), QStringLiteral("Nordvik árazás")));
        // Csak gyenge szavak közösek: más projekt heti meetingje nem rokon.
        QVERIFY(!titlesSimilar(QStringLiteral("Nordvik heti meeting"),
                               QStringLiteral("Lantos heti meeting")));
        // Az automatikus hívás-nevek („Teams-hívás · …”) nem tesznek rokonná.
        QVERIFY(!titlesSimilar(QStringLiteral("Teams-hívás · okt. 3. 14:02"),
                               QStringLiteral("Teams-hívás · szept. 30. 9:15")));
        QCOMPARE(titleSimilarity({}, {QStringLiteral("nordvik")}), 0.0);
    }

    void suggestionsOrderAndExclusions()
    {
        const QVector<Meeting> all = {
            meeting("self", "Nordvik heti meeting", day(29), "Mostani megjegyzés"),
            meeting("a", "Nordvik heti meeting", day(22), "A „Nordwig” helyesen: Nordvik."),
            meeting("b", "Nordvik heti meeting", day(15), "  "),                       // üres
            meeting("c", "Nordvik heti meeting", day(8), "Mostani   megjegyzés"),      // = a mostani
            meeting("d", "Nordvik heti meeting – 2026. szept. 1.", day(1), "Régebbi: PixelTár."),
            meeting("e", "Lantos árazás", day(28), "Más téma."),                     // nem rokon cím
            meeting("f", "Nordvik heti meeting", day(25), "A „Nordwig” helyesen: Nordvik."), // ismétlés
            meeting("g", "Nordvik heti meeting", day(27), "Automatikusan észlelt hívás: Zoom"),
            meeting("h", "Nordvik heti meeting", day(2), "Még régebbi."),
        };
        const QVector<NoteSuggestion> s =
            suggestNotes(all, "self", "Nordvik heti meeting", "Mostani megjegyzés", 3);
        QCOMPARE(s.size(), 3);
        // Legújabb elöl; az azonos szöveg csak egyszer (a legújabb forrással); a saját, üres,
        // a mostanival azonos, az automatikus és a nem rokon kimarad.
        QCOMPARE(s.at(0).meetingId, QStringLiteral("f"));
        QCOMPARE(s.at(1).meetingId, QStringLiteral("h"));
        QCOMPARE(s.at(2).meetingId, QStringLiteral("d"));
        QCOMPARE(s.at(0).note, QStringLiteral("A „Nordwig” helyesen: Nordvik."));

        // Nincs összehasonlítható szó a címben → nincs javaslat.
        QVERIFY(suggestNotes(all, "self", "Teams-hívás · okt. 3.", QString()).isEmpty());
        QVERIFY(suggestNotes(all, "self", "Nordvik heti meeting", QString(), 0).isEmpty());
    }

    void autoCallNoteRecognised()
    {
        QString app;
        QVERIFY(parseAutoCallNote(QStringLiteral("Automatikusan észlelt hívás: Microsoft Teams"), &app));
        QCOMPARE(app, QStringLiteral("Microsoft Teams"));
        QVERIFY(parseAutoCallNote(QStringLiteral("  Automatically detected call: Zoom  "), &app));
        QCOMPARE(app, QStringLiteral("Zoom"));
        // A felhasználó saját szövege nem az.
        QVERIFY(!parseAutoCallNote(QStringLiteral("Automatikusan észlelt hívás: Teams\nA „Nordwig” helyesen: Nordvik.")));
        QVERIFY(!parseAutoCallNote(QStringLiteral("Teams-hívás a Nordvikkal")));
        QVERIFY(!parseAutoCallNote(QStringLiteral("Automatikusan észlelt hívás:")));
    }

    void legacyNoteInterpretedOnLoadAndRewritten()
    {
        QTemporaryDir dir;
        MeetingStore store(dir.filePath("rec"), dir.filePath("meta"));
        Meeting m = store.createMeeting(QStringLiteral("Teams-hívás · okt. 3. 14:02"));
        m.contextNote = QStringLiteral("Automatikusan észlelt hívás: Microsoft Teams");
        store.saveMeeting(m);
        // A régi formát közvetlenül a meeting.json-ba írjuk (mintha régi build írta volna).
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

        // A következő mentés már a tiszta formát írja.
        store.saveMeeting(back);
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(!o.contains(QStringLiteral("contextNote")));
        QCOMPARE(o.value(QStringLiteral("detectedCallApp")).toString(), QStringLiteral("Microsoft Teams"));

        // A felhasználó saját megjegyzése érintetlen; az angol forma is felismert.
        Meeting u;
        u.contextNote = QStringLiteral("Automatically detected call: Zoom");
        u.detectedCallApp = QStringLiteral("Slack");
        QVERIFY(interpretLegacyNote(u));
        QVERIFY(u.contextNote.isEmpty());
        QCOMPARE(u.detectedCallApp, QStringLiteral("Slack"));   // a meglévő mező nyer
        Meeting own;
        own.contextNote = QStringLiteral("A „pixel tár” helyesen: PixelTár.");
        QVERIFY(!interpretLegacyNote(own));
        QCOMPARE(meetingFromJson(toJson(own)).contextNote, own.contextNote);
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

        SttRequest req;
        fillSttContext(req, m);
        QCOMPARE(req.context, QStringLiteral("A „Nordwig” helyesen: Nordvik."));
        QCOMPARE(req.contextGeneral.value(QStringLiteral("Megbeszélés")), QStringLiteral("Nordvik heti meeting"));
        QCOMPARE(req.contextTerms, QStringList{QStringLiteral("Kovács Anna")});
        for (const QString& v : req.contextGeneral)
            QVERIFY(!v.contains(QStringLiteral("Teams")));
        QVERIFY(!req.contextTerms.contains(QStringLiteral("Microsoft Teams")));

        // Az automatikus mondat (ha valahogy mégis a mezőben maradna) sosem megy az átírónak.
        m.contextNote = QStringLiteral("Automatikusan észlelt hívás: Microsoft Teams");
        fillSttContext(req, m);
        QVERIFY(req.context.isEmpty());
        // Régi meeting.json-ból töltve sem.
        Meeting legacy = meetingFromJson(toJson(m));
        QVERIFY(sttContextText(legacy).isEmpty());
    }

    void summaryMetaRemembersNote()
    {
        SummaryDocument doc;
        doc.exists = true;
        doc.meta.contextNote = QStringLiteral("A „Nordwig” helyesen: Nordvik.");
        doc.meta.contextNoteKnown = true;
        SummaryDocument back = summarystore::fromJson(summarystore::toJson(doc));
        QVERIFY(back.meta.contextNoteKnown);
        QCOMPARE(back.meta.contextNote, doc.meta.contextNote);
        // Régi summary.json: nincs kulcs → ismeretlen (nincs „azóta változott” jelzés).
        QJsonObject o = summarystore::toJson(doc);
        o.remove(QStringLiteral("contextNote"));
        QVERIFY(!summarystore::fromJson(o).meta.contextNoteKnown);
        // Üres megjegyzéssel készült összefoglaló: ismert, üres.
        doc.meta.contextNote.clear();
        back = summarystore::fromJson(summarystore::toJson(doc));
        QVERIFY(back.meta.contextNoteKnown);
        QVERIFY(back.meta.contextNote.isEmpty());
    }

    void promptsApplyContextCorrections()
    {
        for (const QString& id : {QStringLiteral("single"), QStringLiteral("notes"), QStringLiteral("merge"),
                                  QStringLiteral("topic"), QStringLiteral("analysis"), QStringLiteral("reduce")}) {
            const QString p = promptBuiltin(id);
            QVERIFY2(p.contains(QStringLiteral("use the corrected form")), qPrintable(id));
            QVERIFY2(!p.contains(QLatin1Char('%')), qPrintable(id));   // nem maradt helykitöltő
        }
    }

    void librarySuggestionsCachedAndCheap()
    {
        QTemporaryDir dir;
        MeetingStore store(dir.filePath("rec"), dir.filePath("meta"));
        auto add = [&](const QString& title, int dayOfMonth, const QString& note) {
            Meeting m = store.createMeeting(title);
            m.startedAt = day(dayOfMonth);
            m.contextNote = note;
            store.saveMeeting(m);
            return m;
        };
        const Meeting older = add(QStringLiteral("Nordvik heti meeting"), 1, QStringLiteral("Régi: PixelTár."));
        const Meeting newer = add(QStringLiteral("Nordvik heti meeting"), 15, QStringLiteral("A „Nordwig” helyesen: Nordvik."));
        // Pár száz más meeting (a hasonlóság-számítás így is gyors marad).
        for (int i = 0; i < 300; ++i)
            add(QStringLiteral("Lantos projekt %1. egyeztetés témája %2")
                    .arg(i).arg(i % 7 ? QStringLiteral("árazás") : QStringLiteral("bevezetés")),
                1 + i % 28, i % 3 ? QStringLiteral("Megjegyzés %1").arg(i) : QString());
        const Meeting self = add(QStringLiteral("Nordvik heti meeting 2026. szept. 29."), 29, QString());

        MeetingLibrary lib(&store);
        QVector<NoteSuggestion> s = lib.noteSuggestions(self.id, QString());
        QCOMPARE(s.size(), 2);
        QCOMPARE(s.at(0).meetingId, newer.id);
        QCOMPARE(s.at(1).meetingId, older.id);

        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 50; ++i)
            s = lib.noteSuggestions(self.id, QString());
        QVERIFY2(t.elapsed() < 2000, qPrintable(QString::number(t.elapsed())));   // jellemzően < 1 ms / hívás

        // A megjegyzés változása után a könyvtár frissül (a store jelére).
        Meeting n = store.load(newer.id);
        n.contextNote.clear();
        store.saveMeeting(n);
        s = lib.noteSuggestions(self.id, QString());
        QCOMPARE(s.size(), 1);
        QCOMPARE(s.at(0).meetingId, older.id);
        // Ami a mostanival azonos, nem javaslat.
        QVERIFY(lib.noteSuggestions(self.id, QStringLiteral("Régi:  PixelTár.")).isEmpty());
        QVERIFY(lib.noteSuggestions(QStringLiteral("nincs-ilyen"), QString()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(MeetingNotesTest)
#include "test_meeting_notes.moc"

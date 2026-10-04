//
// Gyors összefoglaló — a darabolás, a jegyzet- és JSON-értelmezés, a memó-összerakás és a
// SummaryService vezénylése (forgatókönyvezett ál-LLM-mel: egy rész, sok rész, bukó rész,
// megszakítás, hibás JSON-változatok, a részjegyzet-gyorsítótárból folytatás).
//
#include <QtTest>
#include <QTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <functional>

#include "tanara/Types.h"
#include "tanara/PromptLibrary.h"
#include "tanara/SummaryService.h"
#include "tanara/summary/SummaryPipeline.h"
#include "tanara/llm/ILlmProvider.h"

using namespace tanara;
using namespace tanara::summarypipe;

// ---- forgatókönyvezett ál-LLM ------------------------------------------------------------
// Minden chat()-et naplóz, és a `script` függvénytől kér választ (a kérés sorszáma + maga a
// kérés alapján). A válasz lehet szöveg, hiba vagy „tartás” (sosem válaszol — megszakításhoz).
struct Answer {
    enum Kind { Text, Fail, Hold } kind = Text;
    QString text;
};

class ScriptedJob : public LlmJob {
    Q_OBJECT
public:
    explicit ScriptedJob(QObject* parent) : LlmJob(parent) {}
    void cancel() override {
        cancelled = true;
        // Mint a valódi provider: a megszakított kérés failed-del zárul.
        QTimer::singleShot(0, this, [this]() { emit failed(QStringLiteral("cancelled")); });
    }
    bool cancelled = false;
};

class ScriptedProvider : public QObject, public ILlmProvider {
    Q_OBJECT
public:
    std::function<Answer(int, const LlmRequest&)> script;
    QVector<LlmRequest> requests;
    QVector<QPointer<ScriptedJob>> jobs;

    QString name() const override { return QStringLiteral("scripted"); }
    bool supportsStreaming() const override { return false; }
    LlmJob* chat(const LlmRequest& req) override {
        const int n = int(requests.size());
        requests.append(req);
        auto* job = new ScriptedJob(this);
        jobs.append(job);
        const Answer a = script ? script(n, req) : Answer{};
        if (a.kind == Answer::Hold) return job;
        QTimer::singleShot(0, job, [job, a]() {
            if (a.kind == Answer::Fail) emit job->failed(a.text);
            else emit job->finished(a.text);
        });
        return job;
    }
    QString system(int i) const { return requests.value(i).messages.value(0).content; }
    QString user(int i) const { return requests.value(i).messages.value(1).content; }
};

namespace {

// Átirat: beszéd-blokkok a megadott időpontokban (perc), két beszélő váltakozva. A tokenek
// közti szünet > 1,5 s, így minden elem külön bekezdés.
MergedTranscript transcriptAt(const QVector<double>& minutes, int wordsEach = 40)
{
    MergedTranscript t;
    t.language = QStringLiteral("hu");
    QString text;
    for (int w = 0; w < wordsEach; ++w) text += QStringLiteral(" szó%1").arg(w);
    for (int i = 0; i < minutes.size(); ++i) {
        TranscriptToken tok;
        tok.text = text;
        tok.speaker = i % 2 ? QStringLiteral("Béla") : QStringLiteral("Ádám");
        tok.startMs = qint64(minutes[i] * 60000.0);
        tok.endMs = tok.startMs + 20000;
        t.tokens.append(tok);
    }
    return t;
}

// Bekezdés félpercenként, `total` percig.
MergedTranscript transcriptMinutes(double total)
{
    QVector<double> m;
    for (double x = 0; x < total; x += 0.5) m << x;
    return transcriptAt(m);
}

QString notesFor(int k, const QString& title = QString())
{
    const QString t = title.isEmpty() ? QStringLiteral("Téma %1").arg(k) : title;
    return QStringLiteral(
        "TOPICS\n"
        "### [%1:00-%1:30] %2\n"
        "- Pont A a(z) %3. részből.\n"
        "- Pont B a(z) %3. részből.\n"
        "\n"
        "DECISIONS\n"
        "- [%1:10] Döntés a(z) %3. részben.\n"
        "\n"
        "OPEN\n"
        "- none\n"
        "\n"
        "ACTIONS\n"
        "- [%1:20] Ajánlat küldése — Béla — péntek\n").arg(k * 15).arg(t).arg(k + 1);
}

const char* kMergeJson =
    R"({"execSummary":"Összegzés.","decisions":["Döntés A"],"openQuestions":["Kérdés A"],)"
    R"("actionItems":[{"text":"Ajánlat küldése","owner":"Béla","due":"péntek"}]})";

bool isNotes(const LlmRequest& r) { return r.messages.value(0).content.contains(QStringLiteral("ONE PART")); }
bool isMerge(const LlmRequest& r) { return r.messages.value(0).content.contains(QStringLiteral("SELECT and merge")); }
bool isSingle(const LlmRequest& r) { return r.messages.value(0).content.contains(QStringLiteral("in two steps")); }

int partOf(const LlmRequest& r)
{
    static const QRegularExpression re(QStringLiteral("^PART (\\d+) of (\\d+)"), QRegularExpression::MultilineOption);
    return re.match(r.messages.value(1).content).captured(1).toInt();
}

SummaryRequest request(const MergedTranscript& t, const QString& cache = QString())
{
    SummaryRequest r;
    r.transcript = t;
    r.language = QStringLiteral("magyar");
    r.model = QStringLiteral("teszt");
    r.cachePath = cache;
    return r;
}

} // namespace

class SummaryTest : public QObject {
    Q_OBJECT
private slots:
    // darabolás
    void splitShortMeetingIsOnePart();
    void splitAtExactBoundary();
    void splitLongMeeting();
    void splitMergesTinyTail();
    void splitEmpty();
    // jegyzet
    void parseNotesStandard();
    void parseNotesTolerant();
    void parseNotesWithoutHeadings();
    void parseNotesFillsTimes();
    // memó
    void memoJoinsContinuingSubject();
    // JSON
    void mergeJsonVariants_data();
    void mergeJsonVariants();
    void mergeJsonUnusable();
    void mergeJsonCapsOpenQuestions();
    void singleSplitsNotesAndJson();
    // nyelv
    void languageNamesAndReminder();
    // vezénylés
    void onePartIsOneCall();
    void manyPartsNotesThenMerge();
    void failingPartFailsCleanlyAndResumes();
    void emptyNotesFail();
    void badMergeKeepsNotesAndRetriesMergeOnly();
    void badSingleJsonKeepsNotes();
    void cancelMidRun();
    // markdown
    void renderMarkdownHasHungarianHeaders();
    void emptySectionsSkipped();
};

// ---- darabolás --------------------------------------------------------------------------

void SummaryTest::splitShortMeetingIsOnePart()
{
    const QVector<TranscriptPart> parts = splitTranscript(transcriptMinutes(20).segments());
    QCOMPARE(parts.size(), 1);
    QCOMPARE(parts[0].startMs, 0);
    QVERIFY(parts[0].markdown.startsWith(QStringLiteral("`[00:00]` **Ádám**")));
    QVERIFY(parts[0].markdown.contains(QStringLiteral("`[19:30]`")));
}

void SummaryTest::splitAtExactBoundary()
{
    // A 15:00-kor induló bekezdés már a 2. rész eleje (a rész ≥ 15 perc után zárul).
    const QVector<TranscriptPart> parts = splitTranscript(transcriptMinutes(40).segments());
    QCOMPARE(parts.size(), 3);
    QCOMPARE(parts[1].startMs, 15 * 60000);
    QCOMPARE(parts[2].startMs, 30 * 60000);
    QVERIFY(!parts[0].markdown.contains(QStringLiteral("`[15:00]`")));
    QVERIFY(parts[1].markdown.startsWith(QStringLiteral("`[15:00]`")));
    QCOMPARE(parts[0].index, 0);
    QCOMPARE(parts[2].index, 2);
}

void SummaryTest::splitLongMeeting()
{
    // 100 perc: 6 teljes rész + egy 10 perces (≥ 7,5) záró rész.
    const QVector<TranscriptPart> parts = splitTranscript(transcriptMinutes(100).segments());
    QCOMPARE(parts.size(), 7);
    QCOMPARE(parts.last().startMs, 90 * 60000);
    // Minden bekezdés pontosan egy részbe került.
    int paragraphs = 0;
    for (const TranscriptPart& p : parts) paragraphs += int(p.markdown.count(QStringLiteral("`[")));
    QCOMPARE(paragraphs, 200);
}

void SummaryTest::splitMergesTinyTail()
{
    // 22 perc: a 7 perces záró rész (< 7,5) az elsőhöz olvad → egy rész.
    QCOMPARE(splitTranscript(transcriptMinutes(22).segments()).size(), 1);
    // 24 perc: a 9 perces záró rész megmarad.
    QCOMPARE(splitTranscript(transcriptMinutes(24).segments()).size(), 2);
    // Hosszú, de szinte üres záró rész (egyetlen rövid mondat 10 perc után) → összeolvad.
    MergedTranscript t = transcriptMinutes(15);
    TranscriptToken late;
    late.text = QStringLiteral(" Köszönöm, sziasztok.");
    late.speaker = QStringLiteral("Ádám");
    late.startMs = 27 * 60000;
    late.endMs = late.startMs + 2000;
    t.tokens.append(late);
    const QVector<TranscriptPart> parts = splitTranscript(t.segments());
    QCOMPARE(parts.size(), 1);
    QVERIFY(parts[0].markdown.contains(QStringLiteral("Köszönöm")));
}

void SummaryTest::splitEmpty()
{
    QVERIFY(splitTranscript({}).isEmpty());
}

// ---- jegyzet ----------------------------------------------------------------------------

void SummaryTest::parseNotesStandard()
{
    TranscriptPart part;
    part.startMs = 15 * 60000;
    part.endMs = 30 * 60000;
    const PartNotes n = parseNotes(notesFor(1), part);
    QCOMPARE(n.topics.size(), 1);
    QCOMPARE(n.topics[0].title, QStringLiteral("Téma 1"));
    QCOMPARE(n.topics[0].startMs, 15 * 60000);
    QCOMPARE(n.topics[0].endMs, 15 * 60000 + 30000);
    QCOMPARE(n.topics[0].points.size(), 2);
    QCOMPARE(n.decisions, QStringList({QStringLiteral("[15:10] Döntés a(z) 2. részben.")}));
    QVERIFY(n.open.isEmpty());                                  // „- none” kimarad
    QCOMPARE(n.actions.size(), 1);
}

void SummaryTest::parseNotesTolerant()
{
    TranscriptPart part;
    part.endMs = 15 * 60000;
    // Próza előtte, kódkerítés, félkövér / magyar / kettőspontos fejlécek, számozott cím,
    // cím a végén lévő időkerettel, tördelt pont, „Nincs.” jelölés.
    const QString raw = QStringLiteral(
        "Itt vannak a jegyzetek:\n"
        "```\n"
        "**TOPICS**\n"
        "## 1. Költségkeret (02:00 – 06:30)\n"
        "* A keret 12 millió forint,\n"
        "  ebből 3 millió tartalék.\n"
        "**Pilot helyszínek**\n"
        "- Szeged és Debrecen.\n"
        "DÖNTÉSEK:\n"
        "- Nincs.\n"
        "## Open\n"
        "- [05:00] Bővítés Győrre?\n"
        "Teendők:\n"
        "1. Árajánlat — ? — \n"
        "```\n");
    const PartNotes n = parseNotes(raw, part);
    QCOMPARE(n.topics.size(), 2);
    QCOMPARE(n.topics[0].title, QStringLiteral("Költségkeret"));
    QCOMPARE(n.topics[0].startMs, 2 * 60000);
    QCOMPARE(n.topics[0].endMs, 6 * 60000 + 30000);
    QCOMPARE(n.topics[0].points, QStringList({QStringLiteral("A keret 12 millió forint, ebből 3 millió tartalék.")}));
    QCOMPARE(n.topics[1].title, QStringLiteral("Pilot helyszínek"));
    QCOMPARE(n.topics[1].startMs, 6 * 60000 + 30000);        // az előző végétől
    QCOMPARE(n.topics[1].endMs, 15 * 60000);                  // a rész végéig
    QVERIFY(n.decisions.isEmpty());
    QCOMPARE(n.open.size(), 1);
    QCOMPARE(n.actions.size(), 1);
}

void SummaryTest::parseNotesWithoutHeadings()
{
    TranscriptPart part;
    part.endMs = 60000;
    const PartNotes n = parseNotes(QStringLiteral("- Első pont.\n- Második pont.\n"), part);
    QCOMPARE(n.topics.size(), 1);
    QCOMPARE(n.topics[0].points.size(), 2);
    QVERIFY(n.topics[0].title.isEmpty());
    QVERIFY(parseNotes(QStringLiteral("   \n"), part).isEmpty());
}

void SummaryTest::parseNotesFillsTimes()
{
    TranscriptPart part;
    part.startMs = 60000;
    part.endMs = 600000;
    // Kilógó idő a rész keretére szorul; a hiányzó a szomszédokból.
    const PartNotes n = parseNotes(QStringLiteral(
        "TOPICS\n### [00:10-03:00] A\n- a\n### B\n- b\n### [07:00-99:00] C\n- c\n"), part);
    QCOMPARE(n.topics.size(), 3);
    QCOMPARE(n.topics[0].startMs, 60000);
    QCOMPARE(n.topics[1].startMs, 180000);
    QCOMPARE(n.topics[1].endMs, 420000);
    QCOMPARE(n.topics[2].endMs, 600000);
}

// ---- memó -------------------------------------------------------------------------------

void SummaryTest::memoJoinsContinuingSubject()
{
    TranscriptPart p0; p0.index = 0; p0.endMs = 15 * 60000;
    TranscriptPart p1; p1.index = 1; p1.startMs = 15 * 60000; p1.endMs = 30 * 60000;
    const PartNotes a = parseNotes(QStringLiteral(
        "TOPICS\n### [00:00-05:00] Bevezetés\n- köszöntés\n"
        "### [05:00-15:00] Raktárrendszer migráció\n- a régi rendszer leáll\n- az adatok exportja\n"), p0);
    const PartNotes b = parseNotes(QStringLiteral(
        "TOPICS\n### [15:00-20:00] Raktárrendszer migráció\n- az adatok exportja\n- tesztelés jövő héten\n"
        "### [20:00-30:00] Költségek\n- 2 millió\n"), p1);
    const QVector<MemoSection> memo = assembleMemo({a, b});
    QCOMPARE(memo.size(), 3);                                   // nincs duplikált szakasz
    QCOMPARE(memo[1].title, QStringLiteral("Raktárrendszer migráció"));
    QCOMPARE(memo[1].startMs, 5 * 60000);
    QCOMPARE(memo[1].endMs, 20 * 60000);
    QCOMPARE(memo[1].points, QStringList({QStringLiteral("a régi rendszer leáll"),
                                          QStringLiteral("az adatok exportja"),
                                          QStringLiteral("tesztelés jövő héten")}));
    QCOMPARE(memo[2].title, QStringLiteral("Költségek"));

    // Hasonló (de nem azonos) cím a határon is egy szakasz; a cím nélküli jegyzet az előzőhöz
    // csatlakozik; a teljesen más tárgy külön marad.
    const PartNotes c = parseNotes(QStringLiteral("TOPICS\n### Költségek és keretek\n- 3 millió\n"), p1);
    const PartNotes d = parseNotes(QStringLiteral("- árva pont\n"), p1);
    const QVector<MemoSection> memo2 = assembleMemo({a, b, c, d});
    QCOMPARE(memo2.size(), 3);
    QCOMPARE(memo2[2].points, QStringList({QStringLiteral("2 millió"), QStringLiteral("3 millió"),
                                           QStringLiteral("árva pont")}));

    // Ha a modell egész részeket tesz ugyanazon cím alá, a szakasz nem nő 20 perc fölé:
    // azonos címmel, külön időkerettel folytatódik.
    TranscriptPart p2; p2.index = 2; p2.startMs = 30 * 60000; p2.endMs = 45 * 60000;
    const QString whole = QStringLiteral("TOPICS\n### Eszköz bevezetése\n- pont %1\n");
    const QVector<MemoSection> memo3 = assembleMemo({parseNotes(whole.arg(1), p0), parseNotes(whole.arg(2), p1),
                                                     parseNotes(whole.arg(3), p2)});
    QCOMPARE(memo3.size(), 3);
    QCOMPARE(memo3[0].endMs, 15 * 60000);
    QCOMPARE(memo3[1].title, QStringLiteral("Eszköz bevezetése"));
    QCOMPARE(memo3[1].startMs, 15 * 60000);
    QCOMPARE(memo3[2].startMs, 30 * 60000);
}

// ---- JSON -------------------------------------------------------------------------------

void SummaryTest::mergeJsonVariants_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("exec");
    QTest::addColumn<int>("decisions");
    QTest::addColumn<int>("actions");

    QTest::newRow("tiszta") << QString::fromUtf8(kMergeJson) << "Összegzés." << 1 << 1;
    QTest::newRow("kódkerítés") << QStringLiteral("```json\n%1\n```").arg(QString::fromUtf8(kMergeJson))
                                << "Összegzés." << 1 << 1;
    QTest::newRow("próza előtte-utána")
        << QStringLiteral("Íme az összefoglaló:\n%1\nRemélem, segít! {nem json}").arg(QString::fromUtf8(kMergeJson))
        << "Összegzés." << 1 << 1;
    QTest::newRow("escape-eletlen idézőjel")
        << QStringLiteral(R"({"execSummary": "A "Phoenix" projekt indul.", "decisions": ["A "B" opció marad"], "actionItems": []})")
        << "A \"Phoenix\" projekt indul." << 1 << 0;
    QTest::newRow("záró vessző") << QStringLiteral(R"({"execSummary":"X","decisions":["a","b",],"actionItems":[],})")
                                << "X" << 2 << 0;
    QTest::newRow("nyers sortörés") << QStringLiteral("{\"execSummary\":\"első sor\nmásodik sor\",\"decisions\":[]}")
                                    << "első sor\nmásodik sor" << 0 << 0;
    QTest::newRow("csonka vég") << QStringLiteral(R"({"execSummary":"X","decisions":["a"],"actionItems":[{"text":"Teszt írá)")
                                << "X" << 1 << 1;
    QTest::newRow("hiányzó vessző") << QStringLiteral("{\"execSummary\":\"X\",\"decisions\":[\"a\"\n\"b\"]}")
                                    << "X" << 2 << 0;
    QTest::newRow("hiányzó mezők") << QStringLiteral(R"({"execSummary":"Csak ez."})") << "Csak ez." << 0 << 0;
    QTest::newRow("álnevek és alakok")
        << QStringLiteral(R"({"summary":["Egy.","Kettő."],"decisions":"- a\n- b","action_items":["Tesztelni"],"open_questions":[{"text":"q"}]})")
        << "Egy. Kettő." << 2 << 1;
    QTest::newRow("okos idézőjelek") << QStringLiteral("{“execSummary”: “X”, “decisions”: []}") << "X" << 0 << 0;
    QTest::newRow("másolt idő és none")
        << QStringLiteral(R"({"execSummary":"X","decisions":["[12:30] Döntés","none"],"actionItems":[{"text":"[03:00] Hívás","owner":"?","due":"none"}]})")
        << "X" << 1 << 1;
}

void SummaryTest::mergeJsonVariants()
{
    QFETCH(QString, raw);
    QFETCH(QString, exec);
    QFETCH(int, decisions);
    QFETCH(int, actions);
    const MergeResult r = parseMergeJson(raw);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.execSummary, exec);
    QCOMPARE(r.decisions.size(), decisions);
    QCOMPARE(r.actionItems.size(), actions);
    if (QByteArray(QTest::currentDataTag()) == "másolt idő és none") {
        QCOMPARE(r.decisions.first(), QStringLiteral("Döntés"));
        QCOMPARE(r.actionItems.first().text, QStringLiteral("Hívás"));
        QVERIFY(r.actionItems.first().owner.isEmpty());
        QVERIFY(r.actionItems.first().due.isEmpty());
    }
}

void SummaryTest::mergeJsonUnusable()
{
    MergeResult r = parseMergeJson(QString());
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains(QStringLiteral("üres")));
    r = parseMergeJson(QStringLiteral("Sajnos nem tudok segíteni."));
    QVERIFY(!r.ok);
    QVERIFY2(r.error.contains(QStringLiteral("nincs JSON")), qPrintable(r.error));
    r = parseMergeJson(QStringLiteral(R"({"foo": 1})"));
    QVERIFY(!r.ok);
    QVERIFY2(r.error.contains(QStringLiteral("execSummary")), qPrintable(r.error));
}

void SummaryTest::mergeJsonCapsOpenQuestions()
{
    const MergeResult r = parseMergeJson(QStringLiteral(
        R"({"execSummary":"X","openQuestions":["1","2","3","4","5","6","7","2"]})"));
    QVERIFY(r.ok);
    QCOMPARE(r.openQuestions, QStringList({"1", "2", "3", "4", "5"}));
}

void SummaryTest::singleSplitsNotesAndJson()
{
    TranscriptPart part;
    part.endMs = 20 * 60000;
    const QString raw = QStringLiteral("TOPICS\n### [00:00-10:00] Árazás\n- Az ár 100 euró.\n\nSUMMARY\n")
                        + QString::fromUtf8(kMergeJson);
    SingleResult r = parseSingle(raw, part);
    QVERIFY(r.merge.ok);
    QCOMPARE(r.notes.topics.size(), 1);
    QCOMPARE(r.notes.topics[0].title, QStringLiteral("Árazás"));
    QCOMPARE(r.merge.execSummary, QStringLiteral("Összegzés."));

    // Régi, csak-JSON prompt (saját felülírás): nincs jegyzet, de az összefoglaló megvan.
    r = parseSingle(QString::fromUtf8(kMergeJson), part);
    QVERIFY(r.merge.ok);
    QVERIFY(r.notes.isEmpty());
    // Fejléc nélküli próza a JSON előtt nem lesz memó.
    r = parseSingle(QStringLiteral("Íme:\n") + QString::fromUtf8(kMergeJson), part);
    QVERIFY(r.merge.ok);
    QVERIFY(r.notes.isEmpty());
}

void SummaryTest::languageNamesAndReminder()
{
    QCOMPARE(summaryLanguageName(QStringLiteral("magyar")), QStringLiteral("Hungarian"));
    QCOMPARE(summaryLanguageName(QStringLiteral("  Német ")), QStringLiteral("German"));
    QCOMPARE(summaryLanguageName(QString()), QStringLiteral("Hungarian"));
    QCOMPARE(summaryLanguageName(QStringLiteral("eszperantó")), QStringLiteral("eszperantó"));
    QVERIFY(languageReminder(QStringLiteral("magyar")).contains(QStringLiteral("Hungarian (magyar)")));
    QVERIFY(languageReminder(QStringLiteral("English")).contains(QStringLiteral("in English,")));
    // A beépített promptok angol nyelvnevet kapnak, a régi {{NYELV}} a beállítás szövegét.
    const QString p = applySummaryLanguage(promptBuiltin(QStringLiteral("notes")), QStringLiteral("magyar"));
    QVERIFY(p.contains(QStringLiteral("Write all notes in Hungarian.")));
    QVERIFY(!p.contains(QStringLiteral("{{")));
    QCOMPARE(applySummaryLanguage(QStringLiteral("Írj {{NYELV}} nyelven."), QStringLiteral("német")),
             QStringLiteral("Írj német nyelven."));
}

// ---- vezénylés --------------------------------------------------------------------------

void SummaryTest::onePartIsOneCall()
{
    ScriptedProvider llm;
    llm.script = [](int, const LlmRequest&) {
        return Answer{Answer::Text, QStringLiteral("TOPICS\n### [00:00-05:00] Árazás\n- Az ár 100 euró.\n"
                                                   "### [05:00-12:00] Pilot\n- Két helyszín.\nSUMMARY\n")
                                    + QString::fromUtf8(kMergeJson)};
    };
    SummaryService svc(&llm);
    QSignalSpy ready(&svc, &SummaryService::summaryReady);
    QSignalSpy failed(&svc, &SummaryService::summaryFailed);
    QSignalSpy progress(&svc, &SummaryService::progress);
    SummaryRequest req = request(transcriptMinutes(12));
    req.contextNotes = QStringLiteral("Heti egyeztetés");
    QVERIFY(SummaryService::plan(req).singleCall);
    QCOMPARE(SummaryService::plan(req).llmCalls, 1);
    svc.summarize(req);
    QVERIFY(ready.wait(2000));
    QCOMPARE(failed.count(), 0);
    QCOMPARE(llm.requests.size(), 1);
    QVERIFY(isSingle(llm.requests[0]));
    QVERIFY(llm.system(0).contains(QStringLiteral("in Hungarian")));
    QVERIFY(llm.user(0).startsWith(QStringLiteral("Context / notes:\nHeti egyeztetés")));
    QVERIFY(llm.user(0).trimmed().endsWith(QStringLiteral("JSON keys exactly as specified.")));
    QCOMPARE(llm.requests[0].maxTokens, 8000);
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress[0][0].toString(), QStringLiteral("single"));

    const Summary s = qvariant_cast<Summary>(ready[0][0]);
    QCOMPARE(s.execSummary, QStringLiteral("Összegzés."));
    QCOMPARE(s.openQuestions, QStringList({QStringLiteral("Kérdés A")}));
    QCOMPARE(s.participants, QStringList({QStringLiteral("Ádám"), QStringLiteral("Béla")}));
    QCOMPARE(s.memo.size(), 2);
    QCOMPARE(s.memo[1].title, QStringLiteral("Pilot"));
}

void SummaryTest::manyPartsNotesThenMerge()
{
    QTemporaryDir dir;
    const QString cache = dir.filePath(QStringLiteral("summary.notes.json"));
    ScriptedProvider llm;
    llm.script = [](int, const LlmRequest& r) {
        if (isNotes(r)) {
            const int k = partOf(r) - 1;
            // A 2. rész az 1. rész tárgyával folytatódik (a prompt megkapja a címet).
            return Answer{Answer::Text, notesFor(k, k == 1 ? QStringLiteral("Téma 0") : QString())};
        }
        return Answer{Answer::Text, QString::fromUtf8(kMergeJson)};
    };
    SummaryService svc(&llm);
    QSignalSpy ready(&svc, &SummaryService::summaryReady);
    QSignalSpy progress(&svc, &SummaryService::progress);
    const SummaryRequest req = request(transcriptMinutes(45), cache);
    const SummaryPlan plan = SummaryService::plan(req);
    QCOMPARE(plan.parts, 3);
    QVERIFY(!plan.singleCall);
    QCOMPARE(plan.llmCalls, 4);
    svc.summarize(req);
    QVERIFY(ready.wait(3000));

    QCOMPARE(llm.requests.size(), 4);
    for (int i = 0; i < 3; ++i) {
        QVERIFY(isNotes(llm.requests[i]));
        QCOMPARE(partOf(llm.requests[i]), i + 1);
        QCOMPARE(llm.requests[i].maxTokens, 3000);
        QVERIFY(llm.user(i).contains(QStringLiteral("Reminder: write the output in Hungarian (magyar)")));
    }
    QVERIFY(!llm.user(0).contains(QStringLiteral("previous part ended")));
    QVERIFY(llm.user(1).contains(QStringLiteral("The previous part ended with the subject: \"Téma 0\"")));
    QVERIFY(isMerge(llm.requests[3]));
    QVERIFY(llm.user(3).contains(QStringLiteral("Speakers: Ádám, Béla")));
    QVERIFY(llm.user(3).contains(QStringLiteral("=== PART 3 of 3 [30:00-")));
    QVERIFY(llm.user(3).contains(QStringLiteral("- [30:10] Döntés a(z) 3. részben.")));

    // Haladás: notes 0/3, 1/3, 2/3, 3/3, merge.
    QStringList seen;
    for (const QList<QVariant>& p : progress)
        seen << QStringLiteral("%1 %2/%3").arg(p[0].toString()).arg(p[1].toInt()).arg(p[2].toInt());
    QCOMPARE(seen, QStringList({"notes 0/3", "notes 1/3", "notes 2/3", "notes 3/3", "merge 0/1"}));

    const Summary s = qvariant_cast<Summary>(ready[0][0]);
    QCOMPARE(s.decisions, QStringList({QStringLiteral("Döntés A")}));
    QCOMPARE(s.memo.size(), 2);                                  // Téma 0 (két részen át) + Téma 2
    QCOMPARE(s.memo[0].title, QStringLiteral("Téma 0"));
    QCOMPARE(s.memo[0].points.size(), 4);
    QCOMPARE(s.memo[1].title, QStringLiteral("Téma 2"));
    QVERIFY(!QFile::exists(cache));                              // siker után a gyorsítótár törlődik
}

void SummaryTest::failingPartFailsCleanlyAndResumes()
{
    QTemporaryDir dir;
    const QString cache = dir.filePath(QStringLiteral("summary.notes.json"));
    ScriptedProvider llm;
    bool breakPart2 = true;
    llm.script = [&breakPart2](int, const LlmRequest& r) {
        if (isNotes(r)) {
            const int k = partOf(r);
            if (k == 2 && breakPart2) return Answer{Answer::Fail, QStringLiteral("LLM hiba (HTTP 500): model crashed")};
            return Answer{Answer::Text, notesFor(k - 1)};
        }
        return Answer{Answer::Text, QString::fromUtf8(kMergeJson)};
    };
    const SummaryRequest req = request(transcriptMinutes(45), cache);
    {
        SummaryService svc(&llm);
        QSignalSpy ready(&svc, &SummaryService::summaryReady);
        QSignalSpy failed(&svc, &SummaryService::summaryFailed);
        svc.summarize(req);
        QVERIFY(failed.wait(2000));
        QTest::qWait(50);
        QCOMPARE(ready.count(), 0);                              // nincs félkész összefoglaló
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed[0][0].toString().contains(QStringLiteral("model crashed")));
        QCOMPARE(llm.requests.size(), 2);                         // a 3. rész már nem indult
    }
    QVERIFY(QFile::exists(cache));                               // az 1. rész jegyzete megmaradt
    const SummaryPlan plan = SummaryService::plan(req);
    QCOMPARE(plan.cachedParts, 1);
    QCOMPARE(plan.llmCalls, 3);

    // Újrapróbálás: az 1. rész nem fut újra.
    breakPart2 = false;
    llm.requests.clear();
    SummaryService svc(&llm);
    QSignalSpy ready(&svc, &SummaryService::summaryReady);
    svc.summarize(req);
    QVERIFY(ready.wait(2000));
    QCOMPARE(llm.requests.size(), 3);
    QCOMPARE(partOf(llm.requests[0]), 2);
    QCOMPARE(partOf(llm.requests[1]), 3);
    QVERIFY(isMerge(llm.requests[2]));
    QVERIFY(llm.user(2).contains(QStringLiteral("=== PART 1 of 3")));   // a gyorsítótárból
    QCOMPARE(qvariant_cast<Summary>(ready[0][0]).memo.size(), 3);
    QVERIFY(!QFile::exists(cache));

    // Más modell → más kulcs: a gyorsítótár nem érvényes.
    SummaryRequest other = req;
    other.model = QStringLiteral("masik");
    QCOMPARE(SummaryService::plan(other).cachedParts, 0);
}

void SummaryTest::emptyNotesFail()
{
    ScriptedProvider llm;
    llm.script = [](int, const LlmRequest& r) {
        return Answer{Answer::Text, isNotes(r) ? QStringLiteral("  \n") : QString::fromUtf8(kMergeJson)};
    };
    SummaryService svc(&llm);
    QSignalSpy failed(&svc, &SummaryService::summaryFailed);
    svc.summarize(request(transcriptMinutes(45)));
    QVERIFY(failed.wait(2000));
    QVERIFY2(failed[0][0].toString().contains(QStringLiteral("1/3. rész")), qPrintable(failed[0][0].toString()));
}

void SummaryTest::badMergeKeepsNotesAndRetriesMergeOnly()
{
    QTemporaryDir dir;
    const QString cache = dir.filePath(QStringLiteral("summary.notes.json"));
    ScriptedProvider llm;
    QString mergeAnswer = QStringLiteral("Bocsánat, ezt nem tudom JSON-ban megadni.");
    llm.script = [&mergeAnswer](int, const LlmRequest& r) {
        if (isNotes(r)) return Answer{Answer::Text, notesFor(partOf(r) - 1)};
        return Answer{Answer::Text, mergeAnswer};
    };
    const SummaryRequest req = request(transcriptMinutes(45), cache);
    {
        SummaryService svc(&llm);
        QSignalSpy failed(&svc, &SummaryService::summaryFailed);
        svc.summarize(req);
        QVERIFY(failed.wait(2000));
        const QString e = failed[0][0].toString();
        QVERIFY2(e.contains(QStringLiteral("nincs JSON")), qPrintable(e));
        QVERIFY2(e.contains(QStringLiteral("csak az összegzés")), qPrintable(e));
    }
    QCOMPARE(SummaryService::plan(req).llmCalls, 1);
    mergeAnswer = QString::fromUtf8(kMergeJson);
    llm.requests.clear();
    SummaryService svc(&llm);
    QSignalSpy ready(&svc, &SummaryService::summaryReady);
    svc.summarize(req);
    QVERIFY(ready.wait(2000));
    QCOMPARE(llm.requests.size(), 1);
    QVERIFY(isMerge(llm.requests[0]));
}

void SummaryTest::badSingleJsonKeepsNotes()
{
    QTemporaryDir dir;
    const QString cache = dir.filePath(QStringLiteral("summary.notes.json"));
    ScriptedProvider llm;
    llm.script = [](int n, const LlmRequest& r) {
        if (n == 0 && isSingle(r))
            return Answer{Answer::Text, QStringLiteral("TOPICS\n### Árazás\n- 100 euró.\nSUMMARY\nA JSON sajnos elmaradt.")};
        return Answer{Answer::Text, QString::fromUtf8(kMergeJson)};
    };
    const SummaryRequest req = request(transcriptMinutes(10), cache);
    {
        SummaryService svc(&llm);
        QSignalSpy failed(&svc, &SummaryService::summaryFailed);
        svc.summarize(req);
        QVERIFY(failed.wait(2000));
        QVERIFY(failed[0][0].toString().contains(QStringLiteral("A jegyzet megmaradt")));
    }
    QVERIFY(QFile::exists(cache));
    const SummaryPlan plan = SummaryService::plan(req);
    QVERIFY(!plan.singleCall);
    QCOMPARE(plan.llmCalls, 1);
    // Újrapróbálás: csak az összegzés, a megmaradt jegyzetből — a memó is megvan.
    llm.requests.clear();
    SummaryService svc(&llm);
    QSignalSpy ready(&svc, &SummaryService::summaryReady);
    svc.summarize(req);
    QVERIFY(ready.wait(2000));
    QCOMPARE(llm.requests.size(), 1);
    QVERIFY(isMerge(llm.requests[0]));
    const Summary s = qvariant_cast<Summary>(ready[0][0]);
    QCOMPARE(s.memo.size(), 1);
    QCOMPARE(s.memo[0].title, QStringLiteral("Árazás"));

    // Egy csonka, de javítható JSON egyetlen rossz mezővel nem viszi el az egészet.
    llm.script = [](int, const LlmRequest&) {
        return Answer{Answer::Text, QStringLiteral("TOPICS\n### Árazás\n- 100 euró.\nSUMMARY\n"
                                                   "{\"execSummary\": \"Rövid.\", \"decisions\": [\"A\", ")};
    };
    SummaryService svc2(&llm);
    QSignalSpy ready2(&svc2, &SummaryService::summaryReady);
    svc2.summarize(req);
    QVERIFY(ready2.wait(2000));
    QCOMPARE(qvariant_cast<Summary>(ready2[0][0]).decisions, QStringList({QStringLiteral("A")}));
}

void SummaryTest::cancelMidRun()
{
    QTemporaryDir dir;
    const QString cache = dir.filePath(QStringLiteral("summary.notes.json"));
    ScriptedProvider llm;
    llm.script = [](int, const LlmRequest& r) {
        if (isNotes(r) && partOf(r) == 2) return Answer{Answer::Hold, QString()};
        return Answer{Answer::Text, isNotes(r) ? notesFor(partOf(r) - 1) : QString::fromUtf8(kMergeJson)};
    };
    SummaryService svc(&llm);
    QSignalSpy ready(&svc, &SummaryService::summaryReady);
    QSignalSpy failed(&svc, &SummaryService::summaryFailed);
    svc.summarize(request(transcriptMinutes(45), cache));
    QTRY_COMPARE(llm.requests.size(), 2);
    svc.cancel();
    QVERIFY(llm.jobs[1] && llm.jobs[1]->cancelled);
    QTest::qWait(100);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(failed.count(), 0);                                 // megszakításnál nincs hibajel
    QCOMPARE(llm.requests.size(), 2);                            // a 3. rész nem indult
    QVERIFY(QFile::exists(cache));                               // az 1. rész megmaradt
}

// ---- markdown ---------------------------------------------------------------------------

void SummaryTest::renderMarkdownHasHungarianHeaders()
{
    tanara::Summary sum;
    sum.execSummary = QStringLiteral("Összefoglaló szöveg.");
    sum.decisions = QStringList{QStringLiteral("Döntés 1")};
    sum.openQuestions = QStringList{QStringLiteral("Kérdés 1")};
    ActionItem ai;
    ai.text = QStringLiteral("Teendő");
    ai.owner = QStringLiteral("Ádám");
    ai.due = QStringLiteral("holnap");
    sum.actionItems.append(ai);
    sum.participants = QStringList{QStringLiteral("Ádám"), QStringLiteral("Béla")};
    sum.memo = {MemoSection{QStringLiteral("Árazás"), 0, 330000, {QStringLiteral("Pont 1"), QStringLiteral("Pont 2")}},
                MemoSection{QStringLiteral("Pilot"), 6120000, 6300000, {QStringLiteral("Pont 3")}}};

    const QString md = sum.renderMarkdown();
    QVERIFY(md.contains(QStringLiteral("## Vezetői összefoglaló")));
    QVERIFY(md.contains(QStringLiteral("## Döntések")));
    QVERIFY(md.contains(QStringLiteral("## Nyitott kérdések\n\n- Kérdés 1\n")));
    QVERIFY(md.contains(QStringLiteral("## Teendők")));
    QVERIFY(md.contains(QStringLiteral("## Résztvevők")));
    QVERIFY(md.contains(QStringLiteral("- [ ] Teendő — Ádám (holnap)")));
    QVERIFY(md.contains(QStringLiteral("Ádám, Béla")));
    QVERIFY(md.contains(QStringLiteral("## Memó\n\n### Árazás (00:00–05:30)\n\n- Pont 1\n- Pont 2\n")));
    QVERIFY(md.contains(QStringLiteral("### Pilot (102:00–105:00)")));
    QVERIFY(md.indexOf(QStringLiteral("## Nyitott")) < md.indexOf(QStringLiteral("## Teendők")));
    QVERIFY(md.indexOf(QStringLiteral("## Résztvevők")) < md.indexOf(QStringLiteral("## Memó")));
    QVERIFY(md.endsWith(QStringLiteral("- Pont 3\n")));
}

void SummaryTest::emptySectionsSkipped()
{
    tanara::Summary sum;
    sum.execSummary = QStringLiteral("Csak összefoglaló.");
    const QString md = sum.renderMarkdown();
    QVERIFY(md.contains(QStringLiteral("## Vezetői összefoglaló")));
    QVERIFY(!md.contains(QStringLiteral("## Döntések")));
    QVERIFY(!md.contains(QStringLiteral("## Nyitott")));
    QVERIFY(!md.contains(QStringLiteral("## Teendők")));
    QVERIFY(!md.contains(QStringLiteral("## Résztvevők")));
    QVERIFY(!md.contains(QStringLiteral("## Memó")));
}

QTEST_GUILESS_MAIN(SummaryTest)
#include "test_summary.moc"

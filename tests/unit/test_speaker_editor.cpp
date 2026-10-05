//
// SpeakerEditor / SpeakerOverlay / PeopleDirectory unit-tesztek — ideiglenes mappákban,
// HAMIS embedderrel (a „hang" a megszólalás kezdőidejéből tudható). Valódi adatot,
// modellt vagy ffmpeg-et NEM használ.
//
// A szintetikus meeting: a diarizáció két címkét adott („Beszélő 1", „Beszélő 2"), de a
// „Beszélő 1" valójában KÉT ember (A = Anna, C = Cili) — pont az az eset, amit a
// szerkesztőnek szét kell tudnia bontani. „Beszélő 2" = B = Béla; egy sora (17.) tévesen
// került hozzá (valójában A hangja).
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/Types.h"
#include "tanara/edit/PeopleDirectory.h"
#include "tanara/edit/SpeakerAnalysis.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/VoiceprintStore.h"

#include <atomic>
#include <memory>

using namespace tanara;

namespace {

const QString kB1 = QStringLiteral("Beszélő 1");
const QString kB2 = QStringLiteral("Beszélő 2");

struct Row { qint64 startMs; qint64 durMs; QString raw; char voice; };

const QVector<Row>& scenario()
{
    static const QVector<Row> rows{
        {0,      4000, kB1, 'A'},   // 0
        {6000,   5000, kB2, 'B'},   // 1
        {13000,  4000, kB1, 'A'},   // 2
        {19000,   800, kB2, 'B'},   // 3  rövid
        {22000,  4000, kB1, 'C'},   // 4
        {28000,  6000, kB2, 'B'},   // 5
        {36000,  4000, kB1, 'A'},   // 6
        {42000,  5000, kB1, 'C'},   // 7
        {49000,  4000, kB2, 'B'},   // 8
        {55000,  3500, kB1, 'C'},   // 9
        {60500,  4500, kB1, 'A'},   // 10
        {67000,  5000, kB2, 'B'},   // 11
        {74000,  1000, kB1, 'C'},   // 12 rövid
        {77000,  4000, kB1, 'A'},   // 13
        {83000,  6000, kB1, 'C'},   // 14
        {91000,  5000, kB2, 'B'},   // 15
        {98000,  4000, kB1, 'A'},   // 16
        {104000, 4000, kB2, 'A'},   // 17 tévesen a 2-es címkén
    };
    return rows;
}

QString uid(int row) { return QStringLiteral("u%1").arg(scenario()[row].startMs); }
QStringList uids(std::initializer_list<int> rows)
{
    QStringList out;
    for (int r : rows) out << uid(r);
    return out;
}

// Hamis embedder: a szelet közepe alapján megmondja, kinek a hangja (ortogonális vektorok).
struct FakeWorld {
    std::atomic_int embedCalls{0};
    std::atomic_int opens{0};
    std::atomic_bool openOk{true};
    std::atomic_int delayMs{0};
};

class FakeEmbedder : public IUtteranceEmbedder {
public:
    explicit FakeEmbedder(std::shared_ptr<FakeWorld> w) : m_world(std::move(w)) {}
    bool open(const QString&) override { ++m_world->opens; return m_world->openOk; }
    QVector<float> embed(qint64 startMs, qint64 endMs) override
    {
        ++m_world->embedCalls;
        if (m_world->delayMs > 0) QThread::msleep(m_world->delayMs);
        const qint64 mid = (startMs + endMs) / 2;
        for (const Row& r : scenario()) {
            if (mid < r.startMs || mid > r.startMs + r.durMs) continue;
            switch (r.voice) {
            case 'A': return {1.0f, 0.0f, 0.0f};
            case 'B': return {0.0f, 1.0f, 0.0f};
            default:  return {0.0f, 0.0f, 1.0f};
            }
        }
        return {};
    }
private:
    std::shared_ptr<FakeWorld> m_world;
};

void writeJson(const QString& path, const QJsonDocument& doc)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(doc.toJson());
}

QString readText(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

// Egy teszt-környezet: ideiglenes audio/metadata mappa, store-ok, egy átírt meeting.
struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<MeetingStore> store;
    std::unique_ptr<PeopleStore> people;
    std::unique_ptr<VoiceprintStore> prints;
    std::shared_ptr<FakeWorld> world = std::make_shared<FakeWorld>();
    Meeting meeting;

    Fixture()
    {
        store = std::make_unique<MeetingStore>(dir.filePath(QStringLiteral("rec")),
                                               dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
        meeting = store->createMeeting(QStringLiteral("Teszt"));
        writeTranscript(meeting.folder);
        meeting.hasTranscript = true;
        store->saveMeeting(meeting);
    }

    static void writeTranscript(const QString& folder)
    {
        MergedTranscript mt;
        mt.language = QStringLiteral("hu");
        int i = 0;
        for (const Row& r : scenario()) {
            TranscriptToken a, b;
            a.speaker = b.speaker = r.raw;
            a.trackId = b.trackId = QStringLiteral("mixdown");
            a.startMs = r.startMs;               a.endMs = r.startMs + r.durMs / 2;
            b.startMs = r.startMs + r.durMs / 2; b.endMs = r.startMs + r.durMs;
            a.text = QStringLiteral("L%1a").arg(i);
            b.text = QStringLiteral(" L%1b").arg(i);
            mt.tokens << a << b;
            ++i;
        }
        QJsonArray toks;
        for (const TranscriptToken& t : mt.tokens) {
            QJsonObject o;
            o["text"] = t.text; o["speaker"] = t.speaker;
            o["startMs"] = double(t.startMs); o["endMs"] = double(t.endMs);
            o["confidence"] = 1.0; o["trackId"] = t.trackId;
            toks.append(o);
        }
        QJsonObject root;
        root["language"] = mt.language;
        root["tokens"] = toks;
        writeJson(QDir(folder).filePath(QStringLiteral("transcript.tokens.json")), QJsonDocument(root));

        QJsonArray segs;
        for (const Utterance& u : mt.segments()) {
            QJsonObject o;
            o["startMs"] = double(u.startMs); o["endMs"] = double(u.endMs);
            o["speaker"] = u.speaker; o["text"] = u.text;
            segs.append(o);
        }
        writeJson(QDir(folder).filePath(QStringLiteral("transcript.segments.json")), QJsonDocument(segs));
    }

    void setSpeakerMap(const QMap<QString, QString>& map, bool hasSummary = false)
    {
        meeting = store->load(meeting.id);
        meeting.speakerMap = map;
        meeting.hasSummary = hasSummary;
        store->saveMeeting(meeting);
    }

    std::unique_ptr<SpeakerEditor> editor(bool withEmbedder = true)
    {
        auto ed = std::make_unique<SpeakerEditor>(store.get(), people.get(), prints.get(), meeting.id);
        if (withEmbedder) {
            auto w = world;
            ed->setEmbedderFactory([w] { return std::make_unique<FakeEmbedder>(w); });
        }
        return ed;
    }

    // Embeddingek kiszámolása (megvárva a háttérszálat).
    static bool embed(SpeakerEditor& ed)
    {
        QSignalSpy done(&ed, &SpeakerEditor::embeddingFinished);
        ed.startEmbedding();
        if (done.isEmpty() && !done.wait(10000)) return false;
        return done.last().at(0).toBool();
    }

    QString markdown() const
    {
        return readText(QDir(meeting.folder).filePath(QStringLiteral("transcript.md")));
    }
    QMap<QString, QString> diskMap() const { return store->load(meeting.id).speakerMap; }
};

EditorSpeaker findSpeaker(const SpeakerEditor& ed, const QString& key) { return ed.speaker(key); }

} // namespace

class SpeakerEditorTest : public QObject {
    Q_OBJECT
private slots:
    void utteranceIds_stableAndUnique();
    void readModel_speakersAndUtterances();
    void oldMeeting_withoutOverlayOrTranscript();
    void move_undoRedo_persist();
    void move_toNewPerson_andAnonymous();
    void reassign_keepsSpeakerMapSemantics();
    void reassign_toExistingPerson_merges();
    void reassign_fixVoiceprints_undo();
    void revertToAnonymous();
    void merge_undo();
    void addAndRemoveParticipant();
    void confirm_andUncertainty();
    void shortLines_neverUncertain();
    void recheck_fromConfirmedLines_splitsMixedSpeaker();
    void suggestion_afterMove_acceptIsOneStep();
    void embedding_progressCancelAndCache();
    void embedding_gracefulWithoutModel();
    void voiceprint_explicitOnly();
    void summaryStale_lifecycle();
    void retranscribe_impactBackupDiscard();
    void overlay_fingerprintMismatchDropsEdits();
    void applyResolvedSpeakers_tokens();
    void externalChange_refresh();
    void fineGrainedSignals();
    void people_directoryAndSearch();
    void analysis_pureFunctions();
};

void SpeakerEditorTest::utteranceIds_stableAndUnique()
{
    QTemporaryDir dir;
    QJsonArray segs;
    auto add = [&](qint64 s, qint64 e, const QString& spk) {
        QJsonObject o;
        o["startMs"] = double(s); o["endMs"] = double(e); o["speaker"] = spk; o["text"] = QStringLiteral("x");
        segs.append(o);
    };
    add(1000, 2000, kB1);
    add(1000, 2500, kB2);   // azonos kezdőidő (átfedő beszélők)
    add(3000, 4000, kB1);
    writeJson(dir.filePath(QStringLiteral("transcript.segments.json")), QJsonDocument(segs));

    const QVector<TranscriptLine> a = speakeredit::loadTranscriptLines(dir.path());
    QCOMPARE(a.size(), 3);
    QCOMPARE(a[0].id, QStringLiteral("u1000"));
    QCOMPARE(a[1].id, QStringLiteral("u1000-2"));
    QCOMPARE(a[2].id, QStringLiteral("u3000"));
    // Újraolvasva ugyanazok az azonosítók és ugyanaz az ujjlenyomat.
    const QVector<TranscriptLine> b = speakeredit::loadTranscriptLines(dir.path());
    for (int i = 0; i < a.size(); ++i) QCOMPARE(a[i].id, b[i].id);
    QCOMPARE(speakeredit::transcriptFingerprint(a), speakeredit::transcriptFingerprint(b));
}

void SpeakerEditorTest::readModel_speakersAndUtterances()
{
    Fixture fx;
    fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
    Voiceprint vp; vp.embedding = {0, 1, 0};
    fx.prints->addPrint(QStringLiteral("Béla"), vp);
    speakeredit::recordIdentification(fx.meeting.folder, kB2, QStringLiteral("Béla"), 0.82);

    auto ed = fx.editor();
    ed->setUserSpeakerName(QStringLiteral("béla"));
    QVERIFY(ed->hasTranscript());
    QCOMPARE(ed->utteranceCount(), scenario().size());

    const EditorUtterance u4 = ed->utterance(uid(4));
    QCOMPARE(u4.index, 4);
    QCOMPARE(u4.startMs, 22000);
    QCOMPARE(u4.endMs, 26000);
    QCOMPARE(u4.rawLabel, kB1);
    QCOMPARE(u4.speakerKey, kB1);
    QCOMPARE(u4.text, QStringLiteral("L4a L4b"));
    QVERIFY(!u4.uncertain && !u4.manuallyCorrected && !u4.confirmed);
    QCOMPARE(ed->indexOf(uid(4)), 4);
    QCOMPARE(ed->utterance(QStringLiteral("nincs")).index, -1);

    const QVector<EditorSpeaker> sp = ed->speakers();
    QCOMPARE(sp.size(), 2);
    QCOMPARE(sp[0].key, kB1);
    QCOMPARE(sp[0].displayName, kB1);       // névtelen → a nyers címke
    QVERIFY(sp[0].anonymous);
    QCOMPARE(sp[0].colorIndex, 0);
    QCOMPARE(sp[0].utteranceCount, 11);
    QVERIFY(!sp[0].hasVoiceprint);
    QVERIFY(sp[0].voiceConfidence < 0);

    QCOMPARE(sp[1].key, kB2);
    QCOMPARE(sp[1].displayName, QStringLiteral("Béla"));
    QCOMPARE(sp[1].personName, QStringLiteral("Béla"));
    QVERIFY(!sp[1].anonymous);
    QVERIFY(sp[1].isSelf);
    QCOMPARE(sp[1].colorIndex, 1);
    QCOMPARE(sp[1].utteranceCount, 7);
    QVERIFY(sp[1].hasVoiceprint);
    QCOMPARE(sp[1].voiceConfidence, 0.82);

    qint64 total = 0;
    for (const Row& r : scenario()) total += r.durMs;
    QCOMPARE(sp[0].talkTimeMs + sp[1].talkTimeMs, total);
    QVERIFY(qAbs(sp[0].talkShare + sp[1].talkShare - 1.0) < 1e-9);
}

void SpeakerEditorTest::oldMeeting_withoutOverlayOrTranscript()
{
    Fixture fx;
    // Régi meeting: nincs transcript.speakers.json → minden a nyers állapotból oldódik fel.
    QVERIFY(!QFile::exists(speakeredit::overlayPath(fx.meeting.folder)));
    auto ed = fx.editor();
    QCOMPARE(ed->speakers().size(), 2);
    QVERIFY(!ed->canUndo());
    // Az olvasás nem hoz létre overlay-fájlt.
    QVERIFY(!QFile::exists(speakeredit::overlayPath(fx.meeting.folder)));

    // Átirat nélküli meeting: üres modell, a műveletek no-opok.
    const Meeting empty = fx.store->createMeeting(QStringLiteral("Üres"));
    SpeakerEditor none(fx.store.get(), fx.people.get(), fx.prints.get(), empty.id);
    QVERIFY(!none.hasTranscript());
    QCOMPARE(none.utteranceCount(), 0);
    QVERIFY(none.speakers().isEmpty());
    QVERIFY(!none.moveUtterances({QStringLiteral("u0")}, kB1));
    QVERIFY(none.moveUtterancesToPerson({QStringLiteral("u0")}, QStringLiteral("X")).isEmpty());

    // Ismeretlen meeting sem omlik össze.
    SpeakerEditor unknown(fx.store.get(), nullptr, nullptr, QStringLiteral("nincs-ilyen"));
    QVERIFY(!unknown.hasTranscript());
    QVERIFY(unknown.addParticipant(QStringLiteral("X")).isEmpty());
}

void SpeakerEditorTest::move_undoRedo_persist()
{
    Fixture fx;
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Béla")}});
    auto ed = fx.editor();

    QVERIFY(!ed->moveUtterances(uids({4}), kB1));               // már ott van → nincs lépés
    QVERIFY(!ed->moveUtterances(uids({4}), QStringLiteral("nincs")));
    QVERIFY(!ed->canUndo());

    QVERIFY(ed->moveUtterances(uids({4, 7}), kB2));
    QCOMPARE(ed->utterance(uid(4)).speakerKey, kB2);
    QVERIFY(ed->utterance(uid(4)).manuallyCorrected);
    QCOMPARE(ed->utterance(uid(4)).rawLabel, kB1);              // a nyers címke érintetlen
    QCOMPARE(ed->speaker(kB1).utteranceCount, 9);
    QCOMPARE(ed->speaker(kB2).utteranceCount, 9);
    QVERIFY(ed->canUndo());
    QVERIFY(!ed->canRedo());
    QVERIFY(ed->undoText().contains(QStringLiteral("Béla")));
    QVERIFY(ed->undoText().startsWith(QStringLiteral("2 ")));

    // A transcript.md a feloldott nevekkel frissült; a segments.json nyers maradt.
    ed->flushPendingWrites();   // a transcript.md késleltetve íródik
    QVERIFY(fx.markdown().contains(QStringLiteral("**Béla** L4a L4b")));
    QVERIFY(fx.markdown().contains(QStringLiteral("**Anna** L0a L0b")));
    QCOMPARE(speakeredit::loadTranscriptLines(fx.meeting.folder)[4].rawLabel, kB1);

    // Perzisztens: egy új munkamenet ugyanazt látja (undo-verem nélkül).
    {
        auto ed2 = fx.editor();
        QCOMPARE(ed2->utterance(uid(7)).speakerKey, kB2);
        QVERIFY(ed2->utterance(uid(7)).manuallyCorrected);
        QVERIFY(!ed2->canUndo());
    }

    ed->undo();
    QCOMPARE(ed->utterance(uid(4)).speakerKey, kB1);
    QVERIFY(!ed->utterance(uid(4)).manuallyCorrected);
    QVERIFY(!ed->canUndo());
    QVERIFY(ed->canRedo());
    ed->flushPendingWrites();   // a transcript.md késleltetve íródik
    QVERIFY(fx.markdown().contains(QStringLiteral("**Anna** L4a L4b")));
    QVERIFY(!QFile::exists(speakeredit::overlayPath(fx.meeting.folder)));   // üres overlay → nincs fájl

    ed->redo();
    QCOMPARE(ed->utterance(uid(7)).speakerKey, kB2);
    QVERIFY(ed->canUndo());
    QVERIFY(!ed->canRedo());

    // Új művelet törli a redo-vermet; minden művelet pontosan egy lépés.
    ed->undo();
    QVERIFY(ed->moveUtterances(uids({9}), kB2));
    QVERIFY(!ed->canRedo());
    ed->undo();
    QVERIFY(!ed->canUndo());
}

void SpeakerEditorTest::move_toNewPerson_andAnonymous()
{
    Fixture fx;
    fx.people->add(QStringLiteral("Anna"));
    auto ed = fx.editor();

    // Vadonatúj személy: résztvevő jön létre, a névlistába is bekerül — lenyomat NEM készül.
    const QString cili = ed->moveUtterancesToPerson(uids({4, 7}), QStringLiteral("  Cili "));
    QVERIFY(speakeredit::isParticipantKey(cili));
    EditorSpeaker s = ed->speaker(cili);
    QCOMPARE(s.displayName, QStringLiteral("Cili"));
    QCOMPARE(s.personName, QStringLiteral("Cili"));
    QVERIFY(s.added);
    QCOMPARE(s.utteranceCount, 2);
    QCOMPARE(s.colorIndex, 2);                  // a két nyers címke után
    QVERIFY(fx.people->names().contains(QStringLiteral("Cili")));
    QCOMPARE(fx.prints->totalPrintCount(), 0);
    ed->flushPendingWrites();   // a transcript.md késleltetve íródik
    QVERIFY(fx.markdown().contains(QStringLiteral("**Cili** L4a L4b")));

    // Ugyanahhoz a személyhez újra: a meglévő beszélőhöz megy, nem lesz második oszlop.
    QCOMPARE(ed->moveUtterancesToPerson(uids({9}), QStringLiteral("cili")), cili);
    QCOMPARE(ed->speakers().size(), 3);
    QCOMPARE(ed->speaker(cili).utteranceCount, 3);

    // Új névtelen résztvevő.
    const QString anon = ed->moveUtterancesToNewParticipant(uids({14}));
    QVERIFY(speakeredit::isParticipantKey(anon));
    QVERIFY(anon != cili);
    QVERIFY(ed->speaker(anon).anonymous);
    QCOMPARE(ed->speaker(anon).displayName, QStringLiteral("Új beszélő 1"));
    QCOMPARE(ed->speaker(anon).colorIndex, 3);
    QCOMPARE(ed->speaker(cili).colorIndex, 2);  // a szín a szerkesztéstől nem változik

    // Undo ×3: minden visszaáll, az újonnan felvett személy is kikerül a névlistából.
    ed->undo(); ed->undo(); ed->undo();
    QVERIFY(!ed->canUndo());
    QCOMPARE(ed->speakers().size(), 2);
    QVERIFY(!fx.people->names().contains(QStringLiteral("Cili")));
    QVERIFY(fx.people->names().contains(QStringLiteral("Anna")));
    ed->redo();
    QVERIFY(fx.people->names().contains(QStringLiteral("Cili")));
    QCOMPARE(ed->speaker(cili).utteranceCount, 2);
}

void SpeakerEditorTest::reassign_keepsSpeakerMapSemantics()
{
    Fixture fx;
    auto ed = fx.editor();

    // Nyers beszélő elnevezése = a régi speakerMap-bejegyzés (a CLI ezt olvassa).
    QVERIFY(ed->reassignSpeaker(kB1, QStringLiteral("Anna")));
    QCOMPARE(fx.diskMap().value(kB1), QStringLiteral("Anna"));
    QCOMPARE(ed->speaker(kB1).displayName, QStringLiteral("Anna"));
    QVERIFY(fx.people->names().contains(QStringLiteral("Anna")));
    ed->flushPendingWrites();   // a transcript.md késleltetve íródik
    QVERIFY(fx.markdown().contains(QStringLiteral("**Anna** L0a L0b")));
    QCOMPARE(fx.prints->totalPrintCount(), 0);      // nincs automatikus tanítás
    QVERIFY(!ed->reassignSpeaker(kB1, QStringLiteral("anna")));     // ugyanaz → nincs lépés
    QVERIFY(!ed->reassignSpeaker(kB1, QStringLiteral("  ")));

    ed->undo();
    QVERIFY(!fx.diskMap().contains(kB1));
    QVERIFY(ed->speaker(kB1).anonymous);
    ed->flushPendingWrites();   // a transcript.md késleltetve íródik
    QVERIFY(fx.markdown().contains(QStringLiteral("**Beszélő 1** L0a L0b")));
    ed->redo();
    QCOMPARE(fx.diskMap().value(kB1), QStringLiteral("Anna"));

    // Kézzel felvett résztvevő átnevezése az overlay-ben él (a speakerMap nem változik).
    const QString p = ed->moveUtterancesToNewParticipant(uids({4}));
    QVERIFY(ed->reassignSpeaker(p, QStringLiteral("Cili")));
    QCOMPARE(ed->speaker(p).personName, QStringLiteral("Cili"));
    QCOMPARE(fx.diskMap().size(), 1);
}

void SpeakerEditorTest::reassign_toExistingPerson_merges()
{
    Fixture fx;
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}});
    auto ed = fx.editor();
    // A 2-es címkét is Annához rendeljük → nem lesz két Anna-oszlop: összevonás.
    QVERIFY(ed->reassignSpeaker(kB2, QStringLiteral("Anna")));
    QCOMPARE(ed->speakers().size(), 1);
    QCOMPARE(ed->speaker(kB1).utteranceCount, scenario().size());
    QCOMPARE(ed->utterance(uid(1)).speakerKey, kB1);
    QCOMPARE(fx.diskMap().value(kB2), QStringLiteral("Anna"));  // a régi UI is Annát lát
    ed->undo();
    QCOMPARE(ed->speakers().size(), 2);
    QVERIFY(!fx.diskMap().contains(kB2));
}

void SpeakerEditorTest::reassign_fixVoiceprints_undo()
{
    Fixture fx;
    fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
    // Béla lenyomatai: egy EBBŐL a meetingből (a 2-es címke egyik sorából), egy máshonnan.
    Voiceprint here; here.embedding = {0, 1, 0};
    here.sourceMeetingId = fx.meeting.id;
    here.sampleRef = QStringLiteral("mixdown.mp3#28000-34000");
    here.id = QStringLiteral("itt");
    Voiceprint other; other.embedding = {0, 1, 0};
    other.sourceMeetingId = QStringLiteral("masik-meeting");
    other.id = QStringLiteral("mashol");
    fx.prints->addPrint(QStringLiteral("Béla"), here);
    fx.prints->addPrint(QStringLiteral("Béla"), other);

    auto ed = fx.editor();
    QVERIFY(Fixture::embed(*ed));

    // „Téves felismerés": a 2-es címke valójában Dóra.
    QVERIFY(ed->reassignSpeaker(kB2, QStringLiteral("Dóra"), /*fixVoiceprints*/ true));
    QCOMPARE(fx.prints->printCount(QStringLiteral("Béla")), 1);
    QCOMPARE(fx.prints->printsFor(QStringLiteral("Béla")).first().id, QStringLiteral("mashol"));
    QCOMPARE(fx.prints->printCount(QStringLiteral("Dóra")), 1);
    const Voiceprint made = fx.prints->printsFor(QStringLiteral("Dóra")).first();
    QCOMPARE(made.sourceMeetingId, fx.meeting.id);
    QVERIFY(VoiceprintStore::cosineSimilarity(made.embedding, {0, 1, 0}) > 0.99);
    QVERIFY(ed->speaker(kB2).hasVoiceprint);

    // Az undo a lenyomat-mellékhatásokat is visszacsinálja…
    ed->undo();
    QCOMPARE(fx.prints->printCount(QStringLiteral("Béla")), 2);
    QCOMPARE(fx.prints->printCount(QStringLiteral("Dóra")), 0);
    QCOMPARE(fx.diskMap().value(kB2), QStringLiteral("Béla"));
    // …a redo pedig újra alkalmazza (ugyanazzal a lenyomattal).
    ed->redo();
    QCOMPARE(fx.prints->printCount(QStringLiteral("Béla")), 1);
    QCOMPARE(fx.prints->printsFor(QStringLiteral("Dóra")).first().id, made.id);
    // A lemezen is (a store perzisztál).
    VoiceprintStore reread(fx.prints->filePath());
    QCOMPARE(reread.printCount(QStringLiteral("Dóra")), 1);

    // Jelölő nélkül a lenyomatokhoz nem nyúl.
    ed->undo();
    QVERIFY(ed->reassignSpeaker(kB2, QStringLiteral("Dóra"), false));
    QCOMPARE(fx.prints->printCount(QStringLiteral("Béla")), 2);
    QCOMPARE(fx.prints->printCount(QStringLiteral("Dóra")), 0);
}

void SpeakerEditorTest::revertToAnonymous()
{
    Fixture fx;
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}});
    auto ed = fx.editor();
    QVERIFY(!ed->revertSpeakerToAnonymous(kB2));        // már névtelen
    QVERIFY(ed->revertSpeakerToAnonymous(kB1));
    QVERIFY(ed->speaker(kB1).anonymous);
    QCOMPARE(ed->speaker(kB1).displayName, kB1);
    QVERIFY(!fx.diskMap().contains(kB1));
    ed->undo();
    QCOMPARE(ed->speaker(kB1).personName, QStringLiteral("Anna"));

    // Nevesített résztvevő névtelenítve címkét kap.
    const QString p = ed->moveUtterancesToPerson(uids({4}), QStringLiteral("Cili"));
    QVERIFY(ed->revertSpeakerToAnonymous(p));
    QVERIFY(ed->speaker(p).anonymous);
    QCOMPARE(ed->speaker(p).displayName, QStringLiteral("Új beszélő 1"));
    QCOMPARE(ed->speaker(p).utteranceCount, 1);
}

void SpeakerEditorTest::merge_undo()
{
    Fixture fx;
    fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
    auto ed = fx.editor();
    const QString cili = ed->moveUtterancesToPerson(uids({4, 7}), QStringLiteral("Cili"));

    QVERIFY(!ed->mergeSpeakers(kB1, kB1));
    QVERIFY(!ed->mergeSpeakers(kB1, QStringLiteral("nincs")));

    // Nyers címke összevonása egy másikba: a régi UI felé a cél személye látszik.
    QVERIFY(ed->mergeSpeakers(kB1, kB2));
    QCOMPARE(ed->speakers().size(), 2);                 // Béla + Cili
    QVERIFY(ed->speaker(kB1).key.isEmpty());
    QCOMPARE(ed->utterance(uid(0)).speakerKey, kB2);
    QCOMPARE(ed->utterance(uid(4)).speakerKey, cili);   // a kézzel átrakott sor marad
    QVERIFY(!ed->utterance(uid(0)).manuallyCorrected);  // teljes-beszélő művelet nem „javítva"
    QCOMPARE(fx.diskMap().value(kB1), QStringLiteral("Béla"));
    ed->flushPendingWrites();   // a transcript.md késleltetve íródik
    QVERIFY(fx.markdown().contains(QStringLiteral("**Béla** L0a L0b")));
    QVERIFY(!ed->moveUtterances(uids({1}), kB1));       // megszűnt beszélő nem célpont

    // Résztvevő összevonása nyers beszélőbe: a résztvevő megszűnik.
    QVERIFY(ed->mergeSpeakers(cili, kB2));
    QCOMPARE(ed->speakers().size(), 1);
    QCOMPARE(ed->speaker(kB2).utteranceCount, scenario().size());

    // A cél átnevezése az összevont címkét is viszi (speakerMap-kompatibilitás).
    QVERIFY(ed->reassignSpeaker(kB2, QStringLiteral("Dóra")));
    QCOMPARE(fx.diskMap().value(kB1), QStringLiteral("Dóra"));
    QCOMPARE(fx.diskMap().value(kB2), QStringLiteral("Dóra"));

    ed->undo(); ed->undo();
    QCOMPARE(ed->speaker(cili).utteranceCount, 2);
    ed->undo();
    QCOMPARE(ed->speakers().size(), 3);
    QCOMPARE(ed->utterance(uid(0)).speakerKey, kB1);
    QVERIFY(!fx.diskMap().contains(kB1));
    QCOMPARE(fx.diskMap().value(kB2), QStringLiteral("Béla"));
}

void SpeakerEditorTest::addAndRemoveParticipant()
{
    Fixture fx;
    fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
    auto ed = fx.editor();

    // Sor nélküli résztvevő (akit a rendszer nem különített el).
    const QString dora = ed->addParticipant(QStringLiteral("Dóra"));
    QVERIFY(!dora.isEmpty());
    QCOMPARE(ed->speaker(dora).utteranceCount, 0);
    QCOMPARE(ed->speakers().size(), 3);
    QCOMPARE(ed->addParticipant(QStringLiteral("Béla")), kB2);     // már beszélő → nincs új
    QCOMPARE(ed->speakers().size(), 3);
    const QString anon = ed->addParticipant();
    QCOMPARE(ed->speaker(anon).displayName, QStringLiteral("Új beszélő 1"));

    // Perzisztens: új munkamenetben is ott vannak a sor nélküli résztvevők.
    QCOMPARE(fx.editor()->speakers().size(), 4);

    // Csak üres beszélő távolítható el.
    QVERIFY(ed->moveUtterances(uids({4}), dora));
    QVERIFY(!ed->removeParticipant(dora));
    QVERIFY(!ed->removeParticipant(kB1));
    QVERIFY(ed->removeParticipant(anon));
    QCOMPARE(ed->speakers().size(), 3);
    ed->undo();
    QCOMPARE(ed->speakers().size(), 4);
    QCOMPARE(ed->speaker(anon).displayName, QStringLiteral("Új beszélő 1"));

    // Kiürült nyers beszélő is eltávolítható (és undo-val visszajön).
    QStringList all;
    for (const EditorUtterance& u : ed->utterances())
        if (u.speakerKey == kB1) all << u.id;
    QVERIFY(ed->moveUtterances(all, kB2));
    QCOMPARE(ed->speaker(kB1).utteranceCount, 0);
    QVERIFY(ed->removeParticipant(kB1));
    QVERIFY(ed->speaker(kB1).key.isEmpty());
    ed->undo();
    QCOMPARE(ed->speaker(kB1).key, kB1);
}

void SpeakerEditorTest::confirm_andUncertainty()
{
    Fixture fx;
    auto ed = fx.editor();
    QCOMPARE(ed->uncertainCount(), 0);              // embedding nélkül semmi sem bizonytalan
    QVERIFY(Fixture::embed(*ed));

    // A 17. sor a 2-es címkén van, de A hangja → egy másik beszélőre hasonlít jobban.
    QCOMPARE(ed->uncertainUtteranceIds(), uids({17}));
    QVERIFY(ed->utterance(uid(17)).uncertain);
    QVERIFY(!ed->utterance(uid(1)).uncertain);

    // „Jó így": megerősítve többé nem bizonytalan; egy undo-lépés.
    QSignalSpy count(ed.get(), &SpeakerEditor::uncertainCountChanged);
    QVERIFY(ed->confirmUtterances(uids({17})));
    QVERIFY(ed->utterance(uid(17)).confirmed);
    QVERIFY(!ed->utterance(uid(17)).uncertain);
    QCOMPARE(ed->uncertainCount(), 0);
    QCOMPARE(count.last().at(0).toInt(), 0);
    QVERIFY(!ed->confirmUtterances(uids({17})));    // már megerősített
    ed->undo();
    QVERIFY(ed->utterance(uid(17)).uncertain);

    // Kézzel a helyére téve sem bizonytalan (és „javítva").
    QVERIFY(ed->moveUtterances(uids({17}), kB1));
    QVERIFY(!ed->utterance(uid(17)).uncertain);
    QVERIFY(ed->utterance(uid(17)).manuallyCorrected);
    QCOMPARE(ed->uncertainCount(), 0);
}

// Újraellenőrzés: a „Beszélő 1" két ember (A, C). Ha A három sorát megerősítjük, a mag
// csak A hangja → C sorai (a sajátjukhoz sem illenek) és a 2-es címkén ragadt A-sor kétesek.
void SpeakerEditorTest::recheck_fromConfirmedLines_splitsMixedSpeaker()
{
    Fixture fx;
    auto ed = fx.editor();
    QVERIFY(Fixture::embed(*ed));
    QCOMPARE(ed->uncertainUtteranceIds(), uids({17}));
    QVERIFY(!ed->canRecheck());

    QVERIFY(ed->confirmUtterances(uids({0, 2, 6})));
    QVERIFY(ed->canRecheck());
    const SpeakerEditor::RecheckResult r = ed->recheckFromConfirmed();
    QCOMPARE(r.flagged, 5);
    QCOMPARE(r.speakersWithConfirmedCore, 1);
    QCOMPARE(r.confirmedLines, 3);
    QCOMPARE(ed->uncertainUtteranceIds(), uids({4, 7, 9, 14, 17}));
    // C sorainál nincs javaslat (máshoz sem illenek), a 17. sornál a hang A-é → Beszélő 1.
    QCOMPARE(ed->utterance(uid(4)).likelySpeakerKey, QString());
    QCOMPARE(ed->utterance(uid(17)).likelySpeakerKey, kB1);
    QVERIFY(ed->utterance(uid(4)).rechecked);
    QVERIFY(!ed->utterance(uid(12)).uncertain);       // rövid: sosem kétes

    // A jelzés túléli a rendes újraszámolást (újabb embedding-futás, újratöltés).
    QVERIFY(Fixture::embed(*ed));
    QCOMPARE(ed->uncertainCount(), 5);
    ed.reset();
    auto again = fx.editor();
    QCOMPARE(again->uncertainUtteranceIds(), uids({4, 7, 9, 14, 17}));

    // C sorait új résztvevőhöz rakva a jelzés megszűnik (javítva).
    QVERIFY(!again->moveUtterancesToNewParticipant(uids({4, 7, 9, 14})).isEmpty());
    QCOMPARE(again->uncertainUtteranceIds(), uids({17}));
}

void SpeakerEditorTest::shortLines_neverUncertain()
{
    Fixture fx;
    auto ed = fx.editor();
    QVERIFY(Fixture::embed(*ed));
    // A rövid (3., 12.) sorokra nem is készül embedding → nem árasztják el a listát.
    const int longLines = int(std::count_if(scenario().cbegin(), scenario().cend(),
        [](const Row& r) { return r.durMs >= speakeredit::kMinEmbedMs; }));
    QCOMPARE(fx.world->embedCalls.load(), longLines);
    // Rossz helyre téve sem lesznek bizonytalanok (kézi munka marad).
    QVERIFY(ed->mergeSpeakers(kB2, kB1));
    QVERIFY(!ed->utterance(uid(3)).uncertain);
    QVERIFY(!ed->utterance(uid(12)).uncertain);
}

void SpeakerEditorTest::suggestion_afterMove_acceptIsOneStep()
{
    Fixture fx;
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Béla")}});
    auto ed = fx.editor();

    // Embedding nélkül nincs javaslat (de a művelet megy).
    QVERIFY(!ed->moveUtterancesToPerson(uids({4}), QStringLiteral("Cili")).isEmpty());
    QVERIFY(!ed->hasSuggestion());
    ed->undo();

    QVERIFY(Fixture::embed(*ed));
    QSignalSpy sugg(ed.get(), &SpeakerEditor::suggestionChanged);
    const QString cili = ed->moveUtterancesToPerson(uids({4}), QStringLiteral("Cili"));
    QVERIFY(ed->hasSuggestion());
    QCOMPARE(sugg.count(), 1);
    const SpeakerSuggestion sg = ed->suggestion();
    QCOMPARE(sg.targetSpeaker, cili);
    QCOMPARE(sg.sourceSpeakerKey, kB1);
    QCOMPARE(sg.anchorUtteranceId, uid(4));
    // Az 1-es címke többi C-hangú sora — a rövid 12. kimarad, A sorai nem kerülnek bele.
    QCOMPARE(sg.utteranceIds, uids({7, 9, 14}));

    // Elfogadás = EGY undo-lépés.
    QVERIFY(ed->acceptSuggestion());
    QVERIFY(!ed->hasSuggestion());
    QCOMPARE(ed->speaker(cili).utteranceCount, 4);
    QVERIFY(ed->utterance(uid(14)).manuallyCorrected);
    QVERIFY(ed->undoText().contains(QStringLiteral("Cili")));
    ed->undo();
    QCOMPARE(ed->speaker(cili).utteranceCount, 1);
    QCOMPARE(ed->utterance(uid(7)).speakerKey, kB1);
    ed->undo();
    QVERIFY(ed->speaker(cili).key.isEmpty());

    // Elvetés: a javaslat eltűnik, lépés nélkül.
    ed->redo();
    QVERIFY(!ed->hasSuggestion());                  // redo nem ajánl újra
    QVERIFY(ed->moveUtterances(uids({7}), cili));
    QVERIFY(ed->hasSuggestion());
    QCOMPARE(ed->suggestion().utteranceIds, uids({9, 14}));
    ed->dismissSuggestion();
    QVERIFY(!ed->hasSuggestion());
    QVERIFY(!ed->acceptSuggestion());
    QCOMPARE(ed->speaker(cili).utteranceCount, 2);
}

void SpeakerEditorTest::embedding_progressCancelAndCache()
{
    Fixture fx;
    auto ed = fx.editor();
    QVERIFY(ed->embeddingsSupported());
    QVERIFY(!ed->embeddingsComplete());

    // Megszakítás menet közben: részeredmény megmarad, a folytatás csak a hiányzókat számolja.
    fx.world->delayMs = 15;
    QSignalSpy progress(ed.get(), &SpeakerEditor::embeddingProgress);
    QSignalSpy finished(ed.get(), &SpeakerEditor::embeddingFinished);
    QSignalSpy running(ed.get(), &SpeakerEditor::embeddingRunningChanged);
    ed->startEmbedding();
    QVERIFY(ed->isEmbeddingRunning());
    ed->startEmbedding();                           // futás közben no-op
    ed->cancelEmbedding();
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.last().at(0).toBool(), false);
    QVERIFY(!ed->isEmbeddingRunning());
    QVERIFY(!ed->embeddingsComplete());
    QCOMPARE(running.count(), 2);
    const int afterCancel = fx.world->embedCalls.load();
    const int longLines = 16;
    QVERIFY(afterCancel < longLines);

    fx.world->delayMs = 0;
    QVERIFY(Fixture::embed(*ed));
    QVERIFY(ed->embeddingsComplete());
    QCOMPARE(fx.world->embedCalls.load(), longLines);   // semmit sem számolt kétszer
    QVERIFY(!progress.isEmpty());
    QCOMPARE(progress.last().at(0).toInt(), progress.last().at(1).toInt());
    QCOMPARE(fx.world->opens.load(), 2);                // futásonként EGY dekódolás

    // A cache a meeting-mappában él: új munkamenet számolás nélkül kész.
    QVERIFY(QFile::exists(UtteranceEmbeddingCache::filePath(fx.meeting.folder)));
    auto ed2 = fx.editor();
    QVERIFY(ed2->embeddingsComplete());
    QCOMPARE(ed2->uncertainCount(), 1);
    QVERIFY(Fixture::embed(*ed2));
    QCOMPARE(fx.world->embedCalls.load(), longLines);

    // Futó szál mellett is biztonságosan megszüntethető.
    UtteranceEmbeddingCache::remove(fx.meeting.folder);
    fx.world->delayMs = 15;
    auto ed3 = fx.editor();
    ed3->startEmbedding();
    ed3.reset();
}

void SpeakerEditorTest::embedding_gracefulWithoutModel()
{
    Fixture fx;
    // Nincs gyár (nincs modell / voice-ID nélküli build).
    auto plain = fx.editor(/*withEmbedder*/ false);
    QVERIFY(!plain->embeddingsSupported());
    QSignalSpy fin(plain.get(), &SpeakerEditor::embeddingFinished);
    plain->startEmbedding();
    QCOMPARE(fin.count(), 1);
    QCOMPARE(fin.first().at(0).toBool(), false);
    QVERIFY(plain->moveUtterances(uids({4}), kB2));     // a szerkesztés így is működik
    QCOMPARE(plain->uncertainCount(), 0);
    plain->undo();

    // Van gyár, de a hang / modell nem nyitható meg.
    fx.world->openOk = false;
    auto ed = fx.editor();
    QVERIFY(!Fixture::embed(*ed));
    QVERIFY(!ed->embeddingsComplete());
    QCOMPARE(ed->uncertainCount(), 0);
    QVERIFY(!ed->isEmbeddingRunning());

    // A valódi gyár hiányzó modellel: open() false, nem omlik össze.
    auto real = voiceUtteranceEmbedderFactory(QStringLiteral("/nincs/ilyen/modell.onnx"))();
    QVERIFY(real);
    QVERIFY(!real->open(QStringLiteral("/nincs/ilyen/hang.mp3")));
    QVERIFY(real->embed(0, 3000).isEmpty());
}

void SpeakerEditorTest::voiceprint_explicitOnly()
{
    Fixture fx;
    auto ed = fx.editor();

    // Névtelen beszélőhöz nem készül.
    VoiceprintResult r = ed->createVoiceprint(kB2);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());

    // Kevés anyag: megmondja, mennyi hiányzik, és nem készít gyenge lenyomatot.
    const QString cili = ed->moveUtterancesToPerson(uids({4}), QStringLiteral("Cili"));
    VoiceprintMaterial mat = ed->voiceprintMaterial(cili);
    QVERIFY(!mat.sufficient);
    QCOMPARE(mat.usableLines, 1);
    QCOMPARE(mat.usableMs, 4000);
    QCOMPARE(mat.missingMs, speakeredit::kPrintMinTotalMs - 4000);
    r = ed->createVoiceprint(cili);
    QVERIFY(!r.ok);
    QCOMPARE(r.missingMs, speakeredit::kPrintMinTotalMs - 4000);
    QVERIFY(r.error.contains(QStringLiteral("11")));
    QCOMPARE(fx.prints->totalPrintCount(), 0);

    // Elég anyaggal: a hosszabb sorokból, a cache nélkül is (ilyenkor most számol).
    QVERIFY(ed->moveUtterances(uids({7, 9, 14}), cili));
    mat = ed->voiceprintMaterial(cili);
    QVERIFY(mat.sufficient);
    QCOMPARE(mat.usableMs, 4000 + 5000 + 3500 + 6000);
    QCOMPARE(fx.prints->totalPrintCount(), 0);          // az átsorolás magától nem tanít
    QSignalSpy vpSig(ed.get(), &SpeakerEditor::voiceprintsChanged);
    r = ed->createVoiceprint(cili);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.usedLines, 4);
    QCOMPARE(vpSig.count(), 1);
    QCOMPARE(fx.prints->printCount(QStringLiteral("Cili")), 1);
    const Voiceprint vp = fx.prints->printsFor(QStringLiteral("Cili")).first();
    QCOMPARE(vp.id, r.printId);
    QCOMPARE(vp.sourceMeetingId, fx.meeting.id);
    QCOMPARE(vp.sampleRef, QStringLiteral("mixdown.mp3#83000-89000"));  // a leghosszabb sor
    QVERIFY(VoiceprintStore::cosineSimilarity(vp.embedding, {0, 0, 1}) > 0.99);
    QVERIFY(ed->speaker(cili).hasVoiceprint);

    // Modell nélkül: érthető hiba.
    fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
    auto plain = fx.editor(/*withEmbedder*/ false);
    // A kézi készítés visszavonása: pontosan az a lenyomat törlődik, a jelző visszaáll.
    {
        const VoiceprintResult again = ed->createVoiceprint(cili);
        QVERIFY(again.ok && !again.printId.isEmpty());
        const int before = fx.prints->printCount(QStringLiteral("Cili"));
        QSignalSpy speakersSpy(ed.get(), &SpeakerEditor::speakersChanged);
        QVERIFY(ed->removeVoiceprint(again.printId));
        QCOMPARE(fx.prints->printCount(QStringLiteral("Cili")), before - 1);
        QCOMPARE(speakersSpy.count(), 1);
        QVERIFY(!ed->removeVoiceprint(again.printId));
        QVERIFY(!ed->removeVoiceprint(QString()));
    }

    r = plain->createVoiceprint(kB2);
    QVERIFY(!r.ok);
    QCOMPARE(r.missingMs, 0);
    QVERIFY(!r.error.isEmpty());
}

void SpeakerEditorTest::summaryStale_lifecycle()
{
    Fixture fx;
    // Összefoglaló nélkül nincs mit elavultnak jelölni.
    {
        auto ed = fx.editor();
        QVERIFY(ed->moveUtterances(uids({4}), kB2));
        QVERIFY(!ed->summaryStale().stale);
        ed->undo();
    }

    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}}, /*hasSummary*/ true);
    auto ed = fx.editor();
    QSignalSpy stale(ed.get(), &SpeakerEditor::summaryStaleChanged);
    QVERIFY(!ed->summaryStale().stale);

    const QString cili = ed->moveUtterancesToPerson(uids({4}), QStringLiteral("Cili"));
    QVERIFY(ed->summaryStale().stale);
    QCOMPARE(ed->summaryStale().correctedSpeakers, 1);
    QCOMPARE(stale.count(), 1);
    QCOMPARE(stale.last().at(0).toBool(), true);
    QVERIFY(ed->moveUtterances(uids({7}), cili));               // ugyanaz a beszélő → még 1
    QCOMPARE(ed->summaryStale().correctedSpeakers, 1);
    QVERIFY(ed->reassignSpeaker(kB2, QStringLiteral("Béla")));
    QCOMPARE(ed->summaryStale().correctedSpeakers, 2);
    QVERIFY(ed->confirmUtterances(uids({1})));                  // megerősítés nem beszélő-változás
    QCOMPARE(ed->summaryStale().correctedSpeakers, 2);

    // Megnyitott szerkesztő nélkül is kiolvasható (könyvtár-ikon).
    QCOMPARE(speakeredit::summaryStale(fx.store->load(fx.meeting.id)).correctedSpeakers, 2);

    // Visszavonva az összefoglaló-kori állapotig: nem elavult.
    ed->undo(); ed->undo(); ed->undo(); ed->undo();
    QVERIFY(!ed->summaryStale().stale);
    ed->redo(); ed->redo();
    QCOMPARE(ed->summaryStale().correctedSpeakers, 1);

    // „Rendben így".
    ed->dismissSummaryStale();
    QVERIFY(!ed->summaryStale().stale);
    QCOMPARE(stale.last().at(0).toBool(), false);
    QVERIFY(!speakeredit::summaryStale(fx.store->load(fx.meeting.id)).stale);
    // Az elfogadás UTÁNI visszavonás újra változás az összefoglalóhoz képest.
    ed->undo();
    QVERIFY(ed->summaryStale().stale);

    // Új összefoglaló készült → törlődik; fájl-szinten is (szerkesztő nélkül).
    ed->notifySummaryRegenerated();
    QVERIFY(!ed->summaryStale().stale);
    ed.reset();
    const Meeting m = fx.store->load(fx.meeting.id);
    QVERIFY(speakeredit::markSummaryStale(m, {kB1}));           // régi UI átnevezés horga
    QVERIFY(!speakeredit::markSummaryStale(m, {kB1}));
    QVERIFY(speakeredit::summaryStale(m).stale);
    QVERIFY(speakeredit::clearSummaryStale(m.folder));
    QVERIFY(!speakeredit::summaryStale(m).stale);
}

void SpeakerEditorTest::retranscribe_impactBackupDiscard()
{
    Fixture fx;
    fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
    auto ed = fx.editor();
    QVERIFY(!ed->retranscribeImpact().manualCorrections());

    const QString cili = ed->moveUtterancesToPerson(uids({4, 7}), QStringLiteral("Cili"));
    QVERIFY(ed->confirmUtterances(uids({1, 4})));       // a 4. már javított → nem számít kétszer
    QVERIFY(Fixture::embed(*ed));
    RetranscribeImpact imp = ed->retranscribeImpact();
    QCOMPARE(imp.correctedUtterances, 2);
    QCOMPARE(imp.confirmedUtterances, 1);
    QCOMPARE(imp.manualCorrections(), 3);
    QCOMPARE(imp.addedParticipants, 1);
    QCOMPARE(imp.namedSpeakers, 2);
    // Fájl-szinten ugyanaz (megnyitott szerkesztő nélkül).
    const Meeting m = fx.store->load(fx.meeting.id);
    const RetranscribeImpact disk = speakeredit::retranscribeImpact(m);
    QCOMPARE(disk.manualCorrections(), 3);
    QCOMPARE(disk.namedSpeakers, 2);

    // Másolat a mostani átiratról.
    const QString backup = speakeredit::backupTranscript(m);
    QVERIFY(!backup.isEmpty());
    QVERIFY(backup.startsWith(m.folder));
    for (const char* f : {"transcript.md", "transcript.tokens.json", "transcript.segments.json",
                          "transcript.speakers.json", "speakerMap.json"})
        QVERIFY2(QFile::exists(QDir(backup).filePath(QLatin1String(f))), f);
    QVERIFY(readText(QDir(backup).filePath(QStringLiteral("transcript.md")))
                .contains(QStringLiteral("**Cili** L4a L4b")));
    QVERIFY(speakeredit::backupTranscript(m) != backup);    // második másolat nem írja felül

    // Új átirat készült: a kézi javítások és az embedding-cache eldobva.
    speakeredit::discardForNewTranscript(m.folder);
    QSignalSpy reloaded(ed.get(), &SpeakerEditor::reloaded);
    ed->reloadTranscript();
    QCOMPARE(reloaded.count(), 1);
    QCOMPARE(ed->speakers().size(), 2);
    QVERIFY(!ed->canUndo());
    QVERIFY(!ed->embeddingsComplete());
    QVERIFY(!QFile::exists(UtteranceEmbeddingCache::filePath(m.folder)));
    QCOMPARE(ed->retranscribeImpact().manualCorrections(), 0);
}

void SpeakerEditorTest::overlay_fingerprintMismatchDropsEdits()
{
    Fixture fx;
    {
        auto ed = fx.editor();
        ed->moveUtterancesToPerson(uids({4}), QStringLiteral("Cili"));
    }
    // Az átirat kicserélődik az overlay „háta mögött" (egy sorral rövidebb).
    QFile f(speakeredit::segmentsPath(fx.meeting.folder));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonArray segs = QJsonDocument::fromJson(f.readAll()).array();
    f.close();
    segs.removeLast();
    writeJson(speakeredit::segmentsPath(fx.meeting.folder), QJsonDocument(segs));

    auto ed = fx.editor();
    QCOMPARE(ed->speakers().size(), 2);                 // a régi javítások nem alkalmazódnak
    QCOMPARE(ed->utterance(uid(4)).speakerKey, kB1);
    QVERIFY(!ed->utterance(uid(4)).manuallyCorrected);
}

void SpeakerEditorTest::applyResolvedSpeakers_tokens()
{
    Fixture fx;
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Béla")}});
    auto ed = fx.editor();
    const QString cili = ed->moveUtterancesToPerson(uids({4}), QStringLiteral("Cili"));
    Q_UNUSED(cili);

    MergedTranscript mt;
    auto tok = [&](qint64 s, qint64 e, const QString& spk) {
        TranscriptToken t; t.startMs = s; t.endMs = e; t.speaker = spk; t.text = QStringLiteral("x");
        mt.tokens << t;
    };
    tok(0, 2000, kB1);          // 0. sor → Anna (speakerMap)
    tok(24000, 26000, kB1);     // 4. sor → Cili (soronkénti felülírás)
    tok(6000, 8000, kB2);       // 1. sor → Béla
    tok(500000, 501000, kB1);   // sorhoz nem köthető → beszélő-szint (Anna)
    speakeredit::applyResolvedSpeakers(mt, fx.store->load(fx.meeting.id));
    QCOMPARE(mt.tokens[0].speaker, QStringLiteral("Anna"));
    QCOMPARE(mt.tokens[1].speaker, QStringLiteral("Cili"));
    QCOMPARE(mt.tokens[2].speaker, QStringLiteral("Béla"));
    QCOMPARE(mt.tokens[3].speaker, QStringLiteral("Anna"));

    // Overlay nélkül: pontosan a régi speakerMap-alkalmazás.
    Fixture plain;
    plain.setSpeakerMap({{kB1, QStringLiteral("Anna")}});
    MergedTranscript mt2;
    TranscriptToken t; t.speaker = kB1; mt2.tokens << t;
    t.speaker = kB2; mt2.tokens << t;
    speakeredit::applyResolvedSpeakers(mt2, plain.store->load(plain.meeting.id));
    QCOMPARE(mt2.tokens[0].speaker, QStringLiteral("Anna"));
    QCOMPARE(mt2.tokens[1].speaker, kB2);
}

void SpeakerEditorTest::externalChange_refresh()
{
    Fixture fx;
    auto ed = fx.editor();
    const QString cili = ed->moveUtterancesToPerson(uids({4}), QStringLiteral("Cili"));
    QVERIFY(ed->canUndo());

    // A régi UI / auto-azonosítás elnevezi a nyers címkét: a kézi javítás megmarad,
    // és az undo-verem is (az overlay nem változott).
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}});
    speakeredit::recordIdentification(fx.meeting.folder, kB1, QStringLiteral("Anna"), 0.71);
    QSignalSpy speakers(ed.get(), &SpeakerEditor::speakersChanged);
    ed->refreshFromDisk();
    QVERIFY(speakers.count() >= 1);
    QCOMPARE(ed->speaker(kB1).personName, QStringLiteral("Anna"));
    QCOMPARE(ed->speaker(kB1).voiceConfidence, 0.71);
    QCOMPARE(ed->utterance(uid(4)).speakerKey, cili);
    QVERIFY(ed->canUndo());

    // Az undo csak a saját lépését vonja vissza — a külső elnevezést nem írja felül.
    ed->undo();
    QCOMPARE(fx.diskMap().value(kB1), QStringLiteral("Anna"));
    QCOMPARE(ed->utterance(uid(4)).speakerKey, kB1);
    ed->redo();

    // Globális személy-átnevezés az overlay-t írja → teljes újratöltés, verem eldobva.
    QVERIFY(speakeredit::renamePersonInOverlay(fx.meeting.folder, QStringLiteral("Cili"),
                                               QStringLiteral("Cecília")));
    QSignalSpy reloaded(ed.get(), &SpeakerEditor::reloaded);
    ed->refreshFromDisk();
    QCOMPARE(reloaded.count(), 1);
    QCOMPARE(ed->speaker(cili).personName, QStringLiteral("Cecília"));
    QVERIFY(!ed->canUndo());

    // Személy törlése: a résztvevő névtelenné válik, a sorai megmaradnak.
    QVERIFY(speakeredit::removePersonFromOverlay(fx.meeting.folder, QStringLiteral("Cecília")));
    ed->refreshFromDisk();
    QVERIFY(ed->speaker(cili).anonymous);
    QCOMPARE(ed->speaker(cili).utteranceCount, 1);
}

void SpeakerEditorTest::fineGrainedSignals()
{
    Fixture fx;
    auto ed = fx.editor();
    QSignalSpy rows(ed.get(), &SpeakerEditor::utterancesChanged);
    QSignalSpy speakers(ed.get(), &SpeakerEditor::speakersChanged);
    QSignalSpy undo(ed.get(), &SpeakerEditor::undoStateChanged);
    QSignalSpy map(ed.get(), &SpeakerEditor::speakerMapChanged);
    QSignalSpy ppl(ed.get(), &SpeakerEditor::peopleChanged);

    QVERIFY(ed->moveUtterances(uids({4, 9}), kB2));
    QCOMPARE(rows.count(), 1);
    QCOMPARE(rows.last().at(0).toStringList(), uids({4, 9}));   // csak az érintett sorok
    QCOMPARE(speakers.count(), 1);
    QCOMPARE(undo.count(), 1);
    QCOMPARE(map.count(), 0);                                   // a speakerMap nem változott

    QVERIFY(ed->reassignSpeaker(kB1, QStringLiteral("Anna")));
    QCOMPARE(rows.count(), 1);                                  // sor-adat nem változott
    QCOMPARE(speakers.count(), 2);
    QCOMPARE(map.count(), 1);
    QCOMPARE(map.last().at(0).toString(), fx.meeting.id);
    QCOMPARE(ppl.count(), 1);

    ed->undo();
    ed->undo();
    QCOMPARE(rows.count(), 2);
    QCOMPARE(rows.last().at(0).toStringList(), uids({4, 9}));
    QCOMPARE(undo.count(), 4);
}

void SpeakerEditorTest::people_directoryAndSearch()
{
    QCOMPARE(foldForSearch(QStringLiteral("Ödön ŐRÚT")), QStringLiteral("odon orut"));
    QVERIFY(matchesSearch(QStringLiteral("Szűcs Ödön"), QStringLiteral("odon")));
    QVERIFY(matchesSearch(QStringLiteral("Szűcs Ödön"), QStringLiteral("SZUCS")));
    QVERIFY(matchesSearch(QStringLiteral("Kovács Lilla"), QStringLiteral("ács l")));
    QVERIFY(matchesSearch(QStringLiteral("odon"), QStringLiteral("Ödön")));
    QVERIFY(matchesSearch(QStringLiteral("Bármi"), QStringLiteral("  ")));
    QVERIFY(!matchesSearch(QStringLiteral("Szűcs Ödön"), QStringLiteral("edon")));

    Fixture fx;
    fx.people->add(QStringLiteral("Anna"));
    fx.people->add(QStringLiteral("Béla"));
    fx.people->add(QStringLiteral("Ödön"));
    Voiceprint vp; vp.embedding = {1, 0, 0};
    fx.prints->addPrint(QStringLiteral("anna"), vp);        // kisbetű-független egyesítés
    fx.prints->addPrint(QStringLiteral("Zita"), vp);        // csak lenyomata van
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Anna")}});
    {
        auto ed = fx.editor();
        ed->moveUtterancesToPerson(uids({4}), QStringLiteral("Ödön"));
    }
    Meeting second = fx.store->createMeeting(QStringLiteral("Másik"));
    second.speakerMap.insert(kB1, QStringLiteral("Anna"));
    fx.store->saveMeeting(second);

    const QVector<PersonInfo> all = listPeople(fx.people.get(), fx.prints.get(), fx.store.get());
    QCOMPARE(all.size(), 4);
    auto find = [&](const QString& n) {
        for (const PersonInfo& p : all) if (p.name == n) return p;
        return PersonInfo();
    };
    QCOMPARE(find(QStringLiteral("Anna")).meetingCount, 2);     // meetingenként egyszer számít
    QVERIFY(find(QStringLiteral("Anna")).hasVoiceprint);
    QCOMPARE(find(QStringLiteral("Béla")).meetingCount, 0);
    QVERIFY(!find(QStringLiteral("Béla")).hasVoiceprint);
    QCOMPARE(find(QStringLiteral("Ödön")).meetingCount, 1);     // kézzel felvett résztvevőként
    QVERIFY(find(QStringLiteral("Zita")).hasVoiceprint);

    const QVector<PersonInfo> hits = filterPeople(all, QStringLiteral("odo"));
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().name, QStringLiteral("Ödön"));
    QCOMPARE(filterPeople(all, QString()).size(), 4);
    // Névkezdetre illő előbb, mint a belső egyezés.
    const QVector<PersonInfo> a = filterPeople(all, QStringLiteral("a"));
    QCOMPARE(a.first().name, QStringLiteral("Anna"));
    QCOMPARE(a.size(), 3);                                      // Anna, Béla, Zita
}

void SpeakerEditorTest::analysis_pureFunctions()
{
    using namespace speakeredit;
    const QVector<float> A{1, 0}, B{0, 1};
    QVector<AnalysisLine> lines;
    auto add = [&](const QVector<float>* e, int spk, qint64 dur, bool locked = false) {
        AnalysisLine l; l.embedding = e; l.speaker = spk; l.durationMs = dur; l.locked = locked;
        lines.append(l);
    };
    for (int i = 0; i < 5; ++i) add(&A, 0, 4000);
    for (int i = 0; i < 5; ++i) add(&B, 1, 4000);
    add(&B, 0, 4000);               // 10: rossz helyen
    add(&B, 0, 4000, true);         // 11: rossz helyen, de zárolt
    add(nullptr, 0, 900);           // 12: nincs embedding

    const QVector<bool> unc = computeUncertain(lines, 2);
    for (int i = 0; i < 10; ++i) QVERIFY(!unc[i]);
    QVERIFY(unc[10]);
    QVERIFY(!unc[11]);
    QVERIFY(!unc[12]);

    const QVector<LineFit> fits = computeFits(lines, 2);
    QVERIFY(fits[0].own > 0.85);
    QVERIFY(fits[10].other > 0.99);
    QCOMPARE(fits[10].otherSpeaker, 1);
    QVERIFY(qIsNaN(fits[12].own));

    // Kevés minta a beszélőről → nem ítélünk.
    QVector<AnalysisLine> few;
    AnalysisLine x; x.embedding = &A; x.speaker = 0; x.durationMs = 4000;
    few << x << x;
    QVERIFY(!computeUncertain(few, 1)[0]);

    // Javaslat: a 0-s beszélőnél maradt B-hangú sor az 1-eshez húz; a zárolt nem ajánlott.
    QCOMPARE(suggestSimilar(lines, 0, 1), QVector<int>{10});
    QVERIFY(suggestSimilar(lines, 0, 0).isEmpty());
    QVERIFY(suggestSimilar(lines, 1, 0).isEmpty());
}

QTEST_GUILESS_MAIN(SpeakerEditorTest)
#include "test_speaker_editor.moc"

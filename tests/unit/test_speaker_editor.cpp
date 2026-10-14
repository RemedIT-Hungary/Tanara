// SpeakerEditor / SpeakerOverlay / PeopleDirectory unit tests, in temporary folders, with a
// FAKE embedder (the "voice" is derived from the utterance's start time). It uses no real
// data, model or ffmpeg.
//
// The synthetic meeting: diarization gave two labels ("Speaker 1", "Speaker 2"), but
// "Speaker 1" is really TWO people (A = Anna, C = Cili), exactly the case the editor must be
// able to split. "Speaker 2" = B = Béla; one of its lines (the 17th) was wrongly attached
// (really A's voice).
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/media/FfmpegMediaTool.h"
#include "tanara/domain/Types.h"
#include "tanara/editing/session/PeopleDirectory.h"
#include "tanara/editing/review/SpeakerAnalysis.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"

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
        {19000,   800, kB2, 'B'},   // 3  short
        {22000,  4000, kB1, 'C'},   // 4
        {28000,  6000, kB2, 'B'},   // 5
        {36000,  4000, kB1, 'A'},   // 6
        {42000,  5000, kB1, 'C'},   // 7
        {49000,  4000, kB2, 'B'},   // 8
        {55000,  3500, kB1, 'C'},   // 9
        {60500,  4500, kB1, 'A'},   // 10
        {67000,  5000, kB2, 'B'},   // 11
        {74000,  1000, kB1, 'C'},   // 12 short
        {77000,  4000, kB1, 'A'},   // 13
        {83000,  6000, kB1, 'C'},   // 14
        {91000,  5000, kB2, 'B'},   // 15
        {98000,  4000, kB1, 'A'},   // 16
        {104000, 4000, kB2, 'A'},   // 17 wrongly on label 2
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

// Fake embedder: from the middle of the slice it says whose voice it is (orthogonal vectors).
struct FakeWorld {
    std::atomic_int embedCalls{0};
    std::atomic_int opens{0};
    std::atomic_bool openOk{true};
    std::atomic_int delayMs{0};
};

class FakeEmbedder : public processing::IUtteranceEmbedder {
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

// A multi-model fake embedder: "x" gives the same as FakeEmbedder, "y" and "z" give vectors
// of other dimensions, but also orthogonal per speaker. It logs every request.
struct MultiWorld {
    std::atomic_int opens{0};
    std::atomic_int calls{0};
    QStringList seen;                  // the requested model lists (only the background thread writes)
};

class MultiFakeEmbedder : public processing::IUtteranceEmbedder {
public:
    explicit MultiFakeEmbedder(std::shared_ptr<MultiWorld> w) : m_world(std::move(w)) {}
    bool open(const QString&) override { ++m_world->opens; return true; }
    QVector<float> embed(qint64 s, qint64 e) override { return embedAll(s, e, {"x"}).value("x"); }
    EmbeddingSet embedAll(qint64 startMs, qint64 endMs, const QStringList& modelIds) override
    {
        ++m_world->calls;
        m_world->seen << modelIds.join(QLatin1Char(','));
        const qint64 mid = (startMs + endMs) / 2;
        int voice = -1;
        for (const Row& r : scenario())
            if (mid >= r.startMs && mid <= r.startMs + r.durMs) voice = r.voice == 'A' ? 0 : r.voice == 'B' ? 1 : 2;
        EmbeddingSet out;
        for (const QString& id : modelIds) {
            if (voice < 0) { out.insert(id, {}); continue; }
            const int dim = id == QLatin1String("x") ? 3 : 4;
            QVector<float> v(dim, 0.0f);
            v[id == QLatin1String("z") ? (voice + 1) % dim : voice] = 1.0f;
            out.insert(id, v);
        }
        return out;
    }
private:
    std::shared_ptr<MultiWorld> m_world;
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

// A test environment: temporary audio/metadata folders, stores, a transcribed meeting.
struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<library::MeetingRepository> store;
    std::unique_ptr<library::PeopleStore> people;
    std::unique_ptr<library::VoiceprintStore> prints;
    std::shared_ptr<FakeWorld> world = std::make_shared<FakeWorld>();
    Meeting meeting;

    Fixture()
    {
        store = std::make_unique<library::MeetingRepository>(dir.filePath(QStringLiteral("rec")),
                                               dir.filePath(QStringLiteral("meta")));
        people = std::make_unique<library::PeopleStore>(dir.filePath(QStringLiteral("meta/people.json")));
        prints = std::make_unique<library::VoiceprintStore>(dir.filePath(QStringLiteral("meta/vp.json")));
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

    // A person by name (people.json; created if missing) and back.
    PersonId pid(const QString& name) { return people->ensure(name); }
    QString nameOf(const PersonId& id) const { return people->displayName(id); }
    PersonChoice choice(const QString& name) const { return people->choiceFor(name); }

    // raw label -> person NAME (the people are created in people.json).
    void setSpeakerMap(const QMap<QString, QString>& map, bool hasSummary = false)
    {
        meeting = store->load(meeting.id);
        meeting.speakerMap.clear();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) meeting.speakerMap.insert(it.key(), pid(it.value()));
        meeting.hasSummary = hasSummary;
        store->saveMeeting(meeting);
    }

    std::unique_ptr<editing::SpeakerEditor> editor(bool withEmbedder = true)
    {
        auto ed = std::make_unique<editing::SpeakerEditor>(store.get(), people.get(), prints.get(), meeting.id);
        if (withEmbedder) {
            auto w = world;
            ed->setEmbedderFactory([w] { return std::make_unique<FakeEmbedder>(w); });
        }
        return ed;
    }

    // Computing the embeddings (waiting for the background thread).
    static bool embed(editing::SpeakerEditor& ed)
    {
        QSignalSpy done(&ed, &editing::SpeakerEditor::embeddingFinished);
        ed.startEmbedding();
        if (done.isEmpty() && !done.wait(10000)) return false;
        return done.last().at(0).toBool();
    }

    QString markdown() const
    {
        return readText(QDir(meeting.folder).filePath(QStringLiteral("transcript.md")));
    }
    // The speakerMap on disk with the people's NAMES (raw label -> name).
    QMap<QString, QString> diskMap() const
    {
        QMap<QString, QString> out;
        const QMap<QString, PersonId> m = store->load(meeting.id).speakerMap;
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) out.insert(it.key(), nameOf(it.value()));
        return out;
    }
    // The person with this name (no creation); null if none.
    PersonId idOf(const QString& name) const { people->refresh(); return people->idForName(name); }
    bool hasPerson(const QString& name) const { return !idOf(name).isNull(); }
};

editing::EditorSpeaker findSpeaker(const editing::SpeakerEditor& ed, const QString& key) { return ed.speaker(key); }

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
    void embedding_multiModel_oneDecodeFusedPrints();
    void voiceprint_explicitOnly();
    void voiceprint_fromSingleLine();
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
    add(1000, 2500, kB2);   // the same start time (overlapping speakers)
    add(3000, 4000, kB1);
    writeJson(dir.filePath(QStringLiteral("transcript.segments.json")), QJsonDocument(segs));

    const QVector<TranscriptLine> a = editing::speakeredit::loadTranscriptLines(dir.path());
    QCOMPARE(a.size(), 3);
    QCOMPARE(a[0].id, QStringLiteral("u1000"));
    QCOMPARE(a[1].id, QStringLiteral("u1000-2"));
    QCOMPARE(a[2].id, QStringLiteral("u3000"));
    // Read again: the same ids and the same fingerprint.
    const QVector<TranscriptLine> b = editing::speakeredit::loadTranscriptLines(dir.path());
    for (int i = 0; i < a.size(); ++i) QCOMPARE(a[i].id, b[i].id);
    QCOMPARE(editing::speakeredit::transcriptFingerprint(a), editing::speakeredit::transcriptFingerprint(b));
}

void SpeakerEditorTest::readModel_speakersAndUtterances()
{
    Fixture fx;
    fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
    Voiceprint vp; vp.embedding = {0, 1, 0};
    fx.prints->addPrint(fx.pid(QStringLiteral("Béla")), vp);
    editing::speakeredit::recordIdentification(fx.meeting.folder, kB2, fx.pid(QStringLiteral("Béla")), 0.82);

    auto ed = fx.editor();
    ed->setSelfPerson(SelfPerson{fx.pid(QStringLiteral("Béla")), QStringLiteral("Béla")});
    QVERIFY(ed->hasTranscript());
    QCOMPARE(ed->utteranceCount(), scenario().size());

    const editing::EditorUtterance u4 = ed->utterance(uid(4));
    QCOMPARE(u4.index, 4);
    QCOMPARE(u4.startMs, 22000);
    QCOMPARE(u4.endMs, 26000);
    QCOMPARE(u4.rawLabel, kB1);
    QCOMPARE(u4.speakerKey, kB1);
    QCOMPARE(u4.text, QStringLiteral("L4a L4b"));
    QVERIFY(!u4.uncertain && !u4.manuallyCorrected && !u4.confirmed);
    QCOMPARE(ed->indexOf(uid(4)), 4);
    QCOMPARE(ed->utterance(QStringLiteral("nincs")).index, -1);

    const QVector<editing::EditorSpeaker> sp = ed->speakers();
    QCOMPARE(sp.size(), 2);
    QCOMPARE(sp[0].key, kB1);
    QCOMPARE(sp[0].displayName, kB1);       // anonymous: the raw label
    QVERIFY(sp[0].anonymous);
    QCOMPARE(sp[0].colorIndex, 0);
    QCOMPARE(sp[0].utteranceCount, 11);
    QVERIFY(!sp[0].hasVoiceprint);
    QVERIFY(sp[0].voiceConfidence < 0);

    QCOMPARE(sp[1].key, kB2);
    QCOMPARE(sp[1].displayName, QStringLiteral("Béla"));
    QCOMPARE(fx.nameOf(sp[1].personId), QStringLiteral("Béla"));
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
    // Old meeting: no transcript.speakers.json, so everything resolves from the raw state.
    QVERIFY(!QFile::exists(editing::speakeredit::overlayPath(fx.meeting.folder)));
    auto ed = fx.editor();
    QCOMPARE(ed->speakers().size(), 2);
    QVERIFY(!ed->canUndo());
    // Reading does not create an overlay file.
    QVERIFY(!QFile::exists(editing::speakeredit::overlayPath(fx.meeting.folder)));

    // A meeting without a transcript: an empty model, the operations are no-ops.
    const Meeting empty = fx.store->createMeeting(QStringLiteral("Üres"));
    editing::SpeakerEditor none(fx.store.get(), fx.people.get(), fx.prints.get(), empty.id);
    QVERIFY(!none.hasTranscript());
    QCOMPARE(none.utteranceCount(), 0);
    QVERIFY(none.speakers().isEmpty());
    QVERIFY(!none.moveUtterances({QStringLiteral("u0")}, kB1));
    QVERIFY(none.moveUtterancesToPerson({QStringLiteral("u0")}, fx.choice(QStringLiteral("X"))).isEmpty());

    // An unknown meeting does not crash either.
    editing::SpeakerEditor unknown(fx.store.get(), nullptr, nullptr, QStringLiteral("nincs-ilyen"));
    QVERIFY(!unknown.hasTranscript());
    QVERIFY(unknown.addParticipant(fx.choice(QStringLiteral("X"))).isEmpty());
}

void SpeakerEditorTest::move_undoRedo_persist()
{
    Fixture fx;
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Béla")}});
    auto ed = fx.editor();

    QVERIFY(!ed->moveUtterances(uids({4}), kB1));               // already there: no step
    QVERIFY(!ed->moveUtterances(uids({4}), QStringLiteral("nincs")));
    QVERIFY(!ed->canUndo());

    QVERIFY(ed->moveUtterances(uids({4, 7}), kB2));
    QCOMPARE(ed->utterance(uid(4)).speakerKey, kB2);
    QVERIFY(ed->utterance(uid(4)).manuallyCorrected);
    QCOMPARE(ed->utterance(uid(4)).rawLabel, kB1);              // the raw label is untouched
    QCOMPARE(ed->speaker(kB1).utteranceCount, 9);
    QCOMPARE(ed->speaker(kB2).utteranceCount, 9);
    QVERIFY(ed->canUndo());
    QVERIFY(!ed->canRedo());
    QVERIFY(ed->undoText().contains(QStringLiteral("Béla")));
    QVERIFY(ed->undoText().startsWith(QStringLiteral("2 ")));

    // transcript.md was refreshed with the resolved names; segments.json stays raw.
    ed->flushPendingWrites();   // transcript.md is written with a delay
    QVERIFY(fx.markdown().contains(QStringLiteral("**Béla** L4a L4b")));
    QVERIFY(fx.markdown().contains(QStringLiteral("**Anna** L0a L0b")));
    QCOMPARE(editing::speakeredit::loadTranscriptLines(fx.meeting.folder)[4].rawLabel, kB1);

    // Persistent: a new session sees the same (without an undo stack).
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
    ed->flushPendingWrites();   // transcript.md is written with a delay
    QVERIFY(fx.markdown().contains(QStringLiteral("**Anna** L4a L4b")));
    QVERIFY(!QFile::exists(editing::speakeredit::overlayPath(fx.meeting.folder)));   // empty overlay: no file

    ed->redo();
    QCOMPARE(ed->utterance(uid(7)).speakerKey, kB2);
    QVERIFY(ed->canUndo());
    QVERIFY(!ed->canRedo());

    // A new operation clears the redo stack; every operation is exactly one step.
    ed->undo();
    QVERIFY(ed->moveUtterances(uids({9}), kB2));
    QVERIFY(!ed->canRedo());
    ed->undo();
    QVERIFY(!ed->canUndo());
}

void SpeakerEditorTest::move_toNewPerson_andAnonymous()
{
    Fixture fx;
    fx.pid(QStringLiteral("Anna"));
    auto ed = fx.editor();

    // A brand-new person: a participant is created, it also goes onto the name list; NO voiceprint is made.
    const QString cili = ed->moveUtterancesToPerson(uids({4, 7}), fx.choice(QStringLiteral("  Cili ")));
    QVERIFY(editing::speakeredit::isParticipantKey(cili));
    editing::EditorSpeaker s = ed->speaker(cili);
    QCOMPARE(s.displayName, QStringLiteral("Cili"));
    QCOMPARE(fx.nameOf(s.personId), QStringLiteral("Cili"));
    QVERIFY(s.added);
    QCOMPARE(s.utteranceCount, 2);
    QCOMPARE(s.colorIndex, 2);                  // after the two raw labels
    QVERIFY(fx.hasPerson(QStringLiteral("Cili")));
    QCOMPARE(fx.prints->totalPrintCount(), 0);
    ed->flushPendingWrites();   // transcript.md is written with a delay
    QVERIFY(fx.markdown().contains(QStringLiteral("**Cili** L4a L4b")));

    // To the same person again: it goes to the existing speaker, no second column.
    QCOMPARE(ed->moveUtterancesToPerson(uids({9}), fx.choice(QStringLiteral("cili"))), cili);
    QCOMPARE(ed->speakers().size(), 3);
    QCOMPARE(ed->speaker(cili).utteranceCount, 3);

    // A new anonymous participant.
    const QString anon = ed->moveUtterancesToNewParticipant(uids({14}));
    QVERIFY(editing::speakeredit::isParticipantKey(anon));
    QVERIFY(anon != cili);
    QVERIFY(ed->speaker(anon).anonymous);
    QCOMPARE(ed->speaker(anon).displayName, QStringLiteral("Új beszélő 1"));
    QCOMPARE(ed->speaker(anon).colorIndex, 3);
    QCOMPARE(ed->speaker(cili).colorIndex, 2);  // the color does not change from the edit

    // Undo x3: everything is restored, the newly added person leaves the name list too.
    ed->undo(); ed->undo(); ed->undo();
    QVERIFY(!ed->canUndo());
    QCOMPARE(ed->speakers().size(), 2);
    QVERIFY(!fx.hasPerson(QStringLiteral("Cili")));
    QVERIFY(fx.hasPerson(QStringLiteral("Anna")));
    ed->redo();
    QVERIFY(fx.hasPerson(QStringLiteral("Cili")));
    QCOMPARE(ed->speaker(cili).utteranceCount, 2);
}

void SpeakerEditorTest::reassign_keepsSpeakerMapSemantics()
{
    Fixture fx;
    auto ed = fx.editor();

    // Naming a raw speaker = the old speakerMap entry (the CLI reads this).
    QVERIFY(ed->reassignSpeaker(kB1, fx.choice(QStringLiteral("Anna"))));
    QCOMPARE(fx.diskMap().value(kB1), QStringLiteral("Anna"));
    QCOMPARE(ed->speaker(kB1).displayName, QStringLiteral("Anna"));
    QVERIFY(fx.hasPerson(QStringLiteral("Anna")));
    ed->flushPendingWrites();   // transcript.md is written with a delay
    QVERIFY(fx.markdown().contains(QStringLiteral("**Anna** L0a L0b")));
    QCOMPARE(fx.prints->totalPrintCount(), 0);      // no automatic teaching
    QVERIFY(!ed->reassignSpeaker(kB1, fx.choice(QStringLiteral("anna"))));     // the same: no step
    QVERIFY(!ed->reassignSpeaker(kB1, fx.choice(QStringLiteral("  "))));

    ed->undo();
    QVERIFY(!fx.diskMap().contains(kB1));
    QVERIFY(ed->speaker(kB1).anonymous);
    ed->flushPendingWrites();   // transcript.md is written with a delay
    QVERIFY(fx.markdown().contains(QStringLiteral("**Beszélő 1** L0a L0b")));
    ed->redo();
    QCOMPARE(fx.diskMap().value(kB1), QStringLiteral("Anna"));

    // Renaming a manually added participant lives in the overlay (the speakerMap does not change).
    const QString p = ed->moveUtterancesToNewParticipant(uids({4}));
    QVERIFY(ed->reassignSpeaker(p, fx.choice(QStringLiteral("Cili"))));
    QCOMPARE(fx.nameOf(ed->speaker(p).personId), QStringLiteral("Cili"));
    QCOMPARE(fx.diskMap().size(), 1);
}

void SpeakerEditorTest::reassign_toExistingPerson_merges()
{
    Fixture fx;
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}});
    auto ed = fx.editor();
    // We assign label 2 to Anna too, so there are not two Anna columns: a merge.
    QVERIFY(ed->reassignSpeaker(kB2, fx.choice(QStringLiteral("Anna"))));
    QCOMPARE(ed->speakers().size(), 1);
    QCOMPARE(ed->speaker(kB1).utteranceCount, scenario().size());
    QCOMPARE(ed->utterance(uid(1)).speakerKey, kB1);
    QCOMPARE(fx.diskMap().value(kB2), QStringLiteral("Anna"));  // the old UI sees Anna too
    ed->undo();
    QCOMPARE(ed->speakers().size(), 2);
    QVERIFY(!fx.diskMap().contains(kB2));
}

void SpeakerEditorTest::reassign_fixVoiceprints_undo()
{
    Fixture fx;
    fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
    // Béla's voiceprints: one FROM this meeting (from a line of label 2), one from elsewhere.
    Voiceprint here; here.embedding = {0, 1, 0};
    here.sourceMeetingId = fx.meeting.id;
    here.sampleRef = QStringLiteral("mixdown.mp3#28000-34000");
    here.id = QStringLiteral("itt");
    Voiceprint other; other.embedding = {0, 1, 0};
    other.sourceMeetingId = QStringLiteral("masik-meeting");
    other.id = QStringLiteral("mashol");
    fx.prints->addPrint(fx.pid(QStringLiteral("Béla")), here);
    fx.prints->addPrint(fx.pid(QStringLiteral("Béla")), other);

    auto ed = fx.editor();
    QVERIFY(Fixture::embed(*ed));

    // "Wrong recognition": label 2 is really Dóra.
    QVERIFY(ed->reassignSpeaker(kB2, fx.choice(QStringLiteral("Dóra")), /*fixVoiceprints*/ true));
    QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Béla"))), 1);
    QCOMPARE(fx.prints->printsFor(fx.idOf(QStringLiteral("Béla"))).first().id, QStringLiteral("mashol"));
    QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Dóra"))), 1);
    const Voiceprint made = fx.prints->printsFor(fx.idOf(QStringLiteral("Dóra"))).first();
    QCOMPARE(made.sourceMeetingId, fx.meeting.id);
    QVERIFY(library::VoiceprintStore::cosineSimilarity(made.embedding, {0, 1, 0}) > 0.99);
    QVERIFY(ed->speaker(kB2).hasVoiceprint);

    // Undo also reverts the voiceprint side effects ...
    ed->undo();
    QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Béla"))), 2);
    QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Dóra"))), 0);
    QCOMPARE(fx.diskMap().value(kB2), QStringLiteral("Béla"));
    // ... and redo applies them again (with the same voiceprint).
    ed->redo();
    QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Béla"))), 1);
    QCOMPARE(fx.prints->printsFor(fx.idOf(QStringLiteral("Dóra"))).first().id, made.id);
    // On disk too (the store persists).
    library::VoiceprintStore reread(fx.prints->filePath());
    QCOMPARE(reread.printCount(fx.idOf(QStringLiteral("Dóra"))), 1);

    // Without the marker it does not touch the voiceprints.
    ed->undo();
    QVERIFY(ed->reassignSpeaker(kB2, fx.choice(QStringLiteral("Dóra")), false));
    QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Béla"))), 2);
    QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Dóra"))), 0);
}

void SpeakerEditorTest::revertToAnonymous()
{
    Fixture fx;
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}});
    auto ed = fx.editor();
    QVERIFY(!ed->revertSpeakerToAnonymous(kB2));        // already anonymous
    QVERIFY(ed->revertSpeakerToAnonymous(kB1));
    QVERIFY(ed->speaker(kB1).anonymous);
    QCOMPARE(ed->speaker(kB1).displayName, kB1);
    QVERIFY(!fx.diskMap().contains(kB1));
    ed->undo();
    QCOMPARE(fx.nameOf(ed->speaker(kB1).personId), QStringLiteral("Anna"));

    // A named participant that is made anonymous gets a label.
    const QString p = ed->moveUtterancesToPerson(uids({4}), fx.choice(QStringLiteral("Cili")));
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
    const QString cili = ed->moveUtterancesToPerson(uids({4, 7}), fx.choice(QStringLiteral("Cili")));

    QVERIFY(!ed->mergeSpeakers(kB1, kB1));
    QVERIFY(!ed->mergeSpeakers(kB1, QStringLiteral("nincs")));

    // Merging a raw label into another: toward the old UI the target's person shows.
    QVERIFY(ed->mergeSpeakers(kB1, kB2));
    QCOMPARE(ed->speakers().size(), 2);                 // Béla + Cili
    QVERIFY(ed->speaker(kB1).key.isEmpty());
    QCOMPARE(ed->utterance(uid(0)).speakerKey, kB2);
    QCOMPARE(ed->utterance(uid(4)).speakerKey, cili);   // the manually moved line stays
    QVERIFY(!ed->utterance(uid(0)).manuallyCorrected);  // a whole-speaker operation is not "fixed"
    QCOMPARE(fx.diskMap().value(kB1), QStringLiteral("Béla"));
    ed->flushPendingWrites();   // transcript.md is written with a delay
    QVERIFY(fx.markdown().contains(QStringLiteral("**Béla** L0a L0b")));
    QVERIFY(!ed->moveUtterances(uids({1}), kB1));       // a vanished speaker is not a target

    // Merging a participant into a raw speaker: the participant disappears.
    QVERIFY(ed->mergeSpeakers(cili, kB2));
    QCOMPARE(ed->speakers().size(), 1);
    QCOMPARE(ed->speaker(kB2).utteranceCount, scenario().size());

    // Renaming the target carries the merged label too (speakerMap compatibility).
    QVERIFY(ed->reassignSpeaker(kB2, fx.choice(QStringLiteral("Dóra"))));
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

    // A participant with no lines (one the system did not separate).
    const QString dora = ed->addParticipant(fx.choice(QStringLiteral("Dóra")));
    QVERIFY(!dora.isEmpty());
    QCOMPARE(ed->speaker(dora).utteranceCount, 0);
    QCOMPARE(ed->speakers().size(), 3);
    QCOMPARE(ed->addParticipant(fx.choice(QStringLiteral("Béla"))), kB2);     // already a speaker: no new one
    QCOMPARE(ed->speakers().size(), 3);
    const QString anon = ed->addParticipant();
    QCOMPARE(ed->speaker(anon).displayName, QStringLiteral("Új beszélő 1"));

    // Persistent: in a new session the participants with no lines are there too.
    QCOMPARE(fx.editor()->speakers().size(), 4);

    // Only an empty speaker can be removed.
    QVERIFY(ed->moveUtterances(uids({4}), dora));
    QVERIFY(!ed->removeParticipant(dora));
    QVERIFY(!ed->removeParticipant(kB1));
    QVERIFY(ed->removeParticipant(anon));
    QCOMPARE(ed->speakers().size(), 3);
    ed->undo();
    QCOMPARE(ed->speakers().size(), 4);
    QCOMPARE(ed->speaker(anon).displayName, QStringLiteral("Új beszélő 1"));

    // An emptied raw speaker can be removed too (and comes back with undo).
    QStringList all;
    for (const editing::EditorUtterance& u : ed->utterances())
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
    QCOMPARE(ed->uncertainCount(), 0);              // without embeddings nothing is uncertain
    QVERIFY(Fixture::embed(*ed));

    // Line 17 is on label 2, but it is A's voice, so it resembles another speaker more.
    QCOMPARE(ed->uncertainUtteranceIds(), uids({17}));
    QVERIFY(ed->utterance(uid(17)).uncertain);
    QVERIFY(!ed->utterance(uid(1)).uncertain);

    // "Fine as is": once confirmed it is no longer uncertain; one undo step.
    QSignalSpy count(ed.get(), &editing::SpeakerEditor::uncertainCountChanged);
    QVERIFY(ed->confirmUtterances(uids({17})));
    QVERIFY(ed->utterance(uid(17)).confirmed);
    QVERIFY(!ed->utterance(uid(17)).uncertain);
    QCOMPARE(ed->uncertainCount(), 0);
    QCOMPARE(count.last().at(0).toInt(), 0);
    QVERIFY(!ed->confirmUtterances(uids({17})));    // already confirmed
    ed->undo();
    QVERIFY(ed->utterance(uid(17)).uncertain);

    // Placed by hand, it is not uncertain either (and "fixed").
    QVERIFY(ed->moveUtterances(uids({17}), kB1));
    QVERIFY(!ed->utterance(uid(17)).uncertain);
    QVERIFY(ed->utterance(uid(17)).manuallyCorrected);
    QCOMPARE(ed->uncertainCount(), 0);
}

// Recheck: "Speaker 1" is two people (A, C). If we confirm three of A's lines, the core is
// only A's voice, so C's lines (they do not fit their own either) and the A line stuck on label 2 are doubtful.
void SpeakerEditorTest::recheck_fromConfirmedLines_splitsMixedSpeaker()
{
    Fixture fx;
    auto ed = fx.editor();
    QVERIFY(Fixture::embed(*ed));
    QCOMPARE(ed->uncertainUtteranceIds(), uids({17}));
    QVERIFY(!ed->canRecheck());

    QVERIFY(ed->confirmUtterances(uids({0, 2, 6})));
    QVERIFY(ed->canRecheck());
    const editing::SpeakerEditor::RecheckResult r = ed->recheckFromConfirmed();
    QCOMPARE(r.flagged, 5);
    QCOMPARE(r.speakersWithConfirmedCore, 1);
    QCOMPARE(r.confirmedLines, 3);
    QCOMPARE(ed->uncertainUtteranceIds(), uids({4, 7, 9, 14, 17}));
    // For C's lines there is no suggestion (they fit no other either), for line 17 the voice is A's, so Speaker 1.
    QCOMPARE(ed->utterance(uid(4)).likelySpeakerKey, QString());
    QCOMPARE(ed->utterance(uid(17)).likelySpeakerKey, kB1);
    QVERIFY(ed->utterance(uid(4)).rechecked);
    QVERIFY(!ed->utterance(uid(12)).uncertain);       // short: never doubtful

    // The flag survives a regular recompute (another embedding run, a reload).
    QVERIFY(Fixture::embed(*ed));
    QCOMPARE(ed->uncertainCount(), 5);
    ed.reset();
    auto again = fx.editor();
    QCOMPARE(again->uncertainUtteranceIds(), uids({4, 7, 9, 14, 17}));

    // Placing C's lines under a new participant clears the flag (fixed).
    QVERIFY(!again->moveUtterancesToNewParticipant(uids({4, 7, 9, 14})).isEmpty());
    QCOMPARE(again->uncertainUtteranceIds(), uids({17}));
}

void SpeakerEditorTest::shortLines_neverUncertain()
{
    Fixture fx;
    auto ed = fx.editor();
    QVERIFY(Fixture::embed(*ed));
    // For the short (3rd, 12th) lines no embedding is made, so they do not flood the list.
    const int longLines = int(std::count_if(scenario().cbegin(), scenario().cend(),
        [](const Row& r) { return r.durMs >= editing::speakeredit::kMinEmbedMs; }));
    QCOMPARE(fx.world->embedCalls.load(), longLines);
    // Placed in the wrong place they do not become uncertain either (manual work stays).
    QVERIFY(ed->mergeSpeakers(kB2, kB1));
    QVERIFY(!ed->utterance(uid(3)).uncertain);
    QVERIFY(!ed->utterance(uid(12)).uncertain);
}

void SpeakerEditorTest::suggestion_afterMove_acceptIsOneStep()
{
    Fixture fx;
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Béla")}});
    auto ed = fx.editor();

    // Without embeddings there is no suggestion (but the operation works).
    QVERIFY(!ed->moveUtterancesToPerson(uids({4}), fx.choice(QStringLiteral("Cili"))).isEmpty());
    QVERIFY(!ed->hasSuggestion());
    ed->undo();

    QVERIFY(Fixture::embed(*ed));
    QSignalSpy sugg(ed.get(), &editing::SpeakerEditor::suggestionChanged);
    const QString cili = ed->moveUtterancesToPerson(uids({4}), fx.choice(QStringLiteral("Cili")));
    QVERIFY(ed->hasSuggestion());
    QCOMPARE(sugg.count(), 1);
    const editing::SpeakerSuggestion sg = ed->suggestion();
    QCOMPARE(sg.targetSpeaker, cili);
    QCOMPARE(sg.sourceSpeakerKey, kB1);
    QCOMPARE(sg.anchorUtteranceId, uid(4));
    // The other C-voiced lines of label 1: the short 12th is left out, A's lines do not get in.
    QCOMPARE(sg.utteranceIds, uids({7, 9, 14}));

    // Accepting = ONE undo step.
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

    // Dismiss: the suggestion disappears, without a step.
    ed->redo();
    QVERIFY(!ed->hasSuggestion());                  // redo does not offer again
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

    // Cancel midway: the partial result stays, the resume computes only the missing ones.
    fx.world->delayMs = 15;
    QSignalSpy progress(ed.get(), &editing::SpeakerEditor::embeddingProgress);
    QSignalSpy finished(ed.get(), &editing::SpeakerEditor::embeddingFinished);
    QSignalSpy running(ed.get(), &editing::SpeakerEditor::embeddingRunningChanged);
    ed->startEmbedding();
    QVERIFY(ed->isEmbeddingRunning());
    ed->startEmbedding();                           // a no-op while running
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
    QCOMPARE(fx.world->embedCalls.load(), longLines);   // nothing was computed twice
    QVERIFY(!progress.isEmpty());
    QCOMPARE(progress.last().at(0).toInt(), progress.last().at(1).toInt());
    QCOMPARE(fx.world->opens.load(), 2);                // ONE decode per run

    // The cache lives in the meeting folder: a new session is ready without computing.
    QVERIFY(QFile::exists(processing::UtteranceEmbeddingCache::filePath(fx.meeting.folder)));
    auto ed2 = fx.editor();
    QVERIFY(ed2->embeddingsComplete());
    QCOMPARE(ed2->uncertainCount(), 1);
    QVERIFY(Fixture::embed(*ed2));
    QCOMPARE(fx.world->embedCalls.load(), longLines);

    // It can be destroyed safely with a running thread.
    processing::UtteranceEmbeddingCache::remove(fx.meeting.folder);
    fx.world->delayMs = 15;
    auto ed3 = fx.editor();
    ed3->startEmbedding();
    ed3.reset();
}

void SpeakerEditorTest::embedding_gracefulWithoutModel()
{
    Fixture fx;
    // No factory (no model / a build without voice-ID).
    auto plain = fx.editor(/*withEmbedder*/ false);
    QVERIFY(!plain->embeddingsSupported());
    QSignalSpy fin(plain.get(), &editing::SpeakerEditor::embeddingFinished);
    plain->startEmbedding();
    QCOMPARE(fin.count(), 1);
    QCOMPARE(fin.first().at(0).toBool(), false);
    QVERIFY(plain->moveUtterances(uids({4}), kB2));     // editing works this way too
    QCOMPARE(plain->uncertainCount(), 0);
    plain->undo();

    // There is a factory, but the audio / model cannot be opened.
    fx.world->openOk = false;
    auto ed = fx.editor();
    QVERIFY(!Fixture::embed(*ed));
    QVERIFY(!ed->embeddingsComplete());
    QCOMPARE(ed->uncertainCount(), 0);
    QVERIFY(!ed->isEmbeddingRunning());

    // The real factory with a missing model: open() false, no crash.
    auto real = processing::voiceUtteranceEmbedderFactory(QStringLiteral("/nincs/ilyen/modell.onnx"), media::FfmpegMediaTool::fromSettings({}))();
    QVERIFY(real);
    QVERIFY(!real->open(QStringLiteral("/nincs/ilyen/hang.mp3")));
    QVERIFY(real->embed(0, 3000).isEmpty());
}

// A manual sample from a single, explicitly chosen line: the 15 s minimum does not apply,
// but an uncertain / short line and another speaker's line cannot be accepted.
void SpeakerEditorTest::voiceprint_fromSingleLine()
{
    Fixture fx;
    auto ed = fx.editor();
    const QString cili = ed->moveUtterancesToPerson(uids({14}), fx.choice(QStringLiteral("Cili")));  // 6 s line
    QCOMPARE(fx.prints->totalPrintCount(), 0);

    // Not their line: no sample.
    editing::VoiceprintResult r = ed->createVoiceprintFromLines(cili, uids({4}));
    QVERIFY(!r.ok);

    // Their single long line is enough.
    QSignalSpy vpSig(ed.get(), &editing::SpeakerEditor::voiceprintsChanged);
    r = ed->createVoiceprintFromLines(cili, uids({14}));
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.usedLines, 1);
    QCOMPARE(vpSig.count(), 1);
    QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Cili"))), 1);
    QCOMPARE(fx.prints->printsFor(fx.idOf(QStringLiteral("Cili"))).first().sampleRef,
             QStringLiteral("mixdown.mp3#83000-89000"));

    // A short line: an understandable error, no sample is made.
    const QString dani = ed->moveUtterancesToPerson(uids({3}), fx.choice(QStringLiteral("Dani")));   // 0,8 mp
    r = ed->createVoiceprintFromLines(dani, uids({3}));
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains(QStringLiteral("rövid")));
    QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Dani"))), 0);
}

// Several models: one background run = one open() (one decode) for all models; the cache
// stores per model; the analysis with the fusion vector gives the same as with one model; the
// manual voiceprint is a separate Voiceprint per model, and undo takes them all. When a new
// model is switched on, only it is missing, and only it is computed.
void SpeakerEditorTest::embedding_multiModel_oneDecodeFusedPrints()
{
    Fixture fx;
    auto world = std::make_shared<MultiWorld>();
    auto ed = std::make_unique<editing::SpeakerEditor>(fx.store.get(), fx.people.get(), fx.prints.get(), fx.meeting.id);
    QCOMPARE(ed->voiceModelIds(), QStringList{"campplus"});
    ed->setEmbedderFactory([world] { return std::make_unique<MultiFakeEmbedder>(world); });
    ed->setVoiceModelIds({"y", "x", "x"});
    QCOMPARE(ed->voiceModelIds(), QStringList({"x", "y"}));

    QVERIFY(Fixture::embed(*ed));
    QCOMPARE(world->opens.load(), 1);
    QVERIFY(ed->embeddingsComplete());
    for (const QString& m : std::as_const(world->seen)) QCOMPARE(m, QStringLiteral("x,y"));
    QCOMPARE(ed->uncertainUtteranceIds(), uids({17}));

    // The cache per model on disk (v2).
    const processing::UtteranceEmbeddingCache cache = processing::UtteranceEmbeddingCache::load(
        fx.meeting.folder,
        editing::speakeredit::transcriptFingerprint(editing::speakeredit::loadTranscriptLines(fx.meeting.folder)));
    QCOMPARE(cache.models.keys(), QStringList({"x", "y"}));
    QCOMPARE(cache.vectors("x").value(uid(0)), (QVector<float>{1, 0, 0}));
    QCOMPARE(cache.vectors("y").value(uid(0)), (QVector<float>{1, 0, 0, 0}));

    // Manual voiceprint: one per model, with a shared sample; undo takes both.
    const QString cili = ed->moveUtterancesToPerson(uids({4, 7, 9, 14}), fx.choice(QStringLiteral("Cili")));
    const editing::VoiceprintResult r = ed->createVoiceprint(cili);
    QVERIFY2(r.ok, qPrintable(r.error));
    const QVector<Voiceprint> made = fx.prints->printsFor(fx.idOf(QStringLiteral("Cili")));
    QCOMPARE(made.size(), 2);
    QCOMPARE(made[0].model, QStringLiteral("x"));
    QCOMPARE(made[1].model, QStringLiteral("y"));
    QCOMPARE(made[0].sampleRef, made[1].sampleRef);
    QCOMPARE(made[0].createdAt, made[1].createdAt);
    QVERIFY(made[0].id != made[1].id);
    QCOMPARE(made[0].dim, 3);
    QCOMPARE(made[1].dim, 4);
    QVERIFY(ed->removeVoiceprint(r.printId));
    QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Cili"))), 0);

    // New model: until it has a vector there is no fusion vector (no audio-based uncertainty);
    // the next run computes only "z", again with a single open().
    ed->setVoiceModelIds({"x", "y", "z"});
    QVERIFY(!ed->embeddingsComplete());
    QCOMPARE(ed->uncertainCount(), 0);
    world->seen.clear();
    QVERIFY(Fixture::embed(*ed));
    QCOMPARE(world->opens.load(), 2);
    QVERIFY(!world->seen.isEmpty());
    for (const QString& m : std::as_const(world->seen)) QCOMPARE(m, QStringLiteral("z"));
    QVERIFY(ed->embeddingsComplete());
    QVERIFY(ed->uncertainCount() > 0);
}

void SpeakerEditorTest::voiceprint_explicitOnly()
{
    Fixture fx;
    auto ed = fx.editor();

    // None is made for an anonymous speaker.
    editing::VoiceprintResult r = ed->createVoiceprint(kB2);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());

    // Little material: it says how much is missing, and makes no weak voiceprint.
    const QString cili = ed->moveUtterancesToPerson(uids({4}), fx.choice(QStringLiteral("Cili")));
    editing::VoiceprintMaterial mat = ed->voiceprintMaterial(cili);
    QVERIFY(!mat.sufficient);
    QCOMPARE(mat.usableLines, 1);
    QCOMPARE(mat.usableMs, 4000);
    QCOMPARE(mat.missingMs, editing::speakeredit::kPrintMinTotalMs - 4000);
    r = ed->createVoiceprint(cili);
    QVERIFY(!r.ok);
    QCOMPARE(r.missingMs, editing::speakeredit::kPrintMinTotalMs - 4000);
    QVERIFY(r.error.contains(QStringLiteral("11")));
    QCOMPARE(fx.prints->totalPrintCount(), 0);

    // With enough material: from the longer lines, even without the cache (it computes now).
    QVERIFY(ed->moveUtterances(uids({7, 9, 14}), cili));
    mat = ed->voiceprintMaterial(cili);
    QVERIFY(mat.sufficient);
    QCOMPARE(mat.usableMs, 4000 + 5000 + 3500 + 6000);
    QCOMPARE(fx.prints->totalPrintCount(), 0);          // reassigning does not teach by itself
    QSignalSpy vpSig(ed.get(), &editing::SpeakerEditor::voiceprintsChanged);
    r = ed->createVoiceprint(cili);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.usedLines, 4);
    QCOMPARE(vpSig.count(), 1);
    QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Cili"))), 1);
    const Voiceprint vp = fx.prints->printsFor(fx.idOf(QStringLiteral("Cili"))).first();
    QCOMPARE(vp.id, r.printId);
    QCOMPARE(vp.sourceMeetingId, fx.meeting.id);
    QCOMPARE(vp.sampleRef, QStringLiteral("mixdown.mp3#83000-89000"));  // the longest line
    QVERIFY(library::VoiceprintStore::cosineSimilarity(vp.embedding, {0, 0, 1}) > 0.99);
    QVERIFY(ed->speaker(cili).hasVoiceprint);

    // Without a model: an understandable error.
    fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
    auto plain = fx.editor(/*withEmbedder*/ false);
    // Undoing the manual creation: exactly that voiceprint is deleted, the indicator goes back.
    {
        const editing::VoiceprintResult again = ed->createVoiceprint(cili);
        QVERIFY(again.ok && !again.printId.isEmpty());
        const int before = fx.prints->printCount(fx.idOf(QStringLiteral("Cili")));
        QSignalSpy speakersSpy(ed.get(), &editing::SpeakerEditor::speakersChanged);
        QVERIFY(ed->removeVoiceprint(again.printId));
        QCOMPARE(fx.prints->printCount(fx.idOf(QStringLiteral("Cili"))), before - 1);
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
    // Without a summary there is nothing to mark stale.
    {
        auto ed = fx.editor();
        QVERIFY(ed->moveUtterances(uids({4}), kB2));
        QVERIFY(!ed->summaryStale().stale);
        ed->undo();
    }

    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}}, /*hasSummary*/ true);
    auto ed = fx.editor();
    QSignalSpy stale(ed.get(), &editing::SpeakerEditor::summaryStaleChanged);
    QVERIFY(!ed->summaryStale().stale);

    const QString cili = ed->moveUtterancesToPerson(uids({4}), fx.choice(QStringLiteral("Cili")));
    QVERIFY(ed->summaryStale().stale);
    QCOMPARE(ed->summaryStale().correctedSpeakers, 1);
    QCOMPARE(stale.count(), 1);
    QCOMPARE(stale.last().at(0).toBool(), true);
    QVERIFY(ed->moveUtterances(uids({7}), cili));               // the same speaker: still 1
    QCOMPARE(ed->summaryStale().correctedSpeakers, 1);
    QVERIFY(ed->reassignSpeaker(kB2, fx.choice(QStringLiteral("Béla"))));
    QCOMPARE(ed->summaryStale().correctedSpeakers, 2);
    QVERIFY(ed->confirmUtterances(uids({1})));                  // a confirmation is not a speaker change
    QCOMPARE(ed->summaryStale().correctedSpeakers, 2);

    // Readable without an open editor too (library icon).
    QCOMPARE(editing::speakeredit::summaryStale(fx.store->load(fx.meeting.id)).correctedSpeakers, 2);

    // Undone to the state at the summary: not stale.
    ed->undo(); ed->undo(); ed->undo(); ed->undo();
    QVERIFY(!ed->summaryStale().stale);
    ed->redo(); ed->redo();
    QCOMPARE(ed->summaryStale().correctedSpeakers, 1);

    // "Fine as is".
    ed->dismissSummaryStale();
    QVERIFY(!ed->summaryStale().stale);
    QCOMPARE(stale.last().at(0).toBool(), false);
    QVERIFY(!editing::speakeredit::summaryStale(fx.store->load(fx.meeting.id)).stale);
    // An undo AFTER accepting is a change again relative to the summary.
    ed->undo();
    QVERIFY(ed->summaryStale().stale);

    // A new summary was made: cleared; at file level too (without an editor).
    ed->notifySummaryRegenerated();
    QVERIFY(!ed->summaryStale().stale);
    ed.reset();
    const Meeting m = fx.store->load(fx.meeting.id);
    QVERIFY(editing::speakeredit::markSummaryStale(m, {kB1}));           // the old UI's rename hook
    QVERIFY(!editing::speakeredit::markSummaryStale(m, {kB1}));
    QVERIFY(editing::speakeredit::summaryStale(m).stale);
    QVERIFY(editing::speakeredit::clearSummaryStale(m.folder));
    QVERIFY(!editing::speakeredit::summaryStale(m).stale);
}

void SpeakerEditorTest::retranscribe_impactBackupDiscard()
{
    Fixture fx;
    fx.setSpeakerMap({{kB2, QStringLiteral("Béla")}});
    auto ed = fx.editor();
    QVERIFY(!ed->retranscribeImpact().manualCorrections());

    const QString cili = ed->moveUtterancesToPerson(uids({4, 7}), fx.choice(QStringLiteral("Cili")));
    QVERIFY(ed->confirmUtterances(uids({1, 4})));       // the 4th is already fixed: it does not count twice
    QVERIFY(Fixture::embed(*ed));
    editing::RetranscribeImpact imp = ed->retranscribeImpact();
    QCOMPARE(imp.correctedUtterances, 2);
    QCOMPARE(imp.confirmedUtterances, 1);
    QCOMPARE(imp.manualCorrections(), 3);
    QCOMPARE(imp.addedParticipants, 1);
    QCOMPARE(imp.namedSpeakers, 2);
    // The same at file level (without an open editor).
    const Meeting m = fx.store->load(fx.meeting.id);
    const editing::RetranscribeImpact disk = editing::speakeredit::retranscribeImpact(m);
    QCOMPARE(disk.manualCorrections(), 3);
    QCOMPARE(disk.namedSpeakers, 2);

    // A copy of the current transcript.
    const QString backup = editing::speakeredit::backupTranscript(m, fx.people->names());
    QVERIFY(!backup.isEmpty());
    QVERIFY(backup.startsWith(m.folder));
    for (const char* f : {"transcript.md", "transcript.tokens.json", "transcript.segments.json",
                          "transcript.speakers.json", "speakerMap.json"})
        QVERIFY2(QFile::exists(QDir(backup).filePath(QLatin1String(f))), f);
    QVERIFY(readText(QDir(backup).filePath(QStringLiteral("transcript.md")))
                .contains(QStringLiteral("**Cili** L4a L4b")));
    QVERIFY(editing::speakeredit::backupTranscript(m, fx.people->names()) != backup);    // a second copy does not overwrite

    // A new transcript was made: the manual fixes and the embedding cache dropped.
    editing::speakeredit::discardForNewTranscript(m.folder);
    QSignalSpy reloaded(ed.get(), &editing::SpeakerEditor::reloaded);
    ed->reloadTranscript();
    QCOMPARE(reloaded.count(), 1);
    QCOMPARE(ed->speakers().size(), 2);
    QVERIFY(!ed->canUndo());
    QVERIFY(!ed->embeddingsComplete());
    QVERIFY(!QFile::exists(processing::UtteranceEmbeddingCache::filePath(m.folder)));
    QCOMPARE(ed->retranscribeImpact().manualCorrections(), 0);
}

void SpeakerEditorTest::overlay_fingerprintMismatchDropsEdits()
{
    Fixture fx;
    {
        auto ed = fx.editor();
        ed->moveUtterancesToPerson(uids({4}), fx.choice(QStringLiteral("Cili")));
    }
    // The transcript is replaced behind the overlay's back (one line shorter).
    QFile f(editing::speakeredit::segmentsPath(fx.meeting.folder));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonArray segs = QJsonDocument::fromJson(f.readAll()).array();
    f.close();
    segs.removeLast();
    writeJson(editing::speakeredit::segmentsPath(fx.meeting.folder), QJsonDocument(segs));

    auto ed = fx.editor();
    QCOMPARE(ed->speakers().size(), 2);                 // the old fixes are not applied
    QCOMPARE(ed->utterance(uid(4)).speakerKey, kB1);
    QVERIFY(!ed->utterance(uid(4)).manuallyCorrected);
}

void SpeakerEditorTest::applyResolvedSpeakers_tokens()
{
    Fixture fx;
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Béla")}});
    auto ed = fx.editor();
    const QString cili = ed->moveUtterancesToPerson(uids({4}), fx.choice(QStringLiteral("Cili")));
    Q_UNUSED(cili);

    MergedTranscript mt;
    auto tok = [&](qint64 s, qint64 e, const QString& spk) {
        TranscriptToken t; t.startMs = s; t.endMs = e; t.speaker = spk; t.text = QStringLiteral("x");
        mt.tokens << t;
    };
    tok(0, 2000, kB1);          // line 0: Anna (speakerMap)
    tok(24000, 26000, kB1);     // line 4: Cili (per-line override)
    tok(6000, 8000, kB2);       // line 1: Béla
    tok(500000, 501000, kB1);   // cannot be tied to a line: speaker level (Anna)
    editing::speakeredit::applyResolvedSpeakers(mt, fx.store->load(fx.meeting.id), fx.people->names());
    QCOMPARE(mt.tokens[0].speaker, QStringLiteral("Anna"));
    QCOMPARE(mt.tokens[1].speaker, QStringLiteral("Cili"));
    QCOMPARE(mt.tokens[2].speaker, QStringLiteral("Béla"));
    QCOMPARE(mt.tokens[3].speaker, QStringLiteral("Anna"));

    // Without an overlay: exactly the old speakerMap application.
    Fixture plain;
    plain.setSpeakerMap({{kB1, QStringLiteral("Anna")}});
    MergedTranscript mt2;
    TranscriptToken t; t.speaker = kB1; mt2.tokens << t;
    t.speaker = kB2; mt2.tokens << t;
    editing::speakeredit::applyResolvedSpeakers(mt2, plain.store->load(plain.meeting.id), plain.people->names());
    QCOMPARE(mt2.tokens[0].speaker, QStringLiteral("Anna"));
    QCOMPARE(mt2.tokens[1].speaker, kB2);
}

void SpeakerEditorTest::externalChange_refresh()
{
    Fixture fx;
    auto ed = fx.editor();
    const QString cili = ed->moveUtterancesToPerson(uids({4}), fx.choice(QStringLiteral("Cili")));
    QVERIFY(ed->canUndo());

    // The old UI / auto-identification names the raw label: the manual fix stays, and so does
    // the undo stack (the overlay did not change).
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}});
    editing::speakeredit::recordIdentification(fx.meeting.folder, kB1, fx.pid(QStringLiteral("Anna")), 0.71);
    QSignalSpy speakers(ed.get(), &editing::SpeakerEditor::speakersChanged);
    ed->refreshFromDisk();
    QVERIFY(speakers.count() >= 1);
    QCOMPARE(fx.nameOf(ed->speaker(kB1).personId), QStringLiteral("Anna"));
    QCOMPARE(ed->speaker(kB1).voiceConfidence, 0.71);
    QCOMPARE(ed->utterance(uid(4)).speakerKey, cili);
    QVERIFY(ed->canUndo());

    // Undo reverts only its own step; it does not overwrite the external naming.
    ed->undo();
    QCOMPARE(fx.diskMap().value(kB1), QStringLiteral("Anna"));
    QCOMPARE(ed->utterance(uid(4)).speakerKey, kB1);
    ed->redo();

    // A global person rename is ONE field (people.json): the overlay is untouched, the editor shows the new
    // name after a refresh and keeps its undo stack.
    const PersonId ciliId = fx.idOf(QStringLiteral("Cili"));
    QVERIFY(fx.people->rename(ciliId, QStringLiteral("Cecília")));
    ed->refreshFromDisk();
    QCOMPARE(ed->speaker(cili).personId, ciliId);
    QCOMPARE(ed->speaker(cili).displayName, QStringLiteral("Cecília"));
    QVERIFY(ed->canUndo());

    // A global merge writes the overlay: a full reload, the stack dropped.
    const PersonId dora = fx.pid(QStringLiteral("Dóra"));
    QVERIFY(!editing::speakeredit::replacePersonInOverlay(fx.meeting.folder, ciliId, dora).isEmpty());
    QSignalSpy reloaded(ed.get(), &editing::SpeakerEditor::reloaded);
    ed->refreshFromDisk();
    QCOMPARE(reloaded.count(), 1);
    QCOMPARE(ed->speaker(cili).personId, dora);
    QVERIFY(!ed->canUndo());

    // Deleting a person: the participant becomes anonymous, its lines stay.
    QVERIFY(editing::speakeredit::removePersonFromOverlay(fx.meeting.folder, dora));
    ed->refreshFromDisk();
    QVERIFY(ed->speaker(cili).anonymous);
    QCOMPARE(ed->speaker(cili).utteranceCount, 1);
}

void SpeakerEditorTest::fineGrainedSignals()
{
    Fixture fx;
    auto ed = fx.editor();
    QSignalSpy rows(ed.get(), &editing::SpeakerEditor::utterancesChanged);
    QSignalSpy speakers(ed.get(), &editing::SpeakerEditor::speakersChanged);
    QSignalSpy undo(ed.get(), &editing::SpeakerEditor::undoStateChanged);
    QSignalSpy map(ed.get(), &editing::SpeakerEditor::speakerMapChanged);
    QSignalSpy ppl(ed.get(), &editing::SpeakerEditor::peopleChanged);

    QVERIFY(ed->moveUtterances(uids({4, 9}), kB2));
    QCOMPARE(rows.count(), 1);
    QCOMPARE(rows.last().at(0).toStringList(), uids({4, 9}));   // only the affected lines
    QCOMPARE(speakers.count(), 1);
    QCOMPARE(undo.count(), 1);
    QCOMPARE(map.count(), 0);                                   // the speakerMap did not change

    QVERIFY(ed->reassignSpeaker(kB1, fx.choice(QStringLiteral("Anna"))));
    QCOMPARE(rows.count(), 1);                                  // the line data did not change
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
    QCOMPARE(text::foldForSearch(QStringLiteral("Ödön ŐRÚT")), QStringLiteral("odon orut"));
    QVERIFY(editing::matchesSearch(QStringLiteral("Szűcs Ödön"), QStringLiteral("odon")));
    QVERIFY(editing::matchesSearch(QStringLiteral("Szűcs Ödön"), QStringLiteral("SZUCS")));
    QVERIFY(editing::matchesSearch(QStringLiteral("Kovács Lilla"), QStringLiteral("ács l")));
    QVERIFY(editing::matchesSearch(QStringLiteral("odon"), QStringLiteral("Ödön")));
    QVERIFY(editing::matchesSearch(QStringLiteral("Bármi"), QStringLiteral("  ")));
    QVERIFY(!editing::matchesSearch(QStringLiteral("Szűcs Ödön"), QStringLiteral("edon")));

    Fixture fx;
    fx.pid(QStringLiteral("Anna"));
    fx.pid(QStringLiteral("Béla"));
    fx.pid(QStringLiteral("Ödön"));
    Voiceprint vp; vp.embedding = {1, 0, 0};
    fx.prints->addPrint(fx.pid(QStringLiteral("anna")), vp);        // the name lookup is case-insensitive
    fx.prints->addPrint(fx.pid(QStringLiteral("Zita")), vp);        // a voiceprint, no meeting
    fx.setSpeakerMap({{kB1, QStringLiteral("Anna")}, {kB2, QStringLiteral("Anna")}});
    {
        auto ed = fx.editor();
        ed->moveUtterancesToPerson(uids({4}), fx.choice(QStringLiteral("Ödön")));
    }
    Meeting second = fx.store->createMeeting(QStringLiteral("Másik"));
    second.speakerMap.insert(kB1, fx.pid(QStringLiteral("Anna")));
    fx.store->saveMeeting(second);

    const QVector<editing::PersonInfo> all = editing::listPeople(fx.people.get(), fx.prints.get(), fx.store.get());
    QCOMPARE(all.size(), 4);
    auto find = [&](const QString& n) {
        for (const editing::PersonInfo& p : all) if (p.name == n) return p;
        return editing::PersonInfo();
    };
    QCOMPARE(find(QStringLiteral("Anna")).meetingCount, 2);     // counts once per meeting
    QVERIFY(find(QStringLiteral("Anna")).hasVoiceprint);
    QCOMPARE(find(QStringLiteral("Béla")).meetingCount, 0);
    QVERIFY(!find(QStringLiteral("Béla")).hasVoiceprint);
    QCOMPARE(find(QStringLiteral("Ödön")).meetingCount, 1);     // as a manually added participant
    QVERIFY(find(QStringLiteral("Zita")).hasVoiceprint);

    const QVector<editing::PersonInfo> hits = filterPeople(all, QStringLiteral("odo"));
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().name, QStringLiteral("Ödön"));
    QCOMPARE(filterPeople(all, QString()).size(), 4);
    // A name-start match comes before an inner match.
    const QVector<editing::PersonInfo> a = filterPeople(all, QStringLiteral("a"));
    QCOMPARE(a.first().name, QStringLiteral("Anna"));
    QCOMPARE(a.size(), 3);                                      // Anna, Béla, Zita
}

void SpeakerEditorTest::analysis_pureFunctions()
{
    using namespace editing::speakeredit;
    const QVector<float> A{1, 0}, B{0, 1};
    QVector<editing::speakeredit::AnalysisLine> lines;
    auto add = [&](const QVector<float>* e, int spk, qint64 dur, bool locked = false) {
        editing::speakeredit::AnalysisLine l; l.embedding = e; l.speaker = spk; l.durationMs = dur; l.locked = locked;
        lines.append(l);
    };
    for (int i = 0; i < 5; ++i) add(&A, 0, 4000);
    for (int i = 0; i < 5; ++i) add(&B, 1, 4000);
    add(&B, 0, 4000);               // 10: in the wrong place
    add(&B, 0, 4000, true);         // 11: in the wrong place, but locked
    add(nullptr, 0, 900);           // 12: no embedding

    const QVector<bool> unc = editing::speakeredit::computeUncertain(lines, 2);
    for (int i = 0; i < 10; ++i) QVERIFY(!unc[i]);
    QVERIFY(unc[10]);
    QVERIFY(!unc[11]);
    QVERIFY(!unc[12]);

    const QVector<editing::speakeredit::LineFit> fits = editing::speakeredit::computeFits(lines, 2);
    QVERIFY(fits[0].own > 0.85);
    QVERIFY(fits[10].other > 0.99);
    QCOMPARE(fits[10].otherSpeaker, 1);
    QVERIFY(qIsNaN(fits[12].own));

    // Little sample of the speaker: we do not judge.
    QVector<editing::speakeredit::AnalysisLine> few;
    editing::speakeredit::AnalysisLine x; x.embedding = &A; x.speaker = 0; x.durationMs = 4000;
    few << x << x;
    QVERIFY(!editing::speakeredit::computeUncertain(few, 1)[0]);

    // Suggestion: the B-voiced line left under speaker 0 pulls toward 1; the locked one is not suggested.
    QCOMPARE(editing::speakeredit::suggestSimilar(lines, 0, 1), QVector<int>{10});
    QVERIFY(editing::speakeredit::suggestSimilar(lines, 0, 0).isEmpty());
    QVERIFY(editing::speakeredit::suggestSimilar(lines, 1, 0).isEmpty());
}

QTEST_GUILESS_MAIN(SpeakerEditorTest)
#include "test_speaker_editor.moc"

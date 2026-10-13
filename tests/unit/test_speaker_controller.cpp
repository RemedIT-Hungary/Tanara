// Wiring the transcript editor into AppController: session management, coexistence with the
// old (speakerMap-based) paths, the summary's stale flag and the new-transcript hook. Runs with
// an ISOLATED HOME (~/.tanara and ~/Tanara go under temp); it touches no real data,
// transcription or summary.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/store/MeetingStore.h"

#include <memory>

using namespace tanara;

namespace {
const QString kB1 = QStringLiteral("Beszélő 1");
const QString kB2 = QStringLiteral("Beszélő 2");

QString readText(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}
} // namespace

class SpeakerControllerTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void sessionLifecycle();
    void editorAndLegacyRenameCoexist();
    void summaryReady_clearsStale();
    void transcriptReady_discardsOverlay();
    void globalPersonRename_reachesParticipants();
    void peopleAndImpact();

private:
    Meeting makeMeeting(const QString& title, bool hasSummary);
    QString markdown(const Meeting& m) const
    {
        return readText(QDir(m.folder).filePath(QStringLiteral("transcript.md")));
    }

    QTemporaryDir m_home;
    std::unique_ptr<AppController> m_app;
};

void SpeakerControllerTest::initTestCase()
{
    QVERIFY(m_home.isValid());
    // Isolated HOME: settings, the people / voiceprint DB and the recordings are all under temp.
    // On Windows HOME does not redirect QDir::homePath(), so TANARA_HOME points under temp too
    // (on every platform; it overrides an external TANARA_HOME as well, so the guard rail holds).
    qputenv("HOME", m_home.path().toUtf8());
    qputenv("TANARA_HOME", m_home.filePath(QStringLiteral(".tanara")).toUtf8());
    qputenv("TANARA_CLOUD", "off");
    QDir().mkpath(m_home.filePath(QStringLiteral(".tanara")));
    m_app = std::make_unique<AppController>();
    // Guard rail: if the isolation does not hold for any reason, the test does not go on.
    QVERIFY2(m_app->store()->audioDir().startsWith(m_home.path()),
             qPrintable(m_app->store()->audioDir()));
    QVERIFY2(m_app->store()->metadataDir().startsWith(m_home.path()),
             qPrintable(m_app->store()->metadataDir()));
}

void SpeakerControllerTest::cleanupTestCase() { m_app.reset(); }

// Four lines: 0 and 2 on label 1, 1 and 3 on label 2.
Meeting SpeakerControllerTest::makeMeeting(const QString& title, bool hasSummary)
{
    Meeting m = m_app->store()->createMeeting(title);
    QJsonArray toks, segs;
    for (int i = 0; i < 4; ++i) {
        const QString raw = (i % 2 == 0) ? kB1 : kB2;
        const qint64 start = i * 6000, end = start + 4000;
        QJsonObject t;
        t["text"] = QStringLiteral("S%1").arg(i); t["speaker"] = raw;
        t["startMs"] = double(start); t["endMs"] = double(end);
        t["confidence"] = 1.0; t["trackId"] = QStringLiteral("mixdown");
        toks.append(t);
        QJsonObject s;
        s["startMs"] = double(start); s["endMs"] = double(end);
        s["speaker"] = raw; s["text"] = QStringLiteral("S%1").arg(i);
        segs.append(s);
    }
    QJsonObject root;
    root["language"] = QStringLiteral("hu");
    root["tokens"] = toks;
    QFile tf(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
    if (tf.open(QIODevice::WriteOnly)) tf.write(QJsonDocument(root).toJson());
    QFile sf(QDir(m.folder).filePath(QStringLiteral("transcript.segments.json")));
    if (sf.open(QIODevice::WriteOnly)) sf.write(QJsonDocument(segs).toJson());
    m.hasTranscript = true;
    m.hasSummary = hasSummary;
    m_app->store()->saveMeeting(m);
    return m;
}

void SpeakerControllerTest::sessionLifecycle()
{
    const Meeting m = makeMeeting(QStringLiteral("Munkamenet"), false);
    QVERIFY(m_app->speakerEditor(QStringLiteral("nincs-ilyen")) == nullptr);

    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QVERIFY(ed);
    QCOMPARE(m_app->speakerEditor(m.id), ed);       // one session per meeting
    QCOMPARE(ed->utteranceCount(), 4);
    QVERIFY(!ed->embeddingsSupported());            // the isolated HOME has no model, so it degrades

    QVERIFY(ed->moveUtterances({QStringLiteral("u0")}, kB2));
    QVERIFY(ed->canUndo());
    // Closing: the undo stack is dropped, the fix stays on disk.
    m_app->closeSpeakerEditor(m.id);
    SpeakerEditor* again = m_app->speakerEditor(m.id);
    QVERIFY(again != nullptr);
    QVERIFY(!again->canUndo());
    QCOMPARE(again->utterance(QStringLiteral("u0")).speakerKey, kB2);
    m_app->closeSpeakerEditor(m.id);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void SpeakerControllerTest::editorAndLegacyRenameCoexist()
{
    const Meeting m = makeMeeting(QStringLiteral("Együttélés"), true);
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QSignalSpy mapSig(m_app.get(), &AppController::speakerMapChanged);
    QSignalSpy staleSig(m_app.get(), &AppController::summaryStaleChanged);
    QSignalSpy peopleSig(m_app.get(), &AppController::peopleChanged);

    // The editor's operations go out on the usual signals too.
    const QString cili = ed->moveUtterancesToPerson({QStringLiteral("u12000")}, QStringLiteral("Cili"));
    QVERIFY(!cili.isEmpty());
    QCOMPARE(peopleSig.count(), 1);
    QVERIFY(m_app->knownPeople().contains(QStringLiteral("Cili")));
    QVERIFY(staleSig.count() >= 1);
    QVERIFY(m_app->summaryStale(m.id).stale);
    QCOMPARE(m_app->summaryStale(m.id).correctedSpeakers, 1);
    QVERIFY(ed->reassignSpeaker(kB1, QStringLiteral("Anna")));
    QCOMPARE(mapSig.count(), 1);
    QCOMPARE(m_app->store()->load(m.id).speakerMap.value(kB1), QStringLiteral("Anna"));

    // The OLD path (CLI): renaming a raw label. The editor takes it over, the per-line fix
    // stays, and transcript.md reflects both.
    QSignalSpy speakers(ed, &SpeakerEditor::speakersChanged);
    m_app->renameSpeaker(m.id, kB2, QStringLiteral("Béla"), /*enroll*/ false);
    QVERIFY(speakers.count() >= 1);
    QCOMPARE(ed->speaker(kB2).personName, QStringLiteral("Béla"));
    QCOMPARE(ed->utterance(QStringLiteral("u12000")).speakerKey, cili);
    QVERIFY(ed->canUndo());
    ed->flushPendingWrites();   // transcript.md is written with a delay
    const QString md = markdown(m);
    QVERIFY2(md.contains(QStringLiteral("**Anna** S0")), qPrintable(md));
    QVERIFY(md.contains(QStringLiteral("**Béla** S1")));
    QVERIFY(md.contains(QStringLiteral("**Cili** S2")));
    QVERIFY(md.contains(QStringLiteral("**Béla** S3")));
    QCOMPARE(m_app->summaryStale(m.id).correctedSpeakers, 3);   // Cili, Anna, Béla

    // "Fine as is".
    m_app->dismissSummaryStale(m.id);
    QVERIFY(!m_app->summaryStale(m.id).stale);
    m_app->closeSpeakerEditor(m.id);
}

void SpeakerControllerTest::summaryReady_clearsStale()
{
    const Meeting m = makeMeeting(QStringLiteral("Elavult"), true);
    // WITHOUT an open editor: the old rename makes the summary stale too.
    QSignalSpy staleSig(m_app.get(), &AppController::summaryStaleChanged);
    m_app->renameSpeaker(m.id, kB1, QStringLiteral("Anna"), false);
    QCOMPARE(staleSig.count(), 1);
    QVERIFY(m_app->summaryStale(m.id).stale);
    // A new summary was made (the signal is emitted by AppController at the end of generation).
    emit m_app->summaryReady(m.id, QDir(m.folder).filePath(QStringLiteral("summary.md")));
    QVERIFY(!m_app->summaryStale(m.id).stale);

    // The same with an open editor.
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QVERIFY(ed->moveUtterances({QStringLiteral("u0")}, kB2));
    QVERIFY(ed->summaryStale().stale);
    emit m_app->summaryReady(m.id, QString());
    QVERIFY(!ed->summaryStale().stale);
    QVERIFY(!m_app->summaryStale(m.id).stale);
    m_app->closeSpeakerEditor(m.id);
}

void SpeakerControllerTest::transcriptReady_discardsOverlay()
{
    const Meeting m = makeMeeting(QStringLiteral("Újraírt"), false);
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    ed->moveUtterancesToPerson({QStringLiteral("u0")}, QStringLiteral("Dóra"));
    QVERIFY(ed->confirmUtterances({QStringLiteral("u6000")}));
    QCOMPARE(m_app->retranscribeImpact(m.id).manualCorrections(), 2);
    QCOMPARE(ed->speakers().size(), 3);

    // A new transcript was made (emitted by AppController at the end of transcription): the
    // manual fixes dropped, the session reloaded, the undo stack empty.
    QSignalSpy reloaded(ed, &SpeakerEditor::reloaded);
    emit m_app->transcriptReady(m.id, QDir(m.folder).filePath(QStringLiteral("transcript.md")));
    QCOMPARE(reloaded.count(), 1);
    QCOMPARE(ed->speakers().size(), 2);
    QVERIFY(!ed->canUndo());
    QCOMPARE(m_app->retranscribeImpact(m.id).manualCorrections(), 0);
    QVERIFY(!speakeredit::loadOverlay(m.folder).hasEdits());
    m_app->closeSpeakerEditor(m.id);
}

void SpeakerControllerTest::globalPersonRename_reachesParticipants()
{
    const Meeting m = makeMeeting(QStringLiteral("Átnevezés"), false);
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    const QString p = ed->moveUtterancesToPerson({QStringLiteral("u0")}, QStringLiteral("Emese"));
    QVERIFY(ed->reassignSpeaker(kB2, QStringLiteral("Feri")));

    m_app->renamePerson(QStringLiteral("Emese"), QStringLiteral("Emőke"));
    QCOMPARE(ed->speaker(p).personName, QStringLiteral("Emőke"));
    ed->flushPendingWrites();
    QVERIFY(markdown(m).contains(QStringLiteral("**Emőke** S0")));
    m_app->renamePerson(QStringLiteral("Feri"), QStringLiteral("Ferenc"));
    QCOMPARE(ed->speaker(kB2).personName, QStringLiteral("Ferenc"));

    m_app->removePerson(QStringLiteral("Emőke"));
    QVERIFY(ed->speaker(p).anonymous);
    QCOMPARE(ed->speaker(p).utteranceCount, 1);
    m_app->closeSpeakerEditor(m.id);
}

void SpeakerControllerTest::peopleAndImpact()
{
    const Meeting m = makeMeeting(QStringLiteral("Személyek"), false);
    m_app->renameSpeaker(m.id, kB1, QStringLiteral("Gizi"), false);
    const QVector<PersonInfo> all = m_app->peopleDirectory();
    bool found = false;
    for (const PersonInfo& p : all)
        if (p.name == QStringLiteral("Gizi")) {
            found = true;
            QCOMPARE(p.meetingCount, 1);
            QVERIFY(!p.hasVoiceprint);      // a manual naming (enroll=false) does not teach
        }
    QVERIFY(found);
    const RetranscribeImpact imp = m_app->retranscribeImpact(m.id);
    QCOMPARE(imp.namedSpeakers, 1);
    QCOMPARE(imp.manualCorrections(), 0);
}

QTEST_GUILESS_MAIN(SpeakerControllerTest)
#include "test_speaker_controller.moc"

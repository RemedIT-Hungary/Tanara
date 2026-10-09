//
// Több hangmodell az AppController-ben: lenyomat-felvétel minden használt modellel (testvér-
// lenyomatok, sourceRefs), a modell-lista változása (szerkesztő, pótlás), a lusta lenyomat-pótlás
// háttérszálon (hiányzó hang kihagyva), és a testvérek együttes törlése / áthelyezése.
// Hamis „modellekkel” (setVoiceEmbedderLoader) és ffmpeg-gel generált hanggal fut, IZOLÁLT
// TANARA_HOME-ban (a modellfájlok üres helyőrzők). ffmpeg nélkül QSKIP.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/people/PeopleService.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceModelRegistry.h"

#include <atomic>
#include <memory>

using namespace tanara;

namespace {

const QString kCamp = QStringLiteral("campplus");
const QString kWe = QStringLiteral("wespeaker-resnet34-lm");

// Modellenként állandó irányú vektor; a háttérszálból is hívható (atomikus számláló).
PcmEmbedderLoader fakeLoader(std::shared_ptr<std::atomic<int>> embeds)
{
    return [embeds](const VoiceModelSpec& spec, const QString&, QString*) -> PcmEmbedder {
        const int dim = spec.dim;
        const int hot = spec.id == kCamp ? 0 : 1;
        return [embeds, dim, hot](const QVector<float>& pcm) {
            ++*embeds;
            if (pcm.size() < 8000) return QVector<float>();
            QVector<float> v(dim, 0.0f);
            v[hot] = 1.0f;
            return v;
        };
    };
}

bool makeWav(const QString& path, int seconds)
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
                                       QStringLiteral("sine=frequency=220:duration=%1").arg(seconds),
                                       "-ar", "16000", "-ac", "1", "-y", path});
    return p.waitForFinished(30000) && p.exitCode() == 0 && QFileInfo::exists(path);
}

} // namespace

class VoiceModelsAppTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void enrolCreatesOnePrintPerModel();
    void enrolSpeakerFromTranscript_hasSourceRefs();
    void editorGetsActiveModels();
    void backfillAfterEnablingModel();
    void siblingRemoveAndMoveWithUndo();

private:
    Meeting makeMeeting(const QString& title);
    void enable(const QStringList& ids) { m_app->settings()->setEnabledVoiceModels(ids); }

    bool m_haveFfmpeg = false;
    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<AppController> m_app;
    std::shared_ptr<std::atomic<int>> m_embeds;
};

void VoiceModelsAppTest::initTestCase()
{
    m_haveFfmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    qputenv("TANARA_CLOUD", "off");
}

void VoiceModelsAppTest::init()
{
    if (!m_haveFfmpeg) QSKIP("nincs ffmpeg");
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    // Helyőrző modellfájlok: a hamis betöltő nem olvassa, csak a „megvan-e” számít.
    for (const QString& id : {kCamp, kWe}) {
        const QString f = QDir(m_home->path()).filePath("models/" + VoiceModelRegistry::spec(id)->fileName);
        QDir().mkpath(QFileInfo(f).absolutePath());
        QFile file(f);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    m_app = std::make_unique<AppController>();
    QVERIFY(m_app->store()->metadataDir().startsWith(m_home->path()));
    m_embeds = std::make_shared<std::atomic<int>>(0);
    m_app->setVoiceEmbedderLoader(fakeLoader(m_embeds));
    enable({kCamp, kWe});
    QCOMPARE(m_app->activeVoiceModelIds(), QStringList({kCamp, kWe}));
    QVERIFY(m_app->voiceIdentificationAvailable());
}

void VoiceModelsAppTest::cleanup()
{
    m_app.reset();
    m_home.reset();
}

// Egy 8 mp-es megbeszélés: egy mikrofon-sáv (1 s eltolással) és a keverék, két beszélővel.
Meeting VoiceModelsAppTest::makeMeeting(const QString& title)
{
    Meeting m = m_app->store()->createMeeting(title);
    [&] { QVERIFY(makeWav(QDir(m.folder).filePath("track_mic.wav"), 7)); }();
    [&] { QVERIFY(makeWav(QDir(m.folder).filePath("mixdown.wav"), 8)); }();
    Track t;
    t.id = QStringLiteral("mic");
    t.deviceName = QStringLiteral("Teszt mikrofon");
    t.file = QStringLiteral("track_mic.wav");
    t.startOffsetMs = 1000;
    m.tracks.append(t);
    m.mixdownFile = QStringLiteral("mixdown.wav");
    m.durationMs = 8000;
    QJsonArray toks, segs;
    const struct { qint64 s, e; const char* spk; } rows[] = {{0, 3000, "Beszélő 1"}, {3000, 5000, "Beszélő 2"},
                                                             {5000, 8000, "Beszélő 1"}};
    for (const auto& r : rows) {
        toks.append(QJsonObject{{"text", "x"}, {"speaker", QString::fromUtf8(r.spk)}, {"startMs", double(r.s)},
                                {"endMs", double(r.e)}, {"confidence", 1.0}, {"trackId", "mixdown"}});
        segs.append(QJsonObject{{"startMs", double(r.s)}, {"endMs", double(r.e)},
                                {"speaker", QString::fromUtf8(r.spk)}, {"text", "x"}});
    }
    QFile tf(QDir(m.folder).filePath("transcript.tokens.json"));
    if (tf.open(QIODevice::WriteOnly)) tf.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
    tf.close();
    QFile sf(QDir(m.folder).filePath("transcript.segments.json"));
    if (sf.open(QIODevice::WriteOnly)) sf.write(QJsonDocument(segs).toJson());
    sf.close();
    m.hasTranscript = true;
    m_app->store()->saveMeeting(m);
    return m;
}

void VoiceModelsAppTest::enrolCreatesOnePrintPerModel()
{
    const Meeting m = makeMeeting("Felvétel");
    QSignalSpy changed(m_app.get(), &AppController::voiceprintsChanged);
    m_app->enrollVoiceprintFromSample("Anna", m.id, "mic", 2000, 5000);
    QCOMPARE(changed.count(), 1);
    const QVector<Voiceprint> prints = m_app->voiceprints()->printsFor("Anna");
    QCOMPARE(prints.size(), 2);
    QSet<QString> models;
    for (const Voiceprint& p : prints) {
        models.insert(p.model);
        QCOMPARE(p.sampleRef, QStringLiteral("track_mic.wav#2000-5000"));   // megbeszélés-idő
        QCOMPARE(p.sourceRefs, QStringList{p.sampleRef});
        QCOMPARE(p.createdAt, prints.first().createdAt);
        QCOMPARE(p.sourceMeetingId, m.id);
        QCOMPARE(p.dim, VoiceModelRegistry::spec(p.model)->dim);
    }
    QCOMPARE(models, QSet<QString>({kCamp, kWe}));
    QVERIFY(prints[0].id != prints[1].id);
    QCOMPARE(m_app->voiceprints()->sampleCount("Anna"), 1);
    QCOMPARE(m_app->peopleService()->samples("Anna").size(), 1);   // egy minta a listában

    // A párosítás mindkét modellel megy (a hamis vektorok azonosak → 1.0).
    const VoiceMatch match = m_app->testSpeakerMatch(m.id, "Beszélő 1");
    QCOMPARE(match.name, QStringLiteral("Anna"));
    QVERIFY(match.score > 0.99);

    // Csak az egyik modell bekapcsolva → egy lenyomat.
    enable({kWe});
    QCOMPARE(m_app->activeVoiceModelIds(), QStringList{kWe});
    m_app->enrollVoiceprintFromSample("Béla", m.id, "mic", 2000, 5000);
    QCOMPARE(m_app->voiceprints()->printsFor("Béla").size(), 1);
    QCOMPARE(m_app->voiceprints()->printsFor("Béla").first().model, kWe);

    // Egy sem használható → nincs lenyomat, nincs azonosítás.
    enable({});
    QVERIFY(!m_app->voiceIdentificationAvailable());
    m_app->enrollVoiceprintFromSample("Cili", m.id, "mic", 2000, 5000);
    QCOMPARE(m_app->voiceprints()->printCount("Cili"), 0);
}

void VoiceModelsAppTest::enrolSpeakerFromTranscript_hasSourceRefs()
{
    const Meeting m = makeMeeting("Átirat");
    m_app->enrollSpeaker(m.id, "Beszélő 1", "Dóra");
    const QVector<Voiceprint> prints = m_app->voiceprints()->printsFor("Dóra");
    QCOMPARE(prints.size(), 2);
    for (const Voiceprint& p : prints) {
        QCOMPARE(p.sampleRef, QStringLiteral("mixdown.wav#0-3000"));   // a leghosszabb sor
        QVERIFY(!p.sourceRefs.isEmpty());
        QVERIFY(p.sourceRefs.first().startsWith(QStringLiteral("mixdown.wav#")));
    }
    QCOMPARE(prints[0].sourceRefs, prints[1].sourceRefs);
}

void VoiceModelsAppTest::editorGetsActiveModels()
{
    const Meeting m = makeMeeting("Szerkesztő");
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QVERIFY(ed);
    QCOMPARE(ed->voiceModelIds(), QStringList({kCamp, kWe}));
    QVERIFY(ed->embeddingsSupported());
    QSignalSpy models(m_app.get(), &AppController::voiceModelsChanged);
    enable({kCamp});
    QCOMPARE(models.count(), 1);
    QCOMPARE(ed->voiceModelIds(), QStringList({kCamp}));
    // Más beállítás változása nem számolja újra.
    m_app->settings()->setEnabledVoiceModels({kCamp});
    AppSettings s = m_app->settings()->settings();
    s.userSpeakerName = QStringLiteral("Én");
    m_app->settings()->setSettings(s);
    QCOMPARE(models.count(), 1);
    // Ismeretlen id: a beállítás megmarad, futásidőben kimarad.
    enable({kCamp, "nincs-ilyen"});
    QCOMPARE(m_app->settings()->enabledVoiceModels(), QStringList({kCamp, "nincs-ilyen"}));
    QCOMPARE(m_app->activeVoiceModelIds(), QStringList({kCamp}));
}

void VoiceModelsAppTest::backfillAfterEnablingModel()
{
    const Meeting m = makeMeeting("Pótlás");
    enable({kCamp});
    m_app->enrollVoiceprintFromSample("Anna", m.id, "mic", 2000, 5000);
    QCOMPARE(m_app->voiceprints()->printsFor("Anna").size(), 1);
    // Több szakaszból készült lenyomat (szerkesztő-stílus): a pótlás a sourceRefs-ből számol.
    Voiceprint multi;
    multi.embedding = {1, 0, 0};
    multi.model = kCamp;
    multi.sourceMeetingId = m.id;
    multi.sampleRef = QStringLiteral("mixdown.wav#0-3000");
    multi.sourceRefs = {QStringLiteral("mixdown.wav#0-3000"), QStringLiteral("mixdown.wav#5000-8000")};
    multi.createdAt = QStringLiteral("2026-10-01T10:00:00");
    m_app->voiceprints()->addPrint("Béla", multi);
    // Hiányzó hang: kihagyja (és nem akad el rajta).
    Voiceprint gone = multi;
    gone.sampleRef = QStringLiteral("nincs-ilyen.wav#0-3000");
    gone.sourceRefs.clear();
    m_app->voiceprints()->addPrint("Cili", gone);
    // Ismeretlen forrás-megbeszélés: szintén kihagyva.
    Voiceprint orphan = multi;
    orphan.sourceMeetingId = QStringLiteral("torolt-megbeszeles");
    m_app->voiceprints()->addPrint("Dani", orphan);

    QSignalSpy done(m_app.get(), &AppController::voiceprintBackfillFinished);
    QSignalSpy changed(m_app.get(), &AppController::voiceprintsChanged);
    const int before = m_embeds->load();
    enable({kCamp, kWe});   // bővült → a pótlás magától indul, háttérszálon
    QVERIFY(m_app->voiceprintBackfillRunning());
    QCOMPARE(m_app->voiceprints()->printsFor("Anna").size(), 1);   // a hívás nem várt rá
    QVERIFY(done.wait(30000));
    QCOMPARE(done.first().first().toInt(), 2);
    QCOMPARE(changed.count(), 1);
    QVERIFY(m_embeds->load() > before);

    for (const QString& who : {QStringLiteral("Anna"), QStringLiteral("Béla")}) {
        const QVector<Voiceprint> ps = m_app->voiceprints()->printsFor(who);
        QCOMPARE(ps.size(), 2);
        QCOMPARE(VoiceprintStore::siblingKey(ps[0]), VoiceprintStore::siblingKey(ps[1]));
        QCOMPARE(ps[0].sourceRefs, ps[1].sourceRefs);
        QVERIFY(ps[0].id != ps[1].id);
        QVERIFY(ps[0].model != ps[1].model);
    }
    QCOMPARE(m_app->voiceprints()->printsFor("Cili").size(), 1);
    QCOMPARE(m_app->voiceprints()->printsFor("Dani").size(), 1);
    QCOMPARE(m_app->voiceprints()->samplesMissingModel("Béla", kWe).size(), 0);

    // Újrafuttatás: nincs mit pótolni (a hiányzó hangúak megint kimaradnak).
    done.clear();
    m_app->backfillVoiceprints();
    if (done.isEmpty()) QVERIFY(done.wait(30000));
    QCOMPARE(done.first().first().toInt(), 0);
}

void VoiceModelsAppTest::siblingRemoveAndMoveWithUndo()
{
    const Meeting m = makeMeeting("Testvérek");
    m_app->enrollVoiceprintFromSample("Anna", m.id, "mic", 2000, 5000);
    m_app->enrollVoiceprintFromSample("Anna", m.id, "mic", 5000, 7000);
    QCOMPARE(m_app->voiceprints()->printCount("Anna"), 4);
    PeopleService* svc = m_app->peopleService();
    const QVector<VoiceSample> samples = svc->samples("Anna");
    QCOMPARE(samples.size(), 2);
    QCOMPARE(svc->person("Anna").sampleCount, 2);

    // Törlés: a testvér is megy; visszavonás: mindkettő vissza.
    const QString first = samples.first().id;
    QVERIFY(svc->removeSample(first).ok);
    QCOMPARE(m_app->voiceprints()->printCount("Anna"), 2);
    QCOMPARE(svc->samples("Anna").size(), 1);
    svc->undo();
    QCOMPARE(m_app->voiceprints()->printCount("Anna"), 4);

    // Áthelyezés: a testvérrel együtt, azonosítók megmaradnak; visszavonás.
    const QVector<Voiceprint> sibs = m_app->voiceprints()->siblingsOf(first);
    QCOMPARE(sibs.size(), 2);
    QVERIFY(svc->moveSample(sibs.last().id, "Béla").ok);
    QCOMPARE(m_app->voiceprints()->printCount("Anna"), 2);
    QCOMPARE(m_app->voiceprints()->printCount("Béla"), 2);
    QStringList movedIds;
    for (const Voiceprint& p : m_app->voiceprints()->printsFor("Béla")) movedIds << p.id;
    QVERIFY(movedIds.contains(sibs.first().id) && movedIds.contains(sibs.last().id));
    svc->undo();
    QCOMPARE(m_app->voiceprints()->printCount("Anna"), 4);
    QCOMPARE(m_app->voiceprints()->printCount("Béla"), 0);
}

QTEST_GUILESS_MAIN(VoiceModelsAppTest)
#include "test_voice_models_app.moc"

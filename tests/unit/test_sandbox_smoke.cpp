//
// Opcionális füstteszt VALÓDI mintaadaton (eldobható sandbox-másolaton) — alapból KIMARAD.
//
// Futtatás:  TANARA_SANDBOX_HOME=<sandbox>/home ./build/tests/test_sandbox_smoke
// (a <sandbox>/home egy teljes metaadat-mappa: settings.json az audioDir-rel, people.json,
//  voiceprints.json, models/ …). A teszt a TANARA_HOME-ot erre állítja, így a valódi
// ~/.tanara-hoz nem nyúl. Fizetős hívást NEM indít (se átírás, se összefoglaló): csak helyi
// műveletek — könyvtár-lekérdezések, sáv-nevek, hullámforma (ffmpeg), összefoglaló-értelmezés,
// hang-alapú azonosítás (helyi ONNX-modell).
//
// A kimenetben SZÁNDÉKOSAN nincs átirat-szöveg: csak darabszámok és időmérések.
//
#include <QtTest>
#include <QSignalSpy>

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/Paths.h"
#include "tanara/audio/TrackCatalog.h"
#include "tanara/audio/WaveformService.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/store/MeetingStore.h"

using namespace tanara;

class SandboxSmoke : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void libraryListSearchAndPending();
    void tracksAndWaveforms();
    void summariesAndTopics();
    void identifyAsync();

private:
    std::unique_ptr<AppController> m_app;
};

void SandboxSmoke::initTestCase()
{
    const QString home = qEnvironmentVariable("TANARA_SANDBOX_HOME");
    if (home.isEmpty())
        QSKIP("TANARA_SANDBOX_HOME nincs beállítva — a valós adatos füstteszt kimarad");
    QVERIFY2(QFile::exists(QDir(home).filePath("settings.json")), "nincs settings.json a sandboxban");
    qputenv("TANARA_HOME", home.toUtf8());
    qputenv("TANARA_CLOUD", "off");
    m_app = std::make_unique<AppController>();
    QCOMPARE(m_app->settings()->settings().metadataDir, paths::homeOverride());
    // Biztonsági háló: a felvételek mappája nem lehet a valódi ~/Tanara.
    const QString audio = m_app->store()->audioDir();
    QVERIFY2(!audio.startsWith(QDir(QDir::homePath()).filePath("Tanara")),
             "a sandbox settings.json a VALÓDI ~/Tanara mappára mutat — megszakítva");
    m_app->store()->rebuildIndexFromDisk();   // friss sandbox: az index üres
    m_app->library()->invalidate();
}

void SandboxSmoke::cleanupTestCase()
{
    m_app.reset();
}

void SandboxSmoke::libraryListSearchAndPending()
{
    MeetingLibrary* lib = m_app->library();
    QElapsedTimer clock;
    clock.start();
    const LibraryResult all = lib->query();
    const qint64 listMs = clock.restart();
    QVERIFY(all.count() > 0);
    int withTranscript = 0, withSummary = 0, identified = 0;
    QHash<QString, int> sections;
    for (const LibraryEntry& e : all.entries) {
        if (e.state.transcriptState == StepState::Done) ++withTranscript;
        if (e.state.summaryState == StepState::Done) ++withSummary;
        if (e.state.identifyState == StepState::Done) ++identified;
        sections[MeetingLibrary::sectionKey(e.section)]++;
    }
    for (int i = 1; i < all.count(); ++i)
        QVERIFY(all.entries[i - 1].startedAt >= all.entries[i].startedAt);   // legújabb elöl

    // Ékezet nélküli, kisbetűs keresés egy gyakori szóra; a találatnál a kiemelt rész a
    // hajtogatva egyező eredeti szöveg.
    LibraryQuery q;
    q.text = QStringLiteral("koszonom");
    const LibraryResult cold = lib->query(q);
    const qint64 coldMs = clock.restart();
    const LibraryResult warm = lib->query(q);
    const qint64 warmMs = clock.restart();
    QCOMPARE(warm.count(), cold.count());
    for (const LibraryEntry& e : warm.entries) {
        QVERIFY(e.titleMatch.isValid() || e.snippetMatch.isValid());
        if (e.snippetMatch.isValid()) {
            const QString hit = e.snippet.mid(e.snippetMatch.start, e.snippetMatch.length);
            QCOMPARE(textfold::foldQuery(hit), QStringLiteral("koszonom"));
            QVERIFY(e.snippetMs >= 0);
        }
    }
    LibraryQuery noT;
    noT.noTranscript = true;
    LibraryQuery noS;
    noS.noSummary = true;
    const QVector<PendingItem> pending = lib->pendingItems();
    QCOMPARE(pending.size(), lib->query(noT).count());
    qInfo("meetingek: %d (átirat: %d, összefoglaló: %d, azonosítva: %d) | szekciók: ma %d, tegnap %d, e héten %d, korábban %d",
          all.count(), withTranscript, withSummary, identified, sections.value("today"),
          sections.value("yesterday"), sections.value("thisWeek"), sections.value("earlier"));
    qInfo("lista %lld ms | keresés: %d találat, hideg %lld ms, meleg %lld ms | nincs átirat: %d, nincs összefoglaló: %d | személyek: %d | várnak: %d",
          listMs, cold.count(), coldMs, warmMs, lib->query(noT).count(), lib->query(noS).count(),
          int(lib->people().size()), int(pending.size()));
}

void SandboxSmoke::tracksAndWaveforms()
{
    const LibraryResult all = m_app->library()->query();
    QSet<QString> names;
    int total = 0, missing = 0, dropped = 0;
    for (const LibraryEntry& e : all.entries)
        for (const TrackView& v : m_app->tracks()->tracks(e.id)) {
            ++total;
            if (v.fileMissing) ++missing;
            if (!v.track.active) ++dropped;
            names.insert(QStringLiteral("%1 <- %2").arg(v.displayName, v.rawDeviceName));
        }
    for (const QString& n : std::as_const(names)) qInfo().noquote() << "sáv:" << n;
    qInfo("sávok: %d, eldobott: %d, hiányzó fájl: %d", total, dropped, missing);
    QCOMPARE(missing, 0);

    // Hullámforma az első meeting összes sávjára (+ keverék): aszinkron, majd gyorsítótárból.
    const QString id = all.entries.last().id;
    const QVector<TrackView> views = m_app->tracks()->tracks(id);
    int expected = views.size() + (m_app->store()->load(id).mixdownFile.isEmpty() ? 0 : 1);
    QSignalSpy ready(m_app->waveforms(), &WaveformService::peaksReady);
    QSignalSpy failed(m_app->waveforms(), &WaveformService::peaksFailed);
    QElapsedTimer clock;
    clock.start();
    m_app->requestWaveforms(id);
    const qint64 callMs = clock.elapsed();
    QTRY_COMPARE_WITH_TIMEOUT(ready.count() + failed.count(), expected, 120000);
    const qint64 computeMs = clock.restart();
    QCOMPARE(failed.count(), 0);
    for (const QList<QVariant>& args : ready) {
        const TrackPeaks p = args.at(2).value<TrackPeaks>();
        QVERIFY(p.peaks.size() > 100 && p.peaks.size() <= WaveformService::kBuckets);
        QVERIFY(p.durationMs > 0);
    }
    for (const TrackView& v : m_app->tracks()->tracks(id))
        QVERIFY(v.durationMs > 0);                       // a hossz a cache-ből már megvan
    ready.clear();
    m_app->requestWaveforms(id);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), expected, 5000);
    qInfo("hullámforma: %d fájl, kérés %lld ms (nem blokkol), számítás %lld ms, gyorsítótárból %lld ms",
          expected, callMs, computeMs, clock.elapsed());
}

void SandboxSmoke::summariesAndTopics()
{
    int docs = 0, fromMd = 0, topicLists = 0;
    for (const LibraryEntry& e : m_app->library()->query().entries) {
        const SummaryDocument doc = m_app->summaryDocument(e.id);
        QCOMPARE(doc.exists, e.hasSummary);
        if (doc.exists) {
            ++docs;
            if (doc.fromMarkdown) ++fromMd;
            QVERIFY(!doc.summary.execSummary.isEmpty());
            QVERIFY(doc.meta.createdAt.isValid());
            qInfo("összefoglaló: mód=%s, döntés=%d, teendő=%d, résztvevő=%d, téma=%d",
                  qPrintable(summarystore::modeToString(doc.meta.mode)), int(doc.summary.decisions.size()),
                  int(doc.summary.actionItems.size()), int(doc.summary.participants.size()),
                  int(doc.topics.size()));
        }
        const QVector<SummaryTopic> topics = m_app->meetingTopics(e.id);
        if (topics.isEmpty()) continue;
        ++topicLists;
        const QVector<TopicStatus> sts = m_app->topicStatuses(e.id);
        QCOMPARE(sts.size(), topics.size());
        int done = 0;
        for (const TopicStatus& s : sts) if (s.state == TopicState::Done) ++done;
        QVERIFY(done <= m_app->topicAnalyses(e.id).size());
        // Átrendezés (megfordítás) perzisztál, majd vissza az eredetire.
        QVector<SummaryTopic> rev = topics;
        std::reverse(rev.begin(), rev.end());
        m_app->setMeetingTopics(e.id, rev);
        QCOMPARE(m_app->meetingTopics(e.id).first().id, topics.last().id);
        QCOMPARE(m_app->topicStatuses(e.id).first().topicId, topics.last().id);
        m_app->setMeetingTopics(e.id, topics);
        QCOMPARE(m_app->meetingTopics(e.id).first().id, topics.first().id);
        qInfo("témák: %d (kész elemzés: %d)", int(topics.size()), done);
    }
    qInfo("összefoglalók: %d (markdownból visszanyerve: %d), téma-listák: %d", docs, fromMd, topicLists);
}

void SandboxSmoke::identifyAsync()
{
    // Egy átirattal rendelkező meeting: a nevek törlése után a háttérszálas azonosítás újra
    // megtalálja őket a lenyomat-DB-ből (helyi modell; ha nincs modell, kimarad).
    QString id;
    for (const LibraryEntry& e : m_app->library()->query().entries)
        if (e.hasTranscript && !m_app->store()->load(e.id).speakerMap.isEmpty()) id = e.id;
    if (id.isEmpty()) QSKIP("nincs nevesített beszélőjű meeting a sandboxban");
    Meeting m = m_app->store()->load(id);
    const int namedBefore = m.speakerMap.size();
    m.speakerMap.clear();
    m_app->store()->saveMeeting(m);

    QSignalSpy finished(m_app->jobs(), &MeetingJobTracker::jobFinished);
    QSignalSpy progress(m_app->jobs(), &MeetingJobTracker::jobProgressChanged);
    QElapsedTimer clock;
    clock.start();
    if (!m_app->identifyMeetingAsync(id))
        QSKIP("nincs hang-modell (vagy voice-ID nélküli build) — az azonosítás kimarad");
    const qint64 callMs = clock.elapsed();
    QVERIFY(m_app->jobs()->isRunning(id, JobKind::Identify));
    QCOMPARE(m_app->processingState(id).identifyState, StepState::Running);
    const int total = m_app->jobs()->job(id, JobKind::Identify).total;
    QVERIFY(total > 0);
    QVERIFY(finished.wait(180000));
    QCOMPARE(finished.at(0).at(2).value<JobOutcome>(), JobOutcome::Done);
    const int namedAfter = m_app->store()->load(id).speakerMap.size();
    QCOMPARE(m_app->processingState(id).identifyState, StepState::Done);
    qInfo("azonosítás: %d beszélő, indítás %lld ms (nem blokkol), teljes %lld ms, haladás-jel: %d, nevesítve előtte %d → utána %d",
          total, callMs, clock.elapsed(), int(progress.count()), namedBefore, namedAfter);
    QVERIFY(progress.count() >= total);
}

QTEST_GUILESS_MAIN(SandboxSmoke)
#include "test_sandbox_smoke.moc"

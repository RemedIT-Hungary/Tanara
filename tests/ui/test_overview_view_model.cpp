// Az Áttekintés fül nézetmodellje (OverviewViewModel) valódi AppControllerrel, IZOLÁLT
// TANARA_HOME-ban (QTemporaryDir; hang nincs, modell nincs, hálózat nincs): résztvevők kézi
// felvétele és törlése, „csak én beszéltem”, a kézi leírás, a feldolgozási lépések átirat előtt
// és után, az ADATOK csempék, a „nobody” állapot (csak névtelen beszélők), a sávok beszédaránya
// és a lekeverésbe vonás (TrackListModel). Demó-adat: a négy képernyő-állapot.
#include "AppContext.h"
#include "OverviewViewModel.h"
#include "ShellMeetingModel.h"
#include "TrackListModel.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/store/MeetingStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

class TestOverviewViewModel : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<tanara::AppController> m_app;

    tanara::Meeting recording(const QString& title)
    {
        tanara::Meeting m = m_app->store()->createMeeting(title);
        tanara::Track mic;
        mic.id = QStringLiteral("mic");
        mic.kind = tanara::TrackKind::Mic;
        mic.file = QStringLiteral("track_mic.wav");
        mic.speechRatio = 0.38;
        tanara::Track sys;
        sys.id = QStringLiteral("system");
        sys.kind = tanara::TrackKind::Loopback;
        sys.file = QStringLiteral("track_system.wav");
        sys.active = false;
        sys.speechRatio = 0.0;
        sys.excludedReason = QStringLiteral("noSpeech");
        m.tracks = {mic, sys};
        m.durationMs = 4564000;   // 1:16:04
        for (const tanara::Track& t : m.tracks) {
            QFile f(QDir(m.folder).filePath(t.file));
            if (f.open(QIODevice::WriteOnly)) f.write("nem valódi hang");
        }
        m_app->store()->saveMeeting(m);
        return m;
    }
    tanara::Meeting transcribed(const QString& title)
    {
        tanara::Meeting m = recording(title);
        QFile seg(QDir(m.folder).filePath(QStringLiteral("transcript.segments.json")));
        if (seg.open(QIODevice::WriteOnly))
            seg.write(QJsonDocument(QJsonArray{
                QJsonObject{{"startMs", 0}, {"endMs", 900}, {"speaker", "Beszélő 1"}, {"text", "Sziasztok."}},
                QJsonObject{{"startMs", 1000}, {"endMs", 1900}, {"speaker", "Beszélő 2"}, {"text", "Szia."}},
                QJsonObject{{"startMs", 2000}, {"endMs", 2900}, {"speaker", "Beszélő 1"}, {"text", "Kezdjük."}}}).toJson());
        m.hasTranscript = true;
        m_app->store()->saveMeeting(m);
        return m;
    }
    static QVariantMap stepOf(const OverviewViewModel& vm, const QString& key)
    {
        for (const QVariant& v : vm.steps())
            if (v.toMap().value(QStringLiteral("key")).toString() == key) return v.toMap();
        return {};
    }
    static QStringList names(const OverviewViewModel& vm)
    {
        QStringList out;
        for (const QVariant& v : vm.participants()) out << v.toMap().value(QStringLiteral("name")).toString();
        return out;
    }

private slots:
    void initTestCase() { qputenv("TANARA_CLOUD", "off"); }

    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        AppContext::instance()->setDemo(false);
        m_app = std::make_unique<tanara::AppController>();
        QVERIFY(m_app->store()->audioDir().startsWith(m_home->path()));
    }
    void cleanup()
    {
        m_app.reset();
        m_home.reset();
        qunsetenv("TANARA_HOME");
    }

    void monogram()
    {
        QCOMPARE(OverviewViewModel::monogramOf(QStringLiteral("Varga Árpád")), QStringLiteral("VÁ"));
        QCOMPARE(OverviewViewModel::monogramOf(QStringLiteral("Távoli 1")), QStringLiteral("T1"));
        QCOMPARE(OverviewViewModel::monogramOf(QStringLiteral("Kovács Lilla (te)")), QStringLiteral("KL"));
        QCOMPARE(OverviewViewModel::monogramOf(QString()), QStringLiteral("?"));
    }

    // Átirat előtt: lépések (felvétel kész, az átirat még nem indult), üres résztvevő-lista,
    // kézi résztvevő → lista + forrás „kézi”, × → eltűnik; a kézi leírás mentődik.
    void beforeTranscript_participantsAndSteps()
    {
        const tanara::Meeting m = recording(QStringLiteral("Előtte"));
        OverviewViewModel vm;
        vm.setController(m_app.get());
        vm.setMeetingId(m.id);
        QVERIFY(!vm.hasTranscript());
        QVERIFY(!vm.transcribing());
        QCOMPARE(vm.peopleState(), QStringLiteral("empty"));
        QCOMPARE(stepOf(vm, QStringLiteral("recording")).value("state").toString(), QStringLiteral("done"));
        QCOMPARE(stepOf(vm, QStringLiteral("recording")).value("sub").toString(), QStringLiteral("1:16:04 · 2 sáv"));
        QCOMPARE(stepOf(vm, QStringLiteral("tracks")).value("sub").toString(),
                 QStringLiteral("1 aktív · 1 kimaradt, nincs rajta beszéd"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("waiting"));
        QVERIFY(stepOf(vm, QStringLiteral("transcript")).value("parallel").toBool());
        QCOMPARE(stepOf(vm, QStringLiteral("summary")).value("sub").toString(), QStringLiteral("az átirat után"));
        QVERIFY(!vm.allDone());
        QVERIFY(vm.stats().isEmpty());

        const QString id = vm.addParticipant(QStringLiteral("Fehér Gábor"));
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(vm.peopleState(), QStringLiteral("list"));
        QCOMPARE(names(vm), QStringList{QStringLiteral("Fehér Gábor")});
        const QVariantMap row = vm.participants().first().toMap();
        QCOMPARE(row.value("monogram").toString(), QStringLiteral("FG"));
        QCOMPARE(row.value("talkShare").toDouble(), -1.0);
        const QVariantList sources = row.value("sources").toList();
        QVERIFY(!sources.isEmpty());
        QCOMPARE(sources.first().toMap().value("text").toString(), QStringLiteral("kézi"));

        vm.removeParticipant(id);
        QTRY_COMPARE(vm.peopleState(), QStringLiteral("empty"));

        vm.setContextNote(QStringLiteral("  Q4 partner review  "));
        QTRY_COMPARE(vm.contextNote(), QStringLiteral("Q4 partner review"));
        QCOMPARE(m_app->store()->load(m.id).contextNote, QStringLiteral("Q4 partner review"));
    }

    // „Csak én beszéltem”: saját név nélkül nem megy; beállított névvel a döntés mentődik.
    void soloMeeting()
    {
        const tanara::Meeting m = recording(QStringLiteral("Egyedül"));
        OverviewViewModel vm;
        vm.setController(m_app.get());
        vm.setMeetingId(m.id);
        tanara::AppSettings s = m_app->settings()->settings();
        s.userSpeakerName = QString();
        m_app->settings()->setSettings(s);
        QVERIFY(!vm.setSolo());
        s.userSpeakerName = QStringLiteral("Kovács Lilla");
        m_app->settings()->setSettings(s);
        QVERIFY(vm.setSolo());
        const tanara::Meeting after = m_app->store()->load(m.id);
        QVERIFY(after.approval.has_value());
        QVERIFY(after.approval->solo);
        QTRY_COMPARE(vm.peopleHint(), QStringLiteral("csak én beszéltem"));
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("sub").toString(), QStringLiteral("csak én beszéltem"));
    }

    // Átirat után, csak névtelen beszélőkkel: „nobody” + a nyers beszélők aránnyal; ADATOK csempék.
    void afterTranscript_nobodyAndStats()
    {
        const tanara::Meeting m = transcribed(QStringLiteral("Utána"));
        OverviewViewModel vm;
        vm.setController(m_app.get());
        vm.setMeetingId(m.id);
        QVERIFY(vm.hasTranscript());
        QCOMPARE(vm.peopleState(), QStringLiteral("nobody"));
        QCOMPARE(vm.anonymousSpeakers().size(), 2);
        const QVariantMap first = vm.anonymousSpeakers().first().toMap();
        QCOMPARE(first.value("name").toString(), QStringLiteral("Beszélő 1"));
        QVERIFY(first.value("share").toDouble() > 0.6);
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("done"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("sub").toString(),
                 QStringLiteral("3 megszólalás · 2 beszélő"));
        QCOMPARE(vm.stats().size(), 6);
        QCOMPARE(vm.stats().at(0).toMap().value("value").toString(), QStringLiteral("1:16:04"));
        QCOMPARE(vm.stats().at(2).toMap().value("value").toString(), QStringLiteral("2"));
        QCOMPARE(vm.stats().at(2).toMap().value("sub").toString(), QStringLiteral("2 névtelen"));
        QCOMPARE(vm.stats().at(3).toMap().value("value").toString(), QStringLiteral("3"));
        QCOMPARE(vm.stats().at(5).toMap().value("value").toString(), QStringLiteral("nincs"));
    }

    // A sávok beszédaránya, a kimaradás oka és a „Beemelem” (TrackListModel → AppController).
    void tracks_speechAndInclude()
    {
        const tanara::Meeting m = recording(QStringLiteral("Sávok"));
        TrackListModel tracks;
        tracks.setController(m_app.get());
        tracks.setMeetingId(m.id);
        QCOMPARE(tracks.count(), 2);
        QCOMPARE(tracks.includedCount(), 1);
        QCOMPARE(tracks.excludedCount(), 1);
        int sys = -1;
        for (int i = 0; i < tracks.count(); ++i)
            if (tracks.trackIdAt(i) == QLatin1String("system")) sys = i;
        QVERIFY(sys >= 0);
        const QModelIndex idx = tracks.index(sys);
        QCOMPARE(tracks.data(idx, TrackListModel::ExcludedReasonRole).toString(), QStringLiteral("noSpeech"));
        QCOMPARE(tracks.data(idx, TrackListModel::SpeechRatioRole).toDouble(), 0.0);
        QCOMPARE(tracks.data(idx, TrackListModel::KindRole).toString(), QStringLiteral("loopback"));
        QVERIFY(!tracks.data(idx, TrackListModel::IncludedRole).toBool());

        tracks.setIncluded(sys, true);
        QTRY_COMPARE(tracks.includedCount(), 2);
        const tanara::Meeting after = m_app->store()->load(m.id);
        for (const tanara::Track& t : after.tracks)
            QVERIFY2(t.included(), qPrintable(t.id));
        tracks.setIncluded(0, false);
        QTRY_COMPARE(tracks.includedCount(), 1);
        QCOMPARE(m_app->store()->load(m.id).tracks.at(0).excludedReason, QStringLiteral("manual"));
    }

    // A fül-állapot a héjnak: van-e összefoglaló, fut-e átírás.
    void shellMeetingTabState()
    {
        const tanara::Meeting m = transcribed(QStringLiteral("Fülek"));
        ShellMeetingModel model;
        model.setController(m_app.get());
        model.setMeetingId(m.id);
        QVERIFY(model.hasTranscript());
        QVERIFY(!model.hasSummary());
        QCOMPARE(model.transcribePercent(), -1);
        QCOMPARE(model.summarizePercent(), -1);
    }

    // Demó (képernyőkép): a négy állapot kitalált adata.
    void demoStates()
    {
        m_app.reset();
        AppContext::instance()->setDemo(true);
        OverviewViewModel vm;
        vm.setDemoState(QStringLiteral("overviewProcessing"));
        QVERIFY(vm.approvalPending());
        QVERIFY(vm.transcribing());
        QCOMPARE(vm.candidateCount(), 4);
        QCOMPARE(vm.participants().size(), 6);
        QVERIFY(vm.participants().last().toMap().value("suggested").toBool());
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("state").toString(), QStringLiteral("attention"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("progress").toInt(), 62);
        // „Hozzáadás” / × a javaslaton.
        vm.removeParticipant(QStringLiteral("suggest:Szabó Bence"));
        QCOMPARE(vm.participants().size(), 5);

        vm.setDemoState(QStringLiteral("overviewDone"));
        QVERIFY(vm.allDone());
        QCOMPARE(vm.stats().size(), 6);
        QVERIFY(vm.approved());

        vm.setDemoState(QStringLiteral("overviewEmpty"));
        QCOMPARE(vm.peopleState(), QStringLiteral("empty"));
        QVERIFY(vm.contextNote().isEmpty());

        vm.setDemoState(QStringLiteral("overviewNobody"));
        QCOMPARE(vm.peopleState(), QStringLiteral("nobody"));
        QCOMPARE(vm.anonymousSpeakers().size(), 3);
        AppContext::instance()->setDemo(false);
    }
};

QTEST_MAIN(TestOverviewViewModel)
#include "test_overview_view_model.moc"

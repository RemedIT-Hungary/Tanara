// A felvevő nézetmodellje (tanara_qml::RecorderViewModel): a design-állapotok kitalált
// adattal (controller nélkül), a szint-skála, az automatikus név — és a felvevő QML-oldalai
// figyelmeztetés nélkül betöltődnek.
//
// ÉLŐ teszt (alapból kimarad): TANARA_LIVE_AUDIO_TEST=1 mellett egy ideiglenes mappába
// (TANARA_HOME = QTemporaryDir, a felhasználó adataihoz nem nyúl) valódi, pár másodperces
// felvételt készít a gép hangeszközeivel: szintek ≥ 20 Hz, sáv hozzáadása felvétel közben,
// leállítás → „Elmentve” csak a lezárt sávfájlok után, nincs lekeverés-indítás.
#include "AppContext.h"
#include "QmlApp.h"
#include "RecorderViewModel.h"
#include "RecorderWindowHost.h"

#include "tanara/AppController.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/TagService.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace tanara_qml;

class TestRecorderViewModel : public QObject {
    Q_OBJECT

    static QVariant role(RecorderViewModel& vm, int row, const char* name)
    {
        QAbstractItemModel* m = vm.devices();
        return m->data(m->index(row, 0), m->roleNames().key(name));
    }

    QTemporaryDir m_home;

private slots:
    void initTestCase()
    {
        // Biztonsági háló: a teszt semmilyen úton ne érje el a felhasználó ~/.tanara mappáját.
        QVERIFY(m_home.isValid());
        qputenv("TANARA_HOME", m_home.path().toUtf8());
        AppContext::instance()->setDemo(true);
    }

    void levelScaleIsLogarithmic()
    {
        QCOMPARE(RecorderViewModel::levelSegments(0.0f), 0);
        QCOMPARE(RecorderViewModel::levelSegments(0.0005f), 0);
        QCOMPARE(RecorderViewModel::levelSegments(1.0f), 14);
        QCOMPARE(RecorderViewModel::levelSegments(4.0f), 14);
        const int speech = RecorderViewModel::levelSegments(0.05f);   // ≈ −26 dBFS
        QVERIFY(speech >= 7 && speech <= 9);
        QVERIFY(RecorderViewModel::levelSegments(0.2f) > speech);
    }

    void automaticTitleNamesTheCall()
    {
        const QDateTime when(QDate(2026, 10, 3), QTime(14, 2));
        const QString plain = RecorderViewModel::automaticTitle(QString(), when);
        const QString teams = RecorderViewModel::automaticTitle(QStringLiteral("Teams"), when);
        QVERIFY(plain.startsWith(QStringLiteral("Megbeszélés · ")));
        QVERIFY(teams.startsWith(QStringLiteral("Teams-hívás · ")));
        QVERIFY(teams.endsWith(QStringLiteral("14:02")));
    }

    void demoStatesMatchTheDesign()
    {
        RecorderViewModel vm;
        vm.setDemoState(QStringLiteral("R01"));
        QCOMPARE(vm.state(), QStringLiteral("idle"));
        QVERIFY(vm.titleAutomatic());
        QCOMPARE(vm.deviceCount(), 6);
        QCOMPARE(vm.selectedCount(), 3);
        QVERIFY(vm.canStart());
        QCOMPARE(role(vm, 4, "status").toString(), QStringLiteral("signalUnrecorded"));
        QCOMPARE(role(vm, 2, "appName").toString(), QStringLiteral("Microsoft Teams"));

        vm.setDemoState(QStringLiteral("R03"));
        QCOMPARE(vm.state(), QStringLiteral("recording"));
        QCOMPARE(vm.trackCount(), 3);
        QCOMPARE(vm.elapsedText(), QStringLiteral("00:12:47"));
        QVERIFY(role(vm, 0, "locked").toBool());          // rögzített sáv: zárolt
        QVERIFY(!role(vm, 0, "toggleable").toBool());
        QVERIFY(role(vm, 1, "toggleable").toBool());      // nem rögzített: bekapcsolható
        QCOMPARE(role(vm, 3, "status").toString(), QStringLiteral("silentWarn"));

        vm.setDemoState(QStringLiteral("R06"));
        QVERIFY(vm.askVisible());
        vm.continueRecording();
        QVERIFY(!vm.askVisible());
        QCOMPARE(vm.state(), QStringLiteral("recording"));   // a „Folytatom” nem állít le

        vm.setDemoState(QStringLiteral("R09"));
        QCOMPARE(vm.state(), QStringLiteral("done"));
        QVERIFY(vm.doneProblem().isEmpty());
        QVERIFY(vm.doneSummary().startsWith(QStringLiteral("30:34")));

        vm.setDemoState(QStringLiteral("R10"));
        QCOMPARE(vm.state(), QStringLiteral("noDevice"));
        QVERIFY(!vm.canStart());
    }

    void togglingAndRenaming()
    {
        RecorderViewModel vm;
        vm.setDemoState(QStringLiteral("R01"));
        QSignalSpy counts(&vm, &RecorderViewModel::countsChanged);
        vm.toggleDevice(1);                                // kikapcsolt mikrofon be
        QCOMPARE(vm.selectedCount(), 4);
        vm.toggleDevice(1);
        QCOMPARE(vm.selectedCount(), 3);
        QVERIFY(counts.count() >= 2);

        vm.setTitle(QStringLiteral("  Heti státusz  "));
        QCOMPARE(vm.title(), QStringLiteral("Heti státusz"));
        QVERIFY(!vm.titleAutomatic());
        vm.setTitle(QString());                            // üresre nem írható
        QCOMPARE(vm.title(), QStringLiteral("Heti státusz"));

        // Felvétel közben a rögzített sáv nem kapcsolható ki, más bekapcsolható.
        vm.setDemoState(QStringLiteral("R04"));
        vm.toggleDevice(0);
        QVERIFY(role(vm, 0, "selected").toBool());
        vm.toggleDevice(1);
        QVERIFY(role(vm, 1, "locked").toBool());
        QCOMPARE(vm.trackCount(), 4);
    }

    void externalRequestSetsTitle()
    {
        RecorderViewModel vm;
        vm.setDemoState(QStringLiteral("R10"));
        vm.applyRequest(QString(), QStringLiteral("Zoom"), QString());
        QVERIFY(vm.titleAutomatic());
        QVERIFY(vm.title().startsWith(QStringLiteral("Zoom-hívás")));
        vm.applyRequest(QStringLiteral("Tervezés"), QString(), QString());
        QCOMPARE(vm.title(), QStringLiteral("Tervezés"));
        QVERIFY(!vm.titleAutomatic());
    }

    // C06: címkék indítás előtt és felvétel közben (kitalált adattal), Ctrl+T a nézetmodellen át.
    void tagsBeforeAndDuringRecording()
    {
        RecorderViewModel vm;
        vm.setDemoState(QStringLiteral("R01"));
        QVERIFY(vm.tagsEditable());
        QCOMPARE(vm.tagIds(), QStringList{QStringLiteral("t-nordvik")});

        QSignalSpy tags(&vm, &RecorderViewModel::tagsChanged);
        QVERIFY(vm.addTag(QStringLiteral("  q4 TERVEZÉS ")));              // a meglévő címke, kis/nagybetűtől függetlenül
        QCOMPARE(vm.tagIds(), (QStringList{QStringLiteral("t-nordvik"), QStringLiteral("t-q4")}));
        QCOMPARE(vm.tags().at(1).toMap().value("name").toString(), QStringLiteral("Q4 tervezés"));
        QVERIFY(!vm.addTag(QStringLiteral("Nordvik")));                    // már rajta van
        QVERIFY(!vm.addTag(QStringLiteral("   ")));
        QVERIFY(vm.addTag(QStringLiteral("Új ügyfél")));                   // új név
        QCOMPARE(vm.tags().size(), 3);
        vm.removeTag(QStringLiteral("t-nordvik"));
        QCOMPARE(vm.tags().first().toMap().value("id").toString(), QStringLiteral("t-q4"));
        QCOMPARE(tags.count(), 3);

        // Ctrl+T: a nézetmodell kéri a mező megnyitását.
        QSignalSpy input(&vm, &RecorderViewModel::tagInputRequested);
        QVERIFY(vm.openTagInput());
        QCOMPARE(input.count(), 1);

        // Felvétel közben is szerkeszthető.
        vm.setDemoState(QStringLiteral("R03"));
        QCOMPARE(vm.state(), QStringLiteral("recording"));
        QCOMPARE(vm.tagIds(), (QStringList{QStringLiteral("t-nordvik"), QStringLiteral("t-q4")}));
        QVERIFY(vm.tagsEditable());
        QVERIFY(vm.openTagInput());
        QVERIFY(vm.addTag(QStringLiteral("Partnerek")));
        QCOMPARE(vm.tags().size(), 3);

        // A kész felvételnél és eszköz nélkül nincs címke-sor.
        vm.setDemoState(QStringLiteral("R09"));
        QVERIFY(!vm.tagsEditable());
        QVERIFY(!vm.openTagInput());
        QVERIFY(!vm.addTag(QStringLiteral("Nordvik")));
        QCOMPARE(input.count(), 2);
        vm.setDemoState(QStringLiteral("R10"));
        QVERIFY(vm.tags().isEmpty());
    }

    // A core-ra kötve: a név a készletbe kerül (TagService::create), az id-k a felvétel
    // címkéiként a controllerhez (setRecordingTags); átnevezés / törlés követve. Hang-eszközhöz
    // nem nyúl (csak a címkék vannak bekötve).
    void tagsGoToTheController()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QByteArray oldHome = qgetenv("TANARA_HOME");
        qputenv("TANARA_HOME", home.path().toUtf8());
        {
            tanara::AppController c;
            tanara::TagService* svc = c.tags();
            QVERIFY(svc);
            const tanara::Tag existing = svc->create(QStringLiteral("Nordvik"));

            RecorderViewModel vm;
            vm.attachTagsOnly(&c);
            QTest::qWait(10);                                   // a sorba állított App.controller-keresés lefut
            QVERIFY(vm.tags().isEmpty());                       // nincs demó-adat
            QVERIFY(vm.addTag(QStringLiteral("nordvik")));      // a meglévő (kulcs-egyezés)
            QVERIFY(vm.addTag(QStringLiteral("Q4 tervezés")));  // új a készletben
            QCOMPARE(vm.tagIds().first(), existing.id);
            QVERIFY(svc->byName(QStringLiteral("Q4 tervezés")).isValid());
            QCOMPARE(c.recordingTags(), vm.tagIds());

            vm.removeTag(existing.id);
            QCOMPARE(c.recordingTags(), vm.tagIds());
            QCOMPARE(vm.tagIds().size(), 1);

            const QString q4 = vm.tagIds().first();
            QVERIFY(svc->rename(q4, QStringLiteral("Q4 terv")));
            QCOMPARE(vm.tags().first().toMap().value("name").toString(), QStringLiteral("Q4 terv"));
            svc->remove(q4);
            QVERIFY(vm.tags().isEmpty());
            QVERIFY(c.recordingTags().isEmpty());
        }
        qputenv("TANARA_HOME", oldHome);
    }

    void previewPagesLoadWithoutWarnings_data()
    {
        QTest::addColumn<QString>("state");
        QTest::addColumn<QString>("theme");
        for (const char* theme : {"light", "dark"})
            for (const char* st : {"R01", "R02", "R03", "R03typing", "R04", "R05", "R06", "R07", "R09", "R10"})
                QTest::addRow("%s-%s", st, theme) << QString::fromLatin1(st) << QString::fromLatin1(theme);
    }
    void previewPagesLoadWithoutWarnings()
    {
        QFETCH(QString, state);
        QFETCH(QString, theme);
        AppContext::instance()->setThemeMode(theme);
        QStringList warnings;
        QQmlApplicationEngine engine;
        connect(&engine, &QQmlEngine::warnings, &engine, [&warnings](const QList<QQmlError>& l) {
            for (const QQmlError& e : l) warnings << e.toString();
        });
        QVERIFY(loadPage(engine, QStringLiteral("RecorderPreview"),
                         {{QStringLiteral("demoState"), state}}, QSize(420, 640)));
        QTest::qWait(80);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    // A gazda controller nélkül is létrehozza az ablakot; a bezárás (nem fut felvétel) closed().
    void hostCreatesWindow()
    {
        RecorderWindowHost host(nullptr);
        QSignalSpy closed(&host, &RecorderWindowHost::closed);
        QVERIFY(host.show());
        QVERIFY(host.window());
        QVERIFY(host.viewModel());
        QCOMPARE(host.window()->width(), 380);
        QVERIFY(!host.recording());
        QVERIFY(QMetaObject::invokeMethod(host.window(), "closeRequested"));
        QCOMPARE(closed.count(), 1);
        QVERIFY(!host.isVisible());
    }

    // A főalkalmazásban a felvevő SAJÁT QML-motort kap a főablak motorja mellett. Az `App`
    // singleton folyamat-szintű, ezért a téma váltása mindkét motor Theme-jében megjelenik,
    // és a második motor sem ad figyelmeztetést.
    void hostWithOwnEngineFollowsTheme()
    {
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
        QStringList warnings;
        auto collect = [&warnings](const QList<QQmlError>& l) {
            for (const QQmlError& e : l) warnings << e.toString();
        };
        QQmlApplicationEngine mainEngine;            // a főablak motorja
        connect(&mainEngine, &QQmlEngine::warnings, &mainEngine, collect);
        QVERIFY(loadPage(mainEngine, QStringLiteral("RecorderPreview"), {}, QSize(420, 640)));
        QQmlComponent comp(&mainEngine);
        comp.loadFromModule("Tanara", "TDivider");
        std::unique_ptr<QObject> mainScope(comp.create());
        QVERIFY2(mainScope, qPrintable(comp.errorString()));

        RecorderWindowHost host(nullptr);            // engine = nullptr → saját motor
        QVERIFY(host.show());
        QQmlEngine* recEngine = qmlEngine(host.window());
        QVERIFY(recEngine && recEngine != &mainEngine);
        connect(recEngine, &QQmlEngine::warnings, recEngine, collect);

        auto bg = [](QObject* scope) {
            QQmlExpression e(qmlContext(scope), scope, QStringLiteral("Theme.bg"));
            return e.evaluate().value<QColor>();
        };
        const QColor light = bg(host.window());
        QVERIFY(light.isValid());
        QCOMPARE(bg(mainScope.get()), light);

        AppContext::instance()->setThemeMode(QStringLiteral("dark"));
        QTest::qWait(50);
        const QColor dark = bg(host.window());
        QVERIFY(dark != light);
        QCOMPARE(bg(mainScope.get()), dark);

        AppContext::instance()->setThemeMode(QStringLiteral("light"));
        QCOMPARE(bg(host.window()), light);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void liveRecording()
    {
        if (qEnvironmentVariableIntValue("TANARA_LIVE_AUDIO_TEST") != 1)
            QSKIP("élő hangeszköz kell hozzá — TANARA_LIVE_AUDIO_TEST=1 kapcsolja be");
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
            QSKIP("nincs ffmpeg");
        AppContext::instance()->setDemo(false);
        QTemporaryDir home;
        qputenv("TANARA_HOME", home.path().toUtf8());   // a user ~/.tanara-ja érintetlen
        {
            tanara::AppController c;
            c.setAutoMixdownAfterRecording(false);       // mint az önálló felvevő-folyamat
            RecorderViewModel vm;
            vm.setController(&c);
            if (vm.deviceCount() < 2)
                QSKIP("legalább két capture-eszköz kell");
            qInfo() << "eszközök:" << vm.deviceCount();

            // 1) Szintek MINDEN eszközre felvétel előtt, ≥ 20 Hz.
            QHash<QString, int> seen;
            auto conn = connect(&c, &tanara::AppController::deviceLevelPeak, this,
                                [&seen](const QString& n, float, float) { ++seen[n]; });
            QTest::qWait(1000);
            QCOMPARE(seen.size(), vm.deviceCount());
            for (auto it = seen.cbegin(); it != seen.cend(); ++it)
                QVERIFY2(it.value() >= 20, qPrintable(QStringLiteral("%1: %2 Hz").arg(it.key()).arg(it.value())));

            // 2) Csak az első eszközzel indulunk.
            for (int i = 0; i < vm.deviceCount(); ++i)
                if (role(vm, i, "selected").toBool() != (i == 0)) vm.toggleDevice(i);
            QCOMPARE(vm.selectedCount(), 1);
            QCOMPARE(c.lastUsedDeviceNames().size(), 1);   // a kijelölés mentve
            vm.setTitle(QStringLiteral("Élő teszt"));
            vm.start();
            QTRY_COMPARE_WITH_TIMEOUT(vm.state(), QStringLiteral("recording"), 8000);
            QCOMPARE(vm.trackCount(), 1);
            QVERIFY(role(vm, 0, "locked").toBool());

            // 3) Felvétel közben is jön szint a rögzített ÉS a nem rögzített eszközökről.
            seen.clear();
            QTest::qWait(1500);
            QCOMPARE(seen.size(), vm.deviceCount());
            for (auto it = seen.cbegin(); it != seen.cend(); ++it)
                QVERIFY2(it.value() >= 30, qPrintable(QStringLiteral("%1: %2 / 1,5 mp").arg(it.key()).arg(it.value())));

            // 4) Második eszköz bekapcsolása felvétel közben → új sáv.
            vm.toggleDevice(1);
            QTRY_COMPARE_WITH_TIMEOUT(vm.trackCount(), 2, 5000);
            QVERIFY(role(vm, 1, "locked").toBool());
            QVERIFY(vm.errorText().isEmpty());
            vm.setTitle(QStringLiteral("Élő teszt (átnevezve)"));   // átnevezés felvétel közben
            QTest::qWait(1500);

            // 5) Leállítás: előbb „stopping”, és „done” csak a lezárt fájlok után.
            QSignalSpy finished(&c, &tanara::AppController::recordingFinished);
            vm.stop();
            QCOMPARE(vm.state(), QStringLiteral("stopping"));
            QTRY_COMPARE_WITH_TIMEOUT(vm.state(), QStringLiteral("done"), 20000);
            QCOMPARE(finished.count(), 1);
            QVERIFY2(vm.doneProblem().isEmpty(), qPrintable(vm.doneProblem()));
            disconnect(conn);

            const tanara::Meeting m = c.store()->load(vm.doneMeetingId());
            QCOMPARE(m.title, QStringLiteral("Élő teszt (átnevezve)"));
            QCOMPARE(m.tracks.size(), 2);
            QVERIFY(m.folder.startsWith(home.path()));
            const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
            QList<double> durations;
            for (const tanara::Track& t : m.tracks) {
                const QString path = QDir(m.folder).absoluteFilePath(t.file);
                QVERIFY2(QFileInfo(path).size() > 0, qPrintable(path));
                if (ffprobe.isEmpty()) continue;
                QProcess p;
                p.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                                  QStringLiteral("format=duration"), QStringLiteral("-of"),
                                  QStringLiteral("csv=p=0"), path});
                QVERIFY(p.waitForFinished(10000));
                durations << p.readAllStandardOutput().trimmed().toDouble();
            }
            qInfo() << "sávok:" << m.tracks.size() << "hosszak (mp):" << durations << "meeting:" << m.durationMs;
            if (durations.size() == 2) {
                // A később indult sáv elejét csend tölti ki → a két fájl egyforma hosszú.
                QVERIFY(durations[0] > 2.5);
                QVERIFY(qAbs(durations[0] - durations[1]) < 0.4);
            }
            // Nincs (félbehagyott) lekeverés.
            QVERIFY(!QFileInfo::exists(QDir(m.folder).filePath(QStringLiteral("mixdown.mp3"))));
            QVERIFY(!QFileInfo::exists(QDir(m.folder).filePath(QStringLiteral("mixdown.part.mp3"))));

            // 6) „Új felvétel”: vissza üresjáratba, ugyanazokkal a forrásokkal.
            vm.newRecording();
            QCOMPARE(vm.state(), QStringLiteral("idle"));
            QVERIFY(vm.titleAutomatic());
            QCOMPARE(vm.selectedCount(), 2);
        }
        qputenv("TANARA_HOME", m_home.path().toUtf8());
        AppContext::instance()->setDemo(true);
    }
};

QTEST_MAIN(TestRecorderViewModel)
#include "test_recorder_view_model.moc"

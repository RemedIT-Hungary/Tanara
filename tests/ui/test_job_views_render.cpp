// A „nézetek” szelet QML-komponensei (PreTranscriptView, SummaryTab, TracksTab):
//  1. minden demó-állapot mindkét témában QML-figyelmeztetés nélkül betöltődik;
//  2. a gombok a szerződés (gui/qml/CONTRACT.md) szerinti ShellActions / PlayerController
//     hívásokat teszik — egy ál-héjjal és ál-lejátszóval ellenőrizve, valódi ablak nélkül;
//  3. (csak kérésre) képek egy ELDOBHATÓ mintaadat-készletről: ha a TANARA_VIEWS_SHOTS
//     környezeti változó egy mappára mutat ÉS a TANARA_HOME be van állítva, a készlet minden
//     meetingjének nézetei PNG-be kerülnek (a fájlnévben csak sorszám van). Különben kimarad.
#include "AppContext.h"
#include "QmlApp.h"

#include "tanara/AppController.h"
#include "tanara/Paths.h"
#include "tanara/store/MeetingStore.h"

#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

// A szerződés ShellActions-felülete — minden hívást naplóz.
class FakeShell : public QObject {
    Q_OBJECT
public:
    QStringList calls;
    bool confirmAnswer = true;
    QString pickedFile;

    Q_INVOKABLE void openSettings(const QString& page, const QString& focusField = QString())
    {
        calls << "openSettings:" + page;
        lastFocusField = focusField;
    }
    QString lastFocusField;
    Q_INVOKABLE void startTranscription(const QString& id) { calls << "startTranscription:" + id; }
    Q_INVOKABLE void startQuickSummary(const QString& id) { calls << "startQuickSummary:" + id; }
    Q_INVOKABLE void startTopicExtraction(const QString& id) { calls << "startTopicExtraction:" + id; }
    Q_INVOKABLE void startTopicAnalysis(const QString& id) { calls << "startTopicAnalysis:" + id; }
    Q_INVOKABLE void analyzeTopic(const QString& id, const QString& topic) { calls << "analyzeTopic:" + id + ":" + topic; }
    Q_INVOKABLE void cancelJob(const QString& id, int kind) { calls << QStringLiteral("cancelJob:%1:%2").arg(id).arg(kind); }
    Q_INVOKABLE void revealInFolder(const QString& id) { calls << "revealInFolder:" + id; }
    Q_INVOKABLE QString pickAudioFile() { calls << "pickAudioFile"; return pickedFile; }
    Q_INVOKABLE bool confirm(const QString& title, const QString&, const QString&, bool danger)
    {
        calls << QStringLiteral("confirm:%1:%2").arg(title).arg(danger);
        return confirmAnswer;
    }
    Q_INVOKABLE void seekTo(const QString& id, int ms) { calls << QStringLiteral("seekTo:%1:%2").arg(id).arg(ms); }
    Q_INVOKABLE void toast(const QString&) { calls << "toast"; }
};

// A szerződés PlayerController-felületéből az előnézethez kellő rész.
class FakePlayer : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString previewPath READ previewPath NOTIFY previewPathChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY previewPathChanged)
public:
    QString previewPath() const { return m_preview; }
    bool playing() const { return !m_preview.isEmpty(); }
    Q_INVOKABLE void playFile(const QString& path) { m_preview = path; emit previewPathChanged(); }
    Q_INVOKABLE void stopPreview() { m_preview.clear(); emit previewPathChanged(); }
    Q_INVOKABLE void pause() {}
signals:
    void previewPathChanged();
private:
    QString m_preview;
};

class TestJobViewsRender : public QObject {
    Q_OBJECT

    // Betölt egy komponenst ablakba, opcionálisan PNG-t ment; visszaadja a QML-figyelmeztetéseket.
    static QStringList render(const QString& page, const QVariantMap& props, const QString& png = QString(),
                              int waitMs = 150, const QSize& size = QSize(1004, 640))
    {
        QStringList warnings;
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                         [&warnings](const QList<QQmlError>& list) {
                             for (const QQmlError& e : list) warnings << e.toString();
                         });
        if (!loadPage(engine, page, props, size)) {
            warnings << QStringLiteral("nem tölthető be: %1").arg(page);
            return warnings;
        }
        QTest::qWait(waitMs);
        if (!png.isEmpty()) {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
            if (!window) {
                // A loadPage a gyökeret a motor gyerekeként hozza létre.
                window = engine.findChild<QQuickWindow*>();
            }
            if (!window || !window->grabWindow().save(png))
                warnings << QStringLiteral("nem menthető: %1").arg(png);
        }
        return warnings;
    }

    // Egy komponens példánya ablak nélkül (függvény-hívásokhoz).
    static std::unique_ptr<QObject> create(QQmlEngine& engine, const QString& page, const QVariantMap& props)
    {
        setupEngine(engine);
        QQmlComponent comp(&engine);
        comp.loadFromModule("Tanara", page);
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties(props));
        if (!obj)
            qWarning().noquote() << comp.errorString();
        return obj;
    }

private slots:
    void initTestCase() { AppContext::instance()->setDemo(true); }

    void demoStatesLoadWithoutWarnings_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("theme");
        QTest::addColumn<QString>("state");
        const QList<QPair<const char*, QStringList>> pages{
            {"PreTranscriptView", {"steps", "ready", "cloud", "mixing", "running", "uploading", "failed"}},
            {"SummaryTab", {"stale", "done", "memo", "memoShort", "oldSummary", "oldMemo", "running", "topicsDoc", "empty",
                            "emptyBlocked", "emptyRunning", "emptyRunningParts", "emptyRunningMerge", "emptyError",
                            "emptyErrorKept", "topics"}},
            {"TracksTab", {"default", "loading", "idle"}},
        };
        for (const char* theme : {"light", "dark"})
            for (const auto& p : pages)
                for (const QString& st : p.second)
                    QTest::addRow("%s-%s-%s", p.first, qPrintable(st), theme)
                        << QString::fromLatin1(p.first) << QString::fromLatin1(theme) << st;
    }
    void demoStatesLoadWithoutWarnings()
    {
        QFETCH(QString, page);
        QFETCH(QString, theme);
        QFETCH(QString, state);
        AppContext::instance()->setThemeMode(theme);
        const QStringList warnings = render(page, {{"demoState", state}});
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void narrowWidthLoadsWithoutWarnings()
    {
        // A minimális ablakszélességnél (960 − 276 oldalsáv) sem törik szét egyik nézet sem.
        for (const char* page : {"PreTranscriptView", "SummaryTab", "TracksTab"}) {
            const QStringList warnings = render(QString::fromLatin1(page), {}, QString(), 100, QSize(684, 520));
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
        }
    }

    void nullShellAndPlayerAreTolerated()
    {
        // Önállóan (shell / player nélkül) a műveletek csendben nem csinálnak semmit.
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError>& l) {
            for (const QQmlError& e : l) warnings << e.toString();
        });
        auto pre = create(engine, "PreTranscriptView", {{"demoState", "ready"}});
        QVERIFY(pre);
        QVERIFY(QMetaObject::invokeMethod(pre.get(), "start"));
        auto tracks = create(engine, "TracksTab", {});
        QVERIFY(tracks);
        QVERIFY(QMetaObject::invokeMethod(tracks.get(), "deleteDropped"));
        QVERIFY(QMetaObject::invokeMethod(tracks.get(), "locate", Q_ARG(QVariant, 3)));
        QVERIFY(QMetaObject::invokeMethod(tracks.get(), "togglePreview", Q_ARG(QVariant, QStringLiteral("/x"))));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    void actionsGoThroughTheShell()
    {
        QQmlEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError>& l) {
            for (const QQmlError& e : l) warnings << e.toString();
        });
        FakeShell shell;
        FakePlayer player;
        const QVariant shellVar = QVariant::fromValue<QObject*>(&shell);
        const QVariant playerVar = QVariant::fromValue<QObject*>(&player);

        // Átirat előtt: az indítás és a „Szolgáltató beállítása” a héjon megy át.
        auto pre = create(engine, "PreTranscriptView", {{"demoState", "ready"}, {"shell", shellVar}, {"meetingId", "m1"}});
        QVERIFY(pre);
        QVERIFY(QMetaObject::invokeMethod(pre.get(), "start"));
        QVERIFY(QMetaObject::invokeMethod(pre.get(), "openSettings", Q_ARG(QVariant, QStringLiteral("providers"))));
        QCOMPARE(shell.calls, QStringList({"startTranscription:m1", "openSettings:providers"}));
        QCOMPARE(shell.lastFocusField, QStringLiteral("stt"));   // B04: az átíró kártyájához visz
        shell.calls.clear();

        // Sávok: előnézet a lejátszón át (ugyanarra a sávra újra → leáll).
        auto tracks = create(engine, "TracksTab", {{"shell", shellVar}, {"player", playerVar}, {"meetingId", "m1"}});
        QVERIFY(tracks);
        QVERIFY(QMetaObject::invokeMethod(tracks.get(), "togglePreview", Q_ARG(QVariant, QStringLiteral("/demo/track-01.ogg"))));
        QCOMPARE(player.previewPath(), QStringLiteral("/demo/track-01.ogg"));
        QVERIFY(QMetaObject::invokeMethod(tracks.get(), "togglePreview", Q_ARG(QVariant, QStringLiteral("/demo/track-01.ogg"))));
        QCOMPARE(player.previewPath(), QString());

        // Eldobottak törlése: megerősítés nélkül nem történik semmi; megerősítve törlés + értesítés.
        shell.confirmAnswer = false;
        QVERIFY(QMetaObject::invokeMethod(tracks.get(), "deleteDropped"));
        QCOMPARE(shell.calls.size(), 1);
        QVERIFY(shell.calls[0].startsWith("confirm:"));
        QVERIFY(shell.calls[0].endsWith(":1"));                 // veszélyes művelet
        shell.calls.clear();
        shell.confirmAnswer = true;
        QVERIFY(QMetaObject::invokeMethod(tracks.get(), "deleteDropped"));
        QCOMPARE(shell.calls.size(), 2);
        QCOMPARE(shell.calls[1], QStringLiteral("toast"));
        shell.calls.clear();

        // Megkeresés: a fájlválasztó a héjé; megszakítva (üres út) nincs további hívás.
        QVERIFY(QMetaObject::invokeMethod(tracks.get(), "locate", Q_ARG(QVariant, 0)));
        QCOMPARE(shell.calls, QStringList({"pickAudioFile"}));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    }

    // ---- képek eldobható mintaadatról (csak kérésre) ----
    void sandboxShots()
    {
        const QString outDir = qEnvironmentVariable("TANARA_VIEWS_SHOTS");
        const QString home = tanara::paths::homeOverride();
        if (outDir.isEmpty() || home.isEmpty())
            QSKIP("TANARA_VIEWS_SHOTS + TANARA_HOME nincs beállítva (csak kézi ellenőrzéshez kell)");
        QVERIFY2(!home.startsWith(QDir::homePath() + "/.tanara"), "valódi adatra nem futhat");
        QVERIFY(QDir().mkpath(outDir));
        qputenv("TANARA_CLOUD", "off");

        tanara::AppController app;
        AppContext* ctx = AppContext::instance();
        ctx->setDemo(false);
        ctx->setController(&app);
        QVector<tanara::Meeting> meetings = app.store()->loadAll();
        if (meetings.isEmpty()) {            // friss másolat: az index még nem épült fel
            app.store()->rebuildIndexFromDisk();
            meetings = app.store()->loadAll();
        }
        int n = 0;
        for (const tanara::Meeting& brief : meetings) {
            const tanara::Meeting m = app.store()->load(brief.id);
            ++n;
            for (const char* theme : {"light", "dark"}) {
                ctx->setThemeMode(QString::fromLatin1(theme));
                auto name = [&](const char* view) {
                    return QDir(outDir).filePath(QStringLiteral("m%1-%2-%3.png").arg(n).arg(QLatin1String(view), QLatin1String(theme)));
                };
                QStringList warnings;
                if (!m.hasTranscript) {
                    warnings += render("PreTranscriptView", {{"meetingId", m.id}}, name("pre"));
                } else {
                    warnings += render("SummaryTab", {{"meetingId", m.id}}, name("summary"), 250, QSize(1004, 900));
                    if (!app.meetingTopics(m.id).isEmpty())
                        warnings += render("SummaryTab", {{"meetingId", m.id}, {"topicsOpen", true}},
                                           name("topics"), 250, QSize(1004, 900));
                }
                // A hullámformák számolása ffmpeg-gel megy: bőven hagyunk rá időt.
                warnings += render("TracksTab", {{"meetingId", m.id}}, name("tracks"),
                                   theme == QByteArray("light") ? 6000 : 400);
                QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
            }
        }
        ctx->setController(nullptr);
        ctx->setDemo(true);
        qInfo("%d meeting nézetei mentve", n);
    }
};

QTEST_MAIN(TestJobViewsRender)
#include "test_job_views_render.moc"

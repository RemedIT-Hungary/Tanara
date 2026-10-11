// Sávok meghallgatása (G6) kijelző nélkül:
//  - PlayerController: a logikai sáv szakaszának feloldása megbeszélés-időből (resolveTrackSeek),
//    sáv-lejátszás valódi AppControllerrel és ál-motorral (ugrás a 2. szakaszba, szünet / folytatás,
//    a szakasz végén a következő szakasz);
//  - a beszéd-csík számítása (TrackActivityModel::laneFrom, TrackActivityItem::columns);
//  - ParticipantTrackBinder demó-adattal (következtetett → kézi kötés, vissza);
//  - QML: a „Sávok részletei” (TracksTab) és a kompakt sor (TrackRow compact, ahogy az Áttekintés
//    SÁVOK listája használja) — kattintás a csíkra = ugrás + lejátszás azon a sávon, a kapcsoló
//    mindkét helyen; a sáv-chip kattintása a kötést váltja.
// G6_SHOT_DIR beállítva: a kompakt sorok + chipek képernyőképe (világos és sötét) oda kerül.
#include "AppContext.h"
#include "LibraryDemoData.h"
#include "ParticipantTrackBinder.h"
#include "PlayerController.h"
#include "QmlApp.h"
#include "TrackActivityItem.h"
#include "TrackActivityModel.h"

#include "tanara/AppController.h"
#include "tanara/store/MeetingStore.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <limits>
#include <memory>

using namespace tanara_qml;

namespace {

class FakeBackend : public PlayerBackend {
public:
    using PlayerBackend::PlayerBackend;
    static inline FakeBackend* last = nullptr;
    QStringList sources;
    qint64 pos = 0;
    bool playingNow = false;
    void setSource(const QString& path, qint64) override
    {
        sources << path;
        playingNow = false;
        pos = 0;
        emit durationChanged(60000);
    }
    void play() override { playingNow = true; emit playingChanged(true); }
    void pause() override { if (playingNow) { playingNow = false; emit playingChanged(false); } }
    void setPosition(qint64 ms) override { pos = ms; }
    qint64 position() const override { return pos; }
    qint64 duration() const override { return 60000; }
    bool isPlaying() const override { return playingNow; }
    void setRate(qreal) override {}
    void setVolume(qreal) override {}
    void finish() { playingNow = false; emit playingChanged(false); emit finished(); }
};

tanara::Track track(const QString& id, const QString& device, qint64 offset, const QString& file,
                    tanara::TrackKind kind = tanara::TrackKind::Mic)
{
    tanara::Track t;
    t.id = id;
    t.deviceName = device;
    t.startOffsetMs = offset;
    t.file = file;
    t.kind = kind;
    return t;
}

void collect(QQuickItem* root, const QString& name, QList<QQuickItem*>& out)
{
    if (!root) return;
    if (root->objectName() == name) out << root;
    for (QQuickItem* child : root->childItems()) collect(child, name, out);
}

QList<QQuickItem*> visibleItems(QQuickItem* root, const char* name)
{
    QList<QQuickItem*> all, out;
    collect(root, QLatin1String(name), all);
    for (QQuickItem* it : all)
        if (it->isVisible()) out << it;
    return out;
}

// A harness: a kompakt sávsorok (mint az Áttekintés SÁVOK listáján) + egy résztvevő sáv-chipjei.
const char* kCompactHarness = R"(
import QtQuick
import QtQuick.Window
import Tanara
Window {
    id: win
    width: 340; height: 340
    color: Theme.bg
    property var player: null
    TrackListModel { id: tm; demoState: "overviewDone" }
    TrackActivityModel { id: am; demoState: "overviewDone" }
    ParticipantTrackBinder { id: binder; objectName: "binder" }
    Column {
        x: 10; y: 10
        width: 320
        spacing: 10
        Rectangle {
            width: parent.width
            height: rows.implicitHeight
            radius: Theme.radiusPopup
            color: Theme.raised
            border.width: 1
            border.color: Theme.border
            Column {
                id: rows
                width: parent.width
                Repeater {
                    model: tm
                    TrackRow {
                        width: rows.width
                        compact: true
                        tracks: tm
                        player: win.player
                        activity: am
                        sideWho: ({ mic: "Kovács Lilla", loopback: "4 résztvevő" })
                    }
                }
            }
        }
        Row {
            spacing: 8
            TLabel { text: "Fehér Gábor"; font.weight: Theme.weightSemiBold; anchors.verticalCenter: parent.verticalCenter }
            ParticipantTrackChips { binder: binder; participantId: "p2" }
        }
        Row {
            spacing: 8
            TLabel { text: "Varga Árpád"; font.weight: Theme.weightSemiBold; anchors.verticalCenter: parent.verticalCenter }
            ParticipantTrackChips { binder: binder; participantId: "p3" }
        }
    }
}
)";

} // namespace

class TestTrackListening : public QObject {
    Q_OBJECT

    QStringList m_warnings;

    void click(QQuickWindow* w, QQuickItem* it, qreal fx = 0.5)
    {
        QVERIFY(it);
        const QPoint p = it->mapToScene(QPointF(it->width() * fx, it->height() / 2)).toPoint();
        QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, p);
        QTest::qWait(60);
    }
    void watch(QQmlEngine& engine)
    {
        m_warnings.clear();
        connect(&engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError>& list) {
            for (const QQmlError& e : list) m_warnings << e.toString();
        });
    }

private slots:
    void init()
    {
        AppContext::instance()->setController(nullptr);
        AppContext::instance()->setDemo(true);
        AppContext::instance()->setThemeMode(QStringLiteral("light"));
        PlayerController::setBackendFactory({});
        FakeBackend::last = nullptr;
    }

    void resolveSeekAcrossSegments()
    {
        const QVector<tanara::Track> tracks = {
            track("mic", "Jabra", 0, "a.ogg"),
            track("loop", "Monitor", 0, "l.ogg", tanara::TrackKind::Loopback),
            track("mic-2", "Jabra", 60000, "b.ogg"),
            track("late", "USB", 10000, "u.ogg"),
        };
        auto s = PlayerController::resolveTrackSeek(tracks, "mic", 30000);
        QCOMPARE(s.index, 0);
        QCOMPARE(s.leaderId, QStringLiteral("mic"));
        QCOMPARE(s.fileMs, 30000);
        s = PlayerController::resolveTrackSeek(tracks, "mic", 70000);
        QCOMPARE(s.index, 2);
        QCOMPARE(s.offsetMs, 60000);
        QCOMPARE(s.fileMs, 10000);
        s = PlayerController::resolveTrackSeek(tracks, "mic-2", 5000);   // szakasz-id → a vezető
        QCOMPARE(s.leaderId, QStringLiteral("mic"));
        QCOMPARE(s.index, 0);
        QCOMPARE(s.fileMs, 5000);
        s = PlayerController::resolveTrackSeek(tracks, "late", 2000);    // a kezdete előtt → az eleje
        QCOMPARE(s.index, 3);
        QCOMPARE(s.fileMs, 0);
        QCOMPARE(PlayerController::resolveTrackSeek(tracks, "nincs", 0).index, -1);
    }

    void playTrackOnRealMeeting()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("TANARA_HOME", home.path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        AppContext::instance()->setDemo(false);
        PlayerController::setBackendFactory([](QObject* parent) -> PlayerBackend* {
            return FakeBackend::last = new FakeBackend(parent);
        });
        {
            tanara::AppController app;
            tanara::Meeting m = app.store()->createMeeting(QStringLiteral("Sávok"));
            for (const char* f : {"a.ogg", "b.ogg", "l.ogg"}) {
                QFile file(QDir(m.folder).filePath(QLatin1String(f)));
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write("x");
            }
            m.tracks = {track("mic", "Jabra", 0, "a.ogg"),
                        track("loop", "Monitor", 0, "l.ogg", tanara::TrackKind::Loopback),
                        track("mic-2", "Jabra", 60000, "b.ogg")};
            m.durationMs = 120000;
            app.store()->saveMeeting(m);

            PlayerController p;
            p.setController(&app);
            p.setMeetingId(m.id);
            p.seek(5000);
            p.playTrack(QStringLiteral("mic"), 70000);
            FakeBackend* b = FakeBackend::last;
            QVERIFY(b);
            QVERIFY(b->sources.last().endsWith(QLatin1String("b.ogg")));
            QCOMPARE(b->pos, 10000);
            QCOMPARE(p.previewTrackId(), QStringLiteral("mic"));
            QCOMPARE(p.previewMeetingMs(), 70000);
            QVERIFY(p.previewPlaying());
            QVERIFY(!p.playing());
            QCOMPARE(p.positionMs(), 5000);          // a lekeverés pozíciója érintetlen
            b->pos = 12000;
            QTRY_COMPARE(p.previewMeetingMs(), 72000);

            // Szünet / folytatás: a forrás és a pozíció megmarad.
            p.toggleTrack(QStringLiteral("mic"));
            QVERIFY(!p.previewPlaying());
            QCOMPARE(p.previewTrackId(), QStringLiteral("mic"));
            const int sourcesBefore = int(b->sources.size());
            p.toggleTrack(QStringLiteral("mic"));
            QVERIFY(p.previewPlaying());
            QCOMPARE(int(b->sources.size()), sourcesBefore);

            // Másik sáv: az ő fájlja, megbeszélés-időben.
            p.playTrack(QStringLiteral("loop"), 3000);
            QVERIFY(b->sources.last().endsWith(QLatin1String("l.ogg")));
            QCOMPARE(p.previewTrackId(), QStringLiteral("loop"));
            QCOMPARE(b->pos, 3000);

            // Az első szakasz vége → a következő szakasz az elejétől; az utolsó vége → kész.
            p.playTrack(QStringLiteral("mic"), 1000);
            QVERIFY(b->sources.last().endsWith(QLatin1String("a.ogg")));
            b->finish();
            QVERIFY(b->sources.last().endsWith(QLatin1String("b.ogg")));
            QCOMPARE(p.previewMeetingMs(), 60000);
            QVERIFY(p.previewPlaying());
            b->finish();
            QCOMPARE(p.previewTrackId(), QString());
            QCOMPARE(p.previewPath(), QString());

            // Hiányzó fájl: hiba, nincs előnézet.
            QFile::remove(QDir(m.folder).filePath("l.ogg"));
            QSignalSpy errors(&p, &PlayerController::errorOccurred);
            p.playTrack(QStringLiteral("loop"), 0);
            QCOMPARE(errors.count(), 1);
            QCOMPARE(p.previewTrackId(), QString());
        }
        qunsetenv("TANARA_HOME");
        AppContext::instance()->setDemo(true);
    }

    void speechFramesAndColumns()
    {
        tanara::TrackActivity t;
        t.trackId = "mic";
        t.frameMs = 1000;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (int i = 0; i < 10; ++i) t.dbAboveFloor << 30.0f;
        for (int i = 0; i < 10; ++i) t.dbAboveFloor << 2.0f;
        for (int i = 0; i < 5; ++i) t.dbAboveFloor << nan;
        for (int i = 0; i < 10; ++i) t.dbAboveFloor << 25.0f;   // a beszédszint −10 dB-en belül
        for (int i = 0; i < 4; ++i) t.dbAboveFloor << 12.0f;    // aktív, de messze a beszédszint alatt
        const TrackActivityModel::Lane lane = TrackActivityModel::laneFrom(t);
        QCOMPARE(lane.frames.size(), 39);
        QCOMPARE(lane.speechFrames, 20);
        QCOMPARE(lane.frames.at(0), qint8(TrackActivityModel::Speech));
        QCOMPARE(lane.frames.at(12), qint8(TrackActivityModel::Quiet));
        QCOMPARE(lane.frames.at(22), qint8(TrackActivityModel::NoData));
        QCOMPARE(lane.frames.at(30), qint8(TrackActivityModel::Speech));
        QCOMPARE(lane.frames.at(36), qint8(TrackActivityModel::Quiet));

        // Oszlopok: [S,S,Q,Q,N,N,S,Q] 1 s-os keretek, 8 s, 4 oszlop.
        const QVector<qint8> f = {1, 1, 0, 0, -1, -1, 1, 0};
        QCOMPARE(TrackActivityItem::columns(f, 0, 1000, 8000, 4), (QVector<qreal>{1.0, 0.0, -1.0, 0.5}));
        // Később kezdődő sáv (origin 2 s) 10 s-os idővonalon, 5 oszlop: az első üres.
        const QVector<qreal> late = TrackActivityItem::columns(f, 2000, 1000, 10000, 5);
        QCOMPARE(late.at(0), -1.0);
        QCOMPARE(late.at(1), 1.0);
        QCOMPARE(late.at(2), 0.0);
        QCOMPARE(late.at(3), -1.0);
        QCOMPARE(late.at(4), 0.5);
        // Sok keret, kevés oszlop (2 óra × 50 ms): gyors és helyes arány.
        QVector<qint8> big(144000, 0);
        for (int i = 0; i < 144000; i += 4) big[i] = 1;
        QElapsedTimer timer;
        timer.start();
        const QVector<qreal> cols = TrackActivityItem::columns(big, 0, 50, 7200000, 600);
        QVERIFY(timer.elapsed() < 200);
        QCOMPARE(cols.size(), 600);
        QVERIFY(std::abs(cols.at(300) - 0.25) < 0.01);
    }

    void demoModelNextSpeech()
    {
        TrackActivityModel m;
        m.setDemoState(QStringLiteral("overviewDone"));
        QCOMPARE(m.state(), QStringLiteral("ready"));
        QVERIFY(m.hasTrack(QStringLiteral("mic")));
        QVERIFY(!m.hasTrack(QStringLiteral("nincs")));
        const int first = m.nextSpeechMs(QStringLiteral("mic"), 0);
        QVERIFY(first >= 0);
        QVERIFY(m.nextSpeechMs(QStringLiteral("mic"), first) == first);
        QCOMPARE(m.nextSpeechMs(QStringLiteral("system"), 0), -1);   // beszéd nélküli sáv
        QVERIFY(m.speechShare(QStringLiteral("call")) > m.speechShare(QStringLiteral("system")));
        m.setDemoState(QStringLiteral("loading"));
        QCOMPARE(m.state(), QStringLiteral("loading"));
        QVERIFY(!m.hasTrack(QStringLiteral("mic")));
    }

    void binderDemoToggles()
    {
        ParticipantTrackBinder b;
        QVERIFY(b.available());
        auto chip = [&b](const QString& id, int i) { return b.chips(id).at(i).toMap(); };
        QCOMPARE(b.chips(QStringLiteral("p1")).size(), 2);
        QVERIFY(chip("p1", 0).value("on").toBool());            // következtetett: mikrofon
        QVERIFY(!chip("p1", 0).value("manual").toBool());
        QVERIFY(!chip("p1", 1).value("on").toBool());
        QVERIFY(b.chips(QStringLiteral("suggest:Szabó Bence")).isEmpty());

        QVERIFY(b.toggle(QStringLiteral("p1"), QStringLiteral("call")));   // + hívás → kézi, mindkettő
        QVERIFY(chip("p1", 0).value("on").toBool() && chip("p1", 1).value("on").toBool());
        QVERIFY(chip("p1", 0).value("manual").toBool());
        QVERIFY(b.toggle(QStringLiteral("p1"), QStringLiteral("mic")));    // csak a hívás
        QVERIFY(!chip("p1", 0).value("on").toBool());
        QVERIFY(b.toggle(QStringLiteral("p1"), QStringLiteral("call")));   // üres → vissza a következtetésre
        QVERIFY(chip("p1", 0).value("on").toBool());
        QVERIFY(!chip("p1", 0).value("manual").toBool());
        // A következtetett egyetlen sáv kattintása = megerősítés.
        QVERIFY(b.toggle(QStringLiteral("p2"), QStringLiteral("call")));
        QVERIFY(chip("p2", 1).value("on").toBool());
        QVERIFY(chip("p2", 1).value("manual").toBool());
        QVERIFY(b.reset(QStringLiteral("p2")));
        QVERIFY(!chip("p2", 1).value("manual").toBool());
    }

    // „Sávok részletei”: csík-kattintás → ugrás + lejátszás azon a sávon; kapcsoló → kivétel, és
    // a kimaradt sornál „Beemelem”.
    void detailsStripSeekAndSwitch()
    {
        PlayerController player;
        player.setMeetingId(demo::defaultMeetingId());
        QVERIFY(player.durationMs() > 60000);
        QQmlApplicationEngine engine;
        watch(engine);
        QVERIFY(loadPage(engine, QStringLiteral("TracksTab"),
                         {{"demoState", "overviewDone"}, {"player", QVariant::fromValue<QObject*>(&player)}},
                         QSize(1100, 500)));
        auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        if (!w) w = engine.findChild<QQuickWindow*>();
        QVERIFY(w);
        QVERIFY(QTest::qWaitForWindowExposed(w));
        QTest::qWait(150);

        const QList<QQuickItem*> rows = visibleItems(w->contentItem(), "detailTrack");
        QCOMPARE(rows.size(), 3);
        QList<QQuickItem*> strips = visibleItems(rows.at(1), "trackActivityStrip");
        QCOMPARE(strips.size(), 1);
        QCOMPARE(strips.first()->height(), 40.0);
        click(w, strips.first(), 0.1);
        QCOMPARE(player.previewTrackId(), QStringLiteral("call"));
        QVERIFY(player.previewPlaying());
        // A csík az ACTIVITY idővonalán (a demó: 30:34) ugrik.
        const qreal expected = 0.1 * ((30 * 60 + 34) * 1000);
        QVERIFY2(std::abs(player.previewMeetingMs() - expected) < expected * 0.05,
                 qPrintable(QString::number(player.previewMeetingMs())));
        // A lejátszás-fej a csíkon.
        QVERIFY(!visibleItems(strips.first(), "trackActivityPlayhead").isEmpty());

        // Lejátszás gomb ugyanazon a sávon = szünet.
        click(w, visibleItems(rows.at(1), "trackPlay").value(0));
        QVERIFY(!player.previewPlaying());
        // A mikrofon lejátszása az első beszédnél kezd.
        click(w, visibleItems(rows.at(0), "trackPlay").value(0));
        QCOMPARE(player.previewTrackId(), QStringLiteral("mic"));
        TrackActivityModel probe;
        probe.setDemoState(QStringLiteral("overviewDone"));
        const int firstSpeech = probe.nextSpeechMs(QStringLiteral("mic"), 0);
        // (a néma motor órája közben halad)
        QVERIFY2(player.previewMeetingMs() >= firstSpeech && player.previewMeetingMs() < firstSpeech + 1000,
                 qPrintable(QString::number(player.previewMeetingMs())));
        player.stopPreview();

        // Kapcsoló → kivéve: a sor „Beemelem”-et mutat, arra vissza.
        QVERIFY(visibleItems(rows.at(0), "trackInclude").isEmpty());
        click(w, visibleItems(rows.at(0), "trackSwitch").value(0));
        QTRY_VERIFY(!visibleItems(rows.at(0), "trackInclude").isEmpty());
        click(w, visibleItems(rows.at(0), "trackInclude").value(0));
        QTRY_VERIFY(visibleItems(rows.at(0), "trackInclude").isEmpty());
        QVERIFY(!visibleItems(rows.at(0), "trackSwitch").isEmpty());
        // Végleges törlés: a gomb megvan (shell nélkül tiltott).
        QVERIFY(!visibleItems(rows.at(0), "trackDelete").isEmpty());
        QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QLatin1Char('\n'))));
    }

    // A kompakt sor (az Áttekintés SÁVOK listájának komponense) és a sáv-chipek.
    void compactRowsAndChips()
    {
        PlayerController player;
        player.setMeetingId(demo::defaultMeetingId());
        QQmlEngine engine;
        watch(engine);
        setupEngine(engine);
        QQmlComponent comp(&engine);
        comp.setData(kCompactHarness, QUrl(QStringLiteral("qrc:/qt/qml/Tanara/G6Harness.qml")));
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties(
            {{"player", QVariant::fromValue<QObject*>(&player)}}));
        QVERIFY2(obj, qPrintable(comp.errorString()));
        auto* w = qobject_cast<QQuickWindow*>(obj.get());
        QVERIFY(w);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        QTest::qWait(150);

        const QList<QQuickItem*> rows = visibleItems(w->contentItem(), "overviewTrack");
        QCOMPARE(rows.size(), 3);
        const QList<QQuickItem*> strips = visibleItems(rows.at(0), "trackActivityStrip");
        QCOMPARE(strips.size(), 1);
        QCOMPARE(strips.first()->height(), 18.0);
        // A kimaradt (beszéd nélküli) sornál is van csík (meghallgatható a döntéshez) és „Beemelem”.
        QCOMPARE(visibleItems(rows.at(2), "trackActivityStrip").size(), 1);
        QCOMPARE(visibleItems(rows.at(2), "trackInclude").size(), 1);

        click(w, visibleItems(rows.at(2), "trackActivityStrip").value(0), 0.5);
        QCOMPARE(player.previewTrackId(), QStringLiteral("system"));
        QVERIFY(player.previewPlaying());

        // Kapcsoló a kompakt soron is.
        click(w, visibleItems(rows.at(1), "trackSwitch").value(0));
        QTRY_VERIFY(!visibleItems(rows.at(1), "trackInclude").isEmpty());
        click(w, visibleItems(rows.at(1), "trackInclude").value(0));
        QTRY_VERIFY(visibleItems(rows.at(1), "trackInclude").isEmpty());

        if (const QByteArray dir = qgetenv("G6_SHOT_DIR"); !dir.isEmpty()) {
            player.stopPreview();
            player.playTrack(QStringLiteral("call"), 400000);
            player.toggleTrack(QStringLiteral("call"));      // szünet: álló lejátszás-fej
            QTest::qWait(100);
            w->grabWindow().save(QString::fromLocal8Bit(dir) + "/G6_compact_light.png");
            AppContext::instance()->setThemeMode(QStringLiteral("dark"));
            QTest::qWait(150);
            w->grabWindow().save(QString::fromLocal8Bit(dir) + "/G6_compact_dark.png");
            AppContext::instance()->setThemeMode(QStringLiteral("light"));
        }

        // Sáv-chip: Fehér Gábor (p2) következtetett hívás → a mikrofon chip kattintására kézi kötés.
        auto* binder = obj->findChild<ParticipantTrackBinder*>(QStringLiteral("binder"));
        QVERIFY(binder);
        const QList<QQuickItem*> chipSets = visibleItems(w->contentItem(), "participantTrackChips");
        QCOMPARE(chipSets.size(), 2);
        QVERIFY(!binder->chips("p2").at(0).toMap().value("manual").toBool());
        click(w, visibleItems(chipSets.at(0), "participantTrackChip_mic").value(0));
        const QVariantList chips = binder->chips("p2");
        QVERIFY(chips.at(0).toMap().value("on").toBool());
        QVERIFY(chips.at(1).toMap().value("on").toBool());
        QVERIFY(chips.at(0).toMap().value("manual").toBool());
        QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestTrackListening)
#include "test_track_listening.moc"

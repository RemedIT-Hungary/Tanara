// PlayerController: the player's contract (gui/qml/CONTRACT.md) without audio:
//  - with the built-in silent engine (stepped by a clock): play, pause, seek, speed,
//    playRange (one utterance, then pause), end handling;
//  - with a fake engine (from a factory): lazy source loading, preview (playFile / stopPreview)
//    preserving the mixdown's position;
//  - with a real AppController in an isolated TANARA_HOME: resolving the meeting's audio
//    (the mixdown, failing that the largest active track).
#include "AppContext.h"
#include "demo/LibraryDemoData.h"
#include "PlayerController.h"

#include "tanara/app/AppController.h"
#include "tanara/library/store/MeetingRepository.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace tanara_qml;

namespace {

// Fake engine: records the calls; the test steps the position.
class FakeBackend : public PlayerBackend {
public:
    using PlayerBackend::PlayerBackend;
    static inline FakeBackend* last = nullptr;
    static inline int created = 0;

    QStringList sources;
    qint64 pos = 0;
    qint64 dur = 0;
    bool playingNow = false;
    qreal rate = 1.0, volume = 1.0;

    void setSource(const QString& path, qint64) override
    {
        sources << path;
        playingNow = false;
        pos = 0;
        dur = path.endsWith(QLatin1String("track.ogg")) ? 5000 : 60000;
        emit durationChanged(dur);
    }
    void play() override { playingNow = true; emit playingChanged(true); }
    void pause() override { if (playingNow) { playingNow = false; emit playingChanged(false); } }
    void setPosition(qint64 ms) override { pos = ms; }
    qint64 position() const override { return pos; }
    qint64 duration() const override { return dur; }
    bool isPlaying() const override { return playingNow; }
    void setRate(qreal r) override { rate = r; }
    void setVolume(qreal v) override { volume = v; }
    void finish() { playingNow = false; emit playingChanged(false); emit finished(); }
};

} // namespace

class TestPlayerController : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        AppContext::instance()->setController(nullptr);
        AppContext::instance()->setDemo(true);
        PlayerController::setBackendFactory({});
        FakeBackend::last = nullptr;
        FakeBackend::created = 0;
    }

    void emptyPlayerIsInert()
    {
        PlayerController p;
        QVERIFY(!p.available());
        QVERIFY(!p.playing());
        QCOMPARE(p.durationMs(), 0);
        p.play();
        p.toggle();
        p.seek(5000);
        p.playRange(100, 200);
        QVERIFY(!p.playing());
        QCOMPARE(p.positionMs(), 0);
    }

    void demoMeetingPlaysWithSilentBackend()
    {
        PlayerController p;
        QSignalSpy positions(&p, &PlayerController::positionMsChanged);
        p.setMeetingId(tanara_demo::defaultMeetingId());
        QVERIFY(p.available());
        QCOMPARE(p.durationMs(), int(tanara_demo::find(tanara_demo::defaultMeetingId())->entry.durationMs));
        QCOMPARE(p.positionMs(), 0);

        // seek does not change the playback state.
        p.seek(10000);
        QVERIFY(!p.playing());
        QCOMPARE(p.positionMs(), 10000);

        p.play();
        QVERIFY(p.playing());
        positions.clear();
        QTest::qWait(350);
        // During playback the position updates at least 10 times per second.
        QVERIFY2(positions.count() >= 4, qPrintable(QString::number(positions.count())));
        QVERIFY(p.positionMs() > 10200 && p.positionMs() < 11500);

        p.seek(20000);
        QVERIFY(p.playing());            // keeps playing during a seek
        QTRY_VERIFY(p.positionMs() >= 20000);

        p.toggle();
        QVERIFY(!p.playing());
        const int paused = p.positionMs();
        QTest::qWait(120);
        QCOMPARE(p.positionMs(), paused);

        // Another meeting: stops and goes to the start.
        p.play();
        p.setMeetingId(tanara_demo::failedMeetingId());
        QVERIFY(!p.playing());
        QCOMPARE(p.positionMs(), 0);
        QCOMPARE(p.durationMs(), int(tanara_demo::find(tanara_demo::failedMeetingId())->entry.durationMs));
        p.setMeetingId(QString());
        QVERIFY(!p.available());
    }

    void playRangeStopsAtTheEndOfTheUtterance()
    {
        PlayerController p;
        p.setMeetingId(tanara_demo::defaultMeetingId());
        p.playRange(5000, 5250);
        QVERIFY(p.playing());
        QVERIFY(p.positionMs() >= 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!p.playing(), 2000);
        QVERIFY(p.positionMs() >= 5250 && p.positionMs() < 5600);
        // Afterwards plain playback is continuous (it does not stop again at the range end).
        p.play();
        QTest::qWait(200);
        QVERIFY(p.playing());
        p.pause();
    }

    // Line playback with lead-in / tail (150 / 100 ms), limited to the neighboring lines' boundary.
    void playRangeWithBoundsPadsAndClamps()
    {
        PlayerController p;
        p.setMeetingId(tanara_demo::defaultMeetingId());
        // Without a limit: starts 150 ms earlier, stops 100 ms later.
        p.playRange(5000, 5250, -1, -1);
        QVERIFY(p.playing());
        QCOMPARE(p.positionMs(), 4850);
        QTRY_VERIFY_WITH_TIMEOUT(!p.playing(), 2000);
        QVERIFY2(p.positionMs() >= 5350 && p.positionMs() < 5700, qPrintable(QString::number(p.positionMs())));

        // The previous line ends at 4950, the next starts at 5300.
        p.playRange(5000, 5250, 4950, 5300);
        QCOMPARE(p.positionMs(), 4950);
        QTRY_VERIFY_WITH_TIMEOUT(!p.playing(), 2000);
        QVERIFY2(p.positionMs() >= 5300 && p.positionMs() < 5650, qPrintable(QString::number(p.positionMs())));

        // Overlapping neighbor (the boundary is inside the line): no lead-in / tail, but the whole line plays.
        p.playRange(5000, 5250, 5100, 5200);
        QCOMPARE(p.positionMs(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!p.playing(), 2000);
        QVERIFY(p.positionMs() >= 5250);

        // At the start of the meeting it does not go below 0.
        p.playRange(50, 300, -1, -1);
        QCOMPARE(p.positionMs(), 0);
        p.pause();
    }

    void rateSpeedsUpAndEndIsHandled()
    {
        PlayerController p;
        p.setMeetingId(tanara_demo::defaultMeetingId());
        p.setRate(2.0);
        QCOMPARE(p.rate(), 2.0);
        p.seek(p.durationMs() - 400);
        p.play();
        // At 2x speed 400 ms runs out in about 200 ms.
        QTRY_VERIFY_WITH_TIMEOUT(!p.playing(), 1500);
        QCOMPARE(p.positionMs(), p.durationMs());
        // Started from the end, it starts over from the beginning.
        p.play();
        QVERIFY(p.playing());
        QTRY_VERIFY(p.positionMs() < 2000);
        p.pause();

        p.setVolume(0.4);
        QCOMPARE(p.volume(), 0.4);
        p.setVolume(7);
        QCOMPARE(p.volume(), 1.0);
    }

    void backendIsLazyAndPreviewKeepsMixPosition()
    {
        PlayerController::setBackendFactory([](QObject* parent) -> PlayerBackend* {
            ++FakeBackend::created;
            return FakeBackend::last = new FakeBackend(parent);
        });
        PlayerController p;
        p.setMeetingId(tanara_demo::defaultMeetingId());
        p.setVolume(0.5);
        p.setRate(1.5);
        p.seek(30000);
        QCOMPARE(FakeBackend::created, 0);     // no audio engine until playback

        p.play();
        QCOMPARE(FakeBackend::created, 1);
        FakeBackend* b = FakeBackend::last;
        QCOMPARE(b->sources.size(), 1);
        QCOMPARE(b->pos, 30000);
        QCOMPARE(b->volume, 0.5);
        QCOMPARE(b->rate, 1.5);
        QVERIFY(p.playing());
        QCOMPARE(p.durationMs(), 60000);       // the length measured by the engine is the valid one

        b->pos = 31000;
        QTRY_COMPARE(p.positionMs(), 31000);

        // Preview: the real engine plays only an existing file.
        QSignalSpy errors(&p, &PlayerController::errorOccurred);
        p.playFile(QStringLiteral("/nincs/ilyen/track.ogg"));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(p.previewPath(), QString());
        QVERIFY(p.playing());

        QTemporaryDir dir;
        const QString track = dir.filePath(QStringLiteral("track.ogg"));
        QFile f(track);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        p.playFile(track);
        QCOMPARE(p.previewPath(), track);
        QVERIFY(p.previewPlaying());
        QVERIFY(!p.playing());                 // the mixdown stops
        QCOMPARE(p.positionMs(), 31000);       // and keeps its position
        QCOMPARE(p.previewDurationMs(), 5000);
        b->pos = 1200;
        QTRY_COMPARE(p.previewPositionMs(), 1200);
        QCOMPARE(p.positionMs(), 31000);

        // At the end of the preview it returns to the mixdown (paused), at the old position.
        b->finish();
        QCOMPARE(p.previewPath(), QString());
        QVERIFY(!p.previewPlaying());
        p.play();
        QCOMPARE(b->sources.last(), b->sources.first());
        QCOMPARE(b->pos, 31000);
        QVERIFY(p.playing());

        // stopPreview by hand; play() during a preview starts the mixdown.
        p.playFile(track);
        QCOMPARE(p.previewPath(), track);
        p.stopPreview();
        QCOMPARE(p.previewPath(), QString());
        QVERIFY(!p.playing());
        p.playFile(track);
        p.play();
        QCOMPARE(p.previewPath(), QString());
        QVERIFY(p.playing());
        p.pause();
    }

    void realMeetingResolvesMixdownOrLargestTrack()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("TANARA_HOME", home.path().toUtf8());
        qputenv("TANARA_CLOUD", "off");
        AppContext::instance()->setDemo(false);
        {
            tanara::app::AppController app;
            tanara::Meeting m = app.store()->createMeeting(QStringLiteral("Hangos megbeszélés"));
            auto write = [&m](const QString& name, int bytes) {
                QFile f(QDir(m.folder).filePath(name));
                if (f.open(QIODevice::WriteOnly)) f.write(QByteArray(bytes, 'x'));
            };
            tanara::Track small, big, dropped;
            small.id = "mic"; small.file = "track_mic.ogg"; small.active = true;
            big.id = "loop"; big.file = "track_loop.ogg"; big.active = true;
            dropped.id = "sys"; dropped.file = "track_sys.ogg"; dropped.active = false;
            write(small.file, 100);
            write(big.file, 900);
            write(dropped.file, 5000);
            m.tracks = {small, big, dropped};
            m.durationMs = 42000;
            app.store()->saveMeeting(m);

            PlayerController p;
            p.setController(&app);
            QSignalSpy availability(&p, &PlayerController::availableChanged);
            p.setMeetingId(m.id);
            QVERIFY(p.available());
            QCOMPARE(p.durationMs(), 42000);
            // No mixdown: the largest ACTIVE track (a dropped one does not count).
            QCOMPARE(QFileInfo(p.audioPath()).fileName(), QStringLiteral("track_loop.ogg"));

            // The mixdown is finished: the source switches to it.
            write(QStringLiteral("mixdown.mp3"), 300);
            m.mixdownFile = QStringLiteral("mixdown.mp3");
            app.store()->saveMeeting(m);
            emit app.mixdownUpdated(m.id, true);
            QCOMPARE(QFileInfo(p.audioPath()).fileName(), QStringLiteral("mixdown.mp3"));
            QVERIFY(availability.count() >= 2);

            // A meeting without audio: not playable.
            const tanara::Meeting empty = app.store()->createMeeting(QStringLiteral("Üres"));
            p.setMeetingId(empty.id);
            QVERIFY(!p.available());
            p.play();
            QVERIFY(!p.playing());
        }
        qunsetenv("TANARA_HOME");
    }
};

QTEST_MAIN(TestPlayerController)
#include "test_player_controller.moc"

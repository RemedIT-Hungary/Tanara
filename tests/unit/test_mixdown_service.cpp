// Tanara: recording::MixdownService with a fake media tool (no ffmpeg): the mix replaces the old
// one only on success, sets mixdownFile and clears the dirty flag; progress + the Mixdown task; a
// cancel or an ffmpeg failure leaves the old mix and no .part file; a missing active track keeps
// an existing mix (with the reason for the caller); a mix that is a transcription stage begins no
// task of its own; a second request while one runs is a no-op.
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "tanara/domain/Job.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/recording/MixdownService.h"

#include "fake_media_tool.h"

using namespace tanara;

namespace {

struct Rig {
    QTemporaryDir dir;
    std::shared_ptr<testing::FakeMediaTool> media = std::make_shared<testing::FakeMediaTool>();
    std::unique_ptr<library::MeetingRepository> store;
    std::unique_ptr<library::MeetingJobTracker> jobs;
    std::unique_ptr<recording::MixdownService> mix;

    Rig()
    {
        store = std::make_unique<library::MeetingRepository>(dir.filePath(QStringLiteral("rec")),
                                                             dir.filePath(QStringLiteral("meta")));
        jobs = std::make_unique<library::MeetingJobTracker>(store.get());
        mix = std::make_unique<recording::MixdownService>(store.get(), jobs.get(), media);
    }

    // A meeting with two active tracks (files on disk) and optionally an old mix.
    Meeting meeting(bool oldMix, bool missingTrack = false)
    {
        Meeting m;
        m.id = QStringLiteral("m-%1").arg(++n);
        m.title = QStringLiteral("Keverés");
        m.folder = dir.filePath(QStringLiteral("rec/%1").arg(m.id));
        m.durationMs = 1000;
        QDir().mkpath(m.folder);
        for (const QString& id : {QStringLiteral("mic"), QStringLiteral("sys")}) {
            Track t;
            t.id = id;
            t.deviceName = id;
            t.file = QStringLiteral("track_%1.ogg").arg(id);
            t.active = true;
            m.tracks << t;
            if (missingTrack && id == QStringLiteral("sys")) continue;
            QFile f(QDir(m.folder).filePath(t.file));
            f.open(QIODevice::WriteOnly);
            f.write("x");
        }
        if (oldMix) {
            QFile f(QDir(m.folder).filePath(QStringLiteral("mixdown.mp3")));
            f.open(QIODevice::WriteOnly);
            f.write("old mix");
            m.mixdownFile = QStringLiteral("mixdown.mp3");
        }
        m.mixdownDirty = true;
        store->saveMeeting(m);
        return m;
    }
    QByteArray mixOf(const Meeting& m) const
    {
        QFile f(QDir(m.folder).filePath(QStringLiteral("mixdown.mp3")));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    int n = 0;
};

} // namespace

class MixdownServiceTest : public QObject {
    Q_OBJECT
private slots:
    void successReplacesAndMarksClean();
    void cancelKeepsOldMix();
    void failureKeepsOldMix();
    void missingTrackKeepsExistingMix();
    void transcriptionStageBeginsNoTask();
};

void MixdownServiceTest::successReplacesAndMarksClean()
{
    Rig rig;
    const Meeting m = rig.meeting(/*oldMix*/ true);
    QSignalSpy progress(rig.mix.get(), &recording::MixdownService::progress);
    QSignalSpy finished(rig.mix.get(), &recording::MixdownService::finished);
    QSignalSpy ended(rig.mix.get(), &recording::MixdownService::runEnded);
    rig.mix->regenerate(m.id);
    QVERIFY(rig.mix->running(m.id));
    QVERIFY(rig.jobs->isRunning(m.id, JobKind::Mixdown));
    rig.mix->regenerate(m.id);   // already running: no-op
    QCOMPARE(rig.media->graphs().size(), 1);
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.at(0).at(1).toBool(), true);
    QCOMPARE(ended.size(), 1);
    QCOMPARE(progress.first().at(1).toInt(), 0);
    QCOMPARE(progress.last().at(1).toInt(), 100);
    QVERIFY(progress.size() >= 3);   // 0, the ffmpeg-reported 50, 100
    QVERIFY(!rig.mix->running(m.id));
    QVERIFY(!rig.jobs->isRunning(m.id, JobKind::Mixdown));
    QCOMPARE(rig.mixOf(m), QByteArray("fake media output"));
    QVERIFY(!QDir(m.folder).exists(QStringLiteral("mixdown.part.mp3")));
    const Meeting after = rig.store->load(m.id);
    QCOMPARE(after.mixdownFile, QStringLiteral("mixdown.mp3"));
    QVERIFY(!after.mixdownDirty);
}

void MixdownServiceTest::cancelKeepsOldMix()
{
    Rig rig;
    const Meeting m = rig.meeting(true);
    QSignalSpy finished(rig.mix.get(), &recording::MixdownService::finished);
    QSignalSpy errors(rig.mix.get(), &recording::MixdownService::error);
    QVERIFY(!rig.mix->cancel(m.id));   // nothing runs
    rig.mix->regenerate(m.id);
    QVERIFY(rig.mix->cancel(m.id));
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.at(0).at(1).toBool(), false);
    QCOMPARE(errors.size(), 0);        // a cancel is not an error
    QCOMPARE(rig.mixOf(m), QByteArray("old mix"));
    QVERIFY(!QDir(m.folder).exists(QStringLiteral("mixdown.part.mp3")));
    QVERIFY(!rig.jobs->isRunning(m.id, JobKind::Mixdown));
    QVERIFY(!rig.jobs->lastError(m.id, JobKind::Mixdown).isValid());
    QVERIFY(rig.store->load(m.id).mixdownDirty);
}

void MixdownServiceTest::failureKeepsOldMix()
{
    Rig rig;
    rig.media->graphOk = false;
    const Meeting m = rig.meeting(true);
    QSignalSpy finished(rig.mix.get(), &recording::MixdownService::finished);
    QSignalSpy errors(rig.mix.get(), &recording::MixdownService::error);
    rig.mix->regenerate(m.id);
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.at(0).at(1).toBool(), false);
    QCOMPARE(errors.size(), 1);
    QCOMPARE(rig.mixOf(m), QByteArray("old mix"));
    QVERIFY(rig.jobs->lastError(m.id, JobKind::Mixdown).isValid());
}

void MixdownServiceTest::missingTrackKeepsExistingMix()
{
    Rig rig;
    const Meeting m = rig.meeting(/*oldMix*/ true, /*missingTrack*/ true);
    QSignalSpy finished(rig.mix.get(), &recording::MixdownService::finished);
    QSignalSpy errors(rig.mix.get(), &recording::MixdownService::error);
    QSignalSpy ended(rig.mix.get(), &recording::MixdownService::runEnded);
    rig.mix->regenerate(m.id);
    QCOMPARE(finished.size(), 1);      // synchronously: nothing started
    QCOMPARE(finished.at(0).at(1).toBool(), false);
    QCOMPARE(errors.size(), 1);
    QCOMPARE(ended.size(), 0);
    QCOMPARE(rig.media->graphs().size(), 0);
    QVERIFY(rig.mix->takeFailReason(m.id).contains(QStringLiteral("sys")));
    QVERIFY(rig.mix->takeFailReason(m.id).isEmpty());   // taken once
    QCOMPARE(rig.mixOf(m), QByteArray("old mix"));
}

void MixdownServiceTest::transcriptionStageBeginsNoTask()
{
    Rig rig;
    const Meeting m = rig.meeting(false);
    rig.mix->setTranscriptionStageProbe([&m](const QString& id) { return id == m.id; });
    QSignalSpy finished(rig.mix.get(), &recording::MixdownService::finished);
    rig.mix->regenerate(m.id);
    QVERIFY(!rig.jobs->isRunning(m.id, JobKind::Mixdown));
    QVERIFY(!rig.mix->cancel(m.id));          // not a task of its own...
    QVERIFY(rig.mix->cancelProcess(m.id));    // ...but its process can be stopped
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.at(0).at(1).toBool(), false);
    QVERIFY(!QDir(m.folder).exists(QStringLiteral("mixdown.mp3")));
    QVERIFY(!rig.mix->cancelProcess(m.id));
}

QTEST_GUILESS_MAIN(MixdownServiceTest)
#include "test_mixdown_service.moc"

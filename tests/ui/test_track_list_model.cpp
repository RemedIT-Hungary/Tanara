// TrackListModel + WaveformItem: the model of the Tracks tab on an isolated TANARA_HOME: names,
// renaming, restoring / permanently deleting a dropped track, locating a missing file,
// waveform peaks and the mixdown state (with a real ffmpeg, if available).
#include "AppContext.h"
#include "JobTestSupport.h"
#include "UiServices.h"
#include "TrackListModel.h"
#include "WaveformItem.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

using namespace tanara;
using tanara_qml::TrackListModel;
using tanara_qml::WaveformItem;

namespace {
QVariant cell(TrackListModel& model, int row, int role) { return model.data(model.index(row), role); }
} // namespace

class TestTrackListModel : public QObject {
    Q_OBJECT
private slots:
    void waveformBarLevels()
    {
        // Averaging resampling, projected onto a common scale (reference), clamped to 0..1; the
        // levels stretched slightly (exponent > 1): the loud part is full, the quiet one stays low.
        const QList<qreal> peaks{1.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.5, 0.5};
        const QList<qreal> bars = WaveformItem::barLevels(peaks, 4, 1.0);
        QCOMPARE(bars.size(), 4);
        QCOMPARE(bars[0], 1.0);
        QCOMPARE(bars[1], 0.0);
        QVERIFY(bars[2] > 0.2 && bars[2] < 0.5);    // mean 0.5, stretched below 0.5
        QVERIFY(qAbs(bars[2] - bars[3]) < 1e-9);
        // The reference level is the upper percentile: a single outlier does not squash the picture.
        QList<qreal> many(100, 0.1);
        many[50] = 1.0;
        QVERIFY(WaveformItem::referenceLevel(many) < 0.2);
        QCOMPARE(WaveformItem::referenceLevel({}), 0.0);
        // A quiet track looks flat on the loud track's scale.
        const QList<qreal> quiet = WaveformItem::barLevels({0.004, 0.003}, 2, 0.9);
        QVERIFY(quiet[0] < 0.1);
        // More columns than buckets: repeats; empty input: all zeros.
        QCOMPARE(WaveformItem::barLevels({1.0}, 3, 0).size(), 3);
        QCOMPARE(WaveformItem::barLevels({}, 3, 0), QList<qreal>({0.0, 0.0, 0.0}));
    }

    void demoWithoutController()
    {
        TrackListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(model.demo());
        QCOMPARE(model.count(), 4);
        QCOMPARE(model.droppedCount(), 1);
        QCOMPARE(model.mixdownState(), QStringLiteral("running"));
        QCOMPARE(model.mixdownPercent(), 64);
        QVERIFY(cell(model, 3, TrackListModel::MissingRole).toBool());
        QCOMPARE(cell(model, 0, TrackListModel::PeaksStateRole).toString(), QStringLiteral("ready"));

        model.setDemoState(QStringLiteral("loading"));
        QCOMPARE(cell(model, 0, TrackListModel::PeaksStateRole).toString(), QStringLiteral("loading"));
        QCOMPARE(model.mixdownState(), QStringLiteral("stale"));

        // In demo mode the actions work in memory.
        QVERIFY(model.rename(0, QStringLiteral("Fejhallgató")));
        QCOMPARE(cell(model, 0, TrackListModel::DisplayNameRole).toString(), QStringLiteral("Fejhallgató"));
        model.restore(2);
        QCOMPARE(model.droppedCount(), 0);
    }

    void waveExtentOnMeetingTimeline()
    {
        using P = QPair<qreal, qreal>;
        QCOMPARE(TrackListModel::waveExtent(0, 60000, 60000), P(0.0, 1.0));
        QCOMPARE(TrackListModel::waveExtent(15000, 30000, 60000), P(0.25, 0.5));
        // Unknown file length: from its start to the end.
        QCOMPARE(TrackListModel::waveExtent(15000, -1, 60000), P(0.25, 0.75));
        // A file longer than the meeting / too large an offset: stays within the track.
        QCOMPARE(TrackListModel::waveExtent(0, 61000, 60000), P(0.0, 1.0));
        QCOMPARE(TrackListModel::waveExtent(45000, 30000, 60000), P(0.75, 0.25));
        QCOMPARE(TrackListModel::waveExtent(90000, 1000, 60000), P(1.0, 0.0));
        // Unknown meeting length: the full width.
        QCOMPARE(TrackListModel::waveExtent(5000, 1000, 0), P(0.0, 1.0));
    }

    // A device switched off and on: two segments, the second starts later. The row's second line
    // shows the segment and the start, the waveform is in place, the length is the file's.
    void segmentRowsAndWaveGap()
    {
        jobtest::Sandbox sb;
        Meeting m = sb.recording("Szakaszok", 1, /*secondTrack*/ true);
        tanara::Track seg2 = m.tracks[1];
        seg2.id = "loop-2";
        seg2.file = "track_loop-2.wav";
        seg2.startOffsetMs = 3000;
        QVERIFY(QFile::copy(QDir(m.folder).filePath("track_loop.wav"), QDir(m.folder).filePath(seg2.file)));
        m.tracks.append(seg2);
        m.tracks[1].startOffsetMs = 1000;
        m.durationMs = 4000;
        sb.app->store()->saveMeeting(m);

        TrackListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setFacade(tanara_qml::AppContext::instance()->servicesFor(sb.app.get()).tracks(QString(), nullptr));
        model.setMeetingId(m.id);
        QCOMPARE(model.count(), 3);
        QCOMPARE(cell(model, 1, TrackListModel::TrackIdRole).toString(), QStringLiteral("loop"));
        QCOMPARE(cell(model, 2, TrackListModel::TrackIdRole).toString(), QStringLiteral("loop-2"));
        QCOMPARE(cell(model, 1, TrackListModel::DisplayNameRole), cell(model, 2, TrackListModel::DisplayNameRole));
        QCOMPARE(cell(model, 0, TrackListModel::SegmentRole).toInt(), 0);
        QCOMPARE(cell(model, 2, TrackListModel::SegmentRole).toInt(), 2);
        const QString raw2 = cell(model, 2, TrackListModel::RawNameRole).toString();
        QVERIFY2(raw2.startsWith(QStringLiteral("2. szakasz · kezdete: 00:03 · Monitor of Teszt")), qPrintable(raw2));
        QVERIFY(raw2.endsWith(QStringLiteral("track_loop-2.wav")));
        QVERIFY(!cell(model, 0, TrackListModel::RawNameRole).toString().contains(QStringLiteral("szakasz")));

        // No peaks yet: the length runs from the start to the end of the meeting.
        QCOMPARE(cell(model, 0, TrackListModel::WaveStartRole).toReal(), 0.0);
        QCOMPARE(cell(model, 2, TrackListModel::WaveStartRole).toReal(), 0.75);
        QCOMPARE(cell(model, 2, TrackListModel::WaveSpanRole).toReal(), 0.25);
        QCOMPARE(cell(model, 1, TrackListModel::DurationTextRole).toString(), QStringLiteral("00:03"));

        // After the peaks (real ffmpeg) the file's real length: 1 s, a quarter of the track.
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
            QSKIP("ffmpeg nincs a PATH-on");
        model.requestWaveforms();
        QTRY_COMPARE_WITH_TIMEOUT(cell(model, 1, TrackListModel::PeaksStateRole).toString(), QStringLiteral("ready"), 15000);
        QCOMPARE(cell(model, 1, TrackListModel::DurationTextRole).toString(), QStringLiteral("00:01"));
        QCOMPARE(cell(model, 1, TrackListModel::WaveStartRole).toReal(), 0.25);
        QVERIFY(qAbs(cell(model, 1, TrackListModel::WaveSpanRole).toReal() - 0.25) < 0.02);
    }

    void namesRenameRestoreAndDelete()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.recording("Sávok", 1, /*secondTrack*/ true, /*dropSecond*/ true);

        TrackListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setFacade(tanara_qml::AppContext::instance()->servicesFor(sb.app.get()).tracks(QString(), nullptr));
        QCOMPARE(model.count(), 0);
        model.setMeetingId(m.id);
        QVERIFY(!model.demo());
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.activeCount(), 1);
        QCOMPARE(model.droppedCount(), 1);

        // A friendly name from the role, below it the raw device name + file.
        QCOMPARE(cell(model, 0, TrackListModel::DisplayNameRole).toString(), QStringLiteral("Saját mikrofon"));
        QCOMPARE(cell(model, 0, TrackListModel::IconNameRole).toString(), QStringLiteral("mic"));
        QVERIFY(cell(model, 0, TrackListModel::RawNameRole).toString().contains("Teszt mikrofon"));
        QVERIFY(cell(model, 0, TrackListModel::RawNameRole).toString().contains("track_mic.wav"));
        QVERIFY(!cell(model, 1, TrackListModel::ActiveRole).toBool());
        QCOMPARE(cell(model, 0, TrackListModel::DurationTextRole).toString(), QStringLiteral("00:01"));

        // Rename (persistent), with an empty name back to the friendly one.
        QVERIFY(model.rename(0, QStringLiteral("Asztali mikrofon")));
        QCOMPARE(cell(model, 0, TrackListModel::DisplayNameRole).toString(), QStringLiteral("Asztali mikrofon"));
        QVERIFY(cell(model, 0, TrackListModel::RenamedRole).toBool());
        QCOMPARE(sb.app->store()->load(m.id).tracks[0].customName, QStringLiteral("Asztali mikrofon"));
        QVERIFY(model.rename(0, QString()));
        QCOMPARE(cell(model, 0, TrackListModel::DisplayNameRole).toString(), QStringLiteral("Saját mikrofon"));

        // Restore: active; the mixdown becomes stale / missing because of it.
        model.restore(1);
        QTRY_COMPARE(model.droppedCount(), 0);
        QVERIFY(cell(model, 1, TrackListModel::ActiveRole).toBool());

        // Dropped again, then permanently deleting the dropped ones: the file disappears too, the active one stays.
        Meeting mm = sb.app->store()->load(m.id);
        mm.tracks[1].active = false;
        sb.app->store()->saveMeeting(mm);
        emit sb.app->tracksChanged(m.id);
        QTRY_COMPARE(model.droppedCount(), 1);
        const QString droppedFile = QDir(m.folder).filePath("track_loop.wav");
        QVERIFY(QFile::exists(droppedFile));
        QCOMPARE(model.deleteDropped(), 1);
        QTRY_COMPARE(model.count(), 1);
        QVERIFY(!QFile::exists(droppedFile));
        QVERIFY(QFile::exists(QDir(m.folder).filePath("track_mic.wav")));
    }

    // Under the confirmation / file-picker window the selection can switch to another meeting:
    // the delete and the file assignment affect only the meeting the user agreed to.
    void confirmedActionsNeverHitAnotherMeeting()
    {
        jobtest::Sandbox sb;
        const Meeting a = sb.recording("Megerősített", 1, /*secondTrack*/ true);
        const Meeting b = sb.recording("Közben kijelölt", 1, /*secondTrack*/ true, /*dropSecond*/ true);
        const QString bDropped = QDir(b.folder).filePath(b.tracks[1].file);
        QVERIFY(QFile::exists(bDropped));

        TrackListModel model;
        model.setFacade(tanara_qml::AppContext::instance()->servicesFor(sb.app.get()).tracks(QString(), nullptr));
        model.setMeetingId(a.id);
        const QString aTrack = model.trackIdAt(0);
        QVERIFY(!aTrack.isEmpty());
        QCOMPARE(model.trackIdAt(99), QString());

        // ... under the window the selection switches to B:
        model.setMeetingId(b.id);
        QCOMPARE(model.droppedCount(), 1);
        QCOMPARE(model.deleteDroppedIn(a.id), -1);         // the confirmation was for A, so nothing
        QVERIFY(QFile::exists(bDropped));
        QCOMPARE(model.droppedCount(), 1);
        const QString src = sb.home->filePath("masik.wav");
        QVERIFY(QFile::copy(QDir(a.folder).filePath(a.tracks[0].file), src));
        QVERIFY(!model.relocateTrack(a.id, aTrack, src).isEmpty());   // an error message, no copy
        QCOMPARE(sb.app->store()->load(b.id).tracks[0].file, b.tracks[0].file);

        // For a matching meeting the delete happens.
        QCOMPARE(model.deleteDroppedIn(b.id), 1);
        QVERIFY(!QFile::exists(bDropped));
    }

    void missingFileIsFoundAgain()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.recording("Hiányzó sáv", 1);
        const QString original = QDir(m.folder).filePath("track_mic.wav");
        const QString moved = sb.home->filePath("elrakott.wav");
        QVERIFY(QFile::rename(original, moved));

        TrackListModel model;
        model.setFacade(tanara_qml::AppContext::instance()->servicesFor(sb.app.get()).tracks(QString(), nullptr));
        model.setMeetingId(m.id);
        QVERIFY(cell(model, 0, TrackListModel::MissingRole).toBool());
        QCOMPARE(cell(model, 0, TrackListModel::DurationTextRole).toString(), QStringLiteral("–"));
        QCOMPARE(model.activeCount(), 0);

        // A non-existent file gives a human error message, no change.
        QVERIFY(!model.relocate(0, sb.home->filePath("nincs-ilyen.wav")).isEmpty());
        QVERIFY(cell(model, 0, TrackListModel::MissingRole).toBool());

        // The found file is copied into the meeting's folder (the original stays in place).
        QCOMPARE(model.relocate(0, moved), QString());
        QTRY_VERIFY(!cell(model, 0, TrackListModel::MissingRole).toBool());
        QVERIFY(QFile::exists(moved));
        QVERIFY(QFile::exists(cell(model, 0, TrackListModel::PathRole).toString()));
        QCOMPARE(model.activeCount(), 1);
    }

    void waveformsAndMixdown()
    {
        jobtest::Sandbox sb;
        if (!sb.ffmpeg) QSKIP("ffmpeg nem található");
        const Meeting m = sb.recording("Hullámforma", 2, /*secondTrack*/ true);

        TrackListModel model;
        model.setFacade(tanara_qml::AppContext::instance()->servicesFor(sb.app.get()).tracks(QString(), nullptr));
        model.setMeetingId(m.id);
        QCOMPARE(cell(model, 0, TrackListModel::PeaksStateRole).toString(), QStringLiteral("none"));
        QCOMPARE(model.mixdownState(), QStringLiteral("none"));

        // When the tab appears we request the peaks: "loading" while computing, then "ready".
        model.requestWaveforms();
        QCOMPARE(cell(model, 0, TrackListModel::PeaksStateRole).toString(), QStringLiteral("loading"));
        QTRY_COMPARE_WITH_TIMEOUT(cell(model, 0, TrackListModel::PeaksStateRole).toString(), QStringLiteral("ready"), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(cell(model, 1, TrackListModel::PeaksStateRole).toString(), QStringLiteral("ready"), 20000);
        QVERIFY(!cell(model, 0, TrackListModel::PeaksRole).value<QList<qreal>>().isEmpty());
        QVERIFY(model.peakReference() > 0.1);
        QCOMPARE(cell(model, 0, TrackListModel::DurationTextRole).toString(), QStringLiteral("00:02"));

        // A new model on the same meeting: available at once from the cache (without a request).
        TrackListModel second;
        second.setFacade(tanara_qml::AppContext::instance()->servicesFor(sb.app.get()).tracks(QString(), nullptr));
        second.setMeetingId(m.id);
        QCOMPARE(cell(second, 0, TrackListModel::PeaksStateRole).toString(), QStringLiteral("ready"));

        // Mixdown: running (cancellable), then done; the mix's waveform arrives too.
        model.refreshMixdown();
        QCOMPARE(model.mixdownState(), QStringLiteral("running"));
        QVERIFY(model.mixdownCancellable());
        QTRY_COMPARE_WITH_TIMEOUT(model.mixdownState(), QStringLiteral("ready"), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!model.mixdownPeaks().isEmpty(), 20000);

        // After dropping a track the mix is stale.
        Meeting mm = sb.app->store()->load(m.id);
        mm.tracks[1].active = false;
        mm.mixdownDirty = true;
        sb.app->store()->saveMeeting(mm);
        emit sb.app->tracksChanged(m.id);
        QTRY_COMPARE(model.mixdownState(), QStringLiteral("stale"));

        // Canceled mixdown: the old mix is untouched, the state stays stale.
        model.refreshMixdown();
        QCOMPARE(model.mixdownState(), QStringLiteral("running"));
        if (sb.app->cancelJob(m.id, JobKind::Mixdown)) {
            QTRY_VERIFY_WITH_TIMEOUT(model.mixdownState() != QStringLiteral("running"), 15000);
            QCOMPARE(model.mixdownState(), QStringLiteral("stale"));
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(model.mixdownState(), QStringLiteral("ready"), 30000);
        }
    }
};

QTEST_MAIN(TestTrackListModel)
#include "test_track_list_model.moc"

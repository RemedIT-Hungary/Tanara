// TrackListModel + WaveformItem — a Sávok fül (M09) modellje izolált TANARA_HOME-on: nevek,
// átnevezés, eldobott sáv visszaállítása / végleges törlése, hiányzó fájl megkeresése,
// hullámforma-csúcsok és a lekeverés állapota (valódi ffmpeg-gel, ha van).
#include "JobTestSupport.h"
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
        // Átlagoló újramintavételezés, közös skálára (reference) vetítve, 0..1 közé szorítva;
        // a szintek enyhén széthúzva (kitevő > 1): a hangos rész telt, a halk alacsony marad.
        const QList<qreal> peaks{1.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.5, 0.5};
        const QList<qreal> bars = WaveformItem::barLevels(peaks, 4, 1.0);
        QCOMPARE(bars.size(), 4);
        QCOMPARE(bars[0], 1.0);
        QCOMPARE(bars[1], 0.0);
        QVERIFY(bars[2] > 0.2 && bars[2] < 0.5);    // átlag 0,5 → széthúzva 0,5 alatt
        QVERIFY(qAbs(bars[2] - bars[3]) < 1e-9);
        // A viszonyítási szint a felső percentilis: egyetlen kiugró érték nem nyomja össze a képet.
        QList<qreal> many(100, 0.1);
        many[50] = 1.0;
        QVERIFY(WaveformItem::referenceLevel(many) < 0.2);
        QCOMPARE(WaveformItem::referenceLevel({}), 0.0);
        // Csendes sáv a hangos sáv skáláján laposnak látszik.
        const QList<qreal> quiet = WaveformItem::barLevels({0.004, 0.003}, 2, 0.9);
        QVERIFY(quiet[0] < 0.1);
        // Több oszlop, mint vödör → ismétel; üres bemenet → csupa nulla.
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

        // Demóban a műveletek memóriában működnek.
        QVERIFY(model.rename(0, QStringLiteral("Fejhallgató")));
        QCOMPARE(cell(model, 0, TrackListModel::DisplayNameRole).toString(), QStringLiteral("Fejhallgató"));
        model.restore(2);
        QCOMPARE(model.droppedCount(), 0);
    }

    void namesRenameRestoreAndDelete()
    {
        jobtest::Sandbox sb;
        const Meeting m = sb.recording("Sávok", 1, /*secondTrack*/ true, /*dropSecond*/ true);

        TrackListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setController(sb.app.get());
        QCOMPARE(model.count(), 0);
        model.setMeetingId(m.id);
        QVERIFY(!model.demo());
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.activeCount(), 1);
        QCOMPARE(model.droppedCount(), 1);

        // Barátságos név a szerepből, alatta a nyers eszköznév + fájl.
        QCOMPARE(cell(model, 0, TrackListModel::DisplayNameRole).toString(), QStringLiteral("Saját mikrofon"));
        QCOMPARE(cell(model, 0, TrackListModel::IconNameRole).toString(), QStringLiteral("mic"));
        QVERIFY(cell(model, 0, TrackListModel::RawNameRole).toString().contains("Teszt mikrofon"));
        QVERIFY(cell(model, 0, TrackListModel::RawNameRole).toString().contains("track_mic.wav"));
        QVERIFY(!cell(model, 1, TrackListModel::ActiveRole).toBool());
        QCOMPARE(cell(model, 0, TrackListModel::DurationTextRole).toString(), QStringLiteral("00:01"));

        // Átnevezés (perzisztens), üres névvel vissza a barátságosra.
        QVERIFY(model.rename(0, QStringLiteral("Asztali mikrofon")));
        QCOMPARE(cell(model, 0, TrackListModel::DisplayNameRole).toString(), QStringLiteral("Asztali mikrofon"));
        QVERIFY(cell(model, 0, TrackListModel::RenamedRole).toBool());
        QCOMPARE(sb.app->store()->load(m.id).tracks[0].customName, QStringLiteral("Asztali mikrofon"));
        QVERIFY(model.rename(0, QString()));
        QCOMPARE(cell(model, 0, TrackListModel::DisplayNameRole).toString(), QStringLiteral("Saját mikrofon"));

        // Visszaállítás → aktív; a lekeverés ettől elavul / hiányzik.
        model.restore(1);
        QTRY_COMPARE(model.droppedCount(), 0);
        QVERIFY(cell(model, 1, TrackListModel::ActiveRole).toBool());

        // Újra eldobva, majd az eldobottak végleges törlése: a fájl is eltűnik, az aktív marad.
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

    // A megerősítő / fájlválasztó ablak alatt a kijelölés másik megbeszélésre válthat: a törlés
    // és a fájl-hozzárendelés csak arra a megbeszélésre hat, amelyre a felhasználó rábólintott.
    void confirmedActionsNeverHitAnotherMeeting()
    {
        jobtest::Sandbox sb;
        const Meeting a = sb.recording("Megerősített", 1, /*secondTrack*/ true);
        const Meeting b = sb.recording("Közben kijelölt", 1, /*secondTrack*/ true, /*dropSecond*/ true);
        const QString bDropped = QDir(b.folder).filePath(b.tracks[1].file);
        QVERIFY(QFile::exists(bDropped));

        TrackListModel model;
        model.setController(sb.app.get());
        model.setMeetingId(a.id);
        const QString aTrack = model.trackIdAt(0);
        QVERIFY(!aTrack.isEmpty());
        QCOMPARE(model.trackIdAt(99), QString());

        // …az ablak alatt a kijelölés B-re vált:
        model.setMeetingId(b.id);
        QCOMPARE(model.droppedCount(), 1);
        QCOMPARE(model.deleteDroppedIn(a.id), -1);         // A-ra szólt a megerősítés → semmi
        QVERIFY(QFile::exists(bDropped));
        QCOMPARE(model.droppedCount(), 1);
        const QString src = sb.home->filePath("masik.wav");
        QVERIFY(QFile::copy(QDir(a.folder).filePath(a.tracks[0].file), src));
        QVERIFY(!model.relocateTrack(a.id, aTrack, src).isEmpty());   // hibaüzenet, nincs másolás
        QCOMPARE(sb.app->store()->load(b.id).tracks[0].file, b.tracks[0].file);

        // Egyező megbeszélésnél a törlés megtörténik.
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
        model.setController(sb.app.get());
        model.setMeetingId(m.id);
        QVERIFY(cell(model, 0, TrackListModel::MissingRole).toBool());
        QCOMPARE(cell(model, 0, TrackListModel::DurationTextRole).toString(), QStringLiteral("–"));
        QCOMPARE(model.activeCount(), 0);

        // Nem létező fájl → emberi hibaüzenet, nincs változás.
        QVERIFY(!model.relocate(0, sb.home->filePath("nincs-ilyen.wav")).isEmpty());
        QVERIFY(cell(model, 0, TrackListModel::MissingRole).toBool());

        // A megtalált fájl a meeting mappájába másolódik (az eredeti a helyén marad).
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
        model.setController(sb.app.get());
        model.setMeetingId(m.id);
        QCOMPARE(cell(model, 0, TrackListModel::PeaksStateRole).toString(), QStringLiteral("none"));
        QCOMPARE(model.mixdownState(), QStringLiteral("none"));

        // A fül megjelenésekor kérjük a csúcsokat: számolás közben „loading”, utána „ready”.
        model.requestWaveforms();
        QCOMPARE(cell(model, 0, TrackListModel::PeaksStateRole).toString(), QStringLiteral("loading"));
        QTRY_COMPARE_WITH_TIMEOUT(cell(model, 0, TrackListModel::PeaksStateRole).toString(), QStringLiteral("ready"), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(cell(model, 1, TrackListModel::PeaksStateRole).toString(), QStringLiteral("ready"), 20000);
        QVERIFY(!cell(model, 0, TrackListModel::PeaksRole).value<QList<qreal>>().isEmpty());
        QVERIFY(model.peakReference() > 0.1);
        QCOMPARE(cell(model, 0, TrackListModel::DurationTextRole).toString(), QStringLiteral("00:02"));

        // Új modell ugyanarra a meetingre: a gyorsítótárból azonnal megvan (kérés nélkül).
        TrackListModel second;
        second.setController(sb.app.get());
        second.setMeetingId(m.id);
        QCOMPARE(cell(second, 0, TrackListModel::PeaksStateRole).toString(), QStringLiteral("ready"));

        // Lekeverés: fut (megszakítható) → kész; a keverék hullámformája is megjön.
        model.refreshMixdown();
        QCOMPARE(model.mixdownState(), QStringLiteral("running"));
        QVERIFY(model.mixdownCancellable());
        QTRY_COMPARE_WITH_TIMEOUT(model.mixdownState(), QStringLiteral("ready"), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!model.mixdownPeaks().isEmpty(), 20000);

        // Egy sáv eldobása után a keverék elavult.
        Meeting mm = sb.app->store()->load(m.id);
        mm.tracks[1].active = false;
        mm.mixdownDirty = true;
        sb.app->store()->saveMeeting(mm);
        emit sb.app->tracksChanged(m.id);
        QTRY_COMPARE(model.mixdownState(), QStringLiteral("stale"));

        // Megszakított lekeverés: a régi keverék érintetlen, az állapot elavult marad.
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

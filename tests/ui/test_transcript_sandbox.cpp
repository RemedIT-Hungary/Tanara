// Az átirat-szerkesztő mérése VALÓDI (eldobható) minta-meetingeken, a teljes úton:
// AppController → speakerEditor(meetingId) → TranscriptEditorViewModel → TranscriptTab.qml.
// Alapból KIHAGYVA — csak a TANARA_SANDBOX környezeti változóval fut:
//
//   TANARA_SANDBOX=<mappa> QT_QPA_PLATFORM=offscreen ./build/tests/test_transcript_sandbox
//
// ahol <mappa>/home a metaadat-mappa (settings.json a sandbox mappáira mutat, benne
// models/campplus_sv_zh_en_16k.onnx) és <mappa>/recordings a meetingek. A teszt a
// TANARA_HOME-ot erre állítja, így SEMMI nem megy a valódi ~/.tanara-ba; a futás ír a
// sandboxba (embedding-cache, overlay). Átirat-szöveget és nevet NEM ír ki — csak számokat.
//
// Mit mér (offscreen + szoftveres renderer, 1004×640 — a GPU-s élő ablaknál ez lassabb,
// tehát felső becslés): megnyitás, első képkocka, görgetés képkockánként, ugrás a végére,
// egy sor áthelyezése + visszavonás, sín / szűrő / keresés, és a hang-elemzés ideje.
#include "AppContext.h"
#include "QmlApp.h"
#include "TranscriptEditorViewModel.h"
#include "TranscriptListModel.h"

#include "tanara/AppController.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/store/MeetingStore.h"

#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include <algorithm>
#include <memory>

using namespace tanara;
using namespace tanara_qml;
using Role = TranscriptListModel::Role;

namespace {

const char* kHost = R"(
import QtQuick
import QtQuick.Controls
import Tanara
ApplicationWindow {
    width: 1004; height: 640
    color: Theme.bg
    property string meeting: ""
    TranscriptTab { objectName: "tab"; anchors.fill: parent; meetingId: parent ? meeting : "" }
}
)";

double ms(qint64 nsecs) { return double(nsecs) / 1e6; }

QString stats(QVector<double> v)
{
    if (v.isEmpty()) return QStringLiteral("-");
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (double x : v) sum += x;
    return QStringLiteral("átlag %1 ms · p95 %2 ms · max %3 ms")
        .arg(sum / v.size(), 0, 'f', 1).arg(v[int((v.size() - 1) * 0.95)], 0, 'f', 1).arg(v.last(), 0, 'f', 1);
}

} // namespace

class TestTranscriptSandbox : public QObject {
    Q_OBJECT
private slots:
    void measure();
};

void TestTranscriptSandbox::measure()
{
    const QString root = qEnvironmentVariable("TANARA_SANDBOX");
    if (root.isEmpty()) QSKIP("TANARA_SANDBOX nincs beállítva — a valós-adatos mérés kihagyva.");
    const QString home = QDir(root).absoluteFilePath(QStringLiteral("home"));
    QVERIFY2(QFileInfo::exists(QDir(home).filePath(QStringLiteral("settings.json"))), "nem sandbox: nincs home/settings.json");
    QVERIFY2(!QDir(home).absolutePath().startsWith(QDir::homePath() + QStringLiteral("/.tanara")),
             "a sandbox nem mutathat a valódi ~/.tanara-ra");
    qputenv("TANARA_HOME", home.toUtf8());
    qputenv("TANARA_CLOUD", "off");

    AppController controller;
    QVERIFY2(QDir(controller.store()->audioDir()).absolutePath().startsWith(QDir(root).absolutePath()),
             "az audio-mappa nem a sandboxban van");
    controller.store()->rebuildIndexFromDisk();     // a friss sandboxban még nincs index
    AppContext::instance()->setController(&controller);
    AppContext::instance()->setThemeMode(QStringLiteral("light"));

    QStringList warnings;
    QQmlEngine engine;
    setupEngine(engine);
    connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError>& list) {
        for (const QQmlError& e : list) warnings << e.toString();
    });
    QQmlComponent comp(&engine);
    comp.setData(kHost, QUrl(QStringLiteral("qrc:/qt/qml/Tanara/TranscriptSandboxProbe.qml")));
    std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow*>(comp.create()));
    QVERIFY2(window, qPrintable(comp.errorString()));
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    auto* vm = window->findChild<TranscriptEditorViewModel*>();
    auto* list = window->findChild<QQuickItem*>(QStringLiteral("transcriptList"));
    QVERIFY(vm && list);
    QVERIFY(!vm->demo());                   // van controller: nincs demó-adat
    QVERIFY(!vm->hasTranscript());

    auto frame = [&window] {
        QElapsedTimer t;
        t.start();
        QCoreApplication::processEvents();
        const QImage img = window->grabWindow();
        Q_UNUSED(img)
        return ms(t.nsecsElapsed());
    };

    int n = 0;
    for (const Meeting& entry : controller.store()->loadAll()) {
        ++n;
        const QString tag = QStringLiteral("meeting #%1").arg(n);
        QElapsedTimer t;
        t.start();
        window->setProperty("meeting", entry.id);       // → speakerEditor() + modell-építés
        const double openMs = ms(t.nsecsElapsed());
        const double firstFrameMs = frame();
        QCOMPARE(vm->meetingId(), entry.id);
        if (!vm->hasTranscript()) {
            qInfo().noquote() << tag << QStringLiteral("— nincs szerkeszthető átirat (régi formátum: %1); megnyitás %2 ms")
                                            .arg(vm->legacyTranscript() ? "igen" : "nem").arg(openMs, 0, 'f', 1);
            continue;
        }
        const int rows = vm->rows()->rowCount();
        qInfo().noquote() << tag << QStringLiteral("— %1 megszólalás, %2 beszélő (%3 oszlop + %4 összecsukva), %5 perc")
                                        .arg(rows).arg(vm->speakerCount()).arg(vm->lanes().size())
                                        .arg(vm->collapsedCount()).arg(vm->durationMs() / 60000);
        qInfo().noquote() << tag << QStringLiteral("  megnyitás (modell) %1 ms · első képkocka %2 ms")
                                        .arg(openMs, 0, 'f', 1).arg(firstFrameMs, 0, 'f', 1);

        // Szerkesztés a hang-elemzés KÖZBEN is megy: egy áthelyezés + visszavonás most.
        const bool embeddingAtOpen = vm->embeddingRunning();
        QSignalSpy resets(vm->rows(), &QAbstractItemModel::modelReset);
        const int mid = rows / 2;
        const int ownLane = vm->rows()->data(vm->rows()->index(mid), Role::LaneRole).toInt();
        const int otherLane = vm->lanes().size() > 1 ? (ownLane == 0 ? 1 : 0) : -1;
        QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, mid), Q_ARG(int, 1 /*Center*/));
        frame();
        if (otherLane >= 0) {
            vm->setRailVisible(true);
            const double railMs = frame();
            t.restart();
            QVERIFY(vm->moveRowToLane(mid, otherLane));
            const double moveMs = ms(t.nsecsElapsed());
            const double moveFrameMs = frame();
            t.restart();
            vm->undo();
            const double undoMs = ms(t.nsecsElapsed());
            const double undoFrameMs = frame();
            QCOMPARE(vm->rows()->data(vm->rows()->index(mid), Role::LaneRole).toInt(), ownLane);
            QCOMPARE(resets.count(), 0);
            qInfo().noquote() << tag << QStringLiteral("  egy sor áthelyezése %1 ms + képkocka %2 ms · visszavonás %3 ms + képkocka %4 ms "
                                                       "· sín bekapcsolása %5 ms%6")
                                            .arg(moveMs, 0, 'f', 1).arg(moveFrameMs, 0, 'f', 1).arg(undoMs, 0, 'f', 1)
                                            .arg(undoFrameMs, 0, 'f', 1).arg(railMs, 0, 'f', 1)
                                            .arg(embeddingAtOpen ? QStringLiteral(" (hang-elemzés közben)") : QString());
        }

        // Görgetés: 120 lépés, lépésenként 240 px, mindegyik után teljes képkocka.
        QMetaObject::invokeMethod(list, "positionViewAtBeginning");
        frame();
        QVector<double> scroll;
        for (int i = 0; i < 120; ++i) {
            const qreal maxY = list->property("originY").toReal()
                + qMax<qreal>(0, list->property("contentHeight").toReal() - list->height());
            list->setProperty("contentY", qMin(maxY, list->property("contentY").toReal() + 240));
            scroll.append(frame());
        }
        qInfo().noquote() << tag << QStringLiteral("  görgetés (120 × 240 px): %1").arg(stats(scroll));
        t.restart();
        QMetaObject::invokeMethod(list, "positionViewAtEnd");
        const double endMs = frame();
        QMetaObject::invokeMethod(list, "positionViewAtBeginning");
        const double beginMs = frame();
        qInfo().noquote() << tag << QStringLiteral("  ugrás a végére %1 ms · vissza az elejére %2 ms")
                                        .arg(endMs, 0, 'f', 1).arg(beginMs, 0, 'f', 1);

        // Keresés (gyakori betűre): találatok száma + idő.
        t.restart();
        vm->setSearchQuery(QStringLiteral("a"));
        const double searchMs = ms(t.nsecsElapsed());
        const double searchFrameMs = frame();
        const int matches = vm->searchMatchCount();
        vm->setSearchQuery(QString());
        frame();
        qInfo().noquote() << tag << QStringLiteral("  keresés: %1 találat, %2 ms + képkocka %3 ms")
                                        .arg(matches).arg(searchMs, 0, 'f', 1).arg(searchFrameMs, 0, 'f', 1);

        // Hang-elemzés: megvárjuk (cache-ből azonnal kész), közben a UI él.
        if (vm->voiceAvailable()) {
            QElapsedTimer voice;
            voice.start();
            QVERIFY2(QTest::qWaitFor([vm] { return !vm->embeddingRunning(); }, 300000), "a hang-elemzés nem ért véget");
            qInfo().noquote() << tag << QStringLiteral("  hang-elemzés: %1 mp (a megnyitás után), bizonytalan sorok: %2 (%3 %)%4")
                                            .arg(voice.elapsed() / 1000.0, 0, 'f', 1).arg(vm->uncertainCount())
                                            .arg(100.0 * vm->uncertainCount() / rows, 0, 'f', 1)
                                            .arg(vm->voiceNote().isEmpty() ? QString() : QStringLiteral(" — NEM érhető el"));
            // „Bizonytalan" szűrő be / ki.
            t.restart();
            vm->setUncertainOnly(true);
            const double filterMs = ms(t.nsecsElapsed());
            const double filterFrameMs = frame();
            const int filterRows = vm->rows()->rowCount();
            vm->setUncertainOnly(false);
            const double backFrameMs = frame();
            qInfo().noquote() << tag << QStringLiteral("  szűrő: %1 sor (elválasztókkal), be %2 ms + képkocka %3 ms · ki képkocka %4 ms")
                                            .arg(filterRows).arg(filterMs, 0, 'f', 1).arg(filterFrameMs, 0, 'f', 1)
                                            .arg(backFrameMs, 0, 'f', 1);
        } else {
            qInfo().noquote() << tag << "  hang-elemzés nem érhető el (nincs modell)";
        }
        vm->setRailVisible(false);
        QCOMPARE(resets.count(), 2 * (vm->voiceAvailable() ? 1 : 0));   // csak a szűrő be/ki resetel
    }
    QVERIFY(n > 0);
    window->setProperty("meeting", QString());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    window.reset();
    AppContext::instance()->setController(nullptr);
}

QTEST_MAIN(TestTranscriptSandbox)
#include "test_transcript_sandbox.moc"

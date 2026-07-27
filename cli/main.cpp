// tanara-cli — headless vezérlő a core fölött (NINCS Widgets).
//   devices                          eszközök listája
//   record --title T [--seconds N] [--device IDX]...   felvétel (alapból minden eszköz)
//   list                             meetingek
//   transcribe <meetingId>           átírás (Soniox kulcs kell)
//   summarize <meetingId>            összefoglaló (LM Studio)
//   detect [--watch] [--interval N]  aktív-hívás detektálás (smoke: a figyelő motorja)
//
#include "tanara/AppController.h"
#include "tanara/Localization.h"
#include "tanara/Logging.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceEmbedder.h"
#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/IMeetingDetector.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <QSocketNotifier>
#include <QDateTime>

#include <memory>

using namespace tanara;

static QTextStream out(stdout);
static QTextStream err(stderr);

static QString kindStr(TrackKind k) {
    switch (k) { case TrackKind::Mic: return "mic";
                 case TrackKind::Loopback: return "loopback";
                 default: return QCoreApplication::translate("cli", "egyéb"); }
}

static int cmdDevices(AppController& app) {
    app.refreshDevices();
    const auto devs = app.devices()->captureDevices();
    out << QCoreApplication::translate("cli", "Felvehető eszközök (%1):").arg(devs.size()) << "\n";
    for (int i = 0; i < devs.size(); ++i)
        out << "  [" << i << "] " << devs[i].name << "  (" << kindStr(devs[i].kind)
            << (devs[i].isDefault ? ", default" : "") << ")\n";
    out.flush();
    return 0;
}

static int cmdList(AppController& app) {
    const auto ms = app.store()->loadAll();
    out << QCoreApplication::translate("cli", "Meetingek (%1):").arg(ms.size()) << "\n";
    for (const auto& m : ms)
        out << "  " << m.id << "  " << m.startedAt.toString(Qt::ISODate)
            << "  \"" << m.title << "\""
            << (m.hasTranscript ? "  [transcript]" : "")
            << (m.hasSummary ? "  [summary]" : "") << "\n";
    out.flush();
    return 0;
}

int main(int argc, char** argv) {
    // Logolás MIELŐTT a QCoreApplication. A log-kapcsolókat (--debug, --log-level …)
    // kiszedjük az argv-ből, hogy a parancs-parser tiszta listát kapjon.
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i)
        rawArgs << QString::fromLocal8Bit(argv[i]);
    tanara::initLogging(tanara::parseLogOptions(rawArgs));

    QCoreApplication qapp(argc, argv);
    tanara::installAppTranslator();   // UI-nyelv a kimenetekhez (settings.json uiLanguage)
    const QStringList args = tanara::stripLogArgs(rawArgs);
    const QString cmd = args.value(1);

    AppController app;

    // Részletes induló-diagnosztika csak debugban (a normál parancs-kimenet maradjon tiszta).
    if (tanara::currentLogLevel() == tanara::LogLevel::Debug) {
        app.refreshDevices();
        tanara::logStartupDiagnostics(app);
    }

    if (cmd == "devices") return cmdDevices(app);
    if (cmd == "list")    return cmdList(app);

    if (cmd == "reindex") {
        // Az index (SQLite cache) teljes újraépítése a lemezen lévő meeting-mappákból.
        // Hasznos, ha kézzel másoltunk be felvétel-mappát (pl. másik gépről).
        app.store()->rebuildIndexFromDisk();
        const auto ms = app.store()->loadAll();
        out << QCoreApplication::translate("cli", "Index újraépítve a lemezről — %1 meeting.").arg(ms.size()) << "\n";
        out.flush();
        return 0;
    }

    if (cmd == "detect") {
        // Smoke: a figyelő MOTORJA külön folyamat/tray nélkül. Egyszeri poll, vagy
        // --watch esetén időzített (a settings intervallumával / --interval N mp).
        registerBuiltinDetectors();
        const AppSettings s = app.settings()->settings();
        std::unique_ptr<IMeetingDetector> det(
            s.detectorId.isEmpty()
                ? MeetingDetectorRegistry::instance().createBest()
                : MeetingDetectorRegistry::instance().create(s.detectorId));
        if (!det) {
            err << QCoreApplication::translate("cli", "Nincs elérhető meeting-detektor ezen a platformon (pw-dump?).") << "\n";
            err.flush();
            return 1;
        }
        det->configure(s.knownCallApps, QStringLiteral("tanara"));

        bool watch = false;
        int interval = s.detectorIntervalSec;
        for (int i = 2; i < args.size(); ++i) {
            if (args[i] == "--watch") watch = true;
            else if (args[i] == "--interval" && i + 1 < args.size()) interval = args[++i].toInt();
        }
        if (interval < 1) interval = 1;

        out << QCoreApplication::translate("cli", "Detektor: %1  (ismert appok: %2)")
                   .arg(det->id(), s.knownCallApps.join(QStringLiteral(", ")))
            << "\n";
        out.flush();

        auto pollOnce = [&det]() {
            const MeetingSignal sig = det->poll();
            if (sig.active)
                out << QCoreApplication::translate("cli",
                           "  ● MEETING: %1  [appId=%2, ablak=\"%3\", forrás=%4]")
                           .arg(sig.appName, sig.appId, sig.windowTitle, sig.sourceRef)
                    << "\n";
            else
                out << QCoreApplication::translate("cli", "  ○ nincs aktív hívás") << "\n";
            out.flush();
        };

        if (!watch) { pollOnce(); return 0; }

        pollOnce();
        out << QCoreApplication::translate("cli", "(figyelés %1 mp-enként — Ctrl-C a leállításhoz)").arg(interval) << "\n";
        out.flush();
        auto* timer = new QTimer(&qapp);
        QObject::connect(timer, &QTimer::timeout, &qapp, [&pollOnce]() { pollOnce(); });
        timer->start(interval * 1000);
        return qapp.exec();
    }

    if (cmd == "record") {
        QString title = QStringLiteral("Felvétel %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
        int seconds = 0;
        QList<int> deviceIdx;
        for (int i = 2; i < args.size(); ++i) {
            if (args[i] == "--title" && i + 1 < args.size()) title = args[++i];
            else if (args[i] == "--seconds" && i + 1 < args.size()) seconds = args[++i].toInt();
            else if (args[i] == "--device" && i + 1 < args.size()) deviceIdx << args[++i].toInt();
        }
        app.refreshDevices();
        const auto all = app.devices()->captureDevices();
        QVector<AudioDeviceInfo> sel;
        // --device nélkül: az auto-halmaz (line-in/AUX kihagyva). A --device IDX a teljes
        // 'devices' listára (captureDevices) indexel, így azzal a line-in is felvehető.
        if (deviceIdx.isEmpty()) sel = app.devices()->autoRecordDevices();
        else for (int idx : deviceIdx) if (idx >= 0 && idx < all.size()) sel << all[idx];

        if (sel.isEmpty()) { err << QCoreApplication::translate("cli", "Nincs kiválasztható eszköz.") << "\n"; return 1; }

        out << QCoreApplication::translate("cli", "Felvétel: \"%1\" — %2 sáv").arg(title).arg(sel.size()) << "\n";
        for (const auto& dvc : sel) out << "  • " << dvc.name << "\n";
        out.flush();

        QObject::connect(&app, &AppController::recordingFinished, &qapp, [&](Meeting m) {
            out << QCoreApplication::translate("cli", "KÉSZ. Mappa: %1").arg(m.folder) << "\n";
            for (const auto& t : m.tracks)
                out << QCoreApplication::translate("cli", "  sáv: %1  (%2)").arg(t.file, t.speakerLabel) << "\n";
            if (!m.mixdownFile.isEmpty()) out << "  mixdown: " << m.mixdownFile << "\n";
            out.flush();
            qapp.quit();
        });
        QObject::connect(&app, &AppController::errorOccurred, &qapp, [&](QString e) {
            err << QCoreApplication::translate("cli", "HIBA: %1").arg(e) << "\n"; err.flush(); qapp.exit(1);
        });
        QObject::connect(&app, &AppController::elapsedChanged, &qapp, [&](qint64 ms) {
            out << "\r  " << (ms / 1000) << " s..."; out.flush();
        });

        app.startRecording(title, sel);

        if (seconds > 0) {
            QTimer::singleShot(seconds * 1000, &app, [&] { out << "\n"; app.stopRecording(); });
        } else {
            out << QCoreApplication::translate("cli", "(Felvétel folyik — nyomj ENTER-t a leállításhoz)") << "\n"; out.flush();
            auto* sn = new QSocketNotifier(0, QSocketNotifier::Read, &qapp);
            QObject::connect(sn, &QSocketNotifier::activated, &qapp, [&, sn] {
                sn->setEnabled(false);
                char buf[256]; (void)!fgets(buf, sizeof buf, stdin);
                out << "\n"; app.stopRecording();
            });
        }
        return qapp.exec();
    }

    if (cmd == "transcribe" || cmd == "summarize") {
        const QString id = args.value(2);
        if (id.isEmpty()) { err << QCoreApplication::translate("cli", "Hiányzó meetingId.") << "\n"; return 1; }
        QObject::connect(&app, &AppController::transcriptReady, &qapp, [&](QString, QString p) {
            out << QCoreApplication::translate("cli", "Átirat kész: %1").arg(p) << "\n"; out.flush(); qapp.quit(); });
        QObject::connect(&app, &AppController::summaryReady, &qapp, [&](QString, QString p) {
            out << QCoreApplication::translate("cli", "Összefoglaló kész: %1").arg(p) << "\n"; out.flush(); qapp.quit(); });
        QObject::connect(&app, &AppController::errorOccurred, &qapp, [&](QString e) {
            err << QCoreApplication::translate("cli", "HIBA: %1").arg(e) << "\n"; err.flush(); qapp.exit(1); });
        if (cmd == "transcribe") app.transcribeMeeting(id); else app.summarizeMeeting(id);
        return qapp.exec();
    }

    if (cmd == "rename") {
        const QString id = args.value(2), raw = args.value(3), name = args.value(4);
        if (id.isEmpty() || raw.isEmpty()) {
            err << QCoreApplication::translate("cli", "Használat: rename <meetingId> <nyersCímke> <név>") << "\n"; return 1;
        }
        QObject::connect(&app, &AppController::errorOccurred, &qapp, [&](QString e) {
            err << QCoreApplication::translate("cli", "HIBA: %1").arg(e) << "\n"; err.flush();
        });
        app.renameSpeaker(id, raw, name);   // szinkron
        out << QCoreApplication::translate("cli", "Átnevezve: \"%1\" → \"%2\"").arg(raw, name) << "\n"; out.flush();
        return 0;
    }

    if (cmd == "identify") {
        const QString id = args.value(2);
        if (id.isEmpty()) { err << QCoreApplication::translate("cli", "Használat: identify <meetingId>") << "\n"; return 1; }
        app.autoIdentifyMeeting(id);   // szinkron (ffmpeg + onnx)
        const Meeting m = app.store()->load(id);
        out << QCoreApplication::translate("cli", "Auto-azonosítás kész. Leképezés (speakerMap):") << "\n";
        if (m.speakerMap.isEmpty())
            out << QCoreApplication::translate("cli", "  (üres — nincs küszöb feletti találat, vagy nincs modell/lenyomat)") << "\n";
        for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
            out << "  " << it.key() << " → " << it.value() << "\n";
        out.flush();
        return 0;
    }

    if (cmd == "participants") {
        const QString id = args.value(2);
        if (id.isEmpty()) { err << QCoreApplication::translate("cli", "Használat: participants <meetingId>") << "\n"; return 1; }
        out << QCoreApplication::translate("cli", "Résztvevők azonosítása (átírás előtt, lokálisan)…") << "\n"; out.flush();
        const auto guesses = app.identifyParticipants(id);
        if (guesses.isEmpty()) { out << QCoreApplication::translate("cli", "  (nincs találat — nincs modell/aktív sáv, vagy csend)") << "\n"; out.flush(); return 0; }
        for (const auto& g : guesses) {
            const QString who = g.name.isEmpty()
                ? QCoreApplication::translate("cli", "ISMERETLEN")
                : QStringLiteral("%1 (%2%)").arg(g.name).arg(int(g.score * 100 + 0.5));
            out << QCoreApplication::translate("cli", "  • %1  →  %2   [%3 ablak, minta: %4]")
                       .arg(g.deviceName, who).arg(g.windows).arg(g.sampleRef)
                << "\n";
        }
        out.flush();
        return 0;
    }

    if (cmd == "voiceprints") {
        auto* vp = app.voiceprints();
        out << QCoreApplication::translate("cli", "Hang-lenyomatok (%1 személy, %2 lenyomat):")
                   .arg(vp->people().size()).arg(vp->totalPrintCount())
            << "\n";
        for (const QString& name : vp->people())
            out << QCoreApplication::translate("cli", "  %1: %2 lenyomat").arg(name).arg(vp->printCount(name)) << "\n";
        out.flush();
        return 0;
    }

    if (cmd == "embed-probe") {
        // Diagnosztika: egy hangszegmens embeddingjének kiírása (voiceprint-hibakereséshez).
        //   embed-probe <modelPath> <audioPath> <startMs> <endMs> [scale] [cmn:0/1] [snip:0/1]
        const QString model = args.value(2), path = args.value(3);
        const qint64 s = args.value(4).toLongLong();
        const qint64 e = args.value(5).toLongLong();
        EmbedderConfig cfg;
        if (args.size() > 6) cfg.waveScale = args.value(6).toFloat();
        if (args.size() > 7) cfg.subtractMean = args.value(7).toInt() != 0;
        if (args.size() > 8) cfg.snipEdges = args.value(8).toInt() != 0;
        VoiceEmbedder emb(model, cfg);
        if (!emb.isValid()) { err << QCoreApplication::translate("cli", "HIBA: %1").arg(emb.lastError()) << "\n"; err.flush(); return 1; }
        const QVector<float> v = emb.embedFile(path, s, e);
        if (v.isEmpty()) { err << QCoreApplication::translate("cli", "HIBA: %1").arg(emb.lastError()) << "\n"; err.flush(); return 1; }
        QStringList parts; for (float x : v) parts << QString::number(x, 'g', 8);
        out << parts.join(QLatin1Char(' ')) << "\n"; out.flush();
        return 0;
    }

    out << "tanara-cli " << libraryVersion() << "\n"
        << QCoreApplication::translate("cli",
               "Parancsok: devices | record [--title T --seconds N --device IDX] | list | "
               "transcribe <id> | summarize <id> | rename <id> <nyersCímke> <név> | "
               "identify <id> | voiceprints")
        << "\n";
    out.flush();
    return 0;
}

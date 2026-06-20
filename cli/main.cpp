// tanara-cli — headless vezérlő a core fölött (NINCS Widgets).
//   devices                          eszközök listája
//   record --title T [--seconds N] [--device IDX]...   felvétel (alapból minden eszköz)
//   list                             meetingek
//   transcribe <meetingId>           átírás (Soniox kulcs kell)
//   summarize <meetingId>            összefoglaló (LM Studio)
//
#include "tanara/AppController.h"
#include "tanara/Logging.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceEmbedder.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <QSocketNotifier>
#include <QDateTime>

using namespace tanara;

static QTextStream out(stdout);
static QTextStream err(stderr);

static QString kindStr(TrackKind k) {
    switch (k) { case TrackKind::Mic: return "mic";
                 case TrackKind::Loopback: return "loopback";
                 default: return "egyéb"; }
}

static int cmdDevices(AppController& app) {
    app.refreshDevices();
    const auto devs = app.devices()->captureDevices();
    out << "Felvehető eszközök (" << devs.size() << "):\n";
    for (int i = 0; i < devs.size(); ++i)
        out << "  [" << i << "] " << devs[i].name << "  (" << kindStr(devs[i].kind)
            << (devs[i].isDefault ? ", default" : "") << ")\n";
    out.flush();
    return 0;
}

static int cmdList(AppController& app) {
    const auto ms = app.store()->loadAll();
    out << "Meetingek (" << ms.size() << "):\n";
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
        out << "Index újraépítve a lemezről — " << ms.size() << " meeting.\n";
        out.flush();
        return 0;
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

        if (sel.isEmpty()) { err << "Nincs kiválasztható eszköz.\n"; return 1; }

        out << "Felvétel: \"" << title << "\" — " << sel.size() << " sáv\n";
        for (const auto& dvc : sel) out << "  • " << dvc.name << "\n";
        out.flush();

        QObject::connect(&app, &AppController::recordingFinished, &qapp, [&](Meeting m) {
            out << "KÉSZ. Mappa: " << m.folder << "\n";
            for (const auto& t : m.tracks) out << "  sáv: " << t.file << "  (" << t.speakerLabel << ")\n";
            if (!m.mixdownFile.isEmpty()) out << "  mixdown: " << m.mixdownFile << "\n";
            out.flush();
            qapp.quit();
        });
        QObject::connect(&app, &AppController::errorOccurred, &qapp, [&](QString e) {
            err << "HIBA: " << e << "\n"; err.flush(); qapp.exit(1);
        });
        QObject::connect(&app, &AppController::elapsedChanged, &qapp, [&](qint64 ms) {
            out << "\r  " << (ms / 1000) << " s..."; out.flush();
        });

        app.startRecording(title, sel);

        if (seconds > 0) {
            QTimer::singleShot(seconds * 1000, &app, [&] { out << "\n"; app.stopRecording(); });
        } else {
            out << "(Felvétel folyik — nyomj ENTER-t a leállításhoz)\n"; out.flush();
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
        if (id.isEmpty()) { err << "Hiányzó meetingId.\n"; return 1; }
        QObject::connect(&app, &AppController::transcriptReady, &qapp, [&](QString, QString p) {
            out << "Átirat kész: " << p << "\n"; out.flush(); qapp.quit(); });
        QObject::connect(&app, &AppController::summaryReady, &qapp, [&](QString, QString p) {
            out << "Összefoglaló kész: " << p << "\n"; out.flush(); qapp.quit(); });
        QObject::connect(&app, &AppController::errorOccurred, &qapp, [&](QString e) {
            err << "HIBA: " << e << "\n"; err.flush(); qapp.exit(1); });
        if (cmd == "transcribe") app.transcribeMeeting(id); else app.summarizeMeeting(id);
        return qapp.exec();
    }

    if (cmd == "rename") {
        const QString id = args.value(2), raw = args.value(3), name = args.value(4);
        if (id.isEmpty() || raw.isEmpty()) {
            err << "Használat: rename <meetingId> <nyersCímke> <név>\n"; return 1;
        }
        QObject::connect(&app, &AppController::errorOccurred, &qapp, [&](QString e) {
            err << "HIBA: " << e << "\n"; err.flush();
        });
        app.renameSpeaker(id, raw, name);   // szinkron
        out << "Átnevezve: \"" << raw << "\" → \"" << name << "\"\n"; out.flush();
        return 0;
    }

    if (cmd == "identify") {
        const QString id = args.value(2);
        if (id.isEmpty()) { err << "Használat: identify <meetingId>\n"; return 1; }
        app.autoIdentifyMeeting(id);   // szinkron (ffmpeg + onnx)
        const Meeting m = app.store()->load(id);
        out << "Auto-azonosítás kész. Leképezés (speakerMap):\n";
        if (m.speakerMap.isEmpty())
            out << "  (üres — nincs küszöb feletti találat, vagy nincs modell/lenyomat)\n";
        for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
            out << "  " << it.key() << " → " << it.value() << "\n";
        out.flush();
        return 0;
    }

    if (cmd == "participants") {
        const QString id = args.value(2);
        if (id.isEmpty()) { err << "Használat: participants <meetingId>\n"; return 1; }
        out << "Résztvevők azonosítása (átírás előtt, lokálisan)…\n"; out.flush();
        const auto guesses = app.identifyParticipants(id);
        if (guesses.isEmpty()) { out << "  (nincs találat — nincs modell/aktív sáv, vagy csend)\n"; out.flush(); return 0; }
        for (const auto& g : guesses) {
            const QString who = g.name.isEmpty()
                ? QStringLiteral("ISMERETLEN")
                : QStringLiteral("%1 (%2%)").arg(g.name).arg(int(g.score * 100 + 0.5));
            out << "  • " << g.deviceName << "  →  " << who
                << "   [" << g.windows << " ablak, minta: " << g.sampleRef << "]\n";
        }
        out.flush();
        return 0;
    }

    if (cmd == "voiceprints") {
        auto* vp = app.voiceprints();
        out << "Hang-lenyomatok (" << vp->people().size() << " személy, "
            << vp->totalPrintCount() << " lenyomat):\n";
        for (const QString& name : vp->people())
            out << "  " << name << ": " << vp->printCount(name) << " lenyomat\n";
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
        if (!emb.isValid()) { err << "HIBA: " << emb.lastError() << "\n"; err.flush(); return 1; }
        const QVector<float> v = emb.embedFile(path, s, e);
        if (v.isEmpty()) { err << "HIBA: " << emb.lastError() << "\n"; err.flush(); return 1; }
        QStringList parts; for (float x : v) parts << QString::number(x, 'g', 8);
        out << parts.join(QLatin1Char(' ')) << "\n"; out.flush();
        return 0;
    }

    out << "tanara-cli " << libraryVersion() << "\n"
        << "Parancsok: devices | record [--title T --seconds N --device IDX] | list | "
           "transcribe <id> | summarize <id> | rename <id> <nyersCímke> <név> | "
           "identify <id> | voiceprints\n";
    out.flush();
    return 0;
}

// tanara-cli — headless vezérlő a core fölött (NINCS Widgets).
// A CLI kimenete ANGOL, fordítás nélkül (sima literálok, nincs tr / translate) — lásd cli/README.md.
//   devices                          eszközök listája
//   record --title T [--seconds N] [--device IDX]...   felvétel (alapból minden eszköz)
//   list                             meetingek
//   adopt <folder>                   felvétel-mappa behúzása (másik gépről, pendrive-ról)
//   reindex                          az index újraépítése a lemezen lévő mappákból
//   import <file>… [--title T] [--date ISO] [--split-channels] [--own-track N]
//                                    hangfájl(ok) importálása új meetingbe (fájlonként egy sáv)
//   transcribe <meetingId>           átírás (Soniox kulcs kell)
//   summarize <meetingId>            összefoglaló (LM Studio)
//   detect [--watch] [--interval N] [--app NAME]...
//                                    aktív-hívás detektálás (smoke: a figyelő motorja);
//                                    --app: további figyelt app erre a futásra (pl. ffmpeg)
//   cloud <subcommand>               Tanara Cloud (status, login, estimate …) — CloudCommands.cpp
//   transcribe|summarize <id> [--yes] [--complex]   cloud-módban előtte becslés + megerősítés
//   tags list [<meetingId>]          címkekészlet (darabszámmal) / egy meeting címkéi
//   tags add|remove <meetingId> <name>  címke fel / le
//   tags suggest <meetingId> [--llm] címkejavaslatok (hasonló megbeszélések; --llm: nyelvi modell)
//
#include "tanara/AppController.h"
#include "CloudCommands.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/Logging.h"
#include "tanara/SettingsManager.h"
#include "tanara/audio/DeviceManager.h"
#include "tanara/import/AudioImporter.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/tags/TagService.h"
#include "tanara/voiceid/VoiceEmbedder.h"
#include "tanara/detect/DetectorRegistry.h"
#include "tanara/detect/IMeetingDetector.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>
#include <QSocketNotifier>
#include <QDateTime>

#include <memory>

using namespace tanara;

static QTextStream out(stdout);
static QTextStream err(stderr);

// %n-es mondat (a CLI angol, nem fordít): egyes / többes szám.
static QString plural(qint64 n, const char* one, const char* many) {
    return QString::fromUtf8(n == 1 ? one : many).replace(QStringLiteral("%n"), QString::number(n));
}

static QString kindStr(TrackKind k) {
    switch (k) { case TrackKind::Mic: return "mic";
                 case TrackKind::Loopback: return "loopback";
                 default: return QStringLiteral("other"); }
}

static int cmdDevices(AppController& app) {
    app.refreshDevices();
    const auto devs = app.devices()->captureDevices();
    out << QStringLiteral("Recordable devices (%1):").arg(devs.size()) << "\n";
    for (int i = 0; i < devs.size(); ++i)
        out << "  [" << i << "] " << devs[i].name << "  (" << kindStr(devs[i].kind)
            << (devs[i].isDefault ? ", default" : "") << ")\n";
    out.flush();
    return 0;
}

static int cmdList(AppController& app) {
    const auto ms = app.store()->loadAll();
    out << QStringLiteral("Meetings (%1):").arg(ms.size()) << "\n";
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
    // A CLI kimenete MINDIG angol (a beállított UI-nyelvtől függetlenül). A saját szövegei
    // sima angol literálok; a core-ból jövő (magyar forrású) hibaüzenetekhez az angol
    // fordítás töltődik be.
    {
        auto* translator = new QTranslator(&qapp);
        if (translator->load(QStringLiteral(":/i18n/tanara_en")))
            QCoreApplication::installTranslator(translator);
    }
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

    if (cmd == "adopt") {
        // Egy felvétel-mappa behúzása (másik gépről, pendrive-ról): bemásolja a felvételek közé,
        // és CSAK ezt veszi fel az indexbe. Lásd MeetingStore::adoptMeetingFolder.
        if (args.size() < 3) { err << "Usage: adopt <folder>\n"; return 2; }
        QString error;
        const Meeting m = app.store()->adoptMeetingFolder(args.at(2), &error);
        if (m.id.isEmpty()) { err << error << "\n"; return 1; }
        out << QStringLiteral("Adopted: %1 — %2 (%3)\n%4\n")
                   .arg(m.title, m.id, plural(m.tracks.size(), "%n track", "%n tracks"), m.folder);
        out.flush();
        return 0;
    }

    if (cmd == "reindex") {
        // Az index (SQLite cache) teljes újraépítése a lemezen lévő meeting-mappákból.
        // Hasznos, ha kézzel másoltunk be felvétel-mappát (pl. másik gépről).
        app.store()->rebuildIndexFromDisk();
        const auto ms = app.store()->loadAll();
        out << QStringLiteral("Index rebuilt from disk — %1 meetings.").arg(ms.size()) << "\n";
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
            err << QStringLiteral("No meeting detector available on this platform (pw-dump?).") << "\n";
            err.flush();
            return 1;
        }
        bool watch = false;
        int interval = s.detectorIntervalSec;
        QStringList apps = s.knownCallApps;
        for (int i = 2; i < args.size(); ++i) {
            if (args[i] == "--watch") watch = true;
            else if (args[i] == "--interval" && i + 1 < args.size()) interval = args[++i].toInt();
            // Csak erre a futásra: a beállítások listája nem változik.
            else if (args[i] == "--app" && i + 1 < args.size()) apps << args[++i].trimmed().toLower();
        }
        if (interval < 1) interval = 1;
        det->configure(apps, QStringLiteral("tanara"));

        out << QStringLiteral("Detector: %1  (known apps: %2)")
                   .arg(det->id(), apps.join(QStringLiteral(", ")))
            << "\n";
        out.flush();

        auto pollOnce = [&det]() {
            const MeetingSignal sig = det->poll();
            if (sig.active)
                out << QStringLiteral("  ● MEETING: %1  [appId=%2, window=\"%3\", source=%4]")
                           .arg(sig.appName, sig.appId, sig.windowTitle, sig.sourceRef)
                    << "\n";
            else
                out << QStringLiteral("  ○ no active call") << "\n";
            out.flush();
        };

        if (!watch) { pollOnce(); return 0; }

        pollOnce();
        out << QStringLiteral("(checking every %1 s — Ctrl-C to stop)").arg(interval) << "\n";
        out.flush();
        auto* timer = new QTimer(&qapp);
        QObject::connect(timer, &QTimer::timeout, &qapp, [&pollOnce]() { pollOnce(); });
        timer->start(interval * 1000);
        return qapp.exec();
    }

    if (cmd == "record") {
        QString title = QStringLiteral("Recording %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
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

        if (sel.isEmpty()) { err << QStringLiteral("No selectable device.") << "\n"; return 1; }

        out << plural(sel.size(), "Recording: \"%1\" — %n track", "Recording: \"%1\" — %n tracks").arg(title) << "\n";
        for (const auto& dvc : sel) out << "  • " << dvc.name << "\n";
        out.flush();

        QObject::connect(&app, &AppController::recordingFinished, &qapp, [&](Meeting m) {
            out << QStringLiteral("DONE. Folder: %1").arg(m.folder) << "\n";
            for (const auto& t : m.tracks)
                out << QStringLiteral("  track: %1  (%2)").arg(t.file, t.speakerLabel) << "\n";
            if (!m.mixdownFile.isEmpty()) out << "  mixdown: " << m.mixdownFile << "\n";
            out.flush();
            qapp.quit();
        });
        QObject::connect(&app, &AppController::errorOccurred, &qapp, [&](QString e) {
            err << QStringLiteral("ERROR: %1").arg(e) << "\n"; err.flush(); qapp.exit(1);
        });
        QObject::connect(&app, &AppController::elapsedChanged, &qapp, [&](qint64 ms) {
            out << "\r  " << (ms / 1000) << " s..."; out.flush();
        });

        app.startRecording(title, sel);

        if (seconds > 0) {
            QTimer::singleShot(seconds * 1000, &app, [&] { out << "\n"; app.stopRecording(); });
        } else {
            out << QStringLiteral("(Recording in progress — press ENTER to stop)") << "\n"; out.flush();
            auto* sn = new QSocketNotifier(0, QSocketNotifier::Read, &qapp);
            QObject::connect(sn, &QSocketNotifier::activated, &qapp, [&, sn] {
                sn->setEnabled(false);
                char buf[256]; (void)!fgets(buf, sizeof buf, stdin);
                out << "\n"; app.stopRecording();
            });
        }
        return qapp.exec();
    }

    if (cmd == "import") {
        // Hangfájl(ok) → új meeting, fájlonként egy sáv (--split-channels: csatornánként egy).
        // A végén a meeting azonosítóját írja ki; átírás nem indul.
        ImportRequest req;
        bool split = false;
        int ownTrack = 0;   // 1-től számozva; 0 = nincs
        for (int i = 2; i < args.size(); ++i) {
            if (args[i] == "--title" && i + 1 < args.size()) req.title = args[++i];
            else if (args[i] == "--date" && i + 1 < args.size()) {
                const QString text = args[++i];
                req.startedAt = QDateTime::fromString(text, Qt::ISODate);
                if (!req.startedAt.isValid()) {
                    err << QStringLiteral("Invalid date: %1 (ISO format required, e.g. 2026-03-05T14:30)").arg(text) << "\n";
                    return 1;
                }
            }
            else if (args[i] == "--split-channels") split = true;
            else if (args[i] == "--own-track" && i + 1 < args.size()) ownTrack = args[++i].toInt();
            else if (args[i].startsWith("--")) {
                err << QStringLiteral("Unknown option: %1").arg(args[i]) << "\n"; return 1;
            }
            else req.sources.append({args[i], false});
        }
        if (req.sources.isEmpty()) {
            err << QStringLiteral("Usage: import <file>… [--title T] [--date ISO] [--split-channels] [--own-track N]") << "\n";
            return 1;
        }
        // Előzetes ellenőrzés: minden fájlban legyen hang (érthető hibaüzenettel).
        QVector<ImportFileInfo> infos;
        for (ImportSource& s : req.sources) {
            const ImportFileInfo info = audioimport::probe(s.path);
            if (!info.ok) { err << QStringLiteral("ERROR: %1").arg(info.error) << "\n"; err.flush(); return 1; }
            s.splitChannels = split && info.channels >= 2;
            infos.append(info);
        }
        const QVector<ImportPlannedTrack> plan = audioimport::planTracks(req.sources, infos);
        if (ownTrack < 0 || ownTrack > plan.size()) {
            err << QStringLiteral("--own-track must be between 1 and %1.").arg(plan.size()) << "\n";
            return 1;
        }
        req.ownTrack = ownTrack - 1;
        if (req.title.trimmed().isEmpty()) {
            QStringList paths;
            for (const ImportSource& s : req.sources) paths << s.path;
            req.title = audioimport::defaultTitle(paths);
        }
        out << plural(int(plan.size()), "Importing: \"%1\" — %n track", "Importing: \"%1\" — %n tracks").arg(req.title) << "\n";
        for (int i = 0; i < plan.size(); ++i)
            out << "  • " << plan[i].name
                << (i == req.ownTrack ? QStringLiteral("  (my microphone)") : QString()) << "\n";
        out.flush();

        // Ez a folyamat az importálás után kilép: a lekeverést nem indítjuk el (félbemaradna);
        // az elemző készíti el, amikor kell (az átírás a hiányzó keveréket előbb legyártja).
        app.setAutoMixdownAfterRecording(false);
        QObject::connect(app.importer(), &AudioImporter::progress, &qapp, [&](QString, int pct, int, int) {
            if (pct >= 0) { out << "\r  " << pct << "%"; out.flush(); }
        });
        QObject::connect(&app, &AppController::importFinished, &qapp, [&](Meeting m) {
            out << "\r" << QStringLiteral("DONE. Folder: %1").arg(m.folder) << "\n";
            for (const auto& t : m.tracks)
                out << QStringLiteral("  track: %1  (%2)").arg(t.file, t.deviceName) << "\n";
            out << QStringLiteral("Meeting: %1").arg(m.id) << "\n";
            out.flush();
            qapp.quit();
        });
        QObject::connect(&app, &AppController::importFailed, &qapp, [&](QString, QString message, QString detail) {
            err << "\n" << QStringLiteral("ERROR: %1").arg(message) << "\n";
            if (!detail.isEmpty()) err << "  " << detail << "\n";
            err.flush(); qapp.exit(1);
        });
        if (app.importAudio(req).isEmpty()) return 1;
        return qapp.exec();
    }

    if (cmd == "cloud") return runCloudCommand(app, args);

    if (cmd == "transcribe" || cmd == "summarize") {
        const QString id = args.value(2);
        if (id.isEmpty()) { err << QStringLiteral("Missing meetingId.") << "\n"; return 1; }
        const bool complex = cmd == "summarize" && args.contains(QStringLiteral("--complex"));
        const WorkflowStep step = cmd == "transcribe" ? WorkflowStep::Transcribe : WorkflowStep::Summarize;
        // Tanara Cloud: becslés (K-06) + megerősítés; --yes nélkül rákérdez.
        if (app.usesCloud(step)) {
            const ReadinessResult r = app.canRun(step, id);
            if (!r.runnable) { err << QStringLiteral("Cannot start: %1").arg(r.detail) << "\n"; return 1; }
            bool enough = false;
            if (!printCloudEstimate(app, id, cmd == "transcribe" ? QStringLiteral("transcribe") : QStringLiteral("summarize"),
                                    complex ? QStringLiteral("complex") : QStringLiteral("quick"), &enough))
                return 2;
            if (!enough) return 2;
            if (!args.contains(QStringLiteral("--yes"))) {
                out << QStringLiteral("Start? [y/N] "); out.flush();
                char buf[16] = {0};
                if (!fgets(buf, sizeof buf, stdin) || (buf[0] != 'y' && buf[0] != 'Y')) {
                    out << QStringLiteral("Cancelled — nothing was charged.") << "\n"; return 1;
                }
            }
        }
        const QString lang = QStringLiteral("en");
        QObject::connect(&app, &AppController::cloudCharged, &qapp,
                         [&](QString, QString kind, Money total, int calls, Money balance, QString vatMode) {
            const QString sum = formatMoney(total, MoneyStyle::Charge, lang), bal = formatMoney(balance, MoneyStyle::Balance, lang);
            if (kind == QLatin1String("transcribe"))
                out << QStringLiteral("This transcription cost %1. Balance: %2.").arg(sum, bal);
            else if (kind == QLatin1String("complex"))
                out << plural(calls, "This summary cost %1 (%n part). Balance: %2.", "This summary cost %1 (%n parts). Balance: %2.").arg(sum, bal);
            else if (kind == QLatin1String("topics"))
                out << QStringLiteral("Collecting the topics cost %1. Balance: %2.").arg(sum, bal);
            else
                out << QStringLiteral("This summary cost %1. Balance: %2.").arg(sum, bal);
            out << " (" << vatLabel(vatMode) << ")\n"; out.flush();
        });
        QObject::connect(&app, &AppController::cloudRefunded, &qapp, [&](QString, Money refund, Money, QString rid) {
            err << QStringLiteral("The transcription failed on the provider side. We refunded the %1 charge.")
                       .arg(formatMoney(refund, MoneyStyle::Charge, lang)) << "\n"
                << QStringLiteral("Error ID: %1").arg(rid) << "\n";
            err.flush(); qapp.exit(3); });
        QObject::connect(&app, &AppController::cloudError, &qapp, [&](QString, QString, CloudError e, Money charged) {
            err << describeCloudError(e, charged, lang) << "\n"; err.flush(); qapp.exit(2); });
        QObject::connect(&app, &AppController::transcriptReady, &qapp, [&](QString, QString p) {
            out << QStringLiteral("Transcript done: %1").arg(p) << "\n"; out.flush(); qapp.quit(); });
        QObject::connect(&app, &AppController::summaryReady, &qapp, [&](QString, QString p) {
            out << QStringLiteral("Summary done: %1").arg(p) << "\n"; out.flush(); qapp.quit(); });
        // Komplex: 1. kör (témák) → minden téma elemzése + reduce, szerkesztés nélkül.
        QObject::connect(&app, &AppController::topicsReady, &qapp, [&](QString mid, QVector<SummaryTopic> topics) {
            out << plural(int(topics.size()), "%n topic — starting the analysis…", "%n topics — starting the analysis…") << "\n"; out.flush();
            app.generateComplexSummary(mid, topics); });
        QObject::connect(&app, &AppController::errorOccurred, &qapp, [&](QString e) {
            err << QStringLiteral("ERROR: %1").arg(e) << "\n"; err.flush(); qapp.exit(1); });
        if (cmd == "transcribe") app.transcribeMeeting(id);
        else if (complex) app.extractMeetingTopics(id);
        else app.summarizeMeeting(id);
        return qapp.exec();
    }

    if (cmd == "tags") {
        TagService* tags = app.tags();
        const QString sub = args.value(2), id = args.value(3), name = args.value(4);
        auto usage = [&]() {
            err << QStringLiteral("Usage: tags list [<meetingId>] | tags add|remove <meetingId> <name> | tags suggest <meetingId> [--llm]") << "\n";
            return 1;
        };
        if (sub == "list") {
            if (id.isEmpty()) {
                const auto all = tags->all(TagService::Sort::MostUsed);
                out << QStringLiteral("Tags (%1):").arg(all.size()) << "\n";
                for (const TagUsage& u : all)
                    out << "  #" << u.tag.name << "  " << u.meetingCount << "  " << u.tag.id << "\n";
            } else {
                for (const QString& t : tags->tagsOf(id)) out << "  #" << tags->tag(t).name << "\n";
            }
            out.flush();
            return 0;
        }
        if ((sub == "add" || sub == "remove") && !id.isEmpty() && !name.isEmpty()) {
            if (sub == "add") {
                const Tag t = tags->addTag(id, name);
                if (!t.isValid()) { err << QStringLiteral("ERROR: unknown meeting or empty name.") << "\n"; return 1; }
                out << QStringLiteral("Added: #%1").arg(t.name) << "\n";
            } else {
                Tag t = tags->tag(name);
                if (!t.isValid()) t = tags->byName(name);
                if (!t.isValid()) { err << QStringLiteral("ERROR: no such tag.") << "\n"; return 1; }
                tags->removeTag(id, t.id);
                out << QStringLiteral("Removed: #%1").arg(t.name) << "\n";
            }
            out.flush();
            return 0;
        }
        if (sub == "suggest" && !id.isEmpty()) {
            const bool llm = args.contains(QStringLiteral("--llm"));
            QObject::connect(&app, &AppController::tagSuggestionsReady, &qapp,
                             [&](const QString& mid, const QVector<TagSuggestion>& list) {
                if (mid != id) return;
                out << QStringLiteral("Suggestions (%1):").arg(list.size()) << "\n";
                for (const TagSuggestion& sg : list) {
                    out << "  " << (sg.isNew ? "+NEW " : "+") << sg.name
                        << QStringLiteral("  (%1)").arg(sg.score, 0, 'f', 2);
                    for (const SuggestionReason& r : sg.reasons) out << "  · " << r.values.join(QStringLiteral(", "));
                    out << "\n";
                }
                out.flush();
                qapp.exit(0);
            });
            if (llm) app.requestLlmTagSuggestions(id);
            else app.requestTagSuggestions(id);
            QTimer::singleShot(llm ? 300000 : 60000, &qapp, [&]() {
                err << QStringLiteral("ERROR: no suggestions came back (no summary?).") << "\n";
                qapp.exit(1);
            });
            return qapp.exec();
        }
        return usage();
    }

    if (cmd == "rename") {
        const QString id = args.value(2), raw = args.value(3), name = args.value(4);
        if (id.isEmpty() || raw.isEmpty()) {
            err << QStringLiteral("Usage: rename <meetingId> <rawLabel> <name>") << "\n"; return 1;
        }
        QObject::connect(&app, &AppController::errorOccurred, &qapp, [&](QString e) {
            err << QStringLiteral("ERROR: %1").arg(e) << "\n"; err.flush();
        });
        app.renameSpeaker(id, raw, name);   // szinkron
        out << QStringLiteral("Renamed: \"%1\" → \"%2\"").arg(raw, name) << "\n"; out.flush();
        return 0;
    }

    if (cmd == "identify") {
        const QString id = args.value(2);
        if (id.isEmpty()) { err << QStringLiteral("Usage: identify <meetingId>") << "\n"; return 1; }
        app.autoIdentifyMeeting(id);   // szinkron (ffmpeg + onnx)
        const Meeting m = app.store()->load(id);
        out << QStringLiteral("Auto-identification done. Mapping (speakerMap):") << "\n";
        if (m.speakerMap.isEmpty())
            out << QStringLiteral("  (empty — no match above threshold, or no model/voiceprint)") << "\n";
        for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
            out << "  " << it.key() << " → " << it.value() << "\n";
        out.flush();
        return 0;
    }

    if (cmd == "participants") {
        const QString id = args.value(2);
        if (id.isEmpty()) { err << QStringLiteral("Usage: participants <meetingId>") << "\n"; return 1; }
        out << QStringLiteral("Identifying participants (before transcription, locally)…") << "\n"; out.flush();
        const auto guesses = app.identifyParticipants(id);
        if (guesses.isEmpty()) { out << QStringLiteral("  (no match — no model/active track, or silence)") << "\n"; out.flush(); return 0; }
        for (const auto& g : guesses) {
            const QString who = g.name.isEmpty()
                ? QStringLiteral("UNKNOWN")
                : QStringLiteral("%1 (%2%)").arg(g.name).arg(int(g.score * 100 + 0.5));
            out << plural(g.windows, "  • %1  →  %2   [%n window, sample: %3]", "  • %1  →  %2   [%n windows, sample: %3]")
                       .arg(g.deviceName, who, g.sampleRef)
                << "\n";
        }
        out.flush();
        return 0;
    }

    if (cmd == "voiceprints") {
        auto* vp = app.voiceprints();
        // Két számláló — a %n frázisonként csak egyszer szerepelhet, ezért két plural-frázis.
        out << QStringLiteral("Voiceprints (%1, %2):")
                   .arg(plural(vp->people().size(), "%n person", "%n people"),
                        plural(vp->totalPrintCount(), "%n voiceprint", "%n voiceprints"))
            << "\n";
        for (const QString& name : vp->people())
            out << QStringLiteral("  %1: %2").arg(name,
                       plural(vp->printCount(name), "%n voiceprint", "%n voiceprints"))
                << "\n";
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
        if (!emb.isValid()) { err << QStringLiteral("ERROR: %1").arg(emb.lastError()) << "\n"; err.flush(); return 1; }
        const QVector<float> v = emb.embedFile(path, s, e);
        if (v.isEmpty()) { err << QStringLiteral("ERROR: %1").arg(emb.lastError()) << "\n"; err.flush(); return 1; }
        QStringList parts; for (float x : v) parts << QString::number(x, 'g', 8);
        out << parts.join(QLatin1Char(' ')) << "\n"; out.flush();
        return 0;
    }

    out << "tanara-cli " << libraryVersion() << "\n"
        << "Commands: devices | record [--title T --seconds N --device IDX] | list | adopt <folder> | "
           "import <file>… [--title T --date ISO --split-channels --own-track N] | reindex | "
           "transcribe <id> [--yes] | summarize <id> [--yes --complex] | rename <id> <rawLabel> <name> | "
           "identify <id> | participants <id> | voiceprints | detect [--watch --interval N --app NAME] | "
           "tags <list|add|remove|suggest> … | cloud <status|login|estimate|…>"
        << "\n";
    out.flush();
    return 0;
}

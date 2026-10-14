// tanara-cli, recording module: devices, record, mixdown. All output is English, no tr().
#include "Commands.h"

#include "tanara/app/AppController.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/recording/audio/DeviceManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSocketNotifier>
#include <QTimer>

namespace tanara::cli {

namespace {

QString kindStr(TrackKind k)
{
    switch (k) {
    case TrackKind::Mic: return QStringLiteral("mic");
    case TrackKind::Loopback: return QStringLiteral("loopback");
    default: return QStringLiteral("other");
    }
}

int cmdDevices(CliContext& ctx, const QStringList&)
{
    app::AppController& app = ctx.app();
    app.refreshDevices();
    const auto devs = app.devices()->captureDevices();
    out() << QStringLiteral("Recordable devices (%1):").arg(devs.size()) << "\n";
    for (int i = 0; i < devs.size(); ++i)
        out() << "  [" << i << "] " << devs[i].name << "  (" << kindStr(devs[i].kind)
              << (devs[i].isDefault ? ", default" : "") << ")\n";
    out().flush();
    return 0;
}

// TODO(P12b): recording::RecordingService replaces the AppController recording calls.
int cmdRecord(CliContext& ctx, const QStringList& args)
{
    app::AppController& app = ctx.app();
    QCoreApplication& qapp = ctx.qapp();
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
    // Without --device: the auto set (line-in/AUX skipped). --device IDX indexes the full
    // 'devices' list (captureDevices), so a line-in can be recorded that way.
    if (deviceIdx.isEmpty()) sel = app.devices()->autoRecordDevices();
    else for (int idx : deviceIdx) if (idx >= 0 && idx < all.size()) sel << all[idx];

    if (sel.isEmpty()) { err() << QStringLiteral("No selectable device.") << "\n"; return 1; }

    out() << plural(sel.size(), "Recording: \"%1\" — %n track", "Recording: \"%1\" — %n tracks").arg(title) << "\n";
    for (const auto& dvc : sel) out() << "  • " << dvc.name << "\n";
    out().flush();

    QObject::connect(&app, &app::AppController::recordingFinished, &qapp, [&](Meeting m) {
        out() << QStringLiteral("DONE. Folder: %1").arg(m.folder) << "\n";
        for (const auto& t : m.tracks)
            out() << QStringLiteral("  track: %1  (%2)").arg(t.file, t.speakerLabel) << "\n";
        if (!m.mixdownFile.isEmpty()) out() << "  mixdown: " << m.mixdownFile << "\n";
        out().flush();
        qapp.quit();
    });
    QObject::connect(&app, &app::AppController::errorOccurred, &qapp, [&](QString e) {
        err() << QStringLiteral("ERROR: %1").arg(e) << "\n"; err().flush(); qapp.exit(1);
    });
    QObject::connect(&app, &app::AppController::elapsedChanged, &qapp, [&](qint64 ms) {
        out() << "\r  " << (ms / 1000) << " s..."; out().flush();
    });

    app.startRecording(title, sel);

    if (seconds > 0) {
        QTimer::singleShot(seconds * 1000, &app, [&] { out() << "\n"; app.stopRecording(); });
    } else {
        out() << QStringLiteral("(Recording in progress — press ENTER to stop)") << "\n"; out().flush();
        auto* sn = new QSocketNotifier(0, QSocketNotifier::Read, &qapp);
        QObject::connect(sn, &QSocketNotifier::activated, &qapp, [&, sn] {
            sn->setEnabled(false);
            char buf[256]; (void)!fgets(buf, sizeof buf, stdin);
            out() << "\n"; app.stopRecording();
        });
    }
    return qapp.exec();
}

int cmdMixdown(CliContext& ctx, const QStringList& args)
{
    const QString id = args.value(2);
    if (id.isEmpty()) { err() << "Usage: mixdown <id>\n"; return 2; }
    return regenerateMixdownAndWait(ctx, id, QStringLiteral("Mixdown regenerated."));
}

} // namespace

// TODO(P12b): recording::MixdownService replaces AppController::regenerateMixdown.
int regenerateMixdownAndWait(CliContext& ctx, const QString& meetingId, const QString& doneNote)
{
    app::AppController& app = ctx.app();
    QCoreApplication& qapp = ctx.qapp();
    const Meeting m = app.store()->load(meetingId);
    if (m.id.isEmpty()) { err() << "No such meeting: " << meetingId << "\n"; return 1; }
    bool anyActive = false;
    for (const Track& t : m.tracks) anyActive = anyActive || t.active;
    if (!anyActive) { err() << "The meeting has no active track to mix.\n"; return 1; }
    int rc = 1;
    QObject::connect(&app, &app::AppController::mixdownUpdated, &qapp, [&](const QString& mid, bool ok) {
        if (mid != meetingId) return;
        out() << (ok ? doneNote : QStringLiteral("Mixdown failed.")) << "\n";
        out().flush();
        rc = ok ? 0 : 1;
        qapp.quit();
    });
    app.regenerateMixdown(meetingId);
    qapp.exec();
    return rc;
}

void registerRecordingCommands(CommandRegistry& r)
{
    r.add({"devices", "recording", "List the recordable audio devices", "devices", true, cmdDevices});
    r.add({"record", "recording", "Record from the devices until ENTER (or --seconds)",
           "record [--title T] [--seconds N] [--device IDX]...", true, cmdRecord});
    r.add({"mixdown", "recording", "Regenerate the mixdown of a meeting from its active tracks",
           "mixdown <id>", true, cmdMixdown});
}

} // namespace tanara::cli

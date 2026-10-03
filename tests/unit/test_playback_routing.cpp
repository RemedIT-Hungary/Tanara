//
// Tanara — PlaybackRouting unit-tesztek: a `pw-dump` JSON-jából „melyik app melyik
// kimenetre szól”, és az eszköz-halmaz kulcsai (hot-plug jel). Tiszta parser, I/O nélkül.
//
#include <QtTest>

#include "tanara/audio/PlaybackRouting.h"

using namespace tanara;

namespace {

// Két kimenet, egy mikrofon; a Teams a headsetre, a Firefox a hangfalra szól (a Firefox
// streamje szüneteltetve). Egy stream csatornánként külön linket kap.
const char* kDump = R"JSON([
 {"id": 40, "type": "PipeWire:Interface:Node", "info": {"state": "running", "props": {
    "media.class": "Audio/Sink", "node.name": "alsa_output.usb-headset",
    "node.description": "Sennheiser headset - Kommunikáció"}}},
 {"id": 41, "type": "PipeWire:Interface:Node", "info": {"state": "idle", "props": {
    "media.class": "Audio/Sink", "node.name": "alsa_output.pci-speaker",
    "node.description": "Kanto YU4 - Optikai digitális"}}},
 {"id": 42, "type": "PipeWire:Interface:Node", "info": {"state": "running", "props": {
    "media.class": "Audio/Source", "node.name": "alsa_input.usb-mic",
    "node.description": "Trust USB mikrofon"}}},
 {"id": 60, "type": "PipeWire:Interface:Node", "info": {"state": "running", "props": {
    "media.class": "Stream/Output/Audio", "application.name": "WEBRTC VoiceEngine",
    "application.process.binary": "teams-for-linux"}}},
 {"id": 61, "type": "PipeWire:Interface:Node", "info": {"state": "idle", "props": {
    "media.class": "Stream/Output/Audio", "application.name": "Firefox",
    "application.process.binary": "firefox"}}},
 {"id": 62, "type": "PipeWire:Interface:Node", "info": {"state": "running", "props": {
    "media.class": "Stream/Input/Audio", "application.name": "tanara",
    "application.process.binary": "tanara"}}},
 {"id": 70, "type": "PipeWire:Interface:Link", "info": {"output-node-id": 60, "input-node-id": 40, "state": "active"}},
 {"id": 71, "type": "PipeWire:Interface:Link", "info": {"output-node-id": 60, "input-node-id": 40, "state": "active"}},
 {"id": 72, "type": "PipeWire:Interface:Link", "info": {"output-node-id": 61, "input-node-id": 41, "state": "paused"}},
 {"id": 73, "type": "PipeWire:Interface:Link", "info": {"output-node-id": 42, "input-node-id": 62, "state": "active"}}
])JSON";

} // namespace

class PlaybackRoutingTests : public QObject {
    Q_OBJECT
private slots:

    void parsesRoutesOncePerAppAndSink()
    {
        const AudioGraphSnapshot snap = detail::parsePwDumpGraph(kDump);
        QCOMPARE(snap.routes.size(), 2);   // a két csatorna-link egy útvonal
        bool teams = false, firefox = false;
        for (const PlaybackRoute& r : snap.routes) {
            if (r.appName == QStringLiteral("Microsoft Teams")) {
                teams = true;
                QCOMPARE(r.sinkName, QStringLiteral("alsa_output.usb-headset"));
                QVERIFY(r.running);
            }
            if (r.appName == QStringLiteral("Firefox")) {
                firefox = true;
                QVERIFY(!r.running);
            }
        }
        QVERIFY(teams && firefox);
    }

    // A miniaudio a kimenet monitorját „Monitor of <leírás>” néven adja.
    void appForOutputMatchesMonitorName()
    {
        const AudioGraphSnapshot snap = detail::parsePwDumpGraph(kDump);
        QCOMPARE(snap.appForOutput(QStringLiteral("Monitor of Sennheiser headset - Kommunikáció")),
                 QStringLiteral("Microsoft Teams"));
        QCOMPARE(snap.appForOutput(QStringLiteral("Monitor of Kanto YU4 - Optikai digitális")),
                 QStringLiteral("Firefox"));
        QVERIFY(snap.appForOutput(QStringLiteral("Trust USB mikrofon")).isEmpty());
        QVERIFY(snap.appForOutput(QString()).isEmpty());
    }

    void outputForAppFindsSink()
    {
        const AudioGraphSnapshot snap = detail::parsePwDumpGraph(kDump);
        QCOMPARE(snap.outputForApp(QStringLiteral("Microsoft Teams")),
                 QStringLiteral("Sennheiser headset - Kommunikáció"));
        QCOMPARE(snap.outputForApp(QStringLiteral("firefox")),
                 QStringLiteral("Kanto YU4 - Optikai digitális"));
        QVERIFY(snap.outputForApp(QStringLiteral("Zoom")).isEmpty());
    }

    // Az eszköz-kulcsok a kimenetek és bemenetek; a streamek nem számítanak eszköznek.
    void deviceKeysChangeOnHotplug()
    {
        const AudioGraphSnapshot a = detail::parsePwDumpGraph(kDump);
        QCOMPARE(a.deviceKeys.size(), 3);
        QByteArray unplugged(kDump);
        unplugged.replace("\"media.class\": \"Audio/Source\"", "\"media.class\": \"Gone\"");
        const AudioGraphSnapshot b = detail::parsePwDumpGraph(unplugged);
        QCOMPARE(b.deviceKeys.size(), 2);
        QVERIFY(a.deviceKeys != b.deviceKeys);
    }

    void garbageGivesEmptySnapshot()
    {
        QVERIFY(detail::parsePwDumpGraph("nem json").routes.isEmpty());
        QVERIFY(detail::parsePwDumpGraph("{}").deviceKeys.isEmpty());
    }
};

QTEST_GUILESS_MAIN(PlaybackRoutingTests)
#include "test_playback_routing.moc"
